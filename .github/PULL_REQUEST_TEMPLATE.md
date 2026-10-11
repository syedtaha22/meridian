## Changes

<!-- What the change does and why. Link the issue it closes, if there is one. -->

## Area

- [ ] Backend (engine, server, tools)
- [ ] Frontend package
- [ ] CI or documentation

## Checks

- [ ] `ctest` passes in a Release build (backend changes)
- [ ] `ctest` passes under AddressSanitizer and UndefinedBehaviorSanitizer (backend changes)
- [ ] `npx tsc --noEmit`, `npm run lint`, `npm test` and `npm run format:check` pass
      (frontend changes)
- [ ] Changed C++ files are formatted with `clang-format`
- [ ] Documentation and tables affected by the change are updated

## Engine strength or speed

<!-- For a change to the search or the evaluation. Delete this section otherwise. -->

Nodes at a fixed depth, from an empty hash table, on the fifteen positions of the benchmark suite:

| Depth | Nodes before | Nodes after | Change |
|---:|---:|---:|---:|
| 4 |  |  |  |
| 6 |  |  |  |
| 8 |  |  |  |

Match against the previous preset (200 games or more, colors alternated):

| Configurations | Time or depth | Games | Wins | Draws | Losses | Score | Elo (95% interval) |
|---|---|---:|---:|---:|---:|---:|---|
| new vs previous | 50ms per move |  |  |  |  |  |  |
