// tst_calendar_recordchanged_notifications.cpp
//
// Closes the ISyncHost::recordChanged() gap (PlanStan
// docs/todo/sync-apply-phase-model-refresh.md): the engine's apply phase
// now invokes ISyncHost::recordChanged() for every create/update/delete it
// materializes onto the mapping's SOURCE side (the primary/local side in a
// PlanStan-style local-mirror mapping). Consumers like PlanStan's
// CollectionController::recordChanged always re-read from the source side
// regardless of which side actually changed, so only source-side writes are
// reported — target-side writes (a local push out to a remote spoke) are not
// notified, since notifying there would just cause a redundant re-read of
// data the consumer already has correct.
//
// Key assertions:
//   - A record new on the target (not on source, no baseline) is created on
//     source and reported via recordChanged(Created).
//   - A record modified on the target propagates to source and is reported
//     via recordChanged(Updated).
//   - A record deleted on the target propagates as a delete on source and is
//     reported via recordChanged(Deleted).
//   - A record new on the SOURCE that gets pushed to target does NOT trigger
//     any recordChanged call (target-side apply is not notified).

#include <QtTest/QtTest>
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
constexpr auto kMappingId       = "mapping-recordchanged";

constexpr int kSyncTimeoutMs = 30000;

KCalendarCore::Event::Ptr makeEvent(const QString &uid, const QString &summary)
{
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    event->setUid(uid);
    event->setSummary(summary);
    event->setDtStart(QDateTime::currentDateTimeUtc());
    event->setLastModified(QDateTime::currentDateTimeUtc());
    return event;
}

QString icalFor(const KCalendarCore::Incidence::Ptr &inc)
{
    return KCalendarCore::ICalFormat().toICalString(inc);
}

QString hashFor(const KCalendarCore::Incidence::Ptr &inc)
{
    // Must match MockBackend::computeHash (SHA-256 hex of toICalString).
    const QString ical = icalFor(inc);
    const QByteArray hash = QCryptographicHash::hash(ical.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex());
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

class TestCalendarRecordChangedNotifications : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void targetNewRecord_notifiesCreated();
    void targetModifiedRecord_notifiesUpdated();
    void targetDeletedRecord_notifiesDeleted();
    void sourceSidePush_doesNotNotify();

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

void TestCalendarRecordChangedNotifications::initTestCase()
{
    Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
    Kalburator::registerStockPlugins(pm);
}

void TestCalendarRecordChangedNotifications::init()
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

void TestCalendarRecordChangedNotifications::cleanup()
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

void TestCalendarRecordChangedNotifications::setupCoordinator()
{
    m_coordinator = std::make_unique<SyncEngine>(m_registry.get(), m_host.get(), m_shape);
    m_coordinator->setBaselineStore(m_calendarBaselines.get());
    m_coordinator->setSyncConflictStore(m_conflictStore.get());
    m_coordinator->setConflictManager(m_conflictManager.get());
    m_coordinator->setCollection(m_host->stubCollection());
    m_coordinator->setSyncMappings({ makeTwoWayMapping() });
}

bool TestCalendarRecordChangedNotifications::runOneSync()
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
        qWarning() << "runSync did not finish within" << kSyncTimeoutMs << "ms";
        return false;
    }
    if (future.isCanceled()) {
        qWarning() << "runSync was canceled unexpectedly";
        return false;
    }
    return true;
}

QStringList TestCalendarRecordChangedNotifications::sourceUids() const
{
    return m_source->allUids(QString::fromLatin1(kCalendarId));
}

QStringList TestCalendarRecordChangedNotifications::targetUids() const
{
    return m_target->allUids(QString::fromLatin1(kCalendarId));
}

// ---- Tests ------------------------------------------------------------

