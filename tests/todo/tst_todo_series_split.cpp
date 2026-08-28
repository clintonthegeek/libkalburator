// W3 (VP.e) — pure splitSeriesAtInstant() unit tests. Pins the binding
// spec (docs/2026-08-25-vtodo-parity-handoff-response.md §W3): master ends
// UNTIL<N>; new master starts at N with copied RRULE remainder; exceptions
// with start >= N rebase (RECURRENCE-ID unchanged) onto the new master's
// uid; COUNT-bounded RRULE fails loud (v1 does not recompute COUNT).
//
// NOTE: no terminated raw string literals in this TU (O59 moc tooling rule).

#include <QTest>
#include <QJsonArray>
#include <QJsonObject>

#include "todoseriessplitter.h"

using Kalburator::Todo::SeriesSplitResult;
using Kalburator::Todo::splitSeriesAtInstant;

namespace {

QJsonObject makeWeeklyMaster()
{
    QJsonObject obj;
    obj.insert(QStringLiteral("uid"), QStringLiteral("weekly-series-1"));
    obj.insert(QStringLiteral("summary"), QStringLiteral("Weekly review"));
    obj.insert(QStringLiteral("start"),
               QJsonObject{ { QStringLiteral("dateTime"), QStringLiteral("2026-01-05T09:00:00Z") },
                            { QStringLiteral("floating"), false } });
    obj.insert(QStringLiteral("recurrence"),
               QJsonArray{ QStringLiteral("RRULE:FREQ=WEEKLY;BYDAY=MO") });
    return obj;
}

QJsonObject makeException(const QString &uid, const QString &recurrenceIdIso)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("uid"), uid);
    obj.insert(QStringLiteral("summary"), QStringLiteral("Moved instance"));
    obj.insert(QStringLiteral("recurrenceId"),
               QJsonObject{ { QStringLiteral("dateTime"), recurrenceIdIso } });
    return obj;
}

QString recurrenceLine(const QJsonObject &canon)
{
    const QJsonArray arr = canon.value(QStringLiteral("recurrence")).toArray();
    return arr.isEmpty() ? QString() : arr.first().toString();
}

} // namespace

class TestTodoSeriesSplit : public QObject {
    Q_OBJECT
private slots:

    // Unbounded RRULE: clean split. Old master's UNTIL is tightened to
    // just before splitInstant; new master's RRULE is the ORIGINAL,
    // untightened line.
    void unboundedRruleSplitsCleanly()
    {
        const QJsonObject master = makeWeeklyMaster();
        const QDateTime splitInstant =
            QDateTime::fromString(QStringLiteral("2026-06-01T09:00:00Z"), Qt::ISODate);

        const SeriesSplitResult r = splitSeriesAtInstant(master, splitInstant, {});
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.error.isEmpty());

        const QString oldLine = recurrenceLine(r.updatedOldMaster);
        QVERIFY2(oldLine.contains(QStringLiteral("UNTIL=20260601T085959Z")),
                 qPrintable(QStringLiteral("expected UNTIL=20260601T085959Z (1s before "
                                           "splitInstant); got: ") + oldLine));
        QVERIFY2(oldLine.startsWith(QStringLiteral("RRULE:FREQ=WEEKLY;BYDAY=MO")),
                 qPrintable(oldLine));

