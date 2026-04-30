// tst_calendar_subsequent_sync_uses_blob_view.cpp
//
// Phase D Task 22 — pins the subsequent-sync code path.
//
// When CalendarBaselineStore has baselines for the mapping (useQuickPath=false),
// SyncEngine calls IBlobBackend::loadRecords (the blob view), not the legacy
// SyncBackend::fetchItems.  All records are fetched so computeSyncDiff can
// correctly distinguish unchanged records from deletions.  Records whose
// contentHash matches the BlobBaselineStore baseline generate no write
// operations because computeSyncDiff treats them as unchanged vs. the baseline.
//
// Key assertions:
//   - MockBackend operationLog contains "LOAD_RECORDS" (blob view was used)
//   - MockBackend operationLog does NOT contain "FETCH" (old fetchItems not called)
//   - Records whose contentHash matches the stored BlobBaselineStore baseline
//     generate no calendar-merge write operations.
//   - Records whose contentHash differs from the baseline propagate correctly.

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include "backendregistry.h"
#include "blobbaselinestore.h"
#include "calendarbaselinestore.h"
#include "conflictmanager.h"
#include "mockbackend.h"
#include "syncengine.h"
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
constexpr auto kMappingId       = "mapping-subsequent";

constexpr int kSyncTimeoutMs = 5000;

KCalendarCore::Event::Ptr makeEvent(const QString &uid, const QString &summary,
                                     const QDateTime &lastModified = QDateTime())
{
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    event->setUid(uid);
    event->setSummary(summary);
    event->setDtStart(QDateTime::currentDateTimeUtc());
    if (lastModified.isValid()) {
        event->setLastModified(lastModified);
    } else {
        event->setLastModified(QDateTime::currentDateTimeUtc());
    }
    return event;
}

QString icalFor(const KCalendarCore::Incidence::Ptr &inc)
{
    return KCalendarCore::ICalFormat().toICalString(inc);
}

QString hashFor(const KCalendarCore::Incidence::Ptr &inc)
{
    // Must match MockBackend::computeHash (SHA-256 first 16 hex digits of toICalString)
    KCalendarCore::ICalFormat format;
    const QString ical = format.toICalString(inc);
    const QByteArray hash = QCryptographicHash::hash(ical.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex());  // full hex (MockBackend uses full hex for contentHash)
}

SyncMapping makeTwoWayMapping()
{
    SyncMapping m;
    m.id             = QString::fromLatin1(kMappingId);
    m.sourceBackend  = QString::fromLatin1(kSourceBackendId);
    m.sourceCalendar = QString::fromLatin1(kCalendarId);
    m.targetBackend  = QString::fromLatin1(kTargetBackendId);
    m.targetCalendar = QString::fromLatin1(kCalendarId);
    m.mode           = SyncMode::TwoWay;
    m.conflictPolicy = ConflictResolution::SourceWins;
    m.enabled        = true;
    return m;
}

} // namespace

class TestCalendarSubsequentSyncUsesBlobView : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // SyncEngine calls IBlobBackend::loadRecords (not fetchItems) for the
    // subsequent-sync path (useQuickPath = false).
    void subsequentSync_usesBlobViewNotFetchItems();

    // Per-record hash skip: records whose contentHash matches the stored
    // BlobBaselineStore baseline are elided before calendar-level merge.
    void subsequentSync_hashEqualRecordsAreSkipped();

    // Modified record (hash differs from baseline) propagates correctly.
    void subsequentSync_modifiedRecordPropagates();

private:
    bool runOneSync();
    void setupCoordinator();
    QStringList sourceUids() const;
    QStringList targetUids() const;

    std::unique_ptr<QTemporaryDir>         m_tmpDir;
    std::unique_ptr<BackendRegistry>       m_registry;
    std::unique_ptr<MockBackend>           m_source;
    std::unique_ptr<MockBackend>           m_target;
    std::unique_ptr<StubSyncHost>          m_host;
    std::unique_ptr<CalendarBaselineStore> m_calendarBaselines;
    std::unique_ptr<BlobBaselineStore>     m_blobBaselines;
    std::unique_ptr<SyncConflictStore>     m_conflictStore;
    std::unique_ptr<ConflictManager>       m_conflictManager;
    std::unique_ptr<SyncEngine>       m_coordinator;
};

void TestCalendarSubsequentSyncUsesBlobView::initTestCase() {}
void TestCalendarSubsequentSyncUsesBlobView::cleanupTestCase() {}

void TestCalendarSubsequentSyncUsesBlobView::init()
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

void TestCalendarSubsequentSyncUsesBlobView::cleanup()
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

