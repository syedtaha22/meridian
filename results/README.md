# Results

This folder holds the recorded benchmarks and matches of the engine. The engine grew in ten
steps. Step 00 is the original desktop engine, and each later step adds one technique, or one
group of techniques, to the step before it. Every step runs on the position suites in
[bench/positions](../bench/positions).

| Step                       | Technique added |
|:---------------------------|:----------------|
| 00-original-engine         | Original desktop engine |
| 01-bitboards-ray-attacks   | Bitboards, copy-make, plain alpha-beta, rays for sliding pieces |
| 02-magic-bitboards         | Magic bitboards for sliding pieces |
| 03-move-ordering           | Captures ordered by victim and attacker, then promotions |
| 04-transposition-table     | Transposition table with bounds |
| 05-quiescence              | Quiescence search |
| 06-iterative-deepening-pvs | Iterative deepening, principal variation search |
| 07-pruning-and-reductions  | Null move, reductions, killers, history, check extension |
| 08-search-refinements      | Exchange evaluation, futility pruning, aspiration, counter moves |
| 09-tuned-evaluation        | Tuned evaluation, tuned margins, history gravity |

Note: this page refers to the steps by their numbers, such as step 03 for 03-move-ordering

The adapter in [bench/legacy](../bench/legacy) compiles step 00 without its window and audio.
Every step has a folder under [baselines](baselines) with these files:

- `environment.json`: the machine, the compiler, the load and the source state.
- `rules.json`: the perft speed and correctness, and the times of move generation and slider
  lookup.
- `eval.json`: the time of one evaluation.
- `move_selection.csv`: one row for each depth and position.

The tables below come from `scripts/report.py`.

## Methodology

Every run is pinned to one efficiency core (core 24) of an i9-13900HX with the `powersave`
governor. The times describe this CPU and do not carry over to other hardware. Node counts do not
vary between runs. The search timings of steps 02 to 08 come from one run of `bench_search`
([benchmark_steps.sh](../scripts/benchmark_steps.sh)), which searches with every step in turn,
round by round, so their ratios are comparable. Perft counts the positions that move generation
reaches at a given depth, and a run is correct when its count equals the published count. The
runs cover six reference positions, and step 00 gives a wrong count on three of them, because of
castling and promotion.

A match has 200 games from 100 random openings, each played with both colors at 50ms per move.
The score is the wins plus half the draws, divided by the number of games, with a 95% interval.
A search without iterative deepening cannot use a clock, so it plays to a fixed depth
([run_matches.sh](../scripts/run_matches.sh)). The match files name each side by its preset:
baseline is step 01, ordering is step 03, tt is step 04, quiescence is step 05, iterative is
step 06, pruning is step 07, full is step 08 and tuned is step 09.

Every change of step 08 was tested alone against the engine without it. A change had to solve all
1,404 mate puzzles (no more than its base did, for the changes of step 09), and then play a match
of 50ms per move with a sequential test that stops as soon as it can tell whether the change is at
least 10 Elo stronger. The test counts whole pairs of games, accepts a wrong answer in 5% of the
cases at most, and ends at 3,000 games if it has no answer. A change was kept when the test
accepted it, or when it ended without an answer, with a score of at least 0.5 and clearly fewer
positions at equal depth.

The numbers of the evaluation in step 09 were fitted by the Texel method on 695,000 quiet
positions with the result of the game they came from, played by the engine against itself at
depth 7. A fifth of the games were held out, and the fit stopped when the error on them rose. The
tables were tuned first, and then each new term was added and tuned alone. Each stage was kept
when a 50ms match of the new numbers against the old ones, with the same sequential test, accepted
it or ended with a score of at least 0.5. The search margins of step 09 were tuned with SPSA on
the tuned evaluation. The rows of step 09 come from [benchmark.sh](../scripts/benchmark.sh), and
the ratio to step 08 was checked in one interleaved run of `bench_search`.

## Findings

### Step 00

- Step 00 needs 24 seconds on average to search depth 3, and 55 seconds from opening positions.
- Step 01 needs 0.8ms at depth 3, and step 08 reaches depth 10 in 17ms.
- At depth 3, step 00 visits 159,004 positions and step 01 visits 5,849, which is 27 times fewer.
  Step 00 shuffles its moves, restarts the window at each root move, and tests for checkmate at
  every node.

### Steps 02 to 07

- **Step 02** is 1.3 to 1.4 times faster than step 01 at every depth. One slider lookup is 26 to
  29 times faster.
- **Step 03** gives the largest gain. A search of a given depth takes 1/3 to 1/40 of the time of
  step 02.
- **Step 04** gives no gain to a search of fixed depth from an empty table. The node counts
  barely move, and probing costs up to 7% of the time. The table helps across the passes of
  iterative deepening and across a game, because stored moves order the next search.
