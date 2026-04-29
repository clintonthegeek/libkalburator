// tst_calendar_first_sync_via_blob_engine.cpp
//
// Phase D Task 21 — first-sync dispatch through BlobSyncEngine.
//
// When a SyncMapping has no CalendarBaselineStore baseline yet AND the mode
// is OneWayUpload, SyncWorker routes through BlobSyncEngine::mirror and then
// harvests the resulting records into CalendarBaselineStore + BlobBaselineStore
// so subsequent syncs use the 3-way merge path.
//
// Design note: only OneWayUpload first-syncs go through BlobSyncEngine; TwoWay
// and OneWayDownload first-syncs remain on the quick-path (2-way diff with no
// baselines) to preserve the transcoding and host-change-tracking code paths.

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>

#include <KCalendarCore/Event>
#include <KCalendarCore/MemoryCalendar>

#include "backendregistry.h"
#include "blobbaselinestore.h"
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
constexpr auto kMappingId       = "mapping-first-sync";

constexpr int kSyncTimeoutMs = 5000;

KCalendarCore::Event::Ptr makeEvent(const QString &uid, const QString &summary)
{
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    event->setUid(uid);
    event->setSummary(summary);
    event->setDtStart(QDateTime::currentDateTimeUtc());
    event->setLastModified(QDateTime::currentDateTimeUtc());
    return event;
}

SyncMapping makeOneWayUploadMapping()
{
    SyncMapping m;
    m.id              = QString::fromLatin1(kMappingId);
    m.sourceBackend   = QString::fromLatin1(kSourceBackendId);
    m.sourceCalendar  = QString::fromLatin1(kCalendarId);
    m.targetBackend   = QString::fromLatin1(kTargetBackendId);
    m.targetCalendar  = QString::fromLatin1(kCalendarId);
    m.mode            = SyncMode::OneWayUpload;
    m.conflictPolicy  = ConflictResolution::SourceWins;
    m.enabled         = true;
    return m;
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
    m.conflictPolicy  = ConflictResolution::SourceWins;
    m.enabled         = true;
    return m;
}

} // namespace

class TestCalendarFirstSyncViaBlobEngine : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // OneWayUpload first-sync routes through BlobSyncEngine::mirror and seeds
    // CalendarBaselineStore + BlobBaselineStore.
    void firstSync_oneWayUpload_dispatchesViaBlobEngine_andHarvestsBaselines();

    // Mirror direction: source gets copied to target.
    void firstSync_oneWayUpload_mirrorsSourceToTarget();

    // TwoWay first-sync still works via the old quick-path (NOT BlobSyncEngine),
    // ensuring TwoWay converges correctly on first sync.
    void firstSync_twoWay_usesOldQuickPathAndConverges();

private:
    bool runOneSync(SyncCoordinator::SyncBehavior behavior = SyncCoordinator::SyncBehavior::Unmonitored);
    QStringList sourceUids() const;
    QStringList targetUids() const;
    void setupCoordinator(const QList<SyncMapping> &mappings);

    std::unique_ptr<QTemporaryDir>         m_tmpDir;
    std::unique_ptr<BackendRegistry>       m_registry;
    std::unique_ptr<MockBackend>           m_source;
    std::unique_ptr<MockBackend>           m_target;
    std::unique_ptr<StubSyncHost>          m_host;
    std::unique_ptr<CalendarBaselineStore> m_calendarBaselines;
    std::unique_ptr<BlobBaselineStore>     m_blobBaselines;
    std::unique_ptr<SyncConflictStore>     m_conflictStore;
    std::unique_ptr<ConflictManager>       m_conflictManager;
    std::unique_ptr<SyncCoordinator>       m_coordinator;
};

// ---- Lifecycle ------------------------------------------------------------

void TestCalendarFirstSyncViaBlobEngine::initTestCase() {}
void TestCalendarFirstSyncViaBlobEngine::cleanupTestCase() {}

