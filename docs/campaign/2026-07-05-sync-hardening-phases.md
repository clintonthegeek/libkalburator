# Sync-hardening campaign — phase plan (THE live plan for both repos)

**Date opened:** 2026-07-05
**Status:** Phases H1–H5.5 done; **H6 done — v0.83 tagged 2026-07-05**
(`feature/d1-threading` merged to `main` `--no-ff` @ `e32fac3`, annotated
tag v0.83). CP-A + CP-B recorded (2026-07-05). All lib-side work (T1.1–T1.4,
H1–H5.5) is on `main` and released; FINDINGS O16–O25 Resolved. **Next: H7 —
PlanStan I/O-thread adoption** (repo `~/dev/PlanStan`, pin bump to v0.83,
D0-mitigation removal, backend relocation). See §10 checklist — it is the
single source of truth for progress; update it in the same commit as the work.
**Scope:** closes FINDINGS **O16–O24** (from the 2026-07-05 first-principles
audit) and finishes the D1 threading work (PlanStan I/O-thread adoption,
tag v0.83). Both repos — libkalburator (primary) and PlanStan (H7/H8) —
are owned by this campaign until every phase is closed.
**Supersedes:** `2026-07-04-d1-threading-execution-plan.md` (its T1.1–T1.5
are landed and stay; its Stages 2–4 are absorbed here as H7/H8) and the
ordering proposed in the audit doc's §6 (reordered — see §2).

---

## 0. How to work this document (read this first, every session)

You are one of a series of agents executing this plan one phase — or one
task — at a time. The plan is written so you do not need to make design
decisions: **each phase's design is pre-decided in its "Design (decided)"
block.** If you believe a design is wrong, do NOT improvise a different
one: stop, write your objection under the phase's checklist entry, mark
the phase `BLOCKED (objection)`, commit, and end the session. A checkpoint
model will rule on it.

**Session protocol — follow exactly:**

1. Read §0, §1, and the section for YOUR phase only (plus anything that
   phase's "Read first" line lists). Do not start a phase whose
   predecessor is unchecked in §10, unless the phase says it is
   independent.
2. Verify the **entry state** (§1 and your phase's Entry line): right
   repo, right branch, suite state matches. If it doesn't match, stop and
   report — do not "fix" the discrepancy.
3. Write the phase's RED test(s) first. Run them. Confirm they FAIL for
   the stated reason. Commit the RED test separately
   (`test(...): RED — <what it pins>`).
4. Implement exactly the steps listed. Stay inside the listed files; if
   you find yourself editing a file the phase doesn't name, stop and
   re-read the phase.
5. Run the phase's **Acceptance gate** commands. All must pass as stated.
6. In ONE final commit: the implementation + tick your §10 checklist item
   + update the affected FINDINGS.md entry (move it toward Resolved with a
   dated note) + any doc updates the phase names.
7. If anything unexpected appears (a new bug, a failing unrelated test),
   append a dated note to `docs/campaign/FINDINGS.md` (new O-number) and
   your phase's checklist line. Never silently absorb a surprise.

**Model guidance per phase** (advisory, enforced only at CP tasks):
H1, H2, H6 — any competent agent, fully mechanical.
H3, H4, H5, H7 — mid-tier or better; designs are pre-decided but the
diffs are non-trivial.
**CP-A, CP-B, CP-C — STOP: these are strong-model checkpoints**
(Opus/Fable-class). If you are not confident you are such a model, end
the session and report that the campaign is waiting on a checkpoint.

**Hard rules (from `docs/campaign/INVARIANTS.md` + the threading
postmortems — violations get the work reverted):**

- No `processEvents()`. No new nested `QEventLoop` on any thread. No
  mutex added around a backend. No `sleep`/`msleep` as synchronization.
- RED test first for every behavior change.
- Build with `cmake --build build -- -j 8` — **NEVER `--parallel`**.
- Full suite: `WAYLAND_DISPLAY=wayland-0 ctest --test-dir build -j 8`.
  Never force `QT_QPA_PLATFORM=wayland`. Single tests run with
  `QT_QPA_PLATFORM=offscreen ./build/tests/<dir>/<test>`.
- One phase (or less) per session. Land it fully or mark it BLOCKED.
- Every commit that changes phase state also updates §10 here.

---

## 1. Entry state (as of campaign open)

- **Repo:** `~/dev/libkalburator`, branch **`feature/d1-threading`**
  @ `18d4631` (all H-phase lib work continues on this branch; merge to
  `main` happens only in H6).
- **Suite:** 159 tests; **158 green, 1 known RED** —
  `tst_backend_thread_relocation::stallProbe_relocatedBackends_stayResponsive`
  (that is O16's probe; it is SUPPOSED to stay red until H4 closes it —
  do not delete, skip, or weaken it).
- **PlanStan:** `~/dev/PlanStan`, branch `master`, pinned to libkalburator
  **v0.82** (`PLANSTAN_LIBKALBURATOR_GIT_TAG` in top-level CMakeLists.txt).
  Not touched until H7.
- **Background reading** (only if your phase says so):
  `docs/campaign/2026-07-05-first-principles-sync-architecture-audit.md`
  (the audit — target model in its §1, findings A1–C4),
  `docs/campaign/FINDINGS.md` O16–O24 (terse per-finding versions),
  `docs/campaign/archive/2026-07-05-d1-t1.5-stall-blocker-analysis.md` (O16 deep
  dive).
- **Line numbers in this doc** are against `feature/d1-threading` @
  `18d4631` and drift as phases land. Every reference names its symbol;
  when a line is stale, grep the symbol and trust the symbol.

---

## 2. Why the phases are in this order

Correctness bugs that strand user data (O17/O18) outrank visible jank
(O16), but two cheap, riskless phases (H1 hygiene, H2 marshaling) go
first because they shrink every later diff and can be done by any agent
immediately. The token rework (H3) precedes the worker-side fast path
(H4) because H3 **deletes** `persistRevision` — half of O16's blocking
surface — and changes what the fast path reads, so doing H4 first would
mean doing it twice. H5 reshapes the fetch pipeline last among the
lib-side phases because it depends on the gate fixes (H1) and benefits
from H3/H4 being settled. Then release (H6), PlanStan adoption (H7),
live verification (H8), and the deferred backlog (H9).

The audit's §6 proposed fast-path-first; this plan deliberately reorders
as above. This plan wins.

---

## 3. Phase H1 — mechanical hygiene (O22 partial, O23 partial, O24)

**Entry:** §1 state. Independent of everything; do first.
**Findings:** O24 (gate doesn't gate LocalBackend), O23 (op leak half),
O22 (timeout + honest rejection halves), C4 (dead `await<>`).
**Files:** `src/engine/syncengine.cpp`, `src/engine/syncengine_p.h`,
`src/calendar/remotecalendarbackend.cpp` (one line),
`tests/sync/fakecaldavserver.{h,cpp}`, `tests/calendar/` (tests),
`CLAUDE.md` (one stale paragraph).

### H1.1 — fix the fetch gate: await Pending ops, cancel on wake, delete ops

The worker's two gate blocks (`SyncEngineWorker::dispatchSync`,
"--- Fetch source records ---" at ~`syncengine.cpp:2121` and the twin
"--- Fetch target records ---" at ~`:2244`) currently:
(a) await only when `fetchOp->state() == SyncOperation::Running` — but
`LocalBackend::fetchItems` returns a **Pending** op (deferred via
`QTimer::singleShot`, `localbackend.cpp:697`), so local ops are never
awaited; (b) never call `fetchOp->cancel()` when woken by cancellation;
(c) never `deleteLater()` the op (RemoteCalendarBackend ops are
unparented → leaked forever with the full fetched payload;
LocalBackend ops pile up as backend children).

**Steps (apply identically to both gate blocks):**

1. Change the await condition from `state() == SyncOperation::Running`
   to `!fetchOp->isFinished()` (both the outer `if` and the inner
   re-check before `loop.exec()`).
2. After the loop, if `m_cancelled` and the op is not finished: call
   `fetchOp->cancel()`, then run one more small `QEventLoop` connected
   to `finished` **only if** `!fetchOp->isFinished()` after the cancel
   (this mirrors the semantics of the dead `await<>` helper being
   deleted in H1.4 — that helper's body at `syncengine_p.h:269-300` is
   your reference implementation; copy its teardown-loop shape).
3. At every exit path of each gate block (success, cancelled, Failed),
   `fetchOp->deleteLater()` if non-null. Easiest: hoist the QPointer
   above the block and add a single scope-exit deleteLater before each
   `return` / at block end.

**RED test first** (new case in
`tests/calendar/tst_backend_thread_relocation.cpp`):
`gateOps_areDeleted_afterSync` — run one full engine sync with two
LocalBackends (reuse the T1.4 case-3 fixture), then assert
`QTRY_VERIFY(srcBackend->findChildren<Kalburator::Sync::SyncOperation*>().isEmpty())`
(and same for target). Today this FAILS (children accumulate). Add a
second assertion after a *second* runSync that the count is still 0
(pins no per-cycle growth).

### H1.2 — QNAM transfer timeout

In `RemoteCalendarBackend`'s lazy `nam()` accessor (added in D1 T1.1,
`remotecalendarbackend.cpp`, search `m_nam`): after creating the QNAM,
call `m_nam->setTransferTimeout(std::chrono::seconds(30));`.

**RED test first:** add `FakeCalDavServer::setDropRequests(bool)` —
when true, the server reads the request and never responds (keep the
socket open; do NOT close it — closing produces an immediate error, not
a stall). New test case: a fetch against a dropping server must complete
(op Failed, sync result `success == false`, non-empty errorMessage) —
run the QNAM-level path via `collectionRevision()` or a full mapping.
Use a `QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 60000)` bound.
Today this test HANGS (that is the RED — cap it with the timeout and
assert `isFinished()`); with the timeout it completes with a failure
in ~30s. To keep suite time sane, also add
`RemoteCalendarBackend::setTransferTimeoutMs(int)` (test-visible setter
that re-applies to the existing QNAM) and use ~2000ms in the test.

### H1.3 — distinguishable overlap rejection

`SyncEngine::runSync` (`syncengine.cpp:535-544`): the busy-rejection
currently returns a finished future with an **empty result list** —
indistinguishable from a successful no-op run. Change it to report a
single `SyncResult` with `success = false`,
`errorMessage = QStringLiteral("rejected: a sync is already running")`,
plus a `qWarning()`. Verify PlanStan's consumer copes (it does — the
result surfaces through `syncRunFinished` as a failed run; nothing
parses the empty list specially; confirmed by reading
`SyncRunCoordinator`).

