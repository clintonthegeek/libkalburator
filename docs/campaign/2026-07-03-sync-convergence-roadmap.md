# Sync-convergence campaign — roadmap

**Date:** 2026-07-03 (trimmed to remaining work 2026-07-04)
**Current state:** Tracks A, B, and C are **complete** — tagged v0.80, v0.81,
v0.82; PlanStan pinned to v0.82 and live-verified (fast path converges on a
real account). Only **Track D** remains. Full Track A/B/C phase specs (problem,
evidence, fix design, tests) are archived in
`docs/campaign/archive/2026-07-03-sync-convergence-tracks-a-b-c.md`; landing
detail is in `docs/campaign/FINDINGS.md` and §5 below.
**Origin:** PlanStan investigation of a real-world account-based Nextcloud
collection (`~/Documents/NewCollection2.kalb`, log `PlanStan/longlog.txt`).
Findings doc: `PlanStan/docs/bugs/sync-nonconvergence-vtimezone-corruption-and-dav-transport.md`
(PlanStan commit `3e6c24f3`) — finding IDs **N1–N9** used below match that doc.
**Consumers affected:** PlanStan (primary), WildPalms, libkalcal (transitive pin).
Cross-consumer coordination rules: `docs/campaign/INVARIANTS.md` §10.

---

## 0. How to use this roadmap

Each phase below is sized for one agent session and is self-contained: problem,
evidence, exact code references (file:line on `main` @ `14cd210` unless marked
otherwise), fix design, RED-first test plan, and acceptance gate. Work strictly
in phase order within a track; tracks A/B can interleave, track C (PlanStan)
follows its listed lib dependency.

**Read before any change:** `docs/campaign/INVARIANTS.md` (especially: extend
the shape graph, never fork a mechanism; one definition per field-mapping;
fail loud, never silently-empty).

**Build & test (libkalburator):**
```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -- -j 8        # -j 8 ONLY — GCC ICEs under all-core load
ctest --test-dir build -j 8
```
Baseline: **157 tests green at v0.82** (default profile: `KALBURATOR_HAVE_ORG_IO=OFF`,
`KALBURATOR_HAVE_AKONADI=OFF`). Live probes: `-DKALBURATOR_BUILD_LIVE_PROBES=ON`
(`tests/sync/CMakeLists.txt:308`), local Radicale at `localhost:5232`
(testuser1/password1 — disposable).

**Build PlanStan against the working tree** (for end-to-end verification):
```bash
cd ~/dev/PlanStan
cmake -B build-dev -DPLANSTAN_DEV_BUILD=ON \
      -DPLANSTAN_LIBKALBURATOR_SOURCE_DIR=$HOME/dev/libkalburator
```
PlanStan's release pin is `PLANSTAN_LIBKALBURATOR_GIT_TAG` at
`PlanStan/CMakeLists.txt:69` (currently `v0.82`); bump it only at the tag
points listed in §4.

**Evidence artifacts (do not assume they still exist; copy what you need):**
- `~/dev/PlanStan/longlog.txt` — full log of the failing session (2 collection
  opens + 3 sync cycles). Key line ranges quoted inline below.
- `~/Documents/NewCollection2.kalb[.d]` — the corrupted collection.
  Prize exhibit: `calendars/TBS/81f191f2-ee06-44d8-9a45-2526d3284fd0.ics`
  (a one-off event carrying six RRULEs + stray RDATEs copied from its
  VTIMEZONE). Pristine originals live in
  `cache/caldav-cache-*.db` (`cached_items(url, etag, ical_content, fetched_at)`).

---

## 1. The failure story (shared context for every phase)

A PlanStan local-mirror collection (LocalBackend = primary, per-calendar
multiproto-DAV backends = sync1 spokes, 7 mappings, ~880 items) soft-froze the
GUI every 120 s. The timer tick is not "a slow update check" — it is a **full
sync that can never converge**, because:

1. **VTODOs transcode to empty bytes** through the calendar canon stages
   (known F4 — `docs/2026-06-28-calendar-vtodo-vjournal-shape-dispatch-handoff.md`);
   todo-only mirror calendars are 100 % zero-byte files that fail to parse on
   every subsequent read.
2. **VEVENTs transcode corrupted** (new, N1): the canon stage scrapes
   `RRULE:`/`RDATE:`/`EXDATE:` lines from the *whole* VCALENDAR text —
   including `VTIMEZONE` — so any event with a TZID gains its timezone's DST
   transition rules as event recurrence. The corrupted iCal is invalid
   (multiple RRULEs), so pushing it to Nextcloud fails **415/400** (~50 PUTs
   rejected per cycle; log lines ~940–1030).
3. **Change detection can never report "unchanged"** (new, N2): the diff
   compares SHA-256 of each backend's *native bytes* against a *single*
   baseline hash. Local (KCalendarCore-serialized) and remote
   (server-serialized) bytes always differ → every record reads modified or
   conflicted on every cycle, forever. Failed writes (from #2) additionally
   mark `writeFailed`, so baselines are never advanced (that guard itself is
   CORRECT — see DO-NOTs) and the next cycle repeats identically.
4. All of that work — CTag PROPFINDs, doomed PUTs, PROPPATCHes, full local
   re-parse, model repopulation — executes on the **GUI thread** (N7), 7
   mappings sequentially. Hence the 120 s soft-freeze.
5. Independent transport/integrity defects: a 673-href multiget dies on HTTP/2
   and is misreported as a 401 (N4); the persisted CTag can get ahead of the
   content cache so a populated server calendar reads back as "empty,
   fresh, success" (N5); remote blob records stamp `lastModified = now`,
   rigging LastWriteWins toward the remote (N3).

**End state this campaign must reach:** a mirror collection, once synced,
whose next sync with no user edits performs **zero writes, zero conflicts,
zero item fetches** (revision short-circuit only), completes in well under a
second per calendar, and never blocks the GUI thread on network I/O.

### DO-NOTs still relevant to remaining work (Track D)

- **Do not** treat the Nextcloud 415s as a server quirk to retry around. They
  are the server correctly rejecting corrupted payloads, and currently the
  only thing protecting server-side data.

(The other campaign DO-NOTs — baseline-saving on failed writes, canon-form
hashing, enabling the skip flag early, and the triplicated recurrence-scraper
scope — were guardrails for Tracks A/B, now landed; see the archived tracks
doc if you need the original reasoning.)

---

## 2. Phase index & ordering

**Tracks A, B, C — complete.** See `docs/campaign/archive/
2026-07-03-sync-convergence-tracks-a-b-c.md` for the full phase specs and §5
below for landing detail. Tags: v0.80 (A), v0.81 (B1-3), v0.82 (B4-5).

**Track D — architecture & backlog (remaining):**
- D1. N7: move DAV I/O off the GUI thread (lib enablement + PlanStan adoption)
- → tag **v0.83**
- D2. Backlog: RFC 6578 sync-collection, ETag-cache persistence, misc protocol

D1 is independent of Track B but lands after B5 so freeze-fix claims are
measurable against an already-cheap cycle (satisfied — B5 is done).

---

## Track A/B/C detail — archived

Full phase specs (problem, evidence, exact code references, fix design, RED
test plan) for the now-complete Tracks A, B, and C live in
`docs/campaign/archive/2026-07-03-sync-convergence-tracks-a-b-c.md`. Landing
detail (what actually shipped, deviations from spec, verification numbers) is
in §5 below and `docs/campaign/FINDINGS.md`.

## Track D — architecture & backlog

### Phase D0 — apply-phase `ISyncHost::recordChanged` wiring (landed 2026-07-04)

**Problem.** `ISyncHost::recordChanged(mappingId, recordId, kind)` (G.9.a,
`isynchost.h`) was declared but never invoked anywhere in the engine — grep
turned up only the interface declaration and no-op stubs. PlanStan's consumer
side (`CollectionController::recordChanged` → `ItemLoadingCoordinator`) was
correct and ready, but dead: on an already-open collection, a remote→local
change materialized by a later sync run didn't reach the view model until the
*next* fetch cycle (~120 s) or a reopen. Full origin: PlanStan
`docs/todo/sync-apply-phase-model-refresh.md`.

**Fix.** `SyncEngineWorker::unifiedContinueAfterConflicts`'s `applyBatch`
lambda (`syncengine.cpp`) now calls `m_controller->recordChanged(mappingId,
recordId, kind)` for every record it classifies into `WriterBatch`
creates/updates/deletes, for the write onto the mapping's **source** side
only (`notifyHost=true` on that call site, `false` on the target-side call).
Source is the primary/local side in a PlanStan-style local-mirror mapping
(`syncmappinggenerator.cpp` always passes the logical calendar's primary
binding as `s`/source); consumers that re-read from source regardless of
which side changed (PlanStan's `recordChanged`) get exactly the live-refresh
signal they were built for, without a redundant notify on the push-to-target
half of the same sync.

**Tests.** New `tests/calendar/tst_calendar_recordchanged_notifications.cpp`
(4 cases): target-side create/update/delete each propagate to source and
fire the matching `ChangeKind`; a source-side-only change (pushed to target)
fires no `recordChanged` at all. Full suite still green at the same 2
pre-existing/unrelated flakes (`tst_sync_convergence`
`remoteEditFetchesExactlyOneChangedItem` multiget-count assertion,
reproduces identically on unmodified `main`; `tst_engine_single_mapping_cancel`
segfaults only under `ctest -j` parallel contention, passes standalone).

**Status:** merged to `main`, **not yet tagged** — ships in the same release
as D1 (v0.83) since PlanStan is still pinned to v0.82. PlanStan's initial-sync
reload mitigation (`CollectionController::createLogicalCalendar` →
`reloadModelEligibleCalendars()`) stays in place until the pin bumps past this
commit; drop it then per the todo doc.

### Phase D1 — N7: DAV I/O off the GUI thread

> **Superseded pointer (2026-07-05):** D1 was expanded into the
> **sync-hardening campaign** after a first-principles audit
> (`docs/campaign/2026-07-05-first-principles-sync-architecture-audit.md`)
> found eight issues beyond N7 (FINDINGS O17–O24). The live, task-level
> plan is now **`docs/campaign/2026-07-05-sync-hardening-phases.md`**
> (phases H1–H9; v0.83 tags at its H6). The old execution plan
> `2026-07-04-d1-threading-execution-plan.md` is superseded (its T1.1–T1.5
> landed; its Stages 2–4 became hardening H7/H8). The section below is the
> original phase sketch, kept for context only.

**Problem.** Backends live on the consumer's main thread. The engine worker
marshals every read/classify/apply to them via `Qt::BlockingQueuedConnection`
(sites: syncengine.cpp:1643, 1713, 2004, 2050, 2096, 2116, 2156, 2675, 2691),
and `davSyncRequest` (`remotecalendarbackend.cpp:202–248`) is a synchronous
nested `QEventLoop` — so CTag PROPFINDs, item CRUD PUT/DELETE, PROPPATCHes,
and LocalBackend's full-directory parse all execute on the GUI thread. This
is the 120 s soft-freeze (and was already noted in PlanStan's
`docs/bugs/sequential-sync-performance.md`, 2026-04-10). Tracks A/B remove
most of the *work*; this phase removes the *architecture* that parks the
remainder on the UI.

**Lib-side enablement (this repo):**
1. **Shared QNAM per backend:** `davSyncRequest` constructs a fresh
   `QNetworkAccessManager` per call (`:224`) → new TCP+TLS handshake per
   request (20+ per idle cycle pre-B5) and the stray
   `QIODevice::read (QSslSocket): device not open` warnings. Give
   `RemoteCalendarBackend` one lazily-created, thread-affine QNAM and pass it
   in. Easy, land first, independently valuable.
2. **Thread-relocatability audit:** everything the backend touches must be
   created lazily in its owning thread: `CalDavContentCache` /
   `CTagStore` QSqlDatabase connections (thread-affine by Qt contract — audit
   `ensureOpen` paths), `m_etagCache`, KDAV jobs (created in `fetchItems`,
   parented `this` — fine once `this` lives on the I/O thread). Ops already
   handle affinity via `onOwnerThread` (`remotecalendarbackend.cpp:313`,
   the v0.72 fix). Document the contract: "backend may live on any thread;
   all public entry points are invoked on its thread via queued/blocking
   connections" — which the engine already honors by construction.
3. Verify `LocalBackend` likewise (pure file I/O + QSqlDatabase-free — should
   be trivial).

**Consumer side (PlanStan):** create backends on a dedicated I/O `QThread`
(owned by CollectionController): LocalBackend construction and the provider
`createBackend` path (multiproto per-calendar backends) either construct
there or `moveToThread` before first use (requires: no parent — audit
ownership). All existing cross-thread signal consumers (ItemLoadingCoordinator
etc.) already connect via default auto-connections → become queued
automatically. Gate: GUI event-loop stall probe (a QTimer heartbeat asserting
< 50 ms gaps) stays clean through a full sync cycle over a
latency-injected FakeCalDavServer.

**Deliberately deferred:** parallel mapping execution
(`src/engine/mappingscheduler.*` / `mappingqueue.*` stay sequential) — with
B5 the idle cycle is cheap and the busy cycle is I/O-bound on one server;
parallelism is a separate risk/benefit decision.

**Tag v0.83.**

### Phase D2 — backlog (file separately when picked up)

- **RFC 6578 `sync-collection` REPORT:** replace the per-poll Depth:1 ETag
  PROPFIND (`DavItemsListJob`, `:1366`) with a sync-token delta for servers
  advertising `DAV: sync-collection` (Nextcloud does). Big win for very large
  calendars; keep CTag+PROPFIND as fallback.
- **Persist/seed the KDAV `EtagCache`:** it is in-memory per session, so the
  first fetch after every app open multigets *every* item even though
  `CalDavContentCache` (persistent, keyed url+etag) holds the bytes. Seed the
  EtagCache from the content cache at backend init (or persist it) so a
  restart with unchanged CTag serves entirely from disk. (Post-B3 the CTag
  short-circuit already covers the unchanged case — this item covers
  changed-CTag-but-mostly-unchanged-items.)
- **`updateRecord` wrong-calendar fallback:** the "try all registered
  calendars, first success wins" loop (`remotecalendarbackend.cpp:2097–2103`)
  can write an item into the wrong calendar on a multi-calendar raw backend
  and multiplies failed-PUT latency. Restrict to the calendar that owns the
  record (href map / cache lookup), fail otherwise.
- **`RecordMergerICal` is dead-ish code that parses canon JSON as iCal**
  (`src/calendar/icalrecordmerger.cpp:33–53` — inputs are canon-shaped in the
  unified path; `parseIcal` returns null and the function degrades to
  side-picking). The active merger is `CanonJsonMerger`
  (`calendardomaindefinition.cpp:36–40`). Delete or fix `RecordMergerICal`
  and its registration so nobody re-wires it by accident.
- **Property-phase PROPPATCH noise:** `runPropertyPhase` pushes calendar
  color/description every cycle if baselines miss (evidence log
  `updateCalendar … 207` per cycle for some calendars) — verify the T9
  property-baseline snapshot (syncengine.cpp:2782–2807) suppresses repeats
  post-B4.

---

## 4. Release & coordination summary

| Tag | Contents | Consumer action |
|---|---|---|
| v0.80 | A1 (per-kind dispatch Tasks 4–9) + A2 (N1 recurrence fix) | PlanStan pin bump; re-verify F4 GUI repro; delete/close F4 bug docs |
| v0.81 | B1 (N3) + B2 (N4) + B3 (N5) | PlanStan pin bump; C1 guard lands alongside |
| v0.82 | B4 (N2 per-side baselines, schema v5) + B5 (convergence gate + fast path) | PlanStan pin bump; flip `syncSkipUnchanged` default; C2/C3; **C4 live verification** (done) |
| v0.83 | D0 + D1 (threading + shared QNAM) + sync-hardening H1–H5.5 (O16–O25) | PlanStan I/O-thread adoption (hardening H7); close `sequential-sync-performance.md` freeze half. **Consumer notes:** (a) engine no longer calls `cachedCollectionRevision`/`primeRevisionCache` — WildPalms must verify any independent usage; (b) BaselineStore schema **v7** (additive `sync_tokens` table, forward-only self-migrating) |

Every lib phase: feature branch → RED tests → implement → full suite (157+ at
baseline) → merge → update this roadmap's status line for that phase **in the
same commit** (PlanStan CLAUDE.md "rule of thumb" applies here too). WildPalms
consumes these tags on its own schedule — flag breaking changes (B4's
`perRecordDiff` signature is internal; the BaselineStore schema bump is
forward-only and self-migrating) in the tag message per INVARIANTS §10.

## 5. Phase status (update in the landing commit)

- [x] A1 per-kind dispatch Tasks 4-9 — merged to main 2026-07-03
- [x] A2 N1 component-scoped recurrence extraction — merged to main 2026-07-03
- [x] — tag v0.80
- [x] B1 N3 remote lastModified honesty — merged to main 2026-07-04
- [x] B2 N4 multiget chunking + error truth — merged to main 2026-07-04
- [x] B3 N5 CTag/content-cache coherence — merged to main 2026-07-04
- [x] — tag v0.81
- [x] B4 N2 per-side baselines (the convergence fix) — landed 2026-07-04
- [x] B5 convergence acceptance gate + fast path — landed 2026-07-04
      (superseded by H3 per-mapping tokens, 2026-07-05: the fast path's
      skip check now compares against BaselineStore's engine-owned
      per-mapping sync-progress token, not backend-side
      `cachedCollectionRevision`/`primeRevisionCache`; see
      `docs/campaign/2026-07-05-sync-hardening-phases.md` §6 and
      FINDINGS O17-O19)
- [x] — tag v0.82 (libkalburator main @ `1e985e5`, 2026-07-04)
- [x] C1 PlanStan mass-delete guard — landed
- [x] C2 PlanStan spoke-loading fix — landed (residual found + fixed in C4)
- [x] C3 PlanStan auto-sync ordering — landed
- [x] C4 pin bumps + live end-to-end + recovery-scope — **fully closed
      2026-07-04**: sync converges live on a real account, fast path verified
      converging by cycle 2 on reopen. Full narrative (three PlanStan-side
      issues found and fixed along the way) archived in
      `docs/campaign/archive/2026-07-03-sync-convergence-tracks-a-b-c.md`
      and `PlanStan/CLAUDE.md`.
- [x] D0 apply-phase `recordChanged` wiring — landed 2026-07-04 on `main`,
      untagged (ships with v0.83 alongside D1)
- [x] D1 N7 threading + O16–O25 hardening — **tagged v0.83 2026-07-05**
      (hardening H6): `feature/d1-threading` merged to `main` (`--no-ff`)
      as `e32fac3`. The sync-hardening campaign
      (`docs/campaign/2026-07-05-sync-hardening-phases.md`) landed lib-side
      phases T1.1–T1.4 + H1–H5.5, closing FINDINGS O16–O25. PlanStan
      I/O-thread adoption remains open at hardening H7/H8.
- [ ] D2 backlog triage
