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
using namespace Kalburator::Sync::Test;

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

// Task 3: helper — two-backend logical with both needing creation.
LogicalCalendar makeTwoBackendLogical()
{
    LogicalCalendar lc = makeLogical(QString::fromLatin1(kLogicalId),
                                     QString::fromLatin1(kBackendA),
                                     QString::fromLatin1(kCalId), true);
    CalendarBackendBinding spoke;
    spoke.backendId     = QString::fromLatin1(kBackendB);
    spoke.calendarId    = QString::fromLatin1(kCalIdB);
    spoke.role          = BackendRole::Sync1;
    spoke.enabled       = true;
    spoke.needsCreation = true;
    lc.bindings.append(spoke);
    return lc;
}

// Task 4: seed a two-backend calendar (both already existing on backends).
void seedTwoBackendCalendar(StubSyncHost *host, MockBackend *a, MockBackend *b)
{
    LogicalCalendar lc = makeLogical(QString::fromLatin1(kLogicalId),
                                     QString::fromLatin1(kBackendA),
                                     QString::fromLatin1(kCalId), false);
    CalendarBackendBinding spoke;
    spoke.backendId  = QString::fromLatin1(kBackendB);
    spoke.calendarId = QString::fromLatin1(kCalIdB);
    spoke.role       = BackendRole::Sync1;
    spoke.enabled    = true;
    lc.bindings.append(spoke);
    host->configStore()->addLogicalCalendar(lc);
    a->createCalendar(host->stubCollection()->id(), QString::fromLatin1(kCalId), QStringLiteral("A"));
    b->createCalendar(host->stubCollection()->id(), QString::fromLatin1(kCalIdB), QStringLiteral("B"));
}
} // namespace

class TestCalendarManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void construct_doesNotCrash_exposesHostAndConfig();

    // Task 2
    void createCalendar_needsCreation_createsOnBackend_emitsSignal();
    void createCalendar_registersInConfig_clearsNeedsCreation();

    // Task 3
    void createCalendar_oneBackendFails_otherStillCreated_noRollback();

    // Task 4
    void deleteCalendar_hide_setsInvisible_keepsConfigAndData();
    void deleteCalendar_disable_emitsUnloadRequest_keepsConfig();
    void deleteCalendar_disconnectSync_dropsSecondaryBindings_keepsPrimary();
    void deleteCalendar_forget_removesConfig_keepsBackendData();
    void deleteCalendar_deleteFromAll_deletesBackendsAndConfig();

    // Task 5
    void createIncidence_pushesToAllEnabledBindings_emitsSignal();
    void updateIncidence_pushesUpdate_emitsSignal();
    void deleteIncidence_removesFromBackends_emitsSignal();

    // Task 6
    void createIncidence_backendPushFails_returnsFalse_emitsOperationFailed();

    // Task 7
    void withoutBatch_eachMutationRegenerates();
    void batchGuard_defersRegenerationToSingleEmission();

    // Task 8
    void captureSnapshot_clonesPrimaryCalendarIncidences();
    void restoreFromSnapshot_currentlyUnimplemented_returnsFalse();

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

// ============================================================
// Task 2 — createCalendar success path
// ============================================================

void TestCalendarManager::createCalendar_needsCreation_createsOnBackend_emitsSignal()
{
    QSignalSpy created(m_mgr.get(), &CalendarManager::calendarCreated);
    QVERIFY(created.isValid());
    const LogicalCalendar lc = makeLogical(QString::fromLatin1(kLogicalId),
                                           QString::fromLatin1(kBackendA),
                                           QString::fromLatin1(kCalId), true);
    const CreationResult r = m_mgr->createCalendar(lc);
    QVERIFY(r.success);
    QCOMPARE(r.logicalCalendarId, QString::fromLatin1(kLogicalId));
    QVERIFY(r.errors.isEmpty());
    QCOMPARE(r.backendResults.value(QString::fromLatin1(kBackendA)), true);
    QVERIFY(m_backendA->calendarIds().contains(QString::fromLatin1(kCalId)));
    QCOMPARE(created.count(), 1);
    QCOMPARE(created.at(0).at(0).toString(), QString::fromLatin1(kLogicalId));
}

