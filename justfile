# C++ poker examples
#
# Container Overlay Pattern:
# --------------------------
# This justfile uses an overlay pattern for container execution:
#
# 1. `justfile` (this file) - runs on the host, delegates to container
# 2. `justfile.container` - mounted over this file inside the container
#
# When running outside a devcontainer:
#   - Builds/uses local devcontainer image with `just` pre-installed
#   - Docker mounts justfile.container as /workspace/justfile
#   - `just build` on host -> docker runs -> `just build` in container -> `cmake ...`
#
# When running inside a devcontainer (DEVCONTAINER=true):
#   - Commands execute directly via `just <target>`
#   - No container nesting

set shell := ["bash", "-c"]

# Reusable submodule-protection recipes (install-submodule-hooks,
# check-submodules-clean). Source of truth: angzarr-project/submodule.just.
import? 'angzarr-project/submodule.just'

ROOT := `git rev-parse --show-toplevel`
IMAGE := "angzarr-cpp-dev"

# Build the devcontainer image
[private]
_build-image:
    docker build --network=host -t {{IMAGE}} -f "{{ROOT}}/.devcontainer/Containerfile" "{{ROOT}}/.devcontainer"

# Run just target in container (or directly if already in devcontainer).
# Rootless docker: -u 0:0 maps to host user via subuid; writes to the
# bind-mount land owned by the host user. Rootful: direct uid match.
# See feedback_docker_rootless.
[private]
_container +ARGS: _build-image
    #!/usr/bin/env bash
    if [ "${DEVCONTAINER:-}" = "true" ]; then
        just {{ARGS}}
    else
        if docker info --format '{{{{.SecurityOptions}}}}' 2>/dev/null | grep -q rootless; then
            USER_FLAG="-u 0:0"
        else
            USER_FLAG="-u $(id -u):$(id -g)"
        fi
        docker run --rm --network=host \
            $USER_FLAG \
            -v "{{ROOT}}:/workspace:Z" \
            -v "{{ROOT}}/justfile.container:/workspace/justfile:ro" \
            -w /workspace \
            -e DEVCONTAINER=true \
            {{IMAGE}} just {{ARGS}}
    fi

