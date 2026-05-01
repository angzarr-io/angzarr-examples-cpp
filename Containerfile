# syntax=docker/dockerfile:1.4
# C++ poker examples - optimized multi-stage build
# Build: podman build -t poker-cpp-player --target agg-player -f Containerfile .
# Context must be repo root
#
# Optimizations:
# 1. Shared CMake build - all targets share proto compilation
# 2. Named cache IDs for ccache persistence
# 3. Distroless runtime - minimal attack surface
# 4. Multi-arch support (amd64 + arm64)

# ============================================================================
# Base builder - CMake with gRPC and protobuf
# ============================================================================
FROM docker.io/library/debian:bookworm-slim AS base

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    ca-certificates \
    git \
    pkg-config \
    libgrpc++-dev \
    libprotobuf-dev \
    protobuf-compiler \
    protobuf-compiler-grpc \
    libabsl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# ============================================================================
# Proto build - generate C++ from protos
# ============================================================================
FROM base AS proto

# Copy the angzarr-project submodule (CMake reads .proto files from
# angzarr-project/proto/ directly).
COPY angzarr-project/proto ./angzarr-project/proto

# Copy C++ client headers (router.hpp, aggregate.hpp, etc.)
COPY angzarr-client-cpp/include ./angzarr-client-cpp/include

COPY CMakeLists.txt ./
COPY client ./client

# Create stub CMakeLists for subdirectories
RUN mkdir -p player/agg player/upc player/saga-table \
    table/agg hand/agg \
    table/saga-hand table/saga-player \
    hand/saga-table hand/saga-player \
    hand-flow hand-flow-oo prj-output \
    && for dir in player/agg player/upc player/saga-table table/agg hand/agg \
       table/saga-hand table/saga-player hand/saga-table hand/saga-player \
       hand-flow hand-flow-oo prj-output; do \
       echo "# Stub" > $dir/CMakeLists.txt; \
    done

WORKDIR /app/build

# Configure and build just proto library
RUN cmake -G Ninja -DCMAKE_BUILD_TYPE=Release .. \
    && ninja angzarr_proto

# ============================================================================
# Source - copy all C++ source
# ============================================================================
FROM proto AS source

WORKDIR /app

# Copy all example source
COPY player ./player
COPY table ./table
COPY hand ./hand
COPY hand-flow ./hand-flow
COPY hand-flow-oo ./hand-flow-oo
COPY prj-output ./prj-output

WORKDIR /app/build

# Reconfigure with real CMakeLists
RUN cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..

# ============================================================================
# Aggregate builds
# ============================================================================
FROM source AS build-player
WORKDIR /app/build
RUN --mount=type=cache,id=cpp-build-cache,target=/root/.cache,sharing=locked \
    ninja agg-player

FROM source AS build-table
WORKDIR /app/build
RUN --mount=type=cache,id=cpp-build-cache,target=/root/.cache,sharing=locked \
    ninja agg-table

FROM source AS build-hand
WORKDIR /app/build
RUN --mount=type=cache,id=cpp-build-cache,target=/root/.cache,sharing=locked \
    ninja agg-hand

# ============================================================================
# Saga builds
# ============================================================================
FROM source AS build-saga-table-hand
WORKDIR /app/build
RUN --mount=type=cache,id=cpp-build-cache,target=/root/.cache,sharing=locked \
    ninja saga-table-hand

FROM source AS build-saga-table-player
WORKDIR /app/build
RUN --mount=type=cache,id=cpp-build-cache,target=/root/.cache,sharing=locked \
    ninja saga-table-player

FROM source AS build-saga-hand-table
WORKDIR /app/build
RUN --mount=type=cache,id=cpp-build-cache,target=/root/.cache,sharing=locked \
    ninja saga-hand-table

FROM source AS build-saga-hand-player
WORKDIR /app/build
RUN --mount=type=cache,id=cpp-build-cache,target=/root/.cache,sharing=locked \
    ninja saga-hand-player

# ============================================================================
# Process Manager build
# ============================================================================
FROM source AS build-pmg-hand-flow
WORKDIR /app/build
RUN --mount=type=cache,id=cpp-build-cache,target=/root/.cache,sharing=locked \
    ninja pmg-hand-flow

# ============================================================================
# Projector build
# ============================================================================
FROM source AS build-prj-output
WORKDIR /app/build
RUN --mount=type=cache,id=cpp-build-cache,target=/root/.cache,sharing=locked \
    ninja prj-output

# ============================================================================
# Runtime base - minimal debian (C++ needs libc)
# ============================================================================
FROM docker.io/library/debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libgrpc++1.51 \
    libprotobuf32 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -m -u 1000 angzarr

WORKDIR /app
USER angzarr

# ============================================================================
# Domain Aggregates
# ============================================================================
FROM runtime AS agg-player
COPY --from=build-player --chown=angzarr:angzarr /app/build/player/agg/agg-player ./server
ENV PORT=50601
EXPOSE 50601
ENTRYPOINT ["./server"]

FROM runtime AS agg-table
COPY --from=build-table --chown=angzarr:angzarr /app/build/table/agg/agg-table ./server
ENV PORT=50602
EXPOSE 50602
ENTRYPOINT ["./server"]

FROM runtime AS agg-hand
COPY --from=build-hand --chown=angzarr:angzarr /app/build/hand/agg/agg-hand ./server
ENV PORT=50603
EXPOSE 50603
ENTRYPOINT ["./server"]

# ============================================================================
# Sagas
# ============================================================================
FROM runtime AS saga-table-hand
COPY --from=build-saga-table-hand --chown=angzarr:angzarr /app/build/table/saga-hand/saga-table-hand ./server
ENV PORT=50611
EXPOSE 50611
ENTRYPOINT ["./server"]

FROM runtime AS saga-table-player
COPY --from=build-saga-table-player --chown=angzarr:angzarr /app/build/table/saga-player/saga-table-player ./server
ENV PORT=50612
EXPOSE 50612
ENTRYPOINT ["./server"]

FROM runtime AS saga-hand-table
COPY --from=build-saga-hand-table --chown=angzarr:angzarr /app/build/hand/saga-table/saga-hand-table ./server
ENV PORT=50613
EXPOSE 50613
ENTRYPOINT ["./server"]

FROM runtime AS saga-hand-player
COPY --from=build-saga-hand-player --chown=angzarr:angzarr /app/build/hand/saga-player/saga-hand-player ./server
ENV PORT=50614
EXPOSE 50614
ENTRYPOINT ["./server"]

# ============================================================================
# Process Manager
# ============================================================================
FROM runtime AS pmg-hand-flow
COPY --from=build-pmg-hand-flow --chown=angzarr:angzarr /app/build/hand-flow/pmg-hand-flow ./server
ENV PORT=50691
EXPOSE 50691
ENTRYPOINT ["./server"]

# ============================================================================
# Projector
# ============================================================================
FROM runtime AS prj-output
COPY --from=build-prj-output --chown=angzarr:angzarr /app/build/prj-output/prj-output ./server
ENV PORT=50690
EXPOSE 50690
ENTRYPOINT ["./server"]
