// B2C P3.b — MockGoogleTasksServer transport tests. Pins: lists endpoint,
// items[]/nextPageToken pagination honoring maxResults, default-omits-
// completed/deleted vs showCompleted/showHidden flags, O68-family create
// rejections (id/created/updated ⇒ 400 reason "invalid"), create minting a
// deterministic id + storing, PATCH merge / 404, delete 204/404, Bearer
// Authorization header recording.

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QUrl>

#include "mockgoogletasksserver.h"

using Kalburator::Tasks::httpRequest;
using Kalburator::Tasks::MockGoogleTasksServer;

namespace {

QJsonObject makeTask(int i, const QString &status = QStringLiteral("needsAction"),
                     bool deleted = false)
{
    QJsonObject task{
        { QStringLiteral("id"), QStringLiteral("seed-%1").arg(i) },
        { QStringLiteral("title"), QStringLiteral("Task %1").arg(i) },
        { QStringLiteral("status"), status }
    };
    if (deleted)
        task.insert(QStringLiteral("deleted"), true);
    return task;
}

} // namespace

class TestMockGoogleTasksServer : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void taskListsServedAtUsersAtMeListsEndpoint()
    {
        MockGoogleTasksServer server;
        QVERIFY(server.start());
        server.setTaskLists({ QJsonObject{
            { QStringLiteral("id"), QStringLiteral("list-1") },
            { QStringLiteral("title"), QStringLiteral("Kalburator") } } });

        const auto resp = httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1/users/@me/lists")),
            "GET");
        QVERIFY(resp.ok());
        const QJsonObject page = QJsonDocument::fromJson(resp.body).object();
        const QJsonArray items =
            page.value(QStringLiteral("items")).toArray();
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.at(0).toObject().value(QStringLiteral("id")).toString(),
                 QStringLiteral("list-1"));
        QVERIFY(page.value(QStringLiteral("nextPageToken")).isUndefined());
    }

    void taskCollectionPaginatesWithMaxResults()
    {
        MockGoogleTasksServer server;
        QVERIFY(server.start());

        QJsonArray tasks;
        for (int i = 0; i < 25; ++i)
            tasks.append(makeTask(i));
        server.setTasks(QStringLiteral("list-1"), tasks);

        auto resp = httpRequest(
            QUrl(server.baseUrl()
                 + QStringLiteral("/v1/lists/list-1/tasks?maxResults=10")),
            "GET");
        QVERIFY(resp.ok());
        QJsonObject page = QJsonDocument::fromJson(resp.body).object();
        QCOMPARE(page.value(QStringLiteral("items")).toArray().size(), 10);
        QString next =
            page.value(QStringLiteral("nextPageToken")).toString();
        QVERIFY(!next.isEmpty());
        QCOMPARE(next, QStringLiteral("10"));

        resp = httpRequest(QUrl(server.baseUrl() + QStringLiteral(
                                    "/v1/lists/list-1/tasks?maxResults=10")
                                + QStringLiteral("&pageToken=") + next),
                           "GET");
        QVERIFY(resp.ok());
        page = QJsonDocument::fromJson(resp.body).object();
        QCOMPARE(page.value(QStringLiteral("items")).toArray().size(), 10);
        next = page.value(QStringLiteral("nextPageToken")).toString();

        resp = httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1/lists/list-1/tasks")
                 + QStringLiteral("?pageToken=") + next),
            "GET");
        QVERIFY(resp.ok());
        page = QJsonDocument::fromJson(resp.body).object();
        QCOMPARE(page.value(QStringLiteral("items")).toArray().size(), 5);
        QVERIFY(page.value(QStringLiteral("nextPageToken")).isUndefined());

        // Last page's tail rows are the highest-seeded ids.
        QCOMPARE(page.value(QStringLiteral("items")).toArray().at(0)
                     .toObject()
                     .value(QStringLiteral("id")).toString(),
                 QStringLiteral("seed-20"));
    }

    void defaultOmitsCompletedAndDeletedFlagsIncludeThem()
    {
        MockGoogleTasksServer server;
        QVERIFY(server.start());
        server.setTasks(QStringLiteral("list-1"),
                        { makeTask(0),
                          makeTask(1, QStringLiteral("completed")),
                          makeTask(2, QStringLiteral("needsAction"), true) });

        // Default: completed AND deleted omitted.
        auto resp = httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1/lists/list-1/tasks")),
            "GET");
        QVERIFY(resp.ok());
        QJsonArray items = QJsonDocument::fromJson(resp.body).object()
                               .value(QStringLiteral("items")).toArray();
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.at(0).toObject().value(QStringLiteral("id")).toString(),
                 QStringLiteral("seed-0"));

        // showCompleted=true reveals the completed row only.
        resp = httpRequest(
            QUrl(server.baseUrl()
                 + QStringLiteral("/v1/lists/list-1/tasks?showCompleted=true")),
            "GET");
        QVERIFY(resp.ok());
        items = QJsonDocument::fromJson(resp.body).object()
                    .value(QStringLiteral("items")).toArray();
        QCOMPARE(items.size(), 2);
        QCOMPARE(items.at(1).toObject().value(QStringLiteral("id")).toString(),
                 QStringLiteral("seed-1"));

        // showHidden=true reveals the deleted row only.
        resp = httpRequest(
            QUrl(server.baseUrl()
                 + QStringLiteral("/v1/lists/list-1/tasks?showHidden=true")),
            "GET");
        QVERIFY(resp.ok());
        items = QJsonDocument::fromJson(resp.body).object()
                    .value(QStringLiteral("items")).toArray();
        QCOMPARE(items.size(), 2);
        QCOMPARE(items.at(1).toObject().value(QStringLiteral("id")).toString(),
                 QStringLiteral("seed-2"));

        // Both flags reveal everything.
        resp = httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1/lists/list-1/tasks")
                 + QStringLiteral("?showCompleted=true&showHidden=true")),
            "GET");
        QVERIFY(resp.ok());
        items = QJsonDocument::fromJson(resp.body).object()
                    .value(QStringLiteral("items")).toArray();
        QCOMPARE(items.size(), 3);
    }

    void createRejectsClientSuppliedIdCreatedUpdatedAs400Invalid()
    {
        MockGoogleTasksServer server;
        QVERIFY(server.start());
        server.setTasks(QStringLiteral("list-1"), {});

        for (const char *key : { "id", "created", "updated" }) {
            QJsonObject body{ { QStringLiteral("title"),
                                QStringLiteral("Rejected") } };
            body.insert(QLatin1String(key), QStringLiteral("x"));
            const auto resp = httpRequest(
                QUrl(server.baseUrl()
                     + QStringLiteral("/v1/lists/list-1/tasks")),
                "POST", {},
                QJsonDocument(body).toJson(QJsonDocument::Compact));
            QCOMPARE(resp.status, 400);
            const QJsonObject error =
                QJsonDocument::fromJson(resp.body).object()
                    .value(QStringLiteral("error")).toObject();
            QCOMPARE(error.value(QStringLiteral("code")).toInt(), 400);
            const QJsonArray errors =
                error.value(QStringLiteral("errors")).toArray();
            QCOMPARE(errors.size(), 1);
            QCOMPARE(errors.at(0).toObject()
                         .value(QStringLiteral("reason")).toString(),
                     QStringLiteral("invalid"));
        }

        // A clean body is accepted.
        const auto resp = httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1/lists/list-1/tasks")),
            "POST", {},
            QJsonDocument(QJsonObject{
                { QStringLiteral("title"), QStringLiteral("Clean") } })
                .toJson(QJsonDocument::Compact));
        QVERIFY(resp.ok());
    }

    void createMintsTransportIdStoresAndEchoes()
    {
        MockGoogleTasksServer server;
        QVERIFY(server.start());
        server.setTasks(QStringLiteral("list-1"), {});

        const auto resp = httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1/lists/list-1/tasks")),
            "POST", {},
            QJsonDocument(QJsonObject{
                { QStringLiteral("title"), QStringLiteral("New Task") },
                { QStringLiteral("notes"), QStringLiteral("body") } })
                .toJson(QJsonDocument::Compact));
        QVERIFY(resp.ok());
        const QJsonObject echoed =
            QJsonDocument::fromJson(resp.body).object();
        const QString minted =
            echoed.value(QStringLiteral("id")).toString();
        QVERIFY(minted.startsWith(QLatin1String("mocktask")));
        QCOMPARE(echoed.value(QStringLiteral("title")).toString(),
                 QStringLiteral("New Task"));
        QCOMPARE(echoed.value(QStringLiteral("status")).toString(),
                 QStringLiteral("needsAction"));
        QVERIFY(!echoed.contains(QStringLiteral("created")));
        QVERIFY(!echoed.contains(QStringLiteral("updated")));

        // Stored: default listing (no flags) shows it.
        auto list = httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1/lists/list-1/tasks")),
            "GET");
        QVERIFY(list.ok());
        const QJsonArray items =
            QJsonDocument::fromJson(list.body).object()
                .value(QStringLiteral("items")).toArray();
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.at(0).toObject().value(QStringLiteral("id")).toString(),
                 minted);

        // Deterministic ids advance monotonically.
        const auto second = httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1/lists/list-1/tasks")),
            "POST", {},
            QJsonDocument(QJsonObject{
                { QStringLiteral("title"), QStringLiteral("Two") } })
                .toJson(QJsonDocument::Compact));
        QVERIFY(second.ok());
        const QString minted2 = QJsonDocument::fromJson(second.body).object()
                                    .value(QStringLiteral("id")).toString();
        QVERIFY(minted2 != minted);
    }

    void patchMergesInPlaceAndMissIs404()
    {
        MockGoogleTasksServer server;
        QVERIFY(server.start());
        server.setTasks(QStringLiteral("list-1"), { makeTask(0) });

        auto resp = httpRequest(
            QUrl(server.baseUrl()
                 + QStringLiteral("/v1/lists/list-1/tasks/seed-0")),
            "PATCH", {},
            QJsonDocument(QJsonObject{
                { QStringLiteral("title"), QStringLiteral("Renamed") },
                { QStringLiteral("status"), QStringLiteral("completed") } })
                .toJson(QJsonDocument::Compact));
        QVERIFY(resp.ok());
        const QJsonObject patched =
            QJsonDocument::fromJson(resp.body).object();
        QCOMPARE(patched.value(QStringLiteral("title")).toString(),
                 QStringLiteral("Renamed"));
        QCOMPARE(patched.value(QStringLiteral("status")).toString(),
                 QStringLiteral("completed"));

        // Merge persisted — and now hidden behind the default filter.
        resp = httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1/lists/list-1/tasks")),
            "GET");
        QVERIFY(resp.ok());
        QCOMPARE(QJsonDocument::fromJson(resp.body).object()
                     .value(QStringLiteral("items")).toArray().size(),
                 0);
        resp = httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1/lists/list-1/tasks")
                 + QStringLiteral("?showCompleted=true")),
            "GET");
        QVERIFY(resp.ok());
        const QJsonArray items =
            QJsonDocument::fromJson(resp.body).object()
                .value(QStringLiteral("items")).toArray();
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.at(0).toObject().value(QStringLiteral("title"))
                     .toString(),
                 QStringLiteral("Renamed"));

        resp = httpRequest(
            QUrl(server.baseUrl()
                 + QStringLiteral("/v1/lists/list-1/tasks/nope")),
            "PATCH", {},
            QJsonDocument(QJsonObject{
                { QStringLiteral("title"), QStringLiteral("x") } })
                .toJson(QJsonDocument::Compact));
        QCOMPARE(resp.status, 404);
        QCOMPARE(QJsonDocument::fromJson(resp.body).object()
                     .value(QStringLiteral("error")).toObject()
                     .value(QStringLiteral("code")).toInt(),
                 404);
    }

    void deleteRemovesWith204AndMissIs404()
    {
        MockGoogleTasksServer server;
        QVERIFY(server.start());
        server.setTasks(QStringLiteral("list-1"),
                        { makeTask(0), makeTask(1) });

        auto resp = httpRequest(
            QUrl(server.baseUrl()
                 + QStringLiteral("/v1/lists/list-1/tasks/seed-0")),
            "DELETE");
        QCOMPARE(resp.status, 204);

        // Re-list confirmation drill: exactly one survivor.
        resp = httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1/lists/list-1/tasks")),
            "GET");
        QVERIFY(resp.ok());
        const QJsonArray items =
            QJsonDocument::fromJson(resp.body).object()
                .value(QStringLiteral("items")).toArray();
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.at(0).toObject().value(QStringLiteral("id")).toString(),
                 QStringLiteral("seed-1"));

        resp = httpRequest(
            QUrl(server.baseUrl()
                 + QStringLiteral("/v1/lists/list-1/tasks/seed-0")),
            "DELETE");
        QCOMPARE(resp.status, 404);
        const QJsonObject error =
            QJsonDocument::fromJson(resp.body).object()
                .value(QStringLiteral("error")).toObject();
        QCOMPARE(error.value(QStringLiteral("code")).toInt(), 404);
    }

    void bearerAuthorizationHeaderRecorded()
    {
        MockGoogleTasksServer server;
        QVERIFY(server.start());
        server.setTaskLists({});

        httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1/users/@me/lists")),
            "GET",
            { { QByteArrayLiteral("Authorization"),
                QByteArrayLiteral("Bearer test-token") } });
        httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1/lists/l/tasks")),
            "GET",
            { { QByteArrayLiteral("Authorization"),
                QByteArrayLiteral("Bearer test-token") } },
            {});
        httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1/lists/l/tasks")),
            "POST",
            { { QByteArrayLiteral("Authorization"),
                QByteArrayLiteral("Bearer test-token") } },
            QJsonDocument(QJsonObject{
                { QStringLiteral("title"), QStringLiteral("t") } })
                .toJson(QJsonDocument::Compact));

        const auto reqs = server.requests();
        QCOMPARE(reqs.size(), 3);
        for (const auto &r : reqs) {
            QCOMPARE(r.authorizationHeader, QByteArray("Bearer test-token"));
        }
        QCOMPARE(reqs.at(0).method, QByteArray("GET"));
        QCOMPARE(reqs.at(2).method, QByteArray("POST"));
    }
};

QTEST_MAIN(TestMockGoogleTasksServer)
#include "tst_mockgoogle_tasks_server.moc"
