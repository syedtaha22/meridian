#!/usr/bin/env python3
"""
Prints one table of the recorded results as Markdown.

Usage: report.py TABLE [--results DIR]

TABLE is one of:
    rules      perft speed, move generation, evaluation and correctness of every step
    time       mean milliseconds per move by depth, in tables of five depths
    nodes      mean nodes per move by depth, in tables of five depths
    speedup    time of each step relative to the step before it
    phases     mean milliseconds per move at depth 3 by class of position
    matches    the matches between two configurations
    FILE       a match against a UCI engine, given as a file name in results/matches

The tool reads results/baselines/*/{rules.json,eval.json,move_selection.csv} and
results/matches/*.txt. It prints the table and nothing else.
"""

import argparse
import csv
import json
import re
import statistics
import sys
from pathlib import Path


def number(value, decimals):
    """
    Formats a value with a fixed number of decimals.
    """
    return f"{value:.{decimals}f}"


def grouped(value):
    """
    Formats a value as an integer with a comma between groups of three digits.
    """
    return f"{round(value):,}"


def milliseconds(value):
    """
    Formats a time in milliseconds with enough digits to compare steps.
    """
    if value >= 1000:
        return grouped(value)

    return number(value, 2 if value < 10 else 1)


def print_table(header, rows):
    """
    Prints a Markdown table. The first column is left aligned and the others are right
    aligned. Every column is as wide as its widest cell.
    """
    widths = [max(len(str(row[i])) for row in [header] + rows) for i in range(len(header))]

    def line(cells):
        parts = []

        for i, cell in enumerate(cells):
            cell = str(cell)
            parts.append(cell.ljust(widths[i]) if i == 0 else cell.rjust(widths[i]))

        return "| " + " | ".join(parts) + " |"

    separator = ["-" * (w + 1) for w in widths]
    separator = [":" + separator[0]] + [s + ":" for s in separator[1:]]

    print(line(header))
    print("|" + "|".join(separator) + "|")

    for row in rows:
        print(line(row))


def read_runs(results):
    """
    Reads every run under results/baselines, in the order of the step numbers.
    """
    runs = []

    for folder in sorted((results / "baselines").iterdir()):
        if not folder.is_dir():
            continue

        run = {"name": folder.name, "search": [], "rules": None, "eval_ns": None}
        search = folder / "move_selection.csv"

        if search.exists():
            with search.open() as handle:
                for row in csv.DictReader(handle):
                    run["search"].append(
                        {
                            "depth": int(row["depth"]),
                            "region": row["region"],
                            "ms": float(row["ms"]),
                            "nodes": float(row["nodes"]),
                        }
                    )

        rules = folder / "rules.json"

        if rules.exists():
            run["rules"] = json.loads(rules.read_text())

        evaluation = folder / "eval.json"

        if evaluation.exists():
            run["eval_ns"] = json.loads(evaluation.read_text())["evaluate_ns"]["median"]

        runs.append(run)

    return runs


def mean_over(rows, depth, field):
    """
    The mean of a field at a depth over all positions, or None when there is no row.
    """
    values = [row[field] for row in rows if row["depth"] == depth]

    return sum(values) / len(values) if values else None


def rules_table(runs):
    header = ["Step", "Perft Mnps", "Movegen ns", "make_move ns", "Eval ns", "Correct"]
    rows = []

    for run in runs:
        rules = run["rules"]

        if rules is None:
            continue

        speeds = []
        correct = 0

        for entry in rules["perft"]:
            correct += 1 if entry["correct"] else 0

            if "nps" in entry:
                speeds.append(entry["nps"]["median"])
            elif entry.get("seconds"):
                speeds.append(entry["nodes"] / entry["seconds"])

        movegen = rules.get("legal_movegen_ns_per_position", {}).get("median")
        make = rules.get("copy_make_ns_per_move", {}).get("median")
        rows.append(
            [
                run["name"],
                number(statistics.median(speeds) / 1e6, 2),
                number(movegen, 0) if movegen else "n/a",
                number(make, 1) if make else "n/a",
                number(run["eval_ns"], 0) if run["eval_ns"] else "n/a",
                f"{correct} of {len(rules['perft'])}",
            ]
        )

    print_table(header, rows)


