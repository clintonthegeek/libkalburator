# Architecture decisions

ADRs record durable choices that constrain later work. They do not track tasks, progress, or session history.

| ADR | Decision | Status |
|---|---|---|
| [0001](0001-runtime-owns-sync-execution.md) | Runtime owns synchronization execution; consumers own intent and external-resource sessions | Accepted |
| [0002](0002-coordinated-breaking-refactor.md) | Prefer coordinated breaking changes during consolidation | Accepted |
| [0003](0003-collection-runtime-surface.md) | Establish the collection runtime facade | Accepted |
| [0004](0004-backend-executor-ownership.md) | Runtime owns ordinary backend executors | Accepted |
| [0005](0005-operational-topology-materialization.md) | Operational endpoint materialization belongs in desired topology | Accepted |

Use the next four-digit number. Each ADR states context, decision, consequences, and supersession status. Revise factual corrections in place; supersede changed decisions with a new ADR.
