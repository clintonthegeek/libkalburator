// EEE Phase 7.B step 1 — RFC5545 ⇄ Graph patternedRecurrence converter unit
// suite. Pins every reference-§1.3 row in BOTH directions plus every declared
// cannot-represent ruling and the live-corpus sentinel findings (O57(e)/(f)).
// Declared-loss source: docs/2026-08-23-ms-event-edge-loss-profile.md.

#include <QTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "recurrencepatternconverter.h"

using Kalburator::Calendar::RecurrencePattern::DemoteResult;
using Kalburator::Calendar::RecurrencePattern::rruleLinesToPatternedRecurrence;
using Kalburator::Calendar::RecurrencePattern::patternedRecurrenceToRruleLines;

namespace {

QJsonObject prFrom(const char* json)
{
    return QJsonDocument::fromJson(QByteArray(json)).object();
}

QString rruleOf(const QStringList& lines)
{
    for (const QString& l : lines)
        if (l.startsWith(QLatin1String("RRULE:")))
            return l;
    return {};
}

} // namespace

class TestRecurrencePatternConverter : public QObject {
    Q_OBJECT

private slots:

    // ---- MS → RFC5545 (promote): lossless per §1.3 ------------------------

    void msDailyMapsToRrule()
    {
        const auto lines = patternedRecurrenceToRruleLines(prFrom(
            "{\"pattern\":{\"type\":\"daily\",\"interval\":2},"
            "\"range\":{\"type\":\"noEnd\",\"startDate\":\"2026-01-01\"}}"));
        QCOMPARE(lines.size(), 1);
        QCOMPARE(rruleOf(lines), QStringLiteral("RRULE:FREQ=DAILY;INTERVAL=2"));
    }

    void msWeeklyWithDaysAndWkst()
    {
        const auto lines = patternedRecurrenceToRruleLines(prFrom(
            "{\"pattern\":{\"type\":\"weekly\",\"interval\":1,"
            "\"daysOfWeek\":[\"thursday\"],\"firstDayOfWeek\":\"monday\","
            // O57(f): live weekly patterns still serialize index:"first".
            "\"index\":\"first\"},"
            "\"range\":{\"type\":\"noEnd\",\"startDate\":\"2026-01-01\"}}"));
        QCOMPARE(rruleOf(lines),
                 QStringLiteral("RRULE:FREQ=WEEKLY;INTERVAL=1;BYDAY=TH;WKST=MO"));
    }

    void msWeeklyMultiDay()
    {
        const auto lines = patternedRecurrenceToRruleLines(prFrom(
            "{\"pattern\":{\"type\":\"weekly\",\"interval\":2,"
            "\"daysOfWeek\":[\"monday\",\"wednesday\",\"friday\"]},"
            "\"range\":{\"type\":\"noEnd\",\"startDate\":\"2026-01-01\"}}"));
        QCOMPARE(rruleOf(lines),
                 QStringLiteral("RRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,WE,FR"));
    }

    void msAbsoluteMonthly()
    {
        const auto lines = patternedRecurrenceToRruleLines(prFrom(
            "{\"pattern\":{\"type\":\"absoluteMonthly\",\"interval\":3,\"dayOfMonth\":15},"
            "\"range\":{\"type\":\"numbered\",\"startDate\":\"2026-01-15\","
            "\"numberOfOccurrences\":6}}"));
        QCOMPARE(rruleOf(lines), QStringLiteral("RRULE:FREQ=MONTHLY;INTERVAL=3;BYMONTHDAY=15;COUNT=6"));
    }

    void msRelativeMonthlyIndexBecomesBysetpos()
    {
        const auto lines = patternedRecurrenceToRruleLines(prFrom(
            "{\"pattern\":{\"type\":\"relativeMonthly\",\"interval\":1,"
            "\"daysOfWeek\":[\"thursday\"],\"index\":\"fourth\"},"
            "\"range\":{\"type\":\"noEnd\",\"startDate\":\"2026-01-01\"}}"));
        QCOMPARE(rruleOf(lines),
                 QStringLiteral("RRULE:FREQ=MONTHLY;INTERVAL=1;BYDAY=TH;BYSETPOS=4"));
    }

