// B2C P3.b — MockGraphTodoServer transport tests. Pins: todo-lists endpoint,
// $top/$skip pagination with absolute nextLink, expand-on/off carrier
// visibility (incl. %27 variant), inline-create wire-lie (extensions echoed
// but NOT stored), nav-POST carrier UPSERT semantics, PATCH-with-extensions
// ⇒ 500, wrong-prefix expand filter ⇒ 500, create minting an '='-terminated
// id, delete 204/ErrorItemNotFound, Bearer Authorization header recording.

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QUrl>
#include <QUrlQuery>

#include "mockgraphtodoserver.h"

using Kalburator::Todo::httpRequest;
using Kalburator::Todo::MockGraphTodoServer;

namespace {

const QString kCanonExtensionId = QStringLiteral(
    "microsoft.graph.openTypeExtension.kalburator.canon");

const QString kListTasksPath =
    QStringLiteral("/v1.0/me/todo/lists/list-1/tasks");

QJsonObject makeTask(int i, bool withCarrier = false)
{
    QJsonObject task{
        { QStringLiteral("id"), QStringLiteral("tid-%1").arg(i) },
        { QStringLiteral("title"), QStringLiteral("Task %1").arg(i) },
        { QStringLiteral("status"),
          i == 0 ? QStringLiteral("notStarted")
                 : QStringLiteral("inProgress") }
    };
    if (withCarrier) {
        task.insert(QStringLiteral("extensions"),
                    QJsonArray{ QJsonObject{
                        { QStringLiteral("id"), kCanonExtensionId },
                        { QStringLiteral("extensionName"),
                          QStringLiteral("kalburator.canon") },
                        { QStringLiteral("canon"),
                          QStringLiteral("{\"v\":%1}").arg(i) } } });
    }
    return task;
}

QUrl expandUrl(const QString &base, const QString &path, bool preEncoded)
{
    const QString filter =
        QStringLiteral("extensions($filter=Id eq '%1')").arg(kCanonExtensionId);
    if (preEncoded)
        return QUrl(base + path
                    + QStringLiteral(
                        "?%24expand=extensions(%24filter=Id%20eq%20%27"
                        "microsoft.graph.openTypeExtension."
                        "kalburator.canon%27)"));
    QUrl url(base + path);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("$expand"), filter);
    url.setQuery(query);
    return url;
}

} // namespace