- **Step 05** is 1.7 to 4 times slower than step 04 at equal depth. At depth 4, it scores 92% of
  the points against step 04 (180 wins, 8 draws, 12 losses).
- **Step 06** is 1.4 to 3.3 times faster than step 05 at equal depth. At 50ms per move, it scores
  59% of the points against step 05 at depth 5 (98 wins, 39 draws, 63 losses).
- **Step 07** is 1.5 to 4.6 times faster than step 06 at depths 4 to 6. At 50ms per move, it
  scores 75% of the points against step 06 (135 wins, 29 draws, 36 losses). Against step 01 at
  depth 4, it scores 96.3% of the points (191 wins, 3 draws, 6 losses).

### Step 08

- **Step 08** is 2.1 to 4.2 times faster than step 07 at depths 3 to 6, and 4.7 times faster at
  depth 10 (17.2ms against 80.4ms). It searches 6.5 times fewer positions at depth 10 (61,642
  against 401,971).
- At 50ms per move, step 08 scores 72% of the points against step 07 (252 wins, 74 draws, 74
  losses in 400 games).
- Seven of the first 13 changes were kept. Reverse futility pruning is the largest: it searches
  63% fewer positions and scores 58% of the points. Futility pruning, exchange evaluation,
  aspiration windows, counter moves and losing captures late each score 52% to 55%.
- Six changes did not score above 0.5: delta pruning, late move pruning, root entropy, the
  history shared by position, the inertness probe and the learned policy. Five ended without an
  answer in 3,000 games: razoring, the logarithmic reduction table, internal iterative
  reduction, singular extensions and the window sized by variance. The last one scored 0.511
  with 9% fewer positions, and it missed one of the mate puzzles.
- Two threads on one table (Lazy SMP) score 59% of the points against one thread at the same
  time per move. It is off by default, because the server runs four requests on two vCPUs.
- Tuning the numbers with SPSA (120 iterations) moved four of five parameters by less than 7%,
  and the tuned engine scored 0.506 over 3,000 games, which is not a difference.
- A build for CPUs with BMI2 finds slider attacks with PEXT and searches 5% to 12% more
  positions per second.

### Step 09

- **Step 09** scores 65% of the points against step 08 at 50ms per move (236 wins, 49 draws, 115
  losses in 400 games, 95% interval 0.608 to 0.695, about +108 Elo).
- The evaluation costs more. One evaluation takes 133ns against 62ns, and the engine searches 2.8
  million positions per second against 3.4 million. At equal depth, step 09 takes 1.25 to 1.35
  times as long as step 08 at depths 5 to 9. At depth 10, the interleaved run gives 19.2ms against
  18.0ms, because step 09 searches 14% fewer positions (52,792 against 61,642).
- The evaluation stages, each against the numbers before it:

  | Stage | Score | Games | Result |
  |:------|------:|------:|:-------|
  | Tuned tables (material, piece-square, king) | 0.626 | 254 | kept |
  | Tempo and bishop pair | 0.522 | 1,962 | kept |
  | Passed pawns | 0.505 | 3,000 | no decision, one puzzle missed |
  | Doubled and isolated pawns | 0.495 | 2,582 | not better |
  | Rooks on open and half-open files | 0.535 | 1,094 | kept |
  | Mobility | 0.519 | 2,666 | kept |
  | Tuning all groups again, on 1.35 million positions | 0.574 | 452 | not kept, one puzzle missed |

- The last row is the largest score that was not kept. The retuned numbers play the mating move
  of the missed puzzle from depth 4, and report the mate two plies later than the test allows, so
  the rule rejected it. It is the one decision where the puzzle test was the only reason.
- Changes to the move ordering, against step 08 (before the tuned evaluation):

  | Change | Score | Games | Result |
  |:-------|------:|------:|:-------|
  | History malus (failed quiet moves lose score) | 0.527 | 1,678 | better, one puzzle missed |
  | History malus at half strength | 0.530 | 1,296 | better |
  | History gravity | 0.505 | 3,000 | no decision |
  | History by piece and destination | 0.483 | 1,266 | not better |
  | Capture history | 0.492 | 2,192 | not better |
  | Continuation score two plies back | 0.500 | 3,000 | no decision |
  | Counter move before the killers | 0.478 | 972 | not better, one puzzle missed |
  | One killer move | 0.485 | 1,438 | not better |
  | Victim weight 8 and 64 against 16 | 0.467 and 0.475 | 704 and 914 | not better |
  | Late losing captures at margin 50 and 100 | 0.504 and 0.509 | 3,000 | no decision |
  | Late underpromotions | 0.494 | 2,300 | not better, one puzzle missed |

  On the tuned evaluation the history malus scores 0.506 over 3,000 games and misses one puzzle,
  and history gravity scores 0.533 over 1,178 games.
