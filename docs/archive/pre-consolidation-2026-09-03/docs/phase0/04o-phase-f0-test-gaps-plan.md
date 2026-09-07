# Phase F.0 — Test gap closure Implementation Plan

**Status:** Landed 2026-04-29 on tag `v0.12-phase-f0-test-gaps`.
All tasks complete; libkalburator 21/21, PlanStan 96/120 (matches
Phase E baseline), WildPalms 73/73 at tag.

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:executing-plans (or subagent-driven-development) to
> work this plan task-by-task. Steps use checkbox (`- [ ]`) syntax
> for tracking.

**Goal:** Add a libkalburator-side integration test that pins the
backend write/fetch error-propagation contract against
`SyncCoordinator`. After Phase F.0, libkalburator no longer depends
on PlanStan's `tst_sync_error_recovery` as the sole defender of
this contract. Bundled: a FINDINGS entry that records the triage
of the 24 PlanStan "failures" so future agents don't re-derive it.

**Architecture:** Pure addition. One new test file in
`tests/calendar/`, wired via the existing
`kalburator_add_calendar_integration_test()` CMake helper. No
production-code changes. The test exercises
`MockBackend::setFailurePoint()` at three surfaces (`OnFetch`,
`OnStoreItems`, `OnDelete`) and asserts
`SyncResult::success == false` arrives via
`SyncCoordinator::allSyncsCompleted`.

**Tech Stack:** Qt6, KCalendarCore (KF6), QTest, CMake. C++20.

**Working tree:**
`~/dev/refactor-engine-merger/libkalburator/` (worktree on
`refactor/engine-merger`).

**Build:** `build/` (legacy preset-less). Use `-j 12`. Never
`--parallel`.

**Reference design:**
`docs/phase0/04o-phase-f0-test-gaps-design.md`.

**Verify after each task that touches code:**
`cmake --build build -j 12` and
`cd build && ctest --output-on-failure`.
After the test lands: `bash ~/dev/refactor-engine-merger/scripts/verify-all.sh`.

---

## Pre-flight: confirm production interfaces

Before Task 1, briefly confirm shapes haven't drifted:

- `src/calendar/mockbackend.h` — confirm `FailurePoint` enum still
  has `OnFetch`, `OnStoreItems`, `OnDelete`. Confirm
  `setFailurePoint(FailurePoint, …)` and `clearFailurePoint()`.
- `src/types/synctypes.h` — confirm `SyncResult` carries
  `bool success` and `QString errorMessage`.
- `src/calendar/synccoordinator.h` — confirm
  `void allSyncsCompleted(const SyncResult& aggregateResult)`
  signal exists.
- `tests/calendar/tst_calendar_sync_full.cpp` — model fixture
  for the new test (init/cleanup pattern, `runOneSync` helper).
- `tests/calendar/stubs/stubsynchost.h` — confirm
  `StubSyncHost(BackendRegistry*)` ctor and `stubCollection()`
  accessor still match what `tst_calendar_sync_full` uses.
- `tests/calendar/CMakeLists.txt` — confirm
  `kalburator_add_calendar_integration_test()` helper is the
  way new integration tests are registered.

If any production shape has drifted from the design, **stop**
and fix `04o-phase-f0-test-gaps-design.md` first.

---

## Task 1: Write `tst_calendar_sync_error_recovery.cpp`

The new test. Follows `tst_calendar_sync_full`'s fixture verbatim
where possible — same stubs, same registry setup, same
`runOneSync` helper. The only differences are the `FailurePoint`
injection and the assertions on the captured `SyncResult`.

**Files:**
- Create: `tests/calendar/tst_calendar_sync_error_recovery.cpp`

- [ ] **Step 1: Write the file**

Skeleton, including the `runOneSync` variant that captures the
`SyncResult` from `allSyncsCompleted`:

