# Meridian engine

This page describes the code: the engine, the UCI program, the tests and the benchmarks. The
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
├── include
│   ├── types.hpp                # squares, pieces, moves
│   ├── attacks.hpp              # attack tables
│   ├── position.hpp             # board state, make move, hashing
│   ├── movegen.hpp              # legal moves, perft, SAN
│   ├── eval.hpp                 # the evaluation and its tunable numbers
│   ├── see.hpp                  # static exchange evaluation of a capture
│   ├── book.hpp                 # the opening book, looked up by position
│   ├── policy.hpp               # move features and the learned policy score
│   ├── tt.hpp                   # transposition table, lockless, shared by threads
│   ├── timeman.hpp              # the budget of one move from the clock of a game
│   ├── nnue.hpp                 # the network of the evaluation, with incremental accumulators
│   ├── syzygy.hpp               # probing of Syzygy endgame tablebases
│   ├── search.hpp               # search options, presets, the searcher
│   ├── uci.hpp                  # the UCI protocol around the searcher
│   └── version.hpp              # the version of the engine
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
├── src
│   ├── main.cpp                 # the meridian program
│   ├── uci.cpp                  # commands, options and output
│   ├── bench_positions.inc      # the positions of the bench command
│   ├── attacks.cpp              # magic bitboard generation and lookup
│   ├── movegen.cpp
│   ├── position.cpp
│   ├── search.cpp
│   ├── timeman.cpp
│   ├── nnue.cpp
│   ├── nnue_builtin.cpp         # the network that is compiled into the library, when there is one
│   ├── syzygy.cpp
│   ├── eval.cpp
│   ├── eval_defaults.inc        # the tuned numbers, as texel_tune found them
│   ├── see.cpp
│   ├── book.cpp
│   ├── book_data.inc            # moves written by build_book
│   ├── policy.cpp
│   ├── policy_weights.inc       # weights written by train_policy
│   └── tt.cpp
├── tests
│   ├── data                     # 1,404 mate-in-one, two and three puzzles
│   ├── position_test.cpp        # rules, hashing, perft
│   ├── search_test.cpp          # search behavior and mate puzzles
│   ├── book_test.cpp            # book moves are legal, and games vary
│   ├── timeman_test.cpp         # the time budget stays inside the clock
│   ├── uci_test.cpp             # scripted UCI sessions
│   ├── nnue_test.cpp            # incremental accumulators, symmetry, file round trip
│   ├── syzygy_test.cpp          # probing, root moves and the search with small tables
│   ├── stress_test.cpp          # random games checked against the rules
│   └── sprt_test.cpp            # the sequential test, with simulated error rates
├── tools
│   ├── play_match.cpp           # two configurations against each other, with a sequential test
│   ├── sprt.hpp                 # the sequential probability ratio test on pairs of games
│   ├── mate_check.cpp           # solves the mate puzzles with one preset
│   ├── build_book.cpp           # builds the opening book by searching the opening tree
│   ├── gen_positions.cpp        # self-play positions with results, for tuning
│   ├── texel_tune.cpp           # tunes the evaluation numbers against the results
│   ├── train_nnue.cpp           # trains the network of the evaluation from self-play positions
│   ├── train_policy.cpp         # trains the move policy from self-play games
│   └── uci_match.cpp            # a match against any UCI engine, with a rating estimate
├── third_party
│   └── fathom                   # Syzygy probing code (MIT licence), see its LICENSE
└── CMakeLists.txt
```

## UCI engine

`meridian` speaks the UCI protocol on standard input and output, so that chess programs and
rating list testers can run the engine.

```bash
build/meridian               # reads UCI commands
build/meridian bench         # searches the built-in positions to depth 10
build/meridian bench 12
build/meridian help          # the commands and the options
build/meridian version
```

It supports `uci`, `isready`, `ucinewgame`, `setoption`, `position` (`startpos` or `fen`, with
`moves`), `go`, `stop`, `ponderhit`, `bench`, `help` and `quit`. A command that it does not know
gets a reply that says so. `go` takes `wtime`, `btime`, `winc`, `binc`, `movestogo`, `movetime`,
`depth`, `nodes`, `mate`, `searchmoves`, `infinite`, `ponder` and `perft`. Each finished iteration
prints an `info` line with depth, score (centipawns, or moves to mate), nodes, speed, time and the
line.

`go ponder` searches the position without a limit until `ponderhit`, which continues the search
under the clock of the command, or until `stop`. `go perft N` prints the number of positions
below each move and the total.

| Option | Type | Default | Meaning |
|:-------|:-----|:--------|:--------|
| `Hash` | spin, 1 to 4096 | 64 | Transposition table size in MB. |
| `Threads` | spin, 1 to 64 | 1 | Search threads on one table (Lazy SMP). |
| `MoveOverhead` | spin, 0 to 5000 | 30 | Milliseconds kept back per move. |
| `OwnBook` | check | false | Play moves of the opening book while the game is in it. |
| `EvalFile` | string | empty | A network from `train_nnue`, in place of the hand written one. |
| `SyzygyPath` | string | empty | Directories of Syzygy tables, separated by `:`. |
| `Ponder` | check | false | Offered so that a chess program may send `go ponder`. |
| `Clear Hash` | button | | Forget the table. |

**Time management.** With a clock, the budget of a move is a share of the time left, 1/30 of it
when the moves to the next time control are not given, plus three quarters of the increment. A
soft limit stops the search after an iteration when about 35% to 70% of it has gone, the smaller
share when the best move has stayed the same. A hard limit, never above 80% of the time left
after the move overhead, stops the search at once. With `movetime` the whole time is used. The
rules are in [timeman.cpp](src/timeman.cpp).

**Tablebases.** With `SyzygyPath` set, the engine reads Syzygy endgame tables with the
[Fathom](third_party/fathom/README.md) probing code, up to the largest set that is found (5
pieces is about 1 GB, 6 pieces about 150 GB). A position with no more pieces than the tables hold
is decided in two ways:

- At the root, the distance-to-zero tables give the move that keeps the result and wins within
  the 50-move rule (or loses as slowly as possible). The search returns it without searching, and
  reports a win as `cp 28000`.
- In the search, a position right after a capture or a pawn move (a half move clock of 0, with no
  castling rights) scores as a win, a draw or a loss from the table, and counts as `tbhits`.

A cursed win and a blessed loss, which the 50-move rule makes draws, score 0. The switch
`tablebases` of `SearchOptions` turns the tables off.

`bench` prints the nodes searched, which do not change between runs of a build, and the speed.
The version in the `id name` line is the one in [version.hpp](include/version.hpp).

## Building

```bash
cmake -S . -B build
cmake --build build -j
build/meridian
```

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
- [timeman_test](tests/timeman_test.cpp) checks that the budget of a move stays inside the clock
  and follows the time, the increment and the moves to go.
- [uci_test](tests/uci_test.cpp) runs scripted sessions: the handshake, the options, searches
  with depth, node, move time and clock limits, a mate score, a stop, the book and bad input.
- [syzygy_test](tests/syzygy_test.cpp) loads the small tables of
  [tests/data/syzygy](tests/data/syzygy) (king and queen, king and rook, king and pawn, queen
  against rook) and checks the result of positions, a game played out from the root moves to mate,
  and that the search uses the tables and stops using them with the switch off.
- [nnue_test](tests/nnue_test.cpp) checks that the accumulator that follows a line of moves equals
  the one computed from the board (random games, and castling, en passant and promotion move by
  move), that a position and its mirror score the same, that a network file loads back, and that
  a search with a network is repeatable.

## Network evaluation

```bash
build/gen_positions --games 60000 --depth 6 --skip 2 --threads 24 --out positions.txt
build/train_nnue --data positions.txt --out net.nnue --epochs 30
build/play_match --a tuned --b tuned --nnue-a net.nnue --sprt --elo0 0 --elo1 10
```

A network (see [nnue.hpp](include/nnue.hpp)) maps the 768 combinations of a piece and a square to
128 values for each side, clips them to [0, 255], and combines both sides into one score. The
values of a position follow the search: a move changes a few rows of the weights, so that an
evaluation costs one pass over 256 values. Weights are 16-bit integers.

- [gen_positions](tools/gen_positions.cpp) writes each position with the result of its game and
  the search score.
- [train_nnue](tools/train_nnue.cpp) fits the weights in floating point with Adam, to the
  expected points of a blend of the search score and the result, with a fifth of the games held
  out. It prints the held-out loss of the hand written evaluation on the same targets, and
  compares the quantized network with the floating point one.
- `play_match`, `uci_match`, `mate_check` and `bench_search` take `--nnue FILE` (`--nnue-a` and
  `--nnue-b` for `play_match`), and `SearchOptions::nnue` holds a network. The `EvalFile` option
  of the UCI program loads one.

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
`ordering`, `tt`, `quiescence`, `iterative`, `pruning`, `full`, `tuned` and `network`. Each preset
is a step of [results](results/README.md): `full` is step 08, with the evaluation, margins and
history that it had, `tuned` is step 09, and `network` is step 10, which is also what
`SearchOptions` defaults to.

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
  of the engine against itself. It writes `src/policy_weights.inc`.

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
- [eval_ladder.py](scripts/eval_ladder.py) tunes `EvalParams` (`include/eval.hpp`) in
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

The book is a set of opening moves that the engine plays while a game is in it, so that the
engine does not play the same game every time. [build_book](tools/build_book.cpp) writes it as
`src/book_data.inc`, which the library compiles in.

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
