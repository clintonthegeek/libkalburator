// tests/calendar/tst_subscriptionbackend_blob_view.cpp
// Phase D Task 17 — IBlobBackend smoke test for SubscriptionBackend.
//
// SubscriptionBackend is abstract (fetchEventsForSource is pure virtual),
// so we build a minimal stub that returns a fixed list of events seeded
// at construction time.
//
// Tests verify:
//   1. SubscriptionBackend* casts to IBlobBackend* successfully.
//   2. Identity methods return non-empty values.
//   3. isAvailable() always returns true.
//   4. availableCollections() reflects registered sources.
//   5. loadRecords() returns one BackendRecord per event in the source.
//   6. loadRecord() finds a record by uid.
//   7. createRecord() returns empty string (read-only).
//   8. updateRecord() returns false (read-only).
//   9. deleteRecord() returns false (read-only).

#include <QtTest>
#include <KCalendarCore/Event>
#include <KCalendarCore/Incidence>

#include "subscriptionbackend.h"
#include "iblobbackend.h"
#include "backendrecord.h"

using namespace Kalburator::Sync;

// ---------------------------------------------------------------------------
// Minimal concrete subclass
// ---------------------------------------------------------------------------

class StubSubscriptionBackend : public SubscriptionBackend
{
    Q_OBJECT
public:
    explicit StubSubscriptionBackend(QObject *parent = nullptr)
        : SubscriptionBackend(parent) {}