- The tuned margins (reverse futility 107, futility 74 and 214, null move step 246, aspiration
  18) score 0.511 against the old margins on the tuned evaluation over 3,000 games, which the
  test accepted by its rule for a tie. History gravity scores 0.533 over 1,178 games and was
  kept. The history malus scores 0.506 over 3,000 games, misses one puzzle, and was not kept.

### Stockfish

- Step 07 rates 2307 against Stockfish 19 limited with `UCI_Elo`, at 1000ms per move. The 95%
  interval is 2270 to 2345, from 600 games.
- Step 08 rates 2444 on the same scale and at the same time control (interval 2406 to 2481, 600
  games). It scores 76% of the points against `UCI_Elo` 2000, 64% against 2200 and 63% against
  2400. It loses above that: 40% against 2600, 23% against 2800 and 10% against 3000.
- Step 09 rates 2602 on the same scale and at the same time control (interval 2564 to 2640, 600
  games). It scores 87.5% of the points against `UCI_Elo` 2000, 84% against 2200, 73.5% against
  2400 and 56% against 2600. It loses above that: 31.5% against 2800 and 15.5% against 3000.
- Per node, step 09 is faster than Stockfish: about 2.8 million nodes per second against 0.6 to
  1.6 million.
- Stockfish needs far fewer nodes for a depth: 2,300 against 52,792 at depth 10. It reaches
  depth 10 7.6 times faster than step 09 (2.5ms against 19ms).

### Opening book

The book has 39,586 moves for 15,584 positions, up to 10 plies deep. Step 08 with the book
scored 0.496 (123 wins, 151 draws, 126 losses, interval 0.458 to 0.535) against step 08 without
it, over 400 games at 50ms per move from the initial position. The book does not change the
strength of the engine. The match is in
[book-50ms-vs-no-book-50ms.txt](matches/book-50ms-vs-no-book-50ms.txt).

## Tables

### Rules

The table shows the speed of move generation and the correctness of every step.

- **Perft Mnps** is the median speed over the six reference positions, in millions of nodes per
  second. Each position uses the shallowest depth that has two million leaves or more. Step 00
  uses depths 1 to 4, with counts under five million.
- **Movegen ns** is the time to generate the legal moves of one position.
- **make_move ns** is the time of one pseudo-legal move.
- **Eval ns** is the time of one evaluation.
- **Correct** counts the perft runs whose node count equals the published count.

| Step                       | Perft Mnps | Movegen ns | make_move ns | Eval ns |  Correct |
|:---------------------------|-----------:|-----------:|-------------:|--------:|---------:|
| 00-original-engine         |       0.07 |        n/a |          n/a |     n/a | 13 of 24 |
| 01-bitboards-ray-attacks   |      27.84 |       1059 |         33.8 |      60 |   6 of 6 |
| 02-magic-bitboards         |      44.18 |        655 |         20.4 |      60 |   6 of 6 |
| 03-move-ordering           |      44.20 |        656 |         20.4 |      60 |   6 of 6 |
| 04-transposition-table     |      44.35 |        677 |         21.2 |      60 |   6 of 6 |
| 05-quiescence              |      44.28 |        676 |         21.1 |      60 |   6 of 6 |
| 06-iterative-deepening-pvs |      44.18 |        674 |         21.1 |      60 |   6 of 6 |
| 07-pruning-and-reductions  |      43.55 |        704 |         22.0 |      60 |   6 of 6 |
| 08-search-refinements      |      44.12 |        658 |         20.5 |      62 |   6 of 6 |
| 09-tuned-evaluation        |      44.24 |        655 |         20.6 |     133 |   6 of 6 |

### Search time

The table shows the mean milliseconds per move over the 15 suite positions (5 opening, 5
middlegame, 5 endgame). Every search starts from an empty transposition table. A dash means that
the step was not measured at that depth.

| Step                       |   d1 |    d2 |     d3 |   d4 |    d5 |
|:---------------------------|-----:|------:|-------:|-----:|------:|
| 00-original-engine         | 10.0 | 357.2 | 24,379 |    - |     - |
| 01-bitboards-ray-attacks   | 0.01 |  0.06 |   0.82 | 9.10 | 136.4 |
| 02-magic-bitboards         | 0.01 |  0.05 |   0.62 | 6.47 |  98.5 |
| 03-move-ordering           | 0.01 |  0.03 |   0.18 | 0.84 |  7.24 |
| 04-transposition-table     | 0.01 |  0.03 |   0.18 | 0.90 |  7.72 |
| 05-quiescence              | 0.06 |  0.20 |   0.59 | 3.63 |  13.2 |
| 06-iterative-deepening-pvs | 0.06 |  0.18 |   0.43 | 1.80 |  5.43 |
| 07-pruning-and-reductions  | 0.07 |  0.19 |   0.51 | 1.21 |  2.50 |
| 08-search-refinements      | 0.03 |  0.10 |   0.25 | 0.47 |  0.71 |
| 09-tuned-evaluation        | 0.04 |  0.13 |   0.25 | 0.64 |  0.96 |

