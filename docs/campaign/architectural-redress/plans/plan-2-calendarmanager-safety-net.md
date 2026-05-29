# CalendarManager Safety-Net (Characterization Tests) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Pin `CalendarManager`'s current destructive-CRUD behavior with a characterization
test suite, so the later `CalendarManager` split (campaign Plan 8) cannot silently regress it.

**Architecture:** A single QtTest integration test (`tests/calendar/tst_calendar_manager.cpp`)
built via the existing `kalburator_add_calendar_integration_test()` helper. `CalendarManager`
is constructed directly against the existing stubs — it needs only `ISyncHost`
(`StubSyncHost`) and `ICalendarCollection` (`StubSyncHost::stubCollection()`); `MockBackend`
instances registered in a `BackendRegistry` provide observable backend state. **No
`SyncEngine`, `BaselineStore`, `ConflictManager`, or `SyncConflictStore` are needed** —
`CalendarManager` only reaches `host->backendById()`, `host->configStore()`, and the
collection.

**Tech Stack:** C++/Qt6, QtTest, KCalendarCore, CMake (legacy `build/` workflow).

**Scope / philosophy:** These are **characterization tests** — they assert the behavior that
exists *today*, including its sharp edges (non-atomic multi-backend writes with no rollback;
`restoreFromSnapshot()` returning `false`). Where a test pins a known-bad behavior, it is
labeled and a `FINDINGS.md` entry records that the later split must address it. **This plan
adds no production code and changes no `CalendarManager` behavior.** If a test cannot pass
against current code, STOP — that is a real bug discovery, not a plan step; record it in
`FINDINGS.md` and surface it before adapting the assertion.

**Branch:** `feature/redress-2-calendarmanager-tests` (off `main`). Delete the stale empty
`feature/redress-2-cycle-break` branch first. Worktree per `superpowers:using-git-worktrees`.

---

## Reference facts (verified against source at HEAD)

These are the exact signatures the test code uses. Do not re-derive; trust and cite.

- `Kalburator::Sync::CalendarManager` ctor: `CalendarManager(ISyncHost *host, ICalendarCollection *collection, QObject *parent = nullptr)` (`src/calendar/calendarmanager.h:106`).
- `StubSyncHost(BackendRegistry *registry)` owns the stubs; getters `stubCollection()`,
  `stubConfig()`, `stubRegistry()`; `configStore()` returns the `StubSyncConfigStore`
  (`tests/calendar/stubs/stubsynchost.h`).
- `StubSyncConfigStore`: `addLogicalCalendar(const LogicalCalendar&)`,
  `logicalCalendar(const QString&) const`, `removeLogicalCalendar(const QString&)`,
  `syncMappings()`, `saveCount()` (`tests/calendar/stubs/stubsyncconfigstore.h`).
- `StubCalendarCollection`: `id()` (default `"stub-collection"`),
  `addCalendarWithId(const QString&, MemoryCalendar*)`, `calendar(const QString&)`,
  `recordedVisible(const QString&)`, `recordedColor(const QString&)`
  (`tests/calendar/stubs/stubcalendarcollection.h`).
- `MockBackend` (a `SyncBackend`): ctor `MockBackend(const QString &backendId, QObject* = nullptr)`;
  `createCalendar(collectionId, calendarId, name, type)`, `deleteCalendar(collectionId, calendarId)`,
  `pushItems(calendarId, items)→PushOperation*`, `deleteItems(calendarId, uids)→DeleteOperation*`,
  `calendarIds()`, `allUids(calendarId)`, `incidence(calendarId, uid)`,
  `addIncidence(calendarId, incidence)`, `operationLog()`,
  `setFailurePoint(FailurePoint, afterN, msg)` with points `OnCreateCalendar`/`OnDeleteCalendar`/
  `OnPush`/`OnDelete` (`src/calendar/mockbackend.h`). `supportsCalendarCreation()==true`.
- `LogicalCalendar` (`src/types/logicalcalendar.h:141`): fields `id`, `displayName`, `type`,
  `bindings` (`QList<CalendarBackendBinding>`); accessors `primaryBinding()`, `syncBindings()`,
  `enabledBindings()`, `isValid()`.
- `CalendarBackendBinding` (`:66`): `backendId`, `calendarId`, `role` (`BackendRole::Primary`/
  `Sync1`/…/`ReadOnly`), `enabled`, `needsCreation`.
- `CreationResult { bool success; QString logicalCalendarId; QStringList errors; QStringList warnings; QMap<QString,bool> backendResults; }` (`calendarmanager.h:38`).
- `DeletionResult { bool success; QString logicalCalendarId; QStringList errors; QMap<QString,bool> backendResults; }` (`:49`).
- `enum class DeleteMode { Hide, Disable, DisconnectSync, Forget, DeleteFromAll }` (`:27`).
- `CalendarSnapshot { LogicalCalendar logicalCalendar; QList<Incidence::Ptr> incidences; QDateTime capturedAt; bool isValid(); }` (`:59`); `captureSnapshot(id) const`, `restoreFromSnapshot(snapshot)` (returns `false`, stub at `calendarmanager.cpp:766-783`).
- Signals (`calendarmanager.h:280`): `calendarCreated/Updated/Deleted(id)`,
  `bindingAdded/Removed/Updated(id, backendId)`, `incidenceCreated/Updated/Deleted(id, uid)`,
  `operationFailed(operation, error)`, `calendarUnloadRequested(calendarId)`,
  `syncMappingRegenerationRequested()`.
