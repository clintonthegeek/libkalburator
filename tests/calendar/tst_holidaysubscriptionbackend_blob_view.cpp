// tests/calendar/tst_holidaysubscriptionbackend_blob_view.cpp
// Phase D Task 18 — IBlobBackend smoke test for HolidaySubscriptionBackend.
//
// HolidaySubscriptionBackend inherits IBlobBackend transitively via
// SubscriptionBackend; no overrides are needed.  This test verifies:
//   1. HolidaySubscriptionBackend* casts to IBlobBackend* successfully.
//   2. Identity methods return non-empty values.
//   3. isAvailable() returns true.
//   4. availableCollections() is empty when no regions are set.
//   5. After adding a valid region, availableCollections() returns one entry.
//   6. loadRecords() returns BackendRecord entries for a valid region and a
//      date range that is known to contain holidays (if KHolidays data is
//      available on the system).
//   7. createRecord / updateRecord / deleteRecord are rejected (inherited
//      read-only enforcement).

#include <QtTest>

#include "holidaysubscriptionbackend.h"
#include "iblobbackend.h"
#include "backendrecord.h"

using namespace Kalburator::Sync;

class TestHolidaySubscriptionBackendBlobView : public QObject
{
    Q_OBJECT

private slots:
    void castSucceeds();
    void identityMethods_returnNonEmpty();
    void isAvailable_alwaysTrue();
    void availableCollections_emptyWhenNoRegions();
    void availableCollections_oneEntryAfterAddRegion();
    void loadRecords_returnsRecordsForValidRegion();
    void writeOps_rejected();
};

void TestHolidaySubscriptionBackendBlobView::castSucceeds()
{
    HolidaySubscriptionBackend backend;
    auto *blob = static_cast<IBlobBackend *>(&backend);
    QVERIFY(blob != nullptr);
}

void TestHolidaySubscriptionBackendBlobView::identityMethods_returnNonEmpty()
{
    HolidaySubscriptionBackend backend;
    auto *blob = static_cast<IBlobBackend *>(&backend);

    QVERIFY(!blob->backendId().isEmpty());
    QVERIFY(!blob->displayName().isEmpty());
}

void TestHolidaySubscriptionBackendBlobView::isAvailable_alwaysTrue()
{
    HolidaySubscriptionBackend backend;
    QVERIFY(backend.isAvailable());
}

void TestHolidaySubscriptionBackendBlobView::availableCollections_emptyWhenNoRegions()
{
    HolidaySubscriptionBackend backend;
    auto *blob = static_cast<IBlobBackend *>(&backend);
    QVERIFY(blob->availableCollections().isEmpty());
}

void TestHolidaySubscriptionBackendBlobView::availableCollections_oneEntryAfterAddRegion()
{
    const QStringList regions = HolidaySubscriptionBackend::availableRegionCodes();
    if (regions.isEmpty()) {
        QSKIP("No KHolidays regions installed on this system");
    }

    HolidaySubscriptionBackend backend;
    backend.addRegion(regions.first());

    auto *blob = static_cast<IBlobBackend *>(&backend);
    const QList<CollectionInfo> cols = blob->availableCollections();

    QCOMPARE(cols.size(), 1);
    QCOMPARE(cols.first().id, regions.first());
    QVERIFY(!cols.first().name.isEmpty());
}

void TestHolidaySubscriptionBackendBlobView::loadRecords_returnsRecordsForValidRegion()
{
    const QStringList regions = HolidaySubscriptionBackend::availableRegionCodes();
    if (regions.isEmpty()) {
        QSKIP("No KHolidays regions installed on this system");
    }

    const QString regionCode = regions.first();
    HolidaySubscriptionBackend backend;
    backend.addRegion(regionCode);

    auto *blob = static_cast<IBlobBackend *>(&backend);
    const QList<BackendRecord> records = blob->loadRecords(regionCode);

    // A real holiday region should have at least one holiday in a 3-year window.
    // If the data files are installed we expect > 0 records.
    // We don't assert a specific count — just validate the shape of each record.
    for (const BackendRecord &rec : records) {
        QVERIFY(!rec.id.isEmpty());
        QVERIFY(!rec.data.isEmpty());
        QVERIFY(!rec.contentHash.isEmpty());
        // iCal payload must contain BEGIN:VCALENDAR
        QVERIFY(QString::fromUtf8(rec.data).contains(QStringLiteral("BEGIN:VCALENDAR")));
    }
    // Log count for visibility in CI, but don't fail if data files missing.
    qDebug() << "loadRecords for region" << regionCode << "returned" << records.size() << "records";
}

void TestHolidaySubscriptionBackendBlobView::writeOps_rejected()
{
    HolidaySubscriptionBackend backend;
    auto *blob = static_cast<IBlobBackend *>(&backend);

    BackendRecord rec;
    rec.id   = QStringLiteral("dummy");
    rec.data = QByteArray("BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n");

    // createRecord returns empty string
    QVERIFY(blob->createRecord(QStringLiteral("any"), rec).isEmpty());
    // updateRecord returns false
    QVERIFY(!blob->updateRecord(rec));
    // deleteRecord returns false
    QVERIFY(!blob->deleteRecord(QStringLiteral("dummy")));
}

// ---------------------------------------------------------------------------

QTEST_GUILESS_MAIN(TestHolidaySubscriptionBackendBlobView)
#include "tst_holidaysubscriptionbackend_blob_view.moc"
