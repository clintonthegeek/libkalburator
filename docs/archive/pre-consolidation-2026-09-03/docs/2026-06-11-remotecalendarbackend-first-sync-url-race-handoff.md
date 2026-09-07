# RemoteCalendarBackend: first-sync DAV-URL race + thread-affinity violation

> **Defect 1 RESOLVED in v0.71** (`d306c54`, derive-on-miss in `davUrlFor`).
> Verified: libkalburator `tst_remotecalendarbackend_writepaths` 11/11 (new
> `startSync_undiscovered_calendar_derives_url_and_writes` regression + live
> Radicale lane + convergence); PlanStan `tst_sync_caldav_conflicts` 10/10
> against the pristine v0.71 tag (was 8/10).
>
> **Defect 2 (thread-affinity) RESOLVED in v0.72** — the three op-based API
> methods (`fetchItems`/`pushItems`/`deleteItems`) no longer parent the
> operation to `this` across a thread boundary. The op is created unparented and
> pushed onto the backend's thread via a small `onOwnerThread()` helper
> (no-op when the API is already called from the backend thread). TDD: three new
> `tst_remotecalendarbackend_writepaths` cases
> (`{fetch,push,delete}Items_fromWorkerThread_opLivesOnBackendThread`) assert the
> returned op's `thread()` equals the backend's thread when the call originates
> on a worker thread — watched RED (op stranded, `Actual: <null>` + the "Cannot
> create children…" warning), then GREEN. Verified: writepaths **14/14**, live
> Radicale lane (121s) + convergence + blob-view + subsequent-sync all green,
> and the affinity warning no longer prints. See the section below.

**Date:** 2026-06-11
**From:** PlanStan dev (triaged via PlanStan's gated `tst_sync_caldav_conflicts`)
**Affects:** `src/calendar/remotecalendarbackend.cpp` (observed on tag **v0.69**;
the relevant code is unchanged on `main`/Plan-8-step-3 as of `87e895e`).
**Severity:** medium latent defect. Not user-facing in primed (provider) flows;
silently drops new local events on first sync of a **directly-configured
(non-provider) CalDAV backend**.

## Summary

On the **first** sync of a CalDAV backend whose per-calendar DAV URL was *not*
primed (i.e. configured inline from a `.kalb` `backends[]` entry rather than via
a provider's `primeCalendars()`), the sync worker's `fetchItems` runs **before**
the async `loadCalendars` PROPFIND discovery has populated
`m_calendars[calendarId].davUrl`. The null-URL guard then fails the operation
("No DAV URL registered"), so:

- the remote side reads as empty / errors, and
- brand-new local events are **never uploaded** to the server on that first sync.

A second sync works, because discovery has completed by then and the URL map is
populated.

## Evidence

PlanStan `tst_sync_caldav_conflicts` (gated `PLANSTAN_ENABLE_CALDAV_TESTS=ON`):
the only two tests that write **local-only** events and depend on the
create-on-remote path — `testCalDavFirstSync`, `testCalDavRecurrenceRoundTrip` —
fail; the 6 that pre-`PUT` to CalDAV or run two syncs pass. Instrumented log
(`QT_LOGGING_RULES=kalburator.*.debug=true`), ordering condensed:

```
RemoteCalendarBackend: Loading calendars for collection: "caldav-sync-test"   ← discovery STARTS (async)
SyncEngine: Worker thread started                                             ← first sync starts
RemoteCalendarBackend::updateCalendar: HTTP status 207 → updated successfully
RemoteCalendarBackend::fetchItems: No DAV URL for calendar: "synctest-…"       ← map still empty
RemoteCalendarBackend::loadRecords: fetchItems failed … "No DAV URL registered…"
QObject: Cannot create children for a parent that is in a different thread.
   (Parent is RemoteCalendarBackend, parent's thread …ab120, current …aaf8)
SyncEngineWorker::unifiedContinueAfterConflicts completed                      ← sync ends, nothing uploaded
RemoteCalendarBackend: discovered calendar "synctest-…" with URL http://…/     ← URL registered, too late
FAIL!: 'event1' returned FALSE
```

## Defect 1 — first-sync URL-map race (primary)

`davUrlFor(calendarId)` returns `m_calendars[calendarId].davUrl`, populated by
`registerCalendarUrl()`, async discovery, or `primeCalendars()`. For an inline
backend none of the first two have run when the first sync fires, and
`primeCalendars()` is provider-only. Guards that hard-fail:
`remotecalendarbackend.cpp:1279` (`fetchItems`), `:821` (`startSync`), `:1626`,
`:1734`.

The URL is derivable with no network round-trip — discovery itself builds it as
`m_url` (base, e.g. `http://user@host:5232/user/`) + calendarId + `/` (see the
`QUrl baseUrl = m_url;` discovery path ~`:387`). Suggested fix, either/both:

- **Lazy derivation:** in `davUrlFor` (or a helper the guards call), when the map
  has no entry, derive `configuredDavUrl(m_url + calendarId + "/")`, cache it in
  `m_calendars[calendarId].davUrl`, and proceed. Makes first sync robust
  regardless of discovery timing. (Confirm calendarId→path-segment escaping
  matches what discovery registers.)
- **Or await discovery:** have the backend report "not ready to sync" until the
  initial `loadCalendars` completes, and have the sync orchestration gate the
  first run on it. Heavier; the derivation is the smaller fix.

## Defect 2 — thread-affinity violation (secondary, latent UB) — RESOLVED in v0.72

**Was:** `fetchItems`/`pushItems`/`deleteItems` each did `new XOperation(…, this)`
**synchronously on the sync worker thread** (the blob-view CRUD adapters —
`loadRecords`/`createRecord`/`deleteRecord` — run there), before the
`QMetaObject::invokeMethod(this, …)` marshalled the real work onto the backend's
thread. `this` lives on the main thread, so parenting the op to it across the
boundary was illegal: Qt logged "Cannot create children for a parent that is in
a different thread", the parent link was silently dropped, and the op was
stranded with no thread affinity (`op->thread()` came back null) — yet its
completion lambda still ran on the backend thread and emitted `finished` from
there, i.e. mutating/signalling the op from a thread it didn't live on.

**Fix (chosen: "don't parent the op to `this`"):** the op is created unparented
and pushed onto the backend's thread via a small anonymous-namespace helper:

```cpp
template <typename Op>
Op *onOwnerThread(Op *op, const QObject *owner) {
    if (op->thread() != owner->thread())
        op->moveToThread(owner->thread());
    return op;
}
```

So `new FetchOperation(calendarId, this)` → `onOwnerThread(new FetchOperation(calendarId), this)`
(and likewise for Push/Delete). `moveToThread` is called from the op's current
(creating) thread, on a parentless object — both Qt preconditions hold — and is a
no-op on the common primed-provider path where the API is already invoked on the
backend thread. The op now lives where it completes, so `finished` is emitted
from its own thread; the worker-thread `awaitOperation()` receives it via the
normal cross-thread queued connection (`SyncOperation::state` is `std::atomic`,
so the await's `isFinished()`/`state()` reads are already safe). Ownership is
unchanged: the synchronous adapters `deleteLater()` the op they await, and there
are no raw async consumers relying on parent-based cleanup (the base destructor
never touches `m_pendingOperations`).

**Tests:** `tst_remotecalendarbackend_writepaths` +3 cases
(`{fetch,push,delete}Items_fromWorkerThread_opLivesOnBackendThread`): construct
the op-API call on a `QThread::create` worker (empty base URL → `davUrlFor`
returns nullopt → early-fail path, no network, but the op is still constructed
first — the site under test) and assert `op->thread() == backend.thread()`.
Watched RED (`Actual: <null>` + the affinity warning), then GREEN. Suite 14/14;
live Radicale lane + convergence + blob-view + subsequent-sync still green; the
warning is gone.

> Still-open related shape (NOT addressed here — separate teardown-ordering
> hazard, no repro yet): `ProviderManager::~ProviderManager → disconnectAll →
> unregisterProviderBackends` can cross threads at exit if a provider is still
> connected and the manager outlives the registry. Worth an audit glance when
> next in that code.

## Repro (in a PlanStan checkout pinned to v0.69)

```bash
cmake --preset dev -DPLANSTAN_ENABLE_CALDAV_TESTS=ON
cmake --build build-dev -j 8 --target tst_sync_caldav_conflicts
cd build-dev/tests/sync-workflow && QT_QPA_PLATFORM=offscreen \
  QT_LOGGING_RULES="kalburator.*.debug=true" \
  ./tst_sync_caldav_conflicts testCalDavFirstSync testCalDavRecurrenceRoundTrip
```

A libkalburator-side unit test for this would write a directly-configured
(unprimed) `RemoteCalendarBackend`, fire a sync immediately, and assert the new
item is `PUT` — `tst_remotecalendarbackend_writepaths` is the natural home (its
current coverage checks `calendarCreated` signals and a `PUT==0` case, not a
first-sync create-on-remote round-trip).
