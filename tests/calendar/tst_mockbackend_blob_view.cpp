// tests/calendar/tst_mockbackend_blob_view.cpp
// Phase D Task 11 — verify MockBackend's IBlobBackend implementation.

#include <QtTest>

#include "mockbackend.h"
#include "iblobbackend.h"

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>

using namespace Kalburator::Sync;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Build a minimal VCALENDAR/VEVENT iCal string carrying the given UID and
/// summary, with an explicit LAST-MODIFIED timestamp so modifiedSince tests
/// can control the modification time.
static BackendRecord makeRecord(const QString &uid,
                                const QString &summary,
                                const QDateTime &lastModified = QDateTime())
{
    auto event = KCalendarCore::Event::Ptr::create();
    event->setUid(uid);
    event->setSummary(summary);
    QDateTime lm = lastModified.isValid() ? lastModified
                                          : QDateTime::currentDateTimeUtc();
    event->setLastModified(lm);

    KCalendarCore::ICalFormat format;
    const QString ical = format.toICalString(event);

    BackendRecord r;
    r.id   = uid;
    r.data = ical.toUtf8();
    // contentHash and other fields left empty — createRecord derives them from
    // the parsed incidence.
    return r;
}

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class TestMockBackendBlobView : public QObject
{
    Q_OBJECT
private slots:
    void roundTrip_createLoadDelete();
    void modifiedSince_filtersByTimestamp();
    void loadRecord_missingId_returnsNullopt();
};

// ---------------------------------------------------------------------------
// Slot: roundTrip_createLoadDelete
// ---------------------------------------------------------------------------

void TestMockBackendBlobView::roundTrip_createLoadDelete()
{
    MockBackend mock(QStringLiteral("test-backend"));
    auto *blob = static_cast<IBlobBackend *>(&mock);

    QVERIFY(blob);

    // --- create ---
    BackendRecord rec = makeRecord(QStringLiteral("uid-1"),
                                   QStringLiteral("foo"));
    const QString returned = blob->createRecord(QStringLiteral("cal-1"), rec);
    QCOMPARE(returned, QStringLiteral("uid-1"));

    // --- load ---
    auto loaded = blob->loadRecord(QStringLiteral("uid-1"));
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->id, QStringLiteral("uid-1"));

    // data should round-trip (decoded back to valid iCal containing our UID)
    const QString ical = QString::fromUtf8(loaded->data);
    QVERIFY(ical.contains(QStringLiteral("uid-1")));
    QVERIFY(ical.contains(QStringLiteral("foo")));

    // contentHash must be non-empty
    QVERIFY(!loaded->contentHash.isEmpty());

    // --- delete ---
    bool deleted = blob->deleteRecord(QStringLiteral("uid-1"));
    QVERIFY(deleted);

    // --- should be gone now ---
    auto afterDelete = blob->loadRecord(QStringLiteral("uid-1"));
    QVERIFY(!afterDelete.has_value());
}

// ---------------------------------------------------------------------------
// Slot: modifiedSince_filtersByTimestamp
// ---------------------------------------------------------------------------

void TestMockBackendBlobView::modifiedSince_filtersByTimestamp()
{
    MockBackend mock(QStringLiteral("test-backend"));
    auto *blob = static_cast<IBlobBackend *>(&mock);

    const QDateTime T = QDateTime(QDate(2025, 1, 15), QTime(12, 0, 0), QTimeZone::utc());
    const QDateTime before = T.addSecs(-3600);   // 1 hour before T
    const QDateTime after  = T.addSecs( 3600);   // 1 hour after  T

    // Seed two records: one modified before T, one after
    BackendRecord old = makeRecord(QStringLiteral("uid-old"),
                                   QStringLiteral("old event"),
                                   before);
    BackendRecord recent = makeRecord(QStringLiteral("uid-recent"),
                                      QStringLiteral("recent event"),
                                      after);

    blob->createRecord(QStringLiteral("cal-1"), old);
    blob->createRecord(QStringLiteral("cal-1"), recent);

    // Query for records modified after T
    QList<BackendRecord> results = blob->modifiedSince(QStringLiteral("cal-1"), T);

    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().id, QStringLiteral("uid-recent"));
}

// ---------------------------------------------------------------------------
// Slot: loadRecord_missingId_returnsNullopt
// ---------------------------------------------------------------------------

void TestMockBackendBlobView::loadRecord_missingId_returnsNullopt()
{
    MockBackend mock(QStringLiteral("test-backend"));
    auto *blob = static_cast<IBlobBackend *>(&mock);

    auto result = blob->loadRecord(QStringLiteral("nope"));
    QVERIFY(!result.has_value());
}

// ---------------------------------------------------------------------------

QTEST_GUILESS_MAIN(TestMockBackendBlobView)
#include "tst_mockbackend_blob_view.moc"
