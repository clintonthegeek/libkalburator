# Handoff → libkalburator: redundant per-backend calendar-list discovery on collection open

> **CLOSED (2026-06-03):** Resolved by the v0.63 RemoteCalendarBackend convergence: the
> CalDAV discovery primer (`PrimedCalendar`) short-circuits the per-backend server-wide
> PROPFIND on collection open. Zero redundant PROPFINDs verified live against Radicale.

**Date:** 2026-06-03
**From:** PlanStan dev (via an autonomous engineering agent working in the
PlanStan tree)
**Component:** `src/sync/caldavprovider.cpp`, `src/calendar/remotecalendarbackend.cpp`
**libkalburator tip examined:** `8a35e54` on `main` (post-Plan-5 merge)
**Related PlanStan-side fix:** `Phase R: collapse PlanStan-side signal storm
+ discovery redundancy` (PlanStan commit `6efa0f99`, merged to master
2026-06-02 as part of the Tier-A wave)
**Related prior handoff:** `docs/2026-05-27-libkalburator-content-cache.md`
(content-cache filename non-determinism — the other half of "Phase R" that
remained libkalburator-side)

---

## TL;DR

On collection open, every bound logical calendar's `RemoteCalendarBackend`
independently PROPFINDs the entire CalDAV server's collection list — even
though `CalDavProvider::connect()` has already done a clean discovery walk
and `m_calendarUrls` (plus per-calendar capabilities) sit cached at the
provider. With N bound calendars on a server hosting M of them:

- **N redundant PROPFINDs** issued at session open (one per backend)
- **N × M `calendarDiscovered` signals** fan out upstream — quadratic when
  M ≈ N
- **Each PROPFIND returns the full M-item list** — the per-backend
  `registerCalendarUrl()` value, just handed in, is then ignored by the
  backend's own discovery

PlanStan just landed a fix for the upstream half of this (PlanStan
`BackendDiscoveryCoordinator` now counts ignored-calendar callbacks per
backend and emits a single summary line; `CollectionController` now
guards `loadCalendars` against duplicate per-backend invocations from
the two control-flow entry points). The wire-level redundancy is still
libkalburator's problem to solve.

This handoff proposes a primer pattern analogous to the existing
`primeCtagCache`/`fetchAllCtags` flow (header lines `145–153` and
`128–142`), letting the provider seed each backend's per-calendar
metadata directly from the connect-time discovery so the per-backend
`loadCalendars` PROPFIND can be skipped entirely.

---

## My understanding of the current architecture

### Phase 1 — provider connect (correct, one PROPFIND walk)

`CalDavProvider::connect()` (`src/sync/caldavprovider.cpp:51`) spawns a
single `CalDavCapabilityDiscovery`. That class does the 3-step PROPFIND
chain documented in its own header
(`src/sync/carddavcapabilitydiscovery.h:24-27` — and the equivalent
walk for CalDav lives in the sibling `caldavcapabilitydiscovery.{cpp,h}`):
root → current-user-principal → calendar-home-set → home-set Depth:1 for
calendar collections. On success, `onDiscoveryFinished`
(`caldavprovider.cpp:94`) populates:

- `m_calendarUrls` — full map of calendar id → DAV URL
  (`caldavprovider.cpp:101`)
- `m_collections` — full list of `CollectionInfo` per server calendar,
  each carrying `name`, `type`, `isDefault`, and (Phase 2C seed)
  `readOnly = !writable` (`caldavprovider.cpp:102-113`, commit `0ded2ec
  feat: DAV providers populate CollectionInfo.readOnly from discovered
  writability`)

After `connect()` resolves, the provider has *everything* PlanStan needs
to know about every calendar on the server. The idempotency fix from
v0.61 (commit `9f8a220 fix(sync): make provider connect() idempotent;
guard manager against bad futures`, merged via `04a9876`) means repeated
`connect()` calls don't redo this work — also correct.

### Phase 2 — backend creation (correct, no PROPFIND)

`CalDavProvider::createBackend(collectionId)` (`caldavprovider.cpp:154`)
looks up `m_calendarUrls[collectionId]`, constructs a
`RemoteCalendarBackend`, and calls `registerCalendarUrl(collectionId, url)`
(`caldavprovider.cpp:166`). No network. The backend now knows its bound
URL.

### Phase 3 — where it goes wrong: `RemoteCalendarBackend::loadCalendars`

