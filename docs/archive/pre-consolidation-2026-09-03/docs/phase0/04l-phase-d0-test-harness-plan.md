# Phase D.0 Test Harness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add 4 stub-`ISyncHost` integration test executables to libkalburator's `tests/calendar/`, growing ctest from 5 to 9 executables. Pin the calendar engine's behavior before any structural refactor in Phases D / E / F / G.

**Architecture:** Four reusable stubs (`StubSyncHost`, `StubCalendarCollection`, `StubIncidenceRegistry`, `StubSyncConfigStore`) compile into a small static library `kalburator_calendar_test_stubs`. Each test executable is QTEST_MAIN, links the stubs lib + `Kalburator::Sync` + `KF6::CalendarCore`, and drives `SyncCoordinator`/`SyncWorker` through the existing `MockBackend` (`src/calendar/mockbackend.h`).

**Tech Stack:** Qt6, KCalendarCore (KF6), QTest, CMake. C++20.

**Working tree:** `~/dev/refactor-engine-merger/libkalburator/` (worktree on `refactor/engine-merger`).

**Build:** legacy preset-less project — build dir is `build/`. Use `-j 12`, never `--parallel`.

**Reference design:** `docs/phase0/04l-phase-d0-test-harness-design.md`.

---

## Pre-flight: confirm production interfaces

Before Task 1, briefly read these to confirm shapes haven't drifted from what the design assumes:
- `src/calendar/isynchost.h` — 9 pure virtuals
- `src/types/icalendarcollection.h` — 6 methods
- `src/calendar/iincidenceregistry.h` — confirm shape
- `src/types/isyncconfigstore.h` — confirm shape
- `src/calendar/syncworker.h:42–195` — `Mode`, `ConflictResolution`,
  signals, `resumeAfterConflict`
- `src/calendar/mockbackend.h` — already-existing mock
- `~/dev/refactor-engine-merger/PlanStan/tests/sync-workflow/tst_sync_error_recovery.cpp:64` — reference stub pattern

If any production shape has drifted from the design, **stop** and fix the design doc (`04l-…-design.md`) first.

---

### Task 1: stubs library skeleton + CMake plumbing

**Files:**
- Create: `tests/calendar/stubs/CMakeLists.txt`
- Create: `tests/calendar/stubs/.gitkeep` (placeholder so the dir commits cleanly)
- Modify: `tests/calendar/CMakeLists.txt`

- [ ] **Step 1: Create stubs/CMakeLists.txt**

```cmake
# tests/calendar/stubs/CMakeLists.txt
# Reusable stubs for libkalburator's calendar-layer integration tests.
# Phase D.0 — added 2026-04-28; pinned by Phases D/E/F/G tests.

add_library(kalburator_calendar_test_stubs STATIC
    stubsynchost.cpp
    stubcalendarcollection.cpp
    stubincidenceregistry.cpp
    stubsyncconfigstore.cpp
)
set_target_properties(kalburator_calendar_test_stubs PROPERTIES
    AUTOMOC ON
    POSITION_INDEPENDENT_CODE ON
)
target_link_libraries(kalburator_calendar_test_stubs
    PUBLIC
        Qt6::Core
        KF6::CalendarCore
        Kalburator::Sync
)
target_include_directories(kalburator_calendar_test_stubs
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
```

- [ ] **Step 2: Extend tests/calendar/CMakeLists.txt**

Append the following at the bottom (preserving the existing `kalburator_add_calendar_test(tst_icsfeedfetcher)` line):

```cmake
# Phase D.0 — integration tests against MockBackend through stub host.
add_subdirectory(stubs)

function(kalburator_add_calendar_integration_test TEST_NAME)
    kalburator_add_calendar_test(${TEST_NAME})
    target_link_libraries(${TEST_NAME}
        PRIVATE kalburator_calendar_test_stubs)
endfunction()
```

