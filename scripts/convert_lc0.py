#!/usr/bin/env python3
"""
Converts Lc0 training tars into positions for train_nnue.

Usage: convert_lc0.py OUT.txt TAR [TAR ...] [--every 2] [--min-ply 6] [--limit-games N]

The tars hold games of Leela Chess Zero in the V7 format, one gzip file for each game and 8396
bytes for each position. The output has the lines `FEN<TAB>result<TAB>game<TAB>score` that
`gen_positions` writes: the result is the points of White, and the score is in centipawns for the
side to move. The data of Lc0 is published under the Open Database License (ODbL), see
https://lczero.org/dev/wiki/training-data/.

A record holds twelve bitboards, the pieces of the side to move (P N B R Q K) and then those of
the other side, with a1 as bit 0 and the side to move at the bottom, so that the board of a
record of a black move is flipped. The score is the mean of the root value and the best value of
the search (side to move, -1 to 1), turned into centipawns on the curve that train_nnue uses
(base 10 over 400 / 1.25). A position is dropped when
- it is among the first `--min-ply` plies, or is not one of every `--every` plies;
- a capture or a promotion follows, since the played move is not quiet;
- it has castling rights without the king and the rook on their squares (a game of Fischer
  random chess), which a standard position cannot hold;
- its score is beyond 2500 centipawns.
"""

import gzip
import math
import sys
import tarfile
from multiprocessing import Pool

import numpy as np

RECORD = 8396  # bytes of one position
PLANES_AT = 8 + 1858 * 4  # the twelve bitboards
CASTLING_AT = 8272  # four bytes: us queenside, us kingside, them queenside, them kingside
SIDE_AT = 8276  # 1 when Black is to move
HALFMOVE_AT = 8277
VALUES_AT = 8280  # 15 floats, the root value, the best value and the result among them
PIECES = "PNBRQK"
SHIFTS = np.arange(64, dtype=np.uint64)

# Castling flag: the squares of the king and of the rook, with their letters.
CASTLING = {
    "K": (4, "K", 7, "R"),
    "Q": (4, "K", 0, "R"),
    "k": (60, "k", 63, "r"),
    "q": (60, "k", 56, "r"),
}


def board_letters(row, black):
    """
    The piece letter of each square of a board, as seen by White.

    `row` holds the plane of the piece on each square, or -1. A plane below 6 is a piece of the
    side to move, which is Black when `black` is set.
    """
    letters = []

    for plane in row:
        if plane < 0:
            letters.append("")
            continue

        white = (plane < 6) != black
        letter = PIECES[plane % 6]
        letters.append(letter if white else letter.lower())

    return letters


def castling_field(record, letters, black):
    """
    The castling field of a FEN, or None when a right does not match the board.
    """
    us_queen, us_king, them_queen, them_king = record[CASTLING_AT : CASTLING_AT + 4]
    rights = {
        "K": them_king if black else us_king,
        "Q": them_queen if black else us_queen,
        "k": us_king if black else them_king,
        "q": us_queen if black else them_queen,
    }
    field = ""

    for flag in "KQkq":
        if not rights[flag]:
            continue

        king_square, king, rook_square, rook = CASTLING[flag]

        if letters[king_square] != king or letters[rook_square] != rook:
            return None

        field += flag

    return field or "-"


def placement(letters):
    """
    The piece placement field of a FEN.
    """
    ranks = []

    for rank in range(7, -1, -1):
        text = ""
        empty = 0

        for file in range(8):
            letter = letters[rank * 8 + file]

            if letter:
                text += (str(empty) if empty else "") + letter
                empty = 0
            else:
                empty += 1

        ranks.append(text + (str(empty) if empty else ""))

    return "/".join(ranks)


