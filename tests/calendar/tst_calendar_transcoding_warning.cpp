// tst_calendar_transcoding_warning.cpp
//
// Verifies that SyncEngine emits transcodingWarning when it demotes a
// record through a lossy canon-to-backend pipeline and the record has
// a property that will be materially lost.
//
// The test routes the TARGET backend through {calendar, org-ical} so that
// canon→org-ical fires and the `recurrence=Simplified` loss is materialized
// on an event that carries a complex RRULE.
//
// See: docs/phase0/04l-phase-d0-test-harness-design.md

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>

#include <KCalendarCore/Event>
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/Recurrence>
#include <KCalendarCore/RecurrenceRule>

#include "backendregistry.h"
#include "baselinestore.h"
#include "calendar_test_helpers.h"
#include "conflictmanager.h"
#include "mockbackend.h"
#include "pluginmanager.h"
#include "shape.h"
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
constexpr auto kTargetBackendId = "target-orgical";
constexpr auto kCollectionId    = "stub-collection";
constexpr auto kCalendarId      = "calendar-1";
constexpr auto kMappingId       = "mapping-1";

constexpr int kSyncTimeoutMs = 30000;

KCalendarCore::Event::Ptr makeRecurringEventWithByDay(const QString &uid)
{
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    event->setUid(uid);
    event->setSummary(QStringLiteral("Weekly meeting"));
    event->setDtStart(QDateTime::currentDateTimeUtc());

    auto *rule = new KCalendarCore::RecurrenceRule();
    rule->setRecurrenceType(KCalendarCore::RecurrenceRule::rWeekly);
    rule->setFrequency(1);
    // Complex BYDAY: Mon/Wed/Fri — org-ical cannot represent multi-day weekly
    // patterns and will simplify/strip them. This ensures the recurrence field
    // is non-empty in canon JSON so materializedLoss() fires.
    rule->setByDays({
        KCalendarCore::RecurrenceRule::WDayPos(0, static_cast<short>(Qt::Monday)),
        KCalendarCore::RecurrenceRule::WDayPos(0, static_cast<short>(Qt::Wednesday)),
        KCalendarCore::RecurrenceRule::WDayPos(0, static_cast<short>(Qt::Friday)),
    });
    event->recurrence()->addRRule(rule);
    return event;
}

SyncMapping makeMapping()
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

class TestCalendarTranscodingWarning : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() {
        Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
        Kalburator::registerStockPlugins(pm);
    }
    void init();
    void cleanup();

    void transcoding_sourceHasRruleByDay_targetCantRepresent_emitsWarning();

private:
    std::unique_ptr<QTemporaryDir>         m_tmpDir;
    std::unique_ptr<BackendRegistry>       m_registry;
    std::unique_ptr<MockBackend>           m_source;
    std::unique_ptr<MockBackend>           m_target;
    std::unique_ptr<StubSyncHost>          m_host;
    std::unique_ptr<Kalburator::Storage::BaselineStore> m_calendarBaselines;
    std::unique_ptr<SyncConflictStore>     m_conflictStore;
    std::unique_ptr<ConflictManager>       m_conflictManager;
    std::unique_ptr<SyncEngine>            m_coordinator;

    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;
};

void TestCalendarTranscodingWarning::init()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());

    m_registry = std::make_unique<BackendRegistry>();
    m_source   = std::make_unique<MockBackend>();
    m_target   = std::make_unique<MockBackend>();

    // Route the target backend through {calendar, org-ical} so that the
    // canon→org-ical edge (which carries recurrence=Simplified loss) is
    // selected when the engine demotes canon records to the target.
    m_target->setShape(Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("calendar")},
        Kalburator::Shape::EncodingId{QStringLiteral("org-ical")} });

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
    auto *hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal->setId(QString::fromLatin1(kCalendarId));
    m_host->stubCollection()->addCalendarWithId(QString::fromLatin1(kCalendarId),
                                                 hostCal);

    const QString dbPath = m_tmpDir->filePath(QStringLiteral(".kalburator-sync.db"));
    m_calendarBaselines = std::make_unique<Kalburator::Storage::BaselineStore>(dbPath);
    m_conflictStore     = std::make_unique<SyncConflictStore>(dbPath);

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());

    m_coordinator = std::make_unique<SyncEngine>(m_registry.get(), m_host.get(), m_shape);
    m_coordinator->setBaselineStore(m_calendarBaselines.get());
    m_coordinator->setSyncConflictStore(m_conflictStore.get());
    m_coordinator->setConflictManager(m_conflictManager.get());
    m_coordinator->setCollection(m_host->stubCollection());
    m_coordinator->setSyncMappings({ makeMapping() });
}

void TestCalendarTranscodingWarning::cleanup()
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

// ---- Tests ---------------------------------------------------------------

void TestCalendarTranscodingWarning::transcoding_sourceHasRruleByDay_targetCantRepresent_emitsWarning()
{
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeRecurringEventWithByDay(QStringLiteral("evt-rrule-1")));

    QSignalSpy warningSpy(m_coordinator.get(),
                          &SyncEngine::transcodingWarning);

    SyncRequest req;
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    auto future = m_coordinator->runSync(req);
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    QVERIFY(!future.isCanceled());

    QVERIFY2(warningSpy.count() >= 1,
             qPrintable(QStringLiteral("expected transcodingWarning, got %1 signals")
                            .arg(warningSpy.count())));

    // Inspect the first warning: the record id should be the event UID,
    // and the warning text should name the "recurrence" property that was
    // simplified by the canon→org-ical pipeline.
    const auto args = warningSpy.takeFirst();
    QCOMPARE(args.size(), 3);
    const QString uid = args.at(1).toString();
    const QStringList warnings = args.at(2).toStringList();
    QCOMPARE(uid, QStringLiteral("evt-rrule-1"));
    QVERIFY2(!warnings.isEmpty(), "warnings list empty");
    QVERIFY2(warnings.first().contains(QStringLiteral("recurrence")),
             qPrintable(QStringLiteral("warning text unexpected: ") + warnings.first()));
}

QTEST_MAIN(TestCalendarTranscodingWarning)
#include "tst_calendar_transcoding_warning.moc"