- **Behavior facts:** `createCalendar` calls `backend->createCalendar(...)` **only when
  `binding.needsCreation == true`** (`calendarmanager.cpp:84`); `collectionId` passed is
  `m_collection->id()` (`:86`). Every mutation ends by emitting
  `syncMappingRegenerationRequested()` unless inside a batch (`regenerateSyncMappings()` at
  `:863`, batch-deferred) and (for create/delete) calling `configStore->save()`.
  `BatchGuard` (`calendarmanager.h:266`) calls `beginBatch()`/`endBatch()` in ctor/dtor.

---

## File Structure

- **Create:** `tests/calendar/tst_calendar_manager.cpp` — the entire test suite (one file, one
  responsibility: characterize `CalendarManager`). Grows task-by-task.
- **Modify:** `tests/calendar/CMakeLists.txt` — one line registering the new test.
- **Modify (plan close only):** `docs/campaign/architectural-redress/FINDINGS.md`,
  `STATUS.md`.

No production source files are created or modified.

---

### Task 1: Test scaffold, CMake wiring, first smoke test

**Files:**
- Create: `tests/calendar/tst_calendar_manager.cpp`
- Modify: `tests/calendar/CMakeLists.txt` (after line `kalburator_add_calendar_integration_test(tst_engine_subset_dispatch)`)

- [ ] **Step 1: Create the test file with harness + one smoke test**

```cpp
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTimeZone>

#include <KCalendarCore/Event>
#include <KCalendarCore/MemoryCalendar>

#include "backendregistry.h"
#include "calendarmanager.h"
#include "logicalcalendar.h"
#include "mockbackend.h"
#include "synctypes.h"

#include "stubs/stubsynchost.h"

using namespace Kalburator::Sync;

namespace {
constexpr auto kBackendA   = "backend-a";
constexpr auto kBackendB   = "backend-b";
constexpr auto kCalId      = "cal-1";
constexpr auto kCalIdB     = "cal-1-b";
constexpr auto kLogicalId  = "logical-1";

KCalendarCore::Event::Ptr makeEvent(const QString &uid, const QString &summary)
{
    auto e = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    e->setUid(uid);
    e->setSummary(summary);
    e->setDtStart(QDateTime(QDate(2026, 5, 29), QTime(9, 0), QTimeZone::utc()));
    return e;
}

// A single-primary LogicalCalendar bound to one backend.
LogicalCalendar makeLogical(const QString &logicalId,
                            const QString &backendId,
                            const QString &calId,
                            bool needsCreation)
{
    LogicalCalendar lc;
    lc.id          = logicalId;
    lc.displayName = QStringLiteral("Test ") + logicalId;
    lc.type        = CalendarType::Hybrid;
    CalendarBackendBinding b;
    b.backendId     = backendId;
    b.calendarId    = calId;
    b.role          = BackendRole::Primary;
    b.enabled       = true;
    b.needsCreation = needsCreation;
    lc.bindings.append(b);
    return lc;
}
} // namespace

class TestCalendarManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void construct_doesNotCrash_exposesHostAndConfig();

private:
    std::unique_ptr<BackendRegistry> m_registry;
    std::unique_ptr<MockBackend>     m_backendA;
    std::unique_ptr<MockBackend>     m_backendB;
    std::unique_ptr<StubSyncHost>    m_host;
    std::unique_ptr<CalendarManager> m_mgr;
};

void TestCalendarManager::init()
{
    m_registry = std::make_unique<BackendRegistry>();
    m_backendA = std::make_unique<MockBackend>(QString::fromLatin1(kBackendA));
    m_backendB = std::make_unique<MockBackend>(QString::fromLatin1(kBackendB));
    m_registry->registerBackendInstance(QString::fromLatin1(kBackendA), m_backendA.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kBackendB), m_backendB.get());

    m_host = std::make_unique<StubSyncHost>(m_registry.get());
    m_mgr  = std::make_unique<CalendarManager>(m_host.get(), m_host->stubCollection());
}

void TestCalendarManager::cleanup()
{
    m_mgr.reset();
    m_host.reset();
    m_backendB.reset();
    m_backendA.reset();
    m_registry.reset();
}

void TestCalendarManager::construct_doesNotCrash_exposesHostAndConfig()
{
    QVERIFY(m_mgr->host() == m_host.get());
    QVERIFY(m_mgr->configManager() == m_host->configStore());
}

QTEST_MAIN(TestCalendarManager)
#include "tst_calendar_manager.moc"
```

