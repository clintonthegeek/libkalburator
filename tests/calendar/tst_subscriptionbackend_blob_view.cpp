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
#include <KCalendarCore/Todo>
#include <KCalendarCore/Incidence>

#include "subscriptionbackend.h"
#include "iblobbackend.h"
#include "backendrecord.h"
#include "recordidentity.h"
#include "icalcodec.h"

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
// VP.c-step-1b fixtures: a recurring todo series' MASTER and one DETACHED
// EXCEPTION, delivered the way a subscription feed stores them — as SEPARATE
// VTODO blocks in one document that share the RFC 5545 UID. The exception
// block carries the RECURRENCE-ID line.
// ---------------------------------------------------------------------------

static KCalendarCore::Todo::Ptr makeTodoMaster(const QString &uid)
{
    auto t = KCalendarCore::Todo::Ptr::create();
    t->setUid(uid);
    t->setSummary(QStringLiteral("Series master %1").arg(uid));
    t->setDtStart(QDateTime(QDate(2026, 6, 1), QTime(9, 0), QTimeZone::utc()));
    t->setLastModified(QDateTime::currentDateTimeUtc());
    return t;
}

static KCalendarCore::Todo::Ptr makeTodoException(const QString &uid)
{
    auto t = KCalendarCore::Todo::Ptr::create();
    t->setUid(uid);
    t->setSummary(QStringLiteral("Series override %1").arg(uid));
    t->setDtStart(QDateTime(QDate(2026, 6, 2), QTime(9, 0), QTimeZone::utc()));
    t->setRecurrenceId(QDateTime(QDate(2026, 6, 2), QTime(9, 0), QTimeZone::utc()));
    t->setLastModified(QDateTime::currentDateTimeUtc());
    return t;
}

