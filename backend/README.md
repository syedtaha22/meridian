# Chess backend

The backend holds the engine, its HTTP server, the tests and the benchmarks. The
[top-level README](../README.md) describes the engine and shows the main results. This page
describes the code.

## Layout

```bash
backend
├── bench                        # benchmark programs and the positions they read
│   ├── legacy
│   │   ├── legacy_adapter.cpp   # runs the original engine on the benchmark suites
│   │   └── run_baseline.sh      # builds the original engine from git history, measures it
│   ├── positions
│   │   ├── perft.txt            # six reference positions with published node counts
│   │   └── suite.txt            # fifteen positions: openings, middlegames, endgames
│   ├── bench_rules.cpp          # perft, move generation, slider lookups
│   ├── bench_search.cpp         # move selection by depth, presets interleaved
│   └── bench_eval.cpp           # cost of one evaluation
├── include/chess
│   ├── types.hpp                # squares, pieces, moves
│   ├── attacks.hpp              # attack tables
│   ├── position.hpp             # board state, make move, hashing
│   ├── movegen.hpp              # legal moves, perft, SAN
│   ├── eval.hpp                 # material and piece-square tables
│   ├── tt.hpp                   # transposition table
│   └── search.hpp               # search options, presets, the searcher
├── results                      # recorded numbers, see results/README.md
├── scripts
│   ├── benchmark.sh             # records one step under results/baselines
│   ├── benchmark_steps.sh       # measures the presets in one process, interleaved
│   ├── run_matches.sh           # runs the matches recorded under results/matches
│   ├── report.py                # prints a table of the recorded results as Markdown
│   └── rating_fit.py            # ratings fitted to the results of several matches
├── server
│   ├── api.cpp                  # request validation and replies
│   ├── api.hpp
│   └── main.cpp                 # the process: routes, environment, shutdown
├── src
│   ├── attacks.cpp              # magic bitboard generation and lookup
│   ├── movegen.cpp
│   ├── position.cpp
│   ├── search.cpp
│   ├── eval.cpp
│   └── tt.cpp
├── tests
│   ├── data                     # 1,404 mate-in-one, two and three puzzles
│   ├── position_test.cpp        # rules, hashing, perft
│   ├── search_test.cpp          # search behavior and mate puzzles
│   ├── stress_test.cpp          # random games checked against the rules
│   └── api_test.cpp             # the HTTP service
├── tools
│   ├── template
│   │   └── tree.html            # the page of visualize_tree, as HTML, CSS and JavaScript
│   ├── print_tree.cpp           # a search tree as text
│   ├── visualize_tree.cpp       # a search tree as SVG and interactive HTML
│   ├── play_match.cpp           # two configurations against each other
│   └── uci_match.cpp            # a match against any UCI engine, with a rating estimate
├── tree-examples                # search trees as SVG and HTML
├── cloudbuild.yaml              # builds the image and deploys it
├── CMakeLists.txt
└── Dockerfile
```

## Endpoints

### `GET /health`

Reply: `{ "status": "ok", "engine_version": string, "architecture": string,
"max_depth": number, "hash_mb": number }`

### `POST /move`

Request: `{ "fen": string, "moves"?: string[], "depth"?: number, "time_limit_ms"?: number }`

Reply: `{ "move": string, "score": { "cp": number | null, "mate": number | null },
"depth": number, "nodes": number, "elapsed_ms": number }`

`fen` is the position the game started from. `moves` are the UCI moves played since then, such
as `e2e4` or `e7e8q`. The server needs the move list, and not only the final board, to see
repeated positions.

`score` is from the point of view of the side to move. `cp` is in centipawns. `mate` is the
number of moves to mate, and it is negative when the side to move is mated. Exactly one of the
two is set.

A request with malformed JSON gets status 400. A request gets status 422 with
`{ "error": string }` if the position is invalid, a move in `moves` is illegal, a parameter is
out of range, or the position has no legal move. A `time_limit_ms` of 0, or no `time_limit_ms`,
means the server's own cap.

The server reads these environment variables:

- `PORT` (8080)
- `CHESS_MAX_DEPTH` (12)
- `CHESS_DEFAULT_DEPTH` (6)
- `CHESS_MAX_TIME_MS` (10000)
- `CHESS_HASH_MB` (64, per worker thread)
- `CHESS_THREADS` (4)
- `CHESS_ALLOWED_ORIGIN` the one origin that browsers may call
  the server from