        QCOMPARE(recurrenceLine(r.newMaster), QStringLiteral("RRULE:FREQ=WEEKLY;BYDAY=MO"));
    }

    // UNTIL-bounded RRULE where the original bound is AFTER the split
    // point: the tightened UNTIL becomes the split-derived bound (the
    // original bound would have let the old master over-run into the new
    // master's territory).
    void untilBoundedRruleTightensToSplitPoint()
    {
        QJsonObject master = makeWeeklyMaster();
        master.insert(QStringLiteral("recurrence"),
                      QJsonArray{ QStringLiteral(
                          "RRULE:FREQ=DAILY;UNTIL=20260801T000000Z") });
        const QDateTime splitInstant =
            QDateTime::fromString(QStringLiteral("2026-06-01T09:00:00Z"), Qt::ISODate);

        const SeriesSplitResult r = splitSeriesAtInstant(master, splitInstant, {});
        QVERIFY2(r.ok, qPrintable(r.error));

        const QString oldLine = recurrenceLine(r.updatedOldMaster);
        QVERIFY2(oldLine.contains(QStringLiteral("UNTIL=20260601T085959Z")),
                 qPrintable(oldLine));
        // exactly one UNTIL token — the original was replaced, not duplicated
        QCOMPARE(oldLine.count(QStringLiteral("UNTIL=")), 1);
    }

    // UNTIL-bounded RRULE where the original bound is BEFORE the split
    // point: the tightened UNTIL must NOT loosen past the original bound
    // (i.e. must stay at the original bound, not move later to the split
    // point).
    void untilBoundedRruleNeverLoosensPastOriginalBound()
    {
        QJsonObject master = makeWeeklyMaster();
        master.insert(QStringLiteral("recurrence"),
                      QJsonArray{ QStringLiteral(
                          "RRULE:FREQ=DAILY;UNTIL=20260501T000000Z") });
        const QDateTime splitInstant =
            QDateTime::fromString(QStringLiteral("2026-06-01T09:00:00Z"), Qt::ISODate);

        const SeriesSplitResult r = splitSeriesAtInstant(master, splitInstant, {});
        QVERIFY2(r.ok, qPrintable(r.error));

        const QString oldLine = recurrenceLine(r.updatedOldMaster);
        QVERIFY2(oldLine.contains(QStringLiteral("UNTIL=20260501T000000Z")),
                 qPrintable(QStringLiteral(
                     "the original (earlier) bound must be preserved, not "
                     "loosened to the split point; got: ") + oldLine));
    }

    // COUNT-bounded RRULE: v1 fails loud rather than mis-computing COUNT.
    void countBoundedRruleFailsLoud()
    {
        QJsonObject master = makeWeeklyMaster();
        master.insert(QStringLiteral("recurrence"),
                      QJsonArray{ QStringLiteral("RRULE:FREQ=DAILY;COUNT=10") });
        const QDateTime splitInstant =
            QDateTime::fromString(QStringLiteral("2026-06-01T09:00:00Z"), Qt::ISODate);

        const SeriesSplitResult r = splitSeriesAtInstant(master, splitInstant, {});
        QVERIFY2(!r.ok, "COUNT-bounded RRULE must fail loud, not succeed");
        QVERIFY2(!r.error.isEmpty(), "a failed split must carry a descriptive error");
    }

    // Exceptions before the split instant are excluded from
    // rebasedExceptions (stay keyed to the old master).
    void exceptionsBeforeSplitInstantAreExcluded()
    {
        const QJsonObject master = makeWeeklyMaster();
        const QDateTime splitInstant =
            QDateTime::fromString(QStringLiteral("2026-06-01T09:00:00Z"), Qt::ISODate);
        const QJsonObject before =
            makeException(QStringLiteral("weekly-series-1"), QStringLiteral("2026-05-25T09:00:00Z"));

        const SeriesSplitResult r =
            splitSeriesAtInstant(master, splitInstant, { before });
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY2(r.rebasedExceptions.isEmpty(),
                 "an exception before the split instant must not be rebased");
    }

    // Exceptions at/after the split instant appear in rebasedExceptions
    // with uid == newMaster's uid and an unchanged recurrenceId.
    void exceptionsAtOrAfterSplitInstantAreRebased()
    {
        const QJsonObject master = makeWeeklyMaster();
        const QDateTime splitInstant =
            QDateTime::fromString(QStringLiteral("2026-06-01T09:00:00Z"), Qt::ISODate);
        const QJsonObject atSplit =
            makeException(QStringLiteral("weekly-series-1"), QStringLiteral("2026-06-01T09:00:00Z"));
        const QJsonObject after =
            makeException(QStringLiteral("weekly-series-1"), QStringLiteral("2026-06-08T09:00:00Z"));
        const QJsonObject before =
            makeException(QStringLiteral("weekly-series-1"), QStringLiteral("2026-05-25T09:00:00Z"));

        const SeriesSplitResult r =
            splitSeriesAtInstant(master, splitInstant, { before, atSplit, after });
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.rebasedExceptions.size(), 2);

        const QString newUid = r.newMaster.value(QStringLiteral("uid")).toString();
        QVERIFY(!newUid.isEmpty());
        for (const QJsonObject &rebased : r.rebasedExceptions) {
            QCOMPARE(rebased.value(QStringLiteral("uid")).toString(), newUid);
            QVERIFY2(rebased.contains(QStringLiteral("recurrenceId")),
                     "a rebased exception must keep its recurrenceId");
        }
        // recurrenceId values themselves are unchanged (same instant).
        QStringList recIds;
        for (const QJsonObject &rebased : r.rebasedExceptions)
            recIds << rebased.value(QStringLiteral("recurrenceId")).toObject()
                          .value(QStringLiteral("dateTime")).toString();
        QVERIFY(recIds.contains(QStringLiteral("2026-06-01T09:00:00Z")));
        QVERIFY(recIds.contains(QStringLiteral("2026-06-08T09:00:00Z")));
    }

    // Master hygiene: the new master carries neither recurrenceId nor
    // recurrenceRange, but does carry seriesSplitOf == the old uid — and
    // its uid is deterministic (old uid + "-split-" + sanitized stamp).
    void newMasterHygieneAndDeterministicUid()
    {
        const QJsonObject master = makeWeeklyMaster();
        const QDateTime splitInstant =
            QDateTime::fromString(QStringLiteral("2026-06-01T09:00:00Z"), Qt::ISODate);

        const SeriesSplitResult r = splitSeriesAtInstant(master, splitInstant, {});
        QVERIFY2(r.ok, qPrintable(r.error));

        QVERIFY2(!r.newMaster.contains(QStringLiteral("recurrenceId")),
                 "new master must not carry recurrenceId");
        QVERIFY2(!r.newMaster.contains(QStringLiteral("recurrenceRange")),
                 "new master must not carry recurrenceRange");
        QCOMPARE(r.newMaster.value(QStringLiteral("seriesSplitOf")).toString(),
                 QStringLiteral("weekly-series-1"));
        QCOMPARE(r.newMaster.value(QStringLiteral("uid")).toString(),
                 QStringLiteral("weekly-series-1-split-20260601T090000Z"));

        // Deterministic: calling again with the same inputs reproduces the
        // identical new-master uid (idempotent retry — Open decision 3).
        const SeriesSplitResult r2 = splitSeriesAtInstant(master, splitInstant, {});
        QCOMPARE(r2.newMaster.value(QStringLiteral("uid")).toString(),
                 r.newMaster.value(QStringLiteral("uid")).toString());
    }

    // The old master's uid is untouched by the split (only its RRULE
    // line's UNTIL changes) — it stays addressable at its original identity.
    void oldMasterUidIsUnchanged()
    {
        const QJsonObject master = makeWeeklyMaster();
        const QDateTime splitInstant =
            QDateTime::fromString(QStringLiteral("2026-06-01T09:00:00Z"), Qt::ISODate);

        const SeriesSplitResult r = splitSeriesAtInstant(master, splitInstant, {});
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.updatedOldMaster.value(QStringLiteral("uid")).toString(),
                 QStringLiteral("weekly-series-1"));
    }

    // Fail-loud guards: no RRULE line, a detached exception passed as
    // "master", and an invalid splitInstant.
    void failsLoudOnMalformedInput()
    {
        {
            QJsonObject master = makeWeeklyMaster();
            master.remove(QStringLiteral("recurrence"));
            const SeriesSplitResult r = splitSeriesAtInstant(
                master, QDateTime::fromString(QStringLiteral("2026-06-01T09:00:00Z"), Qt::ISODate), {});
            QVERIFY2(!r.ok, "a master with no RRULE line must fail loud");
        }
        {
            QJsonObject exceptionAsMaster = makeWeeklyMaster();
            exceptionAsMaster.insert(QStringLiteral("recurrenceId"),
                QJsonObject{ { QStringLiteral("dateTime"), QStringLiteral("2026-06-01T09:00:00Z") } });
            const SeriesSplitResult r = splitSeriesAtInstant(
                exceptionAsMaster, QDateTime::fromString(QStringLiteral("2026-06-08T09:00:00Z"), Qt::ISODate), {});
            QVERIFY2(!r.ok, "a detached exception passed as masterCanon must fail loud");
        }
        {
            const SeriesSplitResult r = splitSeriesAtInstant(makeWeeklyMaster(), QDateTime{}, {});
            QVERIFY2(!r.ok, "an invalid splitInstant must fail loud");
        }
    }
};

QTEST_GUILESS_MAIN(TestTodoSeriesSplit)
#include "tst_todo_series_split.moc"
