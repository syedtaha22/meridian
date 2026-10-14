#!/usr/bin/env bash
# Plays the matches recorded under results/matches/.
#
# A configuration without iterative deepening cannot stop on a clock, so it plays to a
# fixed depth. Those matches compare techniques at equal depth. The matches with time
# limits pit configurations that manage their own clock against each other.
#
# Usage: scripts/run_matches.sh [FIRST_CPU] [THREADS]

set -euo pipefail

first_cpu="${1:-16}"
threads="${2:-16}"
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$repo/build"
out="$repo/results/matches"
mkdir -p "$out"

match() {
    local name="$1"
    shift
    "$build/play_match" "$@" --openings 100 --threads "$threads" --cpu-base "$first_cpu" \
        --seed 7 | tee "$out/$name.txt"
}

# What quiescence search buys, at equal nominal depth.
match quiescence-vs-tt-depth4 --a quiescence --b tt --time-a 0 --time-b 0 \
    --depth-a 4 --depth-b 4
# What iterative deepening and PVS buy against a fixed depth that takes a similar time.
match iterative-50ms-vs-quiescence-depth5 --a iterative --b quiescence --time-a 50 \
    --time-b 0 --depth-b 5
# What null move pruning, late move reductions, killers and check extensions buy.
match pruning-50ms-vs-iterative-50ms --a pruning --b iterative --time-a 50 --time-b 50
# The progression up to null move pruning and reductions, against the plain fixed-depth search.
match pruning-50ms-vs-baseline-depth4 --a pruning --b baseline --time-a 50 --time-b 0 --depth-b 4