**RED test:** call `runSync` twice back-to-back; assert the second
future finishes with one result, `success == false`, message contains
"rejected". Today the second future finishes with an empty list.

### H1.4 — delete dead machinery + fix the lying docs

1. Delete `SyncEngineWorker::await<Op>` (`syncengine_p.h:269-300`) —
   zero call sites (verify with grep before deleting).
2. Fix the false comment in `localbackend.cpp` (~`:767`):
   "sync runs in worker thread" → the lambda runs on the *backend's*
   thread.
3. `CLAUDE.md` (libkalburator, "Cancellation" bullet ~line 162): remove
   the claim that cancellation "wakes any nested QEventLoop (via
   `await<Op>` ...)" — describe the real mechanism (gate loops +
   conflict-pause slot).

**Acceptance gate (H1):** full suite green except the stall probe
(158→161-ish green / 1 known red, exact count depends on cases added);
all new tests green; `grep -rn "await(" src/` returns nothing for the
deleted helper.
**Do NOT:** touch `prepareSyncFastPath`, `persistRevision`,
`dispatchFirstSync`, `runPropertyPhase`, or any token logic — later
phases own those.

---

## 4. Phase H2 — marshaling completeness (O20, O21)

**Entry:** H1 landed.
**Findings:** O20 (`runPropertyPhase` direct backend calls from worker —
live UB pre-D1), O21 (`dispatchFirstSync` runs target-backend calls on
the source's thread).
**Files:** `src/engine/syncengine.cpp`,
`tests/calendar/tst_backend_thread_relocation.cpp`.
**Pattern to copy:** the file-local `runOnBackendThread()` helper
(`syncengine.cpp:104`) and the per-backend `QMetaObject::invokeMethod(
backend, lambda, Qt::BlockingQueuedConnection)` shape used ~20 times in
the same file.

### H2.1 — marshal runPropertyPhase

`SyncEngineWorker::runPropertyPhase` (`syncengine.cpp:3043-3087`) calls
`ops->collectionProperties(src, …)` / `ops->applyCollectionProperties(…)`
directly on the worker thread. Wrap each of the four call sites so the
`DomainOperations` call executes on the owning backend's thread:

```cpp
QVariantMap srcProps;
runOnBackendThread(src, [&]() { srcProps = ops->collectionProperties(src, srcCollectionId); });
```

(and likewise for `tgtProps`, `applyCollectionProperties(tgt, …)` twice,
`applyCollectionProperties(src, …)` once). `runOnBackendThread` already
handles the same-thread case, so pre-relocation topologies keep working.

**RED test first:** extend the relocated-backends engine test with a
mapping whose two sides disagree on calendar color (LocalBackend supports
color via its metadata path — if wiring color through LocalBackend is
disproportionate, use `RemoteCalendarBackend` + `FakeCalDavServer` and
give the fake a color property; whichever is less code). The RED
assertion pre-fix: in a debug build the T1.1
`Q_ASSERT(currentThread == nam->thread())` fires (remote case), or —
portable version — record `QThread::currentThread()` inside a stub
backend's `calendarColor()` override and assert it equals the backend's
thread. The stub-recording version is preferred (deterministic, no
assert-death handling): add a tiny `SyncBackendBase` subclass in the
test that records the calling thread of `calendarColor` /
`updateCalendar` and returns a differing color so the apply path runs.

### H2.2 — split dispatchFirstSync per backend

`SyncEngineWorker::dispatchFirstSync`'s mirror lambda
(`syncengine.cpp:1786-1833`) is marshaled once onto `srcBackend` but
calls `tgt->loadRecordsOrError / createRecord / updateRecord /
deleteRecord` inside it. Restructure into three marshals:

1. On **src** thread: `src->loadRecordsOrError(colId, srcRecords, err)`.
2. On **tgt** thread: `tgt->loadRecordsOrError(colId, tgtRecords, err)`.
3. Compute the create/update/delete lists **on the worker thread** (pure
   list work — move the existing loop bodies out of the lambda
   unchanged, minus the tgt calls).
4. On **tgt** thread: apply the writes (`createRecord`/`updateRecord`/
   `deleteRecord` loop; keep the `tgtWritable` short-circuit and
   `mirrorErrors` counting exactly as-is).

**RED test first:** new case `firstSync_backendsOnDifferentThreads` —
source LocalBackend on I/O thread A, target LocalBackend on I/O thread
B (two `QThread`s), empty target, run a mapping, assert the mirror
completed and record in a thread-recording stub (same trick as H2.1)
that each backend's methods executed on its own thread. Pre-fix the
target's calls execute on thread A → RED.

**Acceptance gate (H2):** full suite green except the stall probe; new
tests green.
**Do NOT:** change what the property phase or first-sync *does* — only
where each call runs. No behavior deltas.

---

## 5. CP-A — checkpoint: strong model reviews H3's design (STOP if not strong model)

Before H3 is implemented, a strong model must:

1. Re-read audit §2 (A1–A4) and §1.4, then the **Design (decided)**
   block in H3 below, against the code as it now stands (post H1/H2).
2. Confirm or amend: the pre-fetch-snapshot token semantics, the
   BaselineStore schema addition, the decision to leave
   `ChangeDetection::cachedCollectionRevision`/`primeRevisionCache`
   present-but-engine-unused (WildPalms compatibility), and the accepted
   one-cycle re-diff lag after self-writes.
3. Record the ruling (one paragraph, dated) in §10 under CP-A and in
   FINDINGS O17. Amendments edit H3's text directly.

CP-A exists because H3 changes persistence and a sync-correctness
contract — the one place a wrong pre-decision quietly costs data.

---

## 6. Phase H3 — sync-progress tokens owned by the engine (O17, O18, O19, audit A4)

**Entry:** H1+H2 landed, CP-A recorded. Read audit §2 before starting.
**Findings closed:** O17 (failed apply strands changes), O18 (local
post-write re-hash masks foreign edits), O19 (inert remote prime +
wasted GUI-thread PROPFIND), audit A4 (phantom baseline condition).

### Design (decided — do not re-design)

**Principle (audit §1.4):** the backend's fetch-time CTag/fingerprint
commit is a *cache-validity* token and stays exactly as it is. The
*sync-progress* token — "which revision did THIS MAPPING last
successfully consume?" — moves into the engine's BaselineStore, keyed
per `(mappingId, side)`, written **only when a mapping run succeeds**,
using the value captured **before** that run's fetch (pre-fetch
snapshot ⇒ the stored token is never newer than the data actually
synced ⇒ a stale token causes at worst one redundant re-diff cycle,
never a masked change).