// The UTC recurrence instant the exception fixture expresses.
static QDateTime seriesExceptionRecurrenceId()
{
    return QDateTime(QDate(2026, 6, 2), QTime(9, 0), QTimeZone::utc());
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

    // VP.c-step-1b — detached exceptions as distinct blob records.
    void detachedException_masterAndExceptionAreTwoRecords();
    void detachedException_loadRecord_addressesByIdentity();
    void detachedException_writePath_stillRejectedForCompositeId();
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

// ============================================================================
// VP.c-step-1b — detached exceptions as distinct blob records.
//
// A subscription feed delivers a recurring series' master and its detached
// exceptions as SEPARATE blocks sharing one RFC 5545 UID. The blob pipeline
// keys records by the COMPOSITE identity (uid\x01<UTC-ISO recurrenceId>,
// src/sync/recordidentity.h) so the exception block no longer clobbers the
// master (the old one-record-per-uid behavior).
// ============================================================================

void TestSubscriptionBackendBlobView::detachedException_masterAndExceptionAreTwoRecords()
{
    const QString uid = QStringLiteral("vtodo-series-1");
    StubSubscriptionBackend backend;
    backend.addSource(QStringLiteral("feed"), QStringLiteral("stub"), {});
    backend.addFixtureEvent(makeTodoMaster(uid));
    backend.addFixtureEvent(makeTodoException(uid));

    auto *blob = static_cast<IBlobBackend *>(&backend);
    const QList<BackendRecord> records = blob->loadRecords(QStringLiteral("feed"));

    // One record per block: the master (bare uid) and the exception
    // (composite id) — no last-block-wins collision.
    QCOMPARE(records.size(), 2);

    const BackendRecord *masterRec = nullptr;
    const BackendRecord *excRec = nullptr;
    for (const auto &rec : records) {
        if (rec.id == uid) {
            masterRec = &rec;
        } else if (isExceptionRecordId(rec.id)) {
            excRec = &rec;
        }
    }
    QVERIFY2(masterRec, "the master record must keep the bare-uid record id");
    QVERIFY2(excRec, "the exception must mint a composite record id");
    QCOMPARE(excRec->id, composeRecordIdentity(uid, seriesExceptionRecurrenceId()));
    QVERIFY2(masterRec->id != excRec->id,
             "master and exception must be distinct records");

    // Both blocks' serialised bytes preserved, each from its own incidence.
    QVERIFY2(masterRec->data.contains("SUMMARY:Series master vtodo-series-1"),
             "the master record must carry the master block's bytes");
    QVERIFY2(!masterRec->data.contains("RECURRENCE-ID"),
             "the master record's payload must not carry a RECURRENCE-ID");
    QVERIFY2(excRec->data.contains("RECURRENCE-ID:20260602T090000Z"),
             "the exception record must retain its RECURRENCE-ID line");
    QVERIFY2(excRec->data.contains("SUMMARY:Series override vtodo-series-1"),
             "the exception record must carry the exception block's bytes");
    QVERIFY2(masterRec->contentHash != excRec->contentHash,
             "two distinct blocks must not hash to identical record content");
}

void TestSubscriptionBackendBlobView::detachedException_loadRecord_addressesByIdentity()
{
    const QString uid = QStringLiteral("vtodo-series-2");
    StubSubscriptionBackend backend;
    backend.addSource(QStringLiteral("feed"), QStringLiteral("stub"), {});
    backend.addFixtureEvent(makeTodoMaster(uid));
    backend.addFixtureEvent(makeTodoException(uid));

    auto *blob = static_cast<IBlobBackend *>(&backend);
    QCOMPARE(blob->loadRecords(QStringLiteral("feed")).size(), 2);

    // Bare master uid resolves to the master record only.
    const auto master = blob->loadRecord(uid);
    QVERIFY(master.has_value());
    QCOMPARE(master->id, uid);
    QVERIFY2(master->data.contains("SUMMARY:Series master vtodo-series-2"),
             "loadRecord(bare uid) must serve the master's own bytes");
    QVERIFY2(!master->data.contains("RECURRENCE-ID"),
             "loadRecord(bare uid) must not surface the exception block");

    // Composite exception id resolves to the exception record — never the
    // master sharing the UID.
    const QString excRecordId = composeRecordIdentity(uid, seriesExceptionRecurrenceId());
    const auto exc = blob->loadRecord(excRecordId);
    QVERIFY(exc.has_value());
    QCOMPARE(exc->id, excRecordId);
    QVERIFY2(exc->data.contains("RECURRENCE-ID:20260602T090000Z"),
             "loadRecord(composite id) must serve the exception's own bytes");

    // A composite id whose exception block the feed no longer carries
    // falls back to the bare master (graceful for stale callers).
    StubSubscriptionBackend backendWithoutException;
    backendWithoutException.addSource(QStringLiteral("feed"), QStringLiteral("stub"), {});
    backendWithoutException.addFixtureEvent(makeTodoMaster(uid));
    auto *staleBlob = static_cast<IBlobBackend *>(&backendWithoutException);
    const auto stale = staleBlob->loadRecord(excRecordId);
    QVERIFY(stale.has_value());
    QCOMPARE(stale->id, uid);
}

void TestSubscriptionBackendBlobView::detachedException_writePath_stillRejectedForCompositeId()
{
    const QString uid = QStringLiteral("vtodo-series-3");
    StubSubscriptionBackend backend;
    backend.addSource(QStringLiteral("feed"), QStringLiteral("stub"), {});
    backend.addFixtureEvent(makeTodoException(uid));

    auto *blob = static_cast<IBlobBackend *>(&backend);

    // Read-only backend: writes addressed by the COMPOSITE id are rejected
    // exactly like bare-uid writes — the composite id never becomes a path.
    BackendRecord rec;
    rec.id   = composeRecordIdentity(uid, seriesExceptionRecurrenceId());
    rec.data = icalFromIncidence(makeTodoException(uid));
    QVERIFY(!blob->updateRecord(rec));
    QVERIFY(!blob->deleteRecord(rec.id));
}

// ---------------------------------------------------------------------------

QTEST_GUILESS_MAIN(TestSubscriptionBackendBlobView)
#include "tst_subscriptionbackend_blob_view.moc"
