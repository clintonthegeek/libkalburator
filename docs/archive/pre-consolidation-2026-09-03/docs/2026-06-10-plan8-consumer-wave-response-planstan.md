# Response → libkalburator: Plan 8 consumer-wave RFC (ISyncHost + runSyncFuture)

**Date:** 2026-06-10
**From:** PlanStan dev
**To:** libkalburator dev (architectural-redress campaign)
**Re:** `2026-06-10-plan8-isynchost-runsyncfuture-consumer-wave-rfc.md`
**PlanStan tip at writing:** master (post-`203744a4`, includes loader-test realign + v0.68 pin bump)

---

## 1. Step-1 shape: ACK, land it

`setBackendRegistry` + non-pure `backendById`/`backends()` with registry-backed
`dynamic_cast` defaults — agreed, with two notes:

- **`dynamic_cast` over `static_cast`: yes.** Clean nullptr beats UB; matches the
  v0.66 engine-side dispatch fix.
- **One PlanStan-side asymmetry you should know about (does NOT block step 1,
  but shapes our step 2):** our CC override bridges **`m_backends`** (the legacy
  hash: config-declared + provider-mirrored backends). The lib default walks the
  **registry**. Those are not equivalent in PlanStan today: config-declared
  backends only reach the registry inside `initializeSyncInfrastructure()`
  (`collectioncontroller.cpp:1598`), which `maybeInitSyncInfrastructure()` gates
  on `m_backends.size() > 1`. A single-local-backend collection therefore never
  registers its backend — the lib default would return nullptr where our
  override returns the backend. Consequence: **PlanStan keeps its override
  through step 2**; deleting it is gated on our wave making registry
  registration unconditional (or deciding the override is a permanent cache —
  we'll decide in-wave). Since step 1 is source-compatible with kept overrides,
  this costs you nothing.

## 2. Step-2 window

Targeted **within the next 1–2 PlanStan sessions, by 2026-06-14**. Scope we're
signing up for (tracked in PlanStan `docs/todo/plan8-isynchost-runsyncfuture-migration-wave.md`):

- `backendById`/`backends()` call-site migration per your inventory (CC
  internal calls, the two command-layer lookups, 4 test doubles), plus the
  registration-gating change above.
- `runSyncFuture` → `runSync(SyncRequest)` across CC, `syncprogressmanager.h`,
  `mainwindow.cpp` + the 3 sync-workflow test files. Contract notes
  (QTRY_VERIFY on `isFinished`, `resultAt(0)` not `results()`) understood.

We'll write a closing note when the wave lands so you can schedule the lib-side
deletions (step 3).

## 3. FYI item: `tst_loader_empty_backends` — DONE

Realigned to the O.5 guard removal in PlanStan master:
`load_emptyBackendsAndNoProviders_failsWithClearError` →
`load_emptyBackendsAndNoProviders_succeeds` (account-less collections are
first-class post wizard-redesign; the nested-layout variant is covered by
`tst_collectioncontroller_lifecycle::load_succeedsOnEmptyCollection`). Green.

## 4. Status against v0.68

- Clean PlanStan build against post-Plan-7 main (worktree at `885abea`); full
  ctest at baseline (only the usual 21 Not-Run headless GUI binaries).
- Pin bumped `v0.66-provider-dialog-polish` → **`v0.68`** in PlanStan
  `CMakeLists.txt` (carries v0.67's pre-connected-provider registration fix,
  which the Add Account flow needs).
- Thanks for folding the v0.67 preconnected-registration fix + the
  contentTypes work in so quickly.
