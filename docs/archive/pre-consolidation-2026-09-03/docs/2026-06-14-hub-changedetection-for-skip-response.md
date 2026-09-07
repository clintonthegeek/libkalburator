# Response — hub-side ChangeDetection so the engine's skip-unchanged fires for WildPalms

**Date:** 2026-06-14
**Direction:** libkalburator (producer) ← WildPalms (consumer)
**Responds to:** `WildPalms/docs/2026-06-14-libkalburator-hub-changedetection-for-skip-handoff.md`
(pinned at WP `493bd804a549e161718986065848f0af301b5667`)
**Status:** ACCEPTED — implementing on branch `feature/hub-changedetection-skip`.
**Severity:** Medium efficiency RFC (not correctness). WP's multi-hop propagation already
works; this unlocks the cheap-repeat-passes half.

---

## TL;DR

WildPalms shipped transparent multi-hop sync (`Palm — Hub — Remote`) as a fixpoint loop with
`SyncEngine::setSkipUnchangedMappings(true)`. The Palm conduit backends implement
`Sync::ChangeDetection` (per-database modnum). But the engine skips a mapping only when **both**
sides implement `ChangeDetection` (`syncengine.cpp:725-726`), and the hub side
(`Kalburator::Sinks::GenericSqliteBackend`, and the `FilteredCollectionBackend` views over it)
does not. So `eligibleToSkip` is always false and the optimization is runtime-inert.

This change implements `Sync::ChangeDetection` on the two hub-side backends. Once it lands,
WP's already-shipped `setSkipUnchangedMappings(true)` activates with **no further WP source
change** beyond bumping the lib pin.

---

## Confirmation against the current tree

Verified on `main` @ `9b5da33`:

- `GenericSqliteBackend : public Kalburator::Sync::SyncBackendBase` only
  (`src/universal/genericsqlitebackend.h:33`). Namespace is still `Kalburator::Sinks`
  (the Plan-10 Sinks↔universal rename moved the *directory* to `src/universal/`, not the
  namespace). The RFC's type name is correct.
- `FilteredCollectionBackend : public Kalburator::Sync::SyncBackendBase`
  (`src/universal/filteredcollectionbackend.h:32`); parent held as borrowed
  `SyncBackendBase* m_parent`, nulled on `backendInstanceUnregistered`.
- Engine ANDs both sides (`syncengine.cpp:725-726`); a side is "covered" iff
  `dynamic_cast<Sync::ChangeDetection*>(base)` succeeds (`:671`, `:709`) — so implementing the
  interface makes **every** collection on that backend covered.
- `prepareSyncFastPath()` snapshots fresh revisions once per multi-mapping run
  (`syncengine.cpp:660`, called from `:359`); primes the cache on per-mapping success
  (`:1109-1133` via `primeRevisionCache`).

### Two facts that shape the implementation

1. **The fast path runs only on the multi-mapping driver.** A `SyncRequest` with
   `mappingIds.size()==1` routes to `processSingleMapping`, which explicitly does **not** run
   `prepareSyncFastPath` and does **not** prime revision baselines (`syncengine.cpp:397-406`).
   `mappingIds` empty (all-enabled) or size>1 routes to `driveQueue` → fast path
   (`syncengine.cpp:495-561`). WP runs the full mapping set per fixpoint pass, so it hits the
   fast path. The skip **test** must therefore use an empty `mappingIds` (not size 1, the
   trap the existing `tst_engine_universal_sink_dispatch` falls into harmlessly).

2. **`cachedCollectionRevision()` is `const`** but `GenericSqliteBackend::threadDb()` is not.
   Resolved with the same `const_cast`-this idiom the FCB already uses
   (`filteredcollectionbackend.cpp:54`).

---

## The `Sync::ChangeDetection` contract (`src/sync/changedetection.h`)