```cpp
// tst_calendar_sync_error_recovery.cpp
//
// Phase F.0 — Backend write/fetch failure paths against
// MockBackend. Pins SyncResult.success == false and
// errorMessage non-empty whenever a backend's setFailurePoint
// triggers on the engine main path. Library-side counterpart of
// PlanStan's tst_sync_error_recovery.cpp.
//
// See: docs/phase0/04o-phase-f0-test-gaps-design.md

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>

#include <KCalendarCore/Event>
#include <KCalendarCore/MemoryCalendar>

#include "backendregistry.h"
#include "calendarbaselinestore.h"
#include "conflictmanager.h"
#include "mockbackend.h"
#include "synccoordinator.h"
#include "syncconflictstore.h"
#include "synctypes.h"

#include "stubs/stubsynchost.h"

using namespace Kalburator::Sync;
using namespace Kalburator::Sync::Test;

namespace {

constexpr auto kSourceBackendId = "source-mock";
constexpr auto kTargetBackendId = "target-mock";
constexpr auto kCollectionId    = "stub-collection";
constexpr auto kCalendarId      = "calendar-1";
constexpr auto kMappingId       = "mapping-1";

constexpr int kSyncTimeoutMs = 5000;

KCalendarCore::Event::Ptr makeEvent(const QString& uid, const QString& summary)
{
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    event->setUid(uid);
    event->setSummary(summary);
    event->setDtStart(QDateTime::currentDateTimeUtc());
    return event;
}

SyncMapping makeTwoWayMapping()
{
    SyncMapping m;
    m.id              = QString::fromLatin1(kMappingId);
    m.sourceBackend   = QString::fromLatin1(kSourceBackendId);
    m.sourceCalendar  = QString::fromLatin1(kCalendarId);
    m.targetBackend   = QString::fromLatin1(kTargetBackendId);
    m.targetCalendar  = QString::fromLatin1(kCalendarId);
    m.mode            = SyncMode::TwoWay;
    m.conflictPolicy  = ConflictResolution::LastWriteWins;
    m.enabled         = true;
    return m;
}

} // namespace

class TestCalendarSyncErrorRecovery : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() {}
    void cleanupTestCase() {}
    void init();
    void cleanup();

    void targetStoreItemsFailure_propagatesAsSyncResultFailure();
    void targetUpdateItemFailure_propagatesAsSyncResultFailure();
    void targetDeleteFailure_propagatesAsSyncResultFailure();
    void sourceFetchFailure_propagatesAsSyncResultFailure();
    void targetFetchFailure_propagatesAsSyncResultFailure();

private:
    /// Run a sync to completion. Returns true on success, false on
    /// timeout. The aggregate SyncResult is captured into
    /// m_lastResult for assertions.
    bool runOneSync();

    QStringList sourceUids() const
    { return m_source->allUids(QString::fromLatin1(kCalendarId)); }
    QStringList targetUids() const
    { return m_target->allUids(QString::fromLatin1(kCalendarId)); }

    std::unique_ptr<QTemporaryDir>         m_tmpDir;
    std::unique_ptr<BackendRegistry>       m_registry;
    std::unique_ptr<MockBackend>           m_source;
    std::unique_ptr<MockBackend>           m_target;
    std::unique_ptr<StubSyncHost>          m_host;
    std::unique_ptr<CalendarBaselineStore> m_calendarBaselines;
    std::unique_ptr<SyncConflictStore>     m_conflictStore;
    std::unique_ptr<ConflictManager>       m_conflictManager;
    std::unique_ptr<SyncCoordinator>       m_coordinator;

    SyncResult m_lastResult;
};

// ---- Lifecycle ------------------------------------------------------------

void TestCalendarSyncErrorRecovery::init()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());

    m_registry = std::make_unique<BackendRegistry>();
    m_source   = std::make_unique<MockBackend>();
    m_target   = std::make_unique<MockBackend>();
    m_registry->registerBackendInstance(QString::fromLatin1(kSourceBackendId),
                                        m_source.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kTargetBackendId),
                                        m_target.get());

    m_source->createCalendar(QString::fromLatin1(kCollectionId),
                             QString::fromLatin1(kCalendarId),
                             QStringLiteral("Calendar 1"));
    m_target->createCalendar(QString::fromLatin1(kCollectionId),
                             QString::fromLatin1(kCalendarId),
                             QStringLiteral("Calendar 1"));

    m_host = std::make_unique<StubSyncHost>(m_registry.get());

    auto* hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal->setId(QString::fromLatin1(kCalendarId));
    m_host->stubCollection()->addCalendarWithId(QString::fromLatin1(kCalendarId),
                                                 hostCal);

    const QString dbPath = m_tmpDir->filePath(QStringLiteral(".kalburator-sync.db"));
    m_calendarBaselines = std::make_unique<CalendarBaselineStore>(dbPath);
    m_conflictStore     = std::make_unique<SyncConflictStore>(dbPath);

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());

    m_coordinator = std::make_unique<SyncCoordinator>(m_registry.get(), m_host.get());
    m_coordinator->setCalendarBaselineStore(m_calendarBaselines.get());
    m_coordinator->setSyncConflictStore(m_conflictStore.get());
    m_coordinator->setConflictManager(m_conflictManager.get());
    m_coordinator->setCollection(m_host->stubCollection());
    m_coordinator->setSyncMappings({ makeTwoWayMapping() });

    m_lastResult = SyncResult{};
}

void TestCalendarSyncErrorRecovery::cleanup()
{
    m_coordinator.reset();
    m_conflictManager.reset();
    m_conflictStore.reset();
    m_calendarBaselines.reset();
    m_host.reset();
    m_target.reset();
    m_source.reset();
    m_registry.reset();
    m_tmpDir.reset();
}

bool TestCalendarSyncErrorRecovery::runOneSync()
{
    QSignalSpy allDoneSpy(m_coordinator.get(),
                          &SyncCoordinator::allSyncsCompleted);
    m_coordinator->runSync(SyncCoordinator::SyncBehavior::Unmonitored);
    if (!allDoneSpy.wait(kSyncTimeoutMs)) {
        return false;
    }
    // SyncResult is the first argument of the last emission.
    const auto args = allDoneSpy.takeLast();
    if (args.isEmpty()) {
        return false;
    }
    m_lastResult = args.first().value<SyncResult>();
    return true;
}

// ---- Tests ---------------------------------------------------------------

void TestCalendarSyncErrorRecovery::targetStoreItemsFailure_propagatesAsSyncResultFailure()
{
    // Source has a new event; target is empty. The sync will try to
    // create the event on the target via storeItems.
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-1"),
                                     QStringLiteral("Event One")));

    m_target->setFailurePoint(MockBackend::FailurePoint::OnStoreItems);

    QVERIFY(runOneSync());
    QVERIFY2(!m_lastResult.success,
             "Expected SyncResult.success == false on storeItems failure");
    QVERIFY2(!m_lastResult.errorMessage.isEmpty(),
             "Expected non-empty errorMessage on storeItems failure");
}

void TestCalendarSyncErrorRecovery::targetUpdateItemFailure_propagatesAsSyncResultFailure()
{
    // Both sides have the event with same uid; target has the older
    // copy. The sync will issue an updateItem on target.
    auto eventA = makeEvent(QStringLiteral("evt-1"), QStringLiteral("New Summary"));
    auto eventB = makeEvent(QStringLiteral("evt-1"), QStringLiteral("Old Summary"));
    m_source->addIncidence(QString::fromLatin1(kCalendarId), eventA);
    m_target->addIncidence(QString::fromLatin1(kCalendarId), eventB);

    // Seed a baseline so the engine takes the diff/merge path rather
    // than the first-sync quick path.
    m_calendarBaselines->setBaseline(
        QString::fromLatin1(kMappingId),
        QString::fromLatin1(kCalendarId),
        eventB->uid(),
        QStringLiteral("dummy-baseline"));

    m_target->setFailurePoint(MockBackend::FailurePoint::OnStoreItems);

    QVERIFY(runOneSync());
    QVERIFY(!m_lastResult.success);
    QVERIFY(!m_lastResult.errorMessage.isEmpty());
}

void TestCalendarSyncErrorRecovery::targetDeleteFailure_propagatesAsSyncResultFailure()
{
    // Target has an event the source no longer has; sync will issue
    // a delete on target. (Requires a baseline so the engine knows
    // it's a delete, not a "first-sync, mirror everything" case.)
    auto stale = makeEvent(QStringLiteral("evt-stale"),
                           QStringLiteral("To Be Deleted"));
    m_target->addIncidence(QString::fromLatin1(kCalendarId), stale);

    m_calendarBaselines->setBaseline(
        QString::fromLatin1(kMappingId),
        QString::fromLatin1(kCalendarId),
        stale->uid(),
        QStringLiteral("dummy-baseline"));

    m_target->setFailurePoint(MockBackend::FailurePoint::OnDelete);

    QVERIFY(runOneSync());
    QVERIFY(!m_lastResult.success);
    QVERIFY(!m_lastResult.errorMessage.isEmpty());
}

void TestCalendarSyncErrorRecovery::sourceFetchFailure_propagatesAsSyncResultFailure()
{
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-1"),
                                     QStringLiteral("Event One")));

    m_source->setFailurePoint(MockBackend::FailurePoint::OnFetch);

    QVERIFY(runOneSync());
    QVERIFY(!m_lastResult.success);
    QVERIFY(!m_lastResult.errorMessage.isEmpty());
}

void TestCalendarSyncErrorRecovery::targetFetchFailure_propagatesAsSyncResultFailure()
{
    m_target->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-1"),
                                     QStringLiteral("Event One")));

    m_target->setFailurePoint(MockBackend::FailurePoint::OnFetch);

    QVERIFY(runOneSync());
    QVERIFY(!m_lastResult.success);
    QVERIFY(!m_lastResult.errorMessage.isEmpty());
}

QTEST_GUILESS_MAIN(TestCalendarSyncErrorRecovery)
#include "tst_calendar_sync_error_recovery.moc"
```

