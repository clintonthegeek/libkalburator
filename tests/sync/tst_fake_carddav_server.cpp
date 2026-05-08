// Phase Ib Task 3 — FakeCardDavServer self-test.
//
// Exercises every route and failure mode of the FakeCardDavServer fixture
// so that later tests relying on it can trust its behavior.

#include <QtTest/QtTest>
#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QSignalSpy>
#include <QUrl>

#include "fakecarddavserver.h"

namespace {

// Synchronous HTTP helper: send a request, wait for reply, return it.
// Uses QSignalSpy on finished so we spin the event loop without blocking.
QNetworkReply *sendSync(QNetworkAccessManager &nam,
                        QNetworkRequest req,
                        const QByteArray &verb,
                        const QByteArray &body = QByteArray(),
                        int timeoutMs = 5000)
{
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply *reply = nullptr;
    if (verb == "GET") {
        reply = nam.get(req);
    } else if (verb == "PUT") {
        reply = nam.put(req, body);
    } else if (verb == "DELETE") {
        reply = nam.deleteResource(req);
    } else {
        // Generic custom verb (e.g. PROPFIND).
        reply = nam.sendCustomRequest(req, verb, body);
    }

    QSignalSpy spy(reply, &QNetworkReply::finished);
    if (!reply->isFinished())
        spy.wait(timeoutMs);
    return reply;
}

QNetworkRequest makeReq(const QUrl &url, const QByteArray &contentType = QByteArray())
{
    QNetworkRequest req(url);
    if (!contentType.isEmpty())
        req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    return req;
}

constexpr const char kPropfindBody[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<d:propfind xmlns:d=\"DAV:\">"
    "<d:prop><d:current-user-principal/></d:prop>"
    "</d:propfind>";

const QByteArray kSampleVCard =
    "BEGIN:VCARD\r\n"
    "VERSION:3.0\r\n"
    "UID:test-uid-001\r\n"
    "FN:Alice Example\r\n"
    "END:VCARD\r\n";

} // namespace

class TstFakeCardDavServer : public QObject
{
    Q_OBJECT
private slots:
    void startListening_binds_to_localhost_port();
    void baseUrl_has_correct_scheme_and_host();

    void propfind_root_returns_207_with_principal_href();
    void propfind_principals_returns_207_with_home_set();
    void propfind_addressbooks_returns_207_with_collection_list();
    void propfind_addressbooks_multiple_books();

    void get_seeded_record_returns_200_with_body();
    void get_missing_record_returns_404();

    void put_new_record_returns_201();
    void put_existing_record_returns_204();
    void put_then_get_roundtrip();

    void delete_existing_record_returns_204();
    void delete_missing_record_returns_404();
    void delete_with_matching_etag_succeeds();
    void delete_with_wrong_etag_returns_412();

    void setReturn401_makes_every_request_fail();
    void setReturn500_makes_every_request_fail();
};

void TstFakeCardDavServer::startListening_binds_to_localhost_port()
{
    FakeCardDavServer server;
    QVERIFY(server.startListening());
    QVERIFY(server.serverPort() != 0);
}

void TstFakeCardDavServer::baseUrl_has_correct_scheme_and_host()
{
    FakeCardDavServer server;
    QVERIFY(server.startListening());
    const QUrl url = server.baseUrl();
    QCOMPARE(url.scheme(), QStringLiteral("http"));
    QCOMPARE(url.host(), QStringLiteral("127.0.0.1"));
    QVERIFY(url.port() > 0);
}

void TstFakeCardDavServer::propfind_root_returns_207_with_principal_href()
{
    FakeCardDavServer server;
    QVERIFY(server.startListening());

    QNetworkAccessManager nam;
    QNetworkRequest req = makeReq(server.baseUrl());
    req.setRawHeader("Depth", "0");
    QNetworkReply *reply = sendSync(nam, req, "PROPFIND",
                                    QByteArray(kPropfindBody));
    QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 207);
    const QByteArray body = reply->readAll();
    QVERIFY(body.contains("current-user-principal"));
    QVERIFY(body.contains("/principals/users/testuser/"));
    reply->deleteLater();
}

void TstFakeCardDavServer::propfind_principals_returns_207_with_home_set()
{
    FakeCardDavServer server;
    QVERIFY(server.startListening());

    QNetworkAccessManager nam;
    const QUrl principalUrl(server.baseUrl().toString() +
                            QStringLiteral("principals/users/testuser/"));
    QNetworkRequest req = makeReq(principalUrl);
    req.setRawHeader("Depth", "0");
    QNetworkReply *reply = sendSync(nam, req, "PROPFIND",
                                    QByteArray(kPropfindBody));
    QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 207);
    const QByteArray body = reply->readAll();
    QVERIFY(body.contains("addressbook-home-set"));
    QVERIFY(body.contains("/addressbooks/testuser/"));
    reply->deleteLater();
}

