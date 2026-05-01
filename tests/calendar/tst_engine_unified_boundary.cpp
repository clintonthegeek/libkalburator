/// F1 Task 7 — engine boundary integration test
///
/// Pins SyncEngine as the unified entry point for both calendar and
/// blob domains. Three test methods:
///
///  1. `runSync_calendarMapping_propagatesEvents` — drives a calendar
///     mapping through `SyncEngine::runSync()` (CalendarDomainAdapter
///     under the hood); confirms the per-mapping flow still works
///     after Tasks 4-5 collapsed `SyncCoordinator` and routed
///     through the adapter.
///  2. `runBlobTwoWay_propagatesRecordsAndCommitsBaselines` — drives
///     the blob one-shot facade (BlobDomainAdapter under the hood)
///     directly on the same SyncEngine class; confirms the BlobDomain
///     seam is reachable from the engine boundary.
///  3. `mixedDomains_oneEngineDrivesBoth` — single SyncEngine
///     instance runs a calendar sync and a blob mirror back-to-back
///     without interference.
///
/// The "registerAdapter() / synthetic SyncMapping with domain='blob'"
/// path the design envisages for Phase G is intentionally out of
/// scope here: F1's contract is that both domains reach the engine
/// through clean entry points (calendar via `runSync`, blob via the
/// one-shot facade). Phase G unifies these into a single
/// `runSync` driven by mapping-domain dispatch.

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <QTimeZone>

#include <KCalendarCore/Event>
#include <KCalendarCore/MemoryCalendar>

#include "backendregistry.h"
#include "blobbaselinestore.h"
#include "calendarbaselinestore.h"
#include "conflicthandlerregistry.h"
#include "conflictmanager.h"
#include "conflictpolicy.h"
#include "conflictstore.h"
#include "mockbackend.h"
#include "mockblobbackend.h"
#include "syncconflictstore.h"
#include "syncengine.h"
#include "synctypes.h"

#include "stubs/stubsynchost.h"

using namespace Kalburator::Sync;
using namespace Kalburator::Sync::Test;

namespace {

constexpr auto kSourceBackendId = "source-mock";
constexpr auto kTargetBackendId = "target-mock";
constexpr auto kCollectionId    = "stub-collection";
constexpr auto kCalendarId      = "calendar-1";
constexpr auto kMappingId       = "mapping-cal";
constexpr auto kBlobMappingId   = "mapping-blob";
constexpr auto kBlobCollection  = "memos";

constexpr int kSyncTimeoutMs = 5000;

KCalendarCore::Event::Ptr makeEvent(const QString &uid, const QString &summary)
{
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    event->setUid(uid);
    event->setSummary(summary);
    event->setDtStart(QDateTime::currentDateTimeUtc());
    return event;
}

SyncMapping makeCalendarMapping()
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

BackendRecord makeBlobRecord(const QString &id, const QString &data)
{
    BackendRecord r;
    r.id = id;
    r.type = QStringLiteral("memo");
    r.displayName = id;
    r.data = data.toUtf8();
    r.contentHash = QStringLiteral("hash-of-%1").arg(data);
    r.lastModified = QDateTime::currentDateTimeUtc();
    return r;
}

CollectionInfo makeBlobCollectionInfo(const QString &id)
{
    CollectionInfo c;
    c.id = id;
    c.name = id;
    c.type = QStringLiteral("memos");
    return c;
}

} // namespace

class TestEngineUnifiedBoundary : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void runSync_calendarMapping_propagatesEvents();
    void runBlobTwoWay_propagatesRecordsAndCommitsBaselines();
    void mixedDomains_oneEngineDrivesBoth();