| Step                       |    d6 |   d7 |    d8 |    d9 |   d10 |
|:---------------------------|------:|-----:|------:|------:|------:|
| 01-bitboards-ray-attacks   | 1,609 |    - |     - |     - |     - |
| 02-magic-bitboards         | 1,150 |    - |     - |     - |     - |
| 03-move-ordering           |  29.0 |    - |     - |     - |     - |
| 04-transposition-table     |  30.1 |    - |     - |     - |     - |
| 05-quiescence              |  89.1 |    - |     - |     - |     - |
| 06-iterative-deepening-pvs |  26.8 | 79.2 | 346.9 | 1,143 | 5,306 |
| 07-pruning-and-reductions  |  5.79 | 10.5 |  20.8 |  40.5 |  80.4 |
| 08-search-refinements      |  1.39 | 2.35 |  4.34 |  8.41 |  17.2 |
| 09-tuned-evaluation        |  1.82 | 3.29 |  6.01 |  10.9 |  19.0 |

### Search nodes

The table shows the mean nodes per move over the same 15 positions, with the same conditions.

| Step                       |    d1 |    d2 |      d3 |     d4 |      d5 |
|:---------------------------|------:|------:|--------:|-------:|--------:|
| 00-original-engine         | 1,034 | 6,993 | 159,004 |      - |       - |
| 01-bitboards-ray-attacks   |    29 |   360 |   5,849 | 62,039 | 965,242 |
| 02-magic-bitboards         |    29 |   360 |   5,849 | 62,039 | 965,242 |
| 03-move-ordering           |    29 |   108 |   1,192 |  4,892 |  52,540 |
| 04-transposition-table     |    29 |   108 |   1,192 |  4,873 |  52,119 |
| 05-quiescence              |   238 |   859 |   2,851 | 18,629 |  73,783 |
| 06-iterative-deepening-pvs |   247 |   739 |   2,010 |  8,746 |  30,192 |
| 07-pruning-and-reductions  |   244 |   767 |   2,341 |  5,493 |  11,830 |
| 08-search-refinements      |    80 |   316 |     779 |  1,527 |   2,325 |
| 09-tuned-evaluation        |    90 |   326 |     667 |  1,830 |   2,687 |

| Step                       |         d6 |      d7 |        d8 |        d9 |        d10 |
|:---------------------------|-----------:|--------:|----------:|----------:|-----------:|
| 01-bitboards-ray-attacks   | 10,959,617 |       - |         - |         - |          - |
| 02-magic-bitboards         | 10,959,617 |       - |         - |         - |          - |
| 03-move-ordering           |    178,062 |       - |         - |         - |          - |
| 04-transposition-table     |    166,960 |       - |         - |         - |          - |
| 05-quiescence              |    471,282 |       - |         - |         - |          - |
| 06-iterative-deepening-pvs |    132,262 | 460,053 | 1,782,208 | 6,507,895 | 26,379,128 |
| 07-pruning-and-reductions  |     27,082 |  52,488 |   104,550 |   203,616 |    401,971 |
| 08-search-refinements      |      4,335 |   8,333 |    15,464 |    29,653 |     61,642 |
| 09-tuned-evaluation        |      5,157 |   9,458 |    17,101 |    30,864 |     52,792 |

### Speedup

Each ratio is the time of the previous step divided by the time of the step at the same depth. A
ratio above 1 means that the step is faster.

| Step                       |                       Over |      d3 |    d4 |     d5 |     d6 |
|:---------------------------|---------------------------:|--------:|------:|-------:|-------:|
| 01-bitboards-ray-attacks   |         00-original-engine | 29,560x |     - |      - |      - |
| 02-magic-bitboards         |   01-bitboards-ray-attacks |   1.34x | 1.41x |  1.38x |  1.40x |
| 03-move-ordering           |         02-magic-bitboards |   3.43x | 7.73x | 13.60x | 39.64x |
| 04-transposition-table     |           03-move-ordering |   1.00x | 0.93x |  0.94x |  0.97x |
| 05-quiescence              |     04-transposition-table |   0.31x | 0.25x |  0.58x |  0.34x |
| 06-iterative-deepening-pvs |              05-quiescence |   1.36x | 2.01x |  2.44x |  3.32x |
| 07-pruning-and-reductions  | 06-iterative-deepening-pvs |   0.84x | 1.49x |  2.17x |  4.63x |
| 08-search-refinements      |  07-pruning-and-reductions |   2.06x | 2.55x |  3.51x |  4.18x |
| 09-tuned-evaluation        |      08-search-refinements |   0.99x | 0.74x |  0.74x |  0.76x |

