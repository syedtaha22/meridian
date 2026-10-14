#!/usr/bin/env bash
# Builds the engine and records one named baseline under results/baselines/.
#
# Each program is pinned to one CPU. `bench_search` interleaves the configurations
# over rounds, so drift in clock speed or temperature affects all of them equally.
# A run writes environment.json, rules.json, eval.json and move_selection.csv.
#
# Usage: scripts/benchmark.sh RUN_ID CONFIG [options]
#   RUN_ID    folder name under results/baselines/
#   CONFIG    search preset: baseline, ordering, tt, quiescence, iterative, pruning, full, tuned
#   --rays    build with ray-walking slider attacks instead of magic bitboards
#   --cpu N   CPU to pin to (default 8)
#   --depths  comma separated search depths (default 1,2,3,4,5,6; to 10 for the last two)
#   --rounds  rounds of the search benchmark (default 3)

set -euo pipefail

run_id="${1:?run id}"
config="${2:?search preset}"
shift 2

rays=OFF
cpu=8
rounds=3
depths=""
while [ $# -gt 0 ]; do
    case "$1" in
        --rays) rays=ON ;;
        --cpu) cpu="$2"; shift ;;
        --depths) depths="$2"; shift ;;
        --rounds) rounds="$2"; shift ;;
        *) echo "unknown option $1" >&2; exit 1 ;;
    esac
    shift
done

if [ -z "$depths" ]; then
    case "$config" in
        iterative|pruning|full|tuned) depths="1,2,3,4,5,6,7,8,9,10" ;;
        *) depths="1,2,3,4,5,6" ;;
    esac
fi

backend="$(cd "$(dirname "$0")/.." && pwd)"
build="$backend/build-bench-$rays"
out="$backend/results/baselines/$run_id"
mkdir -p "$out"

cmake -S "$backend" -B "$build" -DCMAKE_BUILD_TYPE=Release -DCHESS_RAY_ATTACKS="$rays" >/dev/null
cmake --build "$build" -j "$(nproc)" --target bench_rules bench_eval bench_search >/dev/null

cd "$backend"
"$backend/bench/environment.sh" "$cpu" >"$out/environment.json"
"$build/bench_rules" --cpu "$cpu" --out "$out/rules.json"
"$build/bench_eval" --cpu "$cpu" --out "$out/eval.json"
"$build/bench_search" --configs "$config" --depths "$depths" --cpu "$cpu" --rounds "$rounds" \
    --out "$out/move_selection.csv"
echo "wrote $out"