- [ ] **Step 2: Register the test in CMake**

Add this line to `tests/calendar/CMakeLists.txt` immediately after the existing
`kalburator_add_calendar_integration_test(tst_engine_subset_dispatch)` line:

```cmake
kalburator_add_calendar_integration_test(tst_calendar_manager)
```

- [ ] **Step 3: Configure + build**

Run: `cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && make -C build -j$(($(nproc)-1)) tst_calendar_manager`
Expected: compiles and links cleanly (the harness uses only confirmed APIs).

- [ ] **Step 4: Run the smoke test**

Run: `ctest --test-dir build --output-on-failure -R tst_calendar_manager`
Expected: PASS, 1 test.

- [ ] **Step 5: Commit**

```bash
git add tests/calendar/tst_calendar_manager.cpp tests/calendar/CMakeLists.txt
git commit -m "test(calendar): scaffold CalendarManager characterization suite (P2.T1)"
```

---

### Task 2: createCalendar — success path

**Files:**
- Modify: `tests/calendar/tst_calendar_manager.cpp`

- [ ] **Step 1: Add the test declarations** (in the `private slots:` block, after the smoke test)

```cpp
    void createCalendar_needsCreation_createsOnBackend_emitsSignal();
    void createCalendar_registersInConfig_clearsNeedsCreation();
```

- [ ] **Step 2: Add the test bodies** (before `QTEST_MAIN`)

```cpp
void TestCalendarManager::createCalendar_needsCreation_createsOnBackend_emitsSignal()
{
    QSignalSpy created(m_mgr.get(), &CalendarManager::calendarCreated);
    QVERIFY(created.isValid());

    const LogicalCalendar lc =
        makeLogical(QString::fromLatin1(kLogicalId),
                    QString::fromLatin1(kBackendA),
                    QString::fromLatin1(kCalId),
                    /*needsCreation=*/true);

    const CreationResult r = m_mgr->createCalendar(lc);

    QVERIFY(r.success);
    QCOMPARE(r.logicalCalendarId, QString::fromLatin1(kLogicalId));
    QVERIFY(r.errors.isEmpty());
    QCOMPARE(r.backendResults.value(QString::fromLatin1(kBackendA)), true);
    // Backend received the creation.
    QVERIFY(m_backendA->calendarIds().contains(QString::fromLatin1(kCalId)));
    // Signal fired exactly once with the logical id.
    QCOMPARE(created.count(), 1);
    QCOMPARE(created.at(0).at(0).toString(), QString::fromLatin1(kLogicalId));
}

void TestCalendarManager::createCalendar_registersInConfig_clearsNeedsCreation()
{
    const LogicalCalendar lc =
        makeLogical(QString::fromLatin1(kLogicalId),
                    QString::fromLatin1(kBackendA),
                    QString::fromLatin1(kCalId),
                    /*needsCreation=*/true);

    m_mgr->createCalendar(lc);

    // Registered in config.
    const LogicalCalendar stored =
        m_host->configStore()->logicalCalendar(QString::fromLatin1(kLogicalId));
    QCOMPARE(stored.id, QString::fromLatin1(kLogicalId));
    // needsCreation was cleared after successful backend creation.
    QCOMPARE(stored.primaryBinding().needsCreation, false);
    // Config was persisted.
    QVERIFY(m_host->stubConfig()->saveCount() >= 1);
}
```

- [ ] **Step 3: Build + run**

Run: `make -C build -j$(($(nproc)-1)) tst_calendar_manager && ctest --test-dir build --output-on-failure -R tst_calendar_manager`
Expected: PASS, 3 tests.

- [ ] **Step 4: Commit**

```bash
git add tests/calendar/tst_calendar_manager.cpp
git commit -m "test(calendar): pin createCalendar success path (P2.T2)"
```

---

### Task 3: createCalendar — partial failure is non-atomic (no rollback)

**Files:**
- Modify: `tests/calendar/tst_calendar_manager.cpp`

This task pins a **known sharp edge**: when one backend fails, the other backend's calendar is
NOT rolled back. The assertion documents current behavior; a FINDINGS entry flags it for the
split.

- [ ] **Step 1: Add a two-binding helper** (in the anonymous namespace, after `makeLogical`)

```cpp
// Primary on backend A + Sync1 spoke on backend B, both needing creation.
LogicalCalendar makeTwoBackendLogical()
{
    LogicalCalendar lc = makeLogical(QString::fromLatin1(kLogicalId),
                                     QString::fromLatin1(kBackendA),
                                     QString::fromLatin1(kCalId),
                                     /*needsCreation=*/true);
    CalendarBackendBinding spoke;
    spoke.backendId     = QString::fromLatin1(kBackendB);
    spoke.calendarId    = QString::fromLatin1(kCalIdB);
    spoke.role          = BackendRole::Sync1;
    spoke.enabled       = true;
    spoke.needsCreation = true;
    lc.bindings.append(spoke);
    return lc;
}
```

- [ ] **Step 2: Add the test declaration**