private:
    bool runOneCalendarSync();
    void seedCalendarFixtures();

    std::unique_ptr<QTemporaryDir>         m_tmpDir;
    std::unique_ptr<BackendRegistry>       m_registry;
    std::unique_ptr<MockBackend>           m_calSource;
    std::unique_ptr<MockBackend>           m_calTarget;
    std::unique_ptr<StubSyncHost>          m_host;
    std::unique_ptr<CalendarBaselineStore> m_calendarBaselines;
    std::unique_ptr<SyncConflictStore>     m_conflictStore;
    std::unique_ptr<ConflictManager>       m_conflictManager;
    std::unique_ptr<SyncEngine>            m_engine;
};

void TestEngineUnifiedBoundary::seedCalendarFixtures()
{
    m_calSource->createCalendar(QString::fromLatin1(kCollectionId),
                                QString::fromLatin1(kCalendarId),
                                QStringLiteral("Calendar 1"));
    m_calTarget->createCalendar(QString::fromLatin1(kCollectionId),
                                QString::fromLatin1(kCalendarId),
                                QStringLiteral("Calendar 1"));

    auto *hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal->setId(QString::fromLatin1(kCalendarId));
    m_host->stubCollection()->addCalendarWithId(QString::fromLatin1(kCalendarId),
                                                 hostCal);
}

void TestEngineUnifiedBoundary::init()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());

    m_registry = std::make_unique<BackendRegistry>();
    m_calSource = std::make_unique<MockBackend>();
    m_calTarget = std::make_unique<MockBackend>();
    m_registry->registerBackendInstance(QString::fromLatin1(kSourceBackendId),
                                        m_calSource.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kTargetBackendId),
                                        m_calTarget.get());

    m_host = std::make_unique<StubSyncHost>(m_registry.get());
    seedCalendarFixtures();

    const QString dbPath = m_tmpDir->filePath(QStringLiteral(".kalburator-sync.db"));
    m_calendarBaselines = std::make_unique<CalendarBaselineStore>(dbPath);
    m_conflictStore     = std::make_unique<SyncConflictStore>(dbPath);

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());

    m_engine = std::make_unique<SyncEngine>(m_registry.get(), m_host.get());
    m_engine->setCalendarBaselineStore(m_calendarBaselines.get());
    m_engine->setSyncConflictStore(m_conflictStore.get());
    m_engine->setConflictManager(m_conflictManager.get());
    m_engine->setCollection(m_host->stubCollection());
    m_engine->setSyncMappings({ makeCalendarMapping() });
}

void TestEngineUnifiedBoundary::cleanup()
{
    m_engine.reset();
    m_conflictManager.reset();
    m_conflictStore.reset();
    m_calendarBaselines.reset();
    m_host.reset();
    m_calTarget.reset();
    m_calSource.reset();
    m_registry.reset();
    m_tmpDir.reset();
}

bool TestEngineUnifiedBoundary::runOneCalendarSync()
{
    auto future = m_engine->runSyncFuture(
        SyncEngine::SyncBehavior::Unmonitored);
    int waited = 0;
    while (!future.isFinished() && waited < kSyncTimeoutMs) {
        QTest::qWait(10);
        waited += 10;
    }
    if (!future.isFinished()) {
        qWarning() << "runSyncFuture did not finish within"
                   << kSyncTimeoutMs << "ms";
        return false;
    }
    if (future.isCanceled()) {
        qWarning() << "runSyncFuture was canceled unexpectedly";
        return false;
    }
    return true;
}

// ---- Tests ---------------------------------------------------------------

void TestEngineUnifiedBoundary::runSync_calendarMapping_propagatesEvents()
{
    // Source has two events, target empty. After runSync, both
    // sides should converge — proving the calendar adapter is wired
    // and exercised end-to-end through the SyncEngine boundary.
    m_calSource->addIncidence(QString::fromLatin1(kCalendarId),
                              makeEvent(QStringLiteral("evt-1"),
                                        QStringLiteral("Event One")));
    m_calSource->addIncidence(QString::fromLatin1(kCalendarId),
                              makeEvent(QStringLiteral("evt-2"),
                                        QStringLiteral("Event Two")));

    QSignalSpy startedSpy(m_engine.get(), &SyncEngine::syncStarted);

    QVERIFY(runOneCalendarSync());

    QVERIFY(startedSpy.size() >= 1);
    // Completion is observed via QFuture::isFinished inside
    // runOneCalendarSync (per-mapping syncCompleted signal retired
    // in F2 Task 42).

    QCOMPARE(m_calSource->allUids(QString::fromLatin1(kCalendarId)).size(), 2);
    QCOMPARE(m_calTarget->allUids(QString::fromLatin1(kCalendarId)).size(), 2);
}

