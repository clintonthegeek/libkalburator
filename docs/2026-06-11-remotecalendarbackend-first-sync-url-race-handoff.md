# RemoteCalendarBackend: first-sync DAV-URL race + thread-affinity violation

> **Defect 1 RESOLVED in v0.71** (`d306c54`, derive-on-miss in `davUrlFor`).
> Verified: libkalburator `tst_remotecalendarbackend_writepaths` 11/11 (new
> `startSync_undiscovered_calendar_derives_url_and_writes` regression + live
> Radicale lane + convergence); PlanStan `tst_sync_caldav_conflicts` 10/10
> against the pristine v0.71 tag (was 8/10). **Defect 2 (thread-affinity)
> deferred** — confirmed benign for the sync path (the warning prints but the
> PlanStan tests pass with it present); see the section below, still open.

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

## Defect 2 — thread-affinity violation (secondary, latent UB)

`fetchItems` (`:1276`) does `new FetchOperation(calendarId, this)` and
`QTimer::singleShot(0, op, …)` (`:1282`) **synchronously on the sync worker
thread**, before the `QMetaObject::invokeMethod(this, …)` (`:1295`) marshals to
the backend's thread — so QObjects are parented across a thread boundary
(`this` lives on the main thread; the worker is another). Qt logs "Cannot create
children for a parent that is in a different thread." Marshal the op creation
(and the deferred-failure timer) onto the backend thread first, or don't parent
the op to `this`.

> One related shape worth an audit glance (from PlanStan's Plan-8 wave note):
> `ProviderManager::~ProviderManager → disconnectAll → unregisterProviderBackends`
> has the same cross-thread/teardown-ordering hazard if a provider is still
> connected at exit and the manager outlives the registry.

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