```cpp
    void createCalendar_oneBackendFails_otherStillCreated_noRollback();
```

- [ ] **Step 3: Add the test body**

```cpp
void TestCalendarManager::createCalendar_oneBackendFails_otherStillCreated_noRollback()
{
    // Backend B fails to create; backend A succeeds.
    m_backendB->setFailurePoint(MockBackend::FailurePoint::OnCreateCalendar, 0,
                                QStringLiteral("injected create failure"));
    QSignalSpy failed(m_mgr.get(), &CalendarManager::operationFailed);

    const CreationResult r = m_mgr->createCalendar(makeTwoBackendLogical());

    // Overall failure, but per-backend results are split.
    QVERIFY(!r.success);
    QVERIFY(!r.errors.isEmpty());
    QCOMPARE(r.backendResults.value(QString::fromLatin1(kBackendA)), true);
    QCOMPARE(r.backendResults.value(QString::fromLatin1(kBackendB)), false);
    QVERIFY(failed.count() >= 1);

    // CHARACTERIZATION OF CURRENT (NON-ATOMIC) BEHAVIOR: backend A's calendar is
    // NOT rolled back even though the overall operation failed. Pinned so the
    // later CalendarManager split (campaign Plan 8) must consciously decide
    // whether to add transactional rollback. See FINDINGS (P2.T3).
    QVERIFY(m_backendA->calendarIds().contains(QString::fromLatin1(kCalId)));
    QVERIFY(!m_backendB->calendarIds().contains(QString::fromLatin1(kCalIdB)));
}
```

- [ ] **Step 4: Build + run**

Run: `make -C build -j$(($(nproc)-1)) tst_calendar_manager && ctest --test-dir build --output-on-failure -R tst_calendar_manager`
Expected: PASS, 4 tests. **If `r.success` is true or backend A was rolled back, STOP** — the
behavior differs from the audit's reading; record in FINDINGS and surface before continuing.

- [ ] **Step 5: Record the pinned sharp edge in FINDINGS, then commit**

Append under `## Open (new findings, post-rebaseline)` in
`docs/campaign/architectural-redress/FINDINGS.md`:

```markdown
### From Plan 2 (CalendarManager safety net, 2026-05-29)

- 2026-05-29 — `src/calendar/calendarmanager.cpp:43-139` — inv (correctness) — `createCalendar`
  is non-atomic across bindings: a per-backend failure leaves already-created backends in place
  with no rollback (pinned by `tst_calendar_manager::createCalendar_oneBackendFails_*`). The
  CalendarManager split (Plan 8) must decide whether to add transactional semantics.
```

```bash
git add tests/calendar/tst_calendar_manager.cpp docs/campaign/architectural-redress/FINDINGS.md
git commit -m "test(calendar): pin createCalendar non-atomic partial-failure (P2.T3)"
```

---

### Task 4: deleteCalendar — all five DeleteMode variants

**Files:**
- Modify: `tests/calendar/tst_calendar_manager.cpp`

Each mode is one test. All seed the config + backend directly (faster than `createCalendar`) so
the mode behavior is isolated.

- [ ] **Step 1: Add a seeding helper** (anonymous namespace)

```cpp
// Seed config with a primary+Sync1 logical calendar and create both backend calendars.
void seedTwoBackendCalendar(StubSyncHost *host, MockBackend *a, MockBackend *b)
{
    LogicalCalendar lc = makeLogical(QString::fromLatin1(kLogicalId),
                                     QString::fromLatin1(kBackendA),
                                     QString::fromLatin1(kCalId),
                                     /*needsCreation=*/false);
    CalendarBackendBinding spoke;
    spoke.backendId  = QString::fromLatin1(kBackendB);
    spoke.calendarId = QString::fromLatin1(kCalIdB);
    spoke.role       = BackendRole::Sync1;
    spoke.enabled    = true;
    lc.bindings.append(spoke);
    host->configStore()->addLogicalCalendar(lc);
    a->createCalendar(host->stubCollection()->id(), QString::fromLatin1(kCalId),
                      QStringLiteral("A"));
    b->createCalendar(host->stubCollection()->id(), QString::fromLatin1(kCalIdB),
                      QStringLiteral("B"));
}
```

- [ ] **Step 2: Add the test declarations**

```cpp
    void deleteCalendar_hide_setsInvisible_keepsConfigAndData();
    void deleteCalendar_disable_emitsUnloadRequest_keepsConfig();
    void deleteCalendar_disconnectSync_dropsSecondaryBindings_keepsPrimary();
    void deleteCalendar_forget_removesConfig_keepsBackendData();
    void deleteCalendar_deleteFromAll_deletesBackendsAndConfig();
```

- [ ] **Step 3: Add the test bodies**

