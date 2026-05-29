# Discipline log — architectural-redress campaign

> Per INVARIANTS §9. Append one line per smell observed in code you pass through, even
> when off-topic from your current task. Format:
>
> `YYYY-MM-DD` — `file:line` — inv N — one phrase of context. (commit/PR if fixed)
>
> No fix is required this session. The point is that the next agent sees the same smell
> named, not stumbles across it fresh.
>
> The seed entries below restate the audit findings the campaign exists for, so the log
> begins where the work begins. Resolved findings are crossed out, not deleted.

## Open

### Layering (invariant 1)

- 2026-05-28 — `src/sync/akonadiprovider.cpp` — inv 1 — sync/ `#include`s
  `../calendar/akonadibackend.h` directly; resolution in Plan 2.
- 2026-05-28 — `src/sync/akonadibackendcontribution.cpp` — inv 1 — sync/ `#include`s
  `../contacts/akonadicontactsbackend.h`; resolution in Plan 2.
- 2026-05-28 — `src/calendar/akonadibackend.h` — inv 1 — calendar/ pulls
  `syncoperation.h` from sync/; closes the cycle. Plan 2.
- 2026-05-28 — `src/calendar/syncbackend.h` — inv 1 — `SyncBackend` base lives in
  calendar/ but is consumed by sync/; competing with `sync/syncbackendbase.h`. Plan 2.
- 2026-05-28 — `src/types/logicalcalendar.h:14` — inv 1 — types/ `#include`s
  `shape.h`. Plan 3.
- 2026-05-28 — `src/types/iincidenceregistry.h` — inv 1 — types/ `#include`s
  `shape.h`. Plan 3.
- 2026-05-28 — `src/shape/recordwriter.h` — inv 1 — shape/ `#include`s
  `backendrecord.h` from types/. Plan 6.
- 2026-05-28 — `src/shape/recordmerger.h` and `src/shape/canonjsonmerger.h` — inv 1 —
  shape/ `#include`s `conflict/conflictpolicy.h`. Plan 6.

### `types/` purity (invariant 2)

- 2026-05-28 — `src/types/logicalcalendar.h` — inv 2 — 658-LOC value type carrying JSON
  ser/deser, validation, binding-promotion logic. Plan 3.
- 2026-05-28 — `src/types/calendarmetadatamanager.cpp` — inv 2 — atomic file writes
  (VDir spec) in a "type". Plan 3.
- 2026-05-28 — `src/types/crashjournal.cpp` — inv 2 — JSON crash-recovery persistence
  in a "type". Plan 3.
- 2026-05-28 — `src/types/backendconfiguration.cpp` — inv 2 — 200+ LOC JSON in a
  "type". Plan 3.
- 2026-05-28 — `src/types/incidencelock_registry.cpp` — inv 2 — lock state mgmt in a
  "type". Plan 3.
- 2026-05-28 — `src/types/iincidenceregistry.h` — inv 2 — `#include
  <KCalendarCore/Incidence>`; KCalendarCore leaks into every consumer of types/. Plan 3.

### Encapsulation (invariant 3)

- ~~2026-05-28 — `src/engine/syncengine.h:~119-335` — inv 3 — `SyncEngineWorker` is
  publicly declared in the consumer's header despite being meant as private impl. Plan 1.~~
  **Resolved P1.T2 (05ac734, 2026-05-29):** worker moved to `src/engine/syncengine_p.h`;
  only a forward decl remains in the public header (see P1.T2 finding below).
- ~~2026-05-28 — `src/engine/syncengine.cpp` — inv 3 — `QMetaObject::invokeMethod(m_engine,
  "onWorkerSyncCompleted", ...)` cross-class slot calls by string. Plan 1.~~
  **Resolved P1.T2 (0b7c4fe, 2026-05-29):** all string-form cross-thread slot calls
  replaced with explicit signal/slot connections at construction time.

### Public surface (invariant 4)

- ~~2026-05-28 — `src/engine/syncengine.h` — inv 4 — four overloads of `runSyncFuture()`
  with overlapping semantics; `m_pendingOverride` is implicit state machine. Plan 1.~~
  **Resolved P1.T4 + P1.T5 (7e69f1d, 5ee045f, 2026-05-29):** `runSync(SyncRequest)` is
  the canonical entry; four overloads marked `[[deprecated]]` for Plan 8 removal;
  `m_pendingOverride` deleted and the per-call override flows as a method parameter.