The depth limit caps what a request may ask for. The engine itself searches up to 128 plies.

## Building

```bash
cmake -S . -B build
cmake --build build -j
build/chess_server
```

CMake downloads two header-only libraries at pinned versions when they are not installed:
cpp-httplib and nlohmann/json.

Two options change the build. `-DCHESS_RAY_ATTACKS=ON` finds slider attacks by walking rays
instead of with magic bitboards, which the benchmarks use to measure what the tables give.
`-DCHESS_SANITIZE=ON` builds with AddressSanitizer and UBSan.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

- [position_test](tests/position_test.cpp) checks FEN round trips, the attack tables against ray
  walking, special moves, the incremental hash, SAN and perft. Perft uses the published node
  counts for six reference positions, up to 200 million nodes each.
- [search_test](tests/search_test.cpp) solves all 1,404 mate puzzles. It checks that every
  configuration returns legal moves, and that move ordering and the hash table do not change the
  value of a search of fixed depth. It also checks the time and node limits, `stop()` from
  another thread, and repetitions.
- [api_test](tests/api_test.cpp) checks request validation, the reply shape, and a request over
  HTTP.

## Inspecting a search

```bash
build/print_tree tree.txt --depth 4 --max-ply 3 --max-children 8
build/visualize_tree tree --depth 5 --max-depth 3 --top-children 3   # tree.svg and tree.html
```

Both tools take `--fen`, `--moves e2e4,e7e5` and `--config`, which selects a search preset.

[visualize_tree](tools/visualize_tree.cpp) draws each position as a card. A card shows a small
board with the last move highlighted, the score from White's side, the depth, the number of
moves searched out of those available, and badges for a beta cutoff, a hash table answer,
quiescence and a null move. The border of the card has the color of the score. Each edge shows
the move and the number of positions searched below it. The best line is orange. A dashed purple
edge leads to a position that another path reached first. `--scale` sets the score in
pawns at which the colors reach their strongest value.

The HTML page also supports panning (drag), zooming (scroll), `F` to fit the whole tree and `G`
to hide the grid. Hovering over a card shows a larger board and a table of every move tried
there. A click pins the card. The path from the root to the card stays bright, and the rest of
the tree dims. Examples are in [tree-examples](tree-examples).

## Benchmarks

```bash
scripts/benchmark.sh 07-pruning-and-reductions full
```

Each run is pinned to one CPU (`--cpu`, default 8). It writes to `results/baselines/<run-id>/`:
`environment.json`, `rules.json` (perft speed and correctness, move generation and slider lookup
times), `eval.json` and `move_selection.csv` (one row per depth and position). The search
benchmark interleaves its rounds and reports the median.

[run_baseline.sh](bench/legacy/run_baseline.sh) builds the original desktop engine from git
history and measures it on these suites.

[results/README.md](results/README.md) has the recorded numbers and how to read them.
[report.py](scripts/report.py) prints one table of them, such as `scripts/report.py rules`.

## Matches

```bash
build/play_match --a full --b iterative --time-a 50 --time-b 50 --openings 50 --threads 8
```

Each random opening is played twice, once with each color. The report shows wins, draws and
losses, the score with a 95% interval, and the Elo difference.

### Matches against Stockfish

```bash
build/uci_match --engine /path/to/stockfish --elo 1800,2000,2200,2400,2600,2800 \
    --time 1000 --openings 25 --threads 16 --seed 21
```

[uci_match](tools/uci_match.cpp) plays the engine against any UCI engine. It limits the opponent
with `UCI_LimitStrength` and `UCI_Elo`, one level for each value, and gives both sides equal
time per move. It combines the levels into one rating estimate with an interval.

[rating_fit.py](scripts/rating_fit.py) fits the ratings of several players to match results given
as a CSV file of wins, draws and losses for each pair. Stockfish players keep the rating in their
name, and the others are fitted.

```bash
scripts/rating_fit.py results/matches/original/games.csv
```

The games against Stockfish 19 are in [results/matches](results/matches).
[results/README.md](results/README.md) has the table and the method.