    void msAbsoluteYearly()
    {
        const auto lines = patternedRecurrenceToRruleLines(prFrom(
            "{\"pattern\":{\"type\":\"absoluteYearly\",\"interval\":1,"
            "\"month\":8,\"dayOfMonth\":23},"
            "\"range\":{\"type\":\"noEnd\",\"startDate\":\"2026-08-23\"}}"));
        QCOMPARE(rruleOf(lines),
                 QStringLiteral("RRULE:FREQ=YEARLY;INTERVAL=1;BYMONTH=8;BYMONTHDAY=23"));
    }

    void msRelativeYearlyLastMondayOfSeptember()
    {
        const auto lines = patternedRecurrenceToRruleLines(prFrom(
            "{\"pattern\":{\"type\":\"relativeYearly\",\"interval\":1,"
            "\"month\":9,\"daysOfWeek\":[\"monday\"],\"index\":\"last\"},"
            "\"range\":{\"type\":\"noEnd\",\"startDate\":\"2026-09-01\"}}"));
        QCOMPARE(rruleOf(lines),
                 QStringLiteral("RRULE:FREQ=YEARLY;INTERVAL=1;BYMONTH=9;BYDAY=MO;BYSETPOS=-1"));
    }

    void msRangeEndDateBecomesUntil()
    {
        const auto lines = patternedRecurrenceToRruleLines(prFrom(
            "{\"pattern\":{\"type\":\"daily\",\"interval\":1},"
            "\"range\":{\"type\":\"endDate\",\"startDate\":\"2026-01-01\","
            "\"endDate\":\"2026-12-31\"}}"));
        QCOMPARE(lines.size(), 1);
        QCOMPARE(rruleOf(lines), QStringLiteral("RRULE:FREQ=DAILY;INTERVAL=1;UNTIL=20261231T235959Z"));
    }

    void cancelledOccurrencsBecomeExdates()
    {
        const auto lines = patternedRecurrenceToRruleLines(prFrom(
            "{\"pattern\":{\"type\":\"weekly\",\"interval\":1,"
            "\"daysOfWeek\":[\"thursday\"]},"
            "\"range\":{\"type\":\"noEnd\",\"startDate\":\"2026-01-01\"}}"),
            { QStringLiteral("2026-08-27T07:00:00Z"),
              QStringLiteral("2026-09-10T07:00:00Z") });
        QVERIFY(lines.contains(QStringLiteral(
            "EXDATE:20260827T070000Z,20260910T070000Z")));
    }

    void unparsedCancellationsSurfaceForStash()
    {
        QStringList unparsed;
        const auto lines = patternedRecurrenceToRruleLines(prFrom(
            "{\"pattern\":{\"type\":\"daily\",\"interval\":1},"
            "\"range\":{\"type\":\"noEnd\",\"startDate\":\"2026-01-01\"}}"),
            { QStringLiteral("OE9AMBase64Blob==") }, &unparsed);
        QCOMPARE(unparsed.size(), 1);
        QCOMPARE(unparsed.first(), QStringLiteral("OE9AMBase64Blob=="));
        QVERIFY(!lines.isEmpty()); // RRULE still emitted
    }

    // ---- O57(e)/O57(f) sentinel handling ----------------------------------

    void zeroSentinelsTreatedAsAbsentAndDerivedFromStartDate()
    {
        // absoluteYearly with month/dayOfMonth serialized as 0 (O57(e)):
        // derive from range.startDate instead of emitting BYMONTH=0.
        const auto lines = patternedRecurrenceToRruleLines(prFrom(
            "{\"pattern\":{\"type\":\"absoluteYearly\",\"interval\":1,"
            "\"month\":0,\"dayOfMonth\":0},"
            "\"range\":{\"type\":\"noEnd\",\"startDate\":\"2026-08-23\"}}"));
        QCOMPARE(rruleOf(lines),
                 QStringLiteral("RRULE:FREQ=YEARLY;INTERVAL=1;BYMONTH=8;BYMONTHDAY=23"));
    }

