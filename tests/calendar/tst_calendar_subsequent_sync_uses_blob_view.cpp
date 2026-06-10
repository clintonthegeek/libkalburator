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
#include "baselinestore.h"
#include "calendar_test_helpers.h"
#include "conflictmanager.h"
#include "mockbackend.h"
#include "pluginmanager.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "syncengine.h"
#include "syncrequest.h"
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

constexpr int kSyncTimeoutMs = 30000;

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

// Helper: build a blob/raw CanonicalRecord for BlobBaselineStore V3 API.
inline Kalburator::Shape::CanonicalRecord makeBlobRec(const QString &uid, const QString &hash)
{
    Kalburator::Shape::CanonicalRecord rec;
    rec.recordId = uid;
    rec.shape    = Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("blob")},
        Kalburator::Shape::EncodingId{QStringLiteral("raw")}};
    rec.data     = hash.toUtf8();
    return rec;
}

} // namespace

class TestCalendarSubsequentSyncUsesBlobView : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
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

    // FINDINGS O13 probe: a calendar record known via baseline that the source
    // deletes must be deleted on the target — i.e. baseline-driven deletion
    // detection is active for the calendar domain, not just blob.
    void subsequentSync_deletedSourceRecordPropagatesDeletion();

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
    std::unique_ptr<Kalburator::Storage::BaselineStore> m_calendarBaselines;
    std::unique_ptr<SyncConflictStore>     m_conflictStore;
    std::unique_ptr<ConflictManager>       m_conflictManager;
    std::unique_ptr<SyncEngine>       m_coordinator;

    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;
};

void TestCalendarSubsequentSyncUsesBlobView::initTestCase() {
    Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
    Kalburator::registerStockPlugins(pm);
}

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
    m_calendarBaselines = std::make_unique<Kalburator::Storage::BaselineStore>(dbPath);
    m_conflictStore     = std::make_unique<SyncConflictStore>(dbPath);

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());
}

void TestCalendarSubsequentSyncUsesBlobView::cleanup()
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

void TestCalendarSubsequentSyncUsesBlobView::setupCoordinator()
{
    m_coordinator = std::make_unique<SyncEngine>(m_registry.get(), m_host.get(), m_shape);
    m_coordinator->setBaselineStore(m_calendarBaselines.get());
    m_coordinator->setSyncConflictStore(m_conflictStore.get());
    m_coordinator->setConflictManager(m_conflictManager.get());
    m_coordinator->setCollection(m_host->stubCollection());
    m_coordinator->setSyncMappings({ makeTwoWayMapping() });
}

bool TestCalendarSubsequentSyncUsesBlobView::runOneSync()
{
    SyncRequest req;
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    auto future = m_coordinator->runSync(req);
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
    m_calendarBaselines->setBaselineV3(QString::fromLatin1(kMappingId),
                                       calendarTestRec(QStringLiteral("evt-1"), icalFor(evt1)));

    m_source->addIncidence(QString::fromLatin1(kCalendarId), evt1);
    m_target->addIncidence(QString::fromLatin1(kCalendarId), evt1);

    // Pre-condition: baselines exist → useQuickPath will be false.
    QVERIFY(!m_calendarBaselines->baselinesForMappingV3(QString::fromLatin1(kMappingId)).isEmpty());

    setupCoordinator();
    m_source->clearOperationLog();
    m_target->clearOperationLog();
    QVERIFY(runOneSync());

    // The blob view (loadRecords) must have been used for actual data.
    QVERIFY2(m_source->operationLog().contains(
                 QStringLiteral("LOAD_RECORDS:") + QString::fromLatin1(kCalendarId)),
             "Expected LOAD_RECORDS in source operation log");
    QVERIFY2(m_target->operationLog().contains(
                 QStringLiteral("LOAD_RECORDS:") + QString::fromLatin1(kCalendarId)),
             "Expected LOAD_RECORDS in target operation log");

    // Phase Ib.5 Task 7 added a fetchItems() cancellation-gating step that
    // runs immediately before loadRecordsOrError() on each side. This means
    // FETCH:calendarId now appears in the log for every sync — it is the
    // cancellation gate, not the legacy data-fetch path. The important
    // invariant is LOAD_RECORDS (above), not the absence of FETCH.
}