`RemoteCalendarBackend::loadCalendars(collectionId)`
(`src/calendar/remotecalendarbackend.cpp:473`) is called per backend
instance (PlanStan's `CollectionController` iterates the backend map and
calls `loadCalendars` on each, post-mirror). Internally:

```cpp
KDAV::DavUrl davUrl(m_url, KDAV::CalDav);            // :476
auto *fetchJob = new KDAV::DavCollectionsFetchJob(davUrl, this);  // :477
```

`m_url` here is the **provider's base URL**, not the per-backend URL
that `registerCalendarUrl` just stored. The fetchJob therefore
re-enumerates *all* calendars on the server. For each one, the result
handler (`:486-523`):

- stores it in `m_davUrls[calId]`, `m_calendarColors[calId]`,
  `m_calendarContentTypes[calId]`, `m_calendarCtags[calId]`
- emits `calendarDiscovered(collectionId, calId)` (`:521`)

So every backend instance ends up with full state for every calendar on
the server — and PlanStan (which only cares about the *bound* calendar
for that backend) is hit with N × M `calendarDiscovered` callbacks per
session open.

The wasteful part isn't that `loadCalendars` exists — backends do need
per-calendar state for sync. It's that the data is being re-acquired
from the network when the provider already has it. The
`registerCalendarUrl` value is effectively ignored by the discovery
path.

### What PlanStan's compensating fix did (PlanStan side, 2026-06-02)

For context, here's what PlanStan just landed and why I'm flagging the
remainder upstream:

- `BackendDiscoveryCoordinator` now counts ignored-calendar
  `calendarDiscovered` callbacks per backend in
  `m_ignoredCalendarsByBackend` and emits **one summary qDebug** at
  `onBackendDiscoveryFinished` (was N² lines per session: 132+ for a
  12-cal account).
