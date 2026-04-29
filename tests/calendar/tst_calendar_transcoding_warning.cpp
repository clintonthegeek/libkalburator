// tst_calendar_transcoding_warning.cpp
//
// Phase D.0 — Transcoding warning emission. Verifies SyncWorker invokes
// TranscodingRegistry on writes to a backend whose type differs from
// the source's, and that the transcodingWarning signal surfaces lossy
// conversions.
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
#include "conflictmanager.h"
#include "mockbackend.h"
#include "propertytranscoder.h"
#include "synccoordinator.h"
#include "syncstore.h"
#include "synctypes.h"
#include "transcodingregistry.h"

#include "stubs/stubsynchost.h"

using namespace Kalburator::Sync;
using namespace Kalburator::Sync::Test;

namespace {

constexpr auto kSourceBackendId = "source-mock";
constexpr auto kTargetBackendId = "target-lossy";
constexpr auto kCollectionId    = "stub-collection";
constexpr auto kCalendarId      = "calendar-1";
constexpr auto kMappingId       = "mapping-1";

constexpr auto kSourceBackendType = "mock";
constexpr auto kTargetBackendType = "lossy-mock";

constexpr int kSyncTimeoutMs = 5000;

// Subclass of MockBackend that advertises a different backendType()
// so TranscodingRegistry sees source and target as distinct types
// (it short-circuits when sourceType == targetType).
class LossyMockBackend : public MockBackend
{
    Q_OBJECT
public:
    using MockBackend::MockBackend;
    QString backendType() const override
    {
        return QString::fromLatin1(kTargetBackendType);
    }
};

// Stub transcoder applicable to "mock" → "lossy-mock". Fires when
// the incidence has an RRULE with BYDAY, drops the BYDAY rule (lossy),
// and announces lossy fidelity so transcodeIncidence emits a warning.
class ByDayStripTranscoder : public PropertyTranscoder
{
public:
    QString propertyName() const override        { return QStringLiteral("RRULE"); }
    QString sourceBackendType() const override   { return QString::fromLatin1(kSourceBackendType); }
    QString targetBackendType() const override   { return QString::fromLatin1(kTargetBackendType); }
    TranscodingFidelity fidelity() const override { return TranscodingFidelity::Lossy; }
    QString description() const override
    {
        return QStringLiteral("RRULE BYDAY dropped (lossy-mock target cannot represent it)");
    }
    bool transcode(KCalendarCore::Incidence::Ptr &incidence) const override
    {
        if (!incidence || !incidence->recurs()) return false;
        auto *rec = incidence->recurrence();
        bool changed = false;
        const auto rules = rec->rRules();
        for (auto *rule : rules) {
            if (!rule->byDays().isEmpty()) {
                rule->setByDays({});
                changed = true;
            }
        }
        return changed;
    }
};

KCalendarCore::Event::Ptr makeRecurringEventWithByDay(const QString &uid)
{
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    event->setUid(uid);
    event->setSummary(QStringLiteral("Weekly meeting"));
    event->setDtStart(QDateTime::currentDateTimeUtc());

    auto *rule = new KCalendarCore::RecurrenceRule();
    rule->setRecurrenceType(KCalendarCore::RecurrenceRule::rWeekly);
    rule->setFrequency(1);
    rule->setByDays({KCalendarCore::RecurrenceRule::WDayPos(0,
                        static_cast<short>(Qt::Monday))});
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
    void initTestCase() {}
    void cleanupTestCase() {}
    void init();
    void cleanup();

    void transcoding_sourceHasRruleByDay_targetCantRepresent_emitsWarning();

private:
    std::unique_ptr<QTemporaryDir>      m_tmpDir;
    std::unique_ptr<BackendRegistry>    m_registry;
    std::unique_ptr<MockBackend>        m_source;
    std::unique_ptr<LossyMockBackend>   m_target;
    std::unique_ptr<StubSyncHost>       m_host;
    std::unique_ptr<SyncStore>          m_store;
    std::unique_ptr<ConflictManager>    m_conflictManager;
    std::unique_ptr<SyncCoordinator>    m_coordinator;
};

void TestCalendarTranscodingWarning::init()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());

    m_registry = std::make_unique<BackendRegistry>();
    m_source   = std::make_unique<MockBackend>();
    m_target   = std::make_unique<LossyMockBackend>();
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

    m_store = std::make_unique<SyncStore>(m_tmpDir->filePath(QStringLiteral("sync.db")));

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncStore(m_store.get());

    m_coordinator = std::make_unique<SyncCoordinator>(m_registry.get(), m_host.get());
    m_coordinator->setSyncStore(m_store.get());
    m_coordinator->setConflictManager(m_conflictManager.get());
    m_coordinator->setCollection(m_host->stubCollection());
    m_coordinator->setSyncMappings({ makeMapping() });

    // Install the lossy stub transcoder for this test. TranscodingRegistry
    // is a singleton; cleanup() must clear() it to keep tests isolated.
    TranscodingRegistry::instance().registerTranscoder(
        std::make_unique<ByDayStripTranscoder>());
}

void TestCalendarTranscodingWarning::cleanup()
{
    // CRITICAL: TranscodingRegistry is a singleton. Without this,
    // transcoders registered here would leak into subsequent tests in
    // the same suite or into other tests sharing the process.
    TranscodingRegistry::instance().clear();

    m_coordinator.reset();
    m_conflictManager.reset();
    m_store.reset();
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
                          &SyncCoordinator::transcodingWarning);
    QSignalSpy allDoneSpy(m_coordinator.get(),
                          &SyncCoordinator::allSyncsCompleted);

    m_coordinator->runSync(SyncCoordinator::SyncBehavior::Unmonitored);
    QVERIFY(allDoneSpy.wait(kSyncTimeoutMs));

    QVERIFY2(warningSpy.count() >= 1,
             qPrintable(QStringLiteral("expected transcodingWarning, got %1 signals")
                            .arg(warningSpy.count())));

    // Inspect the first warning: should mention the lossy transcoder's
    // description, and reference the conflicting incidence's UID.
    const auto args = warningSpy.takeFirst();
    QCOMPARE(args.size(), 3);
    const QString uid = args.at(1).toString();
    const QStringList warnings = args.at(2).toStringList();
    QCOMPARE(uid, QStringLiteral("evt-rrule-1"));
    QVERIFY2(!warnings.isEmpty(), "warnings list empty");
    QVERIFY2(warnings.first().contains(QStringLiteral("BYDAY")),
             qPrintable(QStringLiteral("warning text unexpected: ") + warnings.first()));
}

QTEST_MAIN(TestCalendarTranscodingWarning)
#include "tst_calendar_transcoding_warning.moc"
