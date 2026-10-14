# Meridian engine

This page describes the code: the engine, its HTTP server, the tests and the benchmarks. The
[README](README.md) describes the engine and shows the main results.

## Layout

```bash
.
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
├── book
│   └── whitelist.txt            # the moves the book may use in the first positions
├── include/chess
│   ├── types.hpp                # squares, pieces, moves
│   ├── attacks.hpp              # attack tables
│   ├── position.hpp             # board state, make move, hashing
│   ├── movegen.hpp              # legal moves, perft, SAN
│   ├── eval.hpp                 # the evaluation and its tunable numbers
│   ├── eval_defaults.inc        # the tuned numbers, as texel_tune found them
│   ├── see.hpp                  # static exchange evaluation of a capture
│   ├── book.hpp                 # the opening book, looked up by position
│   ├── book_data.inc            # moves written by build_book
│   ├── policy.hpp               # move features and the learned policy score
│   ├── policy_weights.inc       # weights written by train_policy
│   ├── tt.hpp                   # transposition table, lockless, shared by threads
│   └── search.hpp               # search options, presets, the searcher
├── results                      # recorded numbers, see results/README.md
├── scripts
│   ├── benchmark.sh             # records one step under results/baselines
│   ├── benchmark_steps.sh       # measures the presets in one process, interleaved
│   ├── run_matches.sh           # runs the matches recorded under results/matches
│   ├── experiment.sh            # tests one change: puzzles, nodes, match with a sequential test
│   ├── search_ladder.py         # tests a list of changes and keeps the ones that pass
│   ├── spsa.py                  # tunes numeric parameters by simultaneous perturbation
│   ├── eval_ladder.py           # Texel tuning of the evaluation in stages, with a match each
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
│   ├── see.cpp
│   ├── book.cpp
│   ├── policy.cpp
│   └── tt.cpp
├── tests
│   ├── data                     # 1,404 mate-in-one, two and three puzzles
│   ├── position_test.cpp        # rules, hashing, perft
│   ├── search_test.cpp          # search behavior and mate puzzles
│   ├── book_test.cpp            # book moves are legal, and games vary
│   ├── stress_test.cpp          # random games checked against the rules
│   ├── sprt_test.cpp            # the sequential test, with simulated error rates
│   └── api_test.cpp             # the HTTP service
├── tools
│   ├── template
│   │   └── tree.html            # the page of visualize_tree, as HTML, CSS and JavaScript
│   ├── print_tree.cpp           # a search tree as text
│   ├── visualize_tree.cpp       # a search tree as SVG and interactive HTML
│   ├── play_match.cpp           # two configurations against each other, with a sequential test
│   ├── sprt.hpp                 # the sequential probability ratio test on pairs of games
│   ├── mate_check.cpp           # solves the mate puzzles with one preset
│   ├── build_book.cpp           # builds the opening book by searching the opening tree
│   ├── gen_positions.cpp        # self-play positions with results, for tuning
│   ├── texel_tune.cpp           # tunes the evaluation numbers against the results
│   ├── train_policy.cpp         # trains the move policy from self-play games
│   └── uci_match.cpp            # a match against any UCI engine, with a rating estimate
├── tree-examples                # search trees as SVG
├── cloudbuild.yaml              # builds the image and deploys it
├── CMakeLists.txt
└── Dockerfile
```

## Endpoints

### `GET /health`

Reply: `{ "status": "ok", "engine_version": string, "architecture": string,
"max_depth": number, "hash_mb": number }`

The version follows the rules in [VERSIONING.md](VERSIONING.md).

### `POST /move`

Request: `{ "fen": string, "moves"?: string[], "depth"?: number, "time_limit_ms"?: number,
"book"?: boolean }`

Reply: `{ "move": string, "score": { "cp": number | null, "mate": number | null },
"depth": number, "nodes": number, "elapsed_ms": number, "book": boolean }`

`fen` is the position the game started from. `moves` are the UCI moves played since then, such
as `e2e4` or `e7e8q`. The server needs the move list, and not only the final board, to see
repeated positions.

`score` is from the point of view of the side to move. `cp` is in centipawns. `mate` is the
number of moves to mate, and it is negative when the side to move is mated. Exactly one of the
two is set.

`book` turns the opening book on or off for the request, and the server's own setting applies
when it is missing. A move from the book has `book` set to true, `nodes` 0, and the score and
depth of the search that built the book. The book holds several moves for most positions and
the reply picks one at random by weight, so the same request can give different moves.

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
- `CHESS_BOOK` (1), 0 turns the opening book off for requests that do not ask for it
- `CHESS_ALLOWED_ORIGIN` (`https://syedtaha22.github.io`), the one origin that browsers may call
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

Three options change the build:

- `-DCHESS_RAY_ATTACKS=ON` finds slider attacks by walking rays instead of with magic bitboards,
  which the benchmarks use to measure what the tables give.
