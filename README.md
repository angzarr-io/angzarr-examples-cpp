> **Out of scope.** This repository is currently out of scope and not maintained until it is ported or archived. It targets the angzarr spec (`angzarr-project`) at `8d60829` (2026-05-19); `angzarr-project` main is `f736250` (2026-11-20), 477 commits ahead. The code is incompatible with the current runtime and API: `main` has been red since that submodule bump and depends on the lagging `angzarr-client-cpp`. Known to differ (see `BEHIND-SPEC.md` on branch `chore/project-f7362501`): reason keys, `Handling` and `ConflictResolution` (removed in the spec), `CommandResult`, the Command | Fact split, descriptor checks and the child handshake (moved to the coordinator), and typed refusal messages. The current reference implementations are [angzarr-client-rust](https://github.com/angzarr-io/angzarr-client-rust) and [angzarr-examples-rust](https://github.com/angzarr-io/angzarr-examples-rust); the spec is [angzarr-project](https://github.com/angzarr-io/angzarr-project).

> **⚠️ Out of Date:** This repository is currently out of date. Primary development focus is on the **Rust** and **Python** implementations. The author will get back to updating this, but if you need it sooner, please [open an issue](https://github.com/angzarr-io/angzarr/issues) or contact the author directly.

> **Build status (2026-10-28):** `main` has been red since the 2026-05-19 `angzarr-project` submodule bump, which the sources have not caught up with. The dev-container fix merged on 2026-10-28 (the `justfile` and `lefthook.yml` changes) fixes only the dev-container recursion and worktree mounts; it does not repair the build.

# angzarr-examples-cpp

Example implementations demonstrating Angzarr event sourcing patterns in Cpp.

> **Status: intentionally lagging.** These examples are temporarily descoped: the project grew too fast in scope, so work is focused on the Rust and Python clients. They are behind the current angzarr stack and may not build against it. They will be brought back up to date once Rust and Python are fully operational.

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

Note: the Kubernetes deployment here uses angzarr core's distributed runtime, which is retired for now; it is preserved at the tag [`distributed-runtime-final`](https://github.com/angzarr-io/angzarr/tree/distributed-runtime-final) and will return.

```bash
skaffold run
```

## License

Licensed under either of Apache License 2.0 or BSD 3-Clause, at your option
([LICENSE-APACHE](LICENSE-APACHE), [LICENSE-BSD](LICENSE-BSD)).