    void weeklyIndexIgnoredPerO57f()
    {
        // Covered in msWeeklyWithDaysAndWkst; here pin that a NON-relative
        // monthly with stray index doesn't emit BYSETPOS.
        const auto lines = patternedRecurrenceToRruleLines(prFrom(
            "{\"pattern\":{\"type\":\"absoluteMonthly\",\"interval\":1,"
            "\"dayOfMonth\":7,\"index\":\"last\"},"
            "\"range\":{\"type\":\"noEnd\",\"startDate\":\"2026-01-07\"}}"));
        QCOMPARE(rruleOf(lines), QStringLiteral("RRULE:FREQ=MONTHLY;INTERVAL=1;BYMONTHDAY=7"));
    }

    // ---- RFC5545 → MS (demote): representable rows --------------------------

    void rfcDailyMapsToGraphDaily()
    {
        const auto r = rruleLinesToPatternedRecurrence(
            { QStringLiteral("RRULE:FREQ=DAILY;COUNT=10") },
            QStringLiteral("2026-06-01T13:00:00Z"));
        QVERIFY(r.carriedLines.isEmpty());
        QCOMPARE(r.patternedRecurrence.value(QStringLiteral("pattern"))
                     .toObject().value(QStringLiteral("type")).toString(),
                 QStringLiteral("daily"));
        QCOMPARE(r.patternedRecurrence.value(QStringLiteral("range"))
                     .toObject().value(QStringLiteral("numberOfOccurrences")).toInt(), 10);
    }

    void rfcWeeklyBydayWkstRoundTrips()
    {
        const auto r = rruleLinesToPatternedRecurrence(
            { QStringLiteral("RRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=TU,TH;WKST=SU") },
            QStringLiteral("2026-01-01T09:00:00Z"));
        QVERIFY(r.carriedLines.isEmpty());
        const QJsonObject pattern =
            r.patternedRecurrence.value(QStringLiteral("pattern")).toObject();
        QCOMPARE(pattern.value(QStringLiteral("type")).toString(),
                 QStringLiteral("weekly"));
        QCOMPARE(pattern.value(QStringLiteral("interval")).toInt(), 2);
        QCOMPARE(pattern.value(QStringLiteral("daysOfWeek")).toArray().size(), 2);
        QCOMPARE(pattern.value(QStringLiteral("firstDayOfWeek")).toString(),
                 QStringLiteral("sunday"));
        QCOMPARE(r.patternedRecurrence.value(QStringLiteral("range"))
                     .toObject().value(QStringLiteral("type")).toString(),
                 QStringLiteral("noEnd"));
    }

    void rfcUntilBecomesEndDateRange()
    {
        const auto r = rruleLinesToPatternedRecurrence(
            { QStringLiteral("RRULE:FREQ=DAILY;UNTIL=20261231T235959Z") },
            QStringLiteral("2026-06-01T09:00:00Z"));
        const QJsonObject range =
            r.patternedRecurrence.value(QStringLiteral("range")).toObject();
        QCOMPARE(range.value(QStringLiteral("type")).toString(),
                 QStringLiteral("endDate"));
        QCOMPARE(range.value(QStringLiteral("endDate")).toString(),
                 QStringLiteral("2026-12-31"));
    }

    void rfcBysetposMonthlyBecomesRelativeMonthly()
    {
        const auto r = rruleLinesToPatternedRecurrence(
            { QStringLiteral("RRULE:FREQ=MONTHLY;INTERVAL=1;BYDAY=TH;BYSETPOS=-1") },
            QStringLiteral("2026-01-01T09:00:00Z"));
        QVERIFY(r.carriedLines.isEmpty());
        const QJsonObject pattern =
            r.patternedRecurrence.value(QStringLiteral("pattern")).toObject();
        QCOMPARE(pattern.value(QStringLiteral("type")).toString(),
                 QStringLiteral("relativeMonthly"));
        QCOMPARE(pattern.value(QStringLiteral("index")).toString(),
                 QStringLiteral("last"));
    }