If the implementer hits compile or runtime issues:

- `SyncResult` may not be `Q_DECLARE_METATYPE`'d. The
  `allSyncsCompleted` signal carries it via Qt's queued mechanism
  if cross-thread; check whether it's already registered (it is in
  practice — `SyncCoordinator` emits it). If `args.first().value<SyncResult>()`
  returns a default-constructed result, fall back to subscribing to
  `syncCompleted(const QString&, const SyncResult&)` which fires
  per-mapping just before the aggregate.
- `setBaseline` signature: confirm it's
  `setBaseline(mappingId, calendarId, uid, hash)` (per
  `calendarbaselinestore.h`); adjust if argument order has drifted.
- `SyncBehavior::Unmonitored` is the no-conflict-pause path used by
  `tst_calendar_sync_full`. Use it consistently here.
- `MockBackend` may need explicit `loadCalendars` priming for the
  source-fetch test if `OnFetch` only triggers on the modified-since
  path. If so, the test still asserts the same outcome — but check
  by running it.

- [ ] **Step 2: Build the new test target**

```bash
cmake --build build -j 12 --target tst_calendar_sync_error_recovery
```

If a vtable error appears (a `Q_OBJECT` class added via globbed
sources — though the test is in an explicit list, the autogen
timestamp may still want a kick), wipe and retry:

