# Versioning

The engine version is the version of the `id name` line of the UCI program. It is defined in
one place, `kEngineVersion` in [include/version.hpp](include/version.hpp). It follows
[Semantic Versioning 2.0.0](https://semver.org), `MAJOR.MINOR.PATCH`, with the definitions below.

The version is independent of the release tags of Horizon, the engine that Meridian rewrites.

## Scope

The version describes two things that a user depends on:

- the UCI interface: the commands, the options and the format of the output
- the behaviour of the engine: the moves that it chooses and the strength that results

Tests, benchmarks, scripts, tools, documents and results do not change the version. A change
that affects only them should not bump it.

## Major

The major version should increase when a program that drives the engine has to change:

- a command or an option is removed, renamed, or changes its meaning
- an option value that was valid is rejected

The minor and patch versions return to 0.

## Minor

The minor version should increase when the interface gains something, or when the engine plays
differently on purpose:

- a new command or a new option
- a change to the search, the evaluation or the opening book that changes the moves, and that a
  match accepted under a sequential test.

The patch version returns to 0.

## Patch

The patch version should increase for a correction that adds no capability:

- a crash, an illegal move, a wrong score or a mishandled position
- a change in speed that leaves every chosen move the same

A correction that changes moves belongs here, and not under minor, when it restores the intended
behaviour and does not claim a gain in strength.

## Versions before 1.0.0

While the major version is 0, the interface is not declared stable, and a minor version may
change it. Version 1.0.0 should be the first release for which the UCI commands and options are
declared stable.

## Releases

- The version should change in the commit that changes the behaviour that it describes.
- A release tag should be named `meridian-vMAJOR.MINOR.PATCH`.