```cpp
void TestCalendarManager::deleteCalendar_hide_setsInvisible_keepsConfigAndData()
{
    seedTwoBackendCalendar(m_host.get(), m_backendA.get(), m_backendB.get());

    const DeletionResult r =
        m_mgr->deleteCalendar(QString::fromLatin1(kLogicalId), DeleteMode::Hide);

    QVERIFY(r.success);
    // Primary calendar marked not-visible via the collection.
    QCOMPARE(m_host->stubCollection()->recordedVisible(QString::fromLatin1(kCalId)), false);
    // Config retained, backend data retained.
    QVERIFY(!m_host->configStore()->logicalCalendar(QString::fromLatin1(kLogicalId)).id.isEmpty());
    QVERIFY(m_backendA->calendarIds().contains(QString::fromLatin1(kCalId)));
}

void TestCalendarManager::deleteCalendar_disable_emitsUnloadRequest_keepsConfig()
{
    seedTwoBackendCalendar(m_host.get(), m_backendA.get(), m_backendB.get());
    QSignalSpy unload(m_mgr.get(), &CalendarManager::calendarUnloadRequested);

    const DeletionResult r =
        m_mgr->deleteCalendar(QString::fromLatin1(kLogicalId), DeleteMode::Disable);

    QVERIFY(r.success);
    QCOMPARE(unload.count(), 1);
    QCOMPARE(unload.at(0).at(0).toString(), QString::fromLatin1(kCalId));
    // Config retained (just disabled), backend data retained.
    QVERIFY(!m_host->configStore()->logicalCalendar(QString::fromLatin1(kLogicalId)).id.isEmpty());
    QVERIFY(m_backendA->calendarIds().contains(QString::fromLatin1(kCalId)));
}

void TestCalendarManager::deleteCalendar_disconnectSync_dropsSecondaryBindings_keepsPrimary()
{
    seedTwoBackendCalendar(m_host.get(), m_backendA.get(), m_backendB.get());

    const DeletionResult r =
        m_mgr->deleteCalendar(QString::fromLatin1(kLogicalId), DeleteMode::DisconnectSync);

    QVERIFY(r.success);
    const LogicalCalendar stored =
        m_host->configStore()->logicalCalendar(QString::fromLatin1(kLogicalId));
    // Primary survives; Sync1 spoke removed.
    QVERIFY(stored.primaryBinding().isValid());
    QVERIFY(stored.syncBindings().isEmpty());
    // Backend data for both calendars is untouched (DisconnectSync is config-only).
    QVERIFY(m_backendB->calendarIds().contains(QString::fromLatin1(kCalIdB)));
}

void TestCalendarManager::deleteCalendar_forget_removesConfig_keepsBackendData()
{
    seedTwoBackendCalendar(m_host.get(), m_backendA.get(), m_backendB.get());

    const DeletionResult r =
        m_mgr->deleteCalendar(QString::fromLatin1(kLogicalId), DeleteMode::Forget);

    QVERIFY(r.success);
    // Removed from config...
    QVERIFY(m_host->configStore()->logicalCalendar(QString::fromLatin1(kLogicalId)).id.isEmpty());
    // ...but backend data is intentionally kept.
    QVERIFY(m_backendA->calendarIds().contains(QString::fromLatin1(kCalId)));
    QVERIFY(m_backendB->calendarIds().contains(QString::fromLatin1(kCalIdB)));
}

void TestCalendarManager::deleteCalendar_deleteFromAll_deletesBackendsAndConfig()
{
    seedTwoBackendCalendar(m_host.get(), m_backendA.get(), m_backendB.get());
    QSignalSpy deleted(m_mgr.get(), &CalendarManager::calendarDeleted);

    const DeletionResult r =
        m_mgr->deleteCalendar(QString::fromLatin1(kLogicalId), DeleteMode::DeleteFromAll);

    QVERIFY(r.success);
    QCOMPARE(r.backendResults.value(QString::fromLatin1(kBackendA)), true);
    QCOMPARE(r.backendResults.value(QString::fromLatin1(kBackendB)), true);
    // Deleted from BOTH backends and from config.
    QVERIFY(!m_backendA->calendarIds().contains(QString::fromLatin1(kCalId)));
    QVERIFY(!m_backendB->calendarIds().contains(QString::fromLatin1(kCalIdB)));
    QVERIFY(m_host->configStore()->logicalCalendar(QString::fromLatin1(kLogicalId)).id.isEmpty());
    QCOMPARE(deleted.count(), 1);
}
```

- [ ] **Step 4: Build + run**

Run: `make -C build -j$(($(nproc)-1)) tst_calendar_manager && ctest --test-dir build --output-on-failure -R tst_calendar_manager`
Expected: PASS, 9 tests. **If any mode behaves differently than asserted, STOP** — read the
mode's impl (`calendarmanager.cpp:224-341`), correct the assertion to match *actual* behavior,
and note any surprise in FINDINGS. (The assertions encode the impl as read; a mismatch is
either a misread or a real discrepancy worth surfacing.)

- [ ] **Step 5: Commit**

```bash
git add tests/calendar/tst_calendar_manager.cpp
git commit -m "test(calendar): pin all five deleteCalendar DeleteMode variants (P2.T4)"
```

---

### Task 5: Incidence CRUD — create / update / delete propagate to backends

