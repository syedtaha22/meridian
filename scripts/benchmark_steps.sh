#!/usr/bin/env bash
# Measures the search presets in one process, rounds interleaved, and files each preset's
# rows under the baseline folder of the step that introduced it.
#
# Timings on a shared machine drift from one run to the next, so ratios between steps are
# only trustworthy when the steps run in one process and round. Node counts do not
# drift. This script writes the search rows of steps 02 to 09; benchmark.sh records the
# rules, evaluation and environment of each step.
#
# Usage: scripts/benchmark_steps.sh [--cpu N] [--rounds N]

set -euo pipefail

cpu=8
rounds=5
while [ $# -gt 0 ]; do
    case "$1" in
        --cpu) cpu="$2"; shift ;;
        --rounds) rounds="$2"; shift ;;
        *) echo "unknown option $1" >&2; exit 1 ;;
    esac
    shift
done

repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$repo/build-bench-OFF"
cmake -S "$repo" -B "$build" -DCMAKE_BUILD_TYPE=Release -DCHESS_RAY_ATTACKS=OFF >/dev/null
cmake --build "$build" -j "$(nproc)" --target bench_search >/dev/null

cd "$repo"
shallow="$(mktemp)"
deep="$(mktemp)"
trap 'rm -f "$shallow" "$deep"' EXIT
"$build/bench_search" --configs baseline,ordering,tt,quiescence,iterative,pruning,full,tuned \
    --depths 1,2,3,4,5,6 --cpu "$cpu" --rounds "$rounds" --out "$shallow" >/dev/null
"$build/bench_search" --configs iterative,pruning,full,tuned --depths 7,8,9,10 --cpu "$cpu" \
    --rounds "$rounds" --out "$deep" >/dev/null

header="$(head -1 "$shallow")"
file_rows() {
    local config="$1"
    local run="$2"
    local out="$repo/results/baselines/$run/move_selection.csv"
    mkdir -p "$(dirname "$out")"
    {
        echo "$header"
        awk -F, -v c="$config" 'NR > 1 && $2 == c' "$shallow"
        awk -F, -v c="$config" 'NR > 1 && $2 == c' "$deep"
    } >"$out"
    echo "wrote $out"
}
file_rows baseline 02-magic-bitboards
file_rows ordering 03-move-ordering
file_rows tt 04-transposition-table
file_rows quiescence 05-quiescence
file_rows iterative 06-iterative-deepening-pvs
file_rows pruning 07-pruning-and-reductions
file_rows full 08-search-refinements
file_rows tuned 09-tuned-evaluation
