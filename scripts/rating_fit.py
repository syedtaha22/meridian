#!/usr/bin/env python3
"""
Fits ratings to the results of games between several players and prints them as a Markdown
table.

Usage: rating_fit.py GAMES.csv

The input is a CSV file with one row for each pair of players and the columns a, b, wins, draws
and losses, counted from the side of a. A player named stockfish-<elo> has a fixed rating, which
is the number in its name. The other ratings are found by maximum likelihood under the logistic
model, with a draw counted as half a point. The interval is 1.96 standard errors, from the
information matrix at the solution.
"""

import csv
import math
import sys


def expected(rating, opponent):
    """
    The expected score of a player against another on the logistic scale.
    """
    return 1 / (1 + 10 ** ((opponent - rating) / 400))


def solve(matrix, rhs):
    """
    Solves matrix * x = rhs by Gaussian elimination with partial pivoting.
    """
    n = len(rhs)
    matrix = [row[:] for row in matrix]
    rhs = rhs[:]

    for column in range(n):
        pivot = max(range(column, n), key=lambda row: abs(matrix[row][column]))
        matrix[column], matrix[pivot] = matrix[pivot], matrix[column]
        rhs[column], rhs[pivot] = rhs[pivot], rhs[column]

        for row in range(column + 1, n):
            factor = matrix[row][column] / matrix[column][column]

            for k in range(column, n):
                matrix[row][k] -= factor * matrix[column][k]

            rhs[row] -= factor * rhs[column]

    x = [0.0] * n

    for row in reversed(range(n)):
        total = rhs[row] - sum(matrix[row][k] * x[k] for k in range(row + 1, n))
        x[row] = total / matrix[row][row]

    return x


def main():
    if len(sys.argv) != 2:
        print("usage: rating_fit.py GAMES.csv", file=sys.stderr)
        return 1

    pairs = []
    names = []

    def player(name):
        if name not in names:
            names.append(name)

        return names.index(name)

    with open(sys.argv[1]) as handle:
        for row in csv.DictReader(handle):
            wins, draws, losses = (int(row[key]) for key in ("wins", "draws", "losses"))
            games = wins + draws + losses
            pairs.append(
                (player(row["a"]), player(row["b"]), games, (wins + 0.5 * draws) / games)
            )

    fixed = {i: float(n.split("-")[1]) for i, n in enumerate(names) if n.startswith("stockfish-")}
    free = [i for i in range(len(names)) if i not in fixed]
    slot = {player_index: k for k, player_index in enumerate(free)}
    rating = [fixed.get(i, 1000.0) for i in range(len(names))]
    scale = math.log(10) / 400

    def system():
        gradient = [0.0] * len(free)
        information = [[0.0] * len(free) for _ in free]

        for a, b, games, score in pairs:
            p = expected(rating[a], rating[b])
            g = games * (score - p) * scale
            h = games * p * (1 - p) * scale * scale

            if a in slot:
                gradient[slot[a]] += g
                information[slot[a]][slot[a]] += h

            if b in slot:
                gradient[slot[b]] -= g
                information[slot[b]][slot[b]] += h

            if a in slot and b in slot:
                information[slot[a]][slot[b]] -= h
                information[slot[b]][slot[a]] -= h

        return gradient, information

    for _ in range(100):
        gradient, information = system()
        step = solve(information, gradient)

        # A step is limited, so that a start far from the solution does not overshoot.
        step = [max(-200.0, min(200.0, s)) for s in step]

        for i in free:
            rating[i] += step[slot[i]]

        if max(abs(s) for s in step) < 1e-9:
            break

    _, information = system()
    rows = []

    for i in free:
        unit = [0.0] * len(free)
        unit[slot[i]] = 1.0
        variance = solve(information, unit)[slot[i]]
        margin = 1.96 * math.sqrt(variance)
        rows.append([names[i], f"{rating[i]:.0f}", f"{rating[i] - margin:.0f} to {rating[i] + margin:.0f}"])

    header = ["Player", "Rating", "95% interval"]
    widths = [max(len(r[i]) for r in [header] + rows) for i in range(3)]

    def line(cells):
        return (
            f"| {cells[0].ljust(widths[0])} | {cells[1].rjust(widths[1])} "
            f"| {cells[2].ljust(widths[2])} |"
        )

    print(line(header))
    print(f"|:{'-' * (widths[0] + 1)}|{'-' * (widths[1] + 1)}:|:{'-' * (widths[2] + 1)}|")

    for row in rows:
        print(line(row))

    return 0


if __name__ == "__main__":
    sys.exit(main())
