# libkalburator

Libkalburator is a Qt 6 / KDE Frameworks synchronization library for personal-information data. It supplies reconciliation, canonical transformation, conflicts, persistence, providers, and local/remote backends for two pre-production consumers:

- **PlanStan** — calendar-oriented personal planning.
- **WildPalms** — calendar, contacts, tasks, and memo synchronization with Palm OS devices over serial or USB cradle connections.

## Project status

**Pre-production; architectural consolidation in progress.**

The synchronization algorithms and backend implementations are substantial, but the library boundary is not yet stable or independently distributable. Both consumers currently assemble too much runtime state themselves. The supported executor-owned test lane is clean; legacy consumer-managed relocation probes remain available only as opt-in diagnostics because manually moving DAV backends is outside the runtime contract.

No source or binary compatibility is promised during this consolidation. PlanStan, WildPalms, and libkalburator may be changed together.

## Current priority

Make libkalburator a reusable library by moving synchronization execution and lifecycle ownership into a library runtime while preserving consumer control over user intent and external resources.

- PlanStan continues to decide when and what to synchronize.
- WildPalms continues to own Palm connection sessions, device preparation, keepalive, backup, and restore.
- Libkalburator owns registries, stores, plugins, backend execution, run state, convergence passes, cancellation, teardown, and atomic topology changes.

See [ROADMAP.md](docs/ROADMAP.md) and [TASKS.md](docs/TASKS.md).

## Build and test

```sh
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j4
ctest --test-dir build --output-on-failure

# Include QtTest runtime skips in the coverage summary.
cmake --build build --target test-report
```

Optional backends and live-service tests require additional dependencies or credentials. A passing default suite does not validate Google, Microsoft Graph, Akonadi, Org, or live DAV behavior.

## Documentation

Read in this order:

1. [AGENTS.md](AGENTS.md) — short working rules for humans and agents making changes.
2. [ARCHITECTURE.md](docs/ARCHITECTURE.md) — current and target architecture.
3. [ROADMAP.md](docs/ROADMAP.md) — ordered outcomes and gates.
4. [TASKS.md](docs/TASKS.md) — the only active implementation queue.
5. [KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md) — current defects and risks.

Reference as needed:

- [CONSUMING.md](docs/CONSUMING.md) — PlanStan and WildPalms integration contracts.
- [FEATURES.md](docs/FEATURES.md) — implemented, integrated, experimental, and incomplete features.
- [CONVERGENCE_MATRIX.md](docs/CONVERGENCE_MATRIX.md) — generated transformation and declared-loss matrix.
- [INVARIANTS.md](docs/INVARIANTS.md) — rules the refactor must preserve.
- [COMPATIBILITY.md](docs/COMPATIBILITY.md) — present compatibility and release policy.
- [adr/](docs/adr/) — durable architectural decisions.
- [archive/](docs/archive/) — historical evidence only; never current guidance.

## Distribution status

Libkalburator currently supports only source-tree CMake embedding. It has no install/export package, stable public include tree, finalized license, or external compatibility commitment. Creating those is part of the roadmap.