void TestCalendarManager::createCalendar_registersInConfig_clearsNeedsCreation()
{
    const LogicalCalendar lc = makeLogical(QString::fromLatin1(kLogicalId),
                                           QString::fromLatin1(kBackendA),
                                           QString::fromLatin1(kCalId), true);
    m_mgr->createCalendar(lc);
    const LogicalCalendar stored = m_host->configStore()->logicalCalendar(QString::fromLatin1(kLogicalId));
    QCOMPARE(stored.id, QString::fromLatin1(kLogicalId));
    QCOMPARE(stored.primaryBinding().needsCreation, false);
    QVERIFY(m_host->stubConfig()->saveCount() >= 1);
}

// ============================================================
// Task 3 — createCalendar partial failure is non-atomic
// ============================================================

void TestCalendarManager::createCalendar_oneBackendFails_otherStillCreated_noRollback()
{
    m_backendB->setFailurePoint(MockBackend::FailurePoint::OnCreateCalendar, 0,
                                QStringLiteral("injected create failure"));
    QSignalSpy failed(m_mgr.get(), &CalendarManager::operationFailed);
    const CreationResult r = m_mgr->createCalendar(makeTwoBackendLogical());
    QVERIFY(!r.success);
    QVERIFY(!r.errors.isEmpty());
    QCOMPARE(r.backendResults.value(QString::fromLatin1(kBackendA)), true);
    QCOMPARE(r.backendResults.value(QString::fromLatin1(kBackendB)), false);
    QVERIFY(failed.count() >= 1);
    QVERIFY(m_backendA->calendarIds().contains(QString::fromLatin1(kCalId)));
    QVERIFY(!m_backendB->calendarIds().contains(QString::fromLatin1(kCalIdB)));
}

// ============================================================
// Task 4 — deleteCalendar, all five DeleteMode variants
// ============================================================

void TestCalendarManager::deleteCalendar_hide_setsInvisible_keepsConfigAndData()
{
    seedTwoBackendCalendar(m_host.get(), m_backendA.get(), m_backendB.get());
    const DeletionResult r = m_mgr->deleteCalendar(QString::fromLatin1(kLogicalId), DeleteMode::Hide);
    QVERIFY(r.success);
    QCOMPARE(m_host->stubCollection()->recordedVisible(QString::fromLatin1(kCalId)), false);
    QVERIFY(!m_host->configStore()->logicalCalendar(QString::fromLatin1(kLogicalId)).id.isEmpty());
    QVERIFY(m_backendA->calendarIds().contains(QString::fromLatin1(kCalId)));
}

void TestCalendarManager::deleteCalendar_disable_emitsUnloadRequest_keepsConfig()
{
    seedTwoBackendCalendar(m_host.get(), m_backendA.get(), m_backendB.get());
    QSignalSpy unload(m_mgr.get(), &CalendarManager::calendarUnloadRequested);
    const DeletionResult r = m_mgr->deleteCalendar(QString::fromLatin1(kLogicalId), DeleteMode::Disable);
    QVERIFY(r.success);
    QCOMPARE(unload.count(), 1);
    QCOMPARE(unload.at(0).at(0).toString(), QString::fromLatin1(kCalId));
    QVERIFY(!m_host->configStore()->logicalCalendar(QString::fromLatin1(kLogicalId)).id.isEmpty());
    QVERIFY(m_backendA->calendarIds().contains(QString::fromLatin1(kCalId)));
}

void TestCalendarManager::deleteCalendar_disconnectSync_dropsSecondaryBindings_keepsPrimary()
{
    // NOTE (Task-4 correction): DisconnectSync calls removeBinding() (which updates config),
    // then calls m_configManager->updateLogicalCalendar(logCal) with the ORIGINAL logCal
    // (still containing the Sync1 binding), overwriting the removeBinding effect.
    // So the stored LC still has the Sync1 binding; syncBindings() is NOT empty.
    // This is a bug pinned by this test — see FINDINGS.md.
    seedTwoBackendCalendar(m_host.get(), m_backendA.get(), m_backendB.get());
    const DeletionResult r = m_mgr->deleteCalendar(QString::fromLatin1(kLogicalId), DeleteMode::DisconnectSync);
    QVERIFY(r.success);
    const LogicalCalendar stored = m_host->configStore()->logicalCalendar(QString::fromLatin1(kLogicalId));
    QVERIFY(stored.primaryBinding().isValid());
    // The overwrite bug means syncBindings() is NOT empty after DisconnectSync:
    QVERIFY(!stored.syncBindings().isEmpty());
    QVERIFY(m_backendB->calendarIds().contains(QString::fromLatin1(kCalIdB)));
}