### Game phases

The table shows the mean milliseconds per move at depth 3 for each class of position.

| Step                       | Opening | Middlegame | Endgame |
|:---------------------------|--------:|-----------:|--------:|
| 00-original-engine         |  55,165 |     17,518 |   454.7 |
| 01-bitboards-ray-attacks   |    1.51 |       0.86 |    0.10 |
| 02-magic-bitboards         |    1.16 |       0.62 |    0.07 |
| 03-move-ordering           |    0.31 |       0.17 |    0.06 |
| 04-transposition-table     |    0.31 |       0.17 |    0.06 |
| 05-quiescence              |    0.80 |       0.79 |    0.17 |
| 06-iterative-deepening-pvs |    0.73 |       0.48 |    0.08 |
| 07-pruning-and-reductions  |    0.80 |       0.61 |    0.12 |
| 08-search-refinements      |    0.38 |       0.24 |    0.12 |
| 09-tuned-evaluation        |    0.40 |       0.23 |    0.12 |

### Search refinements

Every change of the second round, against the engine without it. **Games** is the length of the
match, **Score** the share of the points of the engine with the change, **Verdict** the answer of
the sequential test, **Nodes** the positions searched at the deepest depth of the benchmark
relative to the engine without the change, and **Missed** the number of mate puzzles that the
change failed to solve. The changes with a score under 0.5 lost points.

| Change                            | Games | Score |     Verdict | Nodes | Missed | Kept |
|:----------------------------------|------:|------:|------------:|------:|-------:|-----:|
| Exchange evaluation of captures   |  1172 | 0.537 |      better |  0.75 |      0 |  yes |
| Delta pruning                     |  1934 | 0.491 |  not better |  0.86 |      0 |   no |
| Losing captures after quiet moves |  1452 | 0.530 |      better |  1.14 |      0 |  yes |
| Reverse futility pruning          |   390 | 0.582 |      better |  0.37 |      0 |  yes |
| Futility pruning                  |   640 | 0.552 |      better |  0.55 |      0 |  yes |
| Razoring                          |  3000 | 0.504 | no decision |  0.97 |      0 |   no |
| Late move pruning                 |   348 | 0.425 |  not better |  0.84 |      0 |   no |
| Logarithmic reduction table       |  3000 | 0.511 | no decision |  1.19 |      0 |   no |
| Internal iterative reduction      |  3000 | 0.500 | no decision |  1.00 |      0 |   no |
| Null move reduction by margin     |  3000 | 0.506 | no decision |  0.83 |      0 |  yes |
| Aspiration windows                |  1996 | 0.523 |      better |  1.12 |      0 |  yes |
| Singular extensions               |  3000 | 0.514 | no decision |  1.00 |      0 |   no |
| Counter moves, continuation score |  1078 | 0.535 |      better |  0.97 |      0 |  yes |
| Hash of 64MB against 16MB         |   800 | 0.506 | no decision |  1.00 |      0 |   no |
| Two-way table buckets             |   800 | 0.497 | no decision |  1.00 |      0 |   no |
| Lazy SMP, 2 threads               |   332 | 0.592 |      better |  0.87 |      0 |   no |
| Root entropy                      |  2536 | 0.495 |  not better |  0.97 |      0 |   no |
| History shared by position        |  1446 | 0.487 |  not better |  1.00 |      0 |   no |
| Window sized by variance          |  3000 | 0.511 | no decision |  0.91 |      1 |   no |
| Inertness probe                   |  2912 | 0.497 |  not better |  0.99 |      0 |   no |
| Learned policy, scale 16          |   914 | 0.474 |  not better |  1.03 |      1 |   no |
| Learned policy, scale 4           |  2594 | 0.496 |  not better |  1.00 |      0 |   no |
| Learned policy, scale 48          |  1868 | 0.492 |  not better |  1.00 |      0 |   no |

### Hash size and replacement

A table of 64MB against one of 16MB, at 200ms per move over 800 games, scores 0.506. Two-way
buckets with tables of 4MB score 0.497 over 800 games. Neither is a difference that the test
could separate from noise.

### Matches

Every match has 200 games, except the first two, which have 400. A positive Elo means that the
first side is stronger.

| Match                                    | Wins | Draws | Losses | Score |   95% interval |  Elo |
|:-----------------------------------------|-----:|------:|-------:|------:|---------------:|-----:|
| tuned (50ms) vs full (50ms)              |  236 |    49 |    115 | 0.651 | 0.608 to 0.695 | +108 |
| full (50ms) vs pruning (50ms)            |  252 |    74 |     74 | 0.723 | 0.684 to 0.761 | +166 |
| iterative (50ms) vs quiescence (depth 5) |   98 |    39 |     63 | 0.588 | 0.527 to 0.648 |  +61 |
| pruning (50ms) vs baseline (depth 4)     |  191 |     3 |      6 | 0.963 | 0.938 to 0.987 | +564 |
| pruning (50ms) vs iterative (50ms)       |  135 |    29 |     36 | 0.748 | 0.693 to 0.802 | +189 |
| quiescence (depth 4) vs tt (depth 4)     |  180 |     8 |     12 | 0.920 | 0.885 to 0.955 | +424 |