void TstFakeCardDavServer::propfind_addressbooks_returns_207_with_collection_list()
{
    FakeCardDavServer server;
    QVERIFY(server.startListening());

    QNetworkAccessManager nam;
    const QUrl homeUrl(server.baseUrl().toString() +
                       QStringLiteral("addressbooks/testuser/"));
    QNetworkRequest req = makeReq(homeUrl);
    req.setRawHeader("Depth", "1");
    QNetworkReply *reply = sendSync(nam, req, "PROPFIND",
                                    QByteArray(kPropfindBody));
    QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 207);
    const QByteArray body = reply->readAll();
    QVERIFY(body.contains("addressbook"));
    QVERIFY(body.contains("Personal"));
    reply->deleteLater();
}

void TstFakeCardDavServer::propfind_addressbooks_multiple_books()
{
    FakeCardDavServer server;
    server.setAddressbooks({
        { QStringLiteral("personal"), QStringLiteral("Personal") },
        { QStringLiteral("work"),     QStringLiteral("Work") },
    });
    QVERIFY(server.startListening());

    QNetworkAccessManager nam;
    const QUrl homeUrl(server.baseUrl().toString() +
                       QStringLiteral("addressbooks/testuser/"));
    QNetworkRequest req = makeReq(homeUrl);
    req.setRawHeader("Depth", "1");
    QNetworkReply *reply = sendSync(nam, req, "PROPFIND",
                                    QByteArray(kPropfindBody));
    QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 207);
    const QByteArray body = reply->readAll();
    QVERIFY(body.contains("Personal"));
    QVERIFY(body.contains("Work"));
    reply->deleteLater();
}

void TstFakeCardDavServer::get_seeded_record_returns_200_with_body()
{
    FakeCardDavServer server;
    server.setSeedRecords(QStringLiteral("personal"), { kSampleVCard });
    QVERIFY(server.startListening());

    QNetworkAccessManager nam;
    const QUrl url(server.baseUrl().toString() +
                   QStringLiteral("addressbooks/testuser/personal/test-uid-001.vcf"));
    QNetworkReply *reply = sendSync(nam, makeReq(url), "GET");
    QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
    const QByteArray body = reply->readAll();
    QVERIFY(body.contains("Alice Example"));
    QVERIFY(!reply->rawHeader("ETag").isEmpty());
    reply->deleteLater();
}

void TstFakeCardDavServer::get_missing_record_returns_404()
{
    FakeCardDavServer server;
    QVERIFY(server.startListening());

    QNetworkAccessManager nam;
    const QUrl url(server.baseUrl().toString() +
                   QStringLiteral("addressbooks/testuser/personal/no-such.vcf"));
    QNetworkReply *reply = sendSync(nam, makeReq(url), "GET");
    QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 404);
    reply->deleteLater();
}

void TstFakeCardDavServer::put_new_record_returns_201()
{
    FakeCardDavServer server;
    QVERIFY(server.startListening());

    QNetworkAccessManager nam;
    const QUrl url(server.baseUrl().toString() +
                   QStringLiteral("addressbooks/testuser/personal/new-uid.vcf"));
    QNetworkRequest req = makeReq(url, "text/vcard; charset=utf-8");
    const QByteArray vcard =
        "BEGIN:VCARD\r\nVERSION:3.0\r\nUID:new-uid\r\nFN:Bob\r\nEND:VCARD\r\n";
    QNetworkReply *reply = sendSync(nam, req, "PUT", vcard);
    QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 201);
    QVERIFY(!reply->rawHeader("ETag").isEmpty());
    reply->deleteLater();
}

void TstFakeCardDavServer::put_existing_record_returns_204()
{
    FakeCardDavServer server;
    server.setSeedRecords(QStringLiteral("personal"), { kSampleVCard });
    QVERIFY(server.startListening());

    QNetworkAccessManager nam;
    const QUrl url(server.baseUrl().toString() +
                   QStringLiteral("addressbooks/testuser/personal/test-uid-001.vcf"));
    QNetworkRequest req = makeReq(url, "text/vcard; charset=utf-8");
    const QByteArray updated =
        "BEGIN:VCARD\r\nVERSION:3.0\r\nUID:test-uid-001\r\nFN:Alice Updated\r\nEND:VCARD\r\n";
    QNetworkReply *reply = sendSync(nam, req, "PUT", updated);
    QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 204);
    reply->deleteLater();
}

