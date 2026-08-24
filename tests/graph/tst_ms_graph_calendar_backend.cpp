// EEE Phase 7.C — MSGraphCalendarBackend tests against the Stage D mock
// Graph server. Pins the v1 contract: records carry raw ms-event wire JSON
// (the engine promotes via the registered edge), the Incidence legacy
// surface converts inside the backend, creates alias requested→stored ids
// (O55 machinery), updates are PATCH-in-place (O61(e): carriers do not
// survive re-creates), deletes expect Graph's 204.

#include <QTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "msgraphcalendarbackend.h"
#include "mockgraphserver.h"

using Kalburator::Graph::MockGraphServer;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::MSGraphCalendarBackend;
using Kalburator::Sync::WriterBatch;
using Kalburator::Sync::WriteOperation;

namespace {

QJsonObject wireEvent(const QString &id, const QString &subject,
                      const QString &startIso = QStringLiteral(
                          "2026-11-26T09:00:00.0000000"))
{
    return QJsonObject{
        { QStringLiteral("id"), id },
        { QStringLiteral("uid"),
          QStringLiteral("040000008200E00074C5B7101A82E008") },
        { QStringLiteral("subject"), subject },
        { QStringLiteral("start"),
          QJsonObject{ { QStringLiteral("dateTime"), startIso },
                       { QStringLiteral("timeZone"), QStringLiteral("UTC") } } },
        { QStringLiteral("end"),
          QJsonObject{ { QStringLiteral("dateTime"),
                         QStringLiteral("2026-11-26T10:00:00.0000000") },
                       { QStringLiteral("timeZone"), QStringLiteral("UTC") } } },
        { QStringLiteral("lastModifiedDateTime"),
          QStringLiteral("2026-08-23T12:00:00.0000000Z") },
    };
}

} // namespace

class TestMsGraphCalendarBackend : public QObject {
    Q_OBJECT

private slots:

    void init()
    {
        m_server = std::make_unique<MockGraphServer>(this);
        QVERIFY(m_server->start());
        m_backend = new MSGraphCalendarBackend(this);
        m_backend->setBaseUrl(m_server->baseUrl());
        m_backend->setAccessToken(QStringLiteral("test-token"));
    }

    void cleanup()
    {
        m_backend = nullptr;   // parented: deleted with this
        m_server.reset();
    }

    // Fetch walks the collection and populates BOTH surfaces: the record
    // memo (raw wire JSON per event) and the Incidence legacy surface.
    void fetchPopulatesRecordsAndIncidences()
    {
        QJsonArray items;
        items.append(wireEvent(QStringLiteral("evt-1"),
                               QStringLiteral("Planning Sync")));
        items.append(wireEvent(QStringLiteral("evt-2"),
                               QStringLiteral("Standup")));
        m_server->addCollection(QStringLiteral("/me/events"), items);

        auto *op = m_backend->fetchItems(QStringLiteral("cal"));
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Succeeded);

        // Legacy surface: two incidences with subjects intact.
        QCOMPARE(op->fetchedItems().size(), 2);
        QVERIFY(op->fetchedItems().at(0)->summary()
                    == QStringLiteral("Planning Sync")
                || op->fetchedItems().at(1)->summary()
                    == QStringLiteral("Planning Sync"));

        // Record memo: raw wire JSON preserved verbatim per event.
        QList<BackendRecord> records;
        QString err;
        QVERIFY(m_backend->recordsFromLastFetch(QStringLiteral("cal"),
                                                records, err));
        QCOMPARE(records.size(), 2);
        bool sawVerbatim = false;
        for (const auto &r : records) {
            const QJsonObject wire =
                QJsonDocument::fromJson(r.data).object();
            QCOMPARE(r.type, QStringLiteral("event"));
            if (wire.value(QStringLiteral("id")).toString()
                    == QStringLiteral("evt-1")) {
                QCOMPARE(wire.value(QStringLiteral("subject")).toString(),
                         QStringLiteral("Planning Sync"));
                sawVerbatim = true;
            }
            QVERIFY(!r.id.isEmpty());
            QVERIFY(!r.contentHash.isEmpty());
        }
        QVERIFY2(sawVerbatim, "wire JSON must round-trip into the memo");