# Run a mutation-testing target with the workspace mounted READ-ONLY.
#
# WHY:
#   Mull mutates compiled LLVM IR — it does NOT modify .cpp/.hpp source.
#   The host-leak risk is therefore for build/ artifacts (instrumented
#   binaries, mull state) rather than source. We still isolate via an
#   overlay copy so:
#     1. Crashed runs cannot leave stray build-mull/ trees on the host.
#     2. The pattern matches the other angzarr-*-{lang} repos for
#        operational consistency (one mental model).
#   Source mounts read-only at /src; build happens in /work inside the
#   container's writable overlay; `--rm` wipes it on exit.
#
# WHAT TOUCHES THE HOST:
#   - {{ROOT}}/.mutants-cache/ccache — ccache compiler cache, persisted
#     across runs so mutation rebuilds stay fast. NEVER holds source.
#   - {{ROOT}}/mull-reports/      — mull-runner reports (HTML, json,
#     mull.sqlite) copied out on exit.
#
# WHAT NEVER TOUCHES THE HOST:
#   - The build-mull/ tree (lives in /work, container overlay, --rm wipes).
#   - Mutated LLVM IR / intermediate mull working state.
[private]
_container-ephemeral +ARGS: _build-image
    #!/usr/bin/env bash
    set -euo pipefail
    if [ "${DEVCONTAINER:-}" = "true" ]; then
        # Already inside a devcontainer — that container IS the ephemeral
        # boundary. Run directly; the outer `--rm` keeps the contract.
        just {{ARGS}}
        exit 0
    fi
    mkdir -p "{{ROOT}}/mull-reports" \
             "{{ROOT}}/.mutants-cache/ccache"
    if docker info --format '{{{{.SecurityOptions}}}}' 2>/dev/null | grep -q rootless; then
        USER_FLAG="-u 0:0"
    else
        USER_FLAG="-u $(id -u):$(id -g)"
    fi
    docker run --rm --network=host \
        $USER_FLAG \
        -v "{{ROOT}}:/src:ro,Z" \
        -v "{{ROOT}}/mull-reports:/out:Z" \
        -v "{{ROOT}}/.mutants-cache/ccache:/ccache:Z" \
        -v "{{ROOT}}/justfile.container:/etc/angzarr-justfile:ro" \
        -e CCACHE_DIR=/ccache \
        -e MUTANTS_EPHEMERAL=1 \
        -w /work \
        {{IMAGE}} bash -eu -o pipefail -c '
            echo "[ephemeral] copying /src -> /work (container overlay)"
            mkdir -p /work
            # tar|tar: excludes mirror what we never want to copy —
            # prior build trees, the ephemeral cache, prior reports,
            # and the example binaries scattered under domain dirs.
            tar -C /src \
                --exclude=./build \
                --exclude=./build-mull \
                --exclude=./.mutants-cache \
                --exclude=./mull-reports \
                -cf - . \
                | tar -C /work -xf -
            # Mount the container-side justfile into the copy so `just`
            # finds it (the original /src is read-only, but /work is rw).
            cp /etc/angzarr-justfile /work/justfile
            cd /work
            just {{ARGS}}
            # Persist mull artifacts back to host for inspection.
            shopt -s nullglob globstar
            for art in /work/build-mull/mull.sqlite \
                       /work/build-mull/**/mull.sqlite \
                       /work/mull.sqlite \
                       /work/*.html /work/*.json; do
                [ -e "$art" ] || continue
                cp "$art" /out/ 2>/dev/null || true
            done
            if [ -d /work/build-mull/mull-elements ]; then
                cp -r /work/build-mull/mull-elements /out/ 2>/dev/null || true
            fi
            echo "[ephemeral] mull artifacts (if any) copied to host mull-reports/"
        '

default:
    @just --list

# =============================================================================
# Proto generation — cross-language model (project_proto_generation_model)
# =============================================================================
# `.proto` sources live in the angzarr-project submodule. Generated C++
# bindings (`.pb.{h,cc}` + `.grpc.pb.{h,cc}`) are NEVER committed (see
# .gitignore — both `build/` and `proto/**/*.pb.{cc,h}` are excluded). The
# proto-gen pipeline also stages `.proto` files into
# `angzarr-client-cpp/angzarr/proto/` (an untracked staging dir in the
# submodule worktree — submodule pointer NEVER bumps); CMake's
# `add_custom_command(OUTPUT … COMMAND protoc …)` rules then emit bindings
# under `build/generated/`. Bindings are regenerated:
#   1. on `post-checkout` / `post-merge` via lefthook (covers fresh clones,
#      branch switches, submodule bumps)
#   2. transparently as a recipe dependency of `build`, `test`, `fmt`, etc.
#      The recipe is idempotent — mtime guard skips when bindings are newer
#      than the newest .proto source.
#
# Runs in the same devcontainer image used for build/test/mutation so the
# protoc + grpc_cpp_plugin toolchain is fixed (no host fallback).
# Rootless docker requires `-u 0:0` per feedback_docker_rootless.
#
# Build-tool integration (CMake's `add_custom_command(OUTPUT … COMMAND
# ${Protobuf_PROTOC_EXECUTABLE} …)`) is the EXECUTOR but NOT the trigger:
# this recipe explicitly invokes `cmake --build build --target angzarr_proto`
# which forces the proto custom-command rules to resolve. Plain `cmake
# --build build` after generation is a no-op for the proto step
# (mtime-idempotent). Keeping the regen orchestration in `just` matches the
# 6-lang ecosystem pattern.

PROTO_SRC_DIR := ROOT + "/angzarr-project/proto"
PROTO_OUT_DIR := ROOT + "/build/generated"

# Public entry point. Idempotent: returns immediately if bindings are
# fresher than the newest .proto source.
generate-proto:
    #!/usr/bin/env bash
    set -euo pipefail
    src_dir="{{PROTO_SRC_DIR}}"
    out_dir="{{PROTO_OUT_DIR}}"
    if [ ! -d "$src_dir" ]; then
        echo "[generate-proto] $src_dir missing — is the angzarr-project submodule initialized?" >&2
        exit 1
    fi
    # Staleness check: regenerate if any .proto file is newer than the
    # OLDEST generated binding, or if no bindings exist yet.
    # Catches "submodule bumped" and "fresh clone" — the hot paths driving
    # the lefthook trigger. Does NOT catch manual deletion of one binding
    # while others remain fresh; use `just generate-proto-force` for that.
    #
    # OLDEST (matches Java/Python/Rust/C#) — the CMake build dir holds the
    # generated tree; `just clean` wipes it, so no orphan-stale leftovers
    # persist across regen runs.
    newest_proto=$(find "$src_dir" -name '*.proto' -printf '%T@\n' 2>/dev/null \
                    | sort -n | tail -1)
    # Guard the find for out_dir — on clean state build/ does not yet
    # exist, and `find $missing` exits non-zero which trips pipefail.
    if [ -d "$out_dir" ]; then
        oldest_pb=$(find "$out_dir" \( -name '*.pb.h' -o -name '*.pb.cc' \) \
                        -printf '%T@\n' 2>/dev/null | sort -n | head -1)
    else
        oldest_pb=""
    fi
    if [ -n "$newest_proto" ] && [ -n "$oldest_pb" ] \
        && awk -v p="$newest_proto" -v b="$oldest_pb" 'BEGIN{exit !(b>p)}'; then
        echo "[generate-proto] bindings up-to-date, skipping (use 'just generate-proto-force' to override)"
        exit 0
    fi
    just generate-proto-force

# Always regenerate, ignoring mtimes. Invoked by `generate-proto` when stale
# and exposed directly for users who want to force a rebuild.
generate-proto-force: _build-image
    #!/usr/bin/env bash
    set -euo pipefail
    if [ "${DEVCONTAINER:-}" = "true" ]; then
        # Inside the devcontainer image already — run directly.
        just --justfile "{{ROOT}}/justfile.container" generate-proto-force
        exit 0
    fi
    # Rootless docker: -u 0:0 maps to host user via subuid; writes to the
    # bind-mount land owned by the host user. Rootful: direct uid match.
    # See feedback_docker_rootless.
    if docker info --format '{{{{.SecurityOptions}}}}' 2>/dev/null | grep -q rootless; then
        USER_FLAG="-u 0:0"
    else
        USER_FLAG="-u $(id -u):$(id -g)"
    fi
    docker run --rm --network=host \
        $USER_FLAG \
        -v "{{ROOT}}:/workspace:Z" \
        -v "{{ROOT}}/justfile.container:/workspace/justfile:ro" \
        -w /workspace \
        -e DEVCONTAINER=true \
        {{IMAGE}} just generate-proto-force

# Legacy alias — kept so existing recipe-deps and muscle memory keep working.
proto: generate-proto
proto-gen: generate-proto

build: generate-proto
    just _container build

test-unit: generate-proto
    just _container test-unit

test-acceptance: generate-proto
    just _container test-acceptance

test: test-unit test-acceptance

fmt: generate-proto
    just _container fmt

lint: generate-proto
    just _container lint

# Cross-language alias — `just check` runs lint + fmt-check.
check: fmt

clean:
    # Some mull/clang runs leave files chmodded read-only under build*/
    # (and old rootful-docker runs leave root-owned files). Restore
    # writability of host-owned files before the wipe; root-owned stragglers
    # must be cleared with `sudo rm -rf` manually.
    find "{{ROOT}}" -maxdepth 1 \( -name 'build' -o -name 'build-*' -o -name 'cmake-build-*' \) \
        -exec chmod -R u+w {} + 2>/dev/null || true
    # Primary build tree + variant trees from past mutation iterations.
    # CMakePresets now writes to build/container/ and build/container-mull/;
    # bare build-mull* / build-regen / build-sanity / build-iter* are
    # pre-presets leftovers, swept here too.
    rm -rf "{{ROOT}}/build" "{{ROOT}}"/build-* "{{ROOT}}"/cmake-build-*

# Auto-format code
fmt-fix: generate-proto
    just _container fmt-fix

# === Mutation Testing ===
# All mull-runner runs go through `_container-ephemeral` so the build-mull/
# tree lives in the container's writable overlay layer and is destroyed
# with `--rm`. Mull does NOT mutate source files (it works on LLVM IR of
# compiled objects), but routing through the ephemeral helper preserves
# build-artifact discipline and matches the pattern used by sibling
# angzarr-*-{lang} repos.
mutation-test: generate-proto
    just _container-ephemeral mutation-test

[private]
_ensure-image: _build-image

# Cross-language `mutate` recipe per .plan/mutation-container-isolation.md.
# Builds the mutation_tests target with the mull IR-frontend pass-plugin
# (mull-17 0.33.0 per the example devcontainer image) then runs mull-runner
# with SQLite + IDE reporters. Only mull SQLite outputs escape to host.
mutate: _ensure-image generate-proto
    mkdir -p "{{ROOT}}/mutants-reports"
    docker run --rm --network=host \
        -u 0:0 \
        --mount type=bind,src="{{ROOT}}",dst=/src,readonly \
        --tmpfs /work:rw,exec,size=6g \
        --mount type=bind,src="{{ROOT}}/mutants-reports",dst=/reports \
        -w /work \
        {{IMAGE}} \
        bash -eu -o pipefail -c '\
            if command -v rsync >/dev/null 2>&1; then \
                rsync -a --exclude=build --exclude="build-*" \
                       --exclude="core.*" --exclude=".mutants-cache" /src/ /work/; \
            else \
                tar -C /src \
                    --exclude=./build --exclude="./build-*" \
                    --exclude="./core.*" --exclude=./.mutants-cache \
                    --ignore-failed-read -cf - . | tar -C /work -xf -; \
            fi && \
            cd /work && \
            (cmake --preset mutation 2>&1 || true) && \
            (cmake --build --preset mutation 2>&1 || true) && \
            (${ANGZARR_MULL_RUNNER:-mull-runner-17} build/container-mull/mutation_tests --reporters IDE --reporters SQLite 2>&1 || true) && \
            (find /work -name "mutation-results.sqlite" -exec cp -f {} /reports/ \; 2>/dev/null || true) && \
            (find /work -name "mull.sqlite" -exec cp -f {} /reports/ \; 2>/dev/null || true) && \
            echo "[mutate] mull SQLite results copied to host mutants-reports/" \
        '

mutants: mutate

# Purge the local mutation build cache (.mutants-cache/) — ccache only;
# never holds source.
mutants-purge-cache:
    rm -rf "{{ROOT}}/.mutants-cache"
    @echo "Removed {{ROOT}}/.mutants-cache"