```bash
rm build/kalburator_autogen/timestamp
cmake --build build -j 12 --target tst_calendar_sync_error_recovery
```

- [ ] **Step 3: Run the test in isolation**

```bash
cd build && ctest --output-on-failure -R tst_calendar_sync_error_recovery -V
```

Expected: 5/5 PASS.

If a test fails because the `MockBackend` failure injection doesn't
trigger on the path the test exercises (e.g., the engine takes a
quick-path that bypasses `storeItems`), debug by:

1. Adding `qDebug` in the test to print `m_lastResult.errorMessage`
   and `m_lastResult.targetStats`/`sourceStats` to see what actually
   happened.
2. Cross-referencing with FINDINGS entries about the quick-path
   downgrade and `dispatchFirstSync` guard. The test seeds a
   baseline for `targetUpdateItemFailure` and `targetDeleteFailure`
   to bypass the quick path; if `OnStoreItems` isn't firing, check
   whether the baseline is in fact bypassing the
   first-sync-via-blob-engine route.

- [ ] **Step 4: Run the full calendar suite**

```bash
cd build && ctest --output-on-failure -R "calendar"
```

Expected: 21/21 pass (was 20; +1 new test).

---

## Task 2: Wire the new test into CMake

**Files:**
- Modify: `tests/calendar/CMakeLists.txt`

- [ ] **Step 1: Add the test registration**

Append after the existing `kalburator_add_calendar_integration_test`
calls (i.e., after `tst_calendar_subsequent_sync_uses_blob_view`):

```cmake
# Phase F.0 — Backend write/fetch failure paths
kalburator_add_calendar_integration_test(tst_calendar_sync_error_recovery)
```

- [ ] **Step 2: Reconfigure and build the full library**

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j 12
```

- [ ] **Step 3: Run the full library ctest**

```bash
cd build && ctest --output-on-failure
```

Expected: 21/21 pass.

- [ ] **Step 4: Commit**

```bash
git add tests/calendar/tst_calendar_sync_error_recovery.cpp \
        tests/calendar/CMakeLists.txt
git commit -m "test(calendar): library-side error-recovery test (Phase F.0)

Adds tst_calendar_sync_error_recovery covering the five
backend-failure paths (OnStoreItems × create/update; OnDelete;
OnFetch × source/target). Pins SyncResult.success == false on
the engine boundary, the contract Phase E nearly broke and
PlanStan's tst_sync_error_recovery happened to catch.