def decode_game(raw, game_id, every, min_ply):
    """
    The lines of one game and the number of its positions.
    """
    records = np.frombuffer(raw, dtype=np.uint8).reshape(-1, RECORD)
    count = len(records)

    if count < 2:
        return [], 0

    planes = np.ascontiguousarray(records[:, PLANES_AT : PLANES_AT + 96])
    planes = planes.view("<u8").reshape(count, 12)
    bits = ((planes[:, :, None] >> SHIFTS) & np.uint64(1)).astype(bool)  # (count, 12, 64)
    occupied = bits.any(axis=1)
    kind = np.where(occupied, bits.argmax(axis=1), -1).astype(np.int8)  # plane of each square
    side = records[:, SIDE_AT]

    # The board as seen by White: the ranks of a record where Black is to move are flipped.
    kind = kind.reshape(count, 8, 8)
    flip = side == 1
    kind[flip] = kind[flip][:, ::-1, :]
    kind = kind.reshape(count, 64)

    pieces = occupied.sum(axis=1)
    pawns = bits[:, 0, :].sum(axis=1) + bits[:, 6, :].sum(axis=1)

    values = np.ascontiguousarray(records[:, VALUES_AT : VALUES_AT + 60])
    values = values.view("<f4").reshape(count, 15)
    root_q, best_q, result_q = values[:, 0], values[:, 1], values[:, 7]
    lines = []

    for i in range(count - 1):
        if i < min_ply or i % every:
            continue

        if pieces[i + 1] < pieces[i] or pawns[i + 1] < pawns[i]:
            continue  # a capture or a promotion follows

        black = bool(side[i])
        letters = board_letters(kind[i], black)
        castling = castling_field(records[i], letters, black)

        if castling is None:
            continue

        q = 0.5 * (float(root_q[i]) + float(best_q[i]))
        p = min(max((q + 1.0) / 2.0, 0.0005), 0.9995)
        side_cp = 320.0 * math.log10(p / (1.0 - p))

        if abs(side_cp) > 2500:
            continue

        points = (float(result_q[i]) + 1.0) / 2.0
        white_result = (1.0 - points) if black else points
        white_cp = -side_cp if black else side_cp
        fen = "%s %s %s - %d 1" % (
            placement(letters),
            "b" if black else "w",
            castling,
            int(records[i, HALFMOVE_AT]),
        )
        lines.append("%s\t%g\t%d\t%d\n" % (fen, white_result, game_id, round(white_cp)))

    return lines, count


def convert_tar(job):
    """
    The lines, the number of positions and the number of games of one tar.
    """
    path, tar_index, every, min_ply, limit_games = job
    lines = []
    positions = 0
    games = 0

    try:
        with tarfile.open(path, "r|") as tar:
            for member in tar:
                if not member.isfile() or not member.name.endswith(".gz"):
                    continue

                try:
                    raw = gzip.decompress(tar.extractfile(member).read())
                except (EOFError, OSError):
                    break  # the end of a tar that is still downloading

                if len(raw) % RECORD:
                    continue

                game_id = tar_index * 100000 + games
                game_lines, count = decode_game(raw, game_id, every, min_ply)
                lines.extend(game_lines)
                positions += count
                games += 1

                if limit_games and games >= limit_games:
                    break
    except (tarfile.ReadError, EOFError):
        pass

    return lines, positions, games


def main():
    arguments = sys.argv[1:]
    options = {"--every": 2, "--min-ply": 6, "--limit-games": 0}

    for flag in options:
        if flag in arguments:
            i = arguments.index(flag)
            options[flag] = int(arguments[i + 1])
            del arguments[i : i + 2]

    if len(arguments) < 2:
        sys.exit(__doc__)

    out_path, tars = arguments[0], arguments[1:]
    jobs = [
        (tar, index, options["--every"], options["--min-ply"], options["--limit-games"])
        for index, tar in enumerate(tars)
    ]
    kept = 0
    total = 0
    games = 0

    # The game number of a position is the index of its tar and the place in the tar, so that a
    # fifth of the games can be held out by number whatever the order of the workers.
    with Pool(min(len(jobs), 24)) as pool, open(out_path, "w") as out:
        for lines, positions, count in pool.imap_unordered(convert_tar, jobs):
            out.writelines(lines)
            kept += len(lines)
            total += positions
            games += count

    print("%d games, %d positions read, %d kept" % (games, total, kept))


if __name__ == "__main__":
    main()
