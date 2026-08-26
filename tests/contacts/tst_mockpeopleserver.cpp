// B2C P2.b — MockPeopleServer transport tests. Pins: pageSize/pageToken
// paging, personFields projection, requestSyncToken + queued changes +
// expired-token 410, createContact minting resourceName and echoing
// clientData verbatim, updateContact masked merge, deleteContact removal,
// Bearer Authorization header recording.

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include "mockpeopleserver.h"

using Kalburator::People::httpRequest;
using Kalburator::People::MockPeopleServer;

namespace {

QJsonObject makePerson(int i)
{
    return QJsonObject{
        { QStringLiteral("resourceName"), QStringLiteral("people/seed%1").arg(i) },
        { QStringLiteral("etag"), QStringLiteral("%1").arg(i) },
        { QStringLiteral("names"),
          QJsonArray{ QJsonObject{
              { QStringLiteral("displayName"),
                QStringLiteral("Person %1").arg(i) } } } },
        { QStringLiteral("emailAddresses"),
          QJsonArray{ QJsonObject{
              { QStringLiteral("value"),
                QStringLiteral("p%1@example.com").arg(i) } } } },
        { QStringLiteral("phoneNumbers"),
          QJsonArray{ QJsonObject{
              { QStringLiteral("value"),
                QStringLiteral("+1555%1").arg(i, 7, 10, QLatin1Char('0')) } } } }
    };
}

QJsonObject connectionsPage(const QByteArray &body)
{
    return QJsonDocument::fromJson(body).object();
}

} // namespace

