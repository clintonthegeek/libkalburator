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

- **CRITICAL** — calendar-typed sync core: `BackendRegistry` stores `SyncBackend*`;
  `ProviderManager` `dynamic_cast`s to it; non-calendar backends inherit calendar-typed
  `SyncBackend`; `CalendarManager` destructive CRUD is untested.
- **MAJOR** — `RemoteCalendarBackend` god class; `types/` behavior; `shape/→conflict/`;
  `engine/`+`contacts/`+`universal/` pull calendar headers; raw-pointer lifetimes;
  thread-unsafe `RawFilesBackend`; silent SQLite/DELETE failures; test gaps. (The `SyncEngine`
  god-class MAJOR is largely addressed by the merged Plan 1; see Resolved.)
- **MODERATE/MINOR/UGLY** — see `AUDIT.md`.

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
