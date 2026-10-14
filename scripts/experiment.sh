#!/usr/bin/env bash
# Tests one change to the search: the mate puzzles, the nodes at a fixed depth, and a match with
# a sequential test against a base configuration.
#
# Usage: scripts/experiment.sh LABEL NEW BASE [--elo0 0] [--elo1 10] [--openings 1500]
#                                              [--time 50] [--threads 12] [--depths 5,7]
#                                              [--extra "--hash-a 64 --hash-b 16"]
#                                              [--eval FILE] [--puzzle-plies N]
#
# NEW and BASE are preset names, with changes such as `full+see`. The output is written to
# results/matches/search-experiments/LABEL.txt and to the terminal.

set -euo pipefail

if [ $# -lt 3 ]; then
    sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
fi

label="$1"
new="$2"
base="$3"
shift 3

elo0=0
elo1=10
openings=1500
time_ms=50
threads=12
depths=5,7
extra=""
eval_file=""
puzzle_plies=0

while [ $# -gt 0 ]; do
    case "$1" in
        --elo0) elo0="$2"; shift ;;
        --elo1) elo1="$2"; shift ;;
        --openings) openings="$2"; shift ;;
        --time) time_ms="$2"; shift ;;
        --threads) threads="$2"; shift ;;
        --depths) depths="$2"; shift ;;
        --extra) extra="$2"; shift ;;
        --eval) eval_file="$2"; shift ;;
        --puzzle-plies) puzzle_plies="$2"; shift ;;
        *) echo "unknown option $1" >&2; exit 1 ;;
    esac
    shift
done

repo="$(cd "$(dirname "$0")/.." && pwd)"
build="${CHESS_BUILD:-$repo/build}"
out="$repo/results/matches/search-experiments"
mkdir -p "$out"
cd "$repo"

# The evaluation numbers of a file apply to both sides, to the puzzles and to the node counts.
eval_check=()
eval_bench=()
eval_match=()
if [ -n "$eval_file" ]; then
    eval_check=(--eval "$eval_file")
    eval_bench=(--eval "$eval_file")
    eval_match=(--eval-a "$eval_file" --eval-b "$eval_file")
fi

{
    echo "# $label: $new against $base"
    echo
    echo "## Mate puzzles"
    "$build/mate_check" --config "$new" --data tests/data --extra-plies "$puzzle_plies" \
        "${eval_check[@]}" ||
        echo "MISSED PUZZLES"
    echo
    echo "## Nodes at a fixed depth (mean over the 15 suite positions)"
    "$build/bench_search" --configs "$base,$new" --depths "$depths" --rounds 1 \
        --out /dev/null "${eval_bench[@]}" 2>/dev/null
    echo
    echo "## Match at ${time_ms}ms per move"
    "$build/play_match" --a "$new" --b "$base" --time-a "$time_ms" --time-b "$time_ms" \
        --openings "$openings" --threads "$threads" --sprt --elo0 "$elo0" --elo1 "$elo1" "${eval_match[@]}" $extra
} 2>&1 | tee "$out/$label.txt"