    // ---- RFC5545 → MS: cannot-represent rulings ------------------------------

    void subDailyFreqReducesToDailyAndCarries()
    {
        const auto r = rruleLinesToPatternedRecurrence(
            { QStringLiteral("RRULE:FREQ=HOURLY;INTERVAL=6") },
            QStringLiteral("2026-01-01T09:00:00Z"));
        QCOMPARE(r.patternedRecurrence.value(QStringLiteral("pattern"))
                     .toObject().value(QStringLiteral("type")).toString(),
                 QStringLiteral("daily"));
        QVERIFY(r.carriedLines.contains(
            QStringLiteral("RRULE:FREQ=HOURLY;INTERVAL=6")));
    }

    void byweeknoAndByyeardayCarryOriginalRule()
    {
        const auto r = rruleLinesToPatternedRecurrence(
            { QStringLiteral("RRULE:FREQ=YEARLY;BYWEEKNO=20;BYDAY=MO") },
            QStringLiteral("2026-01-01T09:00:00Z"));
        QVERIFY(r.carriedLines.contains(
            QStringLiteral("RRULE:FREQ=YEARLY;BYWEEKNO=20;BYDAY=MO")));

        const auto r2 = rruleLinesToPatternedRecurrence(
            { QStringLiteral("RRULE:FREQ=YEARLY;BYYEARDAY=140") },
            QStringLiteral("2026-01-01T09:00:00Z"));
        QVERIFY(r2.carriedLines.contains(
            QStringLiteral("RRULE:FREQ=YEARLY;BYYEARDAY=140")));
    }

    void fridayTheThirteenthCarries()
    {
        const auto r = rruleLinesToPatternedRecurrence(
            { QStringLiteral("RRULE:FREQ=MONTHLY;BYDAY=FR;BYMONTHDAY=13") },
            QStringLiteral("2026-01-01T09:00:00Z"));
        QVERIFY(r.carriedLines.contains(
            QStringLiteral("RRULE:FREQ=MONTHLY;BYDAY=FR;BYMONTHDAY=13")));
    }

    void multiValueBymonthdayEmitsFirstAndCarries()
    {
        const auto r = rruleLinesToPatternedRecurrence(
            { QStringLiteral("RRULE:FREQ=MONTHLY;BYMONTHDAY=1,15") },
            QStringLiteral("2026-01-01T09:00:00Z"));
        QCOMPARE(r.patternedRecurrence.value(QStringLiteral("pattern"))
                     .toObject().value(QStringLiteral("dayOfMonth")).toInt(), 1);
        QVERIFY(r.carriedLines.contains(
            QStringLiteral("RRULE:FREQ=MONTHLY;BYMONTHDAY=1,15")));
    }

    void multiValueBymonthEmitsFirstAndCarries()
    {
        const auto r = rruleLinesToPatternedRecurrence(
            { QStringLiteral("RRULE:FREQ=YEARLY;BYMONTH=3,9;BYMONTHDAY=5") },
            QStringLiteral("2026-01-01T09:00:00Z"));
        QCOMPARE(r.patternedRecurrence.value(QStringLiteral("pattern"))
                     .toObject().value(QStringLiteral("month")).toInt(), 3);
        QVERIFY(r.carriedLines.contains(
            QStringLiteral("RRULE:FREQ=YEARLY;BYMONTH=3,9;BYMONTHDAY=5")));
    }

    void multipleRrulesFirstEmittedAllCarried()
    {
        const auto r = rruleLinesToPatternedRecurrence(
            { QStringLiteral("RRULE:FREQ=DAILY;INTERVAL=1"),
              QStringLiteral("RRULE:FREQ=WEEKLY;BYDAY=MO") },
            QStringLiteral("2026-01-01T09:00:00Z"));
        QCOMPARE(r.patternedRecurrence.value(QStringLiteral("pattern"))
                     .toObject().value(QStringLiteral("type")).toString(),
                 QStringLiteral("daily"));
        QVERIFY(r.carriedLines.contains(
            QStringLiteral("RRULE:FREQ=DAILY;INTERVAL=1")));
        QVERIFY(r.carriedLines.contains(
            QStringLiteral("RRULE:FREQ=WEEKLY;BYDAY=MO")));
    }

