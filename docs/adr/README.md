# Architecture decisions

ADRs record durable choices that constrain later work. They do not track tasks, progress, or session history.

| ADR | Decision | Status |
|---|---|---|
| [0001](0001-runtime-owns-sync-execution.md) | Runtime owns synchronization execution; consumers own intent and external-resource sessions | Accepted |
| [0002](0002-coordinated-breaking-refactor.md) | Prefer coordinated breaking changes during consolidation | Accepted |
| [0003](0003-collection-runtime-surface.md) | Establish the collection runtime facade | Accepted |
| [0004](0004-backend-executor-ownership.md) | Runtime owns ordinary backend executors | Accepted |
| [0005](0005-operational-topology-materialization.md) | Operational endpoint materialization belongs in desired topology | Accepted |
| [0006](0006-capability-contract-migration-boundary.md) | Capability contract migration boundary | Accepted |
| [0007](0007-journal-recovery-ownership.md) | Keep user-edit journaling in PlanStan | Accepted |
| [0008](0008-recurrence-override-identity-and-family-assembly.md) | Per-override record identity; families assembled at the transport boundary | Accepted (assembly point closed by 0009) |
| [0009](0009-calendar-record-granularity-and-domain-write-units.md) | A calendar record is one component; write units are declared by the domain | Accepted |
| [0010](0010-one-write-path-for-calendar-edits.md) | The record path is the single write path; the staging flush is transitional | Accepted (target state) |
| [0011](0011-icalendar-collection-is-the-sync-unit-kind-is-scoped-per-mapping.md) | An iCalendar collection is the sync unit; component kind is a per-mapping scope, not a backend split | Accepted |

Use the next four-digit number. Each ADR states context, decision, consequences, and supersession status. Revise factual corrections in place; supersede changed decisions with a new ADR.