        // Single-shot memo: second call falls through.
        QVERIFY(!m_backend->recordsFromLastFetch(QStringLiteral("cal"),
                                                 records, err));
    }

    void fetchFailureSurfacesOnTheOperation()
    {
        // No collection registered at the backend's path → Graph 404 shape.
        auto *op = m_backend->fetchItems(QStringLiteral("cal"));
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Failed);
        QVERIFY2(op->errorString().contains(QLatin1String("ErrorItemNotFound")),
                 qPrintable(op->errorString()));
    }

    // Creates POST under the collection path and bridge requested→stored ids
    // via WriteOperation::idAliases; updates PATCH in place; deletes expect
    // Graph's 204.
    void applyRecordsDrivesPostPatchDelete()
    {
        // --- server-side expectations ------------------------------------
        const QJsonObject createdWire = wireEvent(
            QStringLiteral("srv-new"), QStringLiteral("Created"));
        m_server->addRoute(QStringLiteral("POST"),
                           QStringLiteral("/me/events"), createdWire, 201);
        const QJsonObject patchedWire = wireEvent(
            QStringLiteral("srv-exist"), QStringLiteral("Patched subject"));
        m_server->addRoute(QStringLiteral("PATCH"),
                           QStringLiteral("/me/events/srv-exist"),
                           patchedWire, 200);
        m_server->addRoute(QStringLiteral("DELETE"),
                           QStringLiteral("/me/events/srv-gone"),
                           QJsonObject{}, 204);

        // --- batch ---------------------------------------------------------
        WriterBatch batch;

        BackendRecord createRec;
        createRec.id = QStringLiteral("local-requested");
        createRec.type = QStringLiteral("event");
        createRec.data = QJsonDocument(wireEvent(
            QString(), QStringLiteral("Created")))
                               .toJson(QJsonDocument::Compact);
        batch.creates.append(createRec);

        BackendRecord updateRec;
        updateRec.id = QStringLiteral("srv-exist");
        updateRec.type = QStringLiteral("event");
        updateRec.data = QJsonDocument(wireEvent(
            QStringLiteral("srv-exist"), QStringLiteral("Patched subject")))
                               .toJson(QJsonDocument::Compact);
        batch.updates.append(updateRec);

        batch.deletes.append(QStringLiteral("srv-gone"));

        auto *op = m_backend->applyRecords(QStringLiteral("cal"), batch);
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QCOMPARE(op->succeededUids().size(), 3);

        // O55 join machinery: requested→stored alias captured for the create.
        QCOMPARE(op->idAliases().value(QStringLiteral("local-requested")),
                 QStringLiteral("srv-new"));

        // Wire truth: exactly one POST, one PATCH, one DELETE, in that
        // order, against the right paths.
        const auto reqs = m_server->requests();
        QCOMPARE(reqs.size(), 3);
        QCOMPARE(reqs.at(0).method, QByteArrayLiteral("POST"));
        QCOMPARE(reqs.at(0).path, QStringLiteral("/me/events"));
        QCOMPARE(reqs.at(1).method, QByteArrayLiteral("PATCH"));
        QCOMPARE(reqs.at(1).path, QStringLiteral("/me/events/srv-exist"));
        QCOMPARE(reqs.at(2).method, QByteArrayLiteral("DELETE"));
        QCOMPARE(reqs.at(2).path, QStringLiteral("/me/events/srv-gone"));

        // The POSTed body is the record's ms-event JSON verbatim.
        const QJsonObject postedBody =
            QJsonDocument::fromJson(reqs.at(0).body).object();
        QCOMPARE(postedBody.value(QStringLiteral("subject")).toString(),
                 QStringLiteral("Created"));
    }

    // Delta integration: the first fetch walks /delta initially; a second
    // fetch presents the stored token, applies only changes, and still
    // reports the FULL merged collection (engine diffs expect whole views).
    void deltaWalksMergeChangesIntoFullViews()
    {
        const QString col = QStringLiteral("/me/events");
        QJsonArray initial;
        initial.append(wireEvent(QStringLiteral("evt-1"),
                                 QStringLiteral("Original subject")));
        m_server->addCollection(col, initial);

        // First fetch: initial delta walk seeds cache + token.
        auto *op1 = m_backend->fetchItems(QStringLiteral("cal"));
        QTRY_VERIFY_WITH_TIMEOUT(op1->isFinished(), 5000);
        QCOMPARE(op1->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QList<BackendRecord> records;
        QString err;
        QVERIFY(m_backend->recordsFromLastFetch(QStringLiteral("cal"),
                                                records, err));
        QCOMPARE(records.size(), 1);
        QCOMPARE(records.first().displayName,
                 QStringLiteral("Original subject"));

        // Server-side change queued under the token the client now holds.
        QJsonArray changes;
        changes.append(wireEvent(QStringLiteral("evt-1"),
                                 QStringLiteral("Updated subject")));
        changes.append(wireEvent(QStringLiteral("evt-2"),
                                 QStringLiteral("Brand new")));
        m_server->queueDeltaChanges(col, QStringLiteral("delta_1"), changes);

        // Second fetch: token replay merges into the cached view.
        auto *op2 = m_backend->fetchItems(QStringLiteral("cal"));
        QTRY_VERIFY_WITH_TIMEOUT(op2->isFinished(), 5000);
        QVERIFY(m_backend->recordsFromLastFetch(QStringLiteral("cal"),
                                                records, err));
        QCOMPARE(records.size(), 2);
        for (const auto &r : records) {
            if (r.id == QStringLiteral("evt-1"))
                QCOMPARE(r.displayName, QStringLiteral("Updated subject"));
            else
                QCOMPARE(r.displayName, QStringLiteral("Brand new"));
        }

        // Wire truth: second fetch hit /delta with $deltatoken, NOT the
        // plain listing.
        bool sawDeltaToken = false;
        for (const auto &req : m_server->requests()) {
            if (req.method == "GET" && req.path.contains(QStringLiteral("$deltatoken")))
                sawDeltaToken = true;
        }
        QVERIFY2(sawDeltaToken, "incremental fetch must present the token");
    }

    // 410 ResyncRequired self-heals to a fresh full listing (O42 pattern).
    void resyncRequiredFallsBackToFullListing()
    {
        const QString col = QStringLiteral("/me/events");
        QJsonArray items;
        items.append(wireEvent(QStringLiteral("evt-1"), QStringLiteral("One")));
        m_server->addCollection(col, items);

        auto *op1 = m_backend->fetchItems(QStringLiteral("cal"));
        QTRY_VERIFY_WITH_TIMEOUT(op1->isFinished(), 5000);

        // Simulate server-side token expiry.
        m_server->invalidateDeltaTokens(col);

        QJsonArray fresh;
        fresh.append(wireEvent(QStringLiteral("evt-1"), QStringLiteral("One v2")));
        fresh.append(wireEvent(QStringLiteral("evt-2"), QStringLiteral("Two")));
        m_server->setCollectionItems(col, fresh);

        auto *op2 = m_backend->fetchItems(QStringLiteral("cal"));
        QTRY_VERIFY_WITH_TIMEOUT(op2->isFinished(), 5000);
        QCOMPARE(op2->state(), Kalburator::Sync::SyncOperation::Succeeded);

        QList<BackendRecord> records;
        QString err;
        QVERIFY(m_backend->recordsFromLastFetch(QStringLiteral("cal"),
                                                records, err));
        QCOMPARE(records.size(), 2);   // full re-listing, not an error
    }

    // Discovery: /me/calendars feeds calendarDiscovered + collections.
    void discoveryListsCalendarsAndCollections()
    {
        QJsonArray cals;
        cals.append(QJsonObject{
            { QStringLiteral("id"), QStringLiteral("calA") },
            { QStringLiteral("name"), QStringLiteral("Calendar A") },
            { QStringLiteral("color"), QStringLiteral("#ff8800") },
            { QStringLiteral("canEdit"), true },
            { QStringLiteral("isDefaultCalendar"), true } });
        cals.append(QJsonObject{
            { QStringLiteral("id"), QStringLiteral("calB") },
            { QStringLiteral("name"), QStringLiteral("Archive") },
            { QStringLiteral("canEdit"), false } });
        m_server->addCollection(QStringLiteral("/me/calendars"), cals);

        bool done = false;
        connect(m_backend,
                &Kalburator::Sync::SyncBackend::loadCalendarsFinished,
                this, [&](const QString &, bool ok) { done = ok; });
        m_backend->loadCalendars(QStringLiteral("coll"));
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);

        const auto cols = m_backend->availableCollections();
        QCOMPARE(cols.size(), 2);
        for (const auto &c : cols) {
            QCOMPARE(c.type, QStringLiteral("calendar"));
            QVERIFY(c.contentTypes.contains(QStringLiteral("VEVENT")));
        }

        const auto dflt = m_backend->discoveredCalendar(QStringLiteral("calA"));
        QCOMPARE(dflt.name, QStringLiteral("Calendar A"));
        QCOMPARE(dflt.backendType, QStringLiteral("msgraph"));
        QVERIFY(dflt.writable);
        QVERIFY(!dflt.supportsVTodo);   // Graph tasks live in /me/todo/lists
        QCOMPARE(dflt.color, QColor(QStringLiteral("#ff8800")));

        const auto ro = m_backend->discoveredCalendar(QStringLiteral("calB"));
        QVERIFY(!ro.writable);
    }

    void failingBatchReportsPerRecordFailures()
    {
        // Unmatched ids get the mock's 404 shape; a lone create against a
        // route-less server therefore fails.
        WriterBatch batch;
        BackendRecord r;
        r.id = QStringLiteral("doomed");
        r.data = QByteArrayLiteral("{}");
        batch.creates.append(r);

        auto *op = m_backend->applyRecords(QStringLiteral("cal"), batch);
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->failedUids(), QStringList{ QStringLiteral("doomed") });
        QVERIFY(op->succeededUids().isEmpty());
    }

private:
    std::unique_ptr<MockGraphServer> m_server;
    MSGraphCalendarBackend *m_backend = nullptr;
};

QTEST_MAIN(TestMsGraphCalendarBackend)
#include "tst_ms_graph_calendar_backend.moc"
