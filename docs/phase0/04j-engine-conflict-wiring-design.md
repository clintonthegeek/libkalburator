# Phase B4 — BlobSyncEngine ↔ BlobBaselineStore + ConflictStore

**Date:** 2026-04-21
**Status:** Approved for implementation.
**Phase tag on completion:** `v0.8-phase-b4-engine-conflicts`.

## Motivation

Phase B3 landed `BlobBaselineStore` but did not wire it into the engine.
`BlobSyncEngine::twoWayNaive` is stateless and cannot propagate deletions
or detect true conflicts. This phase closes that gap and completes the
B2-deferred items that block Wild Palms Phase E's integration tests.

## Surface

```cpp
namespace Kalburator::Sync {

BlobSyncResult twoWayWithBaseline(
    IBlobBackend *a,
    IBlobBackend *b,
    const QString &collectionId,
    const QString &mappingId,
    BlobBaselineStore *baseline,
    QSyncCore::ConflictHandlerRegistry *handlers,
    QSyncCore::ConflictStore *conflicts,
    const QSyncCore::ConflictPolicy &policy);

} // namespace Kalburator::Sync
```

`BlobSyncStats` gains a `conflicts` field (unresolved count).

## The nine-case 3-way diff

For each record ID seen on either side or in the baseline:

| baseline | side a | side b | action |
|---|---|---|---|
| present (hash B) | present (hash A) | present (hash B) | modified on A only → copy A to B |
| present (hash B) | present (hash B) | present (hash B') | modified on B only → copy B to A |
| present (hash B) | present (hash B) | present (hash B) | no change |
| present (hash B) | present (hash A) | present (hash B'), A≠B' | **conflict** → handler→handleConflict |
| present (hash B) | missing | present | deleted on A → delete on B |
| present (hash B) | present | missing | deleted on B → delete on A |
| missing | present | missing | new on A → create on B |
| missing | missing | present | new on B → create on A |
| missing | present (hash X) | present (hash X) | concurrent identical create → no-op, record new baseline |
| missing | present (hash X) | present (hash Y), X≠Y | **conflict** → handler→handleConflict |

At end of successful run, `baseline.commitBaselines(mappingId, finalHashMap)`
reflects the synced state.

## Conflict handling

When a conflict is detected:
1. Build a `ConflictRecord` from the two `BackendRecord`s (populating
   `RecordSnapshot` for each side — note that `RecordSnapshot::id` is
   a plain `QString` alias, `RecordId`).
2. Look up `handler = handlers->handlerFor(a->backendId())`. If nullptr,
   fall back to `handlers->defaultHandler()`. If that is also nullptr,
   treat the conflict as deferred (persist and skip).
3. Call `handler->handleConflict(record, policy)` which returns a
   `ConflictDecision`.
4. Apply the decision: UseSource / UseTarget / Skip. (UseBoth /
   Merge / DeleteBoth are parsed as Skip in B4 — proper handling
   lands when WP actually needs it.)
5. If the decision is Skip or Pending, persist via
   `conflicts->addConflict(record)` and increment the conflicts count.

## Design decision — registry ownership

`ConflictHandlerRegistry` is passed into `twoWayWithBaseline` as a borrowed
pointer. The engine does NOT own the registry; the caller owns one and
passes it in. Matches the existing `SyncCoordinator::conflictRegistry()`
pattern. We are NOT adding `BlobSyncEngine::registerConflictHandler(...)`
as originally sketched in the spec.

## Test backend with distinct backend IDs

The existing `MockBlobBackend` hardcodes `backendId() == "mock-blob"`.
For dispatch tests we need two mocks with distinct IDs. The test file
subclasses `MockBlobBackend` with an `IdentifiedMock` carrying an ID.

## Tests

Extend `tests/blob/tst_blobsyncengine.cpp` with slots covering each of
the nine diff cases plus handler dispatch and Skip-persistence.

## Outcome

(Filled in during Task 10.)