void TestCalendarSubsequentSyncUsesBlobView::setupCoordinator()
{
    m_coordinator = std::make_unique<SyncEngine>(m_registry.get(), m_host.get());
    m_coordinator->setCalendarBaselineStore(m_calendarBaselines.get());
    m_coordinator->setBlobBaselineStore(m_blobBaselines.get());
    m_coordinator->setSyncConflictStore(m_conflictStore.get());
    m_coordinator->setConflictManager(m_conflictManager.get());
    m_coordinator->setCollection(m_host->stubCollection());
    m_coordinator->setSyncMappings({ makeTwoWayMapping() });
}

bool TestCalendarSubsequentSyncUsesBlobView::runOneSync()
{
    auto future = m_coordinator->runSyncFuture(
        SyncEngine::SyncBehavior::Unmonitored);
    int waited = 0;
    while (!future.isFinished() && waited < kSyncTimeoutMs) {
        QTest::qWait(10);
        waited += 10;
    }
    if (!future.isFinished()) {
        qWarning() << "runSyncFuture did not finish within" << kSyncTimeoutMs << "ms";
        return false;
    }
    if (future.isCanceled()) {
        qWarning() << "runSyncFuture was canceled unexpectedly";
        return false;
    }
    return true;
}

QStringList TestCalendarSubsequentSyncUsesBlobView::sourceUids() const
{
    return m_source->allUids(QString::fromLatin1(kCalendarId));
}

QStringList TestCalendarSubsequentSyncUsesBlobView::targetUids() const
{
    return m_target->allUids(QString::fromLatin1(kCalendarId));
}

// ---- Tests ----------------------------------------------------------------

void TestCalendarSubsequentSyncUsesBlobView::subsequentSync_usesBlobViewNotFetchItems()
{
    // Pre-seed baselines so useQuickPath = false (subsequent sync path).
    auto evt1 = makeEvent(QStringLiteral("evt-1"), QStringLiteral("Alpha"));
    m_calendarBaselines->setBaselines(QString::fromLatin1(kMappingId),
                                      {{ QStringLiteral("evt-1"), icalFor(evt1) }});

    m_source->addIncidence(QString::fromLatin1(kCalendarId), evt1);
    m_target->addIncidence(QString::fromLatin1(kCalendarId), evt1);

    // Pre-condition: baselines exist → useQuickPath will be false.
    QVERIFY(m_calendarBaselines->hasBaselines(QString::fromLatin1(kMappingId)));

    setupCoordinator();
    m_source->clearOperationLog();
    m_target->clearOperationLog();
    QVERIFY(runOneSync());

    // The blob view (loadRecords) must have been used, NOT legacy fetchItems.
    QVERIFY2(m_source->operationLog().contains(
                 QStringLiteral("LOAD_RECORDS:") + QString::fromLatin1(kCalendarId)),
             "Expected LOAD_RECORDS in source operation log");
    QVERIFY2(m_target->operationLog().contains(
                 QStringLiteral("LOAD_RECORDS:") + QString::fromLatin1(kCalendarId)),
             "Expected LOAD_RECORDS in target operation log");

    // The old calendar-typed fetchItems must NOT have been called.
    const QStringList srcLog = m_source->operationLog();
    for (const QString &entry : srcLog) {
        QVERIFY2(!entry.startsWith(QStringLiteral("FETCH:")),
                 qPrintable(QStringLiteral("Unexpected FETCH in source log: %1").arg(entry)));
    }
    const QStringList tgtLog = m_target->operationLog();
    for (const QString &entry : tgtLog) {
        QVERIFY2(!entry.startsWith(QStringLiteral("FETCH:")),
                 qPrintable(QStringLiteral("Unexpected FETCH in target log: %1").arg(entry)));
    }
}

