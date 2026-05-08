#include <QtTest>
#include <QTemporaryDir>
#include <KCalendarCore/Event>
#include <KCalendarCore/Todo>
#include <KCalendarCore/ICalFormat>
#include "calendarjournal.h"

namespace Kalburator::Sync {}
using namespace Kalburator::Sync;


class CalendarJournalTest : public QObject {
    Q_OBJECT
private slots:
    void testAppendCreation();
    void testAppendUpdate();
    void testAppendDeletion();
    void testAppendDeletionWithRecurrenceId();
    void testTruncateAfterSync();
    void testReplayRoundTrip();
    void testMultipleCalendars();
    void testCalendarsWithJournals();
};

void CalendarJournalTest::testAppendCreation()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    CalendarJournal journal(tmpDir.path());

    auto event = KCalendarCore::Event::Ptr::create();
    event->setUid(QStringLiteral("evt-001"));
    event->setSummary(QStringLiteral("Test Event"));
    event->setDtStart(QDateTime(QDate(2026, 4, 10), QTime(9, 0), QTimeZone::utc()));

    journal.appendCreation(QStringLiteral("work"), event);

    QVERIFY(journal.hasJournal(QStringLiteral("work")));
}

void CalendarJournalTest::testAppendUpdate()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    CalendarJournal journal(tmpDir.path());

    auto event = KCalendarCore::Event::Ptr::create();
    event->setUid(QStringLiteral("evt-002"));
    event->setSummary(QStringLiteral("Updated Event"));

    journal.appendUpdate(QStringLiteral("work"), event);

    QStringList ops;
    journal.replay(QStringLiteral("work"), [&](const QJsonObject &entry) {
        ops.append(entry[QStringLiteral("op")].toString());
    });
    QCOMPARE(ops, QStringList{QStringLiteral("update")});
}

void CalendarJournalTest::testAppendDeletion()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    CalendarJournal journal(tmpDir.path());

    journal.appendDeletion(QStringLiteral("work"), QStringLiteral("evt-003"));

    QStringList ops;
    QStringList uids;
    journal.replay(QStringLiteral("work"), [&](const QJsonObject &entry) {
        ops.append(entry[QStringLiteral("op")].toString());
        uids.append(entry[QStringLiteral("uid")].toString());
    });
    QCOMPARE(ops, QStringList{QStringLiteral("delete")});
    QCOMPARE(uids, QStringList{QStringLiteral("evt-003")});
}

void CalendarJournalTest::testAppendDeletionWithRecurrenceId()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    CalendarJournal journal(tmpDir.path());

    journal.appendDeletion(QStringLiteral("work"),
                           QStringLiteral("evt-recurring"),
                           QStringLiteral("20260410T090000Z"));

    journal.replay(QStringLiteral("work"), [&](const QJsonObject &entry) {
        QCOMPARE(entry[QStringLiteral("op")].toString(), QStringLiteral("delete"));
        QCOMPARE(entry[QStringLiteral("uid")].toString(), QStringLiteral("evt-recurring"));
        QCOMPARE(entry[QStringLiteral("recurrenceId")].toString(), QStringLiteral("20260410T090000Z"));
    });
}

void CalendarJournalTest::testTruncateAfterSync()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    CalendarJournal journal(tmpDir.path());

    auto event = KCalendarCore::Event::Ptr::create();
    event->setUid(QStringLiteral("evt-trunc"));
    event->setSummary(QStringLiteral("Will be truncated"));

    journal.appendCreation(QStringLiteral("work"), event);
    QVERIFY(journal.hasJournal(QStringLiteral("work")));

    journal.truncate(QStringLiteral("work"));
    QVERIFY(!journal.hasJournal(QStringLiteral("work")));
}

void CalendarJournalTest::testReplayRoundTrip()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    CalendarJournal journal(tmpDir.path());

    // Create an event with real data
    auto event = KCalendarCore::Event::Ptr::create();
    event->setUid(QStringLiteral("roundtrip-001"));
    event->setSummary(QStringLiteral("Roundtrip Test"));
    event->setDescription(QStringLiteral("A longer description with special chars: <>&\""));
    event->setDtStart(QDateTime(QDate(2026, 4, 10), QTime(14, 30), QTimeZone::utc()));
    event->setDtEnd(QDateTime(QDate(2026, 4, 10), QTime(15, 30), QTimeZone::utc()));

    journal.appendCreation(QStringLiteral("personal"), event);

    // Replay and deserialize
    KCalendarCore::ICalFormat format;
    int count = journal.replay(QStringLiteral("personal"), [&](const QJsonObject &entry) {
        QCOMPARE(entry[QStringLiteral("op")].toString(), QStringLiteral("create"));
        QCOMPARE(entry[QStringLiteral("uid")].toString(), QStringLiteral("roundtrip-001"));

        QString icalData = entry[QStringLiteral("ical")].toString();
        QVERIFY(!icalData.isEmpty());

        auto recovered = KCalendarCore::Incidence::Ptr(format.fromString(icalData));
        QVERIFY(recovered);
        QCOMPARE(recovered->uid(), QStringLiteral("roundtrip-001"));
        QCOMPARE(recovered->summary(), QStringLiteral("Roundtrip Test"));
        QCOMPARE(recovered->description(), QStringLiteral("A longer description with special chars: <>&\""));
    });
    QCOMPARE(count, 1);
}

void CalendarJournalTest::testMultipleCalendars()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    CalendarJournal journal(tmpDir.path());

    auto e1 = KCalendarCore::Event::Ptr::create();
    e1->setUid(QStringLiteral("work-evt"));
    auto e2 = KCalendarCore::Event::Ptr::create();
    e2->setUid(QStringLiteral("personal-evt"));

    journal.appendCreation(QStringLiteral("work"), e1);
    journal.appendCreation(QStringLiteral("personal"), e2);

    // Each calendar has its own journal
    QStringList workUids;
    journal.replay(QStringLiteral("work"), [&](const QJsonObject &entry) {
        workUids.append(entry[QStringLiteral("uid")].toString());
    });
    QCOMPARE(workUids, QStringList{QStringLiteral("work-evt")});

    QStringList personalUids;
    journal.replay(QStringLiteral("personal"), [&](const QJsonObject &entry) {
        personalUids.append(entry[QStringLiteral("uid")].toString());
    });
    QCOMPARE(personalUids, QStringList{QStringLiteral("personal-evt")});
}

void CalendarJournalTest::testCalendarsWithJournals()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    CalendarJournal journal(tmpDir.path());

    auto evt = KCalendarCore::Event::Ptr::create();
    evt->setUid(QStringLiteral("any"));

    journal.appendCreation(QStringLiteral("work"), evt);
    journal.appendCreation(QStringLiteral("personal"), evt);
    journal.appendCreation(QStringLiteral("hobbies"), evt);
    journal.truncate(QStringLiteral("personal"));

    QStringList cals = journal.calendarsWithJournals();
    cals.sort();
    QCOMPARE(cals, (QStringList{QStringLiteral("hobbies"), QStringLiteral("work")}));
}

QTEST_MAIN(CalendarJournalTest)
#include "tst_calendarjournal.moc"