Phase F.0 — closes the audit-identified pre-Phase-F gap so the
library no longer depends on PlanStan as the sole defender of
this contract."
```

---

## Task 3: Verify-all + baseline refresh

- [ ] **Step 1: Run verify-all from the coordination folder**

```bash
bash ~/dev/refactor-engine-merger/scripts/verify-all.sh
```

Expected: exit 3 (test improvement — libkalburator went 20 → 21).

- [ ] **Step 2: Refresh libkalburator baseline**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
ctest --test-dir build --output-on-failure 2>&1 | \
    tee ~/dev/refactor-engine-merger/baselines/libkalburator-worktree-ctest.txt
```

Verify the new file lists 21 tests and all pass.

- [ ] **Step 3: Re-run verify-all**

```bash
bash ~/dev/refactor-engine-merger/scripts/verify-all.sh
```

Expected: exit 0.

---

## Task 4: Append FINDINGS triage entry

The 24 PlanStan "failures" decompose entirely into noise (per
`04o-phase-f0-test-gaps-design.md` triage table). Record the
breakdown in FINDINGS so future agents don't re-derive it.

**Files:**
- Modify: `~/dev/refactor-engine-merger/FINDINGS.md`

- [ ] **Step 1: Append the entry**

Add at the bottom of FINDINGS, with the standard format:

```markdown
### The 24 PlanStan baseline "failures" are noise (Phase F.0 triage)

**Date:** 2026-04-29
**Source:** Phase F.0 triage of `baselines/planstan-worktree-ctest.txt`
   (commit landing v0.12-phase-f0-test-gaps).

**What:** `verify-all.sh` reports PlanStan as 96/120 — 24 "failures"
since baseline. Triage shows zero of them are real:

- **22 / 24 — "Not Run".** Test executables not built. Two
  subgroups:
  - **13 graph-layout tests** (`tst_graphscene`, `tst_groups`,
    `tst_edgepathstrategies`, `tst_terminus`, `tst_graphedgeitem`,
    `tst_tools`, `tst_circular`, `tst_sugiyama`, `tst_spatialgrid`,
    `tst_quadtree`, `tst_forcelayout`, `tst_batchrenderer`,
    `tst_integration`) — PlanStan-internal graph subsystem,
    EXCLUDE_FROM_ALL or flag-gated. Unrelated to sync.
  - **9 integration_* tests** (`integration_recurrence_editing`,
    `_template_system`, `_incidence_reschedule`,
    `_collection_switching`, `_calendarcrud`, `_incidencecrud`,
    `_app_workflow`, `_collection_lifecycle`, `_incidence_crud`)
    — same EXCLUDE_FROM_ALL pattern. Pre-date refactor.
- **2 / 24 — actually "Failed".**
  - `tst_inboxmanager` — PlanStan-internal inbox feature; non-sync.
  - `sync_workflow_caldav` — environmental. The CalDAV server
    needs a specific Radicale + user-account setup; the QWARN
    trail shows D-Bus registration failures + HTTP 412 from a
    misconfigured server. Test logic is fine; setup is wrong.

**Why it matters:** Future agents reading
`baselines/planstan-worktree-ctest.txt` should not assume the 24
hide engine bugs. They are noise. If the count drops below 24 (a
test that was Not Run becomes Run, or a Failed test passes), that
*is* a signal — investigate. If the count goes above 24, also a
signal.

**Action:** No code changes. The triage is captured here so it
doesn't need re-deriving. If the scope of verify-all's PlanStan
build ever expands (e.g. to build the integration_* targets), the
22 "Not Run" entries will resolve and the breakdown above will
need updating.
```

- [ ] **Step 2: No commit**

CLAUDE.md notes the coordination folder is not a git repo;
`FINDINGS.md` is not version-controlled per-commit. Just save.

---

## Task 5: Update CURRENT-STATUS, ROADMAP, and phase-doc Status

**Files:**
- Modify: `~/dev/refactor-engine-merger/CURRENT-STATUS.md`
- Modify: `libkalburator/docs/phase0/04k-engine-merger-roadmap.md`
- Modify: `libkalburator/docs/phase0/04o-phase-f0-test-gaps-design.md`
  (Status line)
- Modify: `libkalburator/docs/phase0/04o-phase-f0-test-gaps-plan.md`
  (Status header — add post-landing)

- [ ] **Step 1: CURRENT-STATUS.md**

- Bump the date.
- Move the upcoming Phase F into the new "Where we are" section
  with Phase F.0 listed as ✅ landed.
- Replace "Next" with "Phase F1 — Unify (engine collapse +
  IDomainAdapter, preserving threading)".