void TestCalendarSubsequentSyncUsesBlobView::subsequentSync_hashEqualRecordsAreSkipped()
{
    // Pre-seed two records that are "unchanged" (BlobBaselineStore has matching hash).
    auto evt1 = makeEvent(QStringLiteral("evt-1"), QStringLiteral("Unchanged One"));
    auto evt2 = makeEvent(QStringLiteral("evt-2"), QStringLiteral("Unchanged Two"));

    // Seed BaselineStore so baselinesForMappingV3() returns non-empty.
    m_calendarBaselines->setBaselineV3(QString::fromLatin1(kMappingId),
                                       calendarTestRec(QStringLiteral("evt-1"), icalFor(evt1)));
    m_calendarBaselines->setBaselineV3(QString::fromLatin1(kMappingId),
                                       calendarTestRec(QStringLiteral("evt-2"), icalFor(evt2)));

    // Seed BlobBaselineStore with matching hashes so the hash skip fires.
    const QString hash1 = hashFor(evt1);
    const QString hash2 = hashFor(evt2);
    m_calendarBaselines->setBaselineV3(QString::fromLatin1(kMappingId), makeBlobRec(QStringLiteral("evt-1"), hash1));
    m_calendarBaselines->setBaselineV3(QString::fromLatin1(kMappingId), makeBlobRec(QStringLiteral("evt-2"), hash2));

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
}

void TestCalendarSubsequentSyncUsesBlobView::subsequentSync_modifiedRecordPropagates()
{
    // Two unchanged records + one modified record (no BlobBaseline → not skipped).
    auto evt1 = makeEvent(QStringLiteral("evt-1"), QStringLiteral("Unchanged One"));
    auto evt2 = makeEvent(QStringLiteral("evt-2"), QStringLiteral("Unchanged Two"));
    auto evt3 = makeEvent(QStringLiteral("evt-3"), QStringLiteral("New Record"));

    // BaselineStore: seed baselines for evt-1 and evt-2 only (not evt-3).
    m_calendarBaselines->setBaselineV3(QString::fromLatin1(kMappingId),
                                       calendarTestRec(QStringLiteral("evt-1"), icalFor(evt1)));
    m_calendarBaselines->setBaselineV3(QString::fromLatin1(kMappingId),
                                       calendarTestRec(QStringLiteral("evt-2"), icalFor(evt2)));

    // BlobBaselineStore: matching hashes for evt-1 and evt-2 so they are skipped.
    const QString hash1 = hashFor(evt1);
    const QString hash2 = hashFor(evt2);
    m_calendarBaselines->setBaselineV3(QString::fromLatin1(kMappingId), makeBlobRec(QStringLiteral("evt-1"), hash1));
    m_calendarBaselines->setBaselineV3(QString::fromLatin1(kMappingId), makeBlobRec(QStringLiteral("evt-2"), hash2));
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

void TestCalendarSubsequentSyncUsesBlobView::subsequentSync_deletedSourceRecordPropagatesDeletion()
{
    // evt-1 is kept; evt-2 is deleted on the source after the prior sync.
    auto evt1 = makeEvent(QStringLiteral("evt-1"), QStringLiteral("Keeper"));
    auto evt2 = makeEvent(QStringLiteral("evt-2"), QStringLiteral("Doomed"));

    // Seed blob baselines (the form the unified engine actually persists:
    // domain="blob", encoding="raw", data = contentHash bytes). Both records
    // are "known" from a prior sync.
    m_calendarBaselines->setBaselineV3(QString::fromLatin1(kMappingId),
                                       makeBlobRec(QStringLiteral("evt-1"), hashFor(evt1)));
    m_calendarBaselines->setBaselineV3(QString::fromLatin1(kMappingId),
                                       makeBlobRec(QStringLiteral("evt-2"), hashFor(evt2)));

    // Both sides started in sync with both events...
    m_source->addIncidence(QString::fromLatin1(kCalendarId), evt1);
    m_source->addIncidence(QString::fromLatin1(kCalendarId), evt2);
    m_target->addIncidence(QString::fromLatin1(kCalendarId), evt1);
    m_target->addIncidence(QString::fromLatin1(kCalendarId), evt2);

    // ...then the source deleted evt-2.
    m_source->removeItem(QString::fromLatin1(kCalendarId), QStringLiteral("evt-2"));
    QCOMPARE(sourceUids().size(), 1);
    QCOMPARE(targetUids().size(), 2);

    setupCoordinator();
    QVERIFY(runOneSync());

    // The deletion must propagate to the target (source-absent + target-present
    // + baseline-present, source unchanged → delete, not conflict).
    QCOMPARE(targetUids(), QStringList{ QStringLiteral("evt-1") });
    QCOMPARE(sourceUids(), QStringList{ QStringLiteral("evt-1") });
}

QTEST_MAIN(TestCalendarSubsequentSyncUsesBlobView)
#include "tst_calendar_subsequent_sync_uses_blob_view.moc"
