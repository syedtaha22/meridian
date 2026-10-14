#!/usr/bin/env bash
# Builds the original desktop engine from git history, without its window and audio,
# and measures it on the benchmark suites.
#
# The original sources come from commit 9c2ae73, so the numbers describe the engine as the
# game runs it. `raylib.h` here stands in for the library with functions that do nothing;
# the search and move generation are untouched.
#
# Usage: run_baseline.sh OUTPUT_DIR [CPU] [MAX_SEARCH_DEPTH] [MAX_PERFT_DEPTH]

set -euo pipefail

out="${1:?output directory}"
cpu="${2:-8}"
max_search_depth="${3:-3}"
max_perft_depth="${4:-4}"
commit=9c2ae73

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

mkdir -p "$out"
git -C "$repo" archive "$commit" "Chess v5.0/src" "Chess v5.0/headers" | tar -x -C "$work"
src="$work/Chess v5.0"
# The original includes this header with a lower case name, which only Windows resolves.
cp "$src/headers/Functors/IsCaptureMove.h" "$src/headers/Functors/isCaptureMove.h"
cp "$here/legacy_adapter.cpp" "$src/legacy_adapter.cpp"

g++ -std=c++20 -O2 -I"$here" -I/usr/include/jsoncpp -o "$work/legacy_adapter" \
    "$src/legacy_adapter.cpp" "$src"/src/Engine/*.cpp "$src/src/Board/ChessBoard.cpp" \
    "$src/src/Board/BoardState.cpp" "$src/src/GameFlow/Flags.cpp" \
    "$src/src/GameFlow/Settings.cpp" "$src/src/Other/GlobalVariables.cpp" \
    "$src"/src/Pieces/*.cpp "$src/src/Functors/ConvertNotation.cpp" \
    "$src/src/Exceptions/KingNotFound.cpp" -ljsoncpp

run() { taskset -c "$cpu" "$work/legacy_adapter" "$@"; }

"$repo/bench/environment.sh" "$cpu" >"$out/environment.json"

# Move selection, one row per depth and position.
columns="engine,config,depth,region,position,move,score_cp,nodes,quiescence_nodes,ms,nps"
columns="$columns,tt_hits,beta_cutoffs,first_move_cutoffs"
echo "$columns" >"$out/move_selection.csv"
for depth in $(seq 1 "$max_search_depth"); do
    run search "$repo/bench/positions/suite.txt" "$depth" 2>/dev/null | grep '^legacy' |
        awk -F'\t' 'BEGIN{OFS=","} {nps = ($7 > 0) ? $6 / $7 * 1000 : 0;
            print "legacy", "original", $2, $3, $4, $5, "", $6, "", $7, nps, "", "", ""}' \
            >>"$out/move_selection.csv"
done

# Perft correctness and speed. Counts come from perft.txt.
{
    echo "{"
    echo "  \"engine\": \"original\","
    echo "  \"perft\": ["
    first=1
    while IFS=$'\t' read -r name fen counts; do
        case "$name" in '#'*|'') continue ;; esac
        IFS=',' read -ra expected <<<"$counts"
        for depth in $(seq 1 "$max_perft_depth"); do
            want="${expected[$((depth - 1))]}"
            # A depth that would take the original engine hours is skipped.
            if [ "$want" -gt 5000000 ]; then continue; fi
            result="$(run perft "$fen" "$depth" 2>/dev/null | grep '^perft' | tail -1)"
            nodes="$(cut -f2 <<<"$result")"
            seconds="$(cut -f3 <<<"$result")"
            correct=false
            [ "$nodes" = "$want" ] && correct=true
            [ "$first" = 1 ] || echo ","
            first=0
            printf '    {"name": "%s", "depth": %s, "nodes": %s, "expected": %s,' \
                "$name" "$depth" "$nodes" "$want"
            printf ' "correct": %s, "seconds": %s}' "$correct" "$seconds"
        done
    done <"$repo/bench/positions/perft.txt"
    echo
    echo "  ]"
    echo "}"
} >"$out/rules.json"

echo "wrote $out"