| method | meaning |
|---|---|
| `collectionRevision(id)` | fresh token; `""` ⇒ "can't answer" ⇒ engine treats as changed |
| `collectionRevisions(ids)` | batched; default loops `collectionRevision` (we inherit it) |
| `cachedCollectionRevision(id) const` | last persisted token; `""` ⇒ no baseline ⇒ changed |
| `primeRevisionCache(map)` | engine writes the post-sync baseline here |
| `persistsCollectionRevisions()` | `true` for both (sqlite file persists) |

Skip eligibility: `sourceCovered && targetCovered && sourceUnchanged && targetUnchanged`,
where `unchanged = !fresh.isEmpty() && !cached.isEmpty() && fresh == cached`.

---

## Design

### `GenericSqliteBackend` — per-collection **content digest** (NOT a write counter)

The RFC's preferred option was an O(1) write counter bumped on each create/update/delete.
**That does not work here**, and discovering why is the central result of this work:

> The SyncEngine re-writes records back to **both** backends during a TwoWay reconcile
> (`syncengine.cpp:2710-2752`) — even on a logically no-op pass it round-trips each record
> through the (lossy, normalizing) canon pipeline and re-applies it. A write counter bumps on
> every one of those reconcile writes, so a "settled" mapping keeps bumping and the revision
> chases the engine's own write-backs — it never reaches `fresh == cached`. Verified
> empirically: with a counter the fast path reported `0 unchanged` on every pass indefinitely.

The fix is to make the revision a **pure function of collection content**, so an idempotent
re-write of the same logical content leaves it unchanged. This is exactly why the Akonadi
backend digests item ids+revisions rather than counting writes (`akonadibackend.cpp`
`collectionRevision` → `computeRevisionDigest`).

`collectionRevision(id)` = SHA-256 over the collection's `(record_id, content_hash)` tuples,
ordered by `record_id` (order-independent). It reads only those two small columns — no payload
deserialization — so it is cheap relative to the Palm serial read it lets WP skip. An empty
collection hashes to a stable non-empty token; an unknown collection returns `""`.

A small table persists the engine-primed baseline (the Akonadi `AkonadiRevisionStore`
analogue, but in-db so it travels with the file), created in `ensureSchema()` **and** the
per-thread `threadDb()` bootstrap (mirroring `_shapes`):

```sql
CREATE TABLE IF NOT EXISTS _collection_revisions (
  collection_id TEXT PRIMARY KEY,
  synced_rev    TEXT                  -- last digest the engine primed
)
```

- **`collectionRevision(id)`** — content digest; `""` for a collection this backend doesn't own.
- **`cachedCollectionRevision(id) const`** — read `synced_rev`; `""` if absent.
- **`primeRevisionCache(map)`** — `INSERT ... ON CONFLICT(collection_id) DO UPDATE SET synced_rev` per entry.
- **`persistsCollectionRevisions()`** — `true`.

No write-path bumps: create/update/delete/clear/wipe do **not** touch `_collection_revisions`
(the digest is recomputed on demand from current content). `deleteCollection` removes the
collection's `synced_rev` row. Migration is automatic via `CREATE TABLE IF NOT EXISTS`; a
pre-existing collection has no `synced_rev`, so its first post-upgrade sync can't skip
(correct — no baseline), primes, and the next settled sync skips.

### `FilteredCollectionBackend` — parent-derived revision

A filtered view changes iff its parent collection changes, so the conservative + correct token
is the parent's `collectionRevision(parentCollectionId)`. All four methods delegate to the
parent **translated to `m_parentColId`**, guarded for a null parent (post-unregister) exactly
like the existing read/write methods:

- `collectionRevision(virtualId)` → `parentCd->collectionRevision(m_parentColId)` (or `""`).
- `cachedCollectionRevision(virtualId) const` → `parentCd->cachedCollectionRevision(m_parentColId)`.
- `primeRevisionCache({{virtualId, rev}})` → key rewritten to `m_parentColId` → parent.
- `persistsCollectionRevisions()` → parent's value if it implements the interface, else `true`.

Conservative is fine and intended: the filtered route re-syncs whenever anything in the parent
collection changes (even outside the filter); a settle pass that finds no in-filter changes
still terminates WP's loop via `SyncStats::hasChanges()`. Multiple FCBs over the same parent
collection read/prime the **same** row with the **same** per-pass snapshot value, so there is
no stomping.