- ~~2026-05-28 — `src/engine/syncengine.h` — inv 4 — `SyncEngineWorker::Mode` and
  `SyncEngine::SyncBehavior` are two enums with identical semantics. Plan 1.~~
  **Resolved P1.T2 (94cd859, 2026-05-29):** `SyncEngineWorker::Mode` deleted; the worker
  accepts `SyncEngine::SyncBehavior` directly.
- 2026-05-28 — `src/calendar/remotecalendarbackend.h` — inv 4 — ~58 public methods
  across 7 concerns. Plan 4.
- 2026-05-28 — `src/calendar/remotecalendarbackend.h` — inv 4 — six `discoveredX(id)`
  getters that should be one DTO. Plan 4.
- 2026-05-28 — `src/calendar/remotecalendarbackend.h` — inv 4 — `collectionRevision(id)`
  is a trivial delegate to `ctag(id)`; duplicated API. Plan 4.
- 2026-05-28 — `src/calendar/localbackend.h` — inv 4 — ~35 public methods, four separate
  metadata setters. Plan 4.
- 2026-05-28 — `src/calendar/calendarmanager.h` — inv 4 — `DeleteMode {Hide, Disable,
  Forget, DeleteFromAll}` hides four different operations behind one method. Plan 7.

### Vocabulary (invariant 5)

- 2026-05-28 — repo-wide — inv 5 — "Backend" overloaded across `SyncBackend`,
  `IBlobBackend`, `BackendContribution`, `BackendConfiguration`, `BackendCapabilities`,
  `ChangeDetection` mixin. Plan 8.
- 2026-05-28 — repo-wide — inv 5 — "Canon" overloaded across `CanonEnvelope`,
  `CanonicalRecord`, `*CanonStages`, `canonicalShape()`, `*CanonProperties`. Plan 8.
- 2026-05-28 — `src/sync/backendregistry.h` — inv 5 — `BackendRegistry` holds both live
  instances and factory contributions; same method namespace, two meanings. Plan 8.
- 2026-05-28 — `src/types/calendarmetadatamanager.h` — inv 5 — "Manager" in `types/`,
  calendar-specific. Plan 3 moves it; Plan 8 may rename.

### Dead-code candidates (Plan 9)

Confirm no out-of-tree consumer (PlanStan, WildPalms) before deletion.

- 2026-05-28 — `src/types/icommanddispatcher.h` — Plan 9.
- 2026-05-28 — `src/types/iincidencesource.h` — Plan 9.
- 2026-05-28 — `src/types/incidenceref.h` — Plan 9.
- 2026-05-28 — `src/calendar/incidencesyncadapter.h` — Plan 9.
- 2026-05-28 — `src/backend/resourcelinearization.h` — Plan 9.
- 2026-05-28 — `src/calendar/iconflictpresenter.h` — Plan 9.
- 2026-05-28 — `src/blob/mockblobbackend.h` and `localblobbackend.h` — 5 signals
  (`recordCreated`, `recordUpdated`, `recordDeleted`, `errorOccurred`,
  `progressUpdated`) declared but never connected. Plan 9.

## Resolved

(none yet)

## New findings (post-campaign-open)

Append below as work uncovers them. If a finding is in scope for an existing plan, note
the plan number; if not, it becomes a candidate for a future campaign or a STATUS
out-of-scope entry.

### From P1.T3 (MappingQueue extraction, 2026-05-29)

- 2026-05-29 — `src/engine/mappingqueue.h:97-98` — P1.T3 deviation — Plan 1 Task 3
  spec text shows `prime(mappings, behavior, filter)`. Implementation drops
  `behavior` because `MappingQueue` does not consume it (zero `SyncBehavior`
  references in mappingqueue.{h,cpp}), including it would force
  `mappingqueue.h` to `#include "syncengine.h"` for the nested `Q_ENUM`
  (`syncengine.h` already includes `mappingqueue.h` — cycle), and Plan 1 Task 4
  immediately introduces `SyncRequest` which folds the queue's prime() params
  and the engine's `m_currentSyncBehavior` together. Engine retains
  `m_currentSyncBehavior` until T4. Class comment at mappingqueue.h:42-58
  documents the rationale.

### From P1.T4 (runSync(SyncRequest) consolidation, 2026-05-29)

