#include <QtTest/QtTest>

#include "backendcapabilities.h"
#include "backendrecord.h"
#include "calendardomainadapter.h"
#include "enginediff.h"
#include "syncdiff.h"
#include "synctypes.h"
#include "transcodingregistry.h"
#include "transcodingrouter.h"

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>

#include <QTimeZone>

using Kalburator::Sync::BackendCapabilities;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CalendarDomainAdapter;
using Kalburator::Sync::ConflictResolution;
using Kalburator::Sync::EngineDiff;
using Kalburator::Sync::EngineDiffOp;
using Kalburator::Sync::EngineMerge;
using Kalburator::Sync::SyncRecord;
using Kalburator::Sync::TranscodingRegistry;
using Kalburator::Sync::TranscodingRouter;

namespace {

KCalendarCore::Event::Ptr makeEvent(const QString &uid,
                                    const QString &summary,
                                    const QDateTime &start,
                                    const QDateTime &lastModified)
{
    KCalendarCore::Event::Ptr event(new KCalendarCore::Event());
    event->setUid(uid);
    event->setSummary(summary);
    event->setDtStart(start);
    event->setDtEnd(start.addSecs(3600));
    event->setLastModified(lastModified);
    event->setCreated(lastModified);
    return event;
}

BackendRecord toBackendRecord(const KCalendarCore::Incidence::Ptr &inc)
{
    KCalendarCore::ICalFormat format;
    const QString ical = format.toICalString(inc);
    BackendRecord b;
    b.id = inc->uid();
    b.type = QStringLiteral("calendar");
    b.displayName = inc->summary();
    b.data = ical.toUtf8();
    b.contentHash = SyncRecord::computeSemanticHash(inc);
    b.lastModified = inc->lastModified();
    return b;
}

} // namespace

class TstCalendarDomainAdapter : public QObject {
    Q_OBJECT

private slots:
    void cleanup();

    void domainType_isCalendar();
    void emptyInputs_returnsEmptyDiff();
    void createOnlyDiff_returnsToTargetCreate();
    void updateDiff_returnsToTargetUpdate();
    void deleteDiff_returnsToTargetDelete();
    void conflictDetection_returnsConflict();
    void mergeAppliesPolicy_LWW();
};

void TstCalendarDomainAdapter::cleanup()
{
    // TranscodingRegistry is a process-wide singleton; reset between
    // tests to avoid cross-test state leakage. (See
    // libkalburator/CLAUDE.md "Transcoding tests" guidance.)
    TranscodingRegistry::instance().clear();
}

void TstCalendarDomainAdapter::domainType_isCalendar()
{
    TranscodingRouter router(TranscodingRegistry::instance());
    CalendarDomainAdapter adapter(router);
    QCOMPARE(adapter.domainType(), QStringLiteral("calendar"));
}

void TstCalendarDomainAdapter::emptyInputs_returnsEmptyDiff()
{
    TranscodingRouter router(TranscodingRegistry::instance());
    CalendarDomainAdapter adapter(router);

    const EngineDiff d = adapter.diff({}, {}, {},
                                      BackendCapabilities{}, BackendCapabilities{});

    QCOMPARE(d.totalOperations(), 0);
    QVERIFY(!d.hasConflicts());
}

void TstCalendarDomainAdapter::createOnlyDiff_returnsToTargetCreate()
{
    TranscodingRouter router(TranscodingRegistry::instance());
    CalendarDomainAdapter adapter(router);

    const QDateTime start(QDate(2026, 5, 1), QTime(10, 0, 0), QTimeZone::UTC);
    const QDateTime mtime = QDateTime::currentDateTimeUtc();
    const auto ev = makeEvent(QStringLiteral("evt-create"),
                              QStringLiteral("New event"), start, mtime);

    const QList<BackendRecord> source   = {toBackendRecord(ev)};
    const QList<BackendRecord> target;
    const QList<BackendRecord> baseline;

    const EngineDiff d = adapter.diff(source, target, baseline,
                                      BackendCapabilities{}, BackendCapabilities{});

    QCOMPARE(d.toSource.size(), 0);
    QCOMPARE(d.toTarget.size(), 1);
    QCOMPARE(d.toTarget.first().kind, EngineDiffOp::Kind::Create);
    QCOMPARE(d.toTarget.first().record.id, ev->uid());
    QVERIFY(!d.toTarget.first().record.data.isEmpty());
}

void TstCalendarDomainAdapter::updateDiff_returnsToTargetUpdate()
{
    TranscodingRouter router(TranscodingRegistry::instance());
    CalendarDomainAdapter adapter(router);

    const QDateTime start(QDate(2026, 5, 2), QTime(11, 0, 0), QTimeZone::UTC);
    const QDateTime t1 = QDateTime(QDate(2026, 4, 1), QTime(9, 0, 0), QTimeZone::UTC);
    const QDateTime t2 = QDateTime(QDate(2026, 4, 15), QTime(9, 0, 0), QTimeZone::UTC);

    // Baseline: v1
    const auto v1 = makeEvent(QStringLiteral("evt-update"),
                              QStringLiteral("Original summary"), start, t1);
    // Source: v2 (modified summary), target unchanged at v1.
    const auto v2 = makeEvent(QStringLiteral("evt-update"),
                              QStringLiteral("Updated summary"), start, t2);

    const QList<BackendRecord> source   = {toBackendRecord(v2)};
    const QList<BackendRecord> target   = {toBackendRecord(v1)};
    const QList<BackendRecord> baseline = {toBackendRecord(v1)};

    const EngineDiff d = adapter.diff(source, target, baseline,
                                      BackendCapabilities{}, BackendCapabilities{});

    QCOMPARE(d.toSource.size(), 0);
    QCOMPARE(d.toTarget.size(), 1);
    QCOMPARE(d.toTarget.first().kind, EngineDiffOp::Kind::Update);
    QCOMPARE(d.toTarget.first().record.id, v2->uid());
    QVERIFY(!d.hasConflicts());
}

