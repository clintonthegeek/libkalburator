# Sync-excellence campaign — phase plan (THE live plan for both repos)

**Date opened:** 2026-07-07
**Status:** OPEN — no phase started.
**Scope:** the final clearing-up of every known sync-engine fault, flaw, and
inefficiency left open at the close of the sync-hardening campaign
(2026-07-06, v0.84): FINDINGS **O26, O28** and the new **O29–O36** (seeded by
this plan's opening commit), the audit's parked design debt (B7 nested-loop
re-entrancy — the deepest remaining), the roadmap's D2 protocol backlog
(RFC 6578 `sync-collection`, EtagCache persistence), and the dead/dishonest
machinery the Discipline Log has accumulated. Both repos — libkalburator
(primary) and PlanStan (E10/CP-C) — are owned by this campaign until every
phase is closed. WildPalms is a coordination constraint (INVARIANTS §10),
not a work site.
**Predecessor:** `archive/2026-07-05-sync-hardening-phases.md` (H1–H8.5,
CLOSED — read its §10 for what already landed; do not redo it). The
first-principles audit
(`archive/2026-07-05-first-principles-sync-architecture-audit.md`) remains
the architectural reference: its §1 target model is THE model this campaign
finishes building; its B7/C4 findings are this campaign's E5/E1/E3.
**Goal in one sentence:** libkalburator becomes a best-practices,
standards-compliant, maximally-efficient universal sync engine — domain- and
backend-agnostic at the core, with CalDAV as the first-class, fully
RFC-conformant exemplar — and PlanStan is its live proof.

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
E1, E3, E4, E6, E9 — any competent agent; designs are fully pre-decided.
E2, E5.x, E7, E8, E10 — mid-tier or better; non-trivial diffs or
root-cause work.
**CP-A, CP-B, CP-C — STOP: these are strong-model checkpoints**
(Opus/Fable-class). If you are not confident you are such a model, end
the session and report that the campaign is waiting on a checkpoint.

**Hard rules (from `docs/campaign/INVARIANTS.md` + both prior campaigns'
postmortems — violations get the work reverted):**

- No `processEvents()`. No NEW nested `QEventLoop` anywhere (E5 exists to
  DELETE the remaining ones — the count only goes down). No mutex added
  around a backend. No `sleep`/`msleep` as synchronization.
- RED test first for every behavior change; prove it fails for the stated
  reason before implementing.
- Build with `cmake --build build -- -j 8` — **NEVER `--parallel`**.
- Full suite: `WAYLAND_DISPLAY=wayland-0 ctest --test-dir build -j 8`.
  Never force `QT_QPA_PLATFORM=wayland`. Single tests run with
  `QT_QPA_PLATFORM=offscreen ./build/tests/<dir>/<test>`.
- One phase (or one E5 stage) per session. Land it fully or mark it
  BLOCKED.
- Every commit that changes phase state also updates §10 here.
- **Universality rule (this campaign's own):** every engine- or
  `SyncBackendBase`-level contract you add must be stated and tested
  domain-neutrally (MockBackend/LocalBackend first), THEN exercised by the
  CalDAV backend as the first-class implementation. A contract that only
  a CalDAV type can satisfy belongs in `src/calendar/`, not the neutral
  layers. The five WildPalms invariants (INVARIANTS §10) hold throughout.

---

## 1. Entry state (as of campaign open)

- **Repo:** `~/dev/libkalburator`, branch **`main`** at **v0.84**
  (`aea44e4`) plus this campaign's opening docs commit. All campaign lib
  work happens on a new branch **`feature/sync-excellence`** cut from
  `main`; merge to `main` happens at the mid-campaign tag (after E4,
  optional) and at CP-B.
- **Suite:** 160 tests; 160 green *except* `tst_engine_cancellation`,
  which SEGFAULTs nondeterministically ~1-in-3 under parallel ctest
  (FINDINGS **O26** — E2 owns it; until E2 lands, a suite run where only
  that test failed and it passes standalone counts as green).
- **PlanStan:** `~/dev/PlanStan`, branch `master`, pinned to libkalburator
  **v0.84** (`PLANSTAN_LIBKALBURATOR_GIT_TAG`, top-level CMakeLists.txt).
  Not touched until E10.
- **Operational invariants inherited from the hardening campaign (still
  binding):** all sync backends share ONE I/O thread in PlanStan
  (`planstan-backend-io`); teardown order is engine worker first, backend
  I/O thread second; `RecordWriter::BackendThread` applies run on the
  backend's own thread (O27). E5.3 *changes* the apply mechanism — it may
  not silently weaken any of these; the CP-A review checks exactly that.
- **Background reading** (only if your phase says so): audit §1 (target
  model) and §3 B7; FINDINGS O26, O28, O29–O36;
  `2026-07-03-sync-convergence-roadmap.md` §Track D2 (superseded by this
  plan; kept as origin record).
- **Line numbers in this doc** are against `main` @ `aea44e4` and drift
  as phases land. Every reference names its symbol; when a line is stale,
  grep the symbol and trust the symbol.

---

## 2. Why the phases are in this order

Cheap, independent hygiene and honesty fixes go first (E1) because they
shrink every later diff and make `SyncResult` trustworthy — later phases'
gates *read* those stats. The O26 flake is fixed second (E2) because every
subsequent acceptance gate depends on trusting the suite; a known ~1-in-3
crash poisons that. E3/E4 land the remaining small correctness items while
the tree is still familiar. Then the campaign's center of gravity: a
strong-model checkpoint (CP-A) reviews the async-backend design, and E5
performs the deepest cut — deleting the nested-event-loop re-entrancy
(audit B7) by making backend I/O operation-based end to end. E6 and E7
(protocol efficiency) come AFTER E5 because both build on E5.2's async
request primitive — implementing `sync-collection` on the old nested-loop
`davSyncRequest` would mean writing it twice. E8 (O28 phantom conflicts)
is independent of E5 but placed after it so the conflict-adoption logic is
written against the settled write path. E9 is polish. Then release (CP-B),
PlanStan adoption (E10), and live verification + close (CP-C).

If E5 stalls or is re-scoped at CP-A, everything before it is already
landed and taggable — the campaign degrades gracefully instead of holding
correctness fixes hostage to the big refactor.

---

## 3. Phase E1 — dead machinery + honest stats (O30, O31)

**Entry:** §1 state. Independent of everything; do first.
**Findings:** O30 (`SyncResult` stats never populated but read), O31
(dead `updateSyncMetadata`/`makeCalendarRec`; `RecordMergerICal` parses
canon JSON as iCal; engine-unused `primeRevisionCache` residue).
**Files:** `src/engine/syncengine.{h,cpp}`,
`src/calendar/icalrecordmerger.{h,cpp}` (delete),
`src/calendar/calendardomaindefinition.cpp` (registration check),
`src/types/synctypes.h` (doc comments only), `tests/engine/` (new test).

### E1.1 — wire SyncStats from the writer batches

`SyncResult::sourceStats/targetStats` (`synctypes.h:156-157`) are read in
two live places — `advanceQueue`'s aggregate-success check
(`syncengine.cpp:803-806`, `statsOk`) and `onWorkerSyncCompleted`'s
cancelled-path `skipped` classification (`:1168-1171`) — but nothing in
the unified dispatch path ever populates them (grep-confirmed; Discipline
Log 2026-07-04). Consequences: `statsOk` is vacuously true, and every
cancelled run is misreported `skipped=true` ("never started") even when
it cancelled after partial writes — consumers cannot distinguish the two,
which is exactly the distinction the field exists to carry.

**Steps:** in `unifiedContinueAfterConflicts`, after each side's
`WriterBatch` apply returns, populate the corresponding side's
`SyncStats` from the batch: `created/updated/deleted` from the applied
counts, `errors` from the writer's failed-record count, `conflicts` from
the mapping's unresolved-conflict count. Populate for BOTH sides (the
target-side apply and the source-side apply both exist post-D0). Do not
change any signature — the stats ride the existing `SyncResult`.

**RED test first** (`tests/engine/tst_sync_result_stats.cpp`, new):
(a) a two-item create sync reports `targetStats.created == 2`, source
stats zero; (b) a sync cancelled after the first of two applies reports
`skipped == false` (today: `skipped == true` because stats are zero);
(c) a genuinely never-started cancel keeps `skipped == true`. Use
MockBackend pairs (domain-neutral per §0's universality rule).

### E1.2 — delete the dead engine machinery

1. Delete `SyncEngine::updateSyncMetadata` + its `makeCalendarRec` helper
   (`syncengine.cpp:912+`, decl `syncengine.h:476`) — zero call sites
   outside their own definitions (grep-verified 2026-07-07). They write
   legacy `domain="calendar"/encoding="ical"` baseline rows invisible to
   `baselineHashesForMappingV4`'s filter — live corruption risk if ever
   re-wired (Discipline Log 2026-07-04).
2. Delete `RecordMergerICal` (`src/calendar/icalrecordmerger.{h,cpp}`)
   and its registration: in the unified path its inputs are canon-shaped
   JSON, `parseIcal` returns null, and it silently degrades to
   side-picking (roadmap D2 note). The active merger is `CanonJsonMerger`
   (`calendardomaindefinition.cpp:36-40`). Grep for remaining references
   (tests included) and delete or migrate them.
3. `ChangeDetection::primeRevisionCache` residue: grep WildPalms
   (`~/dev/WildPalms`) for `primeRevisionCache` and
   `cachedCollectionRevision`. If WildPalms has zero call sites, delete
   `primeRevisionCache` from the `ChangeDetection` interface and every
   implementation (six backend families — the H3 doc comments already
   mark them engine-unused), and delete `LocalBackend`'s
   prime-to-persisted-store body with it. If WildPalms DOES use them,
   leave the interface, note the usage in FINDINGS O31, and delete only
   the LocalBackend direct-store-write body (the A2 hazard). Either way,
   `cachedCollectionRevision` STAYS (backends use it internally).

**Acceptance gate (E1):** full suite green (O26 flake excepted); new
stats tests green; `grep -rn "updateSyncMetadata\|makeCalendarRec\|RecordMergerICal" src/ tests/`
empty (or, for RecordMergerICal, only migrated test names); FINDINGS
O30/O31 → Resolved with dated notes.
**Do NOT:** touch the diff/merge logic itself, `advanceQueue`'s
aggregation shape, or anything in `src/calendar/remotecalendarbackend.cpp`
— later phases own those.

---

## 4. Phase E2 — root-cause the cancellation-teardown flake (O26)

**Entry:** E1 landed. Read FINDINGS O26 and the Discipline Log's
2026-07-04 `tst_engine_cancellation` note first.
**Finding closed:** O26 (intermittent SEGFAULT, ~1-in-3 under parallel
ctest, passes standalone — observed across two campaigns; almost
certainly a real race in cancel-during-marshal teardown against
MockBackend, i.e. potentially a REAL bug wearing a flake costume).
**Files:** unknown until root-caused — expected:
`src/engine/syncengine.cpp` (worker teardown / cancel path),
`tests/engine/tst_engine_cancellation.cpp`. If the fix wants to touch
backend files, stop and check it against E5's scope first.

### Design (decided as method, not as fix — the fix follows the evidence)

1. Reproduce deterministically: build with
   `-DCMAKE_CXX_FLAGS="-fsanitize=thread -g"` (TSAN first; switch to ASAN
   if TSAN is clean but the crash persists) and loop
   `ctest --test-dir build -R tst_engine_cancellation --repeat until-fail:50`
   under parallel load (`ctest -j 8` on the full suite in another shell
   if needed to recreate contention).
2. Root-cause from the sanitizer report. Expected shapes (from the O26
   note): a `SyncOperation`/watcher outliving its backend across a
   cancel; a queued signal delivered into a destroyed worker; a test-side
   object torn down while the engine still references it.
3. Classify honestly: if the race is in PRODUCTION code, fix it there
   (RED first: a test that pins the ordering — e.g. repeated
   cancel-then-destroy in a loop under the sanitizer); if it is purely
   test-harness lifetime, fix the harness and say so in FINDINGS.
4. Do not "fix" it by serializing the test, adding sleeps, or marking it
   flaky — those are the forbidden outcomes.

**Acceptance gate (E2):** 50 consecutive sanitizer-build repetitions
green; 3 consecutive full parallel suite runs with zero
`tst_engine_cancellation` failures; FINDINGS O26 → Resolved naming the
actual mechanism.

---

## 5. Phase E3 — cancellation + teardown honesty (O33, O22 residue)

**Entry:** E2 landed.
**Findings:** O33 (the `m_cancelled`-clear race; DecSync active
controllers run synchronously on the caller's thread), plus the
`stopWorkerThread` mid-marshal wedge note parked from O22.
**Files:** `src/engine/syncengine.cpp`, `src/engine/syncengine_p.h`,
`tests/engine/` (tests).

### Design (decided)

1. **`m_cancelled` clear race** (audit C4): `processSync` clears
   `m_cancelled` at dispatch (`syncengine.cpp:1542`), so a cancel landing
   between queue advance and worker start is erased and the cancelled
   queue runs one more full mapping. Fix: clear `m_cancelled` ONLY in
   `driveQueue()`'s run-entry (`:363` — the legitimate new-run reset);
   in `processSync`, replace the clear with a check — if `m_cancelled`
   is already set, short-circuit to the cancelled-result path without
   dispatching. RED test: `runSync`, then `future.cancel()` immediately,
   then assert via a MockBackend fetch counter that NO mapping dispatched
   after the cancel was observable (drive timing with the mock's
   controllable latency, no sleeps).
2. **DecSync active controllers** (audit C4): `driveQueue`'s controller
   loop (`syncengine.cpp:376-380`) calls `it.value()->runActiveSync()`
   synchronously on the caller's thread — a §1 role violation for
   whoever enables DecSync next. Fix minimally and honestly: move the
   loop into the worker (dispatch via the existing command-channel
   pattern, exactly like `fastPathRequested` — the controllers are
   `QObject`s; document that `runActiveSync` executes on the worker
   thread and must marshal its own backend touches). RED test: a stub
   active controller records its executing thread; today it records the
   caller's thread.
3. **`stopWorkerThread` bounded teardown** (O22 residue): today, if the
   worker is parked in a `BlockingQueuedConnection` against a backend on
   the caller's own thread (any consumer that has NOT relocated its
   backends — WildPalms), `stopWorkerThread`'s `quit(); wait();`
   deadlocks forever. Full dissolution is E5.3's job (the worker stops
   parking in marshals for I/O-length work). E3 lands the honest interim:
   `wait(deadlineMs)` (30 s default), and on expiry log a loud
   `qCritical` naming the invariant ("a sync backend lives on the thread
   calling stopWorkerThread — relocate backends or destroy the engine
   from a different thread") and `wait()` again unbounded (do NOT
   `terminate()` — a killed worker mid-SQL-write is worse than a hang;
   the diagnostic is the deliverable). RED test: not practical without
   deliberately deadlocking a test — pin instead with a unit test on a
   new extracted helper `waitForWorkerWithDiagnostic()` using a stub
   thread that delays past a 100 ms test deadline, asserting the
   diagnostic fires and the wait still completes.

**Acceptance gate (E3):** full suite green; the three new tests green;
FINDINGS O33 → Resolved; O22's parked note updated to point at E5.3 for
the structural fix.
**Do NOT:** touch the fetch gates, `applyBatch`, or any backend file.

---

## 6. Phase E4 — CalDAV write-path correctness (O32)

**Entry:** E3 landed. This phase is `src/calendar/` scoped — the CalDAV
first-class surface.
**Findings:** O32 (`updateRecord` try-all-calendars fallback), plus
pinning the already-correct-but-untested ETag-precondition contracts.
**Files:** `src/calendar/remotecalendarbackend.cpp`,
`tests/sync/fakecaldavserver.{h,cpp}` (precondition support if missing),
`tests/calendar/` (tests).

### Design (decided)

1. **Delete `updateRecord`'s try-all fallback**
   (`remotecalendarbackend.cpp:2401-2424`): the primary path already
   resolves the owning calendar via the `m_localEtags` url-key check;
   the fallback loop ("try all registered calendars, first success
   wins") can write an item into the WRONG calendar on a multi-calendar
   backend and multiplies failed-PUT latency. Replace with: if the
   etag-map lookup misses, consult the persistent content cache
   (`CalDavContentCache` knows the item's URL) for ownership; if that
   also misses, FAIL the update with a distinct error ("uid not found in
   any owned calendar") — never guess. Same treatment for `deleteRecord`
   if it shares the pattern (check; the grep at `:2430+` suggests it
   does).
2. **Pin the ETag precondition contracts** (they exist — `setRawIcs`
   sends `If-Match` with the cached ETag (`:2166-2170`), creates go
   through `DavItemCreateJob` (`If-None-Match: *`), and the 412
   force-retry is confined to the user-resolved-conflict `startSync`
   path (`launchStartSyncModify`, documented single-retry) — but no test
   asserts any of it, so nothing stops a regression):
   - `FakeCalDavServer` grows real precondition semantics if it lacks
     them: reject PUT with 412 when `If-Match` mismatches the stored
     etag; reject create with 412 when `If-None-Match: *` matches an
     existing resource.
   - RED tests: (a) a steady-state update against a server-side
     concurrent edit (fake bumps the etag first) must FAIL with the 412
     surfaced — no silent overwrite, no auto-force; (b) the next sync
     cycle after that 412 re-diffs and detects the concurrent change
     (conflict or re-merge — pin whichever the engine actually produces,
     with a comment); (c) a create colliding with an existing resource
     fails and is re-routed as an update on the retry path
     (`remotecalendarbackend.cpp:1016`'s documented behavior).
3. **Property-phase PROPPATCH suppression check** (roadmap D2's last
   item): one test — two quiet cycles after a color change; the second
   cycle must issue ZERO PROPPATCHes (fake counts them). If it fails,
   the T9 property-baseline snapshot has a gap: root-cause, file a new
   O-number, and fix within this phase only if the fix is ≤ the phase's
   scope; otherwise BLOCK per §0.

**Acceptance gate (E4):** full suite green; new tests green; FINDINGS
O32 → Resolved. **After E4, any agent may (optionally) merge → `main`
and tag v0.85** ("correctness batch: O26, O30–O33 + write-path pins") —
do it if the campaign is about to pause, skip it if E5 starts next
session.
**Do NOT:** touch `fetchItems`, `davSyncRequest`, or the op/queue
machinery — E5 owns all of it.

---

## 7. CP-A — checkpoint: strong model reviews E5's design (STOP if not strong model)

Before any E5 stage is implemented, a strong model must:

1. Re-read audit §1.1 (the backend model: thread-affine, asynchronous,
   serialized, no nested loops) and §3 B7, then the E5 design below,
   against the code as it stands post-E4.
2. Confirm or amend, explicitly, each of: (a) the per-collection FIFO op
   queue's neutrality (does it live in `SyncBackendBase` without leaking
   CalDAV shapes?); (b) the async-write-operation contract and its
   interaction with the mass-delete guard (H8.5's three-marshal shape is
   being REPLACED — the guard must still resolve on a thread that may
   safely block toward the engine thread); (c) the fate of
   `RecordWriter::Threading` (E5.3 proposes retiring `BackendThread`
   blocking applies — verify WildPalms exposure); (d) cancellation
   semantics of in-queue vs in-flight ops; (e) that the teardown-order
   invariant (engine worker first, I/O thread second) survives or is
   explicitly re-derived.
3. Decide the E5 stage boundaries are still right (three stages, one
   session each, independently landable) or re-cut them.
4. Record the ruling (dated paragraph) in §10 under CP-A and in FINDINGS
   O29. Amendments edit E5's text directly.

CP-A exists because E5 rewires how every byte moves between the engine
and every backend — the one place a wrong pre-decision costs the whole
campaign.

---

## 8. Phase E5 — async backend core: delete the nested loops (O29 / audit B7)

The deepest cut. Audit §1.1's backend model, finally implemented: a
backend is a single-threaded async I/O service whose operations are
queued, serialized per collection, and completed by continuations —
**never by spinning a nested event loop on its own thread**. Nested loops
are live re-entrancy: any app-side call marshaled to the backend mid-sync
(editor save, calendar-list refresh) executes *inside* the in-flight
operation's wait, interleaving unguarded mutations of `pendingCtag`,
`m_lastRawIcsByUid`, the EtagCache, and the content cache — the named
candidate mechanism for the historical N5 corruption class. This phase
removes the mechanism, not just the symptom.

**Entry:** E4 landed, CP-A recorded. Read audit §1.1/§3-B7 first.
**Files:** `src/sync/syncbackendbase.{h,cpp}`,
`src/calendar/remotecalendarbackend.{h,cpp}`,
`src/calendar/localbackend.cpp`, `src/engine/syncengine.cpp` (apply
path), `src/shape/recordwriter.h`, tests throughout.

### Stage E5.1 — per-collection FIFO operation queue (neutral layer)

**Design (decided):** `SyncBackendBase` gains an internal op queue keyed
by collection id: every operation-producing entry point
(`fetchItems`, `pushItems`, `startSync`, and E5.3's new write op)
enqueues; at most ONE operation per collection is in flight; the next
starts from the previous one's `finished` continuation (queued, on the
backend thread). Operations on different collections may interleave
(they share no per-collection state; the shared stores are
thread-affine-safe because everything still runs on the one backend
thread). Cancellation of a queued-not-started op completes it Cancelled
without starting it. The queue is invisible to callers — same signatures,
same `SyncOperation*` returns (state `Pending` until dequeued).

**CP-A amendment A1 (2026-07-07):** the operation-producing entry points
span two layers — `fetchItems`/`deleteItems` are `SyncBackendBase`
virtuals, but `pushItems`/`startSync` are calendar-typed (`SyncBackend`,
`src/calendar/syncbackend.h`). The queue therefore lives in
`SyncBackendBase` as a **protected neutral enqueue primitive** (e.g.
`enqueueOperation(collectionId, SyncOperation*, start-functor)`) that
BOTH the base entry points and the calendar-typed subclass entry points
call; the queue itself never names a calendar type. Three contract
details are binding: (i) queue advance fires on ANY terminal transition
(`Succeeded`/`Failed`/`Cancelled`/`NotSupported`) AND on premature
`QObject` destruction of the op (engine gates `deleteLater` ops they
own — hold entries as `QPointer` and also connect `destroyed`);
(ii) an op that is already finished at enqueue time (e.g. the base's
immediately-failed `NotSupported` defaults) must never occupy the
in-flight slot; (iii) `cancelOperationsFor`/`cancelAllOperations`
traverse both queued and in-flight ops.

**RED tests first** (`tests/sync/tst_backend_op_queue.cpp`, MockBackend +
LocalBackend — neutral first per §0): (a) two `fetchItems` on the same
collection: the second's body must not START until the first finishes
(instrument with started/finished order recording); today they
interleave. (b) ops on two different collections DO overlap. (c) cancel
of a queued op never runs its body. (d) a full engine sync over
LocalBackend↔LocalBackend still passes end-to-end (no starvation, no
deadlock via the gate awaits).

### Stage E5.2 — async `davSyncRequest`; zero nested loops in the fetch/CTag paths

**Design (decided):** add
`void davSyncRequestAsync(const DavRequest &req, std::function<void(const DavResponse&)> done)`
— the existing QNAM plumbing minus the `QEventLoop`
(`remotecalendarbackend.cpp:253`); the continuation runs on the backend
thread from the reply's `finished` signal (timeout already covered by
H1.2's transferTimeout). Convert every backend-internal call site that
currently blocks on the loop — `fetchAllCtags`, `collectionRevision`'s
PROPFIND, discovery, `setRawIcs`, calendar CRUD — into
continuation/operation form. Where a caller genuinely needs a synchronous
answer, the ONLY permitted shape is: the call is entered from a
NON-backend thread (worker or GUI) and blocks THERE while the backend
thread runs the async chain (this is what `runOnBackendThread` +
operation awaits already give the engine). The synchronous
`davSyncRequest` is deleted when its last internal call site converts;
`awaitOperation` (`remotecalendarbackend.cpp:333`) likewise — its three
call sites (`:2326`, `:2396`, `:2437` — the blob CRUD paths) convert in
E5.3.

**RED test first:** a re-entrancy pin — during an in-flight `fetchItems`
against a latency-injected fake, marshal an unrelated backend call
(e.g. `cachedEtag` read or a small write) onto the backend thread;
assert via sequence recording that it executes strictly BEFORE or AFTER
the fetch operation's body, never between the fetch's request and its
continuation (today the nested loop runs it mid-wait — RED). Plus: grep
gate — after E5.2, `QEventLoop` appears in `src/calendar/` only in the
E5.3-owned blob-CRUD sites (listed above) and `calendarmanager.cpp`
(see A4), and after E5.3, only in `calendarmanager.cpp`.

**CP-A amendment A4 (2026-07-07):** the original claim above ("only in
the E5.3-owned blob-CRUD sites") was false against post-E4 code — two
more `src/calendar/` files spin loops. (i) `icsfeedfetcher.{h,cpp}`:
grep-verified ZERO call sites — dead code; E5.2 **deletes it** (and its
CMake entry). (ii) `calendarmanager.cpp` (:583/:630/:677): three
op-await loops that spin on the CALLER's (GUI) thread — live PlanStan
consumers (`CollectionSession`, `MainWindow`). This is NOT B7's
backend-thread re-entrancy mechanism, and converting the app-facing
CalendarManager API to async is out of E5's scope — filed as FINDINGS
**O39**; E5.2 annotates each of the three loops with an `// O39:`
comment instead of converting them. The E5 acceptance grep gate is
amended accordingly (below).

**Amendment A5 (2026-07-08) — the `davSyncRequest` call sites are three
groups with three owners; the helper does NOT die in E5.2.** Tracing how
each of the seven synchronous `davSyncRequest` sites is *reached* (not
just where it sits) splits them cleanly:

- **Group A — CTag/PROPFIND (the actual B7 mechanism):** `fetchAllCtags`
  (`:721`) and `fetchFreshCtag` (`:754`). `fetchFreshCtag` is spun
  *inside* `fetchItems`'s own async op body (`:1481`); `fetchAllCtags` is
  reached through the worker fast-path marshal (`collectionRevisions`,
  `syncengine.cpp` `prepareFastPath` → `runOnBackendThread`) and via
  `modifiedSince` (`:2448`). Backend-thread nested loops running inside
  already-async operations — exactly audit B7. **E5.2 converts these and
  ONLY these** (matches this stage's title, "the fetch/CTag paths").
- **Group B — raw-ICS blob CRUD:** `setRawIcs` (`:2156`, ← `updateRecord`
  `:2418`) and `getRawIcs` (`:2123`, ← `loadRecord` `:2354`). E5.3's
  design bullet 2 already names "update → async `setRawIcs`"; these are
  part of the write-op / blob-read restructure. **E5.3 owns them.**
- **Group C — calendar-collection CRUD:** `createCalendar` (`:1226`),
  `updateCalendar` (`:1323`), `deleteCalendar` (`:1361`). Reached ONLY
  from CalendarManager's synchronous `bool` GUI-thread API and
  `calendardomainoperations.cpp`, never through the engine op pipeline;
  their two consumers are oppositely threaded (libkalburator's own tests
  call same-thread; PlanStan marshals onto the backend io-thread).
  Converting them forces CalendarManager's collection-CRUD API async —
  the SAME app-facing-API work A4 carved out as O39. **Owned by the new
  phase E11 (below), which absorbs O39.**

Consequence: the original E5.2 clause "the synchronous `davSyncRequest`
is deleted when its last internal call site converts" is WRONG as
sequenced — its last callers (Group C) are O39/E11 work, explicitly
outside E5. So: the synchronous `davSyncRequest` helper **survives E5**
(Groups B and C keep it alive through E5.3), and its deletion moves to
**E11**, which removes its last callers. Until then the surviving
helper's `QEventLoop` (`remotecalendarbackend.cpp:253`) carries an
`// O39/E11:` annotation, and the E5 grep gate allows it — the same
mechanism A4 used for `calendarmanager.cpp`. `awaitOperation` still dies
in E5.3 as originally planned (Group B is its last user).

Nothing is orphaned: all seven sites have a named owner (A→E5.2, B→E5.3,
C→E11); the helper's death is scheduled into E11; O39 is promoted from a
filed finding to a checklisted phase (§14a).

### Stage E5.3 — writes become operations; the blocking apply retires

**Design (decided, CP-A reviews it):** the engine's apply phase stops
calling into backends with thread-blocking record CRUD. Mechanism:

1. `SyncBackendBase` gains
   `virtual WriteOperation* applyRecords(const QString &collectionId, const WriterBatch &batch)`
   (neutral: `BackendRecord` lists in, per-record success/failure out via
   the operation; default implementation adapts existing
   `createRecord`/`updateRecord`/`deleteRecord` synchronously on the
   backend thread — correct for backends without async internals, e.g.
   LocalBackend, MockBackend).
2. `RemoteCalendarBackend` overrides it natively: the batch drives the
   existing KDAV job chains / `davSyncRequestAsync` continuations
   (create → `DavItemCreateJob`, update → async `setRawIcs`, delete →
   `DavItemDeleteJob`), all watchdogged (H5.5), all serialized by E5.1's
   queue — `awaitOperation` and its nested loops are deleted here.
3. The engine's `applyBatch` (in `unifiedContinueAfterConflicts`)
   replaces the H8.5 three-marshal shape with: resolve the mass-delete
   guard on the worker (unchanged — it blocks toward the engine-thread
   anchor, which is safe from the worker; **CP-A amendment A2
   (2026-07-07): the guard MUST resolve BEFORE the write op is
   enqueued — the enqueued op carries the already-filtered delete list;
   never move guard resolution into the backend-side op body**), then
   start `applyRecords` via
   a queued invoke and AWAIT the operation with the same cancellable,
   watchdogged gate pattern as the fetch gates (H1.1 semantics: await
   `!isFinished()`, cancel on wake, `deleteLater` on exit). The worker
   no longer parks in a `BlockingQueuedConnection` for I/O-length work —
   which structurally dissolves E3's `stopWorkerThread` interim and O22's
   last parked note.
4. `RecordWriter::Threading::BackendThread`'s "engine wraps apply() in a
   BlockingQueuedConnection" contract (`recordwriter.h:34`) is superseded:
   `DefaultBlobWriter`'s classify logic is retained (it is pure), but its
   apply tail routes through `applyRecords`. Update the header contract
   text in the same commit; per CP-A item (c), coordinate the
   consumer-visible note for WildPalms in the CP-B tag message.
   **CP-A amendment A3 (2026-07-07):** go further — DELETE the
   `Threading` enum and `threading()` outright, and delete `applyBatch`'s
   `WorkerThread` branch: grep across libkalburator, PlanStan, and
   WildPalms found zero `threading()` overrides (the enum's only
   implementation is the default), zero WildPalms references to
   `RecordWriter` or `awaitOperation` at all. WildPalms' only lib-sync
   surface is `SyncEngine` + the `itemFetched` signal (E9/E10
   territory). The CP-B tag note shrinks to "Threading enum removed —
   no known consumer implemented it".

**RED tests first:** (a) re-run of H8.5's
`steadyStateWrites_appliesOnBackendThread` must stay green (the thread
contract survives the mechanism change); (b) a mid-apply cancel settles
the write op and the run reports cancelled-with-partial-stats (E1.1's
field now proves it); (c) mid-apply `stopWorkerThread` (engine destroyed
while a slow fake server is mid-batch) completes without deadlock within
the watchdog window — the pin for O22's final closure; (d) the E5.2
re-entrancy pin extended over a write: an app-side call during an
in-flight apply never interleaves into it.

**Acceptance gate (E5, after all three stages):** full suite green;
`grep -rn "QEventLoop" src/calendar/ src/sync/` hits ONLY in
`calendarmanager.cpp` (three `// O39:` loops, A4) and
`remotecalendarbackend.cpp`'s ONE surviving `davSyncRequest` helper
(annotated `// O39/E11:`, amendment A5 — kept alive by Group C calendar
CRUD until E11 deletes it); every hit carries its annotation;
`icsfeedfetcher.{h,cpp}` deleted;
`grep -rn "awaitOperation" src/` empty; FINDINGS O29 → Resolved; O22's
parked teardown note → Resolved (pointing at E5.3's test c); audit B7 →
noted resolved in the audit doc's header.
**Do NOT:** change diff/merge/conflict logic, fetch delta semantics, or
the ctag/etag commit discipline (N5) — E5 changes WHERE and HOW calls
run, not what they compute. No mapping-level parallelism (still
deliberately deferred — see §14 inventory).

---

## 9. Phase E6 — seed the KDAV EtagCache from the persistent content cache (O35)

**Entry:** E5 landed (uses its settled fetch path; no hard dependency,
but do not reorder — the RED test's fetch-count instrumentation assumes
E5's single-fetch/queued shape).
**Finding closed:** O35 — KDAV's `EtagCache` (`m_etagCache`,
`remotecalendarbackend.cpp:377`) is in-memory per session, so the first
CTag-*changed* re-diff after every app restart re-lists AND re-downloads
the whole collection even though `CalDavContentCache` (persistent,
keyed url+etag) already holds the bytes. Surfaced live at H8.5
verification.

### Design (decided)

At the first `fetchItems` for a collection in a backend instance's
lifetime (lazily, on the backend thread — NOT in the constructor, which
may run on the GUI thread pre-relocation), populate `m_etagCache` with
every `(url, etag)` row the content cache holds for that collection
(`CalDavContentCache` already exposes the per-collection item
enumeration used by `serveCachedItems` — reuse it; add a
`urlEtagPairs(calendarId)` accessor if the existing surface forces full
content loads). Seed BEFORE the `DavItemsListJob` is created so the
listing's changed-set is computed against the persisted state. Items
whose server etag matches the seeded etag then fall into the
served-from-cache path that already exists (`:1639` "Cache miss -
shouldn't happen" stops being reachable on this path). The seed is
per-collection-once; E5.1's queue serializes it against concurrent
fetches for free.

**RED test first** (`tests/calendar/`): simulate a restart — sync once
(populates content cache), DESTROY the backend, construct a fresh
backend instance on the same cache dir, bump ONE item on the fake server
(ctag changes), sync again: assert via the fake's request log that
exactly ONE item body was fetched (today: all N re-download — RED).
Companion: fresh instance + ctag UNCHANGED still serves fully from cache
(pins no regression of the existing short-circuit).

**Acceptance gate (E6):** full suite green; both tests green; FINDINGS
O35 → Resolved.
**Do NOT:** persist KDAV's own cache object or subclass KDAV types; the
content cache remains the single persistent store.

---

## 10. Phase E7 — RFC 6578 `sync-collection` REPORT (O36)

**Entry:** E6 landed. The headline standards/efficiency phase.
**Finding closed:** O36 — no `sync-collection` support: every
CTag-changed poll pays a Depth:1 PROPFIND listing every item's ETag —
O(collection size) response XML for a 1-item change. RFC 6578 has the
server compute the delta (changed + deleted hrefs since a sync-token) —
including **deletion tombstones**, which also removes the full-listing
requirement for delete detection (`remotecalendarbackend.cpp:2473`'s
comment). KDAV 6.27.0 has no sync-collection job (verified against
installed headers) — we implement the REPORT ourselves on E5.2's
`davSyncRequestAsync`.

### Design (decided)

1. **Capability detection:** on collection discovery, PROPFIND
   `supported-report-set`; record `supportsSyncCollection` per collection
   in the existing `m_calendars` facts (Radicale ≥3 and Nextcloud both
   advertise it). Absent or false ⇒ the existing CTag+PROPFIND path runs
   unchanged, forever (it is the permanent fallback, not a deprecation).
2. **Token store:** persist the per-collection `sync-token` alongside the
   CTag — extend `CTagStore`'s table (additive column, self-migrating,
   same pattern as every prior schema bump) rather than a new store.
   NOTE the layering: this token is the *backend's cache-validity* token
   (audit §1.1) — it does NOT replace or touch the engine's
   per-mapping sync-progress tokens (H3); the two-token architecture is
   load-bearing and this phase must not conflate them again.
3. **Fetch path:** in `fetchItems`, when `supportsSyncCollection` and a
   stored token exists: issue
   `REPORT sync-collection` (Depth:0 body: `sync-token`, `sync-level` 1,
   prop `getetag`) via `davSyncRequestAsync`; parse the multistatus into
   changed hrefs (fetch via the existing chunked multiget) and 404
   tombstones (feed the existing deletion path directly). Commit the new
   sync-token with the SAME verified-commit discipline as the CTag (N5:
   only after every listed item materialized). When no stored token:
   fall through to the listing path once, then request a token via an
   empty-token REPORT (or capture from the initial REPORT — servers
   return the current token on a full-state response).
4. **Token invalidation:** on `valid-sync-token` precondition failure /
   HTTP 409/410/507 per RFC 6578 §3.3: clear the stored token, fall back
   to the full listing path for that cycle, re-acquire. Must be tested —
   this is the path real servers exercise after their own DB maintenance.
5. **CTag stays**: the cheap "anything at all changed?" pre-check
   (fetchAllCtags) is still worth one PROPFIND on quiet collections;
   sync-collection replaces only the *enumerate the delta* step.
6. **FakeCalDavServer** grows sync-collection: monotonic token counter,
   per-token change journal, tombstones, a `setInvalidateSyncTokens(bool)`
   knob for the 410 path, and advertises it in supported-report-set
   behind `setSupportsSyncCollection(bool)` so both paths stay testable.

**RED tests first:** (a) steady-state 1-changed-item cycle against a
sync-collection fake: request log shows ONE REPORT + one multiget of ONE
href, and NO Depth:1 items PROPFIND (today: full listing — RED);
(b) a remote deletion arrives via tombstone with no full listing;
(c) token invalidation (fake returns 410): cycle completes correctly via
fallback and re-acquires a token; (d) `setSupportsSyncCollection(false)`
run is byte-identical in behavior to pre-E7 (regression pin for the
fallback); (e) restart-shaped test: fresh backend instance + stored
token + one server change → one REPORT, one item fetched (composes with
E6).

**Acceptance gate (E7):** full suite green; the five tests green; a live
manual pass against scratch Radicale showing the REPORT in the server
log (record the evidence in the checklist entry); FINDINGS O36 →
Resolved; roadmap D2's sync-collection line ticked.
**Do NOT:** remove or weaken the CTag/PROPFIND path; touch engine code
(this is backend-internal delta mechanics — the engine contract is
unchanged).

---

## 11. Phase E8 — post-crash phantom-conflict adoption (O28)

**Entry:** E7 landed (independent of E6/E7 in logic; ordered here so the
write/conflict machinery is settled).
**Finding closed:** O28 — a partial push + server crash leaves same-UID/
no-baseline byte-differing pairs (local = original bytes, remote =
engine-serialized copy; PRODID/property order differ) that re-conflict
every cycle, fail the mapping, and burn a full re-diff every 120 s until
a human resolves N phantom conflicts in the dock.
**Files:** `src/engine/syncengine.cpp` (the no-baseline conflict
classification in the unified diff path), `src/diff/` (only if a helper
belongs there), `tests/engine/`.

### Design (decided)

In the per-record diff's same-UID/**no-baseline**/hash-differing branch
(the branch that today declares Conflict), insert a canonical-equality
check BEFORE declaring conflict: route both sides' bytes through the
mapping's existing shape pipeline to the domain's canonical encoding and
compare canon-level equality (the domain's `createCanonicalDiffer()` —
already the WildPalms-invariant diff surface — reporting zero changed
properties). If canonically equal: **adopt silently** — write each
side's own contentHash as its per-side baseline (the B4/N2 per-side
machinery supports exactly this), count it in stats as neither create
nor conflict, log one info line. If canonically different: conflict as
today (that IS a real conflict). Scope guard: this check runs ONLY in
the no-baseline branch — baselined records keep byte-hash semantics
untouched (performance and correctness both).

Deliberately NOT doing adopt-by-ETag on create-response-lost (the O28
alternative): the canonical-equality adoption covers that case too
(next cycle the pair exists on both sides, canonically equal, adopted),
without new push-path state. Record this decision in FINDINGS O28.

**RED tests first:** (a) engine-level replay of the H8 crash shape:
push N creates, kill the fake after k succeed (fake gains
`setDieAfterNWrites(k)`), restart fake, next cycle: assert ZERO
conflicts, all N present both sides, baselines adopted, mapping
`success == true` (today: k phantom conflicts, success false — RED);
(b) a same-UID pair that is canonically DIFFERENT still conflicts
(the guard against over-adoption); (c) domain-neutrality: a blob-domain
pair (no canon pipeline) keeps today's behavior — byte-differing
no-baseline pairs conflict (blob has no canonical form; adopting there
would guess). MockBackend for (c), calendar backends for (a)/(b).

**Acceptance gate (E8):** full suite green; three tests green; FINDINGS
O28 → Resolved.
**Do NOT:** touch the byte-hash fast path for baselined records; no
canon transcodes added to the steady-state unchanged path (the check
runs only where a conflict was about to be declared — rare by
construction).

---

## 12. Phase E9 — signal and fingerprint efficiency polish (O34)

**Entry:** E8 landed.
**Findings:** O34 (`itemFetched` per-incidence signal storm), plus the
H3-accepted one-cycle re-diff lag (audit A2's "sound version").
**Files:** `src/calendar/localbackend.cpp`,
`src/calendar/remotecalendarbackend.cpp`, `src/sync/syncbackendbase.h`
(new signal), PlanStan consumers at E10.

### Design (decided)

1. **Batch the per-item fetch signal:** `LocalBackend`'s fetch emits
   `itemFetched(calendarId, inc)` once PER INCIDENCE
   (`localbackend.cpp:772-775`) — post-relocation, a cross-thread queued
   signal per item, thousands of queued events on big mirrors. Add
   `itemsFetched(const QString &calendarId, const QList<KCalendarCore::Incidence::Ptr> &items)`
   to the base signal surface, emit it once per fetch pass (or per
   multiget chunk on the remote side — chunk granularity is fine),
   keep the singular signal emitted alongside for one release
   (deprecation comment), and remove it at E10 once PlanStan consumes
   the batch form. RED: a signal-count test — fetching a 50-item
   collection lands ≤ ceil(50/chunk) batch emissions where today it
   lands 50.
2. **Incremental expected-fingerprint for LocalBackend:** removes the
   accepted one-cycle re-diff lag after self-writes, the SOUND way
   (audit A2): at apply completion the backend computes the expected
   post-write fingerprint from its fetch-time snapshot plus its own
   write set (it stats exactly the files it wrote — no full re-scan,
   no foreign-edit absorption: any OTHER file's change still differs
   next cycle) and returns it as the write op's result token; the
   ENGINE (not the backend) stores it as the mapping's side token via
   the existing H3 `setSyncToken` path — preserving engine ownership of
   sync-progress tokens (the two-token architecture, again). E5.3's
   `WriteOperation` carries the token (add an optional
   `resultRevision()` — neutral, empty for backends that don't compute
   one; remote stays empty and keeps its one-cycle lag, which A3 proved
   was always the real behavior). RED: local↔local quiet cycle
   immediately after a writing cycle is SKIPPED (today: one redundant
   re-diff — RED), while a foreign edit landing during the writing
   cycle still defeats the skip (the O18 pin re-run against the new
   mechanism — extend `tst_sync_token_soundness`).

**Acceptance gate (E9):** full suite green; new tests green; FINDINGS
O34 → Resolved; the H3 "accepted costs" paragraph in the archived
hardening plan gets a one-line "local lag removed by E9" annotation.
**Do NOT:** batch signals by adding timers/debounce (emit at natural
pass/chunk boundaries only); no remote-side expected-ctag guessing.

---

## 13. CP-B — checkpoint: strong-model review + release v0.90 (STOP unless strong model)

**Entry:** E1–E9 landed, suite fully green (O26 fixed since E2 — no
excepted flakes remain, by construction).

1. **Review:** re-run this plan's grep gates (E5's `QEventLoop`/
   `awaitOperation` greps; E1's dead-machinery grep); skim each phase's
   landing commit for scope creep; re-run the E5.2 re-entrancy pins and
   the stall probe 5× each (no flake); verify the two-token architecture
   survived E7/E9 (engine never reads backend cache tokens as progress;
   backend never writes engine tokens).
2. **Live smoke** (scratch Radicale — see PlanStan
   `docs/bugs/radicale-test-server-auth-broken` memory / H8 setup notes;
   port 5233 pattern): create→sync→modify→converge; pulled-cable
   (SIGSTOP) fails within timeout and recovers; kill-mid-push now
   recovers with ZERO phantom conflicts (E8's live proof); verify the
   sync-collection REPORT appears in Radicale's log (E7 live proof);
   restart + 1 remote edit → exactly one item download (E6+E7 composed
   live proof).
3. **Release mechanics** (any agent after the ruling is recorded):
   merge `feature/sync-excellence` → `main` (`--no-ff`), tag **v0.90**
   naming: O26, O28–O36 dispositions, audit-B7 closure, and the
   consumer-visible notes — (a) `RecordWriter` BackendThread blocking-
   apply contract superseded by `applyRecords` operations (WildPalms:
   port note), (b) `itemFetched` deprecated for `itemsFetched`,
   (c) CTagStore schema additive bump (sync-token column),
   (d) `primeRevisionCache` removal IF E1.3 removed it. Update roadmap
   §5 and this §10.

Record the CP-B ruling (a paragraph: what was checked, anything found)
in §10.

---

## 14. Phase E10 — PlanStan adoption

**Entry:** v0.90 tagged. **Repo: `~/dev/PlanStan`**, branch `master`
(work on `feature/sync-excellence-adoption`, merge when green).

**Steps:**

1. **Pin bump:** `PLANSTAN_LIBKALBURATOR_GIT_TAG` → `v0.90`. Build; fix
   compile fallout (expected: none in GUI code; possible in any test
   stub implementing `RecordWriter`/backend interfaces).
2. **Adopt `itemsFetched`:** port `ItemLoadingCoordinator` (and any other
   consumer the grep finds) from the per-item signal to the batch
   signal; then delete the deprecated singular signal lib-side in a
   follow-up patch tag (v0.90.1) — coordinate in the same session if
   trivial.
3. **Re-assert the operational invariants:** backends still relocate to
   the shared I/O thread; teardown order unchanged; `backendinvoke.h`
   marshaling still the one door. Run `tst_sync_conflicts` and
   `tst_collectioncontroller` with backends genuinely relocated (the H7
   verification recipe).
4. **Suite + app:** full PlanStan ctest (known pre-existing dev/offscreen
   noise per `docs/bugs/preexisting-suite-failures-dev-offscreen.md`
   excepted); drive the real app against scratch Radicale
   (`PLANSTAN_NONINTERACTIVE=1`, dev build): open, sync, edit mid-sync,
   save from the editor DURING a sync cycle (the E5.1 serialization live
   proof — pre-E5 this interleaved), close mid-sync.
5. Update PlanStan CLAUDE.md status + delete/annotate any
   `docs/todo//docs/bugs/` items this campaign resolved
   (`sequential-sync-performance.md`'s remaining half, sync-related
   entries in `code-todos-features.md` — check each honestly).

**Acceptance gate (E10):** PlanStan suite green (known noise excepted);
app run clean including mid-sync editor save and mid-sync close; docs
updated.

## 14b. Phase E11 — app-facing CalendarManager async API (absorbs O39)

**Added 2026-07-08 by E5.2 amendment A5** — promotes FINDINGS O39 from a
parked residual to a scheduled, checklisted phase so the last B7-family
loops in the calendar backend, and the synchronous `davSyncRequest`
helper itself, actually die inside this campaign rather than nowhere.

**Entry:** E5 landed (all three stages), E10 landed. **Repo:
`~/dev/libkalburator`** for the backend/CalendarManager changes, with a
coordinated **`~/dev/PlanStan`** follow-up if the CalendarManager
signature shifts (the three GUI-thread consumers are PlanStan's
`CollectionSession`/`MainWindow`). Read amendment A5 (§8, Stage E5.2)
and FINDINGS O39 first.

**Scope — the two loop families A5/A4 deferred here:**
1. **Group C calendar-collection CRUD** (`createCalendar` `:1226`,
   `updateCalendar` `:1323`, `deleteCalendar` `:1361` in
   `remotecalendarbackend.cpp`): convert their backend-internal
   `davSyncRequest` bodies to `davSyncRequestAsync` continuation form.
   The dual-threaded-consumer problem is the crux (see A5): libkalburator's
   own tests + CalendarManager call these same-thread; PlanStan marshals
   them onto the backend io-thread. The correct shape is an
   operation-returning async backend method whose *caller* (worker or GUI,
   never the backend thread) blocks on the op — same rule E5.2 applied to
   Group A. `calendardomainoperations.cpp:49` (`updateCalendar`) converts
   with them.
2. **CalendarManager's app-facing CRUD API** (`createIncidence`/
   `updateIncidence`/`deleteIncidence` — the three `// O39:` GUI-thread
   op-await loops at `calendarmanager.cpp` :583/:630/:677, plus the direct
   cross-thread `pushItems`/`deleteItems` calls): convert to async so the
   GUI thread never spins an op-await loop.

**Deletes here:** the synchronous `davSyncRequest` helper
(`remotecalendarbackend.cpp:224`) and its `// O39/E11:`-annotated
`QEventLoop` — Group C is its last caller; and the three
`calendarmanager.cpp` `QEventLoop`s.

**RED tests first:** (a) a re-entrancy pin over a calendar-CRUD op (same
shape as E5.2's, extended to `createCalendar`); (b) a GUI-thread test
that `createIncidence` no longer spins a nested loop (instrument the
event loop / assert via a sequence recorder that no unrelated queued slot
runs mid-call).

**Acceptance gate (E11):** full libkalburator suite green + PlanStan
suite green (if touched); `grep -rn "QEventLoop" src/calendar/ src/sync/`
EMPTY (the A4/A5 carve-outs are now all converted — this is the gate
E5 could not yet meet); `grep -rn "davSyncRequest\b\|awaitOperation" src/`
empty; FINDINGS O39 → Resolved; audit B7 family fully closed in the
calendar backend.
**Do NOT:** touch `src/contacts/` (still §16 residual, its own rule-of-
three) or add mapping-level parallelism.

## 15. CP-C — live verification + campaign close (STOP unless strong model)

1. **Soak:** PlanStan dev build against scratch Radicale, 120 s
   auto-sync, ≥30 min, 650+ item calendar (reuse the H8 rig — see the
   `h8-live-verification-setup` memory): RSS flat, idle cycles skip,
   GUI responsive, zero cross-thread warnings, zero busy-loop re-diffs.
2. **Adversarial:** kill-mid-push → next cycle repairs with zero phantom
   conflicts (O28 live); pulled-cable (SIGSTOP) → fail within timeout,
   recover after SIGCONT; app restart mid-campaign-of-edits → next
   cycle downloads only genuinely-changed items (E6/E7 live); foreign
   edit mid-sync → picked up next cycle (O18 regression); editor save
   mid-sync → serialized, no corruption, both changes land (E5.1 live).
3. **Efficiency audit (the "are we actually efficient" gate):** capture
   one steady-state changed-cycle request log and assert the shape:
   1 CTag PROPFIND (quiet collections) + 1 sync-collection REPORT +
   1 multiget of exactly the changed hrefs + ETag-guarded PUTs of
   exactly the changed records. Record the log excerpt in §10.
4. **Close out:** FINDINGS O26, O28–O36 **and O39** (E11) all Resolved;
   roadmap §5 ticked
   through v0.90; both CLAUDE.md campaign sections rewritten to
   "complete — see archive"; this doc moved to `docs/campaign/archive/`
   with a pointer left in FINDINGS; decide the fate of the §16 residual
   inventory (park or schedule).

## 16. Residual inventory (explicitly OUT of this campaign — decide at CP-C)

- **Parallel mapping execution** — still deliberately sequential; the
  idle cycle is near-free post-E7 and the busy cycle is I/O-bound on one
  server. Revisit only with profiling evidence from a multi-server
  topology.
- **Akonadi ChangeRecorder warm path** (O14) — stays deferred per its
  own entry.
- **CardDAV/other-domain sync-collection** — E7's REPORT machinery is
  written in the CalDAV backend; if/when a CardDAV backend matures,
  extract the REPORT helper to a shared DAV layer THEN (rule of three),
  not now.
- **`src/contacts/remotecontactsbackend.cpp` nested loops** (noted at
  CP-A) — the contacts backend spins backend-thread `QEventLoop`s in its
  blob-view helpers, the same B7 shape E5 deletes from the calendar
  backend. Outside E5's files and grep gate; apply audit §1.1 (reuse
  E5.2's async-request pattern) when/if the contacts backend matures —
  same rule-of-three logic as the CardDAV sync-collection line above.
- ~~**FINDINGS O39**~~ — **PROMOTED to phase E11 (§14b) by amendment A5
  (2026-07-08).** No longer residual: CalendarManager GUI-thread op-await
  loops + Group C calendar-CRUD backend loops + the synchronous
  `davSyncRequest` helper all convert/die in E11, in-campaign.
- **RFC 6638 scheduling (iTIP/iMIP)** — application-layer feature
  (PlanStan `docs/todo/email-itip-scheduling-horizon.md`), not sync
  engine scope.

---

## 17. Checklist (single source of truth — update in the landing commit)

- [x] **E1.1** SyncStats wired from writer batches (RED: created-count,
      cancelled-partial `skipped=false`, never-started `skipped=true`) —
      2026-07-07, FINDINGS O30 Resolved
- [x] **E1.2** dead machinery deleted (updateSyncMetadata/makeCalendarRec,
      RecordMergerICal); primeRevisionCache decision recorded (WildPalms
      grep evidence in FINDINGS O31) — 2026-07-07, FINDINGS O31 Resolved.
      WildPalms grep: zero call sites for `primeRevisionCache` and
      `cachedCollectionRevision`; interface + all six backend
      implementations deleted outright (not left doc-commented).
- [x] **E2** O26 root-caused under TSAN and fixed (mechanism named in
      FINDINGS; 50× repeat + 3× full-suite gates) — 2026-07-07, FINDINGS
      O26 Resolved. Fix confined to `src/calendar/mockbackend.{h,cpp}` (a
      backend file — checked against E5's scope first per this phase's
      gate; MockBackend's test-only blocking-thread simulation is
      orthogonal to E5's real-backend nested-loop rework). A new
      thread-registry TSAN artifact surfaced during verification, filed
      separately as FINDINGS O37 (tool limitation, not an app bug, does
      not block this item).
- [x] **E3** m_cancelled race fixed; DecSync controllers on worker;
      stopWorkerThread bounded-wait diagnostic — 2026-07-07, FINDINGS O33
      Resolved. processSync now only checks m_cancelled (never clears
      it); the sole reset moved to worker slot resetCancellationFlag(),
      invoked once per run from driveQueue()/processSingleMapping()
      before that run's first mapping dispatches. DecSync active-
      controller loop moved to the worker thread via a new
      activeControllersRequested/runActiveControllers/
      activeControllersReady command-channel round trip (mirrors
      fastPathRequested/prepareFastPath); driveQueue()'s tail split into
      continueDriveQueueSetup() so it can resume either synchronously or
      as this round trip's continuation. stopWorkerThread's unbounded
      wait() replaced by waitForWorkerWithDiagnostic() (new
      src/engine/workerteardown.{h,cpp}): bounded wait, loud qCritical
      diagnostic on expiry, then unbounded wait (never terminate()).
      Three new tests: tst_engine_cancel_queue_race,
      tst_decsync_active_controller_thread, tst_worker_teardown. Full
      suite green (163/163, O26 flake not observed). Noted in FINDINGS
      O33: DecSyncControllerStore's SQLite connection is thread-affine
      to whichever thread constructed it, which is now a real gap since
      runActiveSync() executes on the worker thread — out of E3's scope,
      flagged for whoever enables DecSync for real.
- [x] **E4** updateRecord owning-calendar restriction; ETag-precondition
      contracts pinned; PROPPATCH suppression verified — 2026-07-07,
      FINDINGS O32 Resolved. `updateRecord`/`deleteRecord` now route through
      a new `findOwningCalendar(uid)` helper (ETag map, then
      `CalDavContentCache::contains()`, new accessor); the try-all-calendars
      fallback is deleted — a uid no registered calendar can show ownership
      for now FAILS distinctly instead of guess-writing/guess-deleting.
      `FakeCalDavServer` gained real RFC 7232 `If-Match`/`If-None-Match`
      precondition enforcement on PUT (412s), previously a no-op fake — new
      tests in `tst_remotecalendarbackend_blob_view.cpp` pin the ownership-
      miss failure (RED against the old fallback), the stale-ETag 412 (no
      silent overwrite), and the next-fetch pickup of the concurrent edit.
      PROPPATCH suppression pinned by `tst_sync_convergence.cpp`'s new
      `colorChangeThenQuietCycle_secondCycleIssuesZeroProppatches` — passed
      as-is; investigating why surfaced a real but out-of-scope gap, filed
      as FINDINGS O38 (property-phase baseline argument is always empty;
      T9's persisted baseline is written but never read back — masked in
      the two-way already-converged case by `computeMapDiff`'s
      same-value shortcut, but a real risk for asymmetric one-sided
      property edits). Full suite green, 163/163, O26 flake not observed.
- [x] *(optional)* mid-campaign merge + tag **v0.85** — 2026-07-07.
      Pre-tag full suite: 163/163 green, O26 flake not observed.
      `feature/sync-excellence` merged → `main` (`--no-ff`), tagged
      v0.85 "correctness batch: O26, O30–O33 + write-path pins".
- [x] **CP-A** strong-model ruling on E5 design recorded here —
      2026-07-07, Fable-class model, reviewed against `main` @ v0.85.
      **Ruling: E5's design is CONFIRMED with four amendments (edited
      into §8's text directly); the three-stage cut stands.** Per-item:
      (a) queue neutrality CONFIRMED — `SyncBackendBase` already owns
      neutral op tracking (`registerOperation`/`m_pendingOperations`,
      collection-keyed) and the neutral `SyncOperation`; but the
      operation-producing entry points span two layers (`fetchItems`/
      `deleteItems` on the base; `pushItems`/`startSync` calendar-typed
      on `SyncBackend`), so the queue must be exposed as a protected
      neutral enqueue primitive both layers call — amendment A1 states
      this in E5.1, plus queue-advance-on-any-terminal-state/destroyed
      and the immediately-finished-op rule. (b) mass-delete guard
      CONFIRMED — verified in code (`resolveMassDeleteGuard`,
      syncengine.cpp ~:2896): guard already resolves on the worker,
      separate from the apply marshal, blocking toward the engine anchor
      (baseline count) and the GUI (PlanStan `confirmOnGuiThread`) —
      both safe from the worker since neither ever blocks toward it.
      E5.3's shape preserves exactly this; amendment A2 pins that the
      guard resolves BEFORE the write op is enqueued (the op carries the
      already-filtered delete list). (c) `RecordWriter::Threading` —
      CONFIRMED, strengthened: grep across libkalburator, PlanStan, and
      WildPalms finds ZERO `threading()` overrides and zero WildPalms
      references to `RecordWriter`/`awaitOperation` at all; the
      `WorkerThread` branch in `applyBatch` is dead code in practice.
      Amendment A3: E5.3 deletes the `Threading` enum and `threading()`
      outright (not just supersedes the contract text) and deletes the
      WorkerThread branch; WildPalms exposure is nil (its only lib-sync
      surface is `SyncEngine`/`itemFetched` — E9/E10 territory, not
      E5.3). (d) cancellation semantics CONFIRMED as designed: queued-
      not-started → Cancelled without running the body, queue advances;
      in-flight keeps today's cooperative cancel + await-settle gate
      shape (ops aren't pre-emptible mid-record); `cancelOperationsFor`/
      `cancelAllOperations` must traverse both queued and in-flight.
      Ops now start life `Pending` until dequeued — the H1.1/O24 gate
      already checks `isFinished()` (never `state()==Running`), so no
      engine-side change needed; keep that discipline. (e) teardown
      order SURVIVES and is re-derived: worker-first/backend-second
      remains mandatory because the worker's cancel-then-await-settle
      (and E5.3's new apply-op gate) needs a live backend thread to
      settle in-flight ops; post-E5.3 the worker no longer parks in
      BlockingQueuedConnection for I/O-length work, dissolving the O22
      wedge as planned (E3's bounded-wait diagnostic stays as backstop
      for the short classify marshals that remain). Stage boundaries:
      three stages, independently landable — CONFIRMED as cut.
      **Scope finding:** E5.2's claim that post-E4 `src/calendar/`
      QEventLoops live only in the blob-CRUD sites is FALSE —
      `calendarmanager.cpp` spins three GUI-thread op-await loops
      (:583/:630/:677, live PlanStan consumers) and `icsfeedfetcher.cpp`
      spins one with ZERO call sites (dead code). Amendment A4:
      E5.2 deletes `icsfeedfetcher.{h,cpp}` (grep-verified orphan);
      CalendarManager's loops are GUI-thread, NOT B7's backend-thread
      mechanism, and converting that app-facing API is out of E5's
      scope — filed as FINDINGS **O39**; the E5/CP-B grep gate is
      amended to allow `calendarmanager.cpp` hits only (annotated with
      an O39 reference). `src/contacts/remotecontactsbackend.cpp`'s
      backend-thread loops are outside E5's files and gate; added to
      §16 residual inventory. Recorded in FINDINGS O29 same date.
- [x] **E5.1** per-collection FIFO op queue (neutral layer) — 2026-07-07.
      `SyncBackendBase` gains a protected `enqueueOperation(collectionId, op,
      startFunctor)` primitive (`src/sync/syncbackendbase.{h,cpp}`): a
      per-collection `QList<QueuedOp>` queue plus an in-flight marker
      (`QHash<QString, QPointer<SyncOperation>>`); at most one op per
      collection runs at a time; the next dequeues from the previous one's
      `finished` signal (also handles premature `QObject::destroyed`, per
      amendment A1(i)); an op already terminal when its turn comes (e.g.
      cancelled while still queued) is skipped without ever running its
      body (A1(ii)). `registerOperation`/`cancelOperationsFor`/
      `cancelAllOperations` are unchanged and already cover queued-not-yet-
      dequeued ops (A1(iii)) since `registerOperation` fires at enqueue
      time, before dequeue. The start functor is always deferred one
      event-loop tick (`QTimer::singleShot(0, ...)`), preserving the
      "caller can connect signals to the returned op before it starts"
      guarantee every backend's own ad hoc `QTimer::singleShot(0, ...)`
      gave previously. `LocalBackend::fetchItems/pushItems/deleteItems`
      and `MockBackend::fetchItems/pushItems/deleteItems` now route through
      it (their old direct `registerOperation()` + `QTimer::singleShot(0)`
      wrapping is gone; MockBackend's synchronous test-fixture branches —
      `m_fetchOpFailsSilently`, `m_useBaseFetchItems` — and its synchronous
      `setState(Running)` moved inside the deferred functor, since queued
      ops now start life Pending until dequeued, matching CP-A ruling (d);
      the engine's fetch gate already treats fetchItems as
      asynchronous-capable via `isFinished()`, confirmed by reading
      `syncengine.cpp`'s existing gate comment, so this was a timing-only
      change with no observable-behavior regression). `RemoteCalendarBackend`
      (CalDAV) is deliberately NOT wired in this stage — its fetchItems body
      still runs the nested-loop `davSyncRequest` internally, which E5.2
      rewrites to async form; wiring the queue around a synchronous nested
      loop now would be thrown away next session, so queueing CalDAV's
      entry points is deferred to land naturally alongside E5.2's async
      conversion. New RED-then-GREEN test
      `tests/calendar/tst_backend_op_queue.cpp` (plan named
      `tests/sync/`, moved to `tests/calendar/` since it needs
      MockBackend/LocalBackend, which live there and require
      `KF6::CalendarCore`/`kalburator_calendar_test_stubs`): (a) same-
      collection ops serialize (RED confirmed: pre-fix both started on the
      same tick), (b) different-collection ops overlap (always passed),
      (c) cancel of a still-queued op never runs its body (RED confirmed),
      (d) a full LocalBackend↔LocalBackend `SyncEngine::runSync()` still
      converges end-to-end through the queue (always passed — no
      starvation/deadlock). Full suite green, 164/164 (163 pre-existing +
      the new test), O26 flake not observed.
- [ ] **E5.2** async davSyncRequest; nested loops out of fetch/CTag paths
      (re-entrancy pin RED→GREEN). **Re-cut by amendment A5 (2026-07-08):**
      converts **Group A only** (`fetchAllCtags`/`fetchFreshCtag`) + wires
      `RemoteCalendarBackend`'s fetch/push/delete entry points into E5.1's
      op queue (deferred from E5.1); Group B → E5.3, Group C → E11; the
      synchronous `davSyncRequest` helper survives E5 (annotated
      `// O39/E11:`) and dies in E11.
- [ ] **E5.3** applyRecords write operations; blocking apply retired;
      O22 teardown note closed (mid-apply stopWorkerThread pin)
- [ ] **E6** EtagCache seeded from content cache (restart re-download pin)
- [ ] **E7** RFC 6578 sync-collection REPORT + token store + invalidation
      + fallback regression pin + live Radicale evidence
- [ ] **E8** O28 canonical-equality adoption (crash-replay pin; blob
      neutrality pin)
- [ ] **E9** itemsFetched batching; LocalBackend incremental
      expected-fingerprint (lag removed; O18 pin re-run)
- [ ] **CP-B** strong-model review + live smoke + merge + tag **v0.90**
- [ ] **E10** PlanStan adoption (pin bump, itemsFetched port, invariants
      re-asserted, mid-sync editor-save live proof)
- [ ] **E11** app-facing CalendarManager async API (absorbs O39, §14b):
      Group C calendar-CRUD loops + CalendarManager incidence-CRUD GUI
      loops converted; synchronous `davSyncRequest` helper deleted;
      `grep QEventLoop src/calendar/ src/sync/` finally EMPTY; O39 Resolved
- [ ] **CP-C** soak + adversarial + efficiency audit + campaign close