    /// Seed an event that will be returned for any sourceId.
    void addFixtureEvent(const KCalendarCore::Incidence::Ptr &inc)
    {
        m_fixtures.append(inc);
    }

protected:
    QList<KCalendarCore::Incidence::Ptr> fetchEventsForSource(
        const QString &sourceId,
        const QDate   &startDate,
        const QDate   &endDate) override
    {
        Q_UNUSED(sourceId);
        Q_UNUSED(startDate);
        Q_UNUSED(endDate);
        return m_fixtures;
    }

private:
    QList<KCalendarCore::Incidence::Ptr> m_fixtures;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static KCalendarCore::Event::Ptr makeEvent(const QString &uid,
                                           const QString &summary)
{
    auto ev = KCalendarCore::Event::Ptr::create();
    ev->setUid(uid);
    ev->setSummary(summary);
    ev->setDtStart(QDateTime(QDate(2025, 6, 1), QTime(9, 0), QTimeZone::utc()));
    ev->setDtEnd  (QDateTime(QDate(2025, 6, 1), QTime(10, 0), QTimeZone::utc()));
    ev->setLastModified(QDateTime::currentDateTimeUtc());
    return ev;
}

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class TestSubscriptionBackendBlobView : public QObject
{
    Q_OBJECT

private slots:
    void castSucceeds();
    void identityMethods_returnNonEmpty();
    void isAvailable_alwaysTrue();
    void availableCollections_reflectsSources();
    void loadRecords_returnsRecordsForSource();
    void loadRecord_findsByUid();
    void createRecord_returnsEmptyString();
    void updateRecord_returnsFalse();
    void deleteRecord_returnsFalse();
};

void TestSubscriptionBackendBlobView::castSucceeds()
{
    StubSubscriptionBackend backend;
    auto *blob = static_cast<IBlobBackend *>(&backend);
    QVERIFY(blob != nullptr);
}

void TestSubscriptionBackendBlobView::identityMethods_returnNonEmpty()
{
    StubSubscriptionBackend backend;
    auto *blob = static_cast<IBlobBackend *>(&backend);

    QVERIFY(!blob->backendId().isEmpty());
    QVERIFY(!blob->displayName().isEmpty());
}

void TestSubscriptionBackendBlobView::isAvailable_alwaysTrue()
{
    StubSubscriptionBackend backend;
    QVERIFY(backend.isAvailable());
}

void TestSubscriptionBackendBlobView::availableCollections_reflectsSources()
{
    StubSubscriptionBackend backend;
    backend.addSource(QStringLiteral("src1"), QStringLiteral("stub"), {});
    backend.addSource(QStringLiteral("src2"), QStringLiteral("stub"), {});

    auto *blob = static_cast<IBlobBackend *>(&backend);
    const QList<CollectionInfo> cols = blob->availableCollections();

    QCOMPARE(cols.size(), 2);

    QStringList ids;
    for (const CollectionInfo &c : cols) ids << c.id;
    QVERIFY(ids.contains(QStringLiteral("src1")));
    QVERIFY(ids.contains(QStringLiteral("src2")));
}

void TestSubscriptionBackendBlobView::loadRecords_returnsRecordsForSource()
{
    StubSubscriptionBackend backend;
    backend.addSource(QStringLiteral("holidays"), QStringLiteral("stub"), {});
    backend.addFixtureEvent(makeEvent(QStringLiteral("uid-h1"), QStringLiteral("New Year")));
    backend.addFixtureEvent(makeEvent(QStringLiteral("uid-h2"), QStringLiteral("Labour Day")));

    auto *blob = static_cast<IBlobBackend *>(&backend);
    const QList<BackendRecord> records = blob->loadRecords(QStringLiteral("holidays"));

    QCOMPARE(records.size(), 2);

    // Each record must have a non-empty id, data, and contentHash
    for (const BackendRecord &rec : records) {
        QVERIFY(!rec.id.isEmpty());
        QVERIFY(!rec.data.isEmpty());
        QVERIFY(!rec.contentHash.isEmpty());
        // data must contain the uid
        QVERIFY(QString::fromUtf8(rec.data).contains(rec.id));
    }

    // Verify specific UIDs are present
    QStringList ids;
    for (const BackendRecord &rec : records) ids << rec.id;
    QVERIFY(ids.contains(QStringLiteral("uid-h1")));
    QVERIFY(ids.contains(QStringLiteral("uid-h2")));
}

void TestSubscriptionBackendBlobView::loadRecord_findsByUid()
{
    StubSubscriptionBackend backend;
    backend.addSource(QStringLiteral("src"), QStringLiteral("stub"), {});
    backend.addFixtureEvent(makeEvent(QStringLiteral("find-me"), QStringLiteral("Found Event")));

    auto *blob = static_cast<IBlobBackend *>(&backend);
    const auto result = blob->loadRecord(QStringLiteral("find-me"));

    QVERIFY(result.has_value());
    QCOMPARE(result->id, QStringLiteral("find-me"));
    QVERIFY(!result->contentHash.isEmpty());
}

void TestSubscriptionBackendBlobView::createRecord_returnsEmptyString()
{
    StubSubscriptionBackend backend;
    backend.addSource(QStringLiteral("src"), QStringLiteral("stub"), {});

    auto *blob = static_cast<IBlobBackend *>(&backend);

    BackendRecord rec;
    rec.id   = QStringLiteral("should-not-be-created");
    rec.data = QByteArray("BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n");

    const QString returned = blob->createRecord(QStringLiteral("src"), rec);
    QVERIFY(returned.isEmpty());
}

void TestSubscriptionBackendBlobView::updateRecord_returnsFalse()
{
    StubSubscriptionBackend backend;
    auto *blob = static_cast<IBlobBackend *>(&backend);

    BackendRecord rec;
    rec.id   = QStringLiteral("anything");
    rec.data = QByteArray("BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n");

    QVERIFY(!blob->updateRecord(rec));
}

void TestSubscriptionBackendBlobView::deleteRecord_returnsFalse()
{
    StubSubscriptionBackend backend;
    auto *blob = static_cast<IBlobBackend *>(&backend);

    QVERIFY(!blob->deleteRecord(QStringLiteral("anything")));
}

// ---------------------------------------------------------------------------

QTEST_GUILESS_MAIN(TestSubscriptionBackendBlobView)
#include "tst_subscriptionbackend_blob_view.moc"