**Files:**
- Modify: `tests/calendar/tst_calendar_manager.cpp`

`createIncidence`/`updateIncidence`/`deleteIncidence` block on `QEventLoop` until the
`MockBackend`'s `PushOperation`/`DeleteOperation` finishes (synchronous in-test). They iterate
**enabled** bindings.

- [ ] **Step 1: Add the test declarations**

```cpp
    void createIncidence_pushesToAllEnabledBindings_emitsSignal();
    void updateIncidence_pushesUpdate_emitsSignal();
    void deleteIncidence_removesFromBackends_emitsSignal();
```

- [ ] **Step 2: Add the test bodies**

```cpp
void TestCalendarManager::createIncidence_pushesToAllEnabledBindings_emitsSignal()
{
    seedTwoBackendCalendar(m_host.get(), m_backendA.get(), m_backendB.get());
    QSignalSpy created(m_mgr.get(), &CalendarManager::incidenceCreated);

    const bool ok = m_mgr->createIncidence(QString::fromLatin1(kLogicalId),
                                           makeEvent(QStringLiteral("evt-1"),
                                                     QStringLiteral("One")));
    QVERIFY(ok);
    // Pushed to BOTH bindings' calendars (primary cal-1 on A, spoke cal-1-b on B).
    QVERIFY(m_backendA->allUids(QString::fromLatin1(kCalId)).contains(QStringLiteral("evt-1")));
    QVERIFY(m_backendB->allUids(QString::fromLatin1(kCalIdB)).contains(QStringLiteral("evt-1")));
    QCOMPARE(created.count(), 1);
    QCOMPARE(created.at(0).at(1).toString(), QStringLiteral("evt-1"));
}

void TestCalendarManager::updateIncidence_pushesUpdate_emitsSignal()
{
    seedTwoBackendCalendar(m_host.get(), m_backendA.get(), m_backendB.get());
    m_mgr->createIncidence(QString::fromLatin1(kLogicalId),
                           makeEvent(QStringLiteral("evt-1"), QStringLiteral("One")));
    QSignalSpy updated(m_mgr.get(), &CalendarManager::incidenceUpdated);

    const bool ok = m_mgr->updateIncidence(QString::fromLatin1(kLogicalId),
                                           makeEvent(QStringLiteral("evt-1"),
                                                     QStringLiteral("One (edited)")));
    QVERIFY(ok);
    QCOMPARE(updated.count(), 1);
    auto fetched = m_backendA->incidence(QString::fromLatin1(kCalId), QStringLiteral("evt-1"));
    QVERIFY(fetched);
    QCOMPARE(fetched->summary(), QStringLiteral("One (edited)"));
}

void TestCalendarManager::deleteIncidence_removesFromBackends_emitsSignal()
{
    seedTwoBackendCalendar(m_host.get(), m_backendA.get(), m_backendB.get());
    m_mgr->createIncidence(QString::fromLatin1(kLogicalId),
                           makeEvent(QStringLiteral("evt-1"), QStringLiteral("One")));
    QSignalSpy deleted(m_mgr.get(), &CalendarManager::incidenceDeleted);

    const bool ok = m_mgr->deleteIncidence(QString::fromLatin1(kLogicalId),
                                           QStringLiteral("evt-1"));
    QVERIFY(ok);
    QCOMPARE(deleted.count(), 1);
    QVERIFY(!m_backendA->allUids(QString::fromLatin1(kCalId)).contains(QStringLiteral("evt-1")));
    QVERIFY(!m_backendB->allUids(QString::fromLatin1(kCalIdB)).contains(QStringLiteral("evt-1")));
}
```

- [ ] **Step 3: Build + run**

Run: `make -C build -j$(($(nproc)-1)) tst_calendar_manager && ctest --test-dir build --output-on-failure -R tst_calendar_manager`
Expected: PASS, 12 tests. **If a test hangs**, the `MockBackend` push/delete op did not finish
(check it is not in a blocking mode); this would be a real discovery — STOP and surface.

- [ ] **Step 4: Commit**

```bash
git add tests/calendar/tst_calendar_manager.cpp
git commit -m "test(calendar): pin incidence create/update/delete propagation (P2.T5)"
```

---

### Task 6: Incidence create — backend push failure surfaces as failure

**Files:**
- Modify: `tests/calendar/tst_calendar_manager.cpp`

- [ ] **Step 1: Add the test declaration**

```cpp
    void createIncidence_backendPushFails_returnsFalse_emitsOperationFailed();
```

- [ ] **Step 2: Add the test body**

```cpp
void TestCalendarManager::createIncidence_backendPushFails_returnsFalse_emitsOperationFailed()
{
    seedTwoBackendCalendar(m_host.get(), m_backendA.get(), m_backendB.get());
    m_backendA->setFailurePoint(MockBackend::FailurePoint::OnPush, 0,
                                QStringLiteral("injected push failure"));
    QSignalSpy failed(m_mgr.get(), &CalendarManager::operationFailed);

    const bool ok = m_mgr->createIncidence(QString::fromLatin1(kLogicalId),
                                           makeEvent(QStringLiteral("evt-x"),
                                                     QStringLiteral("X")));
    QVERIFY(!ok);
    QVERIFY(failed.count() >= 1);
}
```