void TestCalendarFirstSyncViaBlobEngine::init()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());

    m_registry = std::make_unique<BackendRegistry>();
    m_source   = std::make_unique<MockBackend>(QString::fromLatin1(kSourceBackendId));
    m_target   = std::make_unique<MockBackend>(QString::fromLatin1(kTargetBackendId));
    m_registry->registerBackendInstance(QString::fromLatin1(kSourceBackendId), m_source.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kTargetBackendId), m_target.get());

    m_source->createCalendar(QString::fromLatin1(kCollectionId),
                             QString::fromLatin1(kCalendarId),
                             QStringLiteral("Calendar 1"));
    m_target->createCalendar(QString::fromLatin1(kCollectionId),
                             QString::fromLatin1(kCalendarId),
                             QStringLiteral("Calendar 1"));

    m_host = std::make_unique<StubSyncHost>(m_registry.get());
    auto *hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal->setId(QString::fromLatin1(kCalendarId));
    m_host->stubCollection()->addCalendarWithId(QString::fromLatin1(kCalendarId), hostCal);

    const QString dbPath = m_tmpDir->filePath(QStringLiteral(".kalburator-sync.db"));
    m_calendarBaselines = std::make_unique<CalendarBaselineStore>(dbPath);
    m_blobBaselines     = std::make_unique<BlobBaselineStore>(dbPath);
    m_conflictStore     = std::make_unique<SyncConflictStore>(dbPath);

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());
}

void TestCalendarFirstSyncViaBlobEngine::cleanup()
{
    m_coordinator.reset();
    m_conflictManager.reset();
    m_conflictStore.reset();
    m_blobBaselines.reset();
    m_calendarBaselines.reset();
    m_host.reset();
    m_target.reset();
    m_source.reset();
    m_registry.reset();
    m_tmpDir.reset();
}

void TestCalendarFirstSyncViaBlobEngine::setupCoordinator(const QList<SyncMapping> &mappings)
{
    m_coordinator = std::make_unique<SyncCoordinator>(m_registry.get(), m_host.get());
    m_coordinator->setCalendarBaselineStore(m_calendarBaselines.get());
    m_coordinator->setBlobBaselineStore(m_blobBaselines.get());
    m_coordinator->setSyncConflictStore(m_conflictStore.get());
    m_coordinator->setConflictManager(m_conflictManager.get());
    m_coordinator->setCollection(m_host->stubCollection());
    m_coordinator->setSyncMappings(mappings);
}

bool TestCalendarFirstSyncViaBlobEngine::runOneSync(SyncCoordinator::SyncBehavior behavior)
{
    QSignalSpy allDoneSpy(m_coordinator.get(), &SyncCoordinator::allSyncsCompleted);
    m_coordinator->runSync(behavior);
    if (!allDoneSpy.wait(kSyncTimeoutMs)) {
        qWarning() << "allSyncsCompleted did not fire within" << kSyncTimeoutMs << "ms";
        return false;
    }
    return true;
}

QStringList TestCalendarFirstSyncViaBlobEngine::sourceUids() const
{
    return m_source->allUids(QString::fromLatin1(kCalendarId));
}

QStringList TestCalendarFirstSyncViaBlobEngine::targetUids() const
{
    return m_target->allUids(QString::fromLatin1(kCalendarId));
}

// ---- Tests ----------------------------------------------------------------

