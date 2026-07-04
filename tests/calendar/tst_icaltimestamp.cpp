// tst_icaltimestamp.cpp
//
// N3 fix — direct unit tests for the shared authoritative-timestamp
// extraction helper (Kalburator::Calendar::extractICalTimestamp), used by
// RemoteCalendarBackend::blobRecordFromIcal to stop stamping every remote
// record's lastModified as "now" (which defeated LastWriteWins).

#include <QTest>

#include "icaltimestamp.h"

using Kalburator::Calendar::extractICalTimestamp;

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
};

QTEST_GUILESS_MAIN(TestICalTimestamp)
#include "tst_icaltimestamp.moc"