void TestCalendarRecordChangedNotifications::targetNewRecord_notifiesCreated()
{
    // evt-1 unchanged on both sides (skipped via matching blob baseline);
    // evt-2 exists only on the target, with no baseline at all — a pure
    // remote-side create that must materialize onto source.
    auto evt1 = makeEvent(QStringLiteral("evt-1"), QStringLiteral("Unchanged"));
    auto evt2 = makeEvent(QStringLiteral("evt-2"), QStringLiteral("Remote New"));

    m_calendarBaselines->setBaselineV3(QString::fromLatin1(kMappingId),
                                       calendarTestRec(QStringLiteral("evt-1"), icalFor(evt1)));
    m_calendarBaselines->setBaselineV3(QString::fromLatin1(kMappingId),
                                       makeBlobRec(QStringLiteral("evt-1"), hashFor(evt1)));

    m_source->addIncidence(QString::fromLatin1(kCalendarId), evt1);
    m_target->addIncidence(QString::fromLatin1(kCalendarId), evt1);
    m_target->addIncidence(QString::fromLatin1(kCalendarId), evt2);

    setupCoordinator();
    QVERIFY(runOneSync());

    QVERIFY(sourceUids().contains(QStringLiteral("evt-2")));
    QCOMPARE(m_host->recordChangedCount(ISyncHost::ChangeKind::Created), 1);
    QCOMPARE(m_host->recordChangedCount(ISyncHost::ChangeKind::Updated), 0);
    QCOMPARE(m_host->recordChangedCount(ISyncHost::ChangeKind::Deleted), 0);
}

void TestCalendarRecordChangedNotifications::targetModifiedRecord_notifiesUpdated()
{
    // evt-1 known to both sides via baseline, but the target's copy has since
    // diverged (no matching blob baseline for it) — SourceWins conflict
    // policy means... actually with no source-side change, this is a plain
    // one-sided update that must propagate target -> source.
    auto evt1Old = makeEvent(QStringLiteral("evt-1"), QStringLiteral("Original"));
    auto evt1New = makeEvent(QStringLiteral("evt-1"), QStringLiteral("Renamed On Target"));

    m_calendarBaselines->setBaselineV3(QString::fromLatin1(kMappingId),
                                       calendarTestRec(QStringLiteral("evt-1"), icalFor(evt1Old)));
    m_calendarBaselines->setBaselineV3(QString::fromLatin1(kMappingId),
                                       makeBlobRec(QStringLiteral("evt-1"), hashFor(evt1Old)));

    m_source->addIncidence(QString::fromLatin1(kCalendarId), evt1Old);
    m_target->addIncidence(QString::fromLatin1(kCalendarId), evt1New);

    setupCoordinator();
    QVERIFY(runOneSync());

    QCOMPARE(m_host->recordChangedCount(ISyncHost::ChangeKind::Created), 0);
    QCOMPARE(m_host->recordChangedCount(ISyncHost::ChangeKind::Updated), 1);
    QCOMPARE(m_host->recordChangedCount(ISyncHost::ChangeKind::Deleted), 0);
}

void TestCalendarRecordChangedNotifications::targetDeletedRecord_notifiesDeleted()
{
    // Both sides start with evt-1 (known via baseline); the target deletes
    // it, so the delete must propagate to source.
    auto evt1 = makeEvent(QStringLiteral("evt-1"), QStringLiteral("Doomed"));

    m_calendarBaselines->setBaselineV3(QString::fromLatin1(kMappingId),
                                       makeBlobRec(QStringLiteral("evt-1"), hashFor(evt1)));

    m_source->addIncidence(QString::fromLatin1(kCalendarId), evt1);
    m_target->addIncidence(QString::fromLatin1(kCalendarId), evt1);
    m_target->removeItem(QString::fromLatin1(kCalendarId), QStringLiteral("evt-1"));

    setupCoordinator();
    QVERIFY(runOneSync());

    QVERIFY(!sourceUids().contains(QStringLiteral("evt-1")));
    QCOMPARE(m_host->recordChangedCount(ISyncHost::ChangeKind::Created), 0);
    QCOMPARE(m_host->recordChangedCount(ISyncHost::ChangeKind::Updated), 0);
    QCOMPARE(m_host->recordChangedCount(ISyncHost::ChangeKind::Deleted), 1);
}

void TestCalendarRecordChangedNotifications::sourceSidePush_doesNotNotify()
{
    // evt-1 is new on the SOURCE side only; it gets pushed to target. The
    // engine must not report this via recordChanged — only writes onto the
    // mapping's source side are notified (see syncengine.cpp applyBatch's
    // notifyHost flag).
    auto evt1 = makeEvent(QStringLiteral("evt-1"), QStringLiteral("Local New"));
    m_source->addIncidence(QString::fromLatin1(kCalendarId), evt1);

    setupCoordinator();
    QVERIFY(runOneSync());

    QVERIFY(targetUids().contains(QStringLiteral("evt-1")));
    QCOMPARE(m_host->recordChangedCount(), 0);
}

QTEST_MAIN(TestCalendarRecordChangedNotifications)
#include "tst_calendar_recordchanged_notifications.moc"
