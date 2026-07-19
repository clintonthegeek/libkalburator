// tst_calendar_readonly_skip.cpp
//
// O46 — a write withheld because the target reports read-only
// (discoveredWritable()==false) must be surfaced on SyncResult as a
// "target-readonly:<collection>" warning: a no-op success, not a silent one.
// See docs/2026-07-19-consumer-coordination-status.md O46 and
// docs/campaign/FINDINGS.md O46 (WildPalms RFC 2026-07-18).

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QTimeZone>

#include <KCalendarCore/Event>
#include <KCalendarCore/MemoryCalendar>

#include "backendregistry.h"
#include "baselinestore.h"
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
constexpr auto kMappingId       = "mapping-1";
constexpr int  kSyncTimeoutMs   = 30000;

// A MockBackend whose target collection is discovered read-only.
class ReadOnlyMockBackend : public MockBackend {
public:
    bool discoveredWritable(const QString &) const override { return false; }
};

KCalendarCore::Event::Ptr makeEvent(const QString &uid, const QString &summary)
{
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    event->setUid(uid);
    event->setSummary(summary);
    event->setDtStart(QDateTime::currentDateTimeUtc());
    return event;
}

SyncMapping makeMapping()
{
    SyncMapping m;
    m.id             = QString::fromLatin1(kMappingId);
    m.sourceBackend  = QString::fromLatin1(kSourceBackendId);
    m.sourceCalendar = QString::fromLatin1(kCalendarId);
    m.targetBackend  = QString::fromLatin1(kTargetBackendId);
    m.targetCalendar = QString::fromLatin1(kCalendarId);
    m.mode           = SyncMode::OneWayUpload;
    m.conflictPolicy = ConflictResolution::SourceWins;
    m.enabled        = true;
    return m;
}

} // namespace

class TestCalendarReadonlySkip : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() {
        Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
        Kalburator::registerStockPlugins(pm);
    }

    void readonlyTarget_recordsWarningAndStaysNoOp();
    void writableTarget_noReadonlyWarning();

private:
    // Build the whole harness, seed two source events, run one sync, and return
    // the single mapping's SyncResult. `readOnlyTarget` picks the target class.
    SyncResult runMirror(bool readOnlyTarget);

    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;
};

SyncResult TestCalendarReadonlySkip::runMirror(bool readOnlyTarget)
{
    QTemporaryDir tmpDir;
    Q_ASSERT(tmpDir.isValid());

    BackendRegistry registry;
    MockBackend source;
    std::unique_ptr<MockBackend> target =
        readOnlyTarget ? std::make_unique<ReadOnlyMockBackend>()
                       : std::make_unique<MockBackend>();

    registry.registerBackendInstance(QString::fromLatin1(kSourceBackendId), &source);
    registry.registerBackendInstance(QString::fromLatin1(kTargetBackendId), target.get());

    source.createCalendar(QString::fromLatin1(kCollectionId),
                          QString::fromLatin1(kCalendarId),
                          QStringLiteral("Calendar 1"));
    target->createCalendar(QString::fromLatin1(kCollectionId),
                           QString::fromLatin1(kCalendarId),
                           QStringLiteral("Calendar 1"));

    StubSyncHost host(&registry);
    auto *hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal->setId(QString::fromLatin1(kCalendarId));
    host.stubCollection()->addCalendarWithId(QString::fromLatin1(kCalendarId), hostCal);

    const QString dbPath = tmpDir.filePath(QStringLiteral(".kalburator-sync.db"));
    Kalburator::Storage::BaselineStore baselines(dbPath);
    SyncConflictStore conflictStore(dbPath);
    ConflictManager conflictManager;
    conflictManager.setSyncConflictStore(&conflictStore);

    SyncEngine engine(&registry, &host, m_shape);
    engine.setBaselineStore(&baselines);
    engine.setSyncConflictStore(&conflictStore);
    engine.setConflictManager(&conflictManager);
    engine.setCollection(host.stubCollection());
    engine.setSyncMappings({ makeMapping() });

    source.addIncidence(QString::fromLatin1(kCalendarId),
                        makeEvent(QStringLiteral("evt-1"), QStringLiteral("One")));
    source.addIncidence(QString::fromLatin1(kCalendarId),
                        makeEvent(QStringLiteral("evt-2"), QStringLiteral("Two")));

    SyncRequest req;
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    auto future = engine.runSync(req);

    int waited = 0;
    while (!future.isFinished() && waited < kSyncTimeoutMs) {
        QTest::qWait(10);
        waited += 10;
    }
    Q_ASSERT(future.isFinished());

    const QList<SyncResult> results = future.resultAt(0);
    Q_ASSERT(!results.isEmpty());
    return results.first();
}

void TestCalendarReadonlySkip::readonlyTarget_recordsWarningAndStaysNoOp()
{
    const SyncResult r = runMirror(/*readOnlyTarget=*/true);

    // The skip is a no-op SUCCESS — never a failure.
    QVERIFY2(r.success, "read-only target skip must remain a success");
    // Nothing was written to the target.
    QCOMPARE(r.targetStats.total(), 0);
    // ...but the withheld write is now VISIBLE as a stable-prefix warning.
    const QString expected = QStringLiteral("target-readonly:%1")
                                 .arg(QString::fromLatin1(kCalendarId));
    QVERIFY2(r.warnings.contains(expected),
             qPrintable(QStringLiteral("expected warning '%1'; got: [%2]")
                            .arg(expected, r.warnings.join(QStringLiteral(", ")))));
}

void TestCalendarReadonlySkip::writableTarget_noReadonlyWarning()
{
    const SyncResult r = runMirror(/*readOnlyTarget=*/false);

    // Happy path: a writable target must NOT produce a read-only warning
    // (no false positive). (The first-sync mirror path reports zero in
    // targetStats by design — an E1-era fast-path stats quirk unrelated to
    // O46 — so this case asserts on the warning surface, not the stats.)
    QVERIFY(r.success);
    for (const QString &w : r.warnings)
        QVERIFY2(!w.contains(QStringLiteral("readonly")),
                 qPrintable(QStringLiteral("unexpected read-only warning: %1").arg(w)));
}

QTEST_MAIN(TestCalendarReadonlySkip)
#include "tst_calendar_readonly_skip.moc"
