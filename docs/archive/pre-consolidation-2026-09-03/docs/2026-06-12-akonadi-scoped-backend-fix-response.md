# libkalburator response — Akonadi scoped-backend reads + engine fetch-failure discrimination

**Date:** 2026-06-12
**Direction:** libkalburator → WildPalms (response to
`WildPalms/docs/2026-06-12-libkalburator-akonadi-scoped-backend-read-handoff.md`)
**Branch:** `fix/akonadi-scoped-backend-reads` (off `main` @ v0.73)
**Status:** Both defects fixed + regression-tested. libkalburator suite green
(default profile 149/149; Akonadi profile adds the scoped-collection guard).
The reads-N-incidences live verification is WP's on-device step (criterion 2).

---

## Fix A — scoped AkonadiBackend / AkonadiContactsBackend now resolve their collection

Root cause confirmed verbatim on `main` (v0.73): a per-collection scoped backend
(created by `AkonadiProvider::createBackend` with `akonadiCollectionId`) is never
run through `loadCalendars()`, so `m_collections` stays empty and every
`fetchItems`/`createRecord` fast-fails with "Unknown calendar". `loadCalendars()`
has **no caller in `src/`** (confirmed: only the definition + the live test).

**Fix (lazy, the handoff's preferred approach):** added
`ensureScopedCollection(id)` to both backends. When `m_collections` lacks the
requested id and it equals `m_scopedCollectionId`, it seeds an **id-only**
`Akonadi::Collection(numericId)` (the numeric id is the suffix after the
`akonadi-` / `akonadi-contacts-` prefix). `ItemFetchJob`/`ItemCreateJob` resolve
an id-only collection server-side, so no `CollectionFetchJob` round-trip or
`loadCalendars()` revival is needed. Called at the top of `fetchItems` and
`createRecord` in both backends. `updateRecord`/`deleteRecord` resolve via the
cached-item map (populated by `fetchItems`), so they work once the read path does.

- `src/calendar/akonadibackend.{h,cpp}` — `ensureScopedCollection`, wired into
  `fetchItems` (read) and `createRecord` (blob write).
- `src/contacts/akonadicontactsbackend.{h,cpp}` — same shape.
- `loadCalendars()` left in place (still used by `tst_akonadibackend_live` and
  the non-scoped path); the lazy seed is the scoped-path fix.

**Regression test:** `tests/calendar/tst_akonadi_scoped_collection.cpp` (gated by
`KALBURATOR_HAVE_AKONADI`) — the handoff's unit-level guard: a scoped backend's
`fetchItems` for its own id must NOT return an immediately-`Failed` op carrying
"Unknown calendar"/"Unknown collection". Verified RED with the fix disabled
(state `Failed`, "Unknown calendar: akonadi-1") and GREEN with it on, for both
backends. The full reads-`N`-incidences integration test needs a live Akonadi
with seeded collections (54/64) — that's the on-device verification (criterion 2).

---

## Fix B — engine distinguishes a genuine fetch failure from "not implemented"

The handoff's amendment is exactly right: a blanket "`Failed` op ⇒ fail mapping"
would break every backend that reads solely via `loadRecords` and relies on the
base-class `fetchItems` default being skipped (e.g. `Sinks::FilteredCollectionBackend`,
`RemoteContactsBackend`, the blob backends). The engine must discriminate.

**Mechanism chosen — WP's #1 preference: a distinct `SyncOperation::NotSupported`
terminal state.** Rationale over the alternatives: it touches no overriding
backend, the engine check is a one-liner, and it removes the overload where a
`Failed` op meant *both* "error" and "not implemented".

- `src/sync/syncoperation.{h,cpp}` — appended `NotSupported` to the `State` enum
  (last, so existing ordinals are unchanged; no exhaustive switch or persisted
  int depends on the values — audited). Added `notSupported(reason)`; made
  `NotSupported` terminal in `isFinished()` and `setState()`.
- `src/sync/syncbackendbase.cpp` — the default `fetchItems` now returns
  `notSupported(...)` instead of `fail(...)`. (`deleteItems` default left as
  `fail` — it has external callers and is not part of the fetch gate.)
- `src/engine/syncengine.cpp` — both fetch blocks (source + target): after the
  cancel check, a `fetchOp` in state `Failed` (genuine failure from a backend
  that *implements* fetch) fails the mapping with `op->errorString()`. A
  `NotSupported` op proceeds to `loadRecordsOrError` exactly as before.

**Clobber data-loss footgun:** confirmed the wipe already sits *after* the source
fetch by design, so the source-fetch gate fires **before** the wipe — a target is
never destroyed when the source can't be read. The target-fetch gate converts a
post-wipe target-fetch failure into a reported failure.

**Regression tests:** `tests/engine/tst_engine_fetch_failure_discrimination.cpp`
(+ a `MockBackend` test affordance `setFetchOpFailsSilently` mimicking Akonadi:
a `Failed` fetch op with a *silent* `loadRecordsOrError`, and `setUseBaseFetchItems`
emitting a `NotSupported` op):

1. `genuineFetchFailure_failsMappingNotSilentSuccess` — genuine fetch failure +
   silent read ⇒ mapping fails (RED→GREEN).
2. `notImplementedFetch_stillSyncsViaLoadRecords` — `NotSupported` ⇒ still syncs
   via `loadRecords` (the loadRecords-only contract; the FilteredCollectionBackend
   case).
3. `clobberWithGenuineSourceFetchFailure_failsAndDoesNotWipeTarget` — clobber with
   a genuine source-fetch failure fails the mapping **and leaves the target
   intact** (RED→GREEN; the RED run logged the live "clobber wiped target
   collection" footgun).

Full default-profile suite: **149/149** (was 148; +1 new test). No regressions —
empirical proof the `NotSupported` discrimination is safe across every
base-default-backend path (CardDAV/contacts, blob, sqlite).

---

## ⚠ Known gap discovered — first-sync fast path is NOT fetch-gated

The fetch gate lives on the **unified dispatch path**
(`dispatchSync` → `unifiedContinueAfterConflicts`). That is the path WP's real
Akonadi routes take — they are **TwoWay** ("mode: TwoWay" in the HotSync log) —
so this fix covers them.

But the **first-sync fast path** (`dispatchFirstSync`, taken only for
`OneWayUpload` + same-shape + quick-path + non-clobber) reads source/target via
`loadRecordsOrError` **without** a `fetchItems` gate. For a backend whose
`loadRecordsOrError` is *silent* (the base default — neither Akonadi backend
overrides it), a genuine fetch failure there is still swallowed: it harvests
empty baselines and reports success. Two consequences:

1. A `OneWayUpload` first sync with a cache-backed source (Akonadi) reads 0
   records even after Fix A, because that path never calls `fetchItems` to
   populate the cache. (Akonadi's cache is filled by `fetchItems`, which the
   fast path skips.)
2. A genuine fetch failure on that path is silent (the Fix-B concern, on the path
   the gate doesn't cover).

This predates this fix and does not affect the reported (TwoWay) scenario. Two
candidate follow-ups, not done here to keep the change scoped to the handoff:

- **(preferred)** add the same `fetchItems` gate to `dispatchFirstSync` (target
  probe, blob mirror, and `harvestBaselinesAfterFirstSync`) so the fast path is
  uniformly gated — this also fixes consequence (1) for cache-backed sources.
- have the Akonadi backends override `loadRecordsOrError` to surface a failed
  fetch (the handoff's secondary note), so every read site catches it regardless
  of path.

Flagging for a WP/libkalburator decision. If WP ever runs an Akonadi route as
`OneWayUpload`, the first follow-up becomes load-bearing.

---

## Acceptance criteria status

1. Scoped backend resolves its collection for read/write — **done** (Fix A + guard
   test; live N>0 read = on-device).
2. HotSync pulls collections 54/64 into `hub.db` — **WP on-device** (needs live
   Akonadi); covered for the TwoWay routes by Fix A + Fix B.
3. A genuine fetch failure fails the mapping (and a clobber before the wipe)
   rather than a 0-record success — **done on the unified path** (Fix B + 3 tests);
   first-sync fast path is the known gap above.
4. libkalburator suite green; PlanStan ctest baseline before tagging — **lib green;
   PlanStan pretest still to run before any tag.**
