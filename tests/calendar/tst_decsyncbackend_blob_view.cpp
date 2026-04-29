// tests/calendar/tst_decsyncbackend_blob_view.cpp
// Phase D Task 16 — IBlobBackend smoke test for DecSyncBackend.
//
// Uses QTemporaryDir for an isolated DecSync directory so no external
// DecSync installation is required.  Tests verify:
//   1. DecSyncBackend* casts to IBlobBackend* successfully.
//   2. Identity methods return non-empty values.
//   3. isAvailable() returns true for an existing directory.
//   4. availableCollections() returns empty for an empty DecSync dir.
//   5. createRecord() and loadRecord() roundtrip correctly.

#include <QtTest>
#include <QTemporaryDir>

#include "decsyncbackend.h"
#include "iblobbackend.h"
#include "backendrecord.h"
#include "calendartype.h"

using namespace Kalburator::Sync;

namespace {

/// Minimal VCALENDAR iCal byte string for a given UID.
static QByteArray makeIcal(const QString &uid)
{
    return QStringLiteral(
        "BEGIN:VCALENDAR\r\n"
        "VERSION:2.0\r\n"
        "PRODID:-//test//test//EN\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:%1\r\n"
        "SUMMARY:DecSync test event %1\r\n"
        "DTSTART:20250101T120000Z\r\n"
        "DTEND:20250101T130000Z\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n").arg(uid).toUtf8();
}

} // anonymous namespace

class TestDecSyncBackendBlobView : public QObject
{
    Q_OBJECT

private slots:
    void castSucceeds();
    void identityMethods_returnNonEmpty();
    void isAvailable_trueForExistingDir();
    void availableCollections_emptyForNewDir();
    void createAndLoadRecord_roundTrip();
};

void TestDecSyncBackendBlobView::castSucceeds()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DecSyncBackend backend(dir.path(), QStringLiteral("test-app"));
    auto *blob = static_cast<IBlobBackend *>(&backend);
    QVERIFY(blob != nullptr);
}

void TestDecSyncBackendBlobView::identityMethods_returnNonEmpty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DecSyncBackend backend(dir.path(), QStringLiteral("test-app"));
    auto *blob = static_cast<IBlobBackend *>(&backend);

    QVERIFY(!blob->backendId().isEmpty());
    QVERIFY(!blob->displayName().isEmpty());
}

void TestDecSyncBackendBlobView::isAvailable_trueForExistingDir()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DecSyncBackend backend(dir.path(), QStringLiteral("test-app"));
    auto *blob = static_cast<IBlobBackend *>(&backend);

    QVERIFY(blob->isAvailable());
}

void TestDecSyncBackendBlobView::availableCollections_emptyForNewDir()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DecSyncBackend backend(dir.path(), QStringLiteral("test-app"));
    auto *blob = static_cast<IBlobBackend *>(&backend);

    // No collections have been created yet.
    QVERIFY(blob->availableCollections().isEmpty());
}

void TestDecSyncBackendBlobView::createAndLoadRecord_roundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DecSyncBackend backend(dir.path(), QStringLiteral("test-app"));
    auto *blob = static_cast<IBlobBackend *>(&backend);

    // First create the collection so the backend has a collection to write to.
    // Use CalendarType::Event to create the calendars/ side immediately (avoiding
    // the hybrid-deferred pattern which only creates the tasks/ side eagerly).
    backend.createCalendar(QString(), QStringLiteral("test-cal"), QStringLiteral("Test Calendar"),
                           CalendarType::Event);

    BackendRecord rec;
    rec.id   = QStringLiteral("uid-decsync-1");
    rec.data = makeIcal(QStringLiteral("uid-decsync-1"));

    const QString returned = blob->createRecord(QStringLiteral("test-cal"), rec);
    QCOMPARE(returned, QStringLiteral("uid-decsync-1"));

    // loadRecord should find it.
    const auto loaded = blob->loadRecord(QStringLiteral("uid-decsync-1"));
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->id, QStringLiteral("uid-decsync-1"));
    QVERIFY(!loaded->contentHash.isEmpty());
}

QTEST_GUILESS_MAIN(TestDecSyncBackendBlobView)
#include "tst_decsyncbackend_blob_view.moc"