Steps 07, 08 and 09 also play Stockfish 19 at equal time per move. Each table has 50 games per level
of `UCI_Elo`. The last row joins the levels into one rating estimate with a 95% interval.

#### Step 07, 1000ms per move, seed 21

| UCI_Elo | Games | Wins | Draws | Losses | Score |              Rating |
|:--------|------:|-----:|------:|-------:|------:|--------------------:|
| 1800    |    50 |   39 |     3 |      8 | 0.810 | 2052 (1953 to 2209) |
| 2000    |    50 |   39 |     1 |     10 | 0.790 | 2230 (2130 to 2384) |
| 2200    |    50 |   36 |     1 |     13 | 0.730 | 2373 (2277 to 2503) |
| 2400    |    50 |   18 |     0 |     32 | 0.360 | 2300 (2187 to 2395) |
| 2600    |    50 |   13 |     3 |     34 | 0.290 | 2444 (2323 to 2538) |
| 2800    |    50 |    4 |     4 |     42 | 0.120 | 2454 (2243 to 2560) |
| All     |   300 |  149 |    12 |    139 | 0.517 | 2321 (2268 to 2374) |

#### Step 07, 1000ms per move, seed 22

| UCI_Elo | Games | Wins | Draws | Losses | Score |              Rating |
|:--------|------:|-----:|------:|-------:|------:|--------------------:|
| 1800    |    50 |   42 |     0 |      8 | 0.840 | 2088 (1980 to 2283) |
| 2000    |    50 |   33 |     1 |     16 | 0.670 | 2123 (2029 to 2240) |
| 2200    |    50 |   27 |     1 |     22 | 0.550 | 2235 (2139 to 2336) |
| 2400    |    50 |   24 |     2 |     24 | 0.500 | 2400 (2303 to 2497) |
| 2600    |    50 |   16 |     1 |     33 | 0.330 | 2477 (2360 to 2571) |
| 2800    |    50 |    3 |     2 |     45 | 0.080 | 2376 (2003 to 2499) |
| All     |   300 |  145 |     7 |    148 | 0.495 | 2294 (2240 to 2347) |

#### Step 07, 200ms per move

| UCI_Elo | Games | Wins | Draws | Losses | Score |              Rating |
|:--------|------:|-----:|------:|-------:|------:|--------------------:|
| 1320    |    50 |   50 |     0 |      0 | 1.000 | 2640 (2640 to 2640) |
| 1600    |    50 |   45 |     1 |      4 | 0.910 | 2002 (1879 to 2350) |
| 1900    |    50 |   32 |     1 |     17 | 0.650 | 2008 (1913 to 2121) |
| 2200    |    50 |   20 |     2 |     28 | 0.420 | 2144 (2041 to 2238) |
| 2500    |    50 |   21 |     2 |     27 | 0.440 | 2458 (2357 to 2552) |
| 2800    |    50 |    3 |     2 |     45 | 0.080 | 2376 (2003 to 2499) |
| 3190    |    50 |    2 |     1 |     47 | 0.050 | 2678 (1870 to 2822) |
| All     |   350 |  173 |     9 |    168 | 0.507 | 2216 (2152 to 2279) |

#### Step 08, 1000ms per move, seed 21

| UCI_Elo | Games | Wins | Draws | Losses | Score |              Rating |
|:--------|------:|-----:|------:|-------:|------:|--------------------:|
| 2000    |    50 |   38 |     0 |     12 | 0.760 | 2200 (2101 to 2343) |
| 2200    |    50 |   32 |     2 |     16 | 0.660 | 2315 (2222 to 2428) |
| 2400    |    50 |   34 |     2 |     14 | 0.700 | 2547 (2453 to 2668) |
| 2600    |    50 |   20 |     2 |     28 | 0.420 | 2544 (2441 to 2638) |
| 2800    |    50 |   12 |     4 |     34 | 0.280 | 2636 (2514 to 2728) |
| 3000    |    50 |    3 |     1 |     46 | 0.070 | 2551 (1925 to 2682) |
| All     |   300 |  139 |    11 |    150 | 0.482 | 2477 (2423 to 2530) |

#### Step 08, 1000ms per move, seed 22

