# Versioning

The engine version is the `engine_version` field of the reply of `GET /health`. It is defined in
one place, `Config::version` in [server/api.hpp](server/api.hpp). It follows
[Semantic Versioning 2.0.0](https://semver.org), `MAJOR.MINOR.PATCH`, with the definitions below.

The version is independent of the release tags of the original desktop game.

## Scope

The version describes two things that a user depends on:

- the HTTP interface: the endpoints, the fields of requests and replies, the status codes and the
  environment variables of the server
- the behaviour of the engine: the moves that it chooses and the strength that results

Tests, benchmarks, scripts, tools, documents and results do not change the version. A change
that affects only them should not bump it.

## Major

The major version should increase when a client of the HTTP interface has to change:

- a field is removed, renamed, or changes its type or meaning
- a request that was valid is rejected, or gets another status code
- an environment variable is removed or changes its meaning

The minor and patch versions return to 0.

## Minor

The minor version should increase when the interface gains something, or when the engine plays
differently on purpose:

- a new optional request field, a new reply field, a new endpoint or a new environment variable
- a change to the search, the evaluation or the opening book that changes the moves, and that a
  match accepted under the sequential test described in
  [results](results/README.md). Each such change is a new step of the results.

The patch version returns to 0.

## Patch

The patch version should increase for a correction that adds no capability:

- a crash, an illegal move, a wrong score, a wrong status code or a mishandled position
- a change in speed that leaves every chosen move the same

A correction that changes moves belongs here, and not under minor, when it restores the intended
behaviour and does not claim a gain in strength.

## Versions before 1.0.0

While the major version is 0, the interface is not declared stable, and a minor version may
change it. Version 1.0.0 should be the first release for which the interface of `GET /health` and
`POST /move` is declared stable.

## Releases

- The version should change in the commit that changes the behaviour that it describes.
- A release tag should be named `meridian-vMAJOR.MINOR.PATCH`, so that it does not collide with the
  tags of the original desktop game.
