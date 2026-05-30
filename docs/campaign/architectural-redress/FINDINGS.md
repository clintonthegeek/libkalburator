# Discipline log — architectural-redress campaign

> Per INVARIANTS §9. Append one line per smell observed in code you pass through, even when
> off-topic from your current task. Format:
>
> `YYYY-MM-DD` — `file:line` — inv N — one phrase of context. (commit/PR if fixed)
>
> No fix is required this session. The point is that the next agent sees the same smell
> named, not stumbles across it fresh. Resolved findings are crossed out, not deleted.

## Baseline

**The finding baseline is `AUDIT.md` (verified rebuild, 2026-05-29), not this file.** All
22 prior seed entries (which restated the 2026-05-28 audit) are superseded: several were
factually wrong and are corrected or refuted in `AUDIT.md` ("Corrected from the prior audit"
and "Refuted / non-issues" sections). Do **not** re-enter audit findings here — they live in
`AUDIT.md` with `file:line` evidence. This log is for **new** smells discovered while working,
beyond what the audit already catalogues.

Quick pointer to the audit's actionable spine (see `AUDIT.md` for evidence + fix direction):

- ~~**CRITICAL** — calendar-typed sync core: `BackendRegistry` stores `SyncBackend*`;
  `ProviderManager` `dynamic_cast`s to it; non-calendar backends inherit calendar-typed
  `SyncBackend`~~ **RESOLVED by Plan 3** (registry/PM/engine traffic in `SyncBackendBase*`;
  RawFiles/GenericSqlite/RemoteContacts/Filtered reparented onto the neutral base). The 4th
  CRITICAL (`CalendarManager` destructive CRUD untested) was resolved by Plan 2's tests.
- **MAJOR** — `RemoteCalendarBackend` god class; `types/` behavior; `shape/→conflict/`;
  ~~`engine/`+`contacts/`+`universal/` pull calendar headers~~ (RESOLVED by Plan 3);
  ~~raw-pointer lifetimes~~ (RESOLVED by Plan 4: `CardDavProvider` `bool*`→`shared_ptr`,
  `SyncEngine` `QFutureInterface*`→`unique_ptr`); ~~thread-unsafe `RawFilesBackend`~~ (RESOLVED by
  Plan 4: `QMutex` guard on RawFiles + GenericSqlite hashes); ~~silent SQLite/DELETE failures~~
  (RESOLVED by Plan 4: `GenericSqliteBackend::clear/deleteCollection`→`bool` + checks); test
  gaps. (The `SyncEngine` god-class MAJOR is largely addressed by the merged Plan 1; see Resolved.)
- **MODERATE/MINOR/UGLY** — see `AUDIT.md`. (Plan 4 also resolved the `CardDavCapabilityDiscovery`
  raw-`QPromise*` MODERATE; the `SyncConflictStore`/`IDMappingStore` silent-PRAGMA MODERATEs
  remain open — see "From Plan 4" below.)

## Open (new findings, post-rebaseline)

Smells discovered during work that the AUDIT does not already catalogue.

### From Plan 1 (SyncEngine decomposition, merged 2026-05-29)

These document why the merged engine code is shaped as it is and what later plans must
address — load-bearing knowledge, not audit restatements.

- 2026-05-29 — `src/engine/syncengine.cpp:578-616` — inv 4/discipline — the deprecated
  `runSyncFuture(mappingId, …)` shims bypass the canonical `runSync(SyncRequest)` and call
  `dispatchSingleNative()` directly, returning `m_currentSingleIface->future()` verbatim.
  Rationale: the single-mapping wrap (`singleFuture.then([](r){return QList{r};})`) drops
  cancellation results in Qt6 — `QFuture::then()` does not run its continuation when the source
  is canceled, so the F2 Task 23 contract (`resultCount()==1` with `resultAt(0).cancelled==true`
  after cancel) is lost on the wrapped future. The bypass preserves it natively for the
  deprecated single-shim consumers. Asymmetry vanishes when the shims are deleted (vocabulary
  plan).
- 2026-05-29 — `src/engine/syncrequest.h:35-56` — inv 4 — `SyncRequest` cannot express
  "explicitly empty subset" (zero mappings) — `mappingIds.isEmpty()` collapses "all enabled" and
  "empty subset" into the same dispatch shape (runs all enabled). The deprecated subset shim
  preserves the historical empty-list-means-zero semantics by short-circuiting before building
  the request. If a canonical-API consumer ever needs the distinction, add an explicit
  `bool allEnabled` (or sentinel) to `SyncRequest`. None needs it yet.