(The four `kalburator_add_calendar_integration_test(...)` lines are added one per test in Tasks 6–9 as each test lands. Don't add them all upfront — that breaks the build until each .cpp exists.)

- [ ] **Step 3: Verify configure picks it up but doesn't try to build yet**

`cmake --build` will fail at this point because the .cpp files don't exist yet. That's expected. Verify the configure step succeeds:

```bash
cmake -S ~/dev/refactor-engine-merger/libkalburator \
      -B ~/dev/refactor-engine-merger/libkalburator/build \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

Expected: configure succeeds, no errors about the new subdirectory. `cmake --build` would fail with "no rule to make stubsynchost.cpp" — that's fine, we haven't written it yet. Move on.

- [ ] **Step 4: Don't commit yet** — wait until Task 5 when stubs exist and the lib actually compiles. Build won't be green between Tasks 1–5.

---

### Task 2: StubCalendarCollection

**Files:**
- Create: `tests/calendar/stubs/stubcalendarcollection.h`
- Create: `tests/calendar/stubs/stubcalendarcollection.cpp`

This is the smallest of the 4 stubs and a good warm-up.

- [ ] **Step 1: Read the production interface**

```bash
cat ~/dev/refactor-engine-merger/libkalburator/src/types/icalendarcollection.h
```

Confirm the 6 methods:
- `id() → QString`
- `calendar(QString id) → MemoryCalendar*`
- `calendars() → QList<MemoryCalendar*>`
- `addCalendar(MemoryCalendar *)`
- `setCalendarColor(QString id, QColor)`
- `setCalendarVisible(QString id, bool)`

- [ ] **Step 2: Write the header**

```cpp
// tests/calendar/stubs/stubcalendarcollection.h
#ifndef KALBURATOR_TEST_STUBCALENDARCOLLECTION_H
#define KALBURATOR_TEST_STUBCALENDARCOLLECTION_H

#include <QColor>
#include <QHash>
#include <QList>
#include <QString>

#include <KCalendarCore/MemoryCalendar>

#include "icalendarcollection.h"

namespace Kalburator::Sync::Test {

/**
 * @brief In-memory ICalendarCollection for libkalburator integration tests.
 *
 * Owns the MemoryCalendar pointers it receives via addCalendar().
 * Property setters (color, visible) record into inspectable hashes.
 */
class StubCalendarCollection : public ICalendarCollection
{
public:
    explicit StubCalendarCollection(QString id = QStringLiteral("stub-collection"));
    ~StubCalendarCollection() override;

    // ICalendarCollection
    QString id() const override { return m_id; }
    KCalendarCore::MemoryCalendar* calendar(const QString &calendarId) override;
    QList<KCalendarCore::MemoryCalendar*> calendars() override;
    void addCalendar(KCalendarCore::MemoryCalendar *cal) override;
    void setCalendarColor(const QString &calendarId, const QColor &color) override;
    void setCalendarVisible(const QString &calendarId, bool visible) override;

    // Inspection
    QColor recordedColor(const QString &calendarId) const { return m_colors.value(calendarId); }
    bool   recordedVisible(const QString &calendarId) const { return m_visibles.value(calendarId, true); }

private:
    QString m_id;
    QHash<QString, KCalendarCore::MemoryCalendar*> m_calendars; // owns the pointers
    QHash<QString, QColor> m_colors;
    QHash<QString, bool>   m_visibles;
};

} // namespace Kalburator::Sync::Test

#endif
```

- [ ] **Step 3: Write the implementation**

```cpp
// tests/calendar/stubs/stubcalendarcollection.cpp
#include "stubcalendarcollection.h"

namespace Kalburator::Sync::Test {

StubCalendarCollection::StubCalendarCollection(QString id)
    : m_id(std::move(id))
{
}

StubCalendarCollection::~StubCalendarCollection()
{
    qDeleteAll(m_calendars);
}

KCalendarCore::MemoryCalendar* StubCalendarCollection::calendar(const QString &calendarId)
{
    return m_calendars.value(calendarId, nullptr);
}

QList<KCalendarCore::MemoryCalendar*> StubCalendarCollection::calendars()
{
    return m_calendars.values();
}

void StubCalendarCollection::addCalendar(KCalendarCore::MemoryCalendar *cal)
{
    if (!cal) return;
    // ICalendarCollection contract: collection identifies the calendar by
    // an id property. KCalendarCore::Calendar lacks a setId; tests pass
    // an id-tagged calendar by setting productId() or by relying on
    // the calendar's pointer identity. For now, key by productId() if
    // present, else by pointer-stringification.
    QString key = cal->productId();
    if (key.isEmpty()) {
        key = QStringLiteral("cal-%1").arg(reinterpret_cast<quintptr>(cal), 0, 16);
    }
    m_calendars.insert(key, cal);
}

void StubCalendarCollection::setCalendarColor(const QString &calendarId, const QColor &color)
{
    m_colors[calendarId] = color;
}

void StubCalendarCollection::setCalendarVisible(const QString &calendarId, bool visible)
{
    m_visibles[calendarId] = visible;
}

} // namespace Kalburator::Sync::Test
```

- [ ] **Step 4: Verify it compiles**

```bash
cmake --build ~/dev/refactor-engine-merger/libkalburator/build -j 12 \
    --target kalburator_calendar_test_stubs 2>&1 | tail -10
```

Expected: at least the StubCalendarCollection part links. Other stubs' .cpp files are still missing → expect "No rule to make target stubsynchost.cpp" etc. That's fine; we'll add them in Tasks 3–5.

(If `addCalendar()`'s key-derivation strategy turns out wrong when tests start exercising it, revisit. Keying by `productId()` is a guess based on the production interface; the tests will tell us.)

---

### Task 3: StubIncidenceRegistry

**Files:**
- Create: `tests/calendar/stubs/stubincidenceregistry.h`
- Create: `tests/calendar/stubs/stubincidenceregistry.cpp`

- [ ] **Step 1: Read the production interface**

```bash
cat ~/dev/refactor-engine-merger/libkalburator/src/calendar/iincidenceregistry.h
```

Confirm the methods. Implementation at this stage targets these — adapt the stub if the actual interface differs.

- [ ] **Step 2: Write the header — minimal viable shape**

```cpp
// tests/calendar/stubs/stubincidenceregistry.h
#ifndef KALBURATOR_TEST_STUBINCIDENCEREGISTRY_H
#define KALBURATOR_TEST_STUBINCIDENCEREGISTRY_H

#include <QHash>
#include <KCalendarCore/Incidence>
#include "iincidenceregistry.h"

namespace Kalburator::Sync::Test {

class StubIncidenceRegistry : public IIncidenceRegistry
{
public:
    StubIncidenceRegistry() = default;
    ~StubIncidenceRegistry() override = default;

    // Implement each pure virtual from IIncidenceRegistry. Methods that
    // look up by UID consult m_incidences. Methods that don't apply
    // return a sensible default (empty/false/nullptr).
    // [Concrete method bodies authored after reading iincidenceregistry.h.]

    // Test setup
    void registerIncidence(const QString &uid, const KCalendarCore::Incidence::Ptr &inc) {
        m_incidences.insert(uid, inc);
    }
    void clear() { m_incidences.clear(); }

private:
    QHash<QString, KCalendarCore::Incidence::Ptr> m_incidences;
};

} // namespace Kalburator::Sync::Test

#endif
```

- [ ] **Step 3: Read the actual interface and fill in the bodies**

The bracketed comment in the header is intentional — `IIncidenceRegistry`'s exact pure-virtual list will dictate what to override. Read the header, mirror each method, and back it with `m_incidences` where the method involves UID lookup. For methods like "register a new incidence from sync" or "iterate all incidences," delegate to `m_incidences`. For host-shaped methods that don't apply in tests, return defaults.

- [ ] **Step 4: Verify it compiles**

```bash
cmake --build ~/dev/refactor-engine-merger/libkalburator/build -j 12 \
    --target kalburator_calendar_test_stubs 2>&1 | tail -5
```

---

### Task 4: StubSyncConfigStore

**Files:**
- Create: `tests/calendar/stubs/stubsyncconfigstore.h`
- Create: `tests/calendar/stubs/stubsyncconfigstore.cpp`

- [ ] **Step 1: Read the interface**

```bash
cat ~/dev/refactor-engine-merger/libkalburator/src/types/isyncconfigstore.h
```

- [ ] **Step 2: Write a minimal stub**

```cpp
// tests/calendar/stubs/stubsyncconfigstore.h
#ifndef KALBURATOR_TEST_STUBSYNCCONFIGSTORE_H
#define KALBURATOR_TEST_STUBSYNCCONFIGSTORE_H

#include <QList>
#include "isyncconfigstore.h"
// SyncMapping or equivalent — included transitively via isyncconfigstore.h

namespace Kalburator::Sync::Test {

class StubSyncConfigStore : public ISyncConfigStore
{
public:
    StubSyncConfigStore() = default;
    ~StubSyncConfigStore() override = default;

    // Implement each pure virtual from ISyncConfigStore. Tests typically
    // need to inject pre-built SyncMapping objects and read them back.
    // [Concrete method bodies authored after reading isyncconfigstore.h.]

    // Test setup
    void setMappings(QList<SyncMapping> mappings) { m_mappings = std::move(mappings); }

private:
    QList<SyncMapping> m_mappings;
};

} // namespace Kalburator::Sync::Test

#endif
```

Implementation file mirrors the header — fill in each pure virtual after reading the production interface. Most return data from `m_mappings`; mutators (e.g. `addMapping`, `removeMapping`) push/erase into it.

- [ ] **Step 3: Compile-check**

```bash
cmake --build ~/dev/refactor-engine-merger/libkalburator/build -j 12 \
    --target kalburator_calendar_test_stubs 2>&1 | tail -5
```

---

### Task 5: StubSyncHost (and commit the whole stubs lib)

**Files:**
- Create: `tests/calendar/stubs/stubsynchost.h`
- Create: `tests/calendar/stubs/stubsynchost.cpp`
- Reference: `~/dev/refactor-engine-merger/PlanStan/tests/sync-workflow/tst_sync_error_recovery.cpp:64-96`

- [ ] **Step 1: Lift PlanStan's pattern as the starting point**

PlanStan's stub delegates to a `BackendRegistry*` for backendById/backends. We do the same, but additionally hold pointers to the other three stubs.

- [ ] **Step 2: Write the header**

```cpp
// tests/calendar/stubs/stubsynchost.h
#ifndef KALBURATOR_TEST_STUBSYNCHOST_H
#define KALBURATOR_TEST_STUBSYNCHOST_H

#include <QHash>
#include <QList>
#include <QString>

#include <KCalendarCore/Incidence>

#include "isynchost.h"
#include "stubcalendarcollection.h"
#include "stubincidenceregistry.h"
#include "stubsyncconfigstore.h"

namespace Kalburator::Sync {
class BackendRegistry;
class SyncBackend;
class IIncidenceSource;
}

namespace Kalburator::Sync::Test {

/**
 * @brief Lifts PlanStan's StubSyncHost pattern, completes the API.
 *
 * Owns the four sub-stubs but does NOT own the BackendRegistry or
 * IIncidenceSource (caller manages those). Records every
 * applyIncidence* call into an inspectable log.
 */
class StubSyncHost : public ISyncHost
{
public:
    struct AppliedChange {
        enum class Kind { Add, Remove, Update };
        Kind kind;
        QString calendarId;
        QString uid;             // for Remove
        KCalendarCore::Incidence::Ptr incidence; // for Add/Update
        bool stageForSync;
    };

    StubSyncHost(BackendRegistry *registry,
                 IIncidenceSource *source = nullptr);
    ~StubSyncHost() override;

    // ISyncHost
    SyncBackend* backendById(const QString &id) override;
    QHash<QString, SyncBackend*> backends() override;

    bool applyIncidenceAddition(const QString &calendarId,
                                const KCalendarCore::Incidence::Ptr &inc,
                                bool stageForSync = true) override;
    bool applyIncidenceRemoval(const QString &calendarId,
                               const QString &uid,
                               bool stageForSync = true,
                               const QDateTime &recurrenceId = {}) override;
    bool applyIncidenceUpdate(const QString &calendarId,
                              const KCalendarCore::Incidence::Ptr &inc,
                              bool stageForSync = true) override;

    ICalendarCollection* collection() override { return m_collection.get(); }
    IIncidenceSource*    incidenceSource() override { return m_source; }
    IIncidenceRegistry*  incidenceRegistry() override { return m_registry.get(); }
    ISyncConfigStore*    configStore() override { return m_config.get(); }

    void unloadCalendar(const QString &) override {}
    void generateSyncMappingsFromLogicalCalendars() override {}

    // Inspection
    QList<AppliedChange> appliedChanges() const { return m_appliedChanges; }
    int appliedAdditionCount() const;
    int appliedRemovalCount() const;
    int appliedUpdateCount() const;

    // Direct accessors for tests that need to seed state
    StubCalendarCollection* stubCollection() { return m_collection.get(); }
    StubIncidenceRegistry*  stubRegistry()   { return m_registry.get(); }
    StubSyncConfigStore*    stubConfig()     { return m_config.get(); }

private:
    BackendRegistry *m_backendRegistry;        // not owned
    IIncidenceSource *m_source;                // not owned

    std::unique_ptr<StubCalendarCollection> m_collection;
    std::unique_ptr<StubIncidenceRegistry>  m_registry;
    std::unique_ptr<StubSyncConfigStore>    m_config;

    QList<AppliedChange> m_appliedChanges;
};

} // namespace Kalburator::Sync::Test

#endif
```

- [ ] **Step 3: Write the implementation**

```cpp
// tests/calendar/stubs/stubsynchost.cpp
#include "stubsynchost.h"

#include "backendregistry.h"
// (other production headers as needed when filling backendById delegation)

namespace Kalburator::Sync::Test {

StubSyncHost::StubSyncHost(BackendRegistry *registry, IIncidenceSource *source)
    : m_backendRegistry(registry)
    , m_source(source)
    , m_collection(std::make_unique<StubCalendarCollection>())
    , m_registry(std::make_unique<StubIncidenceRegistry>())
    , m_config(std::make_unique<StubSyncConfigStore>())
{
}

StubSyncHost::~StubSyncHost() = default;

SyncBackend* StubSyncHost::backendById(const QString &id)
{
    return m_backendRegistry ? m_backendRegistry->backendInstance(id) : nullptr;
}

QHash<QString, SyncBackend*> StubSyncHost::backends()
{
    QHash<QString, SyncBackend*> result;
    if (!m_backendRegistry) return result;
    for (const QString &id : m_backendRegistry->registeredInstanceIds()) {
        result.insert(id, m_backendRegistry->backendInstance(id));
    }
    return result;
}

bool StubSyncHost::applyIncidenceAddition(const QString &calendarId,
                                          const KCalendarCore::Incidence::Ptr &inc,
                                          bool stageForSync)
{
    m_appliedChanges.append({AppliedChange::Kind::Add, calendarId, {}, inc, stageForSync});
    return true;
}

bool StubSyncHost::applyIncidenceRemoval(const QString &calendarId,
                                         const QString &uid,
                                         bool stageForSync,
                                         const QDateTime &)
{
    m_appliedChanges.append({AppliedChange::Kind::Remove, calendarId, uid, {}, stageForSync});
    return true;
}

bool StubSyncHost::applyIncidenceUpdate(const QString &calendarId,
                                        const KCalendarCore::Incidence::Ptr &inc,
                                        bool stageForSync)
{
    m_appliedChanges.append({AppliedChange::Kind::Update, calendarId, {}, inc, stageForSync});
    return true;
}

int StubSyncHost::appliedAdditionCount() const {
    return std::count_if(m_appliedChanges.cbegin(), m_appliedChanges.cend(),
        [](const AppliedChange &c) { return c.kind == AppliedChange::Kind::Add; });
}
int StubSyncHost::appliedRemovalCount() const {
    return std::count_if(m_appliedChanges.cbegin(), m_appliedChanges.cend(),
        [](const AppliedChange &c) { return c.kind == AppliedChange::Kind::Remove; });
}
int StubSyncHost::appliedUpdateCount() const {
    return std::count_if(m_appliedChanges.cbegin(), m_appliedChanges.cend(),
        [](const AppliedChange &c) { return c.kind == AppliedChange::Kind::Update; });
}

} // namespace Kalburator::Sync::Test
```

- [ ] **Step 4: Verify the stubs library compiles cleanly**

```bash
cmake --build ~/dev/refactor-engine-merger/libkalburator/build -j 12 \
    --target kalburator_calendar_test_stubs 2>&1 | tail -10
```

Expected: `[100%] Built target kalburator_calendar_test_stubs`. Any compile error means the production interface drifted from what we assumed; read the header and adjust.

- [ ] **Step 5: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator add tests/calendar/stubs tests/calendar/CMakeLists.txt
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "test(calendar): add StubSyncHost + 3 supporting stubs (Phase D.0)"
```

---

### Task 6: tst_calendar_sync_full

**Files:**
- Create: `tests/calendar/tst_calendar_sync_full.cpp`
- Modify: `tests/calendar/CMakeLists.txt` (add the integration_test line)

This task TDDs four scenarios as separate methods. Each method is its own commit-worthy unit, but the executable can be committed once after all four pass.

- [ ] **Step 1: Skeleton with one trivially-passing method**

Write the file with `initTestCase()`, `init()`, `cleanup()`, `cleanupTestCase()`, and a placeholder method that just `QVERIFY(true)`. Add the corresponding line to `tests/calendar/CMakeLists.txt`:

```cmake
kalburator_add_calendar_integration_test(tst_calendar_sync_full)
```

Build + run; verify the executable runs and the trivial method passes:

```bash
cmake --build ~/dev/refactor-engine-merger/libkalburator/build -j 12 --target tst_calendar_sync_full
ctest --test-dir ~/dev/refactor-engine-merger/libkalburator/build -R tst_calendar_sync_full --output-on-failure
```

Expected: 1/1 pass.

- [ ] **Step 2: Replace placeholder with `fullSync_bothEmpty_doesNothing`**

Set up: source MockBackend (empty), target MockBackend (empty), StubSyncHost with both registered, `SyncCoordinator` driving a single mapping. Run sync, await `syncCompleted` signal via `QSignalSpy`. Assert: zero `applyIncidence*` calls on the host, zero items in either backend after sync, baseline written to SyncStore.

Run the test. Either it passes (good) or it reveals a missing piece in the stubs (e.g., an interface returns nullptr where SyncWorker dereferences). Fix the stubs as gaps surface; this is the first real exercise.

- [ ] **Step 3: Add `fullSync_sourceHasEvents_propagatesToTarget`**

Source seeded with 3 `Incidence::Ptr` events (helper: `makeEvent(uid, summary)` static in this cpp). Target empty. After sync: target's `MockBackend::allUids("calendar-1").size() == 3`, source unchanged.

- [ ] **Step 4: Add `fullSync_targetHasEvents_propagatesToSource`**

Symmetric. Target seeded, source empty. After sync: source has 3 events, `StubSyncHost::appliedAdditionCount() == 3` (the engine pushed target→host through the host's `applyIncidenceAddition`).

- [ ] **Step 5: Add `fullSync_disjointEvents_bothConverge`**

Source has A; target has B. After sync: both backends have {A, B}. Tests that bidirectional propagation works.

- [ ] **Step 6: Run all four; commit**

```bash
ctest --test-dir ~/dev/refactor-engine-merger/libkalburator/build -R tst_calendar_sync_full --output-on-failure
```

Expected: 4/4 pass.

```bash
git -C ~/dev/refactor-engine-merger/libkalburator add tests/calendar/tst_calendar_sync_full.cpp tests/calendar/CMakeLists.txt
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "test(calendar): add tst_calendar_sync_full (4 methods, Phase D.0)"
```

---

### Task 7: tst_calendar_sync_oneway

**Files:**
- Create: `tests/calendar/tst_calendar_sync_oneway.cpp`
- Modify: `tests/calendar/CMakeLists.txt`

- [ ] **Step 1: Add the CMake line + skeleton + first test method**

`oneWayUpload_sourceToTarget`: same setup as `fullSync_sourceHasEvents_propagatesToTarget` but with `SyncMapping::mode = OneWayUpload`. Assert target gets source's events.

- [ ] **Step 2: Add `oneWayUpload_ignoresTargetOnlyEvents`**

Source empty, target has events. After sync: source remains empty (no pull), target unchanged.

- [ ] **Step 3: Run, commit**

```bash
ctest --test-dir ~/dev/refactor-engine-merger/libkalburator/build -R tst_calendar_sync_oneway --output-on-failure
```

```bash
git -C ~/dev/refactor-engine-merger/libkalburator add tests/calendar/tst_calendar_sync_oneway.cpp tests/calendar/CMakeLists.txt
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "test(calendar): add tst_calendar_sync_oneway (Phase D.0)"
```

---

### Task 8: tst_calendar_conflict (monitored + unmonitored)

**Files:**
- Create: `tests/calendar/tst_calendar_conflict.cpp`
- Modify: `tests/calendar/CMakeLists.txt`

- [ ] **Step 1: Add CMake + skeleton + `unmonitored_sameUidDivergent_emitsConflictDetected_appliesPolicy`**

Setup: Both backends have event "evt-1" in baseline, then both modified the summary. `SyncMapping::mode = Unmonitored`, `ConflictPolicy::PreferSource`. Run sync. Assert: `conflictDetected` signal emitted exactly once (use `QSignalSpy`); after sync, target's "evt-1" matches source's (per policy).

- [ ] **Step 2: Add `monitored_sameUidDivergent_pausesUntilResume`**

Same setup, `Mode::Monitored`. Run sync. Use `QSignalSpy` on `conflictPauseRequested`; verify it fires. Verify sync is NOT yet complete (`syncCompleted` spy is empty). Then call `worker->resumeAfterConflict(ConflictResolution::KeepSource, "")` from the test thread (or via `QMetaObject::invokeMethod` if cross-thread). Verify `syncCompleted` fires; verify target now has source's version.

If `SyncWorker` lives on a worker thread, `resumeAfterConflict` must be invoked via queued connection. Use `QMetaObject::invokeMethod(worker, "resumeAfterConflict", Qt::QueuedConnection, Q_ARG(ConflictResolution, ...), Q_ARG(QString, ""))`.

- [ ] **Step 3: Run, commit**

```bash
ctest --test-dir ~/dev/refactor-engine-merger/libkalburator/build -R tst_calendar_conflict --output-on-failure
```

Expected: 2/2 pass.

```bash
git -C ~/dev/refactor-engine-merger/libkalburator add tests/calendar/tst_calendar_conflict.cpp tests/calendar/CMakeLists.txt
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "test(calendar): add tst_calendar_conflict (monitored+unmonitored, Phase D.0)"
```

---

### Task 9: tst_calendar_transcoding_warning

**Files:**
- Create: `tests/calendar/tst_calendar_transcoding_warning.cpp`
- Modify: `tests/calendar/CMakeLists.txt`

- [ ] **Step 1: Survey TranscodingRegistry briefly**

```bash
cat ~/dev/refactor-engine-merger/libkalburator/src/transcoding/transcodingregistry.h
```

Identify how to register a stub `IRruleTranscoder` that flags loss for BYDAY-bearing recurrences when target capability lacks BYDAY. The test will register such a stub at start, run sync against a BYDAY-bearing source, and verify `transcodingWarning` fires.

- [ ] **Step 2: Add CMake + skeleton + `transcoding_sourceHasRruleByDay_targetCantRepresent_emitsWarning`**

Either:
- Register a real lossy stub transcoder in test setup. OR
- Use existing transcoders if `MockBackend` can be configured to advertise reduced capabilities.

Choose whichever is simpler. The test should:
1. Configure target backend's capabilities to NOT support BYDAY.
2. Seed source with an event whose RRULE has BYDAY.
3. Run sync.
4. Assert `transcodingWarning` signal fires (use `QSignalSpy`).
5. Assert the lossy version was written to target.

- [ ] **Step 3: cleanup() must call `TranscodingRegistry::clear()`**

Critical — singleton state must reset between tests. Document this in a comment in the cleanup method.

- [ ] **Step 4: Run, commit**

```bash
ctest --test-dir ~/dev/refactor-engine-merger/libkalburator/build -R tst_calendar_transcoding_warning --output-on-failure
```

```bash
git -C ~/dev/refactor-engine-merger/libkalburator add tests/calendar/tst_calendar_transcoding_warning.cpp tests/calendar/CMakeLists.txt
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "test(calendar): add tst_calendar_transcoding_warning (Phase D.0)"
```

---

### Task 10: refresh worktree baseline + verify-all.sh green

**Files:**
- Modify: `~/dev/refactor-engine-merger/baselines/libkalburator-worktree-ctest.txt`

- [ ] **Step 1: Run all libkalburator tests**

```bash
ctest --test-dir ~/dev/refactor-engine-merger/libkalburator/build --output-on-failure -j 12 \
    > ~/dev/refactor-engine-merger/baselines/libkalburator-worktree-ctest.txt 2>&1
grep "tests passed" ~/dev/refactor-engine-merger/baselines/libkalburator-worktree-ctest.txt
```

Expected: `100% tests passed, 0 tests failed out of 9`.

- [ ] **Step 2: Run verify-all.sh and confirm green**

```bash
bash ~/dev/refactor-engine-merger/scripts/verify-all.sh 2>&1 | tail -20
```

Expected exit 0, "all green, no flips".

If verify-all flags improvements/regressions in PlanStan or WildPalms, investigate before refreshing those baselines — they should be unaffected by libkalburator-side test additions.

- [ ] **Step 3: Don't commit the baseline file** — it lives in the coordination folder which isn't a git repo. The file is updated in place.

---

### Task 11: tag v0.9-phase-d0-tests-first

- [ ] **Step 1: Confirm clean state**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator status
git -C ~/dev/refactor-engine-merger/libkalburator log --oneline pre-engine-merger..HEAD
```

Expected: working tree clean; the log shows ≥ 5 commits (04k roadmap, 04l design, stubs, four test executables, plus 04l plan).

- [ ] **Step 2: Tag**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator tag v0.9-phase-d0-tests-first
git -C ~/dev/refactor-engine-merger/libkalburator describe --tags v0.9-phase-d0-tests-first
```

- [ ] **Step 3: Update phase-status doc per libkalburator's CLAUDE.md**

Open `04b-phase3-status.md` (or whichever current status doc captures Phase 0-era state) and add a "Phase D.0 — landed 2026-04-28" line if the convention applies. If unclear which doc to update, leave it; the 04l-design / 04l-plan / 04k roadmap docs together capture the status.

---

## Self-review checklist

After all 11 tasks:

1. **Spec coverage:** the design's 4 test executables, 4 stubs, and acceptance criteria all map to tasks above. ✓
2. **Placeholder scan:** the bracketed comments in Task 3 / Task 4 (`[Concrete method bodies authored after reading…]`) are intentional — those interfaces aren't fully nailed down without reading the headers, and the plan tells the engineer exactly what to do. Not a placeholder; a deliberate research-then-fill pattern.
3. **Type consistency:** `StubSyncHost::AppliedChange` is referenced once as a public struct — used by tests via `appliedChanges()` accessor. Other type uses (`MemoryCalendar*`, `Incidence::Ptr`, `BackendRegistry*`) are KCalendarCore / production types, consistent across tasks.
4. **Cross-task naming:** `kalburator_calendar_test_stubs` (the static lib name) is used in Tasks 1, 6, 7, 8, 9 — consistent. `kalburator_add_calendar_integration_test` (the helper function) defined in Task 1, used in Tasks 6–9.

## Risks at the plan level

- **Stub method-body authorship requires reading production headers.** The plan deliberately doesn't paste full bodies for `StubIncidenceRegistry` / `StubSyncConfigStore` because those interfaces may have shifted; the engineer reads the header first. If the header has surprising shape, fix the design doc rather than improvising.
- **`SyncCoordinator` setup in tests may need 2-3 helpers we haven't itemized.** Things like creating a mapping, attaching backends, and running a sync. If those become long, factor into a small helper at the top of each test cpp; if the helpers duplicate across files, promote to `calendar_fixtures.{h,cpp}` (deferred per design).
- **Property-sync may run before incidence sync in `SyncWorker::processSync`.** If property paths crash because `MockBackend` doesn't return all expected `discovered*` defaults, surface this and fix `MockBackend` (it's library code, fair game) or set defaults in test setup.
- **Conflict-test signal timing.** Cross-thread `QSignalSpy` can race with `resumeAfterConflict` on slow runners. Use `spy.wait(5000)` defaults; bump to 10s if any test flakes.
