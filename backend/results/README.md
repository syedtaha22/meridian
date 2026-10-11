# Results

This folder holds the recorded benchmarks and matches of the engine. The engine grew in eight
steps. Step 00 is the original desktop engine, and each later step adds one technique to the
step before it. Every step runs on the position suites in
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
vary between runs. The search timings of steps 02 to 07 come from one run of `bench_search`
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
step 06 and full is step 07.

## Findings

### Step 00

- Step 00 needs 24 seconds on average to search depth 3, and 55 seconds from opening positions.
- Step 01 needs 0.8ms at depth 3, and step 07 reaches depth 10 in 75ms.
- At depth 3, step 00 visits 159,004 positions and step 01 visits 5,849, which is 27 times fewer.
  Step 00 shuffles its moves, restarts the window at each root move, and tests for checkmate at
  every node.

### Steps 02 to 07

- **Step 02** is 1.4 to 1.5 times faster than step 01 at every depth. One slider lookup is 26 to
  29 times faster.
- **Step 03** gives the largest gain. A search of a given depth takes 1/4 to 1/40 of the time of
  step 02.
- **Step 04** gives no gain to a search of fixed depth from an empty table. The node counts
  barely move, and probing costs up to 4% of the time. The table helps across the passes of
  iterative deepening and across a game, because stored moves order the next search.
- **Step 05** is 2 to 4 times slower than step 04 at equal depth. At depth 4, it scores 92% of
  the points against step 04 (180 wins, 8 draws, 12 losses).
- **Step 06** is 1.4 to 3.5 times faster than step 05 at equal depth. At 50ms per move, it scores
  59% of the points against step 05 at depth 5 (98 wins, 39 draws, 63 losses).
- **Step 07** is 1.5 to 4.6 times faster than step 06 at depths 4 to 6. At 50ms per move, it
  scores 75% of the points against step 06 (135 wins, 29 draws, 36 losses). Against step 01 at
  depth 4, it scores 96.3% of the points (191 wins, 3 draws, 6 losses).

### Stockfish

- Step 07 rates 2307 against Stockfish 19 limited with `UCI_Elo`, at 1000ms per move. The 95%
  interval is 2270 to 2345, from 600 games.
- Per node, step 07 is faster than Stockfish: 5.3 million nodes per second against 0.6 to 1.6
  million.
- Stockfish needs far fewer nodes for a depth: 2,300 against 401,971 at depth 10. It reaches
  depth 10 30 times faster on average, and 8 times faster for the median position.

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

### Search time

The table shows the mean milliseconds per move over the 15 suite positions (5 opening, 5
middlegame, 5 endgame). Every search starts from an empty transposition table. A dash means that
the step was not measured at that depth.

| Step                       |   d1 |    d2 |     d3 |   d4 |    d5 |
|:---------------------------|-----:|------:|-------:|-----:|------:|
| 00-original-engine         | 10.0 | 357.2 | 24,379 |    - |     - |
| 01-bitboards-ray-attacks   | 0.01 |  0.06 |   0.82 | 9.10 | 136.4 |
| 02-magic-bitboards         | 0.01 |  0.04 |   0.58 | 6.20 |  94.1 |
| 03-move-ordering           | 0.01 |  0.02 |   0.16 | 0.76 |  6.84 |
| 04-transposition-table     | 0.01 |  0.02 |   0.16 | 0.80 |  6.98 |
| 05-quiescence              | 0.06 |  0.19 |   0.56 | 3.49 |  12.7 |
| 06-iterative-deepening-pvs | 0.06 |  0.17 |   0.41 | 1.72 |  5.15 |
| 07-pruning-and-reductions  | 0.06 |  0.17 |   0.48 | 1.14 |  2.37 |

