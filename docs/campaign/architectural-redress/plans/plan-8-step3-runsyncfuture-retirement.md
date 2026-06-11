# Plan 8 step 3 — lib-side `runSyncFuture` retirement + dual future-interface collapse

**Audit refs:** AUDIT §4 (four overloads of one verb = one parameter-struct method);
FINDINGS "From Plan 1" (the three dual-iface / single-shim-bypass entries,
`syncengine.h:~445-501`, `syncengine.cpp:578-616`, `syncrequest.h:35-56`);
STATUS "Next action" + Locked decision **2026-06-10** (overload deletion gate).
**Predecessor:** Plan 8 step 1 (`v0.69`, `ISyncHost` registry defaults) + step 2
(BOTH consumer waves `runSyncFuture`-clean: PlanStan `58bd4835`, WildPalms `4dc3537`).
**Branch:** `feature/redress-8-runsyncfuture-retirement`
**Baseline at open:** `main` @ `348aec9`, ctest **147/147** (confirm at execution start).
**Scope decision (user, 2026-06-10):** collapse the dual future-interface **in this
plan**, not a follow-up — the recommended scope. Deleting the deprecated single shims
makes the collapse possible; doing both together is what closes the FINDINGS "From
Plan 1" entries instead of re-deferring them.

---

## Why now (gate cleared)

Both external consumers are `runSyncFuture`-grep-clean and pin `v0.69`
(STATUS Locked decision 2026-06-10). The only remaining callers are **lib-internal**:

| Caller | Form | Count |
|---|---|---|
| `src/sync/syncruncoordinator.cpp:60` | `runSyncFuture(behavior)` — **multi** | 1 |
| `examples/reference_consumer/main.cpp:300` | `runSyncFuture(Unmonitored)` — **multi** | 1 |
| `tests/**` (24 source files) | mixed single + multi | ~85 |

