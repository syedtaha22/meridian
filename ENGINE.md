# Meridian engine

This page describes the code: the engine, the UCI program and the tests. The
[README](README.md) describes the engine and shows the main results.

## Layout

```bash
.
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
│   ├── experience.hpp           # analysed positions that are read from a file
│   ├── policy.hpp               # move features and the learned policy score
│   ├── tt.hpp                   # transposition table, lockless, shared by threads
│   ├── timeman.hpp              # the budget of one move from the clock of a game
│   ├── nnue.hpp                 # the network of the evaluation, with incremental accumulators
│   ├── syzygy.hpp               # probing of Syzygy endgame tablebases
│   ├── search.hpp               # search options, presets, the searcher
│   ├── uci.hpp                  # the UCI protocol around the searcher
│   └── version.hpp              # the version of the engine
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
│   ├── eval_defaults.inc        # the tuned numbers of the evaluation
│   ├── magic_numbers.inc        # multipliers of the slider tables
│   ├── see.cpp
│   ├── book.cpp
│   ├── book_data.inc            # the moves of the opening book
│   ├── policy.cpp
│   ├── policy_weights.inc       # weights of the move policy
│   └── tt.cpp
├── tests
│   ├── data                     # the Syzygy tables and the positions of the tests
│   ├── position_test.cpp        # rules, hashing, perft
│   ├── search_test.cpp          # search behavior
│   ├── book_test.cpp            # book moves are legal, and games vary
│   ├── timeman_test.cpp         # the time budget stays inside the clock
│   ├── uci_test.cpp             # scripted UCI sessions
│   ├── nnue_test.cpp            # incremental accumulators, king buckets, symmetry, file round trip
│   ├── syzygy_test.cpp          # probing, root moves and the search with small tables
│   └── stress_test.cpp          # random games checked against the rules
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
| `Config` | string | network | A search preset with changes, see below. |
| `EvalFile` | string | empty | A network file, in place of the built-in network. |
| `ExperienceFile` | string | empty | A file of analysed positions, see Experience file. |
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

- `-DCHESS_RAY_ATTACKS=ON` finds slider attacks by walking rays instead of with magic bitboards.
- `-DCHESS_SANITIZE=ON` builds with AddressSanitizer and UBSan.
- `-DCHESS_STATIC=ON` links the programs statically, for the release archives.
- `-DCHESS_BMI2=ON` builds for CPUs with BMI2 (`-mpopcnt -mbmi2`) and finds slider attacks with
  PEXT, which gives 5% to 12% more positions per second. The default build stays portable.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

- [position_test](tests/position_test.cpp) checks FEN round trips, the attack tables against ray
  walking, special moves, the incremental hash, SAN and perft. Perft uses the published node
  counts for six reference positions, up to 200 million nodes each.
- [search_test](tests/search_test.cpp) checks that every configuration returns legal moves, and
  that move ordering and the hash table do not change the value of a search of fixed depth. It
  also checks the time and node limits, `stop()` from another thread, and repetitions.
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
  move, and games of two kings and a few pieces where the kings cross the middle of the board and
  change bucket or mirror), the king buckets, mirrors and output layers of given squares and
  piece counts, that a position and its mirror score the same, that a network file loads back,
  and that a search with a network is repeatable.

## Network evaluation

A network (see [nnue.hpp](include/nnue.hpp)) maps the combinations of a piece and a square to 128
values for each side, clips them to [0, 255], and combines both sides into one score. There are
768 combinations (12 kinds of piece on 64 squares) for each king bucket. The king bucket of a side
is the half of the board by rank that holds its own king, after the board is mirrored so that the
king stands on the files a to d, which gives 2 x 768 inputs. One of 8 output layers is used, chosen
by the number of pieces on the board (2 to 5 pieces the first, 6 to 9 the second, and so on).

The values of a position follow the search: a move changes a few rows of the weights. A king that
moves to the other half or across the mirror changes every input of its side, and the values of
that side are computed again. An evaluation costs one pass over 256 values. Weights are 16-bit
integers, and on x86-64 the functions that add and sum rows exist in an AVX2 version and a base
version, of which the program picks one when it starts.

The built-in network is the one in `src/nnue_net.inc`, which is compiled into the library.
`SearchOptions::nnue` holds a network, and the `EvalFile` option of the UCI program loads one
from a file.

## Search presets

A preset name takes changes after it, joined with `+`: a switch (`tuned+razoring`,
`tuned+see=0`) or a number (`tuned+rfp_margin=100`). Every technique that was tested is a switch
in `SearchOptions`, on if it won its match and off otherwise. The presets are `baseline`,
`ordering`, `tt`, `quiescence`, `iterative`, `pruning`, `full`, `tuned` and `network`. Each preset
is a step of the table in the [README](README.md): `full` is step 08, with the evaluation, margins
and history that it had, `tuned` is step 09, and `network` is step 10, which is also what
`SearchOptions` defaults to.

The `Config` option of the UCI program takes such a name, for example
`setoption name Config value network+razoring=1`. A name that is not a preset, a change that is
not a switch or a parameter, and a value that does not fit are refused with a reason in an
`info string` line, and the earlier configuration stays. The `Threads` option sets the thread count, so
`Config` does not take `threads`.

The preset names, the switch names and the parameter names are part of the interface from version
1.0.0. Removing or renaming one needs a new major version, and adding one needs a new minor
version. A value that a switch or parameter takes keeps its meaning.

## Limits

- Chess960 is not supported, and `UCI_Chess960` is not an option.
- There is no strength limit: `UCI_LimitStrength` and `UCI_Elo` are not options.
- `go perft N` runs on the command thread, so `isready`, `stop` and `quit` wait until it ends.
- Without an increment, a game of 1 minute or less can flag at the default `MoveOverhead`. A
  value of 100 or more should be used for such games.
- The `info` lines carry depth, score, nodes, speed, time and the line. They have no `seldepth`,
  `hashfull` or `multipv`.
- Scaling was measured up to 8 threads. The `Threads` option accepts up to 64.
- The tests run on x86-64 only. The release archive for ARM64 is built by the Release workflow.

## Opening book

The book is a set of opening moves that the engine plays while a game is in it, so that the
engine does not play the same game every time. It is compiled into the library from
`src/book_data.inc`, and the `OwnBook` option turns it on.

[book/whitelist.txt](book/whitelist.txt) names the moves the engine may choose in the first
positions, which are the four first moves and the mainstream replies. Every other position of the
book has one to several moves.

A move of the book is chosen at random with a weight that falls with its score gap, so a move
15cp worse than the best has about a third of its weight.

## Experience file

The option `ExperienceFile` names a file of positions that were analysed earlier, deeper than a
game search can go. When the position to move in is in the file and the depth of its analysis is
above the depth that the search completed, the engine plays the move of the analysis and reports
`info string experience move`. A search that reaches the depth of the analysis keeps its own move.
Without the option, or with an empty value, the engine does not read experience and its searches
do not change.

The file has a header of 16 bytes: the text `MRDEXP01` and the number of entries as a 64-bit
integer. The entries follow in order of their keys, each one 14 bytes: the position hash
(`Position::key`, 64 bits), the best move (`Move::data`, 16 bits), the score for the side to move
(16 bits, mates near 30000) and the depth of the analysis (16 bits). Numbers are little-endian.
The engine reads the file and never writes it. It refuses a file with a wrong size, with another
header, or whose keys are not in strictly increasing order, and keeps the previous experience.
