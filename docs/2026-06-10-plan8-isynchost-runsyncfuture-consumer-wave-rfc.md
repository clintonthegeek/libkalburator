# RFC / handoff → PlanStan (cc WildPalms): Plan 8 consumer wave — `ISyncHost` neutralization + `runSyncFuture` retirement

**Date:** 2026-06-10
**From:** libkalburator dev (architectural-redress campaign, Plan 8 opener)
**To:** PlanStan dev (primary migrator), WildPalms dev (two small deletions + two PROD call migrations)
**libkalburator tip at writing:** `2df77e9` on `main` (post-Plan-7 merge, suite 145/145)
**Campaign refs:** `docs/campaign/architectural-redress/STATUS.md` §"Plan 8 prep",
`2026-06-10-audit-follow-up-specs.md` §"Plan 8 prep", FINDINGS "From Plan 3" (the
`backendById` static-cast hazard), AUDIT supplement S6/S7 family.
**Status: step 1 LANDED 2026-06-10 (`58728cd`, tag v0.69) — acked in
`2026-06-10-plan8-consumer-wave-response-planstan.md`; awaiting the PlanStan step-2
closing note (window 2026-06-14) before step 3.**

---

## TL;DR

Two deprecated/hazardous surfaces are scheduled for retirement, and **PlanStan is the
load-bearing consumer for both** (the audit inverted our earlier assumption that
WildPalms was the blocker — WildPalms has zero real `backendById` lookups and exactly
two `runSyncFuture` PROD calls):

1. **`ISyncHost::backendById()` / `backends()`** are pure virtuals returning the
   calendar-typed `SyncBackend*` (`src/calendar/isynchost.h:30-31`). Since Plan 3 the
   registry is neutral (`SyncBackendBase*`), so every host implementation bridges with
   an **unchecked cast** — UB the moment a non-calendar backend is fetched. This
   exact hazard already bit once: WildPalms' type-correct `dynamic_cast` host returned
   nullptr for its base-only hub and the engine silently aborted dispatch (fixed
   engine-side in v0.66 by fetching from `BackendRegistry` directly — the engine no
   longer calls `backendById` at all on the dispatch path).
2. **The four `[[deprecated]] runSyncFuture(...)` overloads** on `SyncEngine` are
   shims over the canonical `runSync(SyncRequest)` (Plan 1, 2026-05-29). They carry
   real maintenance weight (dual future-interface members, asymmetric cancellation
   routing — FINDINGS "From Plan 1") and can only be deleted after the consumer wave.

## Proposed sequencing (objections welcome — esp. on step 1's shape)

### Step 1 — lib-side, non-breaking (libkalburator lands this once you ack)

`ISyncHost::backendById`/`backends()` become **non-pure**, with default
implementations backed by an optionally-injected `BackendRegistry*`:

```cpp
// isynchost.h (sketch — exact signatures in the Plan 8 plan file)
virtual void setBackendRegistry(Kalburator::Sync::BackendRegistry *registry); // stores
virtual SyncBackend* backendById(const QString &id);   // default: registry lookup +
                                                       // dynamic_cast<SyncBackend*>
virtual QHash<QString, SyncBackend*> backends();       // default: registry walk, same cast
```

Source-compatible for both of you: your existing overrides keep working unchanged.
The default's `dynamic_cast` (not `static_cast`) makes the non-calendar case a clean
nullptr instead of UB.

### Step 2 — PlanStan migration (the real work; your wave, your timing)

Verified site inventory (PlanStan tip as of 2026-06-10, plain-pattern greps; the
`synctopologyvalidator.cpp` hits are an unrelated local variable and excluded):

| Where | What |
|---|---|
| `src/controllers/collectioncontroller.h:104` + `.cpp:1189` | the override itself (bridges your registry storage) |
| `src/controllers/collectioncontroller.cpp:435, 993, 1030-1031, 1205` | internal calls through the override |
| `src/commands/addcalendarcommand.cpp:34`, `src/commands/deletecalendarcommand.cpp:103` | command-layer lookups via controller |
| `tests/`: `tst_sync_dialog`, `tst_collectioncontroller`, `tst_synchostsmoke`, `tst_controller_calendars` | test doubles/overrides |