- 2026-05-29 — `src/engine/syncengine.h:~445-501` — inv 3 — the engine retains two
  `QFutureInterface*` members (`m_currentSingleIface`, `m_currentMultiIface`) + two watchers,
  mirroring the dual return-type surface. Collapsing to multi-iface-only was rejected because
  the deprecated single-shim must return `QFuture<SyncResult>` with the F2 Task 23 contract
  intact (the wrap-then-unwrap loses cancellation results in Qt6). Collapses naturally once the
  deprecated shims are removed (vocabulary plan).
- 2026-05-29 — `src/engine/syncengine.cpp:493-497` and `:~577` — pre-existing — overlap
  rejection reports `QList<SyncResult>{}` / a default `SyncResult{}` (no error or cancelled
  flag), indistinguishable from a legitimate "no enabled mappings" result. Pre-T4 overloads had
  the same behavior (not a regression). A later plan should report a failed future or an
  error-marked `SyncResult`.
- 2026-05-29 — `src/engine/syncengine_p.h` — inv 1/3 — `SyncEngineWorker` retains a
  `QPointer<QObject> m_baselineStoreAnchor` purely as the receiver for queued `BaselineStore`
  marshalling. Coupling reduced from typed-back-pointer to opaque-thread-anchor but not
  eliminated; full ablation needs a thread-safe `Kalburator::Storage::BaselineStore` (own mutex /
  per-thread connections, mirroring the SQLite fix at 6579dfb). Candidate for the SyncEngine
  decomposition follow-up.
- 2026-05-29 — `src/engine/syncengine.h:~102` — inv 3 — forward decl `class SyncEngineWorker;`
  remains in the public header because `SyncEngine::m_worker` is a typed pointer. Class body,
  nested types, and API surface are all in `syncengine_p.h` (spirit of invariant 3 met). Deeper
  PIMPL (`std::unique_ptr<SyncEnginePrivate>`) would remove even the forward decl. Future
  refactor candidate.

### From Plan 2 (CalendarManager safety net, 2026-05-29)

- 2026-05-29 — `src/calendar/calendarmanager.cpp:43-139` — inv (correctness) — `createCalendar`
  is non-atomic across bindings: a per-backend failure leaves already-created backends in place
  with no rollback (pinned by `tst_calendar_manager::createCalendar_oneBackendFails_*`). The
  CalendarManager split must decide whether to add transactional semantics.
- 2026-05-29 — `src/calendar/calendarmanager.cpp:270-280` — inv (correctness) —
  `DeleteMode::DisconnectSync` calls `removeBinding()` to strip the Sync1 binding from config,
  then immediately overwrites config via `m_configManager->updateLogicalCalendar(logCal)` using
  the stale pre-loop `logCal` (which still carries the Sync1 binding). Net effect: the
  Sync1 binding is NOT removed; only `syncEnabled` is cleared. The operation succeeds and
  reports success, but the logical calendar retains the non-primary binding it was supposed
  to drop. Pinned by
  `tst_calendar_manager::deleteCalendar_disconnectSync_dropsSecondaryBindings_keepsPrimary`.
- 2026-05-29 — `src/calendar/calendarmanager.cpp:766-783` — inv (correctness) —
  `restoreFromSnapshot()` is a stub returning `false` while `captureSnapshot()` is fully
  implemented; destructive ops therefore have capture-but-no-undo. Pinned by
  `tst_calendar_manager::restoreFromSnapshot_currentlyUnimplemented_returnsFalse`.
- 2026-05-29 — `tests/engine/tst_engine_cancellation` — inv (correctness/test) — intermittent
  **SEGFAULT** under full-parallel `ctest -jN`: failed once in a full run, then passed 5/5 on
  isolated rerun. Indicates a threading race in the SyncEngine cancellation path (not a
  test-only timeout). Surfaced during Plan 2's regression gate; unrelated to the test-only
  change. Candidate for the SyncEngine decomposition follow-up (cf. the `m_baselineStoreAnchor`
  / dual-iface notes above).

### From Plan 3 (neutralize the calendar-typed sync core, 2026-05-29)