- `-DCHESS_SANITIZE=ON` builds with AddressSanitizer and UBSan.
- `-DCHESS_BMI2=ON` builds for CPUs with BMI2 (`-mpopcnt -mbmi2`) and finds slider attacks with
  PEXT, which gives 5% to 12% more positions per second. The default build stays portable.

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
scripts/benchmark.sh 09-tuned-evaluation tuned
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

A sequential test stops the match as soon as it can tell whether the first configuration is
stronger by a margin. This command asks whether `full` is at least 20 Elo stronger than
`ordering`, with 5% error probabilities, in up to 400 games:

```bash
build/play_match --a full --b ordering --sprt --elo0 0 --elo1 20 --openings 200 --threads 8
```

The test counts whole pairs of games, in the order they started, and decides after 30 pairs at
the earliest. The report adds the likelihood ratio, its bounds and the decision.

### Search experiments

A preset name takes changes after it, joined with `+`: a switch (`tuned+razoring`,
`tuned+see=0`) or a number (`tuned+rfp_margin=100`). Every technique that was tested is a switch
in `SearchOptions`, on if it won its match and off otherwise. The presets are `baseline`,
`ordering`, `tt`, `quiescence`, `iterative`, `pruning`, `full` and `tuned`. Each preset is a
step of [results](results/README.md): `full` is step 08, with the evaluation, margins and history
that it had, and `tuned` is step 09, which is also what `SearchOptions` and the server default to.

```bash
build/mate_check --config tuned+razoring
scripts/experiment.sh my-change tuned+razoring tuned
scripts/search_ladder.py list.txt --base tuned
scripts/spsa.py --base tuned --iterations 120
```

- [mate_check](tools/mate_check.cpp) solves the 1,404 mate puzzles with one preset. A change that
  misses a puzzle is not kept.
- [experiment.sh](scripts/experiment.sh) runs the puzzles, the nodes at a fixed depth and a match
  with the sequential test for one change against a base. It writes to
  `results/matches/search-experiments`.
- [search_ladder.py](scripts/search_ladder.py) tests a list of changes one after the other, and
  adds each change that passes to the base of the next one.
- [spsa.py](scripts/spsa.py) tunes the numbers of the techniques in a base with short matches.
- [train_policy](tools/train_policy.cpp) trains the move policy of the `policy` switch from games
  of the engine against itself. It writes `include/chess/policy_weights.inc`.

`threads=2` (`tuned+threads=2`) lets two threads search one position into the shared table.

### Tuning the evaluation and the move ordering

```bash
build/gen_positions --games 24000 --depth 7 --threads 24 --out positions.txt
scripts/eval_ladder.py positions.txt --threads 12
```

The matches use the clock, so the machine should be idle.

- The move ordering has switches and numbers in `SearchOptions` that
  [search_ladder.py](scripts/search_ladder.py) tests like any other change: `history_malus`,
  `history_gravity`, `history_piece`, `capture_history`, `continuation2`, `counter_first`,
  `killer_count`, `mvv_scale`, `bad_capture_margin`, `underpromotions_late`, `history_limit`,
  `history_bonus`, `malus_percent` and `continuation_limit`.
- [gen_positions](tools/gen_positions.cpp) plays the engine against itself and writes quiet
  positions with the result of their game.
- [eval_ladder.py](scripts/eval_ladder.py) tunes `EvalParams` (`include/chess/eval.hpp`) in
  stages with [texel_tune](tools/texel_tune.cpp) and plays each stage against the numbers before
  it. The numbers have a middlegame and an endgame value. The stages are the material and
  piece-square tables, then tempo and the bishop pair, passed pawns, doubled and isolated pawns,
  rooks on open files, and mobility. A term whose numbers are all zero is not computed.

`experiment.sh`, `search_ladder.py` and `spsa.py` take `--eval FILE` for both sides.
`play_match`, `uci_match`, `mate_check` and `bench_search` take `--eval FILE` (`--eval-a` and
`--eval-b` for `play_match`) to play with the numbers of a file.

### Opening book

```bash
build/build_book --depth 12 --plies 10 --threads 24
```

The book is a set of opening moves that the server plays while a game is in it, so that the
engine does not play the same game every time. [build_book](tools/build_book.cpp) writes it as
`include/chess/book_data.inc`, which the library compiles in.

The tool searches the opening tree once for each colour the engine can play:

- In a position where the engine moves, it keeps at most 3 moves that score within 15cp of the
  best.
- In a position where the opponent moves, it follows at most 4 replies within 30cp, so that the
  book also covers what other players choose.
- Each move is scored by a search of the position after it to depth 11.
- A position that scores more than 60cp from equal is not followed.
- [book/whitelist.txt](book/whitelist.txt) names the moves the engine may choose in the first
  positions, which are the four first moves and the mainstream replies. Every other position is
  decided by the search.

A move of the book is chosen at random with a weight that falls with its score gap, so a move
15cp worse than the best has about a third of its weight. The match and benchmark tools do not
use the book, so their results do not depend on it.

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
