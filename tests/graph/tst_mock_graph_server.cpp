#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include "mockgraphserver.h"

using Kalburator::Graph::MockGraphServer;

class TestMockGraphServer : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void collectionPaginatesWithNextLink()
    {
        MockGraphServer server;
        QVERIFY(server.start());

        QJsonArray events;
        for (int i = 0; i < 25; ++i) {
            QJsonObject evt;
            evt.insert(QStringLiteral("id"),
                       QStringLiteral("event-%1").arg(i, 3, 10, QLatin1Char('0')));
            evt.insert(QStringLiteral("subject"),
                       QStringLiteral("CORPUS: event %1").arg(i));
            events.append(evt);
        }
        server.addCollection(QStringLiteral("/me/calendar/events"), events);

        // Page 1: $top=10 honored, absolute nextLink present.
        const auto get = [](const QString &url) {
            return Kalburator::Graph::httpRequest(QUrl(url), "GET");
        };
        auto resp = get(server.baseUrl()
                        + QStringLiteral("/me/calendar/events?$top=10"));
        QVERIFY(resp.ok());
        QJsonObject page = QJsonDocument::fromJson(resp.body).object();
        QCOMPARE(page.value("value").toArray().size(), 10);
        const QString next1 = page.value("@odata.nextLink").toString();
        QVERIFY(!next1.isEmpty());
        QVERIFY2(next1.startsWith(server.baseUrl()),
                 "nextLink must be absolute (Graph wire truth)");

        // Page 2 via nextLink.
        resp = get(next1);
        QVERIFY(resp.ok());
        page = QJsonDocument::fromJson(resp.body).object();
        QCOMPARE(page.value("value").toArray().size(), 10);
        const QString next2 = page.value("@odata.nextLink").toString();
        QVERIFY(!next2.isEmpty());

        // Page 3: remaining 5, no nextLink.
        resp = get(next2);
        QVERIFY(resp.ok());
        page = QJsonDocument::fromJson(resp.body).object();
        QCOMPARE(page.value("value").toArray().size(), 5);
        QVERIFY(page.value("@odata.nextLink").isUndefined());

        server.stop();
    }

    void exactRouteServesAndUnknownIdGets404Shape()
    {
        MockGraphServer server;
        QVERIFY(server.start());

        QJsonObject master;
        master.insert(QStringLiteral("id"), QStringLiteral("abc"));
        master.insert(QStringLiteral("subject"), QStringLiteral("master"));
        server.addCollection(QStringLiteral("/me/calendar/events"), {});
        server.addRoute(QStringLiteral("GET"), QStringLiteral("/me/events/abc"), master);

        auto resp = Kalburator::Graph::httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/me/events/abc")), "GET");
        QVERIFY(resp.ok());
        QCOMPARE(QJsonDocument::fromJson(resp.body).object()
                     .value("id").toString(), QStringLiteral("abc"));

        resp = Kalburator::Graph::httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/me/events/nope")), "GET");
        QCOMPARE(resp.status, 404);
        const QJsonObject err = QJsonDocument::fromJson(resp.body).object()
                                    .value("error").toObject();
        QCOMPARE(err.value("code").toString(),
                 QStringLiteral("ErrorItemNotFound"));

        server.stop();
    }

    void deltaWalkIssuesDeltaLinkAndReplaysChanges()
    {
        MockGraphServer server;
        QVERIFY(server.start());

        QJsonArray initial;
        QJsonObject e0;
        e0.insert(QStringLiteral("id"), QStringLiteral("e0"));
        initial.append(e0);
        const QString deltaPath = QStringLiteral("/me/calendar/events/delta");
        server.addCollection(QStringLiteral("/me/calendar/events"), initial);

        // Initial walk → all items + deltaLink.
        auto resp = Kalburator::Graph::httpRequest(
            QUrl(server.baseUrl() + deltaPath), "GET");
        QVERIFY(resp.ok());
        QJsonObject page = QJsonDocument::fromJson(resp.body).object();
        QCOMPARE(page.value("value").toArray().size(), 1);
        QString link = page.value("@odata.deltaLink").toString();
        QVERIFY(!link.isEmpty());

        // Queue a change page, then present the token.
        QJsonArray changes;
        QJsonObject e1;
        e1.insert(QStringLiteral("id"), QStringLiteral("e1"));
        e1.insert(QStringLiteral("subject"), QStringLiteral("new occurrence"));
        changes.append(e1);
        server.queueDeltaChanges(QStringLiteral("/me/calendar/events"),
                                 QStringLiteral("delta_1"), changes);

        const QUrl linkUrl(link);
        QVERIFY(linkUrl.query().contains(QStringLiteral("$deltatoken=delta_1")));
        resp = Kalburator::Graph::httpRequest(linkUrl, "GET");
        QVERIFY(resp.ok());
        page = QJsonDocument::fromJson(resp.body).object();
        QCOMPARE(page.value("value").toArray().size(), 1);
        QCOMPARE(page.value("value").toArray().at(0).toObject()
                     .value("id").toString(), QStringLiteral("e1"));

        // The replay issues a fresh deltaLink; presenting it with NO queued
        // changes yields an empty change set + another link (fixpoint).
        link = page.contains(QStringLiteral("@odata.deltaLink"))
            ? page.value("@odata.deltaLink").toString()
            : page.value("@odata.nextLink").toString();
        QVERIFY(!link.isEmpty());
        resp = Kalburator::Graph::httpRequest(QUrl(link), "GET");
        QVERIFY(resp.ok());
        page = QJsonDocument::fromJson(resp.body).object();
        QCOMPARE(page.value("value").toArray().size(), 0);
        QVERIFY(!page.value("@odata.deltaLink").toString().isEmpty());

        // Unknown/expired token → 410 ResyncRequired.
        resp = Kalburator::Graph::httpRequest(
            QUrl(server.baseUrl() + deltaPath + QStringLiteral("?$deltatoken=bogus")),
            "GET");
        QCOMPARE(resp.status, 410);
        QCOMPARE(QJsonDocument::fromJson(resp.body).object()
                     .value("error").toObject().value("code").toString(),
                 QStringLiteral("ResyncRequired"));

        server.stop();
    }

    void requestsAreRecorded()
    {
        MockGraphServer server;
        QVERIFY(server.start());
        server.addCollection(QStringLiteral("/me/calendar/events"), {});

        Kalburator::Graph::httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/me/calendar/events?$top=5")),
            "GET");

        const auto reqs = server.requests();
        QCOMPARE(reqs.size(), 1);
        QCOMPARE(reqs.first().method, QByteArray("GET"));
        QVERIFY(reqs.first().path.contains(QStringLiteral("$top=5")));

        server.clearRequests();
        QVERIFY(server.requests().isEmpty());

        server.stop();
    }
};

QTEST_MAIN(TestMockGraphServer)
#include "tst_mock_graph_server.moc"
