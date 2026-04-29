// tests/calendar/tst_calendar_baseline_store.cpp
#include <QtTest>
#include <QTemporaryDir>
#include <QDateTime>

#include "calendarbaselinestore.h"

using namespace Kalburator::Sync;

class TestCalendarBaselineStore : public QObject
{
    Q_OBJECT
private slots:
    void roundTrip_singleBaseline();
    void bulkSet_returnsAll();
    void removePerMapping_clearsOnlyThatMapping();
    void propertyBaseline_isolatedPerCalendar();
    void hasBaselines_falseWhenEmpty();
    void persistsAcrossReopen();
    void lastSyncTime_roundTrip();

private:
    QTemporaryDir m_dir;
    QString dbPath() const { return m_dir.filePath(QStringLiteral("test.kalburator-sync.db")); }
};

void TestCalendarBaselineStore::roundTrip_singleBaseline()
{
    CalendarBaselineStore store(dbPath());
    QVERIFY(store.isValid());
    QVERIFY(store.setBaseline(QStringLiteral("m1"), QStringLiteral("uid-1"), QStringLiteral("ICAL-TEXT")));
    QCOMPARE(store.baseline(QStringLiteral("m1"), QStringLiteral("uid-1")), QStringLiteral("ICAL-TEXT"));
    QCOMPARE(store.baseline(QStringLiteral("m1"), QStringLiteral("uid-missing")), QString());
}

void TestCalendarBaselineStore::bulkSet_returnsAll()
{
    CalendarBaselineStore store(dbPath());
    QVERIFY(store.isValid());

    // Use a distinct mappingId to avoid contamination from other test slots
    // that share the same persistent database file (m_dir is class-level).
    const QString mappingId = QStringLiteral("bulk-m1");
    const QHash<QString, QString> batch{
        {QStringLiteral("u1"), QStringLiteral("ICAL-1")},
        {QStringLiteral("u2"), QStringLiteral("ICAL-2")},
        {QStringLiteral("u3"), QStringLiteral("ICAL-3")},
    };
    QVERIFY(store.setBaselines(mappingId, batch));

    const auto result = store.allBaselines(mappingId);
    QCOMPARE(result.size(), 3);
    QCOMPARE(result.value(QStringLiteral("u1")), QStringLiteral("ICAL-1"));
    QCOMPARE(result.value(QStringLiteral("u2")), QStringLiteral("ICAL-2"));
    QCOMPARE(result.value(QStringLiteral("u3")), QStringLiteral("ICAL-3"));
}

void TestCalendarBaselineStore::removePerMapping_clearsOnlyThatMapping()
{
    CalendarBaselineStore store(dbPath());
    QVERIFY(store.isValid());

    QVERIFY(store.setBaseline(QStringLiteral("m1"), QStringLiteral("uid-a"), QStringLiteral("ICAL-A")));
    QVERIFY(store.setBaseline(QStringLiteral("m2"), QStringLiteral("uid-b"), QStringLiteral("ICAL-B")));

    QVERIFY(store.removeBaselines(QStringLiteral("m1")));

    // m1 is gone
    QVERIFY(store.allBaselines(QStringLiteral("m1")).isEmpty());
    QVERIFY(!store.hasBaselines(QStringLiteral("m1")));

    // m2 is intact
    QCOMPARE(store.allBaselines(QStringLiteral("m2")).size(), 1);
    QCOMPARE(store.baseline(QStringLiteral("m2"), QStringLiteral("uid-b")), QStringLiteral("ICAL-B"));
}

void TestCalendarBaselineStore::propertyBaseline_isolatedPerCalendar()
{
    CalendarBaselineStore store(dbPath());
    QVERIFY(store.isValid());

    QVERIFY(store.setPropertyBaseline(QStringLiteral("m1"), QStringLiteral("cal-A"), QStringLiteral("{\"color\":\"red\"}")));
    QVERIFY(store.setPropertyBaseline(QStringLiteral("m1"), QStringLiteral("cal-B"), QStringLiteral("{\"color\":\"blue\"}")));

    QCOMPARE(store.propertyBaseline(QStringLiteral("m1"), QStringLiteral("cal-A")), QStringLiteral("{\"color\":\"red\"}"));
    QCOMPARE(store.propertyBaseline(QStringLiteral("m1"), QStringLiteral("cal-B")), QStringLiteral("{\"color\":\"blue\"}"));

    // Removing cal-A doesn't affect cal-B
    QVERIFY(store.removePropertyBaseline(QStringLiteral("m1"), QStringLiteral("cal-A")));
    QCOMPARE(store.propertyBaseline(QStringLiteral("m1"), QStringLiteral("cal-A")), QString());
    QCOMPARE(store.propertyBaseline(QStringLiteral("m1"), QStringLiteral("cal-B")), QStringLiteral("{\"color\":\"blue\"}"));
}

void TestCalendarBaselineStore::hasBaselines_falseWhenEmpty()
{
    CalendarBaselineStore store(dbPath());
    QVERIFY(store.isValid());

    // Fresh store: no baselines for any mapping
    QVERIFY(!store.hasBaselines(QStringLiteral("m1")));

    // After writing one baseline it becomes true
    QVERIFY(store.setBaseline(QStringLiteral("m1"), QStringLiteral("uid-1"), QStringLiteral("ICAL-1")));
    QVERIFY(store.hasBaselines(QStringLiteral("m1")));
}

void TestCalendarBaselineStore::persistsAcrossReopen()
{
    const QString path = dbPath();

    // Write a baseline and let the store go out of scope (destructor closes the connection)
    {
        CalendarBaselineStore store(path);
        QVERIFY(store.isValid());
        QVERIFY(store.setBaseline(QStringLiteral("m1"), QStringLiteral("uid-persist"), QStringLiteral("ICAL-PERSISTED")));
    }

    // Open a new store on the same path and verify the data survived
    CalendarBaselineStore store2(path);
    QVERIFY(store2.isValid());
    QCOMPARE(store2.baseline(QStringLiteral("m1"), QStringLiteral("uid-persist")), QStringLiteral("ICAL-PERSISTED"));
    QVERIFY(store2.hasBaselines(QStringLiteral("m1")));
}

void TestCalendarBaselineStore::lastSyncTime_roundTrip()
{
    CalendarBaselineStore store(dbPath());
    QVERIFY(store.isValid());

    // No time set yet — should return invalid datetime
    QVERIFY(!store.lastSyncTime(QStringLiteral("m1")).isValid());

    // Store uses Qt::ISODate which has 1-second precision; truncate to seconds
    const QDateTime before = QDateTime::currentDateTimeUtc();
    QVERIFY(store.setLastSyncTime(QStringLiteral("m1"), before));

    const QDateTime retrieved = store.lastSyncTime(QStringLiteral("m1"));
    QVERIFY(retrieved.isValid());
    // The stored value should be within 1 second of the set value
    // (ISO format truncates sub-second precision)
    QVERIFY(qAbs(retrieved.secsTo(before)) <= 1);
}

QTEST_GUILESS_MAIN(TestCalendarBaselineStore)
#include "tst_calendar_baseline_store.moc"
