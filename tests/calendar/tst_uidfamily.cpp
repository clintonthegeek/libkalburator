// Unit tests for calendar/uidfamily.h — the shared UID-family assembly both
// write paths must use (ADR 0010 decision 2).
//
// LocalBackend's behaviour is already pinned end to end by
// tst_localbackend_writepaths and PlanStan's tst_localbackend_write_batching.
// These tests pin the helper itself, so the second caller
// (RemoteCalendarBackend's staging flush) inherits a tested contract rather
// than a second implementation of one.
//
// No files, no network, no event loop.

#include <QObject>
#include <QTest>
#include <QTimeZone>

#include <KCalendarCore/Event>

#include <kalburator/calendar/uidfamily.h>

using namespace Kalburator::Calendar;
namespace Family = Kalburator::Calendar::UidFamily;

namespace {

const QString kUid = QStringLiteral("series-1");

QDateTime occurrence(int day, const QByteArray &zone = QByteArrayLiteral("UTC"))
{
    return QDateTime(QDate(2026, 6, day), QTime(9, 0), QTimeZone(zone));
}

KCalendarCore::Event::Ptr master(const QString &summary = QStringLiteral("Weekly"))
{
    auto ev = KCalendarCore::Event::Ptr::create();
    ev->setUid(kUid);
    ev->setSummary(summary);
    ev->setDtStart(occurrence(1));
    ev->recurrence()->setDaily(1);
    return ev;
}

KCalendarCore::Event::Ptr override_(const QDateTime &recurrenceId,
                                    const QString &summary)
{
    auto ev = KCalendarCore::Event::Ptr::create();
    ev->setUid(kUid);
    ev->setSummary(summary);
    ev->setDtStart(recurrenceId);
    ev->setRecurrenceId(recurrenceId);
    return ev;
}

} // namespace

class TstUidFamily : public QObject
{
    Q_OBJECT
private slots:
    void slotKey_masterIsEmpty_overrideIsUtcIso();
    void slotKey_sameInstantDifferentZone_keysIdentically();
    void merge_stagedOverrideJoinsExistingMaster();
    void merge_partialFlushPreservesUntouchedSiblings();
    void merge_laterStagedCopyWinsForTheSameSlot();
    void ordered_masterFirstThenOverridesByRecurrenceId();
    void ordered_orphanOverrideSurvivesWithoutASynthesizedMaster();
    void without_removingOneOccurrenceKeepsTheMaster();
    void without_removingTheMasterLeavesOrphans();
    void parse_roundTripsAWholeFamily();
    void parse_unparseableBytesYieldEmptyNotError();
    void groupByUid_separatesFamiliesAndReportsFirstSeenOrder();
};

void TstUidFamily::slotKey_masterIsEmpty_overrideIsUtcIso()
{
    QVERIFY(Family::slotKey(master()).isEmpty());
    QCOMPARE(Family::slotKey(override_(occurrence(2), QStringLiteral("moved"))),
             QStringLiteral("2026-06-02T09:00:00Z"));
    QVERIFY(Family::slotKey({}).isEmpty());
}

// The whole point of normalizing to UTC: one instant is one slot however the
// producing client spelled its time zone, so a merge cannot end up holding two
// components for the same occurrence.
void TstUidFamily::slotKey_sameInstantDifferentZone_keysIdentically()
{
    const QDateTime utc(QDate(2026, 6, 2), QTime(9, 0), QTimeZone("UTC"));
    const QDateTime elsewhere = utc.toTimeZone(QTimeZone("America/New_York"));
    QVERIFY(utc.timeZone() != elsewhere.timeZone());

    QCOMPARE(Family::slotKey(override_(utc, QStringLiteral("a"))),
             Family::slotKey(override_(elsewhere, QStringLiteral("b"))));
}

void TstUidFamily::merge_stagedOverrideJoinsExistingMaster()
{
    Family::Family existing;
    existing.insert(QString(), master());

    const auto merged = Family::merge(
        existing, { override_(occurrence(2), QStringLiteral("moved")) });

    QCOMPARE(merged.size(), 2);
    QVERIFY(merged.contains(QString()));
    QVERIFY(merged.contains(QStringLiteral("2026-06-02T09:00:00Z")));
}

// The case the whole helper exists for: a flush carrying only the override
// must not destroy the master sitting beside it, and vice versa.
void TstUidFamily::merge_partialFlushPreservesUntouchedSiblings()
{
    Family::Family existing;
    existing.insert(QString(), master());
    existing.insert(QStringLiteral("2026-06-02T09:00:00Z"),
                    override_(occurrence(2), QStringLiteral("original override")));

    // Stage ONLY the master (e.g. it gained an EXDATE).
    const auto merged =
        Family::merge(existing, { master(QStringLiteral("Weekly, edited")) });

    QCOMPARE(merged.size(), 2);
    QCOMPARE(merged.value(QString())->summary(), QStringLiteral("Weekly, edited"));
    QCOMPARE(merged.value(QStringLiteral("2026-06-02T09:00:00Z"))->summary(),
             QStringLiteral("original override"));
}