| Step                       |    d6 |   d7 |    d8 |    d9 |   d10 |
|:---------------------------|------:|-----:|------:|------:|------:|
| 01-bitboards-ray-attacks   | 1,609 |    - |     - |     - |     - |
| 02-magic-bitboards         | 1,096 |    - |     - |     - |     - |
| 03-move-ordering           |  26.9 |    - |     - |     - |     - |
| 04-transposition-table     |  26.9 |    - |     - |     - |     - |
| 05-quiescence              |  85.2 |    - |     - |     - |     - |
| 06-iterative-deepening-pvs |  24.1 | 73.7 | 323.6 | 1,060 | 4,844 |
| 07-pruning-and-reductions  |  5.28 | 9.89 |  19.5 |  37.7 |  74.5 |

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

| Step                       |         d6 |      d7 |        d8 |        d9 |        d10 |
|:---------------------------|-----------:|--------:|----------:|----------:|-----------:|
| 01-bitboards-ray-attacks   | 10,959,617 |       - |         - |         - |          - |
| 02-magic-bitboards         | 10,959,617 |       - |         - |         - |          - |
| 03-move-ordering           |    178,062 |       - |         - |         - |          - |
| 04-transposition-table     |    166,960 |       - |         - |         - |          - |
| 05-quiescence              |    471,282 |       - |         - |         - |          - |
| 06-iterative-deepening-pvs |    132,262 | 460,053 | 1,782,208 | 6,507,895 | 26,379,128 |
| 07-pruning-and-reductions  |     27,082 |  52,488 |   104,550 |   203,616 |    401,971 |

### Speedup

Each ratio is the time of the previous step divided by the time of the step at the same depth. A
ratio above 1 means that the step is faster.

| Step                       |                       Over |      d3 |    d4 |     d5 |     d6 |
|:---------------------------|---------------------------:|--------:|------:|-------:|-------:|
| 01-bitboards-ray-attacks   |         00-original-engine | 29,560x |     - |      - |      - |
| 02-magic-bitboards         |   01-bitboards-ray-attacks |   1.41x | 1.47x |  1.45x |  1.47x |
| 03-move-ordering           |         02-magic-bitboards |   3.66x | 8.16x | 13.77x | 40.78x |
| 04-transposition-table     |           03-move-ordering |   0.98x | 0.96x |  0.98x |  1.00x |
| 05-quiescence              |     04-transposition-table |   0.29x | 0.23x |  0.55x |  0.32x |
| 06-iterative-deepening-pvs |              05-quiescence |   1.38x | 2.03x |  2.47x |  3.54x |
| 07-pruning-and-reductions  | 06-iterative-deepening-pvs |   0.85x | 1.51x |  2.18x |  4.56x |

### Game phases

The table shows the mean milliseconds per move at depth 3 for each class of position.

| Step                       | Opening | Middlegame | Endgame |
|:---------------------------|--------:|-----------:|--------:|
| 00-original-engine         |  55,165 |     17,518 |   454.7 |
| 01-bitboards-ray-attacks   |    1.51 |       0.86 |    0.10 |
| 02-magic-bitboards         |    1.12 |       0.57 |    0.06 |
| 03-move-ordering           |    0.28 |       0.15 |    0.05 |
| 04-transposition-table     |    0.28 |       0.15 |    0.05 |
| 05-quiescence              |    0.77 |       0.76 |    0.16 |
| 06-iterative-deepening-pvs |    0.70 |       0.45 |    0.07 |
| 07-pruning-and-reductions  |    0.76 |       0.57 |    0.10 |

### Matches

Every match has 200 games. A positive Elo means that the first side is stronger.

| Match                                    | Wins | Draws | Losses | Score |   95% interval |  Elo |
|:-----------------------------------------|-----:|------:|-------:|------:|---------------:|-----:|
| full (50ms) vs baseline (depth 4)        |  191 |     3 |      6 | 0.963 | 0.938 to 0.987 | +564 |
| full (50ms) vs iterative (50ms)          |  135 |    29 |     36 | 0.748 | 0.693 to 0.802 | +189 |
| iterative (50ms) vs quiescence (depth 5) |   98 |    39 |     63 | 0.588 | 0.527 to 0.648 |  +61 |
| quiescence (depth 4) vs tt (depth 4)     |  180 |     8 |     12 | 0.920 | 0.885 to 0.955 | +424 |