Mechanically this is small because the capture already exists:
`prepareSyncFastPath()` already queries fresh tokens per side and
stashes them in `m_freshState` (`FreshSyncState`). We keep that capture
and change only (a) what the skip check compares against and (b) what
happens on completion.

1. **BaselineStore** (`src/storage/baselinestore.{h,cpp}`): bump the
   schema (current **v6 → v7** — CP-A correction: the plan originally
   said v5→v6, but B4's hash-column work already stamped
   `kSchemaVersion = 6`; verify the constant before bumping), follow
   the existing migration pattern — forward-only, self-migrating,
   additive table:
   `CREATE TABLE IF NOT EXISTS sync_tokens (mapping_id TEXT NOT NULL,
   side TEXT NOT NULL CHECK(side IN ('source','target')),
   token TEXT NOT NULL, PRIMARY KEY (mapping_id, side))`.
   New API: `QString syncToken(const QString &mappingId, const QString
   &side) const;`, `void setSyncToken(const QString &mappingId, const
   QString &side, const QString &token);`, `void clearSyncTokens(const
   QString &mappingId);`.
   **CP-A addition:** `clearMappingV3(mappingId)` must ALSO delete that
   mapping's `sync_tokens` rows (call `clearSyncTokens` from it). The
   header (`baselinestore.h:106`) already documents clearMappingV3 as
   the mapping-scoped wipe; if baselines are cleared through the API
   but tokens survived, a mapping could skip with no baselines — the
   exact A4 hole this phase is supposed to close structurally.
2. **Skip check** (`prepareSyncFastPath::checkSide`,
   `syncengine.cpp:744-758`): replace the marshaled
   `cd->cachedCollectionRevision(colId)` read with
   `m_baselineStore->syncToken(mapping.id, side)` (direct call — the
   store is engine-thread-affine and this code runs on the engine
   thread until H4). `unchanged` stays "both non-empty and equal".
   Delete the now-unneeded backend marshal from checkSide. Note the
   fresh-token queries at the top of the function remain (H4 relocates
   them; not this phase).
3. **Completion** (`onWorkerSyncCompleted`, `syncengine.cpp:1151-1206`):
   delete the entire `persistRevision` lambda and its two calls (the
   live re-query + `primeRevisionCache` — this is the O18/O19 surface).
   Replace with: on `result.success && m_baselineStore`, look up
   `m_freshState[mappingId]` and `setSyncToken(mappingId, "source",
   fresh.sourceRevision)` / `("target", fresh.targetRevision)` — only
   for non-empty values. On failure: persist nothing (that is the O17
   fix).
4. **Engine-wide grep:** after steps 2–3, `grep -n
   "cachedCollectionRevision\|primeRevisionCache" src/engine/` must be
   empty. (CP-A note: this includes `syncengine.h` — its
   `prepareSyncFastPath` doc comment at ~`:456-459` names
   `primeRevisionCache`, and the `skipUnchangedMappings` doc at
   ~`:278` describes the old baseline condition; update both.) The `ChangeDetection` interface methods stay (WildPalms may
   use them); add a doc comment on each: "engine no longer calls this
   (sync-hardening H3, 2026-07-05); backend-internal / external
   consumers only."
5. **Clobber:** in `driveQueue`'s clobber branch (or in the worker's
   clobber path — pick driveQueue, it's on the engine thread), call
   `m_baselineStore->clearSyncTokens(mapping.id)` for each clobbered
   mapping, so a clobbered mapping can't skip on stale tokens.
6. Single-mapping runs (`processSingleMapping`) have no pre-pass, hence
   no `m_freshState` entry, hence persist no tokens — same as today's
   baseline behavior; the existing comment at `syncengine.cpp:421-431`
   should be updated to describe tokens instead of primeRevisionCache.

**Accepted costs (do not "fix" these):** one extra full re-diff cycle
after any cycle that wrote (self-write bumps the live token; the stored
pre-fetch token then mismatches once). This is the current *actual*
remote behavior (O19 showed the old lag-removal never worked), it is
the safe direction, and the optimization (incremental expected-
fingerprint) is parked in H9.

### RED tests first (new file `tests/calendar/tst_sync_token_soundness.cpp`, use the calendar stubs + LocalBackend/temp dirs; FakeCalDavServer where a remote is needed)

- **O17 pin:** mapping with a target whose writes can be made to fail
  (simplest: LocalBackend target pointed at a directory made read-only
  via `QFile::setPermissions` after the fetch — or a stub blob backend
  whose `createRecord` returns empty). Cycle 1: source has one new item;
  fetch succeeds; apply fails; assert `result.success == false`. Make
  the target writable again. Cycle 2 with `setSkipUnchangedMappings(true)`:
  assert the mapping is NOT skipped and the item lands. Pre-H3 this
  FAILS at "not skipped" for the remote-source variant… for a fully
  deterministic pre-H3 RED use a **remote source** (FakeCalDavServer,
  whose fetch commits the ctag) + local target; post-H3 it passes.