- [ ] **Step 3: Build + run**

Run: `make -C build -j$(($(nproc)-1)) tst_calendar_manager && ctest --test-dir build --output-on-failure -R tst_calendar_manager`
Expected: PASS, 13 tests. **If it hangs**, the failure-injected push never emits `finished` —
that is a real bug (the QEventLoop wait would never return in production either); STOP and
record in FINDINGS.

- [ ] **Step 4: Commit**

```bash
git add tests/calendar/tst_calendar_manager.cpp
git commit -m "test(calendar): pin incidence push-failure error path (P2.T6)"
```

---

### Task 7: Batch mode defers sync-mapping regeneration

**Files:**
- Modify: `tests/calendar/tst_calendar_manager.cpp`

- [ ] **Step 1: Add the test declarations**

```cpp
    void withoutBatch_eachMutationRegenerates();
    void batchGuard_defersRegenerationToSingleEmission();
```

- [ ] **Step 2: Add the test bodies**

```cpp
void TestCalendarManager::withoutBatch_eachMutationRegenerates()
{
    QSignalSpy regen(m_mgr.get(), &CalendarManager::syncMappingRegenerationRequested);

    m_mgr->createCalendar(makeLogical(QStringLiteral("lc-a"),
                                      QString::fromLatin1(kBackendA),
                                      QStringLiteral("ca"), true));
    m_mgr->createCalendar(makeLogical(QStringLiteral("lc-b"),
                                      QString::fromLatin1(kBackendA),
                                      QStringLiteral("cb"), true));

    // Two separate mutations → two regeneration requests.
    QCOMPARE(regen.count(), 2);
}

void TestCalendarManager::batchGuard_defersRegenerationToSingleEmission()
{
    QSignalSpy regen(m_mgr.get(), &CalendarManager::syncMappingRegenerationRequested);
    {
        CalendarManager::BatchGuard guard(m_mgr.get());
        m_mgr->createCalendar(makeLogical(QStringLiteral("lc-a"),
                                          QString::fromLatin1(kBackendA),
                                          QStringLiteral("ca"), true));
        m_mgr->createCalendar(makeLogical(QStringLiteral("lc-b"),
                                          QString::fromLatin1(kBackendA),
                                          QStringLiteral("cb"), true));
        // Inside the batch: regeneration is deferred, not yet fired.
        QCOMPARE(regen.count(), 0);
    } // guard dtor → endBatch() → single deferred emission

    QCOMPARE(regen.count(), 1);
}
```

> **Note on `BatchGuard` construction:** confirm the exact ctor arg shape against
> `calendarmanager.h:266-275` when implementing (it takes the `CalendarManager*`). If the guard
> is nested differently, adjust the brace scope — the assertion intent (0 inside, 1 after) is
> the contract.

- [ ] **Step 3: Build + run**

Run: `make -C build -j$(($(nproc)-1)) tst_calendar_manager && ctest --test-dir build --output-on-failure -R tst_calendar_manager`
Expected: PASS, 15 tests.

- [ ] **Step 4: Commit**

```bash
git add tests/calendar/tst_calendar_manager.cpp
git commit -m "test(calendar): pin batch-mode regeneration deferral (P2.T7)"
```

---

### Task 8: Snapshot capture works; restore is an unimplemented stub

**Files:**
- Modify: `tests/calendar/tst_calendar_manager.cpp`

- [ ] **Step 1: Add the test declarations**

```cpp
    void captureSnapshot_clonesPrimaryCalendarIncidences();
    void restoreFromSnapshot_currentlyUnimplemented_returnsFalse();
```

- [ ] **Step 2: Add the test bodies**

```cpp
void TestCalendarManager::captureSnapshot_clonesPrimaryCalendarIncidences()
{
    // Seed config + a MemoryCalendar (in the collection) holding one event.
    LogicalCalendar lc = makeLogical(QString::fromLatin1(kLogicalId),
                                     QString::fromLatin1(kBackendA),
                                     QString::fromLatin1(kCalId),
                                     /*needsCreation=*/false);
    m_host->configStore()->addLogicalCalendar(lc);

    auto *mem = new KCalendarCore::MemoryCalendar(QTimeZone::utc());
    mem->setId(QString::fromLatin1(kCalId));
    mem->addEvent(makeEvent(QStringLiteral("snap-1"), QStringLiteral("Snap")));
    m_host->stubCollection()->addCalendarWithId(QString::fromLatin1(kCalId), mem);

    const CalendarSnapshot snap = m_mgr->captureSnapshot(QString::fromLatin1(kLogicalId));

    QVERIFY(snap.isValid());
    QCOMPARE(snap.logicalCalendar.id, QString::fromLatin1(kLogicalId));
    QCOMPARE(snap.incidences.size(), 1);
    QCOMPARE(snap.incidences.first()->uid(), QStringLiteral("snap-1"));
}

void TestCalendarManager::restoreFromSnapshot_currentlyUnimplemented_returnsFalse()
{
    // CHARACTERIZATION OF A KNOWN GAP: restoreFromSnapshot() is a stub that always
    // returns false (calendarmanager.cpp:766-783). This test pins the gap so that
    // whoever implements restore (a later plan) flips this test and updates it.
    // See FINDINGS (P2.T8).
    CalendarSnapshot snap;
    snap.logicalCalendar.id          = QString::fromLatin1(kLogicalId);
    snap.logicalCalendar.displayName = QStringLiteral("X");
    QVERIFY(snap.isValid());

    QCOMPARE(m_mgr->restoreFromSnapshot(snap), false);
}
```