void TestCalendarFirstSyncViaBlobEngine::firstSync_oneWayUpload_dispatchesViaBlobEngine_andHarvestsBaselines()
{
    // Pre-condition: no baselines for this mapping.
    QVERIFY(!m_calendarBaselines->hasBaselines(QString::fromLatin1(kMappingId)));

    // Source has 3 events, target is empty.
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-1"), QStringLiteral("Alpha")));
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-2"), QStringLiteral("Beta")));
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-3"), QStringLiteral("Gamma")));

    QCOMPARE(sourceUids().size(), 3);
    QVERIFY(targetUids().isEmpty());

    setupCoordinator({ makeOneWayUploadMapping() });

    QSignalSpy completedSpy(m_coordinator.get(), &SyncCoordinator::syncCompleted);
    QVERIFY(runOneSync());

    // syncCompleted fired exactly once.
    QCOMPARE(completedSpy.count(), 1);

    // Target received the 3 events via BlobSyncEngine::mirror.
    QCOMPARE(targetUids().size(), 3);
    QVERIFY(targetUids().contains(QStringLiteral("evt-1")));
    QVERIFY(targetUids().contains(QStringLiteral("evt-2")));
    QVERIFY(targetUids().contains(QStringLiteral("evt-3")));

    // CalendarBaselineStore was seeded: hasBaselines returns true and 3 entries.
    QVERIFY(m_calendarBaselines->hasBaselines(QString::fromLatin1(kMappingId)));
    QCOMPARE(m_calendarBaselines->allBaselines(QString::fromLatin1(kMappingId)).size(), 3);

    // BlobBaselineStore was seeded: each source record has a stored hash.
    QVERIFY(!m_blobBaselines->baselineHash(
        QString::fromLatin1(kSourceBackendId),
        QString::fromLatin1(kCalendarId),
        QStringLiteral("evt-1")).isEmpty());
    QVERIFY(!m_blobBaselines->baselineHash(
        QString::fromLatin1(kSourceBackendId),
        QString::fromLatin1(kCalendarId),
        QStringLiteral("evt-2")).isEmpty());
    QVERIFY(!m_blobBaselines->baselineHash(
        QString::fromLatin1(kSourceBackendId),
        QString::fromLatin1(kCalendarId),
        QStringLiteral("evt-3")).isEmpty());
}

void TestCalendarFirstSyncViaBlobEngine::firstSync_oneWayUpload_mirrorsSourceToTarget()
{
    // Source has 2 events, target is empty. Mirror should copy source → target.
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("src-1"), QStringLiteral("Source One")));
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("src-2"), QStringLiteral("Source Two")));

    setupCoordinator({ makeOneWayUploadMapping() });
    QVERIFY(runOneSync());

    // Source unchanged; target now mirrors source.
    QCOMPARE(sourceUids().size(), 2);
    QCOMPARE(targetUids().size(), 2);
    QVERIFY(targetUids().contains(QStringLiteral("src-1")));
    QVERIFY(targetUids().contains(QStringLiteral("src-2")));

    // Source-to-target direction: host additions should be 0 (BlobSyncEngine
    // writes directly to backend, bypassing the host channel which is reserved
    // for target→host model propagation).
    QCOMPARE(m_host->appliedAdditionCount(), 0);
}

void TestCalendarFirstSyncViaBlobEngine::firstSync_twoWay_usesOldQuickPathAndConverges()
{
    // TwoWay first-sync does NOT route through BlobSyncEngine (to preserve
    // transcoding and host change-tracking). It uses the existing quick-path.
    // Verify that TwoWay first-sync still converges correctly.
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("src-A"), QStringLiteral("From Source")));
    m_target->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("tgt-B"), QStringLiteral("From Target")));

    setupCoordinator({ makeTwoWayMapping() });
    QVERIFY(runOneSync());

    // Both sides should converge to {src-A, tgt-B}.
    QCOMPARE(sourceUids().size(), 2);
    QCOMPARE(targetUids().size(), 2);
    QVERIFY(sourceUids().contains(QStringLiteral("src-A")));
    QVERIFY(sourceUids().contains(QStringLiteral("tgt-B")));
    QVERIFY(targetUids().contains(QStringLiteral("src-A")));
    QVERIFY(targetUids().contains(QStringLiteral("tgt-B")));
}

QTEST_MAIN(TestCalendarFirstSyncViaBlobEngine)
#include "tst_calendar_first_sync_via_blob_engine.moc"