Step 07 also plays Stockfish 19 at equal time per move. Each table has 50 games per level of
`UCI_Elo`. The last row joins the levels into one rating estimate with a 95% interval.

#### 1000ms per move, seed 21

| UCI_Elo | Games | Wins | Draws | Losses | Score |              Rating |
|:--------|------:|-----:|------:|-------:|------:|--------------------:|
| 1800    |    50 |   39 |     3 |      8 | 0.810 | 2052 (1953 to 2209) |
| 2000    |    50 |   39 |     1 |     10 | 0.790 | 2230 (2130 to 2384) |
| 2200    |    50 |   36 |     1 |     13 | 0.730 | 2373 (2277 to 2503) |
| 2400    |    50 |   18 |     0 |     32 | 0.360 | 2300 (2187 to 2395) |
| 2600    |    50 |   13 |     3 |     34 | 0.290 | 2444 (2323 to 2538) |
| 2800    |    50 |    4 |     4 |     42 | 0.120 | 2454 (2243 to 2560) |
| All     |   300 |  149 |    12 |    139 | 0.517 | 2321 (2268 to 2374) |

#### 1000ms per move, seed 22

| UCI_Elo | Games | Wins | Draws | Losses | Score |              Rating |
|:--------|------:|-----:|------:|-------:|------:|--------------------:|
| 1800    |    50 |   42 |     0 |      8 | 0.840 | 2088 (1980 to 2283) |
| 2000    |    50 |   33 |     1 |     16 | 0.670 | 2123 (2029 to 2240) |
| 2200    |    50 |   27 |     1 |     22 | 0.550 | 2235 (2139 to 2336) |
| 2400    |    50 |   24 |     2 |     24 | 0.500 | 2400 (2303 to 2497) |
| 2600    |    50 |   16 |     1 |     33 | 0.330 | 2477 (2360 to 2571) |
| 2800    |    50 |    3 |     2 |     45 | 0.080 | 2376 (2003 to 2499) |
| All     |   300 |  145 |     7 |    148 | 0.495 | 2294 (2240 to 2347) |

#### 200ms per move

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

The scale is the `UCI_Elo` scale of Stockfish. Stockfish calibrates it for longer time controls,
so the number is a rating on that scale at this speed. It is not a rating-list rating.

### Speed

Stockfish 19 runs with one thread and a 16MB hash, pinned to core 24. It searches the 15 suite
positions from an empty hash ([stockfish-search.csv](comparison/stockfish-search.csv)). The
table compares it with step 07.

| Depth | Stockfish ms | Step 07 ms | Ratio | Stockfish nodes | Step 07 nodes |
|------:|-------------:|-----------:|------:|----------------:|--------------:|
|     1 |          0.4 |        0.1 |  0.1x |              53 |           244 |
|     2 |          0.5 |        0.2 |  0.4x |             115 |           767 |
|     3 |          0.5 |        0.5 |  0.9x |             169 |         2,341 |
|     4 |          0.6 |        1.1 |  1.9x |             273 |         5,493 |
|     5 |          0.8 |        2.4 |  3.0x |             469 |        11,830 |
|     6 |          0.9 |        5.3 |  5.6x |             646 |        27,082 |
|     7 |          1.1 |        9.9 |  8.6x |             892 |        52,488 |
|     8 |          1.5 |       19.5 | 12.9x |           1,234 |       104,550 |
|     9 |          1.7 |       37.7 | 21.8x |           1,494 |       203,616 |
|    10 |          2.5 |       74.5 | 30.1x |           2,299 |       401,971 |

The ratio is the time of step 07 divided by the time of Stockfish. Each Stockfish search has a
fixed cost of about 0.3ms. In steady state (3s searches on three positions), Stockfish runs at
0.6 to 1.6 million nodes per second on this core, and step 07 runs at 5.3 to 5.4 million.

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
at short time controls. Step 07 at 1000ms per move rates 2307 on the same scale, but at a longer
time control.