- [ ] **Step 3: Build + run**

Run: `make -C build -j$(($(nproc)-1)) tst_calendar_manager && ctest --test-dir build --output-on-failure -R tst_calendar_manager`
Expected: PASS, 17 tests.

- [ ] **Step 4: Record the stub gap in FINDINGS, then commit**

Append under the Plan 2 subsection in `FINDINGS.md`:

```markdown
- 2026-05-29 — `src/calendar/calendarmanager.cpp:766-783` — inv (correctness) —
  `restoreFromSnapshot()` is a stub returning `false` while `captureSnapshot()` is fully
  implemented; destructive ops therefore have capture-but-no-undo. Pinned by
  `tst_calendar_manager::restoreFromSnapshot_currentlyUnimplemented_returnsFalse`. Implementing
  restore (or removing the dead capture API) is a candidate for the CalendarManager split.
```

```bash
git add tests/calendar/tst_calendar_manager.cpp docs/campaign/architectural-redress/FINDINGS.md
git commit -m "test(calendar): pin captureSnapshot + restore-stub gap (P2.T8)"
```

---

### Task 9: Full-suite gate + close the plan

**Files:**
- Modify: `docs/campaign/architectural-redress/STATUS.md`

- [ ] **Step 1: Run the entire libkalburator suite (regression gate)**

Run: `cmake --build build -j$(($(nproc)-1)) >/dev/null && ctest --test-dir build --output-on-failure -j$(($(nproc)-1))`
Expected: 100% pass (was 131/131 before this plan; now 132/132 with `tst_calendar_manager`).
Confirm no pre-existing test regressed.

- [ ] **Step 2: Confirm the new test runs offscreen/headless cleanly**

Run: `ctest --test-dir build -R tst_calendar_manager -V 2>&1 | tail -25`
Expected: all 17 subtests pass; no `QEventLoop`/timeout warnings.

- [ ] **Step 3: Update STATUS.md plan table**

In the "Plan sequence and dependencies" table, change the row:
`| 2 | CalendarManager safety net (protective tests) | CRITICAL #4 | next — being detailed |`
to:
`| 2 | CalendarManager safety net (protective tests) | CRITICAL #4 | **DONE — <merge/commit hash>** |`
and set the "Next action" to detailing Plan 3 (neutralize the calendar-typed sync core).

- [ ] **Step 4: Commit + finish the branch**

```bash
git add docs/campaign/architectural-redress/STATUS.md
git commit -m "docs(campaign): close Plan 2 — CalendarManager safety net landed (P2.T9)"
```

Then use `superpowers:finishing-a-development-branch` to verify green and choose merge/PR.

---

## Self-Review

**1. Spec coverage (AUDIT CRITICAL #4 — "tests covering success, partial-failure recovery,
batch semantics, and snapshot/restore before any refactor"):**
- success → T2 (createCalendar), T5 (incidence CRUD), T4 (delete modes).
- partial-failure → T3 (createCalendar non-atomic), T6 (incidence push failure).
- batch semantics → T7.
- snapshot/restore → T8.
- destructive `DeleteMode` (the audit's named danger) → T4 (all five).
- Construction/regression gate → T1, T9.
Covered.

**2. Placeholder scan:** every step shows the actual test code, exact `cmake`/`ctest`/`git`
commands, and expected counts. The one soft spot — `BatchGuard` ctor shape (T7) — is flagged
with the exact header line to confirm and the contract to preserve. No "TBD"/"similar to".

**3. Type consistency:** `kBackendA/B`, `kCalId`, `kCalIdB`, `kLogicalId` constants and helpers
(`makeEvent`, `makeLogical`, `makeTwoBackendLogical`, `seedTwoBackendCalendar`) are defined in
T1/T3/T4 and reused verbatim. Result types (`CreationResult.backendResults`,
`DeletionResult.success`), signal names, and `DeleteMode` enumerators match the verified
reference-facts section. Test method names are unique across tasks.

**4. Characterization caveat honored:** every task that pins a sharp edge (T3 non-atomic, T6
push failure, T8 restore stub) instructs the implementer to STOP and record in FINDINGS rather
than silently adapt, if reality differs from the asserted (impl-derived) behavior.
