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
- **MAJOR** — `RemoteCalendarBackend` god class; ~~`types/` behavior (B2) — RESOLVED by Plan 5 P1: 5 behavioural surfaces → `typesupport/` + `calendar/`; types/ passes purity grep (2026-05-30)~~; `shape/→conflict/`;
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
  **PARTIALLY RESOLVED 2026-06-06 (v0.66, `16afeb0`, out-of-campaign WildPalms RFC):** this
  exact hazard shipped as a consumer regression — WildPalms' type-correct `dynamic_cast` host
  returned nullptr for its base-only hub and the engine bailed ("dispatchSync: backend not
  found"). The ENGINE half is fixed: all six dispatch-path lookups (incl. `advanceQueue`'s
  ResourceLost skip, which the RFC missed) now fetch `SyncBackendBase*` from `BackendRegistry`
  (worker gained the registry via `setDependencies`); `asBlob()`/`runPropertyPhase()` narrowed;
  pinned by `tst_engine_baseonly_backend` (base-only backends + dynamic_cast host). STILL OPEN:
  the interface itself — `ISyncHost::backendById()` remains calendar-typed and its remaining
  consumers (`CalendarManager`, calendar paths) still rely on host impls' casts.
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

### From Plan 5 (types/ purification, 2026-05-30)

- 2026-05-30 — `src/typesupport/incidencelock_registry.{h,cpp}` — inv 1 — `IncidenceLockRegistry` is LIVE: used downstream by libkalcal (view-infrastructure/calendar-views), PlanStan (libs/editor, views, controllers, tests) and PlanEngine via Types-only links; relocated to the light TypeSupport target in Plan 5 P1.T3 (NOT dead code). Downstream relinks to Kalburator::TypeSupport in Plan 5 Phase 2.

### From the v0.63–v0.65 reconciliation sweep (2026-06-06, between Plans 5 and 6)

- 2026-06-06 — `src/engine/syncengine.cpp` (2915 LOC, was 2846 post-Plan-1) — inv 4 — the v0.65
  clobber path regrew the engine by ~70 lines; legitimate orchestration logic, but the trend runs
  against the Plan 1 decomposition. The SyncEngine follow-up (vocabulary/cleanup plan) should
  check whether the clobber dispatch belongs in a collaborator.
- 2026-06-06 — `docs/campaign/architectural-redress/AUDIT.md` B3 — inv 7 — audit evidence for
  `RemoteCalendarBackend` is stale post-v0.63 (2649→2718 LOC, header 427→472, new primer/cache
  surface); annotated in AUDIT.md in this commit. Plan 7 must re-derive `file:line` evidence.
- 2026-06-06 — `conflict/conflictpolicy.h` vs AUDIT B6 fix direction — inv 2/7 — "move
  `ConflictPolicy` into `types/`" is incompatible as written with the Plan 5 purity gate: the
  header carries `fromJson`/`toJson` + abstract resolver machinery, and PlanStan (5 files) /
  WildPalms (~10 files) include it. Plan 6 must split the file and keep a forwarding header
  (recorded in STATUS "Next action").
- 2026-06-06 — verification notes (negative findings, kept cheap): `types/` purity gate intact
  after v0.65 (`ExecutionOverride::clobber` is a pure flag); `engine/lastwritewins.h` (v0.64) is
  engine-internal — only `syncengine.cpp` includes it; `wipeCollection` (v0.65) landed on the
  neutral `IBlobBackend`, consistent with Plan 3. Baseline 136/136 on a clean rebuild.