def search_tables(runs, field):
    runs = [run for run in runs if run["search"]]
    depths = sorted({row["depth"] for run in runs for row in run["search"]})
    format_value = grouped if field == "nodes" else milliseconds

    for first in range(0, len(depths), 5):
        chunk = depths[first : first + 5]
        rows = []

        for run in runs:
            values = [mean_over(run["search"], depth, field) for depth in chunk]

            if any(value is not None for value in values):
                rows.append(
                    [run["name"]]
                    + ["-" if v is None else format_value(v) for v in values]
                )

        if first > 0:
            print()

        print_table(["Step"] + [f"d{depth}" for depth in chunk], rows)


def speedup_table(runs):
    runs = [run for run in runs if run["search"]]
    rows = []

    for before, run in zip(runs, runs[1:]):
        cells = [run["name"], before["name"]]

        for depth in (3, 4, 5, 6):
            a = mean_over(before["search"], depth, "ms")
            b = mean_over(run["search"], depth, "ms")

            if a and b:
                ratio = a / b
                cells.append((grouped(ratio) if ratio >= 1000 else number(ratio, 2)) + "x")
            else:
                cells.append("-")

        rows.append(cells)

    print_table(["Step", "Over", "d3", "d4", "d5", "d6"], rows)


def phases_table(runs):
    rows = []

    for run in runs:
        if not run["search"]:
            continue

        cells = [run["name"]]

        for region in ("opening", "middlegame", "endgame"):
            values = [
                row["ms"]
                for row in run["search"]
                if row["depth"] == 3 and row["region"] == region
            ]
            cells.append(milliseconds(sum(values) / len(values)) if values else "-")

        rows.append(cells)

    print_table(["Step", "Opening", "Middlegame", "Endgame"], rows)


GAME_LINE = re.compile(
    r"games (\d+): \+(\d+) =(\d+) -(\d+)\s+score ([\d.]+)\s+\(95% interval ([\d.]+) to ([\d.]+)\)"
    r"\s+Elo ([+-]?\d+)"
)
LEVEL_LINE = re.compile(
    r"^(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+([\d.]+)\s+(?:\d+/\d+\s+)?(-?\d+) "
    r"\((-?\d+) to (-?\d+)\)"
)
ESTIMATE_LINE = re.compile(r"Estimated rating: (-?\d+) \(95% interval (-?\d+) to (-?\d+)\)")


def matches_table(results):
    rows = []

    for path in sorted((results / "matches").glob("*.txt")):
        lines = path.read_text().splitlines()

        for line in lines[1:]:
            found = GAME_LINE.match(line)

            if found:
                games, wins, draws, losses, score, low, high, elo = found.groups()
                rows.append([lines[0], wins, draws, losses, score, f"{low} to {high}", elo])

    print_table(["Match", "Wins", "Draws", "Losses", "Score", "95% interval", "Elo"], rows)


def level_table(path):
    rows = []
    total = [0, 0, 0, 0]
    estimate = None

    for line in path.read_text().splitlines():
        level = LEVEL_LINE.match(line)
        found = ESTIMATE_LINE.match(line)

        if level:
            elo, games, wins, draws, losses, score, rating, low, high = level.groups()
            rows.append([elo, games, wins, draws, losses, score, f"{rating} ({low} to {high})"])

            for i, value in enumerate((games, wins, draws, losses)):
                total[i] += int(value)
        elif found:
            estimate = found.groups()

    if estimate is not None and total[0] > 0:
        games, wins, draws, losses = total
        score = (wins + 0.5 * draws) / games
        rating, low, high = estimate
        rows.append(
            [
                "All",
                games,
                wins,
                draws,
                losses,
                number(score, 3),
                f"{rating} ({low} to {high})",
            ]
        )

    print_table(["UCI_Elo", "Games", "Wins", "Draws", "Losses", "Score", "Rating"], rows)


def main():
    parser = argparse.ArgumentParser(description="Prints one table of the recorded results.")
    parser.add_argument("table")
    parser.add_argument(
        "--results", type=Path, default=Path(__file__).resolve().parent.parent / "results"
    )
    args = parser.parse_args()

    tables = {
        "rules": lambda: rules_table(read_runs(args.results)),
        "time": lambda: search_tables(read_runs(args.results), "ms"),
        "nodes": lambda: search_tables(read_runs(args.results), "nodes"),
        "speedup": lambda: speedup_table(read_runs(args.results)),
        "phases": lambda: phases_table(read_runs(args.results)),
        "matches": lambda: matches_table(args.results),
    }

    if args.table in tables:
        tables[args.table]()
        return 0

    path = args.results / "matches" / args.table

    if not path.exists():
        path = args.results / "matches" / (args.table + ".txt")

    if not path.exists():
        print(f"unknown table {args.table}", file=sys.stderr)
        return 1

    level_table(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