void TstUidFamily::merge_laterStagedCopyWinsForTheSameSlot()
{
    const auto merged = Family::merge({}, {
        override_(occurrence(2), QStringLiteral("first")),
        override_(occurrence(2), QStringLiteral("second")),
    });

    QCOMPARE(merged.size(), 1);
    QCOMPARE(merged.value(QStringLiteral("2026-06-02T09:00:00Z"))->summary(),
             QStringLiteral("second"));
}

// Deterministic output: the bytes feed fingerprint-based change detection, so
// an unstable order would manufacture spurious "changed" verdicts.
void TstUidFamily::ordered_masterFirstThenOverridesByRecurrenceId()
{
    Family::Family family;
    family.insert(QStringLiteral("2026-06-05T09:00:00Z"),
                  override_(occurrence(5), QStringLiteral("fifth")));
    family.insert(QString(), master());
    family.insert(QStringLiteral("2026-06-02T09:00:00Z"),
                  override_(occurrence(2), QStringLiteral("second")));

    const auto out = Family::ordered(family);
    QCOMPARE(out.size(), 3);
    QVERIFY(!out.at(0)->recurrenceId().isValid());
    QCOMPARE(out.at(1)->summary(), QStringLiteral("second"));
    QCOMPARE(out.at(2)->summary(), QStringLiteral("fifth"));
}

// ADR 0008 decision 4: an override whose master is absent is legal under
// RFC 4791 and common in practice. Carry it through; never invent a master,
// which would write data the user did not author.
void TstUidFamily::ordered_orphanOverrideSurvivesWithoutASynthesizedMaster()
{
    Family::Family family;
    family.insert(QStringLiteral("2026-06-02T09:00:00Z"),
                  override_(occurrence(2), QStringLiteral("orphan")));

    const auto out = Family::ordered(family);
    QCOMPARE(out.size(), 1);
    QCOMPARE(out.at(0)->summary(), QStringLiteral("orphan"));
    QVERIFY(out.at(0)->recurrenceId().isValid());
}

void TstUidFamily::without_removingOneOccurrenceKeepsTheMaster()
{
    Family::Family family;
    family.insert(QString(), master());
    family.insert(QStringLiteral("2026-06-02T09:00:00Z"),
                  override_(occurrence(2), QStringLiteral("moved")));

    const auto left = Family::without(family, QStringLiteral("2026-06-02T09:00:00Z"));
    QCOMPARE(left.size(), 1);
    QVERIFY(left.contains(QString()));
}

void TstUidFamily::without_removingTheMasterLeavesOrphans()
{
    Family::Family family;
    family.insert(QString(), master());
    family.insert(QStringLiteral("2026-06-02T09:00:00Z"),
                  override_(occurrence(2), QStringLiteral("moved")));

    const auto left = Family::without(family, QString());
    QCOMPARE(left.size(), 1);
    QVERIFY(left.contains(QStringLiteral("2026-06-02T09:00:00Z")));
}

void TstUidFamily::parse_roundTripsAWholeFamily()
{
    Family::Family built;
    built.insert(QString(), master());
    built.insert(QStringLiteral("2026-06-02T09:00:00Z"),
                 override_(occurrence(2), QStringLiteral("moved")));

    const QByteArray ics = Family::serialize(built);
    QVERIFY(!ics.isEmpty());
    QVERIFY2(ics.contains("RECURRENCE-ID"),
             "the serialized family must carry the override component");

    const auto reparsed = Family::parse(ics);
    QCOMPARE(reparsed.size(), 2);
    QCOMPARE(reparsed.value(QString())->uid(), kUid);
    QCOMPARE(reparsed.value(QStringLiteral("2026-06-02T09:00:00Z"))->summary(),
             QStringLiteral("moved"));
}

// A resource that does not exist yet, or holds bytes we cannot read, must
// behave as "no existing family" so the merge writes what it was given. An
// error here would block a legitimate first write.
void TstUidFamily::parse_unparseableBytesYieldEmptyNotError()
{
    QVERIFY(Family::parse(QByteArray()).isEmpty());
    QVERIFY(Family::parse(QByteArrayLiteral("not a calendar at all")).isEmpty());
    QVERIFY(Family::serialize({}).isEmpty());
}

void TstUidFamily::groupByUid_separatesFamiliesAndReportsFirstSeenOrder()
{
    auto other = KCalendarCore::Event::Ptr::create();
    other->setUid(QStringLiteral("unrelated"));
    other->setSummary(QStringLiteral("elsewhere"));

    // KCalendarCore assigns a UID on construction, so clear it explicitly.
    auto anonymous = KCalendarCore::Event::Ptr::create();
    anonymous->setUid(QString());   // no UID: dropped
    QVERIFY(anonymous->uid().isEmpty());

    QList<QString> order;
    const auto grouped = Family::groupByUid({
        master(),
        other,
        override_(occurrence(2), QStringLiteral("moved")),
        anonymous,
        {},
    }, &order);

    QCOMPARE(grouped.size(), 2);
    QCOMPARE(grouped.value(kUid).size(), 2);
    QCOMPARE(grouped.value(QStringLiteral("unrelated")).size(), 1);
    QCOMPARE(order, QList<QString>({ kUid, QStringLiteral("unrelated") }));
}

QTEST_MAIN(TstUidFamily)
#include "tst_uidfamily.moc"