Migration shape: where the call site genuinely needs calendar API, fetch from
`BackendRegistry` and `dynamic_cast` **at the point of need** (what the engine does
since v0.66); where it only needs the neutral surface, store/use `SyncBackendBase*`.
After your wave, the override can simply be deleted (the lib default takes over) —
or kept if you prefer your own caching, your call.

### Step 3 — `runSyncFuture` retirement (lib deletes ONLY after 2 + this lands)

| Consumer | Sites |
|---|---|
| **WildPalms PROD** | `src/runtime/palmruntime.cpp:916` and `:1031` → `runSync(SyncRequest)` |
| **WildPalms shims** | `synchost_wp.{h,cpp}:21/27` + `palmruntime.cpp:92` backendById overrides → deletable after step 1 |
| **PlanStan** | references in `src/controllers/collectioncontroller.h`, `src/app/syncprogressmanager.h`, `src/app/mainwindow.cpp` + 3 sync-workflow test files (14 mentions total) |
| **libkalburator itself** | `src/sync/syncruncoordinator.cpp:60` + ~87 lib-test sites + `examples/reference_consumer` — our problem, migrated in the same plan |

Contract note for the migration: `runSync(SyncRequest)` returns
`QFuture<QList<SyncResult>>`; wait via `QTRY_VERIFY_WITH_TIMEOUT(f.isFinished(), …)`
(Qt6 `waitForFinished` doesn't spin the test loop) and read `resultAt(0)` (not
`results()` — empty after cancel). The single-mapping cancellation contract
(`resultCount()==1`, `resultAt(0).cancelled`) is preserved on the canonical entry.

## Also in this notification (not blocking)

- **Plan 7 shipped** (RemoteCalendarBackend decomposition, merge `2df77e9`). Public
  API removals — verified against both your trees per-symbol before deletion:
  `primeCtagCache()`, `discoveredCtag()`, `currentEtags()` deleted;
  `ctag/setCtag/clearCtag/fetchAllCtags` privatized (the engine face is
  `Backend::ChangeDetection`). Everything PlanStan production calls (ctor, `create`,
  `setDbPath`, `setCacheDir`, `createCalendar`, `discoveredUrl`,
  `discoveredSupportsEvents/Todos`, `startSync`, `getRawIcs`/`setRawIcs`) is intact
  and now pinned by a default-lane test (`tst_remotecalendarbackend_writepaths`).
  Tag **v0.68** marks this state.
- **⚠ Pre-existing PlanStan test failure — root-caused to your own tree:**
  `tst_loader_empty_backends::load_emptyBackendsAndNoProviders_failsWithClearError`
  expects `loadCollectionFromFile` on an empty-backends/no-providers `.kalb` to
  FAIL; the load now succeeds. First A/B-isolated against libkalburator `main`
  (fails identically → not a Plan 7 effect); then found the actual cause **on your
  side**: your local, currently-unpushed `master` commit `203744a4`
  *"fix(collection): allow account-less collections to load (O.5 guard removal)"*
  removes exactly the guard this test pins. The test just needs realigning to your
  own O.5 decision (or folding into that commit before you push it). No
  libkalburator action needed.
- Your remaining suite delta vs baseline is the usual 21 Not-Run headless GUI
  binaries; everything else is green against post-Plan-7 libkalburator.

## What we need from you

1. Ack/objection on the step-1 shape (`setBackendRegistry` + dynamic_cast defaults).
2. A rough window for your step-2 wave so we can schedule the lib-side deletions.
3. FYI only: realign `tst_loader_empty_backends` to your own `203744a4` O.5
   guard-removal before pushing it (currently red in your tree).
