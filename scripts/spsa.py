#!/usr/bin/env python3
"""
Tunes the numeric parameters of the search by simultaneous perturbation (SPSA).

Usage: spsa.py --base full+see+futility [--iterations 120] [--openings 24] [--time 20]
                [--threads 12] [--out tuned.txt]

The parameters tuned are those of the techniques that appear in the base. Each iteration plays
a short match between the base with every parameter moved up and the base with every parameter
moved down, by a random sign for each parameter, and moves the parameters towards the side
that won. The last line printed is the base with the tuned values, as a preset name.
"""

import argparse
import os
import random
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# technique -> parameters: name, start, perturbation, minimum, maximum
PARAMETERS = {
    "reverse_futility": [("rfp_margin", 107, 15, 40, 300)],
    "futility": [("futility_base", 74, 15, 20, 400), ("futility_step", 214, 20, 30, 400)],
    "razoring": [("razor_base", 800, 60, 300, 1500), ("razor_step", 300, 40, 50, 600)],
    "late_move_pruning": [("lmp_base", 3, 1, 1, 8)],
    "lmr_table": [("lmr_scale", 225, 20, 120, 400)],
    "null_margin": [("null_margin_step", 246, 30, 60, 500)],
    "aspiration": [("aspiration_delta", 18, 6, 8, 80)],
    "delta_pruning": [("delta_margin", 200, 30, 50, 500)],
    "iir": [("iir_depth", 6, 1, 3, 10)],
    "singular": [("singular_depth", 8, 1, 5, 12)],
}


# The techniques that the presets `tuned` and `network` have on. The start values above are the
# defaults of `SearchOptions`, and a base can name another value with `+name=value`.
DEFAULT_ON = {"reverse_futility", "futility", "null_margin", "aspiration"}


def parameters_of(base):
    """
    The parameters to tune for the techniques in the base, with their start values.
    """
    changes = base.split("+")[1:]
    flags = {c.split("=")[0] for c in changes if not c.endswith("=0")}

    if base.split("+")[0] in ("tuned", "network"):
        flags |= DEFAULT_ON - {c.split("=")[0] for c in changes if c.endswith("=0")}
    explicit = {c.split("=")[0]: int(c.split("=")[1]) for c in changes if "=" in c}
    found = []

    for flag, parameters in PARAMETERS.items():
        if flag in flags:
            for name, start, step, low, high in parameters:
                found.append((name, explicit.get(name, start), step, low, high))

    return found


def match(first, second, args, seed):
    """
    The score of `first` against `second` in a short match.
    """
    binary = Path(os.environ.get("CHESS_BUILD", ROOT / "build")) / "play_match"
    command = [
        str(binary), "--a", first, "--b", second, "--time-a", str(args.time), "--time-b",
        str(args.time), "--openings", str(args.openings), "--threads", str(args.threads),
        "--seed", str(seed),
    ]
    if args.eval:
        command += ["--eval-a", args.eval, "--eval-b", args.eval]
    text = subprocess.run(command, capture_output=True, text=True, cwd=ROOT).stdout
    found = re.search(r"score ([\d.]+)", text)
    return float(found.group(1)) if found else 0.5


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", required=True)
    parser.add_argument("--iterations", type=int, default=120)
    parser.add_argument("--openings", type=int, default=24)
    parser.add_argument("--time", type=int, default=20)
    parser.add_argument("--threads", type=int, default=12)
    parser.add_argument("--out", default=None)
    parser.add_argument("--eval", default="", help="evaluation numbers file for both sides")
    args = parser.parse_args()

    parameters = parameters_of(args.base)

    if not parameters:
        print("nothing to tune")
        print(args.base)
        return 0

    random.seed(7)
    theta = {name: float(start) for name, start, _, _, _ in parameters}
    base_flags = [c for c in args.base.split("+") if "=" not in c or c.split("=")[0] not in theta]
    prefix = "+".join(base_flags)

    def preset(values):
        return prefix + "".join(f"+{name}={round(value)}" for name, value in values.items())

    for k in range(1, args.iterations + 1):
        shrink = k ** 0.101
        signs = {name: random.choice((-1, 1)) for name in theta}
        plus = {}
        minus = {}

        for name, _, step, low, high in parameters:
            c = max(1.0, step / shrink)
            plus[name] = min(high, max(low, theta[name] + c * signs[name]))
            minus[name] = min(high, max(low, theta[name] - c * signs[name]))

        score = match(preset(plus), preset(minus), args, 1000 + k)
        gain = (score - 0.5) * 6 / (1 + k / 30) ** 0.602

        for name, _, step, low, high in parameters:
            c = max(1.0, step / shrink)
            theta[name] = min(high, max(low, theta[name] + gain * c * signs[name]))

        if k % 10 == 0:
            print(k, {n: round(v, 1) for n, v in theta.items()}, flush=True)

    tuned = preset(theta)

    if args.out:
        Path(args.out).write_text(tuned + "\n")

    print(tuned)
    return 0


if __name__ == "__main__":
    sys.exit(main())