| UCI_Elo | Games | Wins | Draws | Losses | Score |              Rating |
|:--------|------:|-----:|------:|-------:|------:|--------------------:|
| 2000    |    50 |   38 |     0 |     12 | 0.760 | 2200 (2101 to 2343) |
| 2200    |    50 |   30 |     1 |     19 | 0.610 | 2278 (2183 to 2385) |
| 2400    |    50 |   27 |     1 |     22 | 0.550 | 2435 (2339 to 2536) |
| 2600    |    50 |   17 |     3 |     30 | 0.370 | 2508 (2400 to 2600) |
| 2800    |    50 |    7 |     3 |     40 | 0.170 | 2525 (2355 to 2626) |
| 3000    |    50 |    2 |     8 |     40 | 0.120 | 2654 (2485 to 2749) |
| All     |   300 |  121 |    16 |    163 | 0.430 | 2411 (2357 to 2464) |

#### Step 09, 1000ms per move, seed 21

| UCI_Elo | Games | Wins | Draws | Losses | Score |              Rating |
|:--------|------:|-----:|------:|-------:|------:|--------------------:|
| 2000    |    50 |   43 |     2 |      5 | 0.880 | 2346 (2235 to 2580) |
| 2200    |    50 |   38 |     5 |      7 | 0.810 | 2452 (2356 to 2601) |
| 2400    |    50 |   36 |     2 |     12 | 0.740 | 2582 (2486 to 2713) |
| 2600    |    50 |   25 |     1 |     24 | 0.510 | 2607 (2510 to 2705) |
| 2800    |    50 |   13 |     6 |     31 | 0.320 | 2669 (2559 to 2758) |
| 3000    |    50 |    5 |     5 |     40 | 0.150 | 2699 (2526 to 2799) |
| All     |   300 |  160 |    21 |    119 | 0.568 | 2587 (2534 to 2641) |

#### Step 09, 1000ms per move, seed 22

| UCI_Elo | Games | Wins | Draws | Losses | Score |              Rating |
|:--------|------:|-----:|------:|-------:|------:|--------------------:|
| 2000    |    50 |   43 |     1 |      6 | 0.870 | 2330 (2219 to 2557) |
| 2200    |    50 |   43 |     1 |      6 | 0.870 | 2530 (2419 to 2757) |
| 2400    |    50 |   36 |     1 |     13 | 0.730 | 2573 (2477 to 2703) |
| 2600    |    50 |   30 |     1 |     19 | 0.610 | 2678 (2583 to 2785) |
| 2800    |    50 |   12 |     7 |     31 | 0.310 | 2661 (2551 to 2749) |
| 3000    |    50 |    5 |     6 |     39 | 0.160 | 2712 (2552 to 2809) |
| All     |   300 |  169 |    17 |    114 | 0.592 | 2617 (2564 to 2671) |

## Comparison with Stockfish 19

### Strength

Step 07 plays Stockfish at 1000ms per move. Stockfish is limited with `UCI_LimitStrength` and
`UCI_Elo`. The table adds the two runs of 50 games per level
([seed 21](matches/vs-stockfish-1000ms-seed21.txt) and
[seed 22](matches/vs-stockfish-1000ms-seed22.txt)).

| UCI_Elo | Games | Wins | Draws | Losses | Score |
|--------:|------:|-----:|------:|-------:|------:|
|    1800 |   100 |   81 |     3 |     16 | 0.825 |
|    2000 |   100 |   72 |     2 |     26 | 0.730 |
|    2200 |   100 |   63 |     2 |     35 | 0.640 |
|    2400 |   100 |   42 |     2 |     56 | 0.430 |
|    2600 |   100 |   29 |     4 |     67 | 0.310 |
|    2800 |   100 |    7 |     6 |     87 | 0.100 |

The two runs give 2321 and 2294. Together they give 2307, with a 95% interval of 2270 to 2345.
The counts are in [vs-stockfish-1000ms.csv](matches/vs-stockfish-1000ms.csv), and
[rating_fit.py](../scripts/rating_fit.py) computes the rating from them:

```bash
scripts/rating_fit.py results/matches/vs-stockfish-1000ms.csv
```

Step 08 plays the same opponent at 1000ms per move, at the levels 2000 to 3000, with 50 games
per level in each of two runs
([seed 21](matches/full-vs-stockfish-1000ms-seed21.txt) and
[seed 22](matches/full-vs-stockfish-1000ms-seed22.txt)).

| UCI_Elo | Games | Wins | Draws | Losses | Score |
|--------:|------:|-----:|------:|-------:|------:|
|    2000 |   100 |   76 |     0 |     24 | 0.760 |
|    2200 |   100 |   62 |     3 |     35 | 0.635 |
|    2400 |   100 |   61 |     3 |     36 | 0.625 |
|    2600 |   100 |   37 |     5 |     58 | 0.395 |
|    2800 |   100 |   19 |     7 |     74 | 0.225 |
|    3000 |   100 |    5 |     9 |     86 | 0.095 |

