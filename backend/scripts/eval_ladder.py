#!/usr/bin/env python3
"""
Tunes the evaluation in stages and tests each stage with a match.

Usage: eval_ladder.py DATA [--threads 12] [--openings 1500] [--time 50] [--base FILE]
                          [--stages "NAME=groups;NAME=groups;..."]

Each stage starts from the numbers that the last accepted stage left, runs `texel_tune` on the
groups of the stage with the positions in DATA, and plays the new numbers against the old with
`full` at equal time (a sequential test, 0 against +10 Elo). A stage is kept when it misses no
mate puzzle that the numbers before it solve, and the test accepts H1 or reaches no decision with
a score of at least 0.5. `--start FILE` begins from the numbers of a file. The
accepted numbers are played against the evaluation of the engine at the end. Files and match
reports go to results/eval-tuning, and the rows to results/eval-tuning/ladder.csv.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
OUT = ROOT / "results" / "eval-tuning"

STAGES = [
    ("A1-tables", "material,pst,king"),
    ("A2a-tempo-bishop-pair", "tempo,bishop_pair"),
    ("A2b-passed-pawns", "passed"),
    ("A2c-pawn-structure", "doubled,isolated"),
    ("A2d-rook-files", "rook_open,rook_half"),
    ("A2e-mobility", "mobility"),
]


def run(command, log=None):
    result = subprocess.run([str(c) for c in command], cwd=ROOT, capture_output=True, text=True)
    text = result.stdout + result.stderr
    if log is not None:
        Path(log).write_text(text)
    return text


def parse(text):
    verdict = "no decision"
    if "H1 accepted" in text:
        verdict = "H1"
    elif "H0 accepted" in text:
        verdict = "H0"
    score = re.search(r"score ([\d.]+)", text)
    games = re.search(r"games (\d+):", text)
    return verdict, float(score.group(1)) if score else 0.0, int(games.group(1)) if games else 0


def match(args, label, new, old):
    command = [BUILD / "play_match", "--a", "full", "--b", "full", "--time-a", args.time,
               "--time-b", args.time, "--openings", args.openings, "--threads", args.threads,
               "--sprt", "--elo0", 0, "--elo1", 10]
    if new is not None:
        command += ["--eval-a", new]
    if old is not None:
        command += ["--eval-b", old]
    return parse(run(command, OUT / f"{label}.txt"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("data")
    parser.add_argument("--threads", type=int, default=12)
    parser.add_argument("--tune-threads", type=int, default=24)
    parser.add_argument("--openings", type=int, default=1500)
    parser.add_argument("--time", type=int, default=50)
    parser.add_argument("--epochs", type=int, default=40)
    parser.add_argument("--stages", default="")
    parser.add_argument("--start", default="", help="numbers file to start from")
    parser.add_argument("--against", default="",
                        help="numbers file that the first stage is played against")
    parser.add_argument("--final-label", default="final-against-engine")
    args = parser.parse_args()

    stages = STAGES
    if args.stages:
        # Stages are separated by `;`, so that the groups of a stage can be a list with commas.
        stages = []
        for item in args.stages.split(";"):
            if not item:
                continue
            if "=" not in item:
                parser.error(f"stage {item!r} is not NAME=groups")
            label, groups = item.split("=", 1)
            stages.append((label, groups))

    OUT.mkdir(parents=True, exist_ok=True)
    table = OUT / "ladder.csv"
    if not table.exists():
        table.write_text("stage,groups,missed,verdict,games,score,kept\n")

    if args.start:
        base = Path(args.start)
    else:
        base = OUT / "start.params"
        run([BUILD / "texel_tune", "--data", args.data, "--limit", 1000, "--epochs", 0,
             "--out", base], OUT / "start.txt")

    def missed_of(params):
        text = run([BUILD / "mate_check", "--config", "full", "--eval", params,
                    "--data", "tests/data"])
        found = re.search(r": (\d+) missed", text)
        return int(found.group(1)) if found else -1

    # The numbers a stage is played against; the tuner starts from `base`.
    reference = Path(args.against) if args.against else base
    base_missed = missed_of(reference)

    for label, groups in stages:
        out = OUT / f"{label}.params"
        run([BUILD / "texel_tune", "--data", args.data, "--init", base, "--groups", groups,
             "--out", out, "--threads", args.tune_threads, "--epochs", args.epochs],
            OUT / f"{label}-tune.txt")
        missed = missed_of(out)
        verdict, score, games = match(args, label, out, reference)
        keep = 0 <= missed <= base_missed and (
            verdict == "H1" or (verdict == "no decision" and score >= 0.5))
        with table.open("a") as handle:
            handle.write(f"{label},{groups.replace(',', ' ')},{missed},{verdict},{games},"
                         f"{score},{int(keep)}\n")
        print(f"{label}: {verdict}, score {score} in {games} games, missed {missed} -> "
              f"{'kept' if keep else 'dropped'}", flush=True)
        if keep:
            base = out
            reference = out
            base_missed = missed

    verdict, score, games = match(args, args.final_label, base, None)
    print(f"final {base.name} against the evaluation of the engine: {verdict}, score {score} "
          f"in {games} games", flush=True)


if __name__ == "__main__":
    sys.exit(main())
