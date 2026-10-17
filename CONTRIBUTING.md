# Contributing

Thank you for helping with the engine. This page describes how the repository is laid out, how
to build and test it, and what a change should include.

## Layout

```bash
.
├── .github            # CI, issue and pull request templates
├── book               # the whitelist of the opening book
├── include            # headers
├── src                # the engine library and the UCI program
├── tests              # perft, time management, UCI and search tests
├── third_party        # code of other projects, with their licenses
├── CMakeLists.txt     # the build
├── CONTRIBUTING.md
├── ENGINE.md          # the code and the UCI program
├── LICENSE
├── README.md          # where the project started and where it stands
├── SECURITY.md
└── VERSIONING.md      # what changes the version of the engine
```

The code and the UCI program are described in [ENGINE.md](ENGINE.md).

## Build and test

Requirements: a C++20 compiler and CMake 3.20 or newer.

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
  [position_test](tests/position_test.cpp) checks the six reference positions.
- A new search technique should be a switch in `SearchOptions` and a named preset. Then its
  effect can be measured on its own.
- The pull request should report the change in nodes at a fixed depth. It should also report
  a match result against the previous preset, with the number of games.
- Node counts are the stable measure on a shared machine. Times should come from an idle
  machine, pinned to one core, and the number of repeats should be stated.

## Pull requests

- A pull request should contain one change, describe what it does and why, and link the issue
  it closes.
- CI should pass: the build and tests, and the sanitizer run.
- The README and the UCI description should be updated when the change affects them.

## Reporting problems

Issues should use the templates. A bug report should include the FEN of the position, the
moves played, the UCI commands and the replies when it concerns the UCI program, and the
engine version from the `id name` line. Security problems should be reported as described in
[SECURITY.md](SECURITY.md).
