# ADR 0001: Runtime owns synchronization execution

- **Status:** Accepted
- **Date:** 2026-09-03

## Context

PlanStan and WildPalms both construct registries, plugins, stores, backends, mappings, conflicts, and `SyncEngine`. PlanStan additionally owns backend thread relocation and topology recompilation. WildPalms additionally owns multi-pass graph convergence and result aggregation because a Palm connection session surrounds synchronization.

This duplication exposes engine implementation details and has produced divergent run paths, lifecycle defects, and unsafe affinity handling.

WildPalms nevertheless has a valid consumer-owned boundary: the physical Palm session. It must decide when a device is connected, prepare Palm databases/categories, manage keepalive, and control backup/restore and EndOfSync timing.

## Decision

A libkalburator collection/profile runtime owns synchronization execution and persistent reconciliation state.

Consumers own:

- user intent and scheduling;
- external-resource session establishment;
- application presentation and model projection;
- consumer-specific backends and transformations.

The library runtime owns:

- registries, plugins, providers, backends, stores, and run state;
- backend executors, cancellation, convergence, and teardown;
- topology compilation and mutation;
- normalized progress, conflicts, and terminal results.

External resources integrate through a generic bounded lease/hook contract. WildPalms implements that contract around its Palm link thread and device access. Palm types remain outside libkalburator.

## Consequences

- PlanStan deletes backend movement and most of `CollectionController`'s synchronization assembly.
- WildPalms deletes engine/store/plugin assembly and its outer multi-pass future loop, but keeps Palm connection management, device preparation, tickle control, backup, and restore.
- One run API must represent normal, full, mirror, subset, and destructive intent.
- The runtime must support both long-lived network/local backends and short-lived leased device backends.
- Consumer contract tests are required before migration.