The two runs give 2477 and 2411. Together they give 2444, with a 95% interval of 2406 to 2481.
The counts are in [full-vs-stockfish-1000ms.csv](matches/full-vs-stockfish-1000ms.csv):

```bash
scripts/rating_fit.py results/matches/full-vs-stockfish-1000ms.csv
```

Step 09 plays the same opponent in the same way
([seed 21](matches/tuned-vs-stockfish-1000ms-seed21.txt) and
[seed 22](matches/tuned-vs-stockfish-1000ms-seed22.txt)).

| UCI_Elo | Games | Wins | Draws | Losses | Score |
|--------:|------:|-----:|------:|-------:|------:|
|    2000 |   100 |   86 |     3 |     11 | 0.875 |
|    2200 |   100 |   81 |     6 |     13 | 0.840 |
|    2400 |   100 |   72 |     3 |     25 | 0.735 |
|    2600 |   100 |   55 |     2 |     43 | 0.560 |
|    2800 |   100 |   25 |    13 |     62 | 0.315 |
|    3000 |   100 |   10 |    11 |     79 | 0.155 |

The two runs give 2587 and 2617. Together they give 2602, with a 95% interval of 2564 to 2640.
The counts are in [tuned-vs-stockfish-1000ms.csv](matches/tuned-vs-stockfish-1000ms.csv):

```bash
scripts/rating_fit.py results/matches/tuned-vs-stockfish-1000ms.csv
```

The scale is the `UCI_Elo` scale of Stockfish. Stockfish calibrates it for longer time controls,
so the number is a rating on that scale at this speed. It is not a rating-list rating.

### Speed

Stockfish 19 runs with one thread and a 16MB hash, pinned to core 24. It searches the 15 suite
positions from an empty hash ([stockfish-search.csv](comparison/stockfish-search.csv)). The
table compares it with step 09.

| Depth | Stockfish ms | Step 09 ms | Ratio | Stockfish nodes | Step 09 nodes |
|------:|-------------:|-----------:|------:|----------------:|--------------:|
|     1 |         0.43 |       0.04 |  0.1x |              53 |            90 |
|     2 |         0.47 |       0.13 |  0.3x |             115 |           326 |
|     3 |         0.51 |       0.25 |  0.5x |             169 |           667 |
|     4 |          0.6 |       0.64 |  1.1x |             273 |         1,830 |
|     5 |          0.8 |       0.96 |  1.2x |             469 |         2,687 |
|     6 |         0.95 |        1.8 |  1.9x |             646 |         5,157 |
|     7 |          1.1 |        3.3 |  3.0x |             892 |         9,458 |
|     8 |          1.5 |        6.0 |  4.0x |           1,234 |        17,101 |
|     9 |          1.7 |       10.9 |  6.4x |           1,494 |        30,864 |
|    10 |          2.5 |       19.0 |  7.6x |           2,299 |        52,792 |

The ratio is the time of step 09 divided by the time of Stockfish. Each Stockfish search has a
fixed cost of about 0.3ms. In steady state (3s searches on three positions), Stockfish runs at
0.6 to 1.6 million nodes per second on this core, and step 09 runs at about 2.8 million.

## Rating of step 00

One logistic fit combines these players:

- step 00 at depth 3, which is the depth of the desktop game
- step 01 at depths 1 to 3
- Stockfish 19 at `UCI_Elo` 1320 to 1800, with 100ms per move

Step 01 plays 50 games per level, and step 00 plays 48 games. The fit keeps the ratings of
Stockfish fixed and finds the other ratings. The games are in
[matches/original](matches/original). The counts of wins, draws and losses of every pair are in
[games.csv](matches/original/games.csv), and [rating_fit.py](../scripts/rating_fit.py) computes
the fit from them:

```bash
scripts/rating_fit.py results/matches/original/games.csv
```

| Player        | Rating | 95% interval |
|:--------------|-------:|:-------------|
| step00-depth3 |   1132 | 1044 to 1220 |
| step01-depth1 |    584 | 337 to 832   |
| step01-depth2 |    992 | 894 to 1090  |
| step01-depth3 |   1213 | 1143 to 1284 |

Step 00 at depth 3 also plays step 01 directly. It scores +22 =2 -0 against depth 1, +13 =6 -5
against depth 2, and +2 =7 -15 against depth 3.

Against Stockfish 1320 and 1500 alone, step 00 scores 0.375 and 0.167. This gives a rating of
1227 (interval 1114 to 1341). The fit weighs this result against the direct matches.

Step 00 makes no illegal move in any game. The ratings are on the `UCI_Elo` scale of Stockfish
at short time controls. Steps 07 and 08 at 1000ms per move rate 2307 and 2444 on the same scale,
but at a longer time control.