Deleting the four `[[deprecated]]` overloads now is a no-op for PlanStan/WildPalms
(they don't reference the symbols) — by design the deletion is the campaign's final
proof the consumer wave landed (INVARIANTS §10; Locked decision 2026-06-10).

## What the collapse buys (the payoff, not just deletion)

Today the canonical `runSync(SyncRequest)` single-mapping branch wraps the native
single-iface future via `.then()` (`syncengine.cpp:541-543`). Qt6's `QFuture::then()`
**drops its continuation when the source is canceled**, so a canonical single-mapping
consumer reading `resultAt(0)` after cancel sees `resultCount()==0` (the WildPalms
finding; FINDINGS "From Plan 1", STATUS Locked decision 2026-06-10). WildPalms worked
around it with a `resultCount()>0` guard + `isCanceled()` synthesis in their watcher.

After this plan the single-mapping path reports **natively** into the sole
`QFutureInterface<QList<SyncResult>>` (no `.then()`), so the F2-Task-23 cancellation
contract — `resultCount()==1`, `resultAt(0)` holding a one-element list whose
`SyncResult.cancelled==true` — is preserved on the canonical path. The guard becomes
**unnecessary** for the lib's own callers, and the three FINDINGS "From Plan 1"
dual-iface entries are resolved rather than re-deferred.

---

## Design

### The collapse: one interface, one watcher

`src/engine/syncengine.h` currently carries a mirrored pair (lines 579-580, 605-606):

```cpp
std::unique_ptr<QFutureInterface<SyncResult>>            m_currentSingleIface;
std::unique_ptr<QFutureInterface<QList<SyncResult>>>     m_currentMultiIface;
...
QFutureWatcher<SyncResult>*        m_singleWatcher = nullptr;
QFutureWatcher<QList<SyncResult>>* m_multiWatcher  = nullptr;
```

Both public + deprecated entry points already return `QList<SyncResult>` **except**
the two single-mapping shims (`runSyncFuture(mappingId, …)` → `QFuture<SyncResult>`).
Once those shims are deleted, **every** entry returns `QFuture<QList<SyncResult>>`, so
the `SyncResult`-typed iface/watcher have no reason to exist. Collapse to one, renamed
to drop the now-meaningless `Multi` qualifier (the pair is gone — INVARIANTS §5,
vocabulary; documented deviation-free because the rename *removes* a smell):

```cpp
std::unique_ptr<QFutureInterface<QList<SyncResult>>> m_currentIface;
QFutureWatcher<QList<SyncResult>>*                   m_currentWatcher = nullptr;
```

`dispatchSingleNative()` (decl `syncengine.h:490-493`, def `syncengine.cpp:598-633`)
is **deleted** — its body folds into `runSync()`'s single-mapping branch.

### Shared run-setup helper (removes the iface/watcher duplication)

Both branches of `runSync()` need identical iface+watcher wiring. Extract a private
helper so the single branch and the multi branch don't duplicate it:

```cpp
// syncengine.h (private):
/// Architectural-redress Plan 8 step 3: create m_currentIface + m_currentWatcher
/// for a run and return the future callers observe. Both runSync() branches use
/// this; the single-mapping branch then dispatches via processSingleMapping(),
/// the multi-mapping branch via driveQueue().
QFuture<QList<SyncResult>> beginRun();
```

```cpp
// syncengine.cpp:
QFuture<QList<SyncResult>> SyncEngine::beginRun()
{
    m_currentIface = std::make_unique<QFutureInterface<QList<SyncResult>>>();
    m_currentIface->reportStarted();
    // F2 Task 23: cancellation-marker results must survive reportCanceled().
    m_currentIface->setAddResultsIfCanceledEnabled(true);
    QFuture<QList<SyncResult>> future = m_currentIface->future();

    delete m_currentWatcher;
    m_currentWatcher = new QFutureWatcher<QList<SyncResult>>(this);
    m_currentWatcher->setFuture(future);
    connect(m_currentWatcher, &QFutureWatcher<QList<SyncResult>>::canceled,
            this, &SyncEngine::onCancelObserved);
    return future;
}
```

### `runSync(SyncRequest)` after the collapse

```cpp
QFuture<QList<SyncResult>> SyncEngine::runSync(const SyncRequest &request)
{
    if (m_isSyncing || m_currentIface) {
        QFutureInterface<QList<SyncResult>> rejected;
        rejected.reportStarted();
        rejected.reportResult(QList<SyncResult>{});
        rejected.reportFinished();
        return rejected.future();
    }

    if (request.isSingleMapping()) {
        // Native single-mapping dispatch: report directly into the sole
        // QList iface (no .then() wrap), so the F2 Task 23 cancellation
        // contract — resultCount()==1, resultAt(0)=={cancelledResult} — is
        // preserved on the canonical path (Plan 8 step 3 collapse; closes
        // FINDINGS "From Plan 1" single-shim-bypass + dual-iface entries).
        QFuture<QList<SyncResult>> future = beginRun();
        m_isSyncing = true;
        std::optional<ExecutionOverride> ov;
        if (request.executionOverride.has_value())
            ov = *request.executionOverride;
        m_queue.primeSingle();
        processSingleMapping(request.mappingIds.first(), request.behavior,
                             ov.value_or(ExecutionOverride{}));
        return future;
    }

    // Multi-mapping path (all-enabled or subset).
    QFuture<QList<SyncResult>> future = beginRun();

    ExecutionOverride queueOverride;
    if (request.executionOverride.has_value())
        queueOverride.clobber = request.executionOverride->clobber;

    if (request.isAllEnabled()) {
        driveQueue(request.behavior, std::nullopt, queueOverride);
    } else {
        QSet<QString> filter(request.mappingIds.constBegin(),
                             request.mappingIds.constEnd());
        driveQueue(request.behavior,
                   std::optional<QSet<QString>>(std::move(filter)),
                   queueOverride);
    }
    return future;
}
```

Note the single branch now calls `m_queue.primeSingle()` and `processSingleMapping`
**directly** (formerly inside `dispatchSingleNative`); `m_isSyncing = true` moves here
from `dispatchSingleNative` (the not-found and cancel-precheck paths inside
`processSingleMapping` clear it, unchanged).

### The five single-iface reporting sites → the sole iface

Each `m_currentSingleIface->reportResult(x)` becomes
`m_currentIface->reportResult(QList<SyncResult>{ x })`; the guard checks lose the
`|| m_currentMultiIface` half. Exact sites:

| File:line (current) | Context | Change |
|---|---|---|
| `syncengine.cpp:417-433` | `processSingleMapping` cancel-precheck | `reportResult(QList<SyncResult>{cancelled})` + `reportCanceled` + `reportFinished`; member `m_currentIface` |
| `syncengine.cpp:470-490` | `processSingleMapping` mapping-not-found | `reportResult(QList<SyncResult>{err})` + `reportFinished`; member `m_currentIface` |
| `syncengine.cpp:1259-1266` | `onWorkerSyncCompleted` Single mode | `reportResult(QList<SyncResult>{finalResult})`; `reportCanceled` when `m_cancelled \|\| finalResult.cancelled`; `reportFinished` |
| `syncengine.cpp:1300-1304` | `onWorkerSyncError` Single mode | `reportResult(QList<SyncResult>{failedResult})` + `reportFinished` |
| `syncengine.cpp:512`, `:603` | overlap guards | `if (m_isSyncing \|\| m_currentIface)` (the `:603` guard is in deleted `dispatchSingleNative`) |

The multi-iface sites (`:322-325`, `:371-375`, `:547-557`, `:865-869`, `:896-899`)
change name only (`m_currentMultiIface`→`m_currentIface`, `m_multiWatcher`→
`m_currentWatcher`); the `:547-557` block is absorbed by `beginRun()`.

`syncengine.cpp:114` teardown comment (`m_singleWatcher/m_multiWatcher … torn down by
~QObject`) → `m_currentWatcher … torn down by ~QObject`.

### Deletions

- `syncengine.h:316-352` — the four `[[deprecated]] runSyncFuture(...)` decls (+ their
  doc-comments at `:304-352`).
- `syncengine.cpp:635-686` — the four overload definitions.
- `syncengine.h:478-493` + `syncengine.cpp:581-633` — `dispatchSingleNative`.
- `syncengine.h:579`, `:605` — `m_currentSingleIface`, `m_singleWatcher`.

### Doc-comment truth sweep (INVARIANTS §7, AUDIT WP-B precedent)

Comments naming `runSyncFuture` as the live entry must point at `runSync(SyncRequest)`:
`syncenginefuture.h:23`, `mappingqueue.h:71-72`, `syncrequest.h:16`, and the in-body
comments at `syncengine.cpp:305-307`, `:486-488`, `:494-534`, `:582-597`. The
`mappingqueue.h` `DispatchMode` enum comments become `runSync(SyncRequest) single
mapping` / `runSync(SyncRequest) multi mapping`.

---

## Migration recipe (call sites → `runSync(SyncRequest)`)

`SyncRequest` (`src/engine/syncrequest.h`) is the canonical parameter struct:

```cpp
struct SyncRequest {
    QList<QString>                 mappingIds;        // empty ⇒ all enabled
    SyncEngine::SyncBehavior       behavior = SyncEngine::SyncBehavior::Unmonitored;
    std::optional<ExecutionOverride> executionOverride;
};
```

`runSync` always returns `QFuture<QList<SyncResult>>`. Per call-shape rewrite:

| Old call | New call |
|---|---|
| `engine.runSyncFuture(behavior)` | `SyncRequest r; r.behavior = behavior; engine.runSync(r)` |
| `engine.runSyncFuture()` | `engine.runSync(SyncRequest{})` |
| `engine.runSyncFuture(ids)` (ids non-empty) | `SyncRequest r; r.mappingIds = ids; engine.runSync(r)` |
| `engine.runSyncFuture(QList<QString>{})` | **see empty-list note below** |
| `engine.runSyncFuture(mappingId)` | `SyncRequest r; r.mappingIds = { mappingId }; engine.runSync(r)` |
| `engine.runSyncFuture(mappingId, behavior)` | `SyncRequest r; r.mappingIds = { mappingId }; r.behavior = behavior; engine.runSync(r)` |
| `engine.runSyncFuture(mappingId, override)` | `SyncRequest r; r.mappingIds = { mappingId }; r.executionOverride = override; engine.runSync(r)` |
| `engine.runSyncFuture(mappingId, override, behavior)` | as above + `r.behavior = behavior` |

**Return-type / read-pattern change (every single-mapping caller):** the old single
shim returned `QFuture<SyncResult>`; `runSync` returns `QFuture<QList<SyncResult>>`.
At each migrated single-mapping site:

- `QFutureWatcher<SyncResult>` → `QFutureWatcher<QList<SyncResult>>`.
- `future.resultAt(0)` (a `SyncResult`) → `future.resultAt(0).first()` (or `.at(0)`).
- A site that asserts on the whole result: read `const QList<SyncResult> list =
  future.resultAt(0); const SyncResult &r = list.first();`.
- **Cancellation sites** (`tst_engine_cancellation`, `tst_cancellation_reason`, and any
  single-mapping `future.cancel()` reader): after the collapse the canonical single
  path preserves the marker, so `QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000)`
  then `QCOMPARE(future.resultAt(0).size(), 1)` and
  `QVERIFY(future.resultAt(0).first().cancelled)`. **No `resultCount()>0` guard needed**
  (that was the `.then()`-path workaround this plan removes). Keep waiting via
  `QTRY_VERIFY_WITH_TIMEOUT` (NOT `waitForFinished`; CLAUDE.md).

**Empty-list note (`runSyncFuture(QList<QString>{})`):** the deleted subset shim
short-circuited an empty `ids` to "zero mappings dispatched, finished empty"
(`syncengine.cpp:675-681`), whereas `runSync(SyncRequest{})` with empty `mappingIds`
means **all enabled** (FINDINGS "From Plan 1", `syncrequest.h:35-56`). Exactly one test
site exercises the empty-subset path (`grep` shows a single
`runSyncFuture(QList<QString>{})`). That test pins the historical empty-subset
semantics, which no canonical consumer needs. Resolve at execution by reading the test:
if it asserts "empty input ⇒ no work", rewrite it to call `runSync` with a
**non-existent** mapping id (`r.mappingIds = { QStringLiteral("__none__") }`) — subset
dispatch with no matching enabled mapping yields the same empty result through the
canonical path — and add a one-line FINDINGS note that the empty-subset *shim*
semantics retired with the overload (no canonical consumer). Do **not** add an
`allEnabled` sentinel to `SyncRequest` (none needed; INVARIANTS §8).

**Prod callers** (`syncruncoordinator.cpp:60`, `reference_consumer/main.cpp:300`) are
both `runSyncFuture(behavior)` (multi) → first table row; they already use
`QFutureWatcher<QList<SyncResult>>` and read `resultAt(0)` as a list, so only the call
line changes. `syncruncoordinator.cpp:86`'s existing `resultCount()>0` guard is correct
and stays (multi path; unrelated to the single-path fix).

---

## Tasks

Each task is one commit; full `ctest` green after every commit (INVARIANTS P3).
Build/test with `make -j8` / `ctest -j8` only (auto-memory `make-j8-only`).

### T1 — Migrate the MULTI-mapping callers (prod + tests), keep overloads. One commit.

Lowest-risk, largest-diff slice, isolated for review. Migrate every `runSyncFuture(behavior)`
/ `runSyncFuture()` / `runSyncFuture(ids)` site — i.e. every caller that does **not**
pass a single `mappingId` — to `runSync(SyncRequest)` per the recipe. The deleted-to-be
multi overloads already just build a `SyncRequest` and call `runSync` internally
(`syncengine.cpp:655-686`), so this is behavior-identical.

- Prod: `src/sync/syncruncoordinator.cpp:60`, `examples/reference_consumer/main.cpp:300`.
- Tests: all `runSyncFuture(behavior|()|ids)` sites (multi forms in the histogram:
  `tst_engine_subset_dispatch`, `tst_engine_baseonly_backend`, `tst_calendar_sync_full`,
  the all-enabled sites, etc. — enumerate at execution with
  `grep -rn "runSyncFuture" tests/ | grep -v mappingId`, then classify each).
- The single-mapping sites and the four overloads are **untouched** in T1.

Gate: `cmake --build build -j8 && ctest --test-dir build -j8` → **147** (no behavior
change; pure call-shape rewrite). `compile_commands.json` regenerated; clangd clean on
touched TUs (CLAUDE.md / acceptance gates).

### T2 — Collapse the dual iface + migrate single-mapping callers + delete overloads + pin the canonical-cancel contract. One commit (shown red first).

This is the engine surgery. Order within the commit:

1. **Add the pinning test, shown red against current `main` first** (INVARIANTS §6,
   falsifiability). New `tests/engine/tst_engine_single_mapping_cancel.cpp`
   (`kalburator_add_engine_test`, default lane), a stub-host single-mapping run that
   calls `future.cancel()` and asserts the **canonical** contract:
   `QCOMPARE(future.resultAt(0).size(), 1)` and
   `QVERIFY(future.resultAt(0).first().cancelled)`. Demonstrate it RED against the
   pre-collapse `.then()` path (`resultCount()==0` ⇒ `resultAt(0)` throws / size 0) in
   the commit message; do not commit the red state.
2. **Engine collapse** per Design: `beginRun()` helper; rewrite `runSync()` single
   branch native; replace the five single-iface reporting sites + two guards; rename
   `m_currentMultiIface`/`m_multiWatcher` → `m_currentIface`/`m_currentWatcher`; delete
   `dispatchSingleNative`, `m_currentSingleIface`, `m_singleWatcher`.
3. **Delete the four `[[deprecated]] runSyncFuture` overloads** (decls `syncengine.h:316-352`,
   defs `syncengine.cpp:635-686`).
4. **Migrate every remaining single-mapping caller** (all in tests) per the recipe,
   including the cancellation sites (now guard-free) and the one empty-subset site.
5. **Doc-comment truth sweep** (`syncenginefuture.h`, `mappingqueue.h`, `syncrequest.h`,
   in-body comments) — same commit.

Gate: `grep -rn "runSyncFuture" src/ tests/ examples/` is **empty** (only this plan doc
+ FINDINGS/STATUS history may mention it). `grep -rn "m_currentSingleIface\|m_singleWatcher\|
dispatchSingleNative\|m_currentMultiIface\|m_multiWatcher" src/` empty. Full `ctest -j8`
**148** (147 + the new cancel test). The new test passes; `tst_engine_cancellation` (the
existing 17-site cancel suite, now canonical) stays green.

### T3 — Gates + close-out. One commit.

- Full `ctest -j8` clean (**148**); record the number.
- **PlanStan gate** (INVARIANTS §10): this is a lib-internal change deleting symbols
  PlanStan does **not** reference (`58bd4835` grep-clean, pins `v0.69`). Build PlanStan
  against this tree (`PLANSTAN_LIBKALBURATOR_SOURCE_DIR` override) and confirm the
  failed-set is exactly the known 21 Not-Run headless-GUI binaries — **and relink its
  `EXCLUDE_FROM_ALL` fixtures first** (the vtable/ABI-change runbook rule from step 1's
  Outcome; deleting public methods changes `SyncEngine`'s layout).
- **WildPalms** (INVARIANTS §10): `4dc3537` is `runSyncFuture`-clean and pins `v0.69`;
  grep-verify it references none of the deleted symbols (it won't). Clone gate optional
  per the plan-7 precedent (it is per-symbol non-consumer).
- **FINDINGS:** cross out (strike, don't delete — log convention) the three "From Plan 1"
  dual-iface / single-shim-bypass entries (`syncengine.cpp:578-616`, `syncengine.h:~445-501`,
  and the single-shim-bypass note) as **RESOLVED by Plan 8 step 3** with the commit hash;
  add the empty-subset retirement one-liner from T2.
- **STATUS:** Plan 8 row → step 3 DONE; "Next action" → Plan 9 (or 10); add a Locked
  decision recording that the canonical single-mapping path now preserves the cancel
  result natively (the WildPalms `resultCount()>0` workaround is no longer required for
  new consumers, though theirs stays — their call).
- **Plan Outcome** section filled (metrics: overloads 4→0, iface members 2→1, watcher
  members 2→1, net LOC delta, final ctest).
- Merge `--no-ff`; push; cut **v0.70**.

---

## Acceptance

- `grep -rn "runSyncFuture" src/ tests/ examples/` empty (history docs excepted).
- The four overloads, `dispatchSingleNative`, and the `Single`-suffixed iface/watcher
  members are gone; one `m_currentIface` + one `m_currentWatcher` remain.
- Canonical single-mapping `runSync` preserves the F2-Task-23 cancel contract
  (`tst_engine_single_mapping_cancel` green; `tst_engine_cancellation` green).
- ctest **148** green; PlanStan failed-set = the 21 Not-Run GUI binaries; WildPalms
  per-symbol non-consumer of the deleted surface.
- `compile_commands.json` regenerated; no new clangd diagnostics on touched TUs.
- FINDINGS "From Plan 1" dual-iface entries struck RESOLVED; STATUS + Locked decision
  updated in the close-out commit (INVARIANTS §7).

## Risks / notes

- **Cancellation routing is the only behavioral change.** The success and error paths
  are pure call-shape + result-wrap rewrites. The single risk is the native single-path
  cancel; T2's pinning test (shown red first) is the guard. `tst_engine_cancellation`'s
  17 sites are the existing safety net — they must stay green through T2.
- **`primeSingle()` ordering:** the single branch must call `m_queue.primeSingle()`
  before `processSingleMapping` (as `dispatchSingleNative` did) so
  `onWorkerSyncCompleted` takes the `DispatchMode::Single` branch. Verified against
  `syncengine.cpp:443-446`.
- **No `SyncRequest` sentinel added** — the empty-subset distinction retires with the
  shim; no canonical consumer needs it (INVARIANTS §8; FINDINGS "From Plan 1").
- **Intermittent `tst_engine_cancellation` SEGFAULT** (FINDINGS "From Plan 2",
  2026-05-29) touches this exact path. If it surfaces during T2, it is **pre-existing**
  (A/B-verify against `main`); root-causing the threading race is out of this plan's
  scope (log it, don't chase it — INVARIANTS §8).

---

## Outcome (2026-06-10, branch `feature/redress-8-runsyncfuture-retirement`)

Landed in commits off `main` @ `348aec9` (baseline 147/147):

| Commit | Task | What |
|---|---|---|
| `0595044` | T1 | Migrate the multi-mapping callers (2 prod + test sites) to `runSync(SyncRequest)`; overloads still present. |
| `26c90ff` | T2 | Engine collapse: `beginRun()` helper; single-mapping branch reports **natively**; **delete the four `[[deprecated]] runSyncFuture` overloads + `dispatchSingleNative`**; collapse `m_currentSingleIface`/`m_singleWatcher` + `m_currentMultiIface`/`m_multiWatcher` → one `m_currentIface` + one `m_currentWatcher`; migrate single-mapping callers (incl. empty-subset → two unknown ids); doc-comment truth sweep. |
| `1243fce` | T2 follow-up | Add the falsifiability test T2 omitted (`tst_engine_single_mapping_cancel`), shown RED against the pre-collapse engine. |
| _(this commit)_ | T3 | Gates + FINDINGS/STATUS/Outcome + `CLAUDE.md` doc-truth; merge `--no-ff`; tag **v0.70**. |

### Metrics

| Metric | Before | After | Delta |
|---|---|---|---|
| `runSyncFuture` overloads | 4 | 0 | **−4** |
| `QFutureInterface<…>` members | 2 (`m_currentSingleIface` + `m_currentMultiIface`) | 1 (`m_currentIface`) | **−1** |
| `QFutureWatcher<…>` members | 2 (`m_singleWatcher` + `m_multiWatcher`) | 1 (`m_currentWatcher`) | **−1** |
| `dispatchSingleNative` | 1 | 0 | **−1** (folded into `runSync()` + new `beginRun()`) |
| `syncengine.h` / `.cpp` LOC | 640 / 2932 | 581 / 2838 | **−153 net** (116 ins / 269 del, T1+T2) |
| Live `runSyncFuture` callers | ~87 (2 prod + ~85 test) | 0 | only historical comments remain |
| ctest | 147 | 148 | **+1** (the cancel pin) |

### The payoff (not just deletion)

The canonical single-mapping path now reports a one-element `QList<SyncResult>`
**straight into the sole iface** — no `.then()` wrap — so the F2 Task 23 cancel
contract (`resultCount()==1`, `resultAt(0).first().cancelled==true`) holds on the
canonical path. This closes the three FINDINGS "From Plan 1" dual-iface /
single-shim-bypass entries **and** the "By Plan 8" cancel-loss entry (all struck
RESOLVED). The WildPalms `resultCount()>0` watcher workaround is no longer required
for new lib consumers (theirs stays — their call).

### Falsifiability (INVARIANTS §6) — and a process correction

T2 (`26c90ff`) did the engine surgery but **omitted the protective test the plan
mandated as T2 step 1**. T3 caught this at the gate (ctest was 147, not the required
148) and added it (`1243fce`). Falsifiability was demonstrated retroactively by A/B
against the pre-collapse engine (revert `syncengine.{h,cpp}` + the three doc-only
engine headers to `0595044`, rebuild): the test FAILS. The pre-collapse failure was
**deeper than the plan predicted** — canceling the `.then()`-wrapped canonical future
never reached the engine's cancel watcher (bound to the native future, not the
continuation), so the worker ran to completion and wrote items (observed: 2 written),
tripping the "no items reached the destination" assertion before the result-shape
check. Only the deleted `runSyncFuture(mappingId)` shims (native future verbatim) had
a working single-mapping cancel pre-collapse. The migrated 17-site
`tst_engine_cancellation` suite (now on the canonical path) is the broad net; the
focused test is the falsifiable pin.

### Deviations (documented per INVARIANTS "Scope and exceptions")

- **Test lane.** Plan named `kalburator_add_engine_test` (default lane); the test
  needs the calendar stub harness (`MockBackend`/`StubSyncHost`/`MemoryCalendar`), so
  it uses `kalburator_add_engine_integration_test` — the lane of its sibling
  `tst_cancellation_reason`.
- **Empty-subset test.** Plan suggested one non-existent id; a single unknown id is
  single-mapping-not-found (one-element error result), so the migrated
  `tst_engine_subset_dispatch::emptySubset_returnsEmptyResults` uses **two** unknown
  ids to stay on the subset path.

### Gates (INVARIANTS §10)

- **libkalburator ctest:** 148/148 green (clean re-run). The one intermittent
  `tst_engine_cancellation` SEGFAULT under `-j8` is the documented FINDINGS "From
  Plan 2" load flake (5/5 isolated + a clean 148/148 full re-run); pre-existing, not
  chased (INVARIANTS §8).
- **WildPalms:** per-symbol non-consumer of every deleted symbol — `grep` for actual
  calls is empty; only historical comments in `palmruntime.cpp` mention the retired
  API. Clone gate skipped (plan-7 precedent).
- **PlanStan** (`58bd4835`, `runSyncFuture`-token-free, pins v0.69): built **fresh**
  against this tree (`PLANSTAN_LIBKALBURATOR_SOURCE_DIR`, `build-redress8gate`, clean
  Makefiles configure → no stale objects, so every fixture relinks against the new
  SyncEngine ABI — a cleaner equivalent of the "relink EXCLUDE_FROM_ALL fixtures
  first" runbook rule). ctest: **88 passed, 27 ✱Not Run, 0 real failures** (0
  `✱✱✱Failed`, 0 SegFault). All 27 Not-Run are EXCLUDE_FROM_ALL GUI / graph-editor /
  GUI-integration binaries the fresh `make all` doesn't build (verified
  `<not built>`); the count is 27 vs Plan-7's 21 only because a fresh dir builds none
  of them (build-dev had ~6 pre-built). **Every libkalburator-consuming test ran and
  passed** — notably `tst_syncruncoordinator` (PlanStan's prod consumer of the engine
  sync API the collapse changed), `tst_synchostsmoke`/`tst_calendarhostsmoke`
  (ISyncHost integration), the full sync/backend/collection/topology suite, and
  `tst_loader_empty_backends` (Plan-7's prior real failure, now green via PlanStan's
  own O.5 realignment `91774225`). The overload deletion + ABI change is transparent
  to PlanStan.
- **clangd:** `compile_commands.json` regenerated; no new diagnostics on touched TUs
  (the unused-include / `.moc`-not-found warnings are pre-existing clangd noise).