    void exruleCarriedOnly()
    {
        const auto r = rruleLinesToPatternedRecurrence(
            { QStringLiteral("RRULE:FREQ=DAILY;INTERVAL=1"),
              QStringLiteral("EXRULE:FREQ=WEEKLY;BYDAY=SU") },
            QStringLiteral("2026-01-01T09:00:00Z"));
        QVERIFY(r.carriedLines.contains(QStringLiteral("EXRULE:FREQ=WEEKLY;BYDAY=SU")));
    }

    void rdateDroppedFromPatternAndCarried()
    {
        const auto r = rruleLinesToPatternedRecurrence(
            { QStringLiteral("RRULE:FREQ=DAILY;INTERVAL=1"),
              QStringLiteral("RDATE:20260704T090000Z") },
            QStringLiteral("2026-01-01T09:00:00Z"));
        QVERIFY(r.patternedRecurrence.contains(QStringLiteral("pattern")));
        QVERIFY(r.carriedLines.contains(QStringLiteral("RDATE:20260704T090000Z")));
    }

    void wkstOnNonWeeklyReducesAndCarries()
    {
        const auto r = rruleLinesToPatternedRecurrence(
            { QStringLiteral("RRULE:FREQ=MONTHLY;INTERVAL=1;BYDAY=MO;WKST=SU") },
            QStringLiteral("2026-01-01T09:00:00Z"));
        QVERIFY(r.carriedLines.contains(
            QStringLiteral("RRULE:FREQ=MONTHLY;INTERVAL=1;BYDAY=MO;WKST=SU")));
    }

    void plainMonthlyBydayReducesToFirstIndexAndCarries()
    {
        // "Every Monday of the month" is NOT relativeMonthly:index=first
        // ("first Monday"); reduce + carry so re-promote stays byte-equal.
        const auto r = rruleLinesToPatternedRecurrence(
            { QStringLiteral("RRULE:FREQ=MONTHLY;BYDAY=MO") },
            QStringLiteral("2026-01-01T09:00:00Z"));
        QCOMPARE(r.patternedRecurrence.value(QStringLiteral("pattern"))
                     .toObject().value(QStringLiteral("index")).toString(),
                 QStringLiteral("first"));
        QVERIFY(r.carriedLines.contains(
            QStringLiteral("RRULE:FREQ=MONTHLY;BYDAY=MO")));
    }

    // ---- round-trip identity for the carried set ------------------------------

    void carriedCasesRePromoteByteIdentically()
    {
        const QStringList cases = {
            QStringLiteral("RRULE:FREQ=HOURLY;INTERVAL=6"),
            QStringLiteral("RRULE:FREQ=MINUTELY;INTERVAL=30"),
            QStringLiteral("RRULE:FREQ=SECONDLY;INTERVAL=15"),
            QStringLiteral("RRULE:FREQ=YEARLY;BYWEEKNO=20;BYDAY=MO"),
            QStringLiteral("RRULE:FREQ=YEARLY;BYYEARDAY=140"),
            QStringLiteral("RRULE:FREQ=MONTHLY;BYDAY=FR;BYMONTHDAY=13"),
            QStringLiteral("RRULE:FREQ=MONTHLY;BYMONTHDAY=1,15"),
            QStringLiteral("RRULE:FREQ=YEARLY;BYMONTH=3,9;BYMONTHDAY=5"),
            QStringLiteral("RRULE:FREQ=MONTHLY;BYDAY=2MO"),   // ordinal, no BYSETPOS
            QStringLiteral("RRULE:FREQ=MONTHLY;INTERVAL=1;BYDAY=MO;WKST=SU")
        };
        for (const QString& line : cases) {
            const auto r = rruleLinesToPatternedRecurrence(
                { line }, QStringLiteral("2026-01-01T09:00:00Z"));
            QVERIFY2(r.carriedLines.contains(line),
                     qPrintable(QStringLiteral("missing carrier for %1").arg(line)));
        }
    }

