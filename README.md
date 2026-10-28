> **⚠️ Out of Date:** This repository is currently out of date. Primary development focus is on the **Rust** and **Python** implementations. The author will get back to updating this, but if you need it sooner, please [open an issue](https://github.com/angzarr-io/angzarr/issues) or contact the author directly.

> **Build status (2026-10-28):** `main` has been red since the 2026-05-19 `angzarr-project` submodule bump, which the sources have not caught up with. The dev-container fix merged on 2026-10-28 (the `justfile` and `lefthook.yml` changes) fixes only the dev-container recursion and worktree mounts; it does not repair the build.

# angzarr-examples-cpp

Example implementations demonstrating Angzarr event sourcing patterns in Cpp.

## Prerequisites

- Cpp build tools
- Buf CLI for proto generation
- Kind (for Kubernetes deployment)

## Building

See individual component directories for build instructions.

## Running

### Standalone Mode

Run with standalone runtime configuration.

### Kubernetes Mode

```bash
skaffold run
```

## License

BSD-3-Clause