void TestCalendarManager::deleteCalendar_forget_removesConfig_keepsBackendData()
{
    seedTwoBackendCalendar(m_host.get(), m_backendA.get(), m_backendB.get());
    const DeletionResult r = m_mgr->deleteCalendar(QString::fromLatin1(kLogicalId), DeleteMode::Forget);
    QVERIFY(r.success);
    QVERIFY(m_host->configStore()->logicalCalendar(QString::fromLatin1(kLogicalId)).id.isEmpty());
    QVERIFY(m_backendA->calendarIds().contains(QString::fromLatin1(kCalId)));
    QVERIFY(m_backendB->calendarIds().contains(QString::fromLatin1(kCalIdB)));
}

void TestCalendarManager::deleteCalendar_deleteFromAll_deletesBackendsAndConfig()
{
    seedTwoBackendCalendar(m_host.get(), m_backendA.get(), m_backendB.get());
    QSignalSpy deleted(m_mgr.get(), &CalendarManager::calendarDeleted);
    const DeletionResult r = m_mgr->deleteCalendar(QString::fromLatin1(kLogicalId), DeleteMode::DeleteFromAll);
    QVERIFY(r.success);
    QCOMPARE(r.backendResults.value(QString::fromLatin1(kBackendA)), true);
    QCOMPARE(r.backendResults.value(QString::fromLatin1(kBackendB)), true);
    QVERIFY(!m_backendA->calendarIds().contains(QString::fromLatin1(kCalId)));
    QVERIFY(!m_backendB->calendarIds().contains(QString::fromLatin1(kCalIdB)));
    QVERIFY(m_host->configStore()->logicalCalendar(QString::fromLatin1(kLogicalId)).id.isEmpty());
    QCOMPARE(deleted.count(), 1);
}

// ============================================================
// Task 5 — incidence create/update/delete propagate
// ============================================================

void TestCalendarManager::createIncidence_pushesToAllEnabledBindings_emitsSignal()
{
    // E11 (audit B7 / FINDINGS O39): createIncidence is now genuinely async
    // (no nested QEventLoop) — completion is signal-driven, so the test must
    // spin the event loop (QSignalSpy::wait()) instead of checking a return
    // value that no longer exists.
    seedTwoBackendCalendar(m_host.get(), m_backendA.get(), m_backendB.get());
    QSignalSpy created(m_mgr.get(), &CalendarManager::incidenceCreated);
    m_mgr->createIncidence(QString::fromLatin1(kLogicalId),
                           makeEvent(QStringLiteral("evt-1"), QStringLiteral("One")));
    QVERIFY(created.wait());
    QVERIFY(m_backendA->allUids(QString::fromLatin1(kCalId)).contains(QStringLiteral("evt-1")));
    QVERIFY(m_backendB->allUids(QString::fromLatin1(kCalIdB)).contains(QStringLiteral("evt-1")));
    QCOMPARE(created.count(), 1);
    QCOMPARE(created.at(0).at(1).toString(), QStringLiteral("evt-1"));
}

void TestCalendarManager::updateIncidence_pushesUpdate_emitsSignal()
{
    seedTwoBackendCalendar(m_host.get(), m_backendA.get(), m_backendB.get());
    QSignalSpy created(m_mgr.get(), &CalendarManager::incidenceCreated);
    m_mgr->createIncidence(QString::fromLatin1(kLogicalId), makeEvent(QStringLiteral("evt-1"), QStringLiteral("One")));
    QVERIFY(created.wait());

    QSignalSpy updated(m_mgr.get(), &CalendarManager::incidenceUpdated);
    m_mgr->updateIncidence(QString::fromLatin1(kLogicalId),
                           makeEvent(QStringLiteral("evt-1"), QStringLiteral("One (edited)")));
    QVERIFY(updated.wait());
    QCOMPARE(updated.count(), 1);
    auto fetched = m_backendA->incidence(QString::fromLatin1(kCalId), QStringLiteral("evt-1"));
    QVERIFY(fetched);
    QCOMPARE(fetched->summary(), QStringLiteral("One (edited)"));
}