    // ---- full promote⇄demote convergence on the representable set -------------

    void representableSetConvergesBothDirections()
    {
        // MS pattern → RRULE → MS pattern must reproduce an equivalent
        // patternedRecurrence for every §1.3 row.
        const QList<QByteArray> rows = {
            { QByteArray("{\"pattern\":{\"type\":\"daily\",\"interval\":3},"
                         "\"range\":{\"type\":\"noEnd\",\"startDate\":\"2026-01-01\"}}") },
            { QByteArray("{\"pattern\":{\"type\":\"weekly\",\"interval\":2,"
                         "\"daysOfWeek\":[\"monday\",\"tuesday\"],"
                         "\"firstDayOfWeek\":\"monday\"},"
                         "\"range\":{\"type\":\"noEnd\",\"startDate\":\"2026-01-01\"}}") },
            { QByteArray("{\"pattern\":{\"type\":\"absoluteMonthly\",\"interval\":1,"
                         "\"dayOfMonth\":31},"
                         "\"range\":{\"type\":\"numbered\",\"startDate\":\"2026-01-31\","
                         "\"numberOfOccurrences\":4}}") },
            { QByteArray("{\"pattern\":{\"type\":\"relativeMonthly\",\"interval\":1,"
                         "\"daysOfWeek\":[\"friday\"],\"index\":\"first\"},"
                         "\"range\":{\"type\":\"noEnd\",\"startDate\":\"2026-01-01\"}}") },
            { QByteArray("{\"pattern\":{\"type\":\"absoluteYearly\",\"interval\":1,"
                         "\"month\":12,\"dayOfMonth\":24},"
                         "\"range\":{\"type\":\"noEnd\",\"startDate\":\"2026-12-24\"}}") },
            { QByteArray("{\"pattern\":{\"type\":\"relativeYearly\",\"interval\":1,"
                         "\"month\":11,\"daysOfWeek\":[\"thursday\"],\"index\":\"fourth\"},"
                         "\"range\":{\"type\":\"noEnd\",\"startDate\":\"2026-11-01\"}}") }
        };
        for (const QByteArray& wire : rows) {
            const QJsonObject pr0 = QJsonDocument::fromJson(wire).object();
            const QStringList rrule = patternedRecurrenceToRruleLines(pr0);
            QVERIFY2(!rrule.isEmpty(), wire.constData());
            const DemoteResult back = rruleLinesToPatternedRecurrence(
                rrule, pr0.value(QStringLiteral("range")).toObject()
                           .value(QStringLiteral("startDate")).toString());
            QVERIFY2(back.carriedLines.isEmpty(),
                     qPrintable(QStringLiteral("unexpected carrier for %1: %2")
                                    .arg(wire.constData(),
                                         back.carriedLines.join(QLatin1Char('|')))));
            const QJsonDocument a(pr0), b(back.patternedRecurrence);
            QCOMPARE(b.toJson(QJsonDocument::Compact),
                     a.toJson(QJsonDocument::Compact));
        }
    }

    void emptyAndDegenerateInputs()
    {
        // No RRULE at all → empty result, no crash.
        const DemoteResult r = rruleLinesToPatternedRecurrence({});
        QVERIFY(r.patternedRecurrence.isEmpty());
        QVERIFY(r.carriedLines.isEmpty());

        // Empty patternedRecurrence → no lines.
        QVERIFY(patternedRecurrenceToRruleLines({}).isEmpty());

        // Unknown pattern type → no lines rather than a wrong rule.
        QVERIFY(patternedRecurrenceToRruleLines(prFrom(
                    "{\"pattern\":{\"type\":\"quantum\",\"interval\":1},"
                    "\"range\":{\"type\":\"noEnd\",\"startDate\":\"2026-01-01\"}}"))
                    .isEmpty());
    }
};

QTEST_MAIN(TestRecurrencePatternConverter)
#include "tst_recurrence_pattern_converter.moc"