### Convergence

`prepareSyncFastPath` snapshots fresh revisions at the **start** of each run (= each WP
fixpoint pass) and primes `synced_rev` to that snapshot on success. With a content digest, a
collection whose content does not change keeps the same digest, so once the engine has primed
`synced_rev` to the current digest the next pass sees `fresh == cached` → skip. A real change
moves the digest, defeating the skip for the pass that propagates it; once propagated and
re-primed, it skips again. The `synced_rev` baseline **persists across process/session
restarts** (sqlite file), so a fresh WP HotSync session over already-settled content skips
immediately rather than re-confirming.

A subtlety worth recording: the canon round-trip the engine applies on write is *normalizing*
(lossy demote), so the **first** sync that writes canonical bytes into a collection can change
its digest once (raw → canonical), costing one extra confirming pass before it settles. That
is a one-time cost per collection; steady-state edits via the engine are already canonical.

---

## Acceptance criteria (from the RFC)

1. ✅ A `GenericSqliteBackend` collection reports a stable `collectionRevision` that changes iff
   its records change; `prime`/`cached` round-trip across process restarts. — backend unit tests
   `revision_*` in `tests/sinks/tst_generic_sqlite_backend.cpp`.
2. ✅ With both sides covered, `prepareSyncFastPath` skips an unchanged mapping — engine test
   `tests/engine/tst_engine_skip_unchanged.cpp`: a real sync primes both sides (prime path),
   then with both caches matching current content the run (empty `mappingIds`,
   `setSkipUnchangedMappings(true)`) skips; a source content change defeats the skip. FCB
   parent-derivation pinned by `tests/sinks/tst_filtered_collection_backend.cpp` `revision_*`.
3. ⏳ WP @ the new pin: a settled HotSync's later fixpoint passes log
   `SyncEngine: skipping unchanged mapping …` instead of a full `DatebookDB` read — verified
   WP-side after the pin bump, no WP source change.
4. ✅ lib suite green (151/151). ✅ PlanStan gate (`build-v074-pretest`, `HAVE_AKONADI=ON`,
   `PLANSTAN_LIBKALBURATOR_SOURCE_DIR` → this tree): builds clean against the changed backends;
   ctest 89/102, with the 13 failures all in the pre-existing `integration`/`sync-workflow`
   cluster (CalDAV/Akonadi-runtime-dependent — e.g. `testCreateCalendar(caldav)` "not
   discoverable"; every `local`/`orgmode` variant passes). Zero new regressions from this
   change. ⏳ tag + WP pin bump remain.

---

## Discovered (out of scope — flagged, not fixed here)

**Two `GenericSqliteBackend`s cannot sync each other correctly.** GS record ids are
collection-prefixed (`collectionId\x01origId`, `genericsqlitebackend.cpp` `encodeRecordId`),
and `createRecord` stores an incoming (already-encoded) id **verbatim** rather than decoding it.
So when the engine round-trips a record between two GS sinks, each side re-prefixes the id and
neither matches the other's records — they re-create them with ever-growing ids and accumulate
duplicates. This is unrelated to ChangeDetection and does **not** affect WP (its hub's peers are
Palm/CalDAV backends with stable bare ids), but it is a latent correctness bug for any
GS↔GS mapping. Recommend a follow-up: have GS `createRecord` decode an incoming encoded id (or
key records by a content-stable id). The skip test deliberately avoids this path by driving the
skip decision from primed state.

---

## Plan / status

- [x] Step 1 — `GenericSqliteBackend` implements `Sync::ChangeDetection` via content digest
      (+ `_collection_revisions`). Counter approach tried and rejected — see Design.
- [x] Step 2 — `FilteredCollectionBackend` parent-derived revision.
- [x] Step 3 — backend unit tests (criterion 1) + engine skip test (criterion 2) + FCB tests.
- [ ] Step 4 — build `-j 8`, lib ctest green, PlanStan gate, tag, hand pin to WP.

Update this checklist in the same commit that lands each step.
