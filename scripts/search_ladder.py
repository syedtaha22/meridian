#!/usr/bin/env python3
"""
Tests a list of changes to the search one after the other.

Usage: search_ladder.py LIST.txt [--base full] [--elo1 10] [--openings 1500] [--threads 12]

Each line of LIST.txt is `label|change|options`. The change is what is added to the base after
a `+`, such as `02-delta|delta_pruning|`. A change that starts with `!` is tested and reported
but not added to the base, and an empty change tests the base against itself with the options
(for example `--extra "--hash-a 64 --hash-b 16"`). The options are passed to
scripts/experiment.sh. A change is kept, and the base grows by it, when it misses no mate
puzzle that the base solves and either the sequential test accepts H1, or it reaches no decision with a score of at
least 0.5 and clearly fewer nodes at equal depth. The results are appended to
results/matches/search-experiments/ladder.csv, and the final base is printed.
"""

import argparse
import re
import shlex
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "results" / "matches" / "search-experiments"


def run(label, new, base, args, options):
    command = [
        str(ROOT / "scripts" / args.script), label, new, base,
        "--elo1", str(args.elo1), "--openings", str(args.openings),
        "--threads", str(args.threads), "--time", str(args.time),
    ] + options
    if args.eval:
        command += ["--eval", args.eval]
    if args.puzzle_plies:
        command += ["--puzzle-plies", str(args.puzzle_plies)]
    subprocess.run(command, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return (OUT / f"{label}.txt").read_text()


def base_missed_of(args, base):
    """
    The number of mate puzzles that the base itself misses.
    """
    command = [str(ROOT / "build" / "mate_check"), "--config", base, "--data", "tests/data",
               "--extra-plies", str(args.puzzle_plies)]
    if args.eval:
        command += ["--eval", args.eval]
    text = subprocess.run(command, capture_output=True, text=True, cwd=ROOT).stdout
    found = re.search(r": (\d+) missed", text)
    return int(found.group(1)) if found else 0


def parse(text, new, base):
    missed = int(re.search(r": (\d+) missed", text).group(1)) if re.search(r": (\d+) missed", text) else -1
    verdict = "no decision"
    if "H1 accepted" in text:
        verdict = "H1"
    elif "H0 accepted" in text:
        verdict = "H0"
    score = re.search(r"score ([\d.]+)", text)
    games = re.search(r"games (\d+):", text)
    llr = re.search(r"LLR (-?[\d.]+)", text)

    # Mean nodes per move over the rows of the deepest depth shown, for each configuration.
    nodes = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 6 and parts[0] in (new, base) and parts[1].isdigit():
            nodes.setdefault(parts[0], {}).setdefault(int(parts[1]), []).append(float(parts[4]))
    ratio = None
    if new in nodes and base in nodes:
        depth = max(nodes[new])
        mean = lambda v: sum(v) / len(v)
        ratio = mean(nodes[new][depth]) / mean(nodes[base][depth])
    return {
        "missed": missed, "verdict": verdict,
        "score": float(score.group(1)) if score else 0.0,
        "games": int(games.group(1)) if games else 0,
        "llr": float(llr.group(1)) if llr else 0.0,
        "nodes_ratio": ratio,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("list")
    parser.add_argument("--base", default="full")
    parser.add_argument("--elo1", type=float, default=10)
    parser.add_argument("--openings", type=int, default=1500)
    parser.add_argument("--threads", type=int, default=12)
    parser.add_argument("--time", type=int, default=50)
    parser.add_argument("--script", default="experiment.sh")
    parser.add_argument("--eval", default="", help="evaluation numbers file for both sides")
    parser.add_argument("--puzzle-plies", type=int, default=0,
                        help="plies added to the depth of the mate puzzle search")
    args = parser.parse_args()

    base = args.base
    base_missed = None
    OUT.mkdir(parents=True, exist_ok=True)
    table = OUT / "ladder.csv"
    if not table.exists():
        table.write_text("label,new,base,missed,verdict,games,score,llr,nodes_ratio,kept\n")

    for line in Path(args.list).read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        label, change, options = (line.split("|") + ["", ""])[:3]
        keep_allowed = not change.startswith("!")
        change = change.lstrip("!").replace(" ", "+")
        new = base + ("+" + change if change else "")
        text = run(label, new, base, args, shlex.split(options))
        r = parse(text, new, base)
        # The puzzles that the base misses are not counted against a change.
        if base_missed is None:
            base_missed = base_missed_of(args, base)
        keep = keep_allowed and 0 <= r["missed"] <= base_missed and (
            r["verdict"] == "H1"
            or (r["verdict"] == "no decision" and r["score"] >= 0.5
                and r["nodes_ratio"] is not None and r["nodes_ratio"] <= 0.95))
        with table.open("a") as handle:
            handle.write(f"{label},{new},{base},{r['missed']},{r['verdict']},{r['games']},"
                         f"{r['score']},{r['llr']},{r['nodes_ratio']},{int(keep)}\n")
        print(f"{label}: {new} vs {base}: {r['verdict']}, score {r['score']} in {r['games']} "
              f"games, missed {r['missed']}, nodes {r['nodes_ratio']} -> "
              f"{'kept' if keep else 'dropped'}", flush=True)
        if keep and change:
            base = new
            base_missed = r["missed"]

    print("final base:", base)


if __name__ == "__main__":
    sys.exit(main())