- 2026-05-29 — `src/calendar/isynchost.h:29` — inv 1/5 — `ISyncHost::backendById()` still returns
  the calendar-typed `SyncBackend*` (the host interface lives in `calendar/` and is consumed by
  `CalendarManager`, which legitimately needs calendar API). The `BackendRegistry` is now neutral
  (`SyncBackendBase*`), so every `ISyncHost` implementation bridges
  `registry.backendInstance()` → `backendById()` with an **unchecked `static_cast<SyncBackend*>`**
  (in libkalburator: the test stubs + `examples/reference_consumer/main.cpp` — safe ONLY because
  those scaffolds register calendar backends). Fetching a non-calendar backend via `backendById()`
  and using it as `SyncBackend*` would be UB. Neutralizing `ISyncHost::backendById()` (return
  `SyncBackendBase*`; have `CalendarManager` `dynamic_cast` where it needs calendar API) is a
  follow-up — candidate for the CalendarManager-split plan or a dedicated host-interface plan.
- 2026-05-29 — downstream contract (INVARIANTS §10) — `BackendRegistry::backendInstance()` now
  returns `SyncBackendBase*` (was `SyncBackend*`) and `registerBackendInstance` takes
  `SyncBackendBase*`. Registration stays source-compatible (upcast), but PlanStan code that
  **stores the `backendInstance()` result as `SyncBackend*`** (e.g. `CollectionController`) will
  need a `dynamic_cast`/`static_cast` (or to store `SyncBackendBase*`) when it next builds against
  this library. Flag for the PlanStan/WildPalms port (O7/O12).

### From Plan 4 (correctness/ownership sweep, 2026-05-29)

- 2026-05-29 — `src/engine/syncengine.cpp` (`~SyncEngine`/`stopWorkerThread` vs.
  `dispatchSync`) — inv (correctness) — **destroying a `SyncEngine` while a fetch is in-flight
  deadlocks.** `dispatchSync` invokes `fetchItems` on the backend's (main) thread via
  `Qt::BlockingQueuedConnection`; `~SyncEngine`→`stopWorkerThread()` calls `m_workerThread.wait()`
  on that same main thread. If the main thread is blocked inside the queued `fetchItems` when the
  dtor runs, the worker waits for the main thread and the main thread waits for the worker —
  classic deadlock (reproduced as a 300 s ASAN timeout while writing the P4.T6 test). Plan 4's
  leak fix (unique_ptr ifaces) is orthogonal and correct; this teardown constraint is why the
  T6 test pins the post-completion teardown path, not a true mid-flight destroy. Candidate for the
  SyncEngine decomposition follow-up: make teardown cancel + drain the in-flight op without a
  blocking round-trip.
- 2026-05-29 — `src/universal/genericsqlitebackend.cpp` (`deleteCollection`) — inv (correctness) —
  best-effort eviction: `m_collections.remove()` runs even when the DROP / `_shapes` DELETE failed
  (the method returns `false` to signal it). On failure the on-disk table or `_shapes` row may
  persist while the in-memory cache no longer lists the collection — a reopen would resurrect it.
  Deliberate + documented in code (P4.T2); named here. A future transactional-delete pass could
  make it atomic.