class TestMockGraphTodoServer : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void todoListsServedAtEndpoint()
    {
        MockGraphTodoServer server;
        QVERIFY(server.start());
        server.setTodoLists({ QJsonObject{
            { QStringLiteral("id"), QStringLiteral("list-1") },
            { QStringLiteral("displayName"), QStringLiteral("Kalburator") } } });

        const auto resp = httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1.0/me/todo/lists")),
            "GET");
        QVERIFY(resp.ok());
        const QJsonArray value = QJsonDocument::fromJson(resp.body).object()
                                     .value(QStringLiteral("value")).toArray();
        QCOMPARE(value.size(), 1);
        QCOMPARE(value.at(0).toObject().value(QStringLiteral("id")).toString(),
                 QStringLiteral("list-1"));
    }

    void collectionPaginatesWithTopSkipNextLink()
    {
        MockGraphTodoServer server;
        QVERIFY(server.start());

        QJsonArray tasks;
        for (int i = 0; i < 25; ++i)
            tasks.append(makeTask(i));
        server.addCollection(kListTasksPath, tasks);

        auto resp = httpRequest(
            QUrl(server.baseUrl() + kListTasksPath + QStringLiteral("?$top=10")),
            "GET");
        QVERIFY(resp.ok());
        QJsonObject page = QJsonDocument::fromJson(resp.body).object();
        QCOMPARE(page.value(QStringLiteral("value")).toArray().size(), 10);
        const QString next1 =
            page.value(QStringLiteral("@odata.nextLink")).toString();
        QVERIFY(!next1.isEmpty());
        QVERIFY2(next1.startsWith(server.baseUrl()),
                 "nextLink must be absolute (Graph wire truth)");

        resp = httpRequest(QUrl(next1), "GET");
        QVERIFY(resp.ok());
        page = QJsonDocument::fromJson(resp.body).object();
        QCOMPARE(page.value(QStringLiteral("value")).toArray().size(), 10);
        const QString next2 =
            page.value(QStringLiteral("@odata.nextLink")).toString();

        resp = httpRequest(QUrl(next2), "GET");
        QVERIFY(resp.ok());
        page = QJsonDocument::fromJson(resp.body).object();
        QCOMPARE(page.value(QStringLiteral("value")).toArray().size(), 5);
        QVERIFY(page.value(QStringLiteral("@odata.nextLink")).isUndefined());

        // $skip alone offsets into the collection.
        resp = httpRequest(
            QUrl(server.baseUrl() + kListTasksPath + QStringLiteral("?$skip=23")),
            "GET");
        QVERIFY(resp.ok());
        page = QJsonDocument::fromJson(resp.body).object();
        const QJsonArray tail =
            page.value(QStringLiteral("value")).toArray();
        QCOMPARE(tail.size(), 2);
        QCOMPARE(tail.at(0).toObject().value(QStringLiteral("id")).toString(),
                 QStringLiteral("tid-23"));
    }

    void expandTogglesCarrierVisibility()
    {
        MockGraphTodoServer server;
        QVERIFY(server.start());
        server.addCollection(kListTasksPath,
                             { makeTask(0, true), makeTask(1, false) });

        // Expand ON (QUrl-encoded variant): matching rows visible; the
        // carrier-less task has no extensions key.
        auto resp = httpRequest(expandUrl(server.baseUrl(), kListTasksPath,
                                          false),
                                "GET");
        QVERIFY(resp.ok());
        QJsonArray value = QJsonDocument::fromJson(resp.body).object()
                               .value(QStringLiteral("value")).toArray();
        QCOMPARE(value.size(), 2);
        QVERIFY(value.at(0).toObject().contains(QStringLiteral("extensions")));
        QCOMPARE(value.at(0).toObject()
                     .value(QStringLiteral("extensions")).toArray().at(0)
                     .toObject()
                     .value(QStringLiteral("id")).toString(),
                 kCanonExtensionId);
        QVERIFY(!value.at(1).toObject().contains(QStringLiteral("extensions")));

        // %27-pre-encoded variant: same result.
        resp = httpRequest(expandUrl(server.baseUrl(), kListTasksPath, true),
                           "GET");
        QVERIFY(resp.ok());
        value = QJsonDocument::fromJson(resp.body).object()
                    .value(QStringLiteral("value")).toArray();
        QVERIFY(value.at(0).toObject().contains(QStringLiteral("extensions")));

        // Expand OFF: extensions stripped even from carrier-bearing records.
        resp = httpRequest(QUrl(server.baseUrl() + kListTasksPath), "GET");
        QVERIFY(resp.ok());
        value = QJsonDocument::fromJson(resp.body).object()
                    .value(QStringLiteral("value")).toArray();
        QVERIFY(!value.at(0).toObject().contains(QStringLiteral("extensions")));
        QVERIFY(!value.at(1).toObject().contains(QStringLiteral("extensions")));
    }

    void inlineCreateEchoesExtensionsButDoesNotStoreThem()
    {
        MockGraphTodoServer server;
        QVERIFY(server.start());
        server.addCollection(kListTasksPath, {});

        QJsonObject body{
            { QStringLiteral("title"), QStringLiteral("With Carrier") } };
        body.insert(QStringLiteral("extensions"),
                    QJsonArray{ QJsonObject{
                        { QStringLiteral("id"), kCanonExtensionId },
                        { QStringLiteral("extensionName"),
                          QStringLiteral("kalburator.canon") },
                        { QStringLiteral("canon"), QStringLiteral("{\"v\":9}") } } });

        auto resp = httpRequest(QUrl(server.baseUrl() + kListTasksPath),
                                "POST", {},
                                QJsonDocument(body).toJson(
                                    QJsonDocument::Compact));
        QCOMPARE(resp.status, 201);
        const QJsonObject echoed =
            QJsonDocument::fromJson(resp.body).object();
        const QString minted =
            echoed.value(QStringLiteral("id")).toString();
        QVERIFY(echoed.contains(QStringLiteral("extensions")));

        // Wire-lie confirmation #1: plain listing carries no extensions.
        resp = httpRequest(QUrl(server.baseUrl() + kListTasksPath), "GET");
        QVERIFY(resp.ok());
        const QJsonArray plain =
            QJsonDocument::fromJson(resp.body).object()
                .value(QStringLiteral("value")).toArray();
        QCOMPARE(plain.size(), 1);
        QVERIFY(!plain.at(0).toObject().contains(QStringLiteral("extensions")));

        // Wire-lie confirmation #2: even the expand shows no extensions.
        resp = httpRequest(expandUrl(server.baseUrl(), kListTasksPath, false),
                           "GET");
        QVERIFY(resp.ok());
        const QJsonArray expanded =
            QJsonDocument::fromJson(resp.body).object()
                .value(QStringLiteral("value")).toArray();
        QVERIFY(!expanded.at(0).toObject()
                     .contains(QStringLiteral("extensions")));
        Q_UNUSED(minted);
    }

    void navPostCarrierUpsertsByExtensionName()
    {
        MockGraphTodoServer server;
        QVERIFY(server.start());
        server.addCollection(kListTasksPath, { makeTask(0) });

        QJsonObject first{
            { QStringLiteral("extensionName"), QStringLiteral("kalburator.canon") },
            { QStringLiteral("canon"), QStringLiteral("{\"v\":7}") } };
        auto resp = httpRequest(
            QUrl(server.baseUrl() + kListTasksPath
                 + QStringLiteral("/tid-0/extensions")),
            "POST", {},
            QJsonDocument(first).toJson(QJsonDocument::Compact));
        QCOMPARE(resp.status, 201);
        QJsonObject row = QJsonDocument::fromJson(resp.body).object();
        QCOMPARE(row.value(QStringLiteral("id")).toString(),
                 kCanonExtensionId);

        // Second POST with the SAME extensionName replaces values — one row.
        QJsonObject second{
            { QStringLiteral("extensionName"),
              QStringLiteral("kalburator.canon") },
            { QStringLiteral("canon"), QStringLiteral("{\"v\":8}") } };
        resp = httpRequest(
            QUrl(server.baseUrl() + kListTasksPath
                 + QStringLiteral("/tid-0/extensions")),
            "POST", {},
            QJsonDocument(second).toJson(QJsonDocument::Compact));
        QCOMPARE(resp.status, 201);
        row = QJsonDocument::fromJson(resp.body).object();
        QCOMPARE(row.value(QStringLiteral("canon")).toString(),
                 QStringLiteral("{\"v\":8}"));

        resp = httpRequest(expandUrl(server.baseUrl(), kListTasksPath, false),
                           "GET");
        QVERIFY(resp.ok());
        const QJsonArray value = QJsonDocument::fromJson(resp.body).object()
                                     .value(QStringLiteral("value")).toArray();
        const QJsonArray exts = value.at(0).toObject()
                                    .value(QStringLiteral("extensions"))
                                    .toArray();
        QCOMPARE(exts.size(), 1);
        QCOMPARE(exts.at(0).toObject().value(QStringLiteral("canon"))
                     .toString(),
                 QStringLiteral("{\"v\":8}"));
        QCOMPARE(exts.at(0).toObject().value(QStringLiteral("id")).toString(),
                 kCanonExtensionId);

        // Nav carrier on unknown task ⇒ ErrorItemNotFound.
        resp = httpRequest(
            QUrl(server.baseUrl() + kListTasksPath
                 + QStringLiteral("/nope/extensions")),
            "POST", {},
            QJsonDocument(first).toJson(QJsonDocument::Compact));
        QCOMPARE(resp.status, 404);
        QCOMPARE(QJsonDocument::fromJson(resp.body).object()
                     .value(QStringLiteral("error")).toObject()
                     .value(QStringLiteral("code")).toString(),
                 QStringLiteral("ErrorItemNotFound"));
    }

    void patchWithExtensionsKeyRejectedAs500()
    {
        MockGraphTodoServer server;
        QVERIFY(server.start());
        server.addCollection(kListTasksPath, { makeTask(0) });

        const QJsonObject badPatch{
            { QStringLiteral("title"), QStringLiteral("x") },
            { QStringLiteral("extensions"), QJsonArray{} }
        };
        auto resp = httpRequest(
            QUrl(server.baseUrl() + kListTasksPath + QStringLiteral("/tid-0")),
            "PATCH", {},
            QJsonDocument(badPatch).toJson(QJsonDocument::Compact));
        QCOMPARE(resp.status, 500);

        // Extension-free PATCH still merges normally.
        resp = httpRequest(
            QUrl(server.baseUrl() + kListTasksPath + QStringLiteral("/tid-0")),
            "PATCH", {},
            QJsonDocument(QJsonObject{
                { QStringLiteral("title"), QStringLiteral("Renamed") } })
                .toJson(QJsonDocument::Compact));
        QVERIFY(resp.ok());
        QCOMPARE(QJsonDocument::fromJson(resp.body).object()
                     .value(QStringLiteral("title")).toString(),
                 QStringLiteral("Renamed"));
        resp = httpRequest(
            QUrl(server.baseUrl() + kListTasksPath + QStringLiteral("?$top=5")),
            "GET");
        QVERIFY(resp.ok());
        QCOMPARE(QJsonDocument::fromJson(resp.body).object()
                     .value(QStringLiteral("value")).toArray().at(0)
                     .toObject()
                     .value(QStringLiteral("title")).toString(),
                 QStringLiteral("Renamed"));
    }

    void expandFilterWrongPrefixRejectedAs500()
    {
        MockGraphTodoServer server;
        QVERIFY(server.start());
        server.addCollection(kListTasksPath, { makeTask(0) });

        QUrl url(server.baseUrl() + kListTasksPath);
        QUrlQuery query;
        query.addQueryItem(
            QStringLiteral("$expand"),
            QStringLiteral("extensions($filter=Id eq 'com.example.foo')"));
        url.setQuery(query);
        const auto resp = httpRequest(url, "GET");
        QCOMPARE(resp.status, 500);
    }

    void createMintsTransportIdEndingInEquals()
    {
        MockGraphTodoServer server;
        QVERIFY(server.start());
        server.addCollection(kListTasksPath, {});

        auto resp = httpRequest(QUrl(server.baseUrl() + kListTasksPath),
                                "POST", {},
                                QJsonDocument(QJsonObject{
                                    { QStringLiteral("title"),
                                      QStringLiteral("New Task") } })
                                    .toJson(QJsonDocument::Compact));
        QCOMPARE(resp.status, 201);
        const QString minted = QJsonDocument::fromJson(resp.body).object()
                                   .value(QStringLiteral("id")).toString();
        QVERIFY(minted.startsWith(QLatin1String("AQMkTEST")));
        QVERIFY2(minted.endsWith(QLatin1Char('=')),
                 "Graph todoTask transport ids end in '='");

        resp = httpRequest(QUrl(server.baseUrl() + kListTasksPath), "GET");
        QVERIFY(resp.ok());
        const QJsonArray value = QJsonDocument::fromJson(resp.body).object()
                                     .value(QStringLiteral("value")).toArray();
        QCOMPARE(value.size(), 1);
        QCOMPARE(value.at(0).toObject().value(QStringLiteral("id")).toString(),
                 minted);
    }

    void deleteRemovesAndMissGetsErrorItemNotFoundShape()
    {
        MockGraphTodoServer server;
        QVERIFY(server.start());
        server.addCollection(kListTasksPath,
                             { makeTask(0), makeTask(1) });

        auto resp = httpRequest(
            QUrl(server.baseUrl() + kListTasksPath + QStringLiteral("/tid-0")),
            "DELETE");
        QCOMPARE(resp.status, 204);

        // Re-list confirmation drill: exactly one survivor.
        resp = httpRequest(QUrl(server.baseUrl() + kListTasksPath), "GET");
        QVERIFY(resp.ok());
        const QJsonArray value = QJsonDocument::fromJson(resp.body).object()
                                     .value(QStringLiteral("value")).toArray();
        QCOMPARE(value.size(), 1);
        QCOMPARE(value.at(0).toObject().value(QStringLiteral("id")).toString(),
                 QStringLiteral("tid-1"));

        resp = httpRequest(
            QUrl(server.baseUrl() + kListTasksPath + QStringLiteral("/tid-0")),
            "DELETE");
        QCOMPARE(resp.status, 404);
        QCOMPARE(QJsonDocument::fromJson(resp.body).object()
                     .value(QStringLiteral("error")).toObject()
                     .value(QStringLiteral("code")).toString(),
                 QStringLiteral("ErrorItemNotFound"));
    }

    void bearerAuthorizationHeaderRecorded()
    {
        MockGraphTodoServer server;
        QVERIFY(server.start());
        server.addCollection(kListTasksPath, {});

        httpRequest(
            QUrl(server.baseUrl() + kListTasksPath + QStringLiteral("?$top=5")),
            "GET",
            { { QByteArrayLiteral("Authorization"),
                QByteArrayLiteral("Bearer test-token") } });

        const auto reqs = server.requests();
        QCOMPARE(reqs.size(), 1);
        QCOMPARE(reqs.first().method, QByteArray("GET"));
        QCOMPARE(reqs.first().authorizationHeader,
                 QByteArray("Bearer test-token"));
    }
};

QTEST_MAIN(TestMockGraphTodoServer)
#include "tst_mockgraph_todo_server.moc"
