# Chess

A chess engine written in C++

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
  depth.
- **Check extension.** The engine searches one ply deeper when the side to move is in check.
- **Quiescence search.** At the end of the search, the engine continues with captures. This
  prevents it from stopping in the middle of an exchange.

The order of moves matters for pruning. The engine tries the hash move first. Then it tries
captures, ordered by the value of the captured piece and the value of the capturing piece. Then
it tries the killer moves. Then it tries the other moves in the order of the history heuristic.

The engine can draw the search tree as an SVG image or as an interactive page. The image below
shows the tree from the initial position at depth 5. The interactive pages are in
[backend/tree-examples](backend/tree-examples).

![Search tree from the initial position](backend/tree-examples/initial-position.svg)

### Evaluation

The engine scores a position from material and from the placement of the pieces. Each piece has
a value. A piece-square table adds a bonus or a penalty for the square the piece stands on. The
king uses one table in the middlegame and another in the endgame.

### Verification

The tests check the engine in three ways:

- Move generation gives the published perft counts for six reference positions.
- The search solves 1,404 puzzles that ask for mate in one, two or three moves.
- The engine runs under AddressSanitizer and UBSan.

## Performance

The engine improves one technique at a time. A technique stays only if the benchmarks show that
it helps. This section shows those benchmarks.

The benchmark uses fifteen fixed positions: five openings, five middlegames and five endgames.
For each position, the engine searches to a given depth, starting with an empty hash table. The
benchmark records the time and the number of nodes. Each configuration of the search adds one
technique to the previous one.

The benchmark runs on one pinned core of an i9-13900HX. The machine is otherwise idle. Node counts
do not change between runs. The full tables, the method and the raw data are in
[backend/results](backend/results/README.md).

The table shows the mean time per move at depth 3, 5 and 6. Step 00 is the original engine.

| Step | Change | Depth 3 | Depth 5 | Depth 6 |
|---|---|---:|---:|---:|
| 00 | Original desktop engine (does not reach depth 5) | 24,379ms | n/a | n/a |
| 01 | Bitboards, legal moves, plain alpha-beta, rays for sliding pieces | 0.82ms | 136ms | 1,609ms |
| 02 | Magic bitboards for sliding pieces | 0.58ms | 94ms | 1,096ms |
| 03 | Move ordering: captures by victim and attacker, killers, history | 0.16ms | 6.8ms | 26.9ms |
| 04 | Transposition table | 0.16ms | 7.0ms | 26.9ms |
| 05 | Quiescence search | 0.56ms | 12.7ms | 85ms |
| 06 | Iterative deepening and principal variation search | 0.41ms | 5.2ms | 24ms |
| 07 | Null move pruning, late move reductions, check extension | 0.48ms | 2.4ms | 5.3ms |

Three results stand out:

- Move ordering gives the largest gain. Depth 6 becomes 40 times faster, and the value of the
  search does not change.
- The transposition table does not help a search of fixed depth from an empty table. It helps
  across the passes of iterative deepening and across a whole game.
- Quiescence search is two to four times slower at equal depth. Against the search without it at
  depth 4, it scores 92% of the points (180 wins, 8 draws and 12 losses in 200 games).

### Summary

| Measure | Result |
|---|---|
| Depth 10 from an opening position | 145ms on one core |
| Search speed | 5.3 million nodes per second |
| Move generation (perft) | 44 million nodes per second |
| Against step 01 (plain alpha-beta) at depth 4, 50ms per move | 191 wins, 3 draws, 6 losses in 200 games (96.3% of the points) |
| Rating against Stockfish 19, 1s per move | about 2300 Elo (interval 2270 to 2345, 600 games) |

The rating uses Stockfish's `UCI_Elo` scale. Stockfish calibrates this scale for longer time
controls. The number therefore is not a rating-list rating. It applies at this speed and on this
machine.

### Comparison with Stockfish

Per node, the engine is faster than Stockfish. It searches 5.3 million nodes per second.
Stockfish searches 0.6 to 1.6 million nodes per second. Both numbers are measured on one core.

Stockfish needs far fewer nodes to reach a given depth. Its search is more selective, and its
evaluation is a neural network.

| Depth | Stockfish 19 | This engine | Stockfish nodes | Engine nodes |
|---:|---:|---:|---:|---:|
| 5 | 0.8ms | 2.4ms | 469 | 11,830 |
| 7 | 1.1ms | 9.9ms | 893 | 52,488 |
| 10 | 2.5ms | 74.5ms | 2,300 | 401,971 |

## Getting started

### Building

```bash
cmake -S backend -B backend/build
cmake --build backend/build -j
```

### Testing

```bash
ctest --test-dir backend/build --output-on-failure
```

## Server

The build also produces `chess_server`, a small HTTP server for the engine. It has two endpoints.
`GET /health` returns the version and the limits. `POST /move` returns the move for a position.
The request can set a depth limit and a time limit, and the server caps both.

```bash
backend/build/chess_server        # listens on port 8080
```

The request asks for the best move from the starting position, searched to depth 6.

```bash
curl -s localhost:8080/move -H 'Content-Type: application/json' \
  -d '{"fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "depth": 6}'
```

```json
{"depth":6,"elapsed_ms":1.448527,"move":"b1c3","nodes":9752,"score":{"cp":0,"mate":null}}
```

The reply gives the move b1c3 (the knight from b1 to c3), a score of 0 centipawns, and the work
done: 9,752 nodes in 1.4ms.

The request and reply formats, the environment variables and the limits are in
[backend/README.md](backend/README.md).

## Structure

```bash
.
├── backend         # the engine, its server, tests and benchmarks
│   ├── ...
│   └── README.md
├── frontend        # the board as a React package
│   ├── ...
│   └── README.md
├── CONTRIBUTING.md
├── LICENSE
├── README.md       # This file
└── SECURITY.md
```

[backend/README.md](backend/README.md) describes the engine and its server.
[frontend/README.md](frontend/README.md) describes a browser board for the engine.

## Roadmap

Measurements show where the engine can improve. Quiescence search makes up 84% to 89% of all
nodes at depths 5 to 10. The first move tried causes the cutoff 94% of the time, so move ordering
is already good. The next gains come from searching fewer nodes.

### Quiescence search

- Static exchange evaluation, to order captures and to skip losing captures
- Delta pruning of captures that cannot raise alpha
- Losing captures tried after the quiet moves

### Selective search

- Reverse futility pruning
- Futility pruning of quiet moves near the leaves
- Razoring at shallow depth
- Late move pruning
- A logarithmic reduction table for late move reductions, with smaller reductions for killers
  and for moves with a high history score
- Internal iterative reduction
- Singular extensions
- A null move reduction that depends on the static margin over beta
- Aspiration windows around the score of the previous pass

### Move ordering

- Counter-move heuristic
- Continuation history

### Infrastructure

- A larger default hash table, and a replacement scheme tested over long games
- Syzygy endgame tablebases
- Lazy SMP, with several search threads over one hash table
- A `-mpopcnt -mbmi2` build option, and PEXT lookups for sliding pieces
- SPSA tuning of every search constant over self-play games

### Evaluation

- A neural evaluation (NNUE) with an incremental accumulator. This is the largest single step
  and the most work.

## Provenance

The engine began as a Windows desktop game with a raylib interface. Its rules, search and
evaluation were rewritten as a library, and the interface moved to the web.
[backend/results](backend/results/README.md) records the speed and the rules correctness of the
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

GPL-3.0-or-later, see [LICENSE](LICENSE).
