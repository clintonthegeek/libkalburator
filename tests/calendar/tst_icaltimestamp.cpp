// tst_icaltimestamp.cpp
//
// N3 fix — direct unit tests for the shared authoritative-timestamp
// extraction helper (Kalburator::Calendar::extractICalTimestamp), used by
// RemoteCalendarBackend::blobRecordFromIcal to stop stamping every remote
// record's lastModified as "now" (which defeated LastWriteWins).

#include <QTest>

#include "icaltimestamp.h"

using Kalburator::Calendar::extractICalTimestamp;
using Kalburator::Calendar::stripICalPropertyParameter;

class TestICalTimestamp : public QObject {
    Q_OBJECT
private slots:

    void lastModifiedIsPreferred()
    {
        const QByteArray ical =
            "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
            "BEGIN:VEVENT\r\nUID:e-1\r\n"
            "CREATED:20240101T000000Z\r\n"
            "DTSTAMP:20250601T000000Z\r\n"
            "LAST-MODIFIED:20250815T093000Z\r\n"
            "SUMMARY:Test\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

        const QDateTime dt = extractICalTimestamp(ical);
        QVERIFY(dt.isValid());
        QCOMPARE(dt, QDateTime(QDate(2025, 8, 15), QTime(9, 30, 0), QTimeZone::utc()));
    }

    void dtstampIsFallbackWhenNoLastModified()
    {
        const QByteArray ical =
            "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
            "BEGIN:VEVENT\r\nUID:e-2\r\n"
            "CREATED:20240101T000000Z\r\n"
            "DTSTAMP:20250601T120000Z\r\n"
            "SUMMARY:Test\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

        const QDateTime dt = extractICalTimestamp(ical);
        QVERIFY(dt.isValid());
        QCOMPARE(dt, QDateTime(QDate(2025, 6, 1), QTime(12, 0, 0), QTimeZone::utc()));
    }

    void createdIsFallbackWhenNoLastModifiedOrDtstamp()
    {
        const QByteArray ical =
            "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
            "BEGIN:VEVENT\r\nUID:e-3\r\n"
            "CREATED:20230314T081500Z\r\n"
            "SUMMARY:Test\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

        const QDateTime dt = extractICalTimestamp(ical);
        QVERIFY(dt.isValid());
        QCOMPARE(dt, QDateTime(QDate(2023, 3, 14), QTime(8, 15, 0), QTimeZone::utc()));
    }

    void invalidWhenNoTimestampPropertyPresent()
    {
        const QByteArray ical =
            "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
            "BEGIN:VEVENT\r\nUID:e-4\r\n"
            "SUMMARY:No stamp at all\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

        const QDateTime dt = extractICalTimestamp(ical);
        QVERIFY2(!dt.isValid(),
                 "must return an invalid QDateTime, never fall back to 'now' (N3)");
    }

    void invalidOnEmptyInput()
    {
        QVERIFY(!extractICalTimestamp(QByteArray()).isValid());
    }

    void doesNotMatchUnrelatedPropertyWithSimilarSuffix()
    {
        // Only the exact property name should match — X-LAST-MODIFIED-BY or
        // similar extension properties must not be mistaken for LAST-MODIFIED.
        const QByteArray ical =
            "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
            "BEGIN:VEVENT\r\nUID:e-5\r\n"
            "X-LAST-MODIFIED-BY:20990101T000000Z\r\n"
            "DTSTAMP:20250601T000000Z\r\n"
            "SUMMARY:Test\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

        const QDateTime dt = extractICalTimestamp(ical);
        QVERIFY(dt.isValid());
        QCOMPARE(dt, QDateTime(QDate(2025, 6, 1), QTime(0, 0, 0), QTimeZone::utc()));
    }

    // ---- IP.12 / O90: stripICalPropertyParameter ---------------------------

    void stripsParameterFromMiddleOfLine()
    {
        const QByteArray ical =
            "BEGIN:VEVENT\r\n"
            "ATTENDEE;CN=A;CUTYPE=INDIVIDUAL;X-UID=93826400444256:mailto:a@example.com\r\n"
            "SUMMARY:Test\r\nEND:VEVENT\r\n";
        const QByteArray out =
            stripICalPropertyParameter(ical, QStringLiteral("ATTENDEE"), QStringLiteral("X-UID"));
        QVERIFY2(!out.contains("X-UID"), "X-UID must be gone");
        QVERIFY2(out.contains("ATTENDEE;CN=A;CUTYPE=INDIVIDUAL:mailto:a@example.com"),
                 "the rest of the ATTENDEE line must survive verbatim");
        QVERIFY2(out.contains("SUMMARY:Test"), "unrelated lines must be untouched");
    }

    void stripsParameterAcrossAFoldPoint()
    {
        // Reproduces the real shape KCalendarCore emits: the fold lands right
        // after the ";" that precedes X-UID (RFC 5545 §3.1 folding — CRLF +
        // a single SPACE introduces a continuation).
        const QByteArray ical =
            "BEGIN:VEVENT\r\n"
            "ATTENDEE;CN=A;RSVP=FALSE;PARTSTAT=NEEDS-ACTION;ROLE=REQ-PARTICIPANT;\r\n"
            " CUTYPE=INDIVIDUAL;X-UID=94004632973840:mailto:a@example.com\r\n"
            "SUMMARY:Test\r\nEND:VEVENT\r\n";
        const QByteArray out =
            stripICalPropertyParameter(ical, QStringLiteral("ATTENDEE"), QStringLiteral("X-UID"));
        QVERIFY2(!out.contains("X-UID"), "X-UID must be gone even when folded onto it");
        QVERIFY2(out.contains("CUTYPE=INDIVIDUAL:mailto:a@example.com"),
                 "the rest of the folded ATTENDEE line must survive");
    }

    void leavesUnrelatedPropertyWithSameParameterNameAlone()
    {
        const QByteArray ical =
            "BEGIN:VEVENT\r\n"
            "ORGANIZER;X-UID=123;CN=Org:mailto:org@example.com\r\n"
            "ATTENDEE;CN=A;X-UID=456:mailto:a@example.com\r\n"
            "END:VEVENT\r\n";
        const QByteArray out =
            stripICalPropertyParameter(ical, QStringLiteral("ATTENDEE"), QStringLiteral("X-UID"));
        QVERIFY2(out.contains("ORGANIZER;X-UID=123;CN=Org:mailto:org@example.com"),
                 "ORGANIZER's own X-UID must survive — only ATTENDEE is targeted");
        QVERIFY2(out.contains("ATTENDEE;CN=A:mailto:a@example.com"),
                 "ATTENDEE's X-UID must be gone");
    }

    void noOpWhenParameterAbsent()
    {
        const QByteArray ical =
            "BEGIN:VEVENT\r\nATTENDEE;CN=A:mailto:a@example.com\r\nEND:VEVENT\r\n";
        QCOMPARE(stripICalPropertyParameter(ical, QStringLiteral("ATTENDEE"),
                                             QStringLiteral("X-UID")),
                 ical);
    }

    void noOpOnEmptyInput()
    {
        QVERIFY(stripICalPropertyParameter(QByteArray(), QStringLiteral("ATTENDEE"),
                                            QStringLiteral("X-UID"))
                    .isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestICalTimestamp)
#include "tst_icaltimestamp.moc"