- Append to "Recently committed (libkalburator)":
  `<sha> test(calendar): library-side error-recovery test (Phase F.0)`.
- Update test posture: libkalburator 20/20 → 21/21.

- [ ] **Step 2: ROADMAP table**

Edit the at-a-glance status table in
`04k-engine-merger-roadmap.md`. Add a Phase F.0 row above F, and
split the current F row into F1 and F2 (per brainstorm decision):

| Phase | Status | Tag |
|---|---|---|
| D.0 | ✅ landed 2026-04-28 | `v0.9-phase-d0-tests-first` |
| D | ✅ landed 2026-04-29 | `v0.10-phase-d-compose` |
| E | ✅ landed 2026-04-29 | `v0.11-phase-e-transcoding-backends` |
| F.0 — Test gap closure | ✅ landed YYYY-MM-DD | `v0.12-phase-f0-test-gaps` |
| F1 — Unify (engine + adapter) | ⬜ not started | `v0.13-phase-f1-unify` |
| F2 — Threading API redesign | ⬜ not started | `v0.14-phase-f2-threading` |
| G — Opaque + plugin | ⬜ not started | `v0.15-phase-g-opaque-plugin` |

Also update the "Phase tagging convention" section near the bottom
of the doc to reflect F.0/F1/F2/G's renumbering. Keep the existing
landed tags' references intact.

- [ ] **Step 3: Phase-doc Status lines**

`04o-phase-f0-test-gaps-design.md`:

```
**Status:** Landed YYYY-MM-DD on tag `v0.12-phase-f0-test-gaps`
(libkalburator HEAD `<short-sha>`). Approved 2026-04-29 by user
via brainstorming session.
```

`04o-phase-f0-test-gaps-plan.md`: add a Status header at the top:

```
**Status:** Landed YYYY-MM-DD on tag `v0.12-phase-f0-test-gaps`
(libkalburator HEAD `<short-sha>`). All tasks complete;
libkalburator 21/21, PlanStan 96/120 (matches Phase E baseline),
WildPalms 73/73 at tag.
```

- [ ] **Step 4: Commit doc updates**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git add docs/phase0/04o-phase-f0-test-gaps-design.md \
        docs/phase0/04o-phase-f0-test-gaps-plan.md \
        docs/phase0/04k-engine-merger-roadmap.md
git commit -m "docs(phase0): mark Phase F.0 landed; renumber F→F1/F2 in roadmap

Phase F.0 closes the pre-Phase-F test gap (library-side
error-recovery). Roadmap table now reflects the brainstorm decision
to slice F into F1 (engine collapse + adapter, preserving
threading) and F2 (threading API redesign). Tag plan: F.0=v0.12,
F1=v0.13, F2=v0.14, G=v0.15."
```

CURRENT-STATUS.md and FINDINGS.md live in the coordination folder
which is not a git repo per CLAUDE.md. Save and move on.

---

## Task 6: Tag

Per CLAUDE.md, the user runs destructive operations including
`git tag` unless explicitly authorized. **Do not tag autonomously.**
After Task 5, report the libkalburator HEAD sha to the user with:

```
Phase F.0 ready to tag. Recommended:
  cd ~/dev/refactor-engine-merger/libkalburator
  git tag v0.12-phase-f0-test-gaps <head-sha>
```

The user runs the tag command. After the tag is in place, return
to Task 5 Step 3 and replace `<short-sha>` placeholders with the
actual sha.

---

## Self-review checklist (run by plan executor before declaring done)

- [ ] `tst_calendar_sync_error_recovery` exists and is wired into
  `tests/calendar/CMakeLists.txt`.
- [ ] libkalburator standalone ctest: 21/21 pass.
- [ ] All five test methods in the new test pass.
- [ ] PlanStan: 96/120 pass (Phase E baseline; F.0 doesn't touch
  PlanStan).
- [ ] WildPalms: 73/73 pass.
- [ ] `verify-all.sh` exit 0 on a stable run after baseline refresh.
- [ ] `04k-engine-merger-roadmap.md` table reflects F.0 / F1 / F2.
- [ ] `04o-phase-f0-test-gaps-design.md` Status line reflects the
  tag (after user runs the tag command).
- [ ] `04o-phase-f0-test-gaps-plan.md` Status header reflects the
  tag.
- [ ] `CURRENT-STATUS.md` updated.
- [ ] `FINDINGS.md` triage entry appended.
- [ ] No new TODOs / FIXMEs left in code.