- `CollectionController` now tracks `m_loadedBackends` and guards
  `loadAllBackendCalendars` against the duplicate `loadCalendars` calls
  produced by the two control-flow entry points (`startDiscoveryAndSync`
  + the mirror path's tail). Was 24 `CalDavProvider::registerCalendarUrl`
  / 24 `fetchItems` passes for 12 calendars; is now 12 / 12.

These collapse the log and call-count amplification but **don't touch
the underlying N redundant PROPFINDs** — each `loadCalendars` invocation,
even when reduced to N from 2N–3N, still re-discovers the whole server.
That's the residual that belongs upstream.

---

## Proposed direction

A primer pattern analogous to the existing
`RemoteCalendarBackend::primeCtagCache` (header lines `145–153`). The
provider already holds the answers from
`CalDavCapabilityDiscovery::discoveredCapabilities()`; let it seed
each backend at construction time so the per-backend `loadCalendars`
can be a near no-op (just emit `calendarDiscovered` for the cached
state).

### Step 1 — add a primer on `RemoteCalendarBackend`

```cpp
struct PrimedCalendar {
    QString    calendarId;
    KDAV::DavUrl davUrl;
    QColor     color;          // may be invalid
    KDAV::DavCollection::ContentTypes contentTypes;
    QString    ctag;           // may be empty
    QString    displayName;    // optional, for surfacing later
    bool       readOnly = false;  // already discovered (Phase 2C authority seed)
};

/// Seed per-calendar metadata that the provider already discovered.
/// After priming, the next loadCalendars(collectionId) call will skip
/// its own DavCollectionsFetchJob and emit calendarDiscovered for
/// each primed entry directly. Re-priming overwrites; priming
/// nothing is a no-op and loadCalendars falls back to PROPFIND.
void primeCalendars(const QList<PrimedCalendar> &calendars);
```

Internally, populate the same maps `loadCalendars` populates today
(`m_davUrls`, `m_calendarColors`, `m_calendarContentTypes`,
`m_calendarCtags`) and flag the primed set so `loadCalendars` knows
to skip the fetchJob.

### Step 2 — `loadCalendars` honors the primed cache

```cpp
void RemoteCalendarBackend::loadCalendars(const QString &collectionId) {
    if (!m_primedCalendars.isEmpty()) {
        for (const auto &id : m_primedCalendars) {
            emit calendarDiscovered(collectionId, id);
        }
        emit loadCalendarsFinished(collectionId, true);
        return;
    }
    // existing DavCollectionsFetchJob path …
}
```

This is the same shape as `fetchItems` already uses w.r.t. the primed
CTag cache today.

### Step 3 — `CalDavProvider::createBackend` primes from its discovery

```cpp
std::unique_ptr<IBlobBackend>
CalDavProvider::createBackend(const QString &collectionId) {
    if (!m_connected) return nullptr;
    const auto urlIt = m_calendarUrls.constFind(collectionId);
    if (urlIt == m_calendarUrls.constEnd()) return nullptr;

    auto backend = std::make_unique<RemoteCalendarBackend>(
        m_serverUrl, m_username, m_password);
    backend->registerCalendarUrl(collectionId, urlIt.value());

    // Seed just the bound calendar from CalDavCapabilityDiscovery state.
    if (m_discovery) {  // or a cached copy of discoveredCapabilities()
        const auto caps = m_discovery->discoveredCapabilities();
        if (caps.perCalendarCapabilities.contains(collectionId)) {
            const auto &c = caps.perCalendarCapabilities[collectionId];
            backend->primeCalendars({{
                collectionId,
                KDAV::DavUrl(urlIt.value(), KDAV::CalDav),
                c.color,
                c.contentTypes,
                c.ctag,
                c.serverDisplayName,
                !c.writable
            }});
        }
    }
    return backend;
}
```

Note one nuance: `CalDavProvider` currently `deleteLater()`'s its
`m_discovery` once `connect()` resolves
(`caldavprovider.cpp:127-132`), so the perCalendarCapabilities map
isn't reachable from `createBackend` time. Either keep `m_discovery`
alive until `disconnect()`, or copy the relevant struct out of it
in `onDiscoveryFinished` before letting it drop. The latter is
cleaner (smaller retained state) and matches what `m_collections`
already does (`caldavprovider.cpp:101-113`).

### What this preserves / collapses

Preserves:
- Per-backend ownership of per-calendar runtime state (sync still
  works the same; the maps the backend uses internally are populated
  the same way, just from cache instead of PROPFIND)
- The unprimed `loadCalendars` fallback (callers without a primer can
  still do the network walk — useful for standalone backend uses, test
  harnesses, etc.)
- Idempotency of `connect()` from the v0.61 fix — re-priming is fine,
  and `loadCalendars` short-circuits regardless of how it got primed

Collapses:
- N PROPFINDs at collection-open → 0 (the only PROPFIND walk is the
  one `CalDavCapabilityDiscovery` does once at provider connect)
- N × M `calendarDiscovered` emissions → N (one per backend, for its
  bound calendar only)
- Server load proportional to bound-calendar count drops accordingly
  (the connect-time walk doesn't change)

### Bigger alternative (probably out of scope)

A more radical refactor would move from "N backend instances, one per
bound calendar" to "one provider-scoped backend handling many calendar
handles." That would naturally eliminate the redundancy at the cost of
reworking `IBlobBackend`'s 1:1 collection-id assumption and threading
model. The primer pattern above is the minimal-disruption path; the
1:N model is a Phase-X conversation for later if you ever decide the
per-binding-backend split has outlived its usefulness.

---

## Test coverage suggestion

A unit test that:

1. Stands up `FakeCalDavServer` with M=5 calendars
2. `CalDavProvider::connect()`, asserts 1 PROPFIND-walk (3 requests
   via `CalDavCapabilityDiscovery`)
3. `createBackend()` for two specific calendar ids
4. Each backend's `loadCalendars()` — asserts **zero additional
   PROPFINDs**, and that each backend emits `calendarDiscovered` for
   exactly its bound calendar

The existing `FakeCalDavServer` (extended in `3c89cb9 Phase J prep: fix
configuredDavUrl; extend FakeCalDavServer with CRUD`) should already be
able to count PROPFIND requests; if not, that's a small extension.

---

## Coordination notes

- PlanStan master (`48d822a7` as of this writing) ships the
  compensating fix described above; it will continue to work fine
  with the primer landed. The compensating guard becomes redundant
  belt-and-braces once the primer is in place, but it's cheap and
  not worth ripping out preemptively.
- PlanStan currently builds against libkalburator `main`
  (`PLANSTAN_LIBKALBURATOR_SOURCE_DIR` override; the documented v0.61
  pin in PlanStan's `CMakeLists.txt:69` is stale — already noted as a
  PlanStan-side cleanup followup). When this lands, a v0.62-ish tag
  would be a natural pin target.
- Touches the same file as the existing `2026-05-27-libkalburator-
  content-cache.md` handoff. Reasonable to bundle both into a single
  branch if you're already in there (content-cache filename
  determinism + this primer).

Happy to review a branch against this design or answer questions about
PlanStan's call patterns if any of the above doesn't match what you're
seeing.
