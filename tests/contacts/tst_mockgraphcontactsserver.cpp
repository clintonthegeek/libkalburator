// B2C P2.b — MockGraphContactsServer transport tests. Pins: folders
// endpoint, $top/$skip pagination with absolute nextLink, expand-on/off
// carrier visibility, nav POST carrier persistence, PATCH-with-extensions
// ⇒ 500, wrong-prefix expand filter ⇒ 500, create minting an '='-terminated
// id, delete 204/404, Bearer Authorization header recording.

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QUrl>
#include <QUrlQuery>

#include "mockgraphcontactsserver.h"

using Kalburator::Contacts::httpRequest;
using Kalburator::Contacts::MockGraphContactsServer;

namespace {

const QString kCanonExtensionId = QStringLiteral(
    "Microsoft.OutlookServices.OpenTypeExtension.kalburator.canon");

QJsonObject makeContact(int i, bool withCarrier = false)
{
    QJsonObject contact{
        { QStringLiteral("id"), QStringLiteral("cid-%1").arg(i) },
        { QStringLiteral("displayName"), QStringLiteral("Contact %1").arg(i) },
        { QStringLiteral("givenName"), QStringLiteral("Contact") },
        { QStringLiteral("surname"), QStringLiteral("%1").arg(i) }
    };
    if (withCarrier) {
        contact.insert(QStringLiteral("extensions"),
                       QJsonArray{ QJsonObject{
                           { QStringLiteral("id"), kCanonExtensionId },
                           { QStringLiteral("canon"),
                             QStringLiteral("{\"v\":%1}").arg(i) } } });
    }
    return contact;
}

QUrl expandUrl(const QString &base, const QString &path, bool preEncoded)
{
    const QString filter =
        QStringLiteral("extensions($filter=Id eq '%1')").arg(kCanonExtensionId);
    if (preEncoded)
        return QUrl(base + path
                    + QStringLiteral(
                        "?%24expand=extensions(%24filter=Id%20eq%20%27"
                        "Microsoft.OutlookServices.OpenTypeExtension."
                        "kalburator.canon%27)"));
    QUrl url(base + path);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("$expand"), filter);
    url.setQuery(query);
    return url;
}

} // namespace

