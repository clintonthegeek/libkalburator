// tst_icalcomponentscan.cpp
//
// N1 fix — direct unit tests for the shared component-scoped recurrence-line
// extraction helper (Kalburator::Calendar::extractComponentRecurrenceLines),
// used by eventcanonfields.cpp, vtodocanonfields.cpp, and
// orgicalcanonstages.cpp. Tests the mechanism once, independent of any
// particular caller (INVARIANTS §1: one definition, tested once).

#include <QTest>

#include "icalcomponentscan.h"

using Kalburator::Calendar::extractComponentRecurrenceLines;

namespace {

const QByteArray kVtimezoneBlock =
    "BEGIN:VTIMEZONE\r\n"
    "TZID:America/New_York\r\n"
    "BEGIN:STANDARD\r\n"
    "DTSTART:20071104T020000\r\n"
    "RRULE:FREQ=YEARLY;BYMONTH=11;BYDAY=1SU\r\n"
    "TZOFFSETFROM:-0400\r\n"
    "TZOFFSETTO:-0500\r\n"
    "TZNAME:EST\r\n"
    "END:STANDARD\r\n"
    "BEGIN:DAYLIGHT\r\n"
    "DTSTART:20070311T020000\r\n"
    "RRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=2SU\r\n"
    "TZOFFSETFROM:-0500\r\n"
    "TZOFFSETTO:-0400\r\n"
    "TZNAME:EDT\r\n"
    "END:DAYLIGHT\r\n"
    "END:VTIMEZONE\r\n";

} // namespace

class TestICalComponentScan : public QObject {
    Q_OBJECT
private slots:

    void vtimezoneNeverContributesLines()
    {
        const QByteArray bytes =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n" + kVtimezoneBlock +
            "BEGIN:VEVENT\r\n"
            "UID:e-1\r\n"
            "DTSTART;TZID=America/New_York:20260615T100000\r\n"
            "SUMMARY:No recurrence\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";

        const auto lines = extractComponentRecurrenceLines(bytes, "VEVENT", QStringLiteral("e-1"));
        QVERIFY2(lines.isEmpty(),
                 "VTIMEZONE's STANDARD/DAYLIGHT RRULEs must never be reported "
                 "as the event's recurrence");
    }