- **O18 pin:** local↔local mapping; cycle 1 syncs and, **after** the
  fetch phase but before completion (inject via a stub target whose
  first write sleeps? no — no sleeps; instead perform the foreign edit
  between cycle 1 and reading its completion, i.e. write a new .ics
  into the source dir immediately after `runSync`'s future starts but
  before it finishes, guarded by `QTRY` on an intermediate signal such
  as the target backend's first `writeStarted`). Then cycle 2 with skip
  enabled: assert not skipped and the foreign file syncs. If the
  mid-run injection proves flaky, the acceptable fallback pin is:
  foreign edit lands *after* cycle 1 completes but the assertion is
  that cycle 2 *runs* (fresh fingerprint ≠ stored pre-fetch token) —
  weaker but still pins the O18 mechanism (post-write re-hash would
  have primed the post-write state; pre-fetch token cannot).
- **Skip still works:** two quiet cycles after a successful one →
  second is skipped (pins that H3 didn't just disable skipping).
- **Clobber clears tokens:** clobber run → tokens gone → next cycle not
  skipped.

**Acceptance gate (H3):** full suite green except the stall probe; the
four token tests green; the grep in step 4 empty; FINDINGS O17/O18/O19
moved to Resolved with dated notes; `docs/campaign/
2026-07-03-sync-convergence-roadmap.md` B5 paragraph gets a one-line
"superseded by H3 per-mapping tokens" note.
**Do NOT:** touch `prepareSyncFastPath`'s fresh-query half (H4), the
gates (H1 owns them), `fetchItems`' internal ctag logic (correct as-is),
or the `ChangeDetection` interface signatures.

---

## 7. Phase H4 — fast path onto the worker (O16)

**Entry:** H3 landed. Read FINDINGS O16 and the T1.5 blocker doc §4.
**Finding closed:** O16 — the last engine-side GUI-thread blocking I/O.
**Files:** `src/engine/syncengine.cpp`, `src/engine/syncengine_p.h`,
`tests/calendar/tst_backend_thread_relocation.cpp` (the probe — should
need no changes, only flip green).

### Design (decided)

`driveQueue()` (`syncengine.cpp:333-409`) currently: active controllers
→ `prepareSyncFastPath()` (synchronous, caller thread, does backend
I/O) → `startWorkerThread()` → `processQueue()`. Restructure to:

1. Active-controller loop unchanged (C4/H9 owns its GUI-thread issue).
2. Engine thread: read the *stored* tokens for every enabled in-filter
   mapping from BaselineStore (fast local SQLite, engine-affine) into a
   `QHash<QString /*mappingId*/, QPair<QString,QString>> storedTokens`.
3. `startWorkerThread()` **before** the fast path.
4. Dispatch a new worker job via the existing command-channel pattern
   (`syncengine_p.h` — add signal
   `fastPathRequested(QList<SyncMapping>, QHash<...> storedTokens,
   bool skipEnabled)` connected QueuedConnection to a new worker slot
   `prepareFastPath(...)`, exactly like `processSyncRequested` →
   `processSync` at `syncengine.cpp:173-174`).
5. Worker slot: batch-query fresh tokens per backend exactly as the old
   `prepareSyncFastPath` top half did — but now the
   `runOnBackendThread` marshals block the **worker**, which is their
   design point. Compute the skip set + per-mapping `FreshSyncState`
   with the same logic (compare fresh vs the passed-in storedTokens).
   Emit `fastPathReady(QSet<QString> skipped, QMap<QString,
   FreshSyncState> fresh)` (queued back to the engine).
6. New engine slot `onFastPathReady(...)`: store into
   `m_skippedMappingIds` / `m_freshState`; if `m_cancelled`, run the
   existing cancelled-teardown block (the one currently at
   `driveQueue`'s `syncengine.cpp:389-404`); else `processQueue()`.
7. The clobber and no-mapping branches keep their current synchronous
   short-circuits (they do no I/O). `prepareSyncFastPath()` as a
   function disappears; keep the name for the worker slot's helper if
   convenient.
8. Register any new metatypes (`FreshSyncState`, the QHash) with
   `qRegisterMetaType` next to the existing `Request` registration.

**Cancellation semantics (pin with a test):** `future.cancel()` between
`runSync()` return and `fastPathReady` must produce a cancelled result
with no mapping dispatched. The path already works structurally:
`onCancelObserved` sets `m_cancelled` on the engine thread and
`onFastPathReady` re-checks it (step 6).

### RED / gate tests

- The **existing stall probe** is the RED→GREEN gate: it currently
  fails at ~212ms; after H4 it must pass (<50ms max heartbeat gap).
  Do not modify the probe.
- New: `cancelDuringFastPath_reportsCancelled` per the semantics above
  (drive with `FakeCalDavServer::setResponseDelayMs(200)` so the fast
  path is reliably in flight when cancel lands).
- New (regression): single-mapping `runSync` path unaffected — existing
  suite covers it; just confirm green.

**Acceptance gate (H4):** **full suite 100% green for the first time in
the campaign** (stall probe included). FINDINGS O16 → Resolved; T1.5
blocker doc gets a "resolved by H4" header line and moves to
`docs/campaign/archive/`; the D1 execution plan's §9 T1.5/T1.6 lines get
ticked with a pointer here.
**Do NOT:** move `SyncEngine` or `BaselineStore` to another thread
(invariant §1.2 of the old plan still holds); no changes to worker↔
backend call sites.

---

## 8. Phase H5 — single-fetch pipeline (O23 remainder)

**Entry:** H4 landed (suite fully green).
**Finding closed:** O23's double-fetch half (the leak half died in H1).
**Files:** `src/sync/syncbackendbase.{h,cpp}`,
`src/calendar/remotecalendarbackend.cpp`, `src/calendar/localbackend.cpp`,
`src/engine/syncengine.cpp` (dispatchSync fetch blocks).

### Design (decided)

Add to `SyncBackendBase` (neutral layer, no KCalendarCore):

```cpp
/// Records equivalent to loadRecords(collectionId), but served from the
/// most recent successfully completed fetchItems() for that collection
/// when the backend can do so without new I/O. Default: delegates to
/// loadRecordsOrError (correct for backends without a fetch cache).
virtual bool recordsFromLastFetch(const QString &collectionId,
                                  QList<BackendRecord> &out,
                                  QString &errorMessage);
```

- `RemoteCalendarBackend` override: the body of the existing
  `loadRecords()` loop (`remotecalendarbackend.cpp:2190-2224`) minus the
  fresh `fetchItems()` call — it converts a stored copy of the last
  completed fetch (`op->fetchedItems()` captured into a
  `QHash<QString /*colId*/, QList<Incidence::Ptr>> m_lastFetchItems`
  member written at both `op->complete()` sites in `fetchItems`) via
  the existing `m_lastRawIcsByUid` / `blobRecordFromIcal` path. If the
  hash has no entry for the collection, fall back to the base impl.
- `LocalBackend` override: same shape — capture the parsed items at
  `fetchItems`' completion into a per-collection member; serve from it;
  clear the entry after serving (single-shot memo; the directory can
  change any time, so never serve it twice).
- `RemoteCalendarBackend`: also clear its memo entry after serving.
  (Single-shot semantics keep the memo from ever masking a later
  change; the worst case of a cleared memo is today's behavior — a
  loadRecords re-fetch, which the ctag short-circuit makes cheap.)
- Worker (`dispatchSync`, both sides): after a gate `fetchOp` finishes
  **Succeeded**, marshal `recordsFromLastFetch` instead of
  `loadRecordsOrError`; for `NotSupported`/null ops keep
  `loadRecordsOrError` exactly as now.

**RED test first:** instrument via signal counting —
`fetchStarted(calendarId, …)` is emitted once per real fetch pass. New
case: one engine sync over LocalBackend↔LocalBackend; assert each
backend emitted `fetchStarted` for its collection **exactly once**
(today: twice — RED). Add a remote variant asserting
`FakeCalDavServer`'s request log contains exactly one items-list
PROPFIND per cycle for the synced calendar (add a simple request
counter to the fake if it lacks one).

**Acceptance gate (H5):** full suite green; the once-per-cycle tests
green; FINDINGS O23 → Resolved.
**Do NOT:** delete `loadRecords`/`loadRecordsOrError` (blob consumers
and the NotSupported fallback still use them); no changes to fetch
*semantics* (delta logic, ctag commits stay untouched).

---

## 8b. Phase H5.5 — KDAV job watchdog (O25; added at CP-B, 2026-07-05)

**Entry:** H5 landed, CP-B review recorded (this phase exists because CP-B's
live smoke found O25). Read FINDINGS O25 first.
**Finding closed:** O25 (and with it the remaining live half of O22's wedge).
**Files:** `src/calendar/remotecalendarbackend.{h,cpp}`,
`tests/sync/` or `tests/calendar/` (RED tests).

### Design (decided at CP-B — do not re-design)

`RemoteCalendarBackend` runs its bulk traffic through KDAV jobs
(`DavItemsListJob`, item multiget/fetch, `DavItemCreateJob`,
`DavItemModifyJob`, `DavItemDeleteJob`, `DavCollectionsFetchJob`) on KDAV's
internal QNAM, which exposes no timeout API. H1.2's `setTransferTimeout`
covers only the backend's own `nam()`. Fix: a per-job watchdog.

**IMPLEMENTATION-TIME CORRECTION (2026-07-05, Opus — the CP-B `kill()`
mechanism is inert on KDAV 6.27.0):** the pre-decided step 1 below assumed
`job->kill(KJob::EmitResult)` finishes the job so its existing `result`
handler runs the error path. It does not: `KJob::kill()` is a no-op unless
the subclass overrides `KJob::doKill()` (default returns `false`, emits
nothing — `kjob.h:255`), and **no KDAV 6.27.0 job overrides `doKill()`**
(verified against the installed headers and the KDE source at tag `v6.27.0`;
`DavItemsListJob`/`DavJobBase` track KIO subjobs manually with no abort
path). A verbatim implementation left both RED tests hanging. The mechanism
was corrected to **fail the operation directly** instead of relying on
`kill()`; the RED-test recipe and acceptance gate are unchanged.

1. Private helper on `RemoteCalendarBackend`:
   `void startJobWithWatchdog(KJob *job, const std::function<void()> &onTimeout)`
   — creates a single-shot `QTimer` parented to the backend with interval
   `m_transferTimeoutMs`, connects the job's `result` → stop + `deleteLater`
   the timer, connects timeout → **detach the job from our slots
   (`job->disconnect(this)`, so its eventual — possibly never — real
   completion can't double-settle the op), best-effort `job->kill(Quietly)`
   (harmless/inert), then run `onTimeout()`** which fails/settles the owning
   `SyncOperation` exactly as that site's normal job-error branch would.
   Then `job->start()`. `m_transferTimeoutMs <= 0` disables the watchdog.
2. Route EVERY KDAV job start site in the file through it (grep
   `new KDAV::`; all nine already call `start()` explicitly — replace with
   the helper). Each site supplies a small `onTimeout` lambda mirroring its
   `job->error()` branch: the two `fetchItems` jobs `op->fail(...) + emit
   fetchFinished(false)`; the write paths reuse the same
   `checkDone()`/`settleIfDone()` accounting the success/error paths use (so
   a timeout settles the shared per-op counter identically to a failure — a
   `settleIfDone` lambda was extracted in `deleteItems` to match `pushItems`);
   the fire-and-forget `removeItem` delete just logs.
3. `setTransferTimeoutMs` needs no change (the helper reads the member at
   job-start time). Because the op is failed directly by `onTimeout`, its
   `errorString` is set to an explicit "timed out" message per site — no
   reliance on `davJobErrorMessage` or a killed job's (empty) `errorString`,
   and no subclassing of KDAV types.

**RED tests first** (use `FakeCalDavServer::setDropRequests(true)` — H1.2's
drop mode): (a) `fetchItems` against a dropping server must complete with
the op **Failed** within ~3× a 2000 ms `setTransferTimeoutMs` (today: hangs
— cap with `QTRY_VERIFY_WITH_TIMEOUT`); (b) one write-path job (pushItems /
createRecord route) same recipe. Optional third: engine-level — a full
`runSync` against a dropping server finishes `success == false` and a
subsequent `runSync` is accepted (the O22 end-to-end pin, cheap now that
the fake does the stalling).

**Acceptance gate (H5.5):** full suite green; new tests green; then re-run
the CP-B live pulled-cable smoke (`live_cpb_smoke`, built with
`-DKALBURATOR_BUILD_LIVE_PROBES=ON`, scratch Radicale + `RADICALE_PID` set)
— all four phases must PASS, including fail-within-timeout, post-resume
recovery, and the stranded item landing. FINDINGS O25 → Resolved, O22 →
Resolved (all halves accounted: timeout both stacks, honest rejection,
gate cancel; the `stopWorkerThread` mid-marshal deadlock note stays parked
in H9).
**Do NOT:** subclass or fork KDAV types; no engine changes; no changes to
`davSyncRequest` (already covered by H1.2).

---

## 9. CP-B — checkpoint: strong-model review + release v0.83 (H6)

**Entry:** H1–H5 landed, suite fully green. **STOP unless strong model.**

1. **Review:** re-run the audit's §6 spot checks against the final
   diffs: grep for engine-side `cachedCollectionRevision` /
   `primeRevisionCache` (must be none), unmarshaled backend calls in
   `runPropertyPhase`/`dispatchFirstSync` (none), gate op deletion,
   timeout wiring. Re-run the stall probe 5× (no flake). Skim each
   H-phase's final commit for scope creep.
2. **Live smoke:** run the lib test suite plus one real-server pass —
   Radicale at `localhost:5232`, account `testuser1/password1` (see
   PlanStan CLAUDE.md) — drive `examples/reference_consumer` or a
   scratch test through create→sync→modify→sync→converge, plus one
   pulled-cable stall check (stop Radicale mid-sync; sync must fail
   within the timeout, engine must accept a new runSync afterward —
   that is O22's end-to-end proof).
3. **H6 release mechanics (any agent may do this part after the review
   is recorded):** merge `feature/d1-threading` → `main` (`--no-ff`),
   tag **v0.83** with a message naming: D0 (already on main), D1 T1.1–
   T1.4, H1–H5, FINDINGS O16–O24 dispositions, and the two
   consumer-visible notes: (a) engine no longer calls
   `cachedCollectionRevision`/`primeRevisionCache` (WildPalms: verify
   independent usage), (b) BaselineStore schema v6 (additive,
   self-migrating). Update roadmap §5 and this doc's §10.

Record the CP-B ruling (a paragraph: what was checked, anything found)
in §10.

---

## 10a. Phase H7 — PlanStan adoption (absorbs old D1 plan Stages 2–3)

**Entry:** v0.83 tagged. **Repo: `~/dev/PlanStan`**, branch `master`
(work on a feature branch `feature/d1-io-thread`, merge when green).
Read first: `docs/todo/sync-apply-phase-model-refresh.md` (the D0
mitigation you will remove) and the archived D1 execution plan's Stage
2–3 sections (file:line inventory of construction sites — still
accurate for PlanStan).

**Steps:**

1. **Pin bump:** `PLANSTAN_LIBKALBURATOR_GIT_TAG` → `v0.83` in top-level
   `CMakeLists.txt`. Build; fix any compile fallout (expected: none —
   no public API removed).
2. **Drop the D0 mitigation** exactly per
   `docs/todo/sync-apply-phase-model-refresh.md` (it documents its own
   removal); delete that todo file in the same commit.
3. **I/O thread:** `CollectionController` creates one
   `QThread m_backendIoThread` (started in
   `initializeSyncInfrastructure`, stopped+waited in the destructor
   **after** `SyncEngine::stopWorkerThread()` — ORDER IS LOAD-BEARING:
   engine first, I/O thread second; add a comment saying why, citing
   FINDINGS O22/B6).
4. **De-parent + relocate backends:** every sync-backend construction
   site (LocalBackend, RemoteCalendarBackend, provider
   `createBackend` paths — the old plan's Stage 2 inventory lists
   them) constructs the backend **unparented**, calls its setters
   (`setDbPath`, cache dir, URL registration) and then
   `backend->moveToThread(m_backendIoThread)` **before first
   operation**. Ownership: track in the existing registry containers;
   delete via `deleteLater()` marshaled to the I/O thread at teardown.
   **ALL sync backends go to the ONE shared I/O thread** — per-backend
   threads are forbidden until O21's invariant note is revisited
   (H2 made dispatchFirstSync safe, but this keeps the conservative
   topology the lib test matrix actually covers).
5. **Call-site sweep:** every direct PlanStan-side backend method call
   (grep for the backend types; the old plan Stage 3 lists the
   consumers — ItemLoadingCoordinator, mirror/provider paths, editor
   save path) must be either (a) signal/slot (auto-queued — fine), or
   (b) wrapped in `QMetaObject::invokeMethod(backend, …,
   Qt::BlockingQueuedConnection)` if it needs a result, or (c) queued
   fire-and-forget. **Never call a backend method directly from the
   GUI thread once backends are relocated.** Add
   `Q_ASSERT(QThread::currentThread() == thread())` at the top of the
   2–3 hottest backend entry points touched here if not already
   present from T1.1.
6. **Suite + app:** `WAYLAND_DISPLAY=wayland-0 ctest` full PlanStan
   suite; then drive the real app against Radicale
   (`PLANSTAN_NONINTERACTIVE=1`, dev build) — open collection, sync,
   edit an event mid-sync, close mid-sync (teardown-order proof — the
   app must exit cleanly, no hang).

**Acceptance gate (H7):** PlanStan suite green; app run clean including
the mid-sync close; `docs/` updated (CLAUDE.md status line; delete the
D0-mitigation todo).

## 10b. Phase H8 — CP-C: live verification + campaign close (STOP unless strong model)

1. Soak: PlanStan dev build against Radicale with the 120s auto-sync,
   ≥30 min, with a large local mirror + a remote calendar; watch for
   heartbeat jank (use the GUI responsiveness subjectively + the
   journal), memory growth (the O23 leak is fixed — RSS should be
   flat across cycles), and convergence (no busy cycles when idle).
2. Adversarial passes: kill Radicale mid-push (O17 live check: restart
   it, next cycle must repair, no stranded item); edit a local .ics
   mid-sync (O18 live check: next cycle picks it up); saturate with a
   600+ item calendar (N4/N7 regression).
3. Close out: FINDINGS O16–O24 all Resolved; roadmap §5 D1/v0.83 lines
   ticked; PlanStan CLAUDE.md campaign section rewritten to "complete —
   see archive"; this doc's §10 fully ticked and the doc moved to
   `docs/campaign/archive/` with a pointer left in FINDINGS. Decide
   H9's fate (schedule or park).

## 10b'. Phase H8.5 — honor the RecordWriter BackendThread contract in applyBatch (O27; gates campaign close)

Found live at CP-C/H8 (2026-07-06): `applyBatch`'s BackendThread branch
(`src/engine/syncengine.cpp` ~2945–2962) runs `writer->apply()` on the
**worker thread**, violating `recordwriter.h:34`'s documented contract, so
every steady-state CalDAV *update* executes `setRawIcs`'s QNAM I/O and
thread-affine QSql etag-cache writes cross-thread (live UB + silently
failing cache persistence). Creates are unaffected (marshaled `pushItems`
gate). Full evidence + root cause in FINDINGS **O27**.

Pre-decided design (CP-C, strong model):

1. **Split the guard from the apply.** In `applyBatch`, compute the
   mass-delete-guard decision on the worker thread exactly as today
   (`m_baselineStoreAnchor` BlockingQueuedConnection is engine-thread-safe
   from there), mutating `batch.deletes` as needed — but do NOT call
   `writer->apply()` inside that lambda.
2. **Marshal the apply for BackendThread writers.** After the guard
   decision, dispatch `writer->apply(colId, creates, updates, deletes)`
   via `QMetaObject::invokeMethod(backend, ..., Qt::BlockingQueuedConnection)`
   when `writer->threading() == Threading::BackendThread`; keep the direct
   worker-thread call for `Threading::WorkerThread` writers (their apply()
   marshals internally). This removes the deadlock the current shape was
   dodging — the guard's engine-thread round-trip no longer nests inside a
   backend-thread BlockingQueuedConnection.
3. **RED first** (H2.1's thread-recording pattern): stub backend records
   `QThread::currentThread()` in `updateRecord`/`deleteRecord`; a
   steady-state modify sync (pre-seeded baseline, one changed record) must
   record the backend's thread. Today it records the worker thread.
4. **Acceptance gate:** RED test green; full libkalburator suite green
   (O26 flake excepted); re-run the H8 40-edit live pass against scratch
   Radicale with `QT_FATAL_WARNINGS` unset but the log grepped — zero
   "Cannot create children" / "does not belong to the calling thread" /
   "database not open" lines on the update path; etag cache rows visibly
   updated (restart the app after an edit sync and confirm the next
   re-diff serves that item from cache instead of re-downloading — the
   bounded cache-rot symptom in FINDINGS O27's scope-check note). Then
   CP-C §3 close-out may proceed.

## 10c. Phase H9 — deferred backlog (schedule at CP-C; file per-item plans when picked up)

Not in scope before CP-C. Inventory (audit + roadmap D2, deduped):

- **O28 post-crash phantom conflicts** (found CP-C/H8): same-UID/
  no-baseline byte-differing pairs after a partial push re-conflict every
  cycle (busy re-diff loop until manual dock resolution). Fix: canonical-
  equality check before declaring baseline-less conflicts, or adopt-by-ETag
  on create-response-lost. See FINDINGS O28.

- **Backend op-queue serialization / async `davSyncRequest`** (audit
  B7 — nested-loop re-entrancy; the deepest remaining design debt).
- RFC 6578 `sync-collection` REPORT; persist/seed KDAV EtagCache
  (roadmap D2).
- Incremental expected-fingerprint for LocalBackend (removes the
  accepted one-cycle re-diff lag from H3).
- LocalBackend `FingerprintStore` + remote `pendingCtag`-via-
  `primeRevisionCache` paths: engine-unused after H3 — remove or
  repurpose (coordinate with WildPalms).
- DecSync `runActiveSync` off the GUI thread; `processSync`'s
  m_cancelled-clear race; per-incidence `itemFetched` signal storm;
  `updateRecord` wrong-calendar fallback; `RecordMergerICal` dead code
  (roadmap D2 items).

---

## 10. Checklist (single source of truth — update in the landing commit)

- [x] **H1.1** gate await/cancel/delete (RED: `gateOps_areDeleted_afterSync`) — 2026-07-05
- [x] **H1.2** QNAM transferTimeout + FakeCalDavServer drop mode — 2026-07-05
- [x] **H1.3** distinguishable overlap rejection — 2026-07-05
- [x] **H1.4** delete dead `await<>`; fix localbackend comment + CLAUDE.md cancellation para — 2026-07-05
- [x] **H2.1** runPropertyPhase marshaled per backend (RED: thread-recording stub) — 2026-07-05 (also fixed a 5th, previously-unlisted unmarshaled call site: the T9 baseline snapshot in unifiedContinueAfterConflicts)
- [x] **H2.2** dispatchFirstSync split per backend (RED: two-thread first sync) — 2026-07-05
- [x] **CP-A** strong-model ruling on H3 design recorded here (2026-07-05, Fable):
      **CONFIRMED with two amendments** (both edited into §6 directly). Reviewed
      audit §1.4 + §2 (A1–A4) against post-H1/H2 code (`feature/d1-threading`
      @ `08c6239`). (1) Pre-fetch-snapshot semantics: CONFIRMED sound —
      `m_freshState` is captured in `prepareSyncFastPath` before
      `startWorkerThread()`, so the persisted token is never newer than the
      data the run consumed; staleness is always safe-direction (worst case
      one redundant re-diff), including the multi-mapping case where the
      snapshot predates earlier queue entries' writes, and the
      single-mapping/`processSingleMapping` case which persists nothing
      (an unpersisted self-write just re-diffs next cycle). (2) Leaving
      `ChangeDetection::cachedCollectionRevision`/`primeRevisionCache`
      present-but-engine-unused: CONFIRMED — six backend families implement
      them (akonadi ×2, remote ×2, local, universal ×2); removal is
      cross-repo churn with WildPalms exposure, doc-comment approach is
      right. (3) One-cycle re-diff lag after self-writes: CONFIRMED
      acceptable — audit A3 proves the remote side never had the
      optimization anyway, so this codifies actual behavior; H9 owns the
      incremental-fingerprint optimization. Amendment 1: BaselineStore is
      already at schema v6 (B4 hash columns), so H3 bumps v6→v7 — the
      plan's "v5→v6" was stale. Amendment 2: `clearMappingV3` must also
      clear the mapping's sync_tokens, otherwise an API-level baseline
      wipe with surviving tokens re-opens A4. Also noted: the step-4 grep
      obliges updating two `syncengine.h` doc comments. Clobber-token
      clearing in `driveQueue` only (not the single-mapping clobber path)
      was examined and is safe: single-mapping runs persist no tokens, and
      a clobbered target's fresh fingerprint/ctag always diverges from any
      stale stored token, so the mapping re-diffs. H3 is cleared to
      implement as amended.
- [x] **H3** BaselineStore v7 sync_tokens + engine-owned tokens; persistRevision deleted
      (RED: O17 pin, O18 pin, skip-still-works, clobber-clears) — 2026-07-05.
      Also fixed a gap the phase's own change introduced: `driveQueue`'s
      clobber branch must clear `m_freshState` too, or a stale entry from a
      prior non-clobber run gets silently re-persisted right after
      `clearSyncTokens`. See FINDINGS O17 for the full landing note.
- [x] **H4** fast path on worker; **stall probe green**; cancel-during-fast-path test — 2026-07-05.
      `driveQueue()`'s inline `prepareSyncFastPath()` call replaced with a
      `fastPathRequested`/`fastPathReady` command-channel signal pair
      (worker slot `SyncEngineWorker::prepareFastPath`, engine slot
      `onFastPathReady` -> shared `finishDriveQueueSetup()` continuation).
      `SyncEngine::FreshSyncState` made public (crosses the signal
      boundary now). New test `cancelDuringFastPath_reportsCancelled`
      pins the cancellation window; `stallProbe_relocatedBackends_
      stayResponsive` green for the first time (5x re-run, no flake).
      Full suite 160/160 green — first fully-green run of the campaign.
      See FINDINGS O16 for the landing note.
- [x] **H5** recordsFromLastFetch single-fetch pipeline (RED:
      `singleFetch_localBackends_noRedundantRead`,
      `singleFetch_remoteBackend_noRedundantListing`) — 2026-07-05. Added
      `SyncBackendBase::recordsFromLastFetch`; `LocalBackend` and
      `RemoteCalendarBackend` populate a single-shot per-collection memo at
      `fetchItems()` (Remote via a `SyncOperation::finished` hook, firing
      uniformly across every completion branch rather than each
      `op->complete()` site); `dispatchSync`'s two "Fetch source/target
      records" blocks now serve from it instead of `loadRecordsOrError()`
      whenever the gate's own `fetchItems()` succeeded. Also fixed a latent
      hash-instability bug the RED test surfaced: `LocalBackend::fetchItems`
      read files with `QIODevice::Text` (strips `\r`) while `loadRecords()`'s
      `recordFromFile()` didn't — different bytes/hash for the same file,
      manufacturing spurious Conflicts in place of genuine Deletes. Full
      suite 160/160 green. See FINDINGS O23 for the landing note.
- [x] **CP-B** strong-model review + live Radicale smoke (2026-07-05, Fable) —
      **review PASSED, release BLOCKED on a live-smoke finding (O25 → new
      phase H5.5).** What was checked: (1) §6 spot checks all clean —
      `grep cachedCollectionRevision|primeRevisionCache` over `src/engine/`
      empty; `runPropertyPhase`'s four `DomainOperations` call sites and
      `dispatchFirstSync`'s three-marshal split all run on the owning
      backend's thread; gate blocks await `!isFinished()`, cancel on wake,
      `deleteLater()` on every exit; QNAM timeout wired in `nam()` +
      re-applied by `setTransferTimeoutMs`. (2) Full suite 160/160 green;
      stall probe re-run 5× — no flake (654–655 ms, stable). (3) Scope skim
      of all five H-phase landing commits: each stays inside its phase's
      named files; H2's fifth unmarshaled call site and H5's
      `QIODevice::Text` hash fix are in-scope surprises, properly documented.
      One protocol deviation noted, no action: H3's RED tests landed in the
      same commit as the implementation (plan §0.3 wants a separate RED
      commit) and in `tests/engine/` rather than the planned
      `tests/calendar/` — content verified sound. (4) Live smoke against
      real Radicale (scratch instance, port 5233; system instance at 5232
      currently has no test accounts configured — empty `users` file),
      backends relocated to one shared I/O thread (H7 topology):
      create→sync→modify(foreign)→sync→converge→settle→quiet-cycle ALL
      PASS. (5) Pulled-cable (SIGSTOP mid-cycle): **FAILED** — sync future
      never finished (>90 s vs 5 s configured timeout), engine wedged,
      subsequent runs rejected. Root cause: KDAV jobs run on KDAV's
      internal QNAM, untouched by H1.2's timeout → recorded as FINDINGS
      **O25** with a pre-decided watchdog design in new phase **H5.5**.
      The smoke driver is committed as `tests/engine/live_cpb_smoke.cpp`
      (gated behind `KALBURATOR_BUILD_LIVE_PROBES`, OFF by default) for the
      post-H5.5 re-run. H6 must not proceed until H5.5's acceptance gate —
      including a clean re-run of all four smoke phases — passes.
- [x] **H5.5** KDAV job watchdog (O25/O22; RED: drop-mode fetch + write
      timeouts) + clean re-run of the CP-B pulled-cable smoke — 2026-07-05.
      Added `RemoteCalendarBackend::startJobWithWatchdog(KJob*, onTimeout)` and
      routed all nine KDAV job start sites through it. **Design correction
      (recorded in §8b + FINDINGS O25):** the CP-B `job->kill(EmitResult)`
      mechanism is INERT on KDAV 6.27.0 — no KDAV job overrides `KJob::doKill()`
      so `kill()` is a no-op that emits nothing (verified vs installed headers +
      KDE source `v6.27.0`); a verbatim impl left both RED tests hanging. The
      watchdog now fails the owning `SyncOperation` directly via a per-site
      `onTimeout` (detaches the job first so a late real completion can't
      double-settle; `deleteItems` grew a `settleIfDone` lambda to share the
      accounting tail with the timeout path, mirroring `pushItems`). RED tests
      `fetchItems_/pushItems_droppedRequests_failsWithinTimeout` go hang→green;
      full suite 159/160 green (the 1 is O26, a pre-existing MockBackend-only
      cancellation flake, ~1-in-3, unrelated to this diff — see FINDINGS O26).
      **CP-B live pulled-cable smoke re-ran ALL PASS** (scratch Radicale :5233,
      one shared I/O thread): phase 4's frozen-server sync failed in 9.5 s with
      `"Failed to list items: transfer timed out"`, engine accepted a fresh
      runSync after SIGCONT, stranded item landed (O17 live proof). O25 → Resolved,
      O22 → Resolved. H6/v0.83 is now unblocked.
- [x] **H6** merge → main, tag v0.83, roadmap §5 updated — 2026-07-05.
      `feature/d1-threading` merged to `main` (`--no-ff`, merge commit
      `e32fac3`); annotated tag **v0.83** at that commit naming D0, D1
      T1.1–T1.4, H1–H5.5, FINDINGS O16–O25 dispositions, and the two
      consumer-visible notes (engine no longer calls
      `cachedCollectionRevision`/`primeRevisionCache`; BaselineStore
      schema v7 additive/self-migrating). Pre-tag suite 159/160 green —
      the 1 is O26 (pre-existing MockBackend cancellation flake, passes
      5/5 in isolation, unrelated). Roadmap §5 D1 line + release table
      updated in the same commit. Next: **H7** (PlanStan adoption).
- [x] **H7** PlanStan: pin bump, D0-mitigation removal, I/O thread, de-parent,
      call-site sweep, teardown proof — 2026-07-06 (PlanStan branch
      `feature/d1-io-thread`). Pinned to **v0.83**; dropped the D0
      `reloadModelEligibleCalendars` mitigation (engine `recordChanged` now
      covers fresh-creation). Added `src/sync/backendinvoke.h` (the "one door":
      `invokeOnBackend` / `queryBackendBlocking`) and routed every GUI-side
      backend call through it; `CollectionController` owns one shared
      `planstan-backend-io` `QThread` and relocates Local + Remote backends
      (config-declared **and** provider-mirrored) onto it right after
      construction/wiring. Teardown order load-bearing: `stopWorkerThread()`
      then `stopBackendIoThread()` (moves backends back to the main thread —
      provider-owned ones outlive CC — then quit+wait) before any backend
      delete. Kill switch `PLANSTAN_SYNC_IO_THREAD`. **Simplification vs the
      plan:** T2.3's BackendFacts cache was proven unnecessary — `backendType`
      / `shapeFor` / `recurrenceCapabilities` are pure constants in
      libkalburator, safe to read cross-thread; the metatype audit found the
      one boundary-crossing non-builtin (`Incidence::Ptr`) already registered.
      **Verified:** `tst_sync_conflicts` 123/0 and `tst_collectioncontroller`
      28/0 with backends genuinely on the I/O thread (no "Cannot queue" /
      "different thread" warnings; the pre-existing collectioncontroller
      teardown-hang flake is *fixed* by the new ordering); live app against
      scratch Radicale relocated the CalDAV backend and fetched an event via
      real DAV on the I/O thread. Pre-existing dev/offscreen suite failures
      (evicted-graph Not-Run, four fragile MainWindow-offscreen integration
      tests, the `tst_collectionassembler` release-`Q_ASSERT` harness bug) all
      reproduce identically on `master` — see PlanStan
      `docs/bugs/preexisting-suite-failures-dev-offscreen.md`. **Next: CP-C/H8**
      (soak + adversarial live verification, strong-model checkpoint).
- [x] **CP-C / H8** soak + adversarial live verification — 2026-07-06 (Fable).
      **Verification PASSED on all planned gates; campaign close BLOCKED on a
      new finding (O27 → phase H8.5), same pattern as CP-B/O25.** Setup:
      scratch Radicale :5233, fresh vault (local + caldav backends on the
      shared I/O thread), `bulk` = 650 items, `soak` = 10, 120s auto-sync.
      (1) N4/N7 saturation: 650-item initial push + fetch-back served from
      cache, single-fetch pipeline confirmed (no redundant re-listing).
      (2) Soak ≥30 min: RSS flat at 173.4 MB across 7+ consecutive idle
      cycles after peaking 179 MB under load (O23 holds live); idle cycles
      skip both mappings ("2 unchanged; 2 actually skipped") after the
      accepted one-cycle re-diff lag; GUI responsive throughout.
      (3) O17 kill-mid-push: SIGKILL after 7/30 creates landed → remaining
      creates failed fast (no wedge), mapping success:false, token NOT
      advanced (verified vs live fingerprint), server restarted → next
      cycle repaired, 30/30 on server, no stranded item. Side-finding:
      the 7 pre-kill copies (same-UID/no-baseline/byte-different)
      re-conflict every cycle until manually resolved → FINDINGS **O28**
      (H9 inventory, not campaign-blocking).
      (4) O18 edit-mid-sync: foreign remote edit pulled to local ✓; a local
      .ics edited 0.5 s into the pull cycle was picked up and pushed by the
      very next cycle ✓ (no masking).
      (5) Teardown: app closed mid-activity twice during the session — clean
      exits, no hang (O22/B6 ordering holds).
      **New finding O27:** the 40-edit modify pass exposed
      `applyBatch`'s BackendThread branch calling `writer->apply()` on the
      worker thread — every steady-state CalDAV update runs `setRawIcs`
      QNAM I/O + etag-cache SQL cross-thread (live UB; the persistent
      etag-cache write fails on every update — 41/41 in the soak log —
      so updated items re-download once after each app restart; in-run
      convergence unaffected, verified by count). Pre-decided fix + RED
      recipe in §10b' (H8.5). Also **(6) teardown proof:** SIGTERM mid-
      bulk-sync exited clean in ~2 s.
- [ ] **H8.5** applyBatch honors RecordWriter::BackendThread (O27; RED:
      thread-recording stub on updateRecord/deleteRecord) — gates close-out
- [ ] **CP-C close-out (§10b item 3)** after H8.5: FINDINGS dispositions,
      doc archive, CLAUDE.md rewrite, H9 schedule/park decision
