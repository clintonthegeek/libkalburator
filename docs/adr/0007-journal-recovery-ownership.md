# ADR 0007: Keep user-edit journaling in PlanStan

- **Status:** Accepted
- **Date:** 2026-09-06

## Context

`CollectionRuntime` constructed a `CalendarJournal` and removed its files on
reset, but no runtime operation wrote to or replayed those files. PlanStan's
`StagingController` and `JournalRecoveryCoordinator` already journal
consumer-owned calendar edits.

## Decision

The runtime owns reconciliation durability through its baseline, conflict, and
topology stores. It does not construct, write, replay, or delete
`CalendarJournal` files. PlanStan remains the sole owner of its staged user-edit
journal and its recovery replay.

## Consequences

Runtime reset removes only runtime database artifacts and leaves consumer edit
journals untouched. A future reconciliation-recovery feature must use a
runtime-owned durable schema and explicit replay contract; it must not reuse
PlanStan's user-edit journal.
