# The engine is built in its own stage so that the compiler and the build tools
# stay out of the final image. Both stages use one base image, so the binary
# runs against the C library and the C++ runtime it was linked with.
FROM debian:bookworm-slim AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential cmake ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY include ./include
COPY src ./src
COPY server ./server
COPY tests ./tests
COPY bench ./bench
COPY tools ./tools

# A portable build: no -march=native, so the image runs on any x86-64 host that Cloud
# Run schedules it on. Only the server is built; tests and benchmarks run in CI.
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --target chess_server -j "$(nproc)"


FROM debian:bookworm-slim

RUN useradd --system --no-create-home chess
USER chess
WORKDIR /app

COPY --from=build /src/build/chess_server ./chess_server

EXPOSE 8080
# Cloud Run injects $PORT (8080 by default) and the server reads it. The limits below
# are the server's own defaults, written out so that they are visible in one place.
ENV CHESS_MAX_DEPTH=12 \
    CHESS_DEFAULT_DEPTH=6 \
    CHESS_MAX_TIME_MS=10000 \
    CHESS_HASH_MB=64 \
    CHESS_THREADS=4 \
    CHESS_ALLOWED_ORIGIN=https://syedtaha22.github.io
CMD ["./chess_server"]