    void ownRruleSurvivesAlongsideVtimezone()
    {
        const QByteArray bytes =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n" + kVtimezoneBlock +
            "BEGIN:VEVENT\r\n"
            "UID:e-2\r\n"
            "DTSTART;TZID=America/New_York:20260615T100000\r\n"
            "SUMMARY:Weekly\r\n"
            "RRULE:FREQ=WEEKLY\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";

        const auto lines = extractComponentRecurrenceLines(bytes, "VEVENT", QStringLiteral("e-2"));
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines.first(), QStringLiteral("RRULE:FREQ=WEEKLY"));
    }

    void componentNameScopesAwayOtherKinds()
    {
        // A VTODO with the same UID text present alongside the VEVENT must
        // not contribute its lines when scanning for "VEVENT".
        const QByteArray bytes =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "BEGIN:VTODO\r\n"
            "UID:shared-uid\r\n"
            "RRULE:FREQ=DAILY\r\n"
            "END:VTODO\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:shared-uid\r\n"
            "RRULE:FREQ=WEEKLY\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";

        const auto eventLines = extractComponentRecurrenceLines(bytes, "VEVENT", QStringLiteral("shared-uid"));
        QCOMPARE(eventLines.size(), 1);
        QCOMPARE(eventLines.first(), QStringLiteral("RRULE:FREQ=WEEKLY"));

        const auto todoLines = extractComponentRecurrenceLines(bytes, "VTODO", QStringLiteral("shared-uid"));
        QCOMPARE(todoLines.size(), 1);
        QCOMPARE(todoLines.first(), QStringLiteral("RRULE:FREQ=DAILY"));
    }

    void masterPreferredOverRecurrenceIdOverrides()
    {
        // A recurring master (RRULE, no RECURRENCE-ID) plus one override
        // instance (RECURRENCE-ID, no RRULE of its own) share a UID. The
        // master's recurrence must be the one reported.
        const QByteArray bytes =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:series-1\r\n"
            "RRULE:FREQ=DAILY\r\n"
            "SUMMARY:Series master\r\n"
            "END:VEVENT\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:series-1\r\n"
            "RECURRENCE-ID:20260602T090000Z\r\n"
            "SUMMARY:Series override\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";

        const auto lines = extractComponentRecurrenceLines(bytes, "VEVENT", QStringLiteral("series-1"));
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines.first(), QStringLiteral("RRULE:FREQ=DAILY"));
    }

    void nestedValarmNeverContributesLines()
    {
        const QByteArray bytes =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:e-3\r\n"
            "SUMMARY:Has an alarm\r\n"
            "BEGIN:VALARM\r\n"
            "ACTION:DISPLAY\r\n"
            "RRULE:FREQ=DAILY\r\n"  // malformed/synthetic — must never surface
            "END:VALARM\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";

        const auto lines = extractComponentRecurrenceLines(bytes, "VEVENT", QStringLiteral("e-3"));
        QVERIFY2(lines.isEmpty(), "a VALARM's body must never contribute recurrence lines");
    }

    void foldedLineIsUnfoldedBeforeMatching()
    {
        const QByteArray bytes =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:folded-1\r\n"
            "RRULE:FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR;WKST=MO;UNTIL=2026123\r\n"
            " 1T000000Z\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";

        const auto lines = extractComponentRecurrenceLines(bytes, "VEVENT", QStringLiteral("folded-1"));
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines.first(),
                 QStringLiteral("RRULE:FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR;WKST=MO;UNTIL=20261231T000000Z"));
    }

    // --- recurrenceId-selected extraction (VP.c-step-1a) -------------------

    void roleSelectedExtractionReturnsExceptionBlockLines()
    {
        // Master (RRULE) plus one override carrying its own EXDATE. The
        // selector must return the OVERRIDE block's lines, not the master's.
        const QByteArray bytes =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:series-2\r\n"
            "RRULE:FREQ=DAILY\r\n"
            "SUMMARY:Series master\r\n"
            "END:VEVENT\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:series-2\r\n"
            "RECURRENCE-ID:20260602T090000Z\r\n"
            "SUMMARY:Series override\r\n"
            "EXDATE:20260603T090000Z\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";

        const auto lines = extractComponentRecurrenceLines(
            bytes, "VEVENT", QStringLiteral("series-2"),
            QStringLiteral("2026-06-02T09:00:00Z"));
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines.first(), QStringLiteral("EXDATE:20260603T090000Z"));

        // Default slot (no selector) still prefers the master.
        const auto defaultLines = extractComponentRecurrenceLines(
            bytes, "VEVENT", QStringLiteral("series-2"));
        QCOMPARE(defaultLines.size(), 1);
        QCOMPARE(defaultLines.first(), QStringLiteral("RRULE:FREQ=DAILY"));
    }

    void roleSelectorMatchesAcrossZoneForms()
    {
        // The override expresses its RECURRENCE-ID with a TZID parameter
        // (08:00 EDT == 12:00 UTC); the selector is the UTC-normalized form.
        const QByteArray bytes =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:series-3\r\n"
            "RRULE:FREQ=WEEKLY\r\n"
            "END:VEVENT\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:series-3\r\n"
            "RECURRENCE-ID;TZID=America/New_York:20260602T080000\r\n"
            "SUMMARY:Tz override\r\n"
            "RDATE:20260609T120000Z\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";

        const auto lines = extractComponentRecurrenceLines(
            bytes, "VEVENT", QStringLiteral("series-3"),
            QStringLiteral("2026-06-02T12:00:00Z"));
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines.first(), QStringLiteral("RDATE:20260609T120000Z"));
    }

    void roleSelectorNoMatchReturnsEmpty()
    {
        const QByteArray bytes =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:series-4\r\n"
            "RRULE:FREQ=DAILY\r\n"
            "END:VEVENT\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:series-4\r\n"
            "RECURRENCE-ID:20260602T090000Z\r\n"
            "SUMMARY:Override\r\n"
            "EXDATE:20260603T090000Z\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";

        // A non-matching selector returns empty — NO fallback to the
        // master's lines (the caller asked for a specific instance).
        const auto lines = extractComponentRecurrenceLines(
            bytes, "VEVENT", QStringLiteral("series-4"),
            QStringLiteral("2026-07-04T09:00:00Z"));
        QVERIFY2(lines.isEmpty(),
                 "a non-matching recurrenceId selector must yield no lines");

        // And a DATE-form override matches via UTC-midnight normalization.
        const QByteArray dateForm =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:series-5\r\n"
            "RRULE:FREQ=DAILY\r\n"
            "END:VEVENT\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:series-5\r\n"
            "RECURRENCE-ID;VALUE=DATE:20260602\r\n"
            "SUMMARY:All-day override\r\n"
            "EXDATE:20260603T000000Z\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";
        const auto dateLines = extractComponentRecurrenceLines(
            dateForm, "VEVENT", QStringLiteral("series-5"),
            QStringLiteral("2026-06-02T00:00:00Z"));
        QCOMPARE(dateLines.size(), 1);
        QCOMPARE(dateLines.first(), QStringLiteral("EXDATE:20260603T000000Z"));
    }
};

QTEST_GUILESS_MAIN(TestICalComponentScan)
#include "tst_icalcomponentscan.moc"
