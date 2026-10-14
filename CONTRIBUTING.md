# Contributing

Thank you for helping with the engine. This page describes how the repository is laid out, how
to build and test it, and what a change should include.

## Layout

```bash
.
├── .github            # CI, issue and pull request templates
├── bench              # benchmark programs and position suites
├── book               # the whitelist of the opening book
├── include            # headers
├── results            # recorded benchmark and match results
├── scripts            # experiments and reports
├── src                # the engine library and the UCI program
├── tests              # perft, mate puzzles, time management, UCI and search tests
├── tools              # matches, tuning, training
├── CMakeLists.txt     # the build
├── CONTRIBUTING.md
├── ENGINE.md          # the code, the UCI program and how to run the benchmarks
├── LICENSE
├── README.md          # where the project started and where it stands
├── SECURITY.md
└── VERSIONING.md      # what changes the version of the engine
```

The code, the UCI program and the benchmarks are described in [ENGINE.md](ENGINE.md).

## Build and test

Requirements: a C++20 compiler and CMake 3.20 or newer. CMake fetches the one dependency of the
tools.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Before opening a pull request:

- The tests should pass under AddressSanitizer and UndefinedBehaviorSanitizer, which CI also
  runs: `cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCHESS_SANITIZE=ON`, then
  build and run `ctest` in `build-asan`.
- Changed files should be formatted with `clang-format -i`. The style is in
  [.clang-format](.clang-format).
- Doxygen-style comments on public headers should stay accurate.

### Changes to rules or search

- Move generation should keep matching the published perft counts.
  [position_test](tests/position_test.cpp) checks the six reference positions, and
  [search_test](tests/search_test.cpp) solves the mate-in-one, two and three puzzles.
- A new search technique should be a switch in `SearchOptions` and a named preset. Then its
  effect can be measured on its own.
- The pull request should report the change in nodes at a fixed depth. It should also report
  a match result against the previous preset (`build/play_match`, 200 games or more).
- Node counts are the stable measure on a shared machine. Times should come from an idle
  machine, pinned to one core, and the number of repeats should be stated.

### Benchmarks

[benchmark.sh](scripts/benchmark.sh) records one step under
[baselines](results/baselines).
[benchmark_steps.sh](scripts/benchmark_steps.sh) measures the presets in one process.
`build/report results` regenerates the tables in
[results/README.md](results/README.md).

Results from a different machine should be recorded with their `environment.json`. They should
not replace the existing baselines.

## Pull requests

- A pull request should contain one change, describe what it does and why, and link the issue
  it closes.
- CI should pass: the build and tests, and the sanitizer run.
- The README, the UCI description and the benchmark tables should be updated when the change
  affects them.

## Reporting problems

Issues should use the templates. A bug report should include the FEN of the position, the
moves played, the UCI commands and the replies when it concerns the UCI program, and the
engine version from the `id name` line. Security problems should be reported as described in
[SECURITY.md](SECURITY.md).