void TstFakeCardDavServer::put_then_get_roundtrip()
{
    FakeCardDavServer server;
    QVERIFY(server.startListening());

    QNetworkAccessManager nam;
    const QUrl url(server.baseUrl().toString() +
                   QStringLiteral("addressbooks/testuser/personal/rt-uid.vcf"));
    const QByteArray vcard =
        "BEGIN:VCARD\r\nVERSION:3.0\r\nUID:rt-uid\r\nFN:Roundtrip\r\nEND:VCARD\r\n";

    // PUT
    {
        QNetworkRequest req = makeReq(url, "text/vcard; charset=utf-8");
        QNetworkReply *reply = sendSync(nam, req, "PUT", vcard);
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 201);
        reply->deleteLater();
    }

    // GET — must return what we PUT
    {
        QNetworkReply *reply = sendSync(nam, makeReq(url), "GET");
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
        QCOMPARE(reply->readAll(), vcard);
        reply->deleteLater();
    }
}

void TstFakeCardDavServer::delete_existing_record_returns_204()
{
    FakeCardDavServer server;
    server.setSeedRecords(QStringLiteral("personal"), { kSampleVCard });
    QVERIFY(server.startListening());

    QNetworkAccessManager nam;
    const QUrl url(server.baseUrl().toString() +
                   QStringLiteral("addressbooks/testuser/personal/test-uid-001.vcf"));
    QNetworkReply *reply = sendSync(nam, makeReq(url), "DELETE");
    QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 204);
    reply->deleteLater();
}

void TstFakeCardDavServer::delete_missing_record_returns_404()
{
    FakeCardDavServer server;
    QVERIFY(server.startListening());

    QNetworkAccessManager nam;
    const QUrl url(server.baseUrl().toString() +
                   QStringLiteral("addressbooks/testuser/personal/ghost.vcf"));
    QNetworkReply *reply = sendSync(nam, makeReq(url), "DELETE");
    QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 404);
    reply->deleteLater();
}

void TstFakeCardDavServer::delete_with_matching_etag_succeeds()
{
    FakeCardDavServer server;
    server.setSeedRecords(QStringLiteral("personal"), { kSampleVCard });
    QVERIFY(server.startListening());

    QNetworkAccessManager nam;
    const QUrl url(server.baseUrl().toString() +
                   QStringLiteral("addressbooks/testuser/personal/test-uid-001.vcf"));

    // First GET to learn the ETag.
    QByteArray etag;
    {
        QNetworkReply *reply = sendSync(nam, makeReq(url), "GET");
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
        etag = reply->rawHeader("ETag");
        reply->deleteLater();
    }
    QVERIFY(!etag.isEmpty());

    // DELETE with correct If-Match.
    {
        QNetworkRequest req = makeReq(url);
        req.setRawHeader("If-Match", etag);
        QNetworkReply *reply = sendSync(nam, req, "DELETE");
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 204);
        reply->deleteLater();
    }
}

void TstFakeCardDavServer::delete_with_wrong_etag_returns_412()
{
    FakeCardDavServer server;
    server.setSeedRecords(QStringLiteral("personal"), { kSampleVCard });
    QVERIFY(server.startListening());

    QNetworkAccessManager nam;
    const QUrl url(server.baseUrl().toString() +
                   QStringLiteral("addressbooks/testuser/personal/test-uid-001.vcf"));

    QNetworkRequest req = makeReq(url);
    req.setRawHeader("If-Match", "\"wrong-etag\"");
    QNetworkReply *reply = sendSync(nam, req, "DELETE");
    QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 412);
    reply->deleteLater();
}

void TstFakeCardDavServer::setReturn401_makes_every_request_fail()
{
    FakeCardDavServer server;
    server.setReturn401(true);
    QVERIFY(server.startListening());

    QNetworkAccessManager nam;
    QNetworkRequest req = makeReq(server.baseUrl());
    req.setRawHeader("Depth", "0");
    // QNAM automatically handles 401 by emitting authenticationRequired.
    // For the purpose of this fixture test we just check the raw status
    // code arrives via HttpStatusCodeAttribute (attribute is set even for
    // error responses when using sendCustomRequest).
    QNetworkReply *reply = sendSync(nam, req, "PROPFIND",
                                    QByteArray(kPropfindBody));
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // QNAM may retry after authenticationRequired; if no credentials
    // are provided it surfaces 401 or a network auth error.
    // Either way the connection did not succeed with 207.
    QVERIFY(status == 401 || reply->error() != QNetworkReply::NoError);
    reply->deleteLater();
}

void TstFakeCardDavServer::setReturn500_makes_every_request_fail()
{
    FakeCardDavServer server;
    server.setReturn500(true);
    QVERIFY(server.startListening());

    QNetworkAccessManager nam;
    QNetworkRequest req = makeReq(server.baseUrl());
    req.setRawHeader("Depth", "0");
    QNetworkReply *reply = sendSync(nam, req, "PROPFIND",
                                    QByteArray(kPropfindBody));
    QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 500);
    reply->deleteLater();
}

QTEST_GUILESS_MAIN(TstFakeCardDavServer)
#include "tst_fake_carddav_server.moc"