void TstCalendarDomainAdapter::deleteDiff_returnsToTargetDelete()
{
    TranscodingRouter router(TranscodingRegistry::instance());
    CalendarDomainAdapter adapter(router);

    const QDateTime start(QDate(2026, 5, 3), QTime(12, 0, 0), QTimeZone::UTC);
    const QDateTime mtime(QDate(2026, 4, 1), QTime(9, 0, 0), QTimeZone::UTC);
    const auto ev = makeEvent(QStringLiteral("evt-delete"),
                              QStringLiteral("Doomed event"), start, mtime);

    // Source absent (deleted), target still has it, baseline has it.
    const QList<BackendRecord> source;
    const QList<BackendRecord> target   = {toBackendRecord(ev)};
    const QList<BackendRecord> baseline = {toBackendRecord(ev)};

    const EngineDiff d = adapter.diff(source, target, baseline,
                                      BackendCapabilities{}, BackendCapabilities{});

    QCOMPARE(d.toSource.size(), 0);
    QCOMPARE(d.toTarget.size(), 1);
    QCOMPARE(d.toTarget.first().kind, EngineDiffOp::Kind::Delete);
    QCOMPARE(d.toTarget.first().record.id, ev->uid());
    QVERIFY(!d.hasConflicts());
}

void TstCalendarDomainAdapter::conflictDetection_returnsConflict()
{
    TranscodingRouter router(TranscodingRegistry::instance());
    CalendarDomainAdapter adapter(router);

    const QDateTime start(QDate(2026, 5, 4), QTime(13, 0, 0), QTimeZone::UTC);
    const QDateTime tBase(QDate(2026, 4, 1),  QTime(9, 0, 0), QTimeZone::UTC);
    const QDateTime tSrc (QDate(2026, 4, 10), QTime(9, 0, 0), QTimeZone::UTC);
    const QDateTime tTgt (QDate(2026, 4, 11), QTime(9, 0, 0), QTimeZone::UTC);

    const auto baselineEv = makeEvent(QStringLiteral("evt-conflict"),
                                      QStringLiteral("Original"), start, tBase);
    const auto sourceEv   = makeEvent(QStringLiteral("evt-conflict"),
                                      QStringLiteral("Source-side edit"), start, tSrc);
    const auto targetEv   = makeEvent(QStringLiteral("evt-conflict"),
                                      QStringLiteral("Target-side edit"), start, tTgt);

    const QList<BackendRecord> source   = {toBackendRecord(sourceEv)};
    const QList<BackendRecord> target   = {toBackendRecord(targetEv)};
    const QList<BackendRecord> baseline = {toBackendRecord(baselineEv)};

    const EngineDiff d = adapter.diff(source, target, baseline,
                                      BackendCapabilities{}, BackendCapabilities{});

    QVERIFY(d.hasConflicts());

    int conflictCount = 0;
    for (const auto &op : d.toTarget) {
        if (op.kind == EngineDiffOp::Kind::Conflict) ++conflictCount;
    }
    for (const auto &op : d.toSource) {
        if (op.kind == EngineDiffOp::Kind::Conflict) ++conflictCount;
    }
    QCOMPARE(conflictCount, 1);
}

void TstCalendarDomainAdapter::mergeAppliesPolicy_LWW()
{
    TranscodingRouter router(TranscodingRegistry::instance());
    CalendarDomainAdapter adapter(router);

    const QDateTime start(QDate(2026, 5, 5), QTime(14, 0, 0), QTimeZone::UTC);
    const QDateTime tBase(QDate(2026, 4, 1),  QTime(9, 0, 0), QTimeZone::UTC);
    const QDateTime tSrc (QDate(2026, 4, 20), QTime(9, 0, 0), QTimeZone::UTC);
    const QDateTime tTgt (QDate(2026, 4, 10), QTime(9, 0, 0), QTimeZone::UTC);

    const auto baselineEv = makeEvent(QStringLiteral("evt-lww"),
                                      QStringLiteral("Original"), start, tBase);
    const auto sourceEv   = makeEvent(QStringLiteral("evt-lww"),
                                      QStringLiteral("Source-side (newer)"), start, tSrc);
    const auto targetEv   = makeEvent(QStringLiteral("evt-lww"),
                                      QStringLiteral("Target-side (older)"), start, tTgt);

    const QList<BackendRecord> source   = {toBackendRecord(sourceEv)};
    const QList<BackendRecord> target   = {toBackendRecord(targetEv)};
    const QList<BackendRecord> baseline = {toBackendRecord(baselineEv)};

    const EngineDiff d = adapter.diff(source, target, baseline,
                                      BackendCapabilities{}, BackendCapabilities{});
    QVERIFY(d.hasConflicts());

    const EngineMerge m = adapter.merge(d, ConflictResolution::LastWriteWins);
    QCOMPARE(m.conflictsResolved, 1);
    QCOMPARE(m.conflictsDeferred, 0);

    // Source is newer (tSrc > tTgt) ⇒ LWW resolves to a toTarget Update
    // carrying the source-side record.
    QCOMPARE(m.finalTarget.size(), 1);
    QCOMPARE(m.finalSource.size(), 0);
    QCOMPARE(m.finalTarget.first().id, sourceEv->uid());
    QVERIFY(!m.finalTarget.first().isDeleted);
}

QTEST_GUILESS_MAIN(TstCalendarDomainAdapter)
#include "tst_calendar_domain_adapter.moc"