- 2026-06-06 — `src/types/synctypes.h:332-388` — inv 2 — **Plan 5 purity-gate hole, found while
  writing Plan 6:** `synctypes.h` carries a full inline JSON codec (`syncMappingToJson`,
  `syncMappingFromJson`, `syncMappingsToJson`, `syncMappingsFromJson`) that the Plan 5 purity
  grep missed by case (`toJson`/`fromJson` lowercase patterns don't match `*ToJson`/`*FromJson`).
  Same B2 family Plan 5 swept; fix direction mirrors P1.T4 (`LogicalCalendarJson`): extract to
  `typesupport/syncmappingjson.h`. NOT Plan 6 scope (inv 8) — fold into a later plan and tighten
  the gate grep to case-insensitive when it lands.

### From Plan 6 (shape/ decoupling, 2026-06-06)

- 2026-06-06 — `src/types/synctypes.h:32` vs `src/shape/autoresolvestrategy.h` — inv 5 —
  DUAL conflict vocabulary: `Sync::ConflictResolution` (what `SyncMapping.conflictPolicy`
  holds and the engine switches on) vs `Shape::AutoResolveStrategy` (what mergers consume);
  the `SyncMapping.conflictPolicy` FIELD is named after the OTHER type
  (`Conflict::ConflictPolicy`, which never flows into the engine). Unification +
  field rename = Plan 10 (vocabulary) input; the AUDIT missed this entirely.
- 2026-06-06 — `src/engine/syncengine.cpp:1665/:2529` (pre-Plan-6 shape) — inv 4 — the rich
  `ConflictPolicy` never flowed into `merge()`: both production callsites passed constant
  `deferAll()`; the engine resolves LastWriteWins itself (`lastwritewins.h`, v0.64) and
  consults mergers only on the CustomMerge/unified-merge paths. Plan 6's narrowing makes the
  real dataflow explicit; the prompt/batch/safety knobs in `ConflictPolicy` are consumed only
  by the `ConflictHandler` UI path (downstream WildPalms/PlanStan).
- 2026-06-06 — `src/outline/outlinemerger.cpp:13` — inv (capability) — `OutlineMerger`
  ignores its strategy parameter (`Q_UNUSED`): structural outline merge is a documented
  follow-on (in-code comment); strategy-aware merge needs a design before Plan 11 closes
  test gaps over it.
- 2026-06-06 — `tests/plugin/scenarios/fake_docstogo_plugin.cpp:8-9` — inv (tooling) —
  pre-existing dual using-directives (`using namespace Kalburator;` + `using namespace
  Kalburator::Shape;`) make every bare `Shape::` reference ambiguous under clang (namespace
  `Kalburator::Shape` vs class `Kalburator::Shape::Shape`); GCC accepts it, so the build is
  green while clangd shows hard errors. Surfaced when Plan 6 T2 touched the file. Fix
  (drop one directive or qualify) is out of Plan 6 scope (inv 8) — fold into a cleanup pass.
- 2026-06-06 — WildPalms `tests/runtime` `tst_palm_mass_delete_guard_e2e` — downstream gate
  evidence (not a libkalburator issue) — heap-teardown abort ("corrupted double-linked list"
  AFTER all 4 subtests pass, during post-`cleanupTestCase()` heap teardown) is a
  **pre-existing WildPalms-side flake**: A/B over 15 isolated runs each on the same temp
  clone — pre-Plan-6 `main` failed 5/15, Plan 6 failed 4/15. Recorded here because the Plan 6
  Task 4 gate surfaced it; the WildPalms dev should chase it (likely a double-free in a test
  fixture or runtime teardown path, load/layout-dependent).
- 2026-06-06 — WildPalms `tests/runtime/CMakeLists.txt` — downstream gate evidence — hardcodes
  `${CMAKE_SOURCE_DIR}/../libkalburator/tests/sync/fakecaldavserver.cpp`, i.e. assumes the flat
  sibling layout even for clones elsewhere; the temp-clone gate needed a `/tmp/libkalburator`
  symlink to satisfy it. Flag for WildPalms: resolve the helper via the (overridable)
  `WILDPALMS_LIBKALBURATOR_SOURCE_DIR` instead of a relative sibling path.

### From the 2026-06-10 audit supplement (five-agent sweep + v0.67 wave)

Full detail in `AUDIT-2026-06-10-supplement.md`; implementation sequencing in
`2026-06-10-audit-follow-up-specs.md`. The supplement's headline items, logged here per
invariant 9:

- 2026-06-10 — `src/sync/{akonadi,multiprotocoldav}backendcontribution.h` — inv 6 —
  **RESOLVED same day** (`fac8522`, merged `b47d75e`): ctor-argument swallow
  (`make_unique<Provider>(parent)` bound QObject* to `bool calendarsOnly`, dropping the
  parent; mode never engaged in production; `5c063c1`'s dialog filter was masking it).
  Fixed behavior-preserving (`(false, parent)`); per-account mode selection = WP-A1
  design decision (⚠RFC).
- 2026-06-10 — `tests/ui/tst_providerconfigdialog.cpp:219` — inv (testing) — **RESOLVED
  same day** (`d1e924b`): `failedTestConnection_surfacesErrorMessage` asserted the
  pre-§4.4 contract (raw error in status label) and was red on main since `bb98ef9`;
  realigned to the §4.4 details-disclosure contract. Suite 136→137 green.
- 2026-06-10 — `src/engine/syncengine.cpp` — inv 4 — watch item updated: 2915 → **2933**
  LOC (clobber orchestration, LWW include, 10 registry-lookup sites since v0.63);
  decomposition trend from Plan 1 has reversed; collaborator extraction candidates listed
  in the supplement (S1).
- 2026-06-10 — `src/sync/providermanager.cpp:7`, `src/engine/syncengine.cpp:30` — inv 2 —
  dead `#include "syncbackend.h"` in the neutralized core (KCalendarCore pulled into
  sync/+engine/ for comments only) + dead fwd-decls syncengine.h:46/74, syncengine_p.h:44/63.
  → WP-A4.
- 2026-06-10 — `src/sync/caldavprovider.cpp:18` / `multiprotocoldavprovider.cpp:20` —
  inv 8 — `contentTypesFromCaps` duplicated verbatim ("mirrors CalDavProvider's helper");
  capability data now has 4 representations and one href can live in 5 maps across
  discovery→provider→backend (supplement S2). → WP-A5 / Plan 7.
- 2026-06-10 — `src/sync/iprovider.h:150/:165` — inv 5 — error/warning channels
  transport-asymmetric (push-only signal vs pull-only accessor); CalDav/Akonadi never emit
  `connectionStateChanged(false)` on connect failure while MultiProto always does,
  contradicting the iprovider.h:153 doc; future-vs-signal ordering flips per impl. → WP-A7.
- 2026-06-10 — `src/universal/{genericsqlite,rawfiles}backend.*` — inv 8 —
  neither overrides `wipeCollection` though GenericSqlite's `clearCollection` already has
  the exact semantics; engine clobber against them runs the per-record default. → WP-A6.
- 2026-06-10 — `src/journal/baselinestore.{h,cpp}` + `src/todo/icalvtodomerger.{h,cpp}` —
  inv 9 — verified dead (zero refs anywhere incl. consumers; the journal one also shadows
  `storage/baselinestore.h` behind include-dir ordering). → WP-C1/C2.
- 2026-06-10 — root `CLAUDE.md` — inv 7 — test-guidance bullets instructed using DELETED
  machinery (`TranscodingRegistry`, `storeItems`/`TranscodingPlan` write path, nonexistent
  `SyncStore::setBaseline`); worst bullets hot-fixed 2026-06-10, full doc-truth sweep =
  WP-B (the doc agent's list covers ~15 more stale claims across CLAUDE.md, phase0,
  handoffs, and public-header comments).
- 2026-06-10 — test gaps with teeth (supplement S10) — inv (testing) — v0.61 in-flight
  connect idempotency untested in all four providers (the actual SIGSEGV path);
  `ConflictResolution::Duplicate` ZERO tests (TargetWins/Skip never engine-exercised);
  `wipeCollection` untested on every concrete backend; Akonadi+Org backends contribute 0
  of 137 green tests (~2.7k LOC dark under the default profile); `incidencelock_registry`
  (225 LOC) and `discovery/` (519 LOC, PlanStan-load-bearing) zero coverage; AUDIT's
  SyncEngine-config-API gap still open. → WP-D.
- 2026-06-10 — consumer reality corrections — (campaign bookkeeping) — PlanStan relink
  `69e7df90` IS pushed (pin v0.66-provider-dialog-polish, pushed); libkalcal `b4ef4ae0` IS
  pushed (stale local tracking refs); WildPalms pins committed locally but origin/main is
  96 commits behind. Plan 8 blocker INVERTED: WildPalms has 0 `backendById` lookups,
  PlanStan ~20 sites; `runSyncFuture` has 2 WildPalms PROD callers — Plan 8 is a
  PlanStan-first consumer wave (specs §Plan 8 prep).
- 2026-06-10 — `src/engine/syncengine.cpp:1637-1641` and `:2517-2520` — inv (correctness) —
  **[C2] Duplicate UID-rebind broken for non-iCal canonical domains.**
  Both Duplicate switch arms patch `clone.data` with
  `data.replace("UID:<id>", "UID:<new-id>")` — correct only for iCal-encoded records.
  At conflict-resolution time `op.targetRecord.data` is already in canonical form
  (`{calendar,canon}` JSON after `tgtToCanon->apply` in `unifiedDispatchSync`), so the
  iCal "UID:" string pattern never matches. Clone inherits original uid in canonical data;
  `canonToTgt->apply` converts back to iCal with original uid; `MockBackend::createRecord`
  stores the clone under the wrong key and the subsequent UPDATE overwrites it — result
  is 1 record on the target, not 2. Fix: rebind the uid in canonical format (replace
  `"uid":"<original-id>"` in the canonical JSON at both switch arms) before the write
  path. QSKIP'd in
  `tst_conflict_policy_matrix::conflictResolution_duplicate_keepsBothSides`
  until resolved. → WP-D1.
- 2026-06-10 — `src/typesupport/incidencelock_registry.cpp:218` (`onOwnerDestroyed`) — inv 4
  (contract mismatch) — **[C3] IncidenceLockRegistry::onOwnerDestroyed fails to eagerly
  release locks on Qt6.** Qt6 nullifies all `QPointer`s to an object *before* emitting
  `destroyed()`, so when `onOwnerDestroyed(QObject *owner)` calls `unlockAll(owner)`, every
  `it->owner == owner` comparison is `nullptr == dangling_ptr` → false. Locks become
  `isValid()==false` (QPointer null) immediately, but are not removed from `m_locks` until
  the next `cleanupInvalidLocks()` call (inside `tryLock`/`unlock`/etc.). Observable
  consequence: `lockCount()` over-reports stale entries; `itemUnlocked` signals are delayed.
  `isLocked()` is correct (checks `isValid()`). The WP-D4 tests pin the lazy-cleanup
  contract and the re-acquirability invariant; a follow-up fix should iterate `m_locks`
  looking for null QPointers inside `onOwnerDestroyed` rather than relying on the raw
  pointer comparison.
- 2026-06-10 — `tests/{calendar,contacts,sync,plugin}/CMakeLists.txt` (KALBURATOR_HAVE_AKONADI /
  KALBURATOR_HAVE_ORG_IO gates) — **[D1] Akonadi/Org backends contribute 0 tests in the default
  build lane.** 11 test sources (~2.7k LOC of backends) are gated out of the default profile.
  The Akonadi backend is PlanStan's only production path to the system calendar; breakage is
  silent unless the `build-akonadi/` lane is run. **Decision (WP-D10):** periodic manual lane
  — not automated CI. Run before any release tag, after changes to `src/calendar/akonadi*`,
  `src/calendar/org*`, `src/sync/akonadi*`, or `src/contacts/akonadi*`, and when PlanStan
  reports an Akonadi regression. Runbook: `docs/2026-06-10-akonadi-org-dark-coverage-lane.md`.

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

### Fixed downstream, now integrated (not campaign work)

- 2026-05-29 — `src/sync/{akonadi,caldav,carddav,multiprotocoldav}provider.cpp` `connect()` +
  `src/sync/providermanager.cpp` `connectAll` — inv (correctness, same family as Plan 4) —
  **provider `connect()` was not idempotent: a re-entrant call overwrote an in-flight
  `m_connectPromise`, destroying the old `QPromise` unfinished → its dtor `cancel()`+`reportFinished()`
  with no result → a `QFutureWatcher::result()` observer SIGSEGVs** (`providermanager.cpp:154`, hit via
  the Akonadi / new-collection wizard, `connectAll` racing `CalendarDiscoveryStep::startConnect`).
  **Fixed OUTSIDE the campaign** by the PlanStan-side dev: v0.61 / `9f8a220` on
  `origin/fix/provider-connect-idempotent` — `connect()` now returns the in-flight future; `connectAll`'s
  watcher guards `result()` on canceled futures. The commit branched off `6579dfb` (pre-Plan-3);
  **now merged into `main` as `04a9876` (clean) and pushed** (133/133 green on the merged tree —
  see STATUS "Parallel downstream fix — v0.61"). Plan 4's sweep covered
  the discovery `QPromise` (T5) and the `errorSeen` `bool*` (T4) but not this provider-promise
  overwrite — the three together close the `sync/` provider-future ownership corner.

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
- 2026-06-10 — `src/calendar/icsfeedfetcher.h` — DECIDE: lib-internal only, zero consumer
  adoption; ship as public API or remove when webcal/subscription features are deferred. (WP-C4)
- 2026-06-10 — `src/calendar/logicalcalendarbuilder.h` — DECIDE: lib-internal only, zero consumer
  adoption; promote to supported API or remove. (WP-C4)