- 2026-05-29 — `src/universal/rawfilesbackend.cpp:79-88` — inv (correctness) — sibling silent
  failure the AUDIT did not flag: `RawFilesBackend::clearCollection`/`deleteCollection` are still
  `void` and call `QFile::remove(...)` without checking the result (same bug class Plan 4 fixed in
  `GenericSqliteBackend`). Left out of Plan 4 scope (audit didn't list it; INVARIANTS §8). Fold
  into a later correctness pass or the vocabulary plan.
- 2026-05-29 — tooling — inv (discipline) — the worktree's clangd compile DB / `.clangd` resolves
  `compile_commands.json` against the **deleted** `.worktrees/redress-3` path, so the editor emits
  spurious `cannot_open_file`/stale-signature diagnostics that the real `build/` does not. Not a
  build problem (ctest is green); re-point `compile_commands.json` at the active build dir per the
  global clangd-setup convention. Cosmetic but misleading to future readers.
- 2026-05-29 — `src/universal/rawfilesbackend.cpp:84` + `src/universal/genericsqlitebackend.cpp`
  (`deleteCollection`) — inv (correctness) — `deleteCollection` removes `m_collections[id]` but
  **leaves `m_shapeByCollection[id]` populated**, so `shapeFor(id)` keeps returning the deleted
  collection's shape. Pre-existing (present on `main`, not a Plan 4 regression); the AUDIT did not
  flag it and it is out of Plan 4 scope (INVARIANTS §8). The Plan 4 `m_collectionsMutex` now makes
  the cleanup safe to add — a one-liner (`m_shapeByCollection.remove(id)` inside the lock) for a
  later correctness pass. Surfaced by the P4 final whole-branch review.
- 2026-05-29 — `src/universal/genericsqlitebackend.cpp` (`createCollection`) — inv (correctness) —
  TOCTOU between the in-memory insert (under `m_collectionsMutex`) and the `_shapes` DB INSERT
  (after the lock, to avoid the `m_connMutex` deadlock). A concurrent `deleteCollection` in that
  window could leave a ghost in-memory entry or an orphan DB row. No worse than `main` (which did
  the whole block unlocked) and benign under the current single-writer usage; `INSERT OR IGNORE`
  keeps the DB self-consistent. Logged for the eventual transactional-collection pass. Surfaced by
  the P4 final whole-branch review.
- 2026-05-29 — `src/universal/genericsqlitebackend.h` `m_open` (and RawFiles equivalents) — inv
  (correctness) — `m_open` is a plain `bool` read as a fast-path guard in every I/O method but
  written in `ensureOpen()`; concurrent construct-to-use overlap is a technical data race under the
  C++ memory model. Benign in practice (`ensureOpen` runs in the ctor before any other thread can
  reach the object) and pre-existing; Plan 4's mutex guards the hashes, not `m_open`. Make it
  `std::atomic<bool>` if a real concurrent-open path ever appears. Surfaced by the P4 final review.

## Resolved

### By Plan 1 (SyncEngine decomposition, merged 2026-05-29)

- ~~`SyncEngineWorker` publicly declared in `syncengine.h` (inv 3).~~ Moved to
  `src/engine/syncengine_p.h`; only a forward decl remains. (05ac734)
- ~~`QMetaObject::invokeMethod(m_*, "slotName", …)` cross-thread slot calls by string
  (inv 3).~~ Replaced with explicit signal/slot connections at construction. (0b7c4fe)
- ~~Four `runSyncFuture()` overloads + `m_pendingOverride` implicit state machine (inv 4).~~
  `runSync(SyncRequest)` is the canonical entry; overloads marked `[[deprecated]]`;
  `m_pendingOverride` deleted, the per-call override flows as a parameter. (7e69f1d, 5ee045f)
- ~~`SyncEngineWorker::Mode` vs `SyncEngine::SyncBehavior` duplicate enums (inv 4).~~
  `Mode` deleted; the worker accepts `SyncEngine::SyncBehavior` directly. (94cd859)

### By Plan 4 (correctness/ownership sweep, 2026-05-29)

- ~~`MockBlobBackend` swallows injected `OnLoadRecords` failures via the base
  `loadRecordsOrError` default (test false-green); production reaches the backend through that
  surface (AUDIT MAJOR).~~ Override added that reports the injected failure (`false` + error);
  pinned by a new `tst_mockblobbackend` slot with an `errorOccurred` spy. (P4.T1)
- ~~`GenericSqliteBackend::clearCollection`/`deleteCollection` (void) silently ignore
  DELETE/DROP failures (AUDIT MAJOR).~~ Now return `bool`, check every `exec()`, `qWarning` on
  failure; `deleteCollection` gained the missing `!m_open` guard. Pinned by a missing-table test. (P4.T2)
- ~~`RawFilesBackend`/`GenericSqliteBackend` race: worker-thread `shapeFor()` reads vs.
  main-thread `createCollection` writes on unguarded collection hashes (AUDIT MAJOR).~~ Dedicated
  `m_collectionsMutex` guards both hashes in both backends, deadlock-free (helpers called outside
  the lock; `saveManifest` snapshots then writes unlocked). TSan-clean; concurrency stress test
  added. (P4.T3)
- ~~Raw `bool*` captured by two `CardDavProvider` lambdas with undefined firing order; one
  deletes it (use-after-free) (AUDIT MAJOR).~~ Converted to `std::make_shared<bool>` captured by
  value in both lambdas; manual `delete` removed. (P4.T4)
- ~~Raw `QPromise*` in `CardDavCapabilityDiscovery` with four hand-maintained `delete` sites
  (AUDIT MODERATE, folded).~~ Converted to `std::unique_ptr`; `.reset()` at each site. (P4.T5)
- ~~Raw `QFutureInterface*` in `SyncEngine` leaked when destroyed mid-sync (dtor freed neither)
  (AUDIT MAJOR).~~ Both members are `std::unique_ptr`; 8 delete sites → `.reset()`; dtor documents
  why it does not `reportFinished()`. ASAN-clean over the sync lifecycle. (P4.T6)