void TestCalendarSubsequentSyncUsesBlobView::subsequentSync_hashEqualRecordsAreSkipped()
{
    // Pre-seed two records that are "unchanged" (BlobBaselineStore has matching hash).
    auto evt1 = makeEvent(QStringLiteral("evt-1"), QStringLiteral("Unchanged One"));
    auto evt2 = makeEvent(QStringLiteral("evt-2"), QStringLiteral("Unchanged Two"));

    // Seed CalendarBaselineStore so hasBaselines() returns true.
    m_calendarBaselines->setBaselines(QString::fromLatin1(kMappingId), {
        { QStringLiteral("evt-1"), icalFor(evt1) },
        { QStringLiteral("evt-2"), icalFor(evt2) },
    });

    // Seed BlobBaselineStore with matching hashes so the hash skip fires.
    const QString hash1 = hashFor(evt1);
    const QString hash2 = hashFor(evt2);
    m_blobBaselines->setBaseline(QString::fromLatin1(kSourceBackendId),
                                  QString::fromLatin1(kCalendarId),
                                  QStringLiteral("evt-1"), hash1);
    m_blobBaselines->setBaseline(QString::fromLatin1(kSourceBackendId),
                                  QString::fromLatin1(kCalendarId),
                                  QStringLiteral("evt-2"), hash2);
    m_blobBaselines->setBaseline(QString::fromLatin1(kTargetBackendId),
                                  QString::fromLatin1(kCalendarId),
                                  QStringLiteral("evt-1"), hash1);
    m_blobBaselines->setBaseline(QString::fromLatin1(kTargetBackendId),
                                  QString::fromLatin1(kCalendarId),
                                  QStringLiteral("evt-2"), hash2);

    m_source->addIncidence(QString::fromLatin1(kCalendarId), evt1);
    m_source->addIncidence(QString::fromLatin1(kCalendarId), evt2);
    m_target->addIncidence(QString::fromLatin1(kCalendarId), evt1);
    m_target->addIncidence(QString::fromLatin1(kCalendarId), evt2);

    setupCoordinator();
    QVERIFY(runOneSync());

    // Source and target still have the same 2 records — no changes were applied.
    QCOMPARE(sourceUids().size(), 2);
    QCOMPARE(targetUids().size(), 2);

    // No changes should have been pushed (hash skip elided both records).
    // PUSH operations would appear if any merge changes were applied.
    const QStringList tgtLog = m_target->operationLog();
    for (const QString &entry : tgtLog) {
        QVERIFY2(!entry.startsWith(QStringLiteral("PUSH:")),
                 qPrintable(QStringLiteral("Unexpected PUSH in target log (hash-equal records should be skipped): %1").arg(entry)));
    }
    QCOMPARE(m_host->appliedAdditionCount(), 0);
    QCOMPARE(m_host->appliedUpdateCount(),   0);
    QCOMPARE(m_host->appliedRemovalCount(),  0);
}

void TestCalendarSubsequentSyncUsesBlobView::subsequentSync_modifiedRecordPropagates()
{
    // Two unchanged records + one modified record (no BlobBaseline → not skipped).
    auto evt1 = makeEvent(QStringLiteral("evt-1"), QStringLiteral("Unchanged One"));
    auto evt2 = makeEvent(QStringLiteral("evt-2"), QStringLiteral("Unchanged Two"));
    auto evt3 = makeEvent(QStringLiteral("evt-3"), QStringLiteral("New Record"));

    // CalendarBaselineStore: seed baselines for evt-1 and evt-2 only (not evt-3).
    m_calendarBaselines->setBaselines(QString::fromLatin1(kMappingId), {
        { QStringLiteral("evt-1"), icalFor(evt1) },
        { QStringLiteral("evt-2"), icalFor(evt2) },
    });

    // BlobBaselineStore: matching hashes for evt-1 and evt-2 so they are skipped.
    const QString hash1 = hashFor(evt1);
    const QString hash2 = hashFor(evt2);
    m_blobBaselines->setBaseline(QString::fromLatin1(kSourceBackendId),
                                  QString::fromLatin1(kCalendarId),
                                  QStringLiteral("evt-1"), hash1);
    m_blobBaselines->setBaseline(QString::fromLatin1(kSourceBackendId),
                                  QString::fromLatin1(kCalendarId),
                                  QStringLiteral("evt-2"), hash2);
    m_blobBaselines->setBaseline(QString::fromLatin1(kTargetBackendId),
                                  QString::fromLatin1(kCalendarId),
                                  QStringLiteral("evt-1"), hash1);
    m_blobBaselines->setBaseline(QString::fromLatin1(kTargetBackendId),
                                  QString::fromLatin1(kCalendarId),
                                  QStringLiteral("evt-2"), hash2);
    // No BlobBaseline for evt-3 → it is NOT skipped → goes through merge.

    // Source: all three events. Target: only evt-1 and evt-2.
    m_source->addIncidence(QString::fromLatin1(kCalendarId), evt1);
    m_source->addIncidence(QString::fromLatin1(kCalendarId), evt2);
    m_source->addIncidence(QString::fromLatin1(kCalendarId), evt3);
    m_target->addIncidence(QString::fromLatin1(kCalendarId), evt1);
    m_target->addIncidence(QString::fromLatin1(kCalendarId), evt2);

    QCOMPARE(sourceUids().size(), 3);
    QCOMPARE(targetUids().size(), 2);

    setupCoordinator();
    QVERIFY(runOneSync());

    // evt-3 was new on source (not in target, not in baseline) → created on target.
    QCOMPARE(targetUids().size(), 3);
    QVERIFY(targetUids().contains(QStringLiteral("evt-3")));

    // Source unchanged: still 3 records.
    QCOMPARE(sourceUids().size(), 3);
}

QTEST_MAIN(TestCalendarSubsequentSyncUsesBlobView)
#include "tst_calendar_subsequent_sync_uses_blob_view.moc"