class TestMockPeopleServer : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_server = new MockPeopleServer(this);
        QVERIFY(m_server->start());
    }

    void cleanup()
    {
        delete m_server;
        m_server = nullptr;
    }

    void connectionsPaginateByPageSizeAndPageToken()
    {
        QJsonArray people;
        for (int i = 0; i < 12; ++i)
            people.append(makePerson(i));
        m_server->setConnections(people);

        auto resp = httpRequest(
            m_server->baseUrl()
                + QStringLiteral("/v1/people/me/connections?pageSize=10"),
            "GET");
        QVERIFY(resp.ok());
        QJsonObject page = connectionsPage(resp.body);
        QCOMPARE(page.value(QStringLiteral("connections")).toArray().size(), 10);
        const QString token =
            page.value(QStringLiteral("nextPageToken")).toString();
        QVERIFY(!token.isEmpty());

        resp = httpRequest(
            m_server->baseUrl()
                + QStringLiteral("/v1/people/me/connections?pageSize=10&pageToken=")
                + token,
            "GET");
        QVERIFY(resp.ok());
        page = connectionsPage(resp.body);
        QCOMPARE(page.value(QStringLiteral("connections")).toArray().size(), 2);
        QVERIFY(page.value(QStringLiteral("nextPageToken")).isUndefined());
    }

    void personFieldsProjectsToRequestedFieldsOnly()
    {
        m_server->setConnections({ makePerson(0) });

        auto resp = httpRequest(
            m_server->baseUrl()
                + QStringLiteral(
                    "/v1/people/me/connections?personFields=names,emailAddresses"),
            "GET");
        QVERIFY(resp.ok());
        const QJsonArray conns = connectionsPage(resp.body)
                                     .value(QStringLiteral("connections"))
                                     .toArray();
        QCOMPARE(conns.size(), 1);
        const QJsonObject person = conns.at(0).toObject();
        QVERIFY(person.contains(QStringLiteral("resourceName")));
        QVERIFY(person.contains(QStringLiteral("names")));
        QVERIFY(person.contains(QStringLiteral("emailAddresses")));
        QVERIFY(!person.contains(QStringLiteral("phoneNumbers")));
        // Projection keeps the etag alongside resourceName.
        QVERIFY(person.contains(QStringLiteral("etag")));
    }

    void syncTokenReplayThenExpiredGone410()
    {
        m_server->setConnections({ makePerson(0) });

        auto resp = httpRequest(
            m_server->baseUrl()
                + QStringLiteral(
                    "/v1/people/me/connections?requestSyncToken=true&pageSize=50"),
            "GET");
        QVERIFY(resp.ok());
        const QString firstToken = connectionsPage(resp.body)
                                       .value(QStringLiteral("nextSyncToken"))
                                       .toString();
        QVERIFY(!firstToken.isEmpty());

        // Presenting the live token replays ONLY the queued changed people
        // and issues a fresh token.
        QJsonObject changed = makePerson(9);
        m_server->queueConnectionChanges(firstToken, { changed });
        resp = httpRequest(
            m_server->baseUrl() + QStringLiteral("/v1/people/me/connections?")
                + QStringLiteral("sync_token=") + firstToken,
            "GET");
        QVERIFY(resp.ok());
        QJsonObject page = connectionsPage(resp.body);
        const QJsonArray conns =
            page.value(QStringLiteral("connections")).toArray();
        QCOMPARE(conns.size(), 1);
        QCOMPARE(conns.at(0).toObject().value(QStringLiteral("resourceName"))
                     .toString(),
                 QStringLiteral("people/seed9"));
        const QString secondToken =
            page.value(QStringLiteral("nextSyncToken")).toString();
        QVERIFY(!secondToken.isEmpty());

        // Expired/unknown token ⇒ 410 with {error:{code:410,...}}.
        m_server->expireSyncTokens();
        resp = httpRequest(
            m_server->baseUrl() + QStringLiteral("/v1/people/me/connections?")
                + QStringLiteral("sync_token=") + secondToken,
            "GET");
        QCOMPARE(resp.status, 410);
        QCOMPARE(connectionsPage(resp.body)
                     .value(QStringLiteral("error")).toObject()
                     .value(QStringLiteral("code")).toInt(),
                 410);
    }

    void createContactMintsResourceNameAndEchoesClientData()
    {
        const QJsonArray clientData{ QJsonObject{
            { QStringLiteral("key"), QStringLiteral("kalburator.canon") },
            { QStringLiteral("value"), QStringLiteral("{\"v\":3}") } } };
        const QJsonObject body{
            { QStringLiteral("names"),
              QJsonArray{ QJsonObject{
                  { QStringLiteral("displayName"), QStringLiteral("New One") } } } },
            { QStringLiteral("clientData"), clientData }
        };
        auto resp = httpRequest(
            m_server->baseUrl()
                + QStringLiteral("/v1/people:createContact"),
            "POST", {}, QJsonDocument(body).toJson(QJsonDocument::Compact));
        QVERIFY(resp.ok());
        const QJsonObject created =
            QJsonDocument::fromJson(resp.body).object();
        const QString rn =
            created.value(QStringLiteral("resourceName")).toString();
        QVERIFY(rn.startsWith(QLatin1String("people/c")));
        // Live-Reversible channel: clientData rows echoed verbatim.
        QCOMPARE(created.value(QStringLiteral("clientData")).toArray(), clientData);

        // Created person shows up in listings.
        resp = httpRequest(
            m_server->baseUrl()
                + QStringLiteral("/v1/people/me/connections?pageSize=50"),
            "GET");
        QVERIFY(resp.ok());
        const QJsonArray conns = connectionsPage(resp.body)
                                     .value(QStringLiteral("connections"))
                                     .toArray();
        QCOMPARE(conns.size(), 1);
        QCOMPARE(conns.at(0).toObject().value(QStringLiteral("resourceName"))
                     .toString(),
                 rn);
    }

    void updateContactMergesMaskedFieldsOnly()
    {
        auto resp = httpRequest(
            m_server->baseUrl()
                + QStringLiteral("/v1/people:createContact"),
            "POST", {},
            QJsonDocument(makePerson(0)).toJson(QJsonDocument::Compact));
        QVERIFY(resp.ok());
        const QString rn = QJsonDocument::fromJson(resp.body).object()
                               .value(QStringLiteral("resourceName")).toString();

        // Mask lists only emailAddresses; a names change in the body must
        // NOT be merged. The etag MUST ride the body (O72) — the mock
        // rejects etag-less patches just like the live service.
        const QJsonObject patch{
            { QStringLiteral("etag"), QStringLiteral("0") },
            { QStringLiteral("emailAddresses"),
              QJsonArray{ QJsonObject{
                  { QStringLiteral("value"),
                    QStringLiteral("moved@example.com") } } } },
            { QStringLiteral("names"),
              QJsonArray{ QJsonObject{
                  { QStringLiteral("displayName"),
                    QStringLiteral("SHOULD NOT MERGE") } } } }
        };
        resp = httpRequest(
            m_server->baseUrl() + QStringLiteral("/v1/") + rn
                + QStringLiteral(":updateContact?updatePersonFields=emailAddresses"),
            "PATCH", {}, QJsonDocument(patch).toJson(QJsonDocument::Compact));
        QVERIFY(resp.ok());
        const QJsonObject merged =
            QJsonDocument::fromJson(resp.body).object();
        QCOMPARE(merged.value(QStringLiteral("emailAddresses")).toArray().at(0)
                     .toObject()
                     .value(QStringLiteral("value")).toString(),
                 QStringLiteral("moved@example.com"));
        QCOMPARE(merged.value(QStringLiteral("names")).toArray().at(0)
                     .toObject()
                     .value(QStringLiteral("displayName")).toString(),
                 QStringLiteral("Person 0"));
    }

    void deleteContactRemovesPerson()
    {
        auto resp = httpRequest(
            m_server->baseUrl()
                + QStringLiteral("/v1/people:createContact"),
            "POST", {},
            QJsonDocument(makePerson(0)).toJson(QJsonDocument::Compact));
        QVERIFY(resp.ok());
        const QString rn = QJsonDocument::fromJson(resp.body).object()
                               .value(QStringLiteral("resourceName")).toString();

        resp = httpRequest(
            m_server->baseUrl() + QStringLiteral("/v1/") + rn
                + QStringLiteral(":deleteContact"),
            "DELETE");
        QVERIFY(resp.ok());

        resp = httpRequest(
            m_server->baseUrl()
                + QStringLiteral("/v1/people/me/connections?pageSize=50"),
            "GET");
        QVERIFY(resp.ok());
        QCOMPARE(connectionsPage(resp.body)
                     .value(QStringLiteral("connections")).toArray().size(),
                 0);
    }

    void bearerAuthorizationHeaderRecorded()
    {
        m_server->setConnections({ makePerson(0) });
        httpRequest(
            m_server->baseUrl()
                + QStringLiteral("/v1/people/me/connections?pageSize=5"),
            "GET",
            { { QByteArrayLiteral("Authorization"),
                QByteArrayLiteral("Bearer test-token") } });

        const auto reqs = m_server->requests();
        QCOMPARE(reqs.size(), 1);
        QCOMPARE(reqs.first().method, QByteArray("GET"));
        QCOMPARE(reqs.first().authorizationHeader,
                 QByteArray("Bearer test-token"));
    }

private:
    MockPeopleServer *m_server = nullptr;
};

QTEST_MAIN(TestMockPeopleServer)
#include "tst_mockpeopleserver.moc"
