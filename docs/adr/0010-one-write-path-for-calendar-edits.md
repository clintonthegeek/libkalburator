# ADR 0010: One write path for calendar edits

- **Status:** Accepted (target state); interim rules binding immediately
- **Date:** 2026-09-11
- **Related:** [ADR 0009](0009-calendar-record-granularity-and-domain-write-units.md)
  decides what a calendar record is. This ADR decides how many ways a change
  may reach a backend. [ADR 0007](0007-journal-recovery-ownership.md)'s
  ownership split is unaffected.

## Context

Two independent write paths reach the same calendar backends.

**The staging flush.** A user edit in PlanStan goes
`IncidenceMutator` → `StagingController` →
`SyncBackend::startSync(collectionId, MemoryCalendar*, creations, updates,
deletions)`. It is incidence-granular and `KCalendarCore`-typed. It does not
touch canon, record identity, baselines, the transformation registry, the
loss profile, the mass-delete guard, or the conflict store.

**The record path.** `SyncEngineWorker::applyBatch` →
`IBackendRecordApplier::applyRecords(collectionId, WriterBatch)`. It is
record-granular and byte-typed, and every one of the facilities above applies.

Both paths write to `LocalBackend` and `RemoteCalendarBackend`. Neither is a
special case of the other, and the same defect therefore has to be found and
fixed twice. The record shows this plainly:

- The detached-override filename collision (ADR 0008) was diagnosed on the
  record path and fixed on the staging path's local half. Its remote half —
  `RemoteCalendarBackend::startSync()` writing `icalFromIncidence(inc)` to
  `generateItemUrl(davUrl, inc->uid())` — is still open, and destroys a master
  or its override on every flush that carries both.
- Family assembly is specified for the record path by ADR 0009 and has no
  counterpart on the staging path at all.
- `WriterBatch` classification, the read-only write guard, and per-record
  success accounting exist only on the record path. A staged flush to a
  read-only CalDAV calendar is stopped by the server's 403, not by us.

There is also a contract defect the duplication produced.
`syncCompleted(const QString &collectionId)` carries no calendar identity, and
`StagingController` dispatches one `startSync()` per calendar and counts raw
callbacks against the number it dispatched. The relationship between calls and
completions is positional and undocumented. Two permanent hangs came from that:
`LocalBackend` coalescing two calls into one completion, and
`StagingController` counting calendars it never dispatched. Both are fixed, and
the contract that allowed them is not.

## Decision

**1. The record path is the single write path for calendar content. The
staging flush is transitional and will be retired.** A consumer edit becomes a
record and travels the same pipeline a synced change does. This is the target
state; it is not scheduled here, because it reaches PlanStan's undo stack,
`GlobalIncidenceModel`, and the edit journal ADR 0007 leaves PlanStan owning.

**2. Until it is retired, the staging flush may not invent behaviour.** Where
the two paths touch the same concern, the staging flush uses the record path's
mechanism rather than a parallel implementation. Specifically, and binding
immediately:

- Family assembly on the staging flush calls the same helper ADR 0009's record
  path calls. Neither backend may carry a second, hand-rolled grouping.
- Record identity on the staging flush is `composeRecordIdentity()` /
  `decomposeRecordIdentity()`. A bare `incidence->uid()` is never a write
  address for a component that may be an override.
- No new capability, guard, or policy is added to `SyncBackend::startSync()`.
  A gap there is closed by moving the caller to the record path, not by
  widening the legacy surface.

**3. A completion signal names what completed.** `syncCompleted` gains the
calendar identity of the submission it acknowledges, so a consumer can count
distinct calendars actually reported rather than raw callbacks. "One completion
per `startSync()` call" stays the contract; it stops being inferable only from
call order.

**4. No new consumer may adopt the staging flush.** `SyncBackend::startSync()`
is closed to new callers as of this ADR.

## Consequences

- `RemoteCalendarBackend::startSync()` must assemble families before writing,
  under decision 2, even though the path it lives on is scheduled for removal.
  It is an open data-loss defect on the path real user edits take, and its
  removal is not near enough to wait for.
- `syncCompleted`'s signature change is a coordinated breaking change under
  [ADR 0002](0002-coordinated-breaking-refactor.md). PlanStan's
  `StagingController::onBackendSyncCompleted()` is the only production consumer
  that counts it.
- PlanStan keeps its staged-edit journal and replay under ADR 0007 throughout.
  Retiring the staging flush changes what a journal entry replays *into*, not
  who owns it.
- The retirement itself needs the record path to serve an interactive edit:
  a single-record write with a synchronous-enough acknowledgement for the undo
  stack, without a full mapping run. That capability does not exist today and
  is the real prerequisite. It should be specified before the retirement is
  scheduled.
- Until retirement, every fix in this area is applied to both paths or
  explicitly recorded as applying to one. Silence about the other path is what
  produced the current state.

## Implementation status (2026-09-11)

Decision 2's family-assembly clause is the immediate work, together with
[ADR 0009](0009-calendar-record-granularity-and-domain-write-units.md)
decision 5. Decisions 1 and 3 are unscheduled.