- 2026-05-29 — `src/engine/syncengine.cpp:578-616` — inv 4/discipline — the deprecated
  `runSyncFuture(mappingId, …)` shims bypass the canonical `runSync(SyncRequest)` and
  call `dispatchSingleNative()` directly, returning the `m_currentSingleIface->future()`
  verbatim. Rationale: `runSync(SyncRequest)` is uniformly `QFuture<QList<SyncResult>>`,
  but the single-mapping wrap (`singleFuture.then([](r){return QList{r};})`) drops
  cancellation results in Qt6 — `QFuture::then()` does not run its continuation when
  the source is canceled, so the F2 Task 23 contract (`resultCount() == 1` with
  `resultAt(0).cancelled == true` after cancel) is lost on the wrapped future. The
  bypass preserves the contract natively for the deprecated single-shim consumers
  (cancel tests, contacts witness). Canonical-API single-mapping consumers must add
  their own `onCanceled` handler if they need a SyncResult on cancel — but no
  canonical-API consumers exist yet (P1.T4 introduced `runSync(SyncRequest)`; Plan 8
  will migrate consumers and remove the shims). Asymmetry vanishes when the shims
  are deleted.
- 2026-05-29 — `src/engine/syncrequest.h:35-56` — inv 4 — `SyncRequest` cannot
  express "explicitly empty subset" (zero mappings dispatched) — `mappingIds.isEmpty()`
  collapses both "all enabled" and "empty subset" into the same dispatch shape (runs
  all enabled). The deprecated `runSyncFuture(ids, …)` subset shim preserves the
  historical empty-list-means-zero-mappings semantics by short-circuiting on
  `ids.isEmpty()` before constructing the request. If Plan 8 ever wants this
  distinction on the canonical API, add an explicit `bool allEnabled` field (or a
  sentinel value) to `SyncRequest`. Currently no canonical-API consumer needs it.
- 2026-05-29 — `src/engine/syncengine.h:~445-501` — inv 3 — the engine retains two
  `QFutureInterface*` members (`m_currentSingleIface`, `m_currentMultiIface`) and two
  watchers (`m_singleWatcher`, `m_multiWatcher`), mirroring the dual return-type
  surface. Approach A (collapse to multi-iface only, redirect single writes to
  `m_currentMultiIface` with `QList{r}` wrap) was considered but rejected because the
  deprecated single-shim must return `QFuture<SyncResult>` with the F2 Task 23
  contract intact; the wrap-then-unwrap chain across types loses cancellation results
  in Qt6. Once the deprecated shims are removed (Plan 8), the dual surface collapses
  naturally to multi-iface only.
- 2026-05-29 — `src/engine/syncengine.cpp:493-497` — pre-existing — `runSync(SyncRequest)`
  reports `QList<SyncResult>{}` on overlap rejection, indistinguishable from a legitimate
  "no enabled mappings" result. The pre-T4 overloads had the same behavior; not a T4
  regression. Plan 8 should report a failed future (or a single error-marked
  `SyncResult`) instead of an empty list. Surfaced during P1.T4 spec review.
- 2026-05-29 — `src/engine/syncengine.cpp:~577` — pre-existing — `dispatchSingleNative`
  overlap-rejection produces a default-constructed `SyncResult{}` with neither error nor
  cancelled flag set. Same paper cut as the multi-iface case above; same Plan 8 cleanup
  window.

### From P1.T2 (Worker collapse, 2026-05-29)

- 2026-05-29 — P1.T2 spec text imprecision — plan said `invokeMethod(m_engine,
  "slot", ...)` but the actual string-form calls in pre-T2 code were
  `invokeMethod(m_worker, "slot", ...)` (engine→worker direction). The implementer
  correctly read the spec's intent and rewrote those. Future plan authors should grep
  before quoting. No action; observational.
- 2026-05-29 — `src/engine/syncengine_p.h` — inv 1/3 — `SyncEngineWorker` retains a
  `QPointer<QObject> m_baselineStoreAnchor` whose only purpose is to be the receiver
  for queued `BaselineStore` marshalling. Coupling reduced from typed-back-pointer to
  opaque-thread-anchor but not eliminated. Full ablation requires thread-safe
  `Kalburator::Storage::BaselineStore` (own mutex / per-thread connections, mirroring
  the SQLite fix at 6579dfb). Out of scope for Plan 1; track as P2-or-later candidate.
- 2026-05-29 — `src/engine/syncengine.h:~102` — inv 3 — forward decl `class
  SyncEngineWorker;` remains in the public header because `SyncEngine::m_worker` is a
  typed pointer. Class body, nested types, and API surface are all in
  `syncengine_p.h` (spirit of invariant 3 met). Deeper PIMPL
  (`std::unique_ptr<SyncEnginePrivate>`) would remove even the forward decl. Note as
  future refactor candidate; not blocking Plan 1.