void TestEngineUnifiedBoundary::runBlobTwoWay_propagatesRecordsAndCommitsBaselines()
{
    using namespace Kalburator::Sync::QSyncCore;

    MockBlobBackend a, b;
    a.createCollection(makeBlobCollectionInfo(QString::fromLatin1(kBlobCollection)));
    b.createCollection(makeBlobCollectionInfo(QString::fromLatin1(kBlobCollection)));

    a.createRecord(QString::fromLatin1(kBlobCollection),
                   makeBlobRecord(QStringLiteral("blob-a"),
                                   QStringLiteral("payload-a")));
    b.createRecord(QString::fromLatin1(kBlobCollection),
                   makeBlobRecord(QStringLiteral("blob-b"),
                                   QStringLiteral("payload-b")));

    BlobBaselineStore baseline(
        m_tmpDir->filePath(QStringLiteral(".blob-baseline.db")));

    ConflictHandlerRegistry handlers;
    ConflictStore conflicts;
    ConflictPolicy policy;

    const auto r = m_engine->runBlobTwoWay(
        &a, &b,
        QString::fromLatin1(kBlobCollection),
        QString::fromLatin1(kBlobMappingId),
        &baseline, &handlers, &conflicts, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));

    // After two-way sync, both sides should hold both records.
    QCOMPARE(a.loadRecords(QString::fromLatin1(kBlobCollection)).size(), 2);
    QCOMPARE(b.loadRecords(QString::fromLatin1(kBlobCollection)).size(), 2);

    // G.4: baselines now keyed by (mapping_id, record_id) in blob_baselines_v3.
    const auto persisted = baseline.baselinesForMappingV3(
        QString::fromLatin1(kBlobMappingId));
    QCOMPARE(persisted.size(), 2);
}

void TestEngineUnifiedBoundary::mixedDomains_oneEngineDrivesBoth()
{
    // Drive the calendar path first.
    m_calSource->addIncidence(QString::fromLatin1(kCalendarId),
                              makeEvent(QStringLiteral("evt-cal"),
                                        QStringLiteral("Calendar Event")));
    QVERIFY(runOneCalendarSync());
    QCOMPARE(m_calTarget->allUids(QString::fromLatin1(kCalendarId)).size(), 1);

    // Then drive the blob path on the same engine instance.
    MockBlobBackend src, tgt;
    src.createCollection(makeBlobCollectionInfo(QString::fromLatin1(kBlobCollection)));
    tgt.createCollection(makeBlobCollectionInfo(QString::fromLatin1(kBlobCollection)));
    src.createRecord(QString::fromLatin1(kBlobCollection),
                     makeBlobRecord(QStringLiteral("memo-1"),
                                     QStringLiteral("note")));

    const auto mirrorResult = m_engine->runBlobMirror(
        &src, &tgt, QString::fromLatin1(kBlobCollection));

    QVERIFY2(mirrorResult.success, qUtf8Printable(mirrorResult.errorMessage));
    QCOMPARE(mirrorResult.targetStats.created, 1);
    QCOMPARE(tgt.loadRecords(QString::fromLatin1(kBlobCollection)).size(), 1);

    // The calendar side is undisturbed by the blob run.
    QCOMPARE(m_calTarget->allUids(QString::fromLatin1(kCalendarId)).size(), 1);
}

QTEST_MAIN(TestEngineUnifiedBoundary)
#include "tst_engine_unified_boundary.moc"