class TestMockGraphContactsServer : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void foldersServedAtContactFoldersEndpoint()
    {
        MockGraphContactsServer server;
        QVERIFY(server.start());
        server.setContactFolders({ QJsonObject{
            { QStringLiteral("id"), QStringLiteral("fld-1") },
            { QStringLiteral("displayName"), QStringLiteral("Kalburator") } } });

        const auto resp = httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1.0/me/contactFolders")),
            "GET");
        QVERIFY(resp.ok());
        const QJsonArray value = QJsonDocument::fromJson(resp.body).object()
                                     .value(QStringLiteral("value")).toArray();
        QCOMPARE(value.size(), 1);
        QCOMPARE(value.at(0).toObject().value(QStringLiteral("id")).toString(),
                 QStringLiteral("fld-1"));
    }

    void collectionPaginatesWithAbsoluteNextLink()
    {
        MockGraphContactsServer server;
        QVERIFY(server.start());

        QJsonArray contacts;
        for (int i = 0; i < 25; ++i)
            contacts.append(makeContact(i));
        server.addCollection(QStringLiteral("/v1.0/me/contacts"), contacts);

        auto resp = httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1.0/me/contacts?$top=10")),
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
    }

    void expandTogglesCarrierVisibility()
    {
        MockGraphContactsServer server;
        QVERIFY(server.start());
        server.addCollection(QStringLiteral("/v1.0/me/contacts"),
                             { makeContact(0, true), makeContact(1, false) });

        // Expand ON (QUrl-encoded variant): matching rows visible; the
        // carrier-less contact has no extensions key.
        auto resp = httpRequest(
            expandUrl(server.baseUrl(), QStringLiteral("/v1.0/me/contacts"),
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
        resp = httpRequest(
            expandUrl(server.baseUrl(), QStringLiteral("/v1.0/me/contacts"),
                      true),
            "GET");
        QVERIFY(resp.ok());
        value = QJsonDocument::fromJson(resp.body).object()
                    .value(QStringLiteral("value")).toArray();
        QVERIFY(value.at(0).toObject().contains(QStringLiteral("extensions")));

        // Expand OFF: extensions stripped even from carrier-bearing records.
        resp = httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1.0/me/contacts")),
            "GET");
        QVERIFY(resp.ok());
        value = QJsonDocument::fromJson(resp.body).object()
                    .value(QStringLiteral("value")).toArray();
        QVERIFY(!value.at(0).toObject().contains(QStringLiteral("extensions")));
        QVERIFY(!value.at(1).toObject().contains(QStringLiteral("extensions")));
    }

    void navPostCarrierPersistsIntoListings()
    {
        MockGraphContactsServer server;
        QVERIFY(server.start());
        server.addCollection(QStringLiteral("/v1.0/me/contacts"),
                             { makeContact(0) });

        const QJsonObject row{
            { QStringLiteral("id"), kCanonExtensionId },
            { QStringLiteral("canon"), QStringLiteral("{\"v\":7}") }
        };
        auto resp = httpRequest(
            QUrl(server.baseUrl()
                 + QStringLiteral("/v1.0/me/contacts/cid-0/extensions")),
            "POST", {}, QJsonDocument(row).toJson(QJsonDocument::Compact));
        QCOMPARE(resp.status, 201);
        const QJsonObject echoed = QJsonDocument::fromJson(resp.body).object();
        QCOMPARE(echoed.value(QStringLiteral("id")).toString(),
                 kCanonExtensionId);
        QCOMPARE(echoed.value(QStringLiteral("canon")).toString(),
                 QStringLiteral("{\"v\":7}"));

        resp = httpRequest(
            expandUrl(server.baseUrl(), QStringLiteral("/v1.0/me/contacts"),
                      false),
            "GET");
        QVERIFY(resp.ok());
        const QJsonArray value = QJsonDocument::fromJson(resp.body).object()
                                     .value(QStringLiteral("value")).toArray();
        QVERIFY(value.at(0).toObject().contains(QStringLiteral("extensions")));
        QCOMPARE(value.at(0).toObject()
                     .value(QStringLiteral("extensions")).toArray().size(),
                 1);

        // Nav carrier on unknown contact ⇒ ErrorItemNotFound.
        resp = httpRequest(
            QUrl(server.baseUrl()
                 + QStringLiteral("/v1.0/me/contacts/nope/extensions")),
            "POST", {},
            QJsonDocument(row).toJson(QJsonDocument::Compact));
        QCOMPARE(resp.status, 404);
        QCOMPARE(QJsonDocument::fromJson(resp.body).object()
                     .value(QStringLiteral("error")).toObject()
                     .value(QStringLiteral("code")).toString(),
                 QStringLiteral("ErrorItemNotFound"));
    }

    void patchWithExtensionsKeyRejectedAs500()
    {
        MockGraphContactsServer server;
        QVERIFY(server.start());
        server.addCollection(QStringLiteral("/v1.0/me/contacts"),
                             { makeContact(0) });

        const QJsonObject badPatch{
            { QStringLiteral("displayName"), QStringLiteral("x") },
            { QStringLiteral("extensions"), QJsonArray{} }
        };
        auto resp = httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1.0/me/contacts/cid-0")),
            "PATCH", {},
            QJsonDocument(badPatch).toJson(QJsonDocument::Compact));
        QCOMPARE(resp.status, 500);

        // Extension-free PATCH still merges normally.
        resp = httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1.0/me/contacts/cid-0")),
            "PATCH", {},
            QJsonDocument(QJsonObject{
                { QStringLiteral("displayName"), QStringLiteral("Renamed") } })
                .toJson(QJsonDocument::Compact));
        QVERIFY(resp.ok());
        QCOMPARE(QJsonDocument::fromJson(resp.body).object()
                     .value(QStringLiteral("displayName")).toString(),
                 QStringLiteral("Renamed"));
        resp = httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1.0/me/contacts?$top=5")),
            "GET");
        QVERIFY(resp.ok());
        QCOMPARE(QJsonDocument::fromJson(resp.body).object()
                     .value(QStringLiteral("value")).toArray().at(0)
                     .toObject()
                     .value(QStringLiteral("displayName")).toString(),
                 QStringLiteral("Renamed"));
    }

    void expandFilterWrongPrefixRejectedAs500()
    {
        MockGraphContactsServer server;
        QVERIFY(server.start());
        server.addCollection(QStringLiteral("/v1.0/me/contacts"),
                             { makeContact(0) });

        QUrl url(server.baseUrl() + QStringLiteral("/v1.0/me/contacts"));
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
        MockGraphContactsServer server;
        QVERIFY(server.start());
        server.addCollection(QStringLiteral("/v1.0/me/contacts"), {});

        auto resp = httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1.0/me/contacts")),
            "POST", {},
            QJsonDocument(QJsonObject{
                { QStringLiteral("displayName"), QStringLiteral("New Guy") } })
                .toJson(QJsonDocument::Compact));
        QCOMPARE(resp.status, 201);
        const QString minted = QJsonDocument::fromJson(resp.body).object()
                                   .value(QStringLiteral("id")).toString();
        QVERIFY(minted.startsWith(QLatin1String("AQMkTEST")));
        QVERIFY2(minted.endsWith(QLatin1Char('=')),
                 "Graph contacts transport ids end in '='");

        resp = httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1.0/me/contacts")),
            "GET");
        QVERIFY(resp.ok());
        const QJsonArray value = QJsonDocument::fromJson(resp.body).object()
                                     .value(QStringLiteral("value")).toArray();
        QCOMPARE(value.size(), 1);
        QCOMPARE(value.at(0).toObject().value(QStringLiteral("id")).toString(),
                 minted);
    }

    void deleteRemovesAndMissGetsNotFoundShape()
    {
        MockGraphContactsServer server;
        QVERIFY(server.start());
        server.addCollection(QStringLiteral("/v1.0/me/contacts"),
                             { makeContact(0), makeContact(1) });

        auto resp = httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1.0/me/contacts/cid-0")),
            "DELETE");
        QCOMPARE(resp.status, 204);

        // Re-list confirmation drill: exactly one survivor.
        resp = httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1.0/me/contacts")),
            "GET");
        QVERIFY(resp.ok());
        const QJsonArray value = QJsonDocument::fromJson(resp.body).object()
                                     .value(QStringLiteral("value")).toArray();
        QCOMPARE(value.size(), 1);
        QCOMPARE(value.at(0).toObject().value(QStringLiteral("id")).toString(),
                 QStringLiteral("cid-1"));

        resp = httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1.0/me/contacts/cid-0")),
            "DELETE");
        QCOMPARE(resp.status, 404);
        QCOMPARE(QJsonDocument::fromJson(resp.body).object()
                     .value(QStringLiteral("error")).toObject()
                     .value(QStringLiteral("code")).toString(),
                 QStringLiteral("ErrorItemNotFound"));
    }

    void bearerAuthorizationHeaderRecorded()
    {
        MockGraphContactsServer server;
        QVERIFY(server.start());
        server.addCollection(QStringLiteral("/v1.0/me/contacts"), {});

        httpRequest(
            QUrl(server.baseUrl() + QStringLiteral("/v1.0/me/contacts?$top=5")),
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

QTEST_MAIN(TestMockGraphContactsServer)
#include "tst_mockgraphcontactsserver.moc"