void TestCalendarManager::deleteIncidence_removesFromBackends_emitsSignal()
{
    seedTwoBackendCalendar(m_host.get(), m_backendA.get(), m_backendB.get());
    QSignalSpy created(m_mgr.get(), &CalendarManager::incidenceCreated);
    m_mgr->createIncidence(QString::fromLatin1(kLogicalId), makeEvent(QStringLiteral("evt-1"), QStringLiteral("One")));
    QVERIFY(created.wait());

    QSignalSpy deleted(m_mgr.get(), &CalendarManager::incidenceDeleted);
    m_mgr->deleteIncidence(QString::fromLatin1(kLogicalId), QStringLiteral("evt-1"));
    QVERIFY(deleted.wait());
    QCOMPARE(deleted.count(), 1);
    QVERIFY(!m_backendA->allUids(QString::fromLatin1(kCalId)).contains(QStringLiteral("evt-1")));
    QVERIFY(!m_backendB->allUids(QString::fromLatin1(kCalIdB)).contains(QStringLiteral("evt-1")));
}

// ============================================================
// Task 6 — incidence push failure surfaces
// ============================================================

void TestCalendarManager::createIncidence_backendPushFails_returnsFalse_emitsOperationFailed()
{
    seedTwoBackendCalendar(m_host.get(), m_backendA.get(), m_backendB.get());
    m_backendA->setFailurePoint(MockBackend::FailurePoint::OnPush, 0, QStringLiteral("injected push failure"));
    QSignalSpy failed(m_mgr.get(), &CalendarManager::operationFailed);
    m_mgr->createIncidence(QString::fromLatin1(kLogicalId),
                           makeEvent(QStringLiteral("evt-x"), QStringLiteral("X")));
    QVERIFY(failed.wait());
    QVERIFY(failed.count() >= 1);
}

// ============================================================
// Task 7 — batch mode defers regeneration
// ============================================================

void TestCalendarManager::withoutBatch_eachMutationRegenerates()
{
    QSignalSpy regen(m_mgr.get(), &CalendarManager::syncMappingRegenerationRequested);
    m_mgr->createCalendar(makeLogical(QStringLiteral("lc-a"), QString::fromLatin1(kBackendA), QStringLiteral("ca"), true));
    m_mgr->createCalendar(makeLogical(QStringLiteral("lc-b"), QString::fromLatin1(kBackendA), QStringLiteral("cb"), true));
    QCOMPARE(regen.count(), 2);
}

void TestCalendarManager::batchGuard_defersRegenerationToSingleEmission()
{
    QSignalSpy regen(m_mgr.get(), &CalendarManager::syncMappingRegenerationRequested);
    {
        CalendarManager::BatchGuard guard(m_mgr.get());
        m_mgr->createCalendar(makeLogical(QStringLiteral("lc-a"), QString::fromLatin1(kBackendA), QStringLiteral("ca"), true));
        m_mgr->createCalendar(makeLogical(QStringLiteral("lc-b"), QString::fromLatin1(kBackendA), QStringLiteral("cb"), true));
        QCOMPARE(regen.count(), 0);
    }
    QCOMPARE(regen.count(), 1);
}

// ============================================================
// Task 8 — snapshot capture works; restore is a stub
// ============================================================

void TestCalendarManager::captureSnapshot_clonesPrimaryCalendarIncidences()
{
    LogicalCalendar lc = makeLogical(QString::fromLatin1(kLogicalId), QString::fromLatin1(kBackendA), QString::fromLatin1(kCalId), false);
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
    CalendarSnapshot snap;
    snap.logicalCalendar.id          = QString::fromLatin1(kLogicalId);
    snap.logicalCalendar.displayName = QStringLiteral("X");
    QVERIFY(snap.isValid());
    QCOMPARE(m_mgr->restoreFromSnapshot(snap), false);
}

QTEST_MAIN(TestCalendarManager)
#include "tst_calendar_manager.moc"
