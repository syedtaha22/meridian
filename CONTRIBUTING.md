# Contributing

Thank you for helping with the engine. This page describes how the repository is laid out, how
to build and test each part, and what a change should include.

## Layout

```bash
.
├── backend            # the C++20 engine, its HTTP server, tests, benchmarks and tools
│   ├── include/chess  # public headers
│   ├── src            # the engine
│   ├── server         # the HTTP service
│   ├── tests          # perft, mate puzzles, API tests
│   ├── bench          # benchmark programs and position suites
│   ├── tools          # matches, tree drawings
│   ├── results        # recorded benchmark and match results
│   └── README.md      # the engine, the API and how to run the benchmarks
├── frontend           # the @chess/frontend React package, with its own rules engine
│   ├── src
│   ├── tests
│   └── README.md      # how a host page mounts the board
├── .github            # CI, issue and pull request templates
├── CONTRIBUTING.md
├── README.md          # where the project started and where it stands
└── SECURITY.md
```

Each part has its own page: [backend](backend/README.md) and [frontend](frontend/README.md).

## Backend

Requirements: a C++20 compiler and CMake 3.20 or newer. CMake fetches the dependencies of the
server and the tools.

```bash
cd backend
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Before opening a pull request:

- The tests should pass under AddressSanitizer and UndefinedBehaviorSanitizer, which CI also
  runs: `cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCHESS_SANITIZE=ON`, then
  build and run `ctest` in `build-asan`.
- Changed files should be formatted with `clang-format -i`. The style is in
  [.clang-format](backend/.clang-format).
- Doxygen-style comments on public headers should stay accurate.

### Changes to rules or search

- Move generation should keep matching the published perft counts.
  [position_test](backend/tests/position_test.cpp) checks the six reference positions, and
  [search_test](backend/tests/search_test.cpp) solves the mate-in-one, two and three puzzles.
- A new search technique should be a switch in `SearchOptions` and a named preset. Then its
  effect can be measured on its own.
- The pull request should report the change in nodes at a fixed depth. It should also report
  a match result against the previous preset (`build/play_match`, 200 games or more).
- Node counts are the stable measure on a shared machine. Times should come from an idle
  machine, pinned to one core, and the number of repeats should be stated.

### Benchmarks

[benchmark.sh](backend/scripts/benchmark.sh) records one step under
[baselines](backend/results/baselines).
[benchmark_steps.sh](backend/scripts/benchmark_steps.sh) measures the presets in one process.
`build/report results` regenerates the tables in
[results/README.md](backend/results/README.md).

Results from a different machine should be recorded with their `environment.json`. They should
not replace the existing baselines.

## Frontend

```bash
cd frontend
npm ci
npx tsc --noEmit
npm run lint
npm test
npm run format:check
```

- The package takes its colors and fonts from the host's CSS custom properties, each with a
  fallback, and its class names begin with `chess-`.
- The black piece drawings are the SVGs in [pieces](frontend/src/assets/pieces), and the white
  pieces are made from them. After changing one, run `npm run build:pieces` and commit the
  regenerated [piece-sprite.ts](frontend/src/lib/piece-sprite.ts).
- Changes to the rules engine in [chess-rules.ts](frontend/src/lib/chess-rules.ts) should come
  with a test. The perft tests there check the backend's six reference positions.

## Pull requests

- A pull request should contain one change, describe what it does and why, and link the issue
  it closes.
- CI should pass: the frontend checks, the backend build and tests, the sanitizer run, and the
  container build.
- The README, the API description and the benchmark tables should be updated when the change
  affects them.

## Reporting problems

Issues should use the templates. A bug report should include the FEN of the position, the
moves played, the request and the response when it concerns the server, and the engine
version from `GET /health`. Security problems should be reported as described in
[SECURITY.md](SECURITY.md).
