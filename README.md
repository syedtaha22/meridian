<div align="center">

  <a href="https://syedtaha22.github.io/meridian/">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="assets/logo-dark.svg">
      <source media="(prefers-color-scheme: light)" srcset="assets/logo.svg">
      <img src="assets/logo.svg" alt="Meridian" width="200">
    </picture>
  </a>

  A UCI chess engine in C++20.
  <br>
  <strong>[Explore Meridian docs »][docs-link]</strong>
  <br>
  <br>
  [Report bug][issue-link]
  ·
  [Open a discussion][discussions-link]
  ·
  [Blog][blog-link]

  [![Build][build-badge]][build-link]
  [![License][license-badge]][license-link]
  <br>
  [![Release][release-badge]][release-link]
  [![Commits][commits-badge]][commits-link]
  <br>
  [![Website][website-badge]][website-link]

</div>

Meridian is a chess engine written in C++. It is the rewrite of
[Horizon](https://github.com/syedtaha22/Chess-v5.0), a chess engine with a desktop interface that
was written for a university course project in 2024.

## Architecture

The engine takes a position and returns a move. A position is a FEN string plus the moves played
since. The engine also returns the score of the move. The caller can set a depth limit and a time
limit.

### Board and moves

The engine stores the board as bitboards. A bitboard is a 64-bit number with one bit for each
square. The engine keeps one bitboard for each piece type and color.

Rooks and bishops attack along lines that other pieces can block. The engine finds these attacks
with magic bitboards. One multiplication and one table read give the attacked squares. This is 20
to 30 times faster than walking along each line.

To make a move, the engine copies the position and changes the copy. A Zobrist hash identifies
each position, and every move updates it.

### Search

The engine searches the tree of moves with negamax alpha-beta. It searches in passes. The first
pass has depth 1, and each pass is one ply deeper than the one before. Each pass uses the result
of the previous pass to order the moves, and a move is ready when the time runs out.

At each position, the engine searches the first move with a full window. It searches the other
moves with a window of zero width. If one of them turns out to be better, the engine searches it
again with a full window. This method is called principal variation search.

The engine uses these techniques to search fewer positions:

- **Transposition table.** It stores results by position hash and gives the best move found
  earlier.
- **Null move pruning.** The engine skips its own move. If the position is still good enough, it
  stops searching that position.
- **Late move reductions.** Quiet moves that come late in the order are searched to a smaller
  depth, by a logarithmic table of the depth and of the number of moves tried.
- **Reverse futility and futility pruning.** Near the leaves, the engine stops a position whose
  static score is far above beta, and it skips quiet moves that cannot reach alpha.
- **Probcut.** If a good capture still beats beta by a margin after a shallower search, the
  engine stops searching that position.
- **Correction history.** The engine learns the error that the search finds in the static score of
  a pawn structure, and adds it to the next static score of that structure.
- **Aspiration windows.** Each pass starts in a window around the score of the pass before it.
- **Check extension.** The engine searches one ply deeper when the side to move is in check.
- **Quiescence search.** At the end of the search, the engine continues with captures. This
  prevents it from stopping in the middle of an exchange. A static exchange evaluation removes
  the captures that lose material.

The order of moves matters for pruning. The engine tries the hash move first. Then it tries
captures that do not lose material, ordered by the value of the captured piece and the value of
the capturing piece. Then it tries the killer moves and the counter move of the move before. Then
it tries the other moves in the order of the history heuristic and of a continuation score.
Captures that lose material come last. A history entry decays in proportion to its size, so that
recent cutoffs count more than old ones.

### Evaluation

The engine evaluates a position with a neural network. The network takes the 768 combinations of
a piece and a square, once for each half of the board that holds the king of the side that looks
(the king is mirrored to the files a to d first), and gives 128 values for each side. One of eight
output layers, chosen by the number of pieces on the board, turns them into a score. The values of
a position follow the search: a move changes a few rows of the weights, and a king that changes
half or mirror makes its side compute its values again. An evaluation costs one pass over 256
integers. The network is compiled into the library, and the option `EvalFile` loads another one.

The network is trained on 38.7 million positions: 16.9 million from games of Leela Chess Zero and
21.8 million from games of the engine against itself.

The preset `tuned` uses a hand written evaluation instead. It scores a position from material and
from the placement of the pieces. Each piece has a value, and a piece-square table adds a bonus or
a penalty for the square the piece stands on.
Four more terms refine the score: the bishop pair, a rook on an open or half-open file, the
number of squares that a knight, bishop, rook or queen can reach, and the right to move.

Every number has a middlegame value and an endgame value, and the score blends the two by the
material left on the board. The numbers were fitted to the results of 695,000 positions from
games of the engine against itself, by a method that minimises the error between the score and
the result of the game (Texel tuning). A term entered the engine only after a match of the
engine with the term against the engine without it.

### Opening book

The engine plays its first moves from an opening book, so that games do not repeat. The book is
built by the engine itself: a tool searches the opening tree to depth 12 for both colours,
keeps the moves that score close to the best, and writes them into the library. A move is chosen
at random among the book moves of a position, weighted by score. The book has 39,586 moves for
15,584 positions, and it scores the same as the engine without it (0.496 over 400 games).

### Verification

The tests check the engine in three ways:

- Move generation gives the published perft counts for six reference positions.
- The search solves 1,404 puzzles that ask for mate in one, two or three moves.
- Scripted UCI sessions check the handshake, the options, every kind of limit, a stop, pondering,
  the book, the tablebases and bad input, and the time budget stays inside the clock.
- The engine runs under AddressSanitizer and UBSan.

## Performance

The engine improves one technique at a time. A technique stays only if the benchmarks show that
it helps. This section shows those benchmarks.

The benchmark uses fifteen fixed positions: five openings, five middlegames and five endgames.
For each position, the engine searches to a given depth, starting with an empty hash table. The
benchmark records the time and the number of nodes. Each configuration of the search adds one
technique, or one group of techniques, to the previous one.

The benchmark runs on one pinned core of an i9-13900HX. The machine is otherwise idle. Node counts
do not change between runs. The full tables, the method and the raw data are in
[results](results/README.md).

The table shows the mean time per move at depth 3, 5 and 6. Step 00 is the original engine.

| Step | Change | Depth 3 | Depth 5 | Depth 6 |
|---|---|---:|---:|---:|
| 00 | Original desktop engine (does not reach depth 5) | 24,379ms | n/a | n/a |
| 01 | Bitboards, legal moves, plain alpha-beta, rays for sliding pieces | 0.82ms | 136ms | 1,609ms |
| 02 | Magic bitboards for sliding pieces | 0.62ms | 99ms | 1,150ms |
| 03 | Move ordering: captures by victim and attacker, then promotions | 0.18ms | 7.2ms | 29ms |
| 04 | Transposition table | 0.18ms | 7.7ms | 30ms |
| 05 | Quiescence search | 0.59ms | 13ms | 89ms |
| 06 | Iterative deepening and principal variation search | 0.43ms | 5.4ms | 27ms |
| 07 | Null move, late move reductions, killers, history, check extension | 0.51ms | 2.5ms | 5.8ms |
| 08 | Exchange evaluation, futility, aspiration windows, counter moves | 0.25ms | 0.71ms | 1.4ms |
| 09 | Tuned evaluation, tuned margins, history gravity | 0.25ms | 0.96ms | 1.8ms |
| 10 | Network, history malus, correction history, probcut, margins | 0.18ms | 0.44ms | 0.93ms |

Three results stand out:

- Move ordering gives the largest gain after the bitboards. Depth 6 becomes 40 times faster, and
  the value of the search does not change.
- The transposition table does not help a search of fixed depth from an empty table. It helps
  across the passes of iterative deepening and across a whole game.
- Quiescence search is 1.7 to 4 times slower at equal depth. Against the search without it at
  depth 4, it scores 92% of the points (180 wins, 8 draws and 12 losses in 200 games).
- The refinements of step 08 make depth 10 4.7 times faster, from 80ms to 17ms, and the search
  visits 6.5 times fewer positions.
- The tuned evaluation of step 09 is slower per position (133ns for one evaluation against 62ns),
  and it plays much better. It scores 65% of the points against step 08 at 50ms per move (236
  wins, 49 draws, 115 losses in 400 games). Depth 10 takes 19ms.
- The network of step 10 scores 90% of the points against step 09 at 50ms per move (55 wins, 2
  draws, 5 losses in 62 games). It searches faster per position and visits fewer positions, so
  depth 10 takes 1.39 times less time in an interleaved run. Step 10 was measured in a later
  session than the rows above it, so the rows are not a comparison, and the interleaved run is.

### Summary

| Measure | Result |
|---|---|
| Depth 10 from an opening position | 20ms on one core |
| Search speed | 4.2 million nodes per second |
| Move generation (perft) | 44 million nodes per second |
| Against step 09 (the engine before the network), 50ms per move | 55 wins, 2 draws, 5 losses in 62 games (90% of the points) |
| Rating against Stockfish 19, 1s per move | about 2930 Elo (interval 2876 to 2986, 250 games) |

The rating uses Stockfish's `UCI_Elo` scale. Stockfish calibrates this scale for longer time
controls. The number therefore is not a rating-list rating. It applies at this speed and on this
machine.

### Comparison with Stockfish

Per node, the engine is faster than Stockfish. It searches 4.2 million nodes per second.
Stockfish searches 0.6 to 1.6 million nodes per second. Both numbers are measured on one core.

Stockfish needs far fewer nodes to reach a given depth, 20 times fewer at depth 10. Its search is
more selective, and its network is larger.

| Depth | Stockfish 19 | This engine | Stockfish nodes | Engine nodes |
|---:|---:|---:|---:|---:|
| 5 | 0.8ms | 0.44ms | 469 | 1,792 |
| 7 | 1.1ms | 1.8ms | 893 | 7,602 |
| 10 | 2.5ms | 11ms | 2,300 | 45,163 |

## Getting started

### Building

```bash
cmake -S . -B build
cmake --build build -j
```

### Testing

```bash
ctest --test-dir build --output-on-failure
```

### Installing

```bash
cmake --install build --prefix /usr/local     # meridian in bin, the licenses in share/doc/meridian
```

A release archive is made from a static build, which runs on any Linux of the same architecture:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCHESS_STATIC=ON
cmake --build build -j
(cd build && cpack)                          # build/meridian-VERSION-Linux-ARCHITECTURE.tar.gz
```

The workflow [release.yml](.github/workflows/release.yml) does this for x86-64 and ARM64 and
attaches the archives to a published release.

## UCI engine

The build produces `meridian`, the engine as a UCI program, for any chess program that speaks
the protocol. It reads the clock of a game and keeps to it, and it has options for the hash
size, the threads and the opening book.

```bash
build/meridian               # reads UCI commands on standard input
build/meridian bench         # nodes searched and speed on the built-in positions
build/meridian help          # the commands and the options
```

The options are `Hash`, `Threads`, `MoveOverhead`, `OwnBook`, `Ponder`, `EvalFile` and
`SyzygyPath`.

The commands, the options and the time management are in [ENGINE.md](ENGINE.md).

## Structure

```bash
.
├── bench           # benchmark programs and the positions they read
├── book            # the whitelist of the opening book
├── include         # headers
├── src             # the engine library and the UCI program
├── tests           # perft, mate puzzles, time management, UCI sessions
├── third_party     # Syzygy probing code, with its license
├── tools           # matches, tuning, training
├── scripts         # experiments and reports
├── results         # recorded benchmark and match results
├── ENGINE.md       # the code, the UCI program and how to run the benchmarks
├── CONTRIBUTING.md
├── LICENSE
├── README.md       # This file
└── SECURITY.md
```

[ENGINE.md](ENGINE.md) describes the engine and the UCI program.

## Roadmap

Quiescence search makes up 57% to 72% of all nodes at depths 5 to 10, and the first move tried
causes the cutoff 94% of the time. The results of the ideas that were tested are in
[results](results/README.md). These remain open.

### Close to a gain

- **Lazy SMP.** Two threads score 59% of the points against one thread. The `Threads` option of
  the UCI program turns it on, and a match with the network at several thread counts, up to the
  64 threads of a server, is open.
- **Vector instructions on ARM.** The AVX2 versions of the network functions are chosen when the
  program starts. A version for the dot-product instructions of an ARM processor is open, and so
  are its measurements on the board that runs the bot.
- **Threat inputs.** Inputs for the pieces that the other side attacks lowered the held-out error
  from 0.01737 to 0.01678, and they cost 18% to 26% of the speed because they are computed for
  every position. Updating them with the move, and not for every position, could make them pay.
- **The puzzle that the network misses.** One of the 1,404 mate puzzles is a mate in 3 that the
  search finds at depth 8. The test requires every mate in 3 to be found by depth 6, so it fails.

### Infrastructure

- A table of 64MB against 16MB and two-way buckets show no difference at 200ms per move. They
  need a longer time control to be measured.
- SPSA tuning with more games, now on the margins of the three techniques of step 10. The 120
  iterations of 48 games moved the margins and gained 18 Elo, so a longer run may give more.
- A build with PEXT lookups, which is 5% to 12% faster, for hosts known to have BMI2.
- A measurement of the tablebases. A match from random openings reaches few positions with five
  pieces, so it needs starting positions from endgames.
- More Leela positions. Twice the positions of the same 60 tars scored the same (+66 Elo against
  +55), so more tars may not help, and 164 more are listed.

### Evaluation

- Passed pawn and pawn structure terms for the hand written evaluation. They did not pass a match
  with the numbers that the tuning found, and need better starting values or more data.
- A learned move policy. A model of 196 features trained on self-play ordered moves worse than
  history, so it needs better features before another test.

## Provenance

The engine began as Horizon, a chess engine with a Windows desktop interface built on raylib. Its
rules, search and evaluation were rewritten as a library, and the interface moved to the web.
[results](results/README.md) records the speed and the rules correctness of the
original engine next to every later step. The table shows the original engine:

| Property of the original engine | Measured |
|---|---|
| Time to search 3 plies | 24s on average, 55s from opening positions |
| Positions visited at depth 3 | 159,004 (plain alpha-beta: 5,849) |
| Perft against published counts | 13 of 24 runs correct (castling and promotion) |
| Move generation | ~70 thousand nodes per second |
| Playing strength at depth 3 | about 1130 on Stockfish's scale (interval 1044 to 1220) |

## Contributing

[CONTRIBUTING.md](CONTRIBUTING.md) describes how to contribute. [SECURITY.md](SECURITY.md)
describes how to report a security problem.

## License

Copyright (C) 2026 Syed Taha.

Meridian is free software: it is distributed under the GNU General Public License, either
version 3 of the License or, at your option, any later version. See [LICENSE](LICENSE).

The Syzygy probing code in [third_party/fathom](third_party/fathom) is distributed under its own
MIT license, which is in that directory.

The network is trained on positions from the training games of
[Leela Chess Zero](https://lczero.org/dev/wiki/training-data/), which are made available under the
[Open Database License (ODbL)](https://opendatacommons.org/licenses/odbl/1-0/), and on games of the
engine against itself.

[blog-link]:          https://syedtaha22.github.io/blog/chess-bitboards-and-benchmarks/
[build-link]:         https://github.com/syedtaha22/meridian/actions/workflows/ci.yml
[commits-link]:       https://github.com/syedtaha22/meridian/commits/main
[discussions-link]:   https://github.com/syedtaha22/meridian/discussions/new
[docs-link]:          ENGINE.md
[issue-link]:         https://github.com/syedtaha22/meridian/issues/new?template=bug_report.yml
[license-link]:       LICENSE
[release-link]:       https://github.com/syedtaha22/meridian/releases/latest
[website-link]:       https://syedtaha22.github.io/meridian/

[build-badge]:        https://img.shields.io/github/actions/workflow/status/syedtaha22/meridian/ci.yml?branch=main&style=for-the-badge&label=meridian&logo=github
[commits-badge]:      https://img.shields.io/github/commits-since/syedtaha22/meridian/latest?style=for-the-badge
[license-badge]:      https://img.shields.io/github/license/syedtaha22/meridian?style=for-the-badge&label=license&color=success
[release-badge]:      https://img.shields.io/github/v/release/syedtaha22/meridian?style=for-the-badge&label=release
[website-badge]:      https://img.shields.io/website?style=for-the-badge&down_color=red&down_message=Offline&label=website&up_color=success&up_message=Online&url=https%3A%2F%2Fsyedtaha22.github.io%2Fmeridian%2F
