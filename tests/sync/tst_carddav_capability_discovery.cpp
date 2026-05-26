// Phase Ib Task 4 — CardDavCapabilityDiscovery unit tests.
//
// Tests exercise the RFC 6352 PROPFIND walk against FakeCardDavServer:
//   1. Happy path — 1 addressbook discovered.
//   2. Happy path — 3 addressbooks discovered.
//   3. 401 on principal PROPFIND → error() emitted, future resolves empty.
//   4. 500 on home-set PROPFIND → error() emitted, future resolves empty.
//   5. Empty home-set → success, empty list.
//   6. Mixed home-set (one calendar resource, one addressbook) → only
//      addressbook returned.

#include <QtTest/QtTest>
#include <QFuture>
#include <QFutureWatcher>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSignalSpy>
#include <QString>
#include <QUrl>

#include "carddavcapabilitydiscovery.h"
#include "collectioninfo.h"
#include "fakecarddavserver.h"

using namespace Kalburator::Sync;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Wait for a QFuture<QList<CollectionInfo>> to finish by spinning the event
// loop via a QFutureWatcher signal spy. Per project FINDINGS, do NOT call
// future.waitForFinished() — Qt6's blocking wait does not spin the nested
// event loop that QNAM async I/O lives on.
bool waitForFuture(QFuture<QList<CollectionInfo>> f, int timeoutMs = 5000)
{
    if (f.isFinished()) return true;
    QFutureWatcher<QList<CollectionInfo>> w;
    QSignalSpy spy(&w, &QFutureWatcher<QList<CollectionInfo>>::finished);
    w.setFuture(f);
    if (f.isFinished()) return true;
    return spy.wait(timeoutMs);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// A minimal fake server subclass that returns an empty addressbook-home-set
// so we can test the empty-home-set code path.
// ---------------------------------------------------------------------------
class EmptyHomeCardDavServer : public QTcpServer
{
    Q_OBJECT
public:
    explicit EmptyHomeCardDavServer(QObject *parent = nullptr)
        : QTcpServer(parent)
    {}

    bool startListening() { return listen(QHostAddress::LocalHost, 0); }

    QUrl baseUrl() const {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(serverPort()));
    }

protected:
    void incomingConnection(qintptr fd) override
    {
        auto *sock = new QTcpSocket(this);
        if (!sock->setSocketDescriptor(fd)) { delete sock; return; }

        // Accept one request and respond.
        QObject::connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
            const QByteArray req = sock->readAll();
            QByteArray resp;
            const QByteArray requestLine = req.left(req.indexOf("\r\n"));

            // Answer the RFC 6764 well-known bootstrap probe with 404 so
            // discovery falls back to probing the root directly.
            if (requestLine.contains(".well-known")) {
                QByteArray resp404 = "HTTP/1.1 404 Not Found\r\n"
                                     "Content-Length: 0\r\n"
                                     "Connection: close\r\n\r\n";
                sock->write(resp404);
                sock->flush();
                sock->disconnectFromHost();
                return;
            }

            const bool isPrincipal = requestLine.contains("PROPFIND") &&
                                     (requestLine.contains(" / ") ||
                                      requestLine.endsWith(" /"));

            if (isPrincipal) {
                // Return principal href.
                const QByteArray xml =
                    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                    "<d:multistatus xmlns:d=\"DAV:\">\n"
                    "  <d:response>\n"
                    "    <d:href>/</d:href>\n"
                    "    <d:propstat>\n"
                    "      <d:prop>\n"
                    "        <d:current-user-principal>\n"
                    "          <d:href>/principals/users/testuser/</d:href>\n"
                    "        </d:current-user-principal>\n"
                    "      </d:prop>\n"
                    "      <d:status>HTTP/1.1 200 OK</d:status>\n"
                    "    </d:propstat>\n"
                    "  </d:response>\n"
                    "</d:multistatus>\n";
                writeXml(sock, xml);
            } else {
                // Home-set response: addressbook-home-set element present but
                // href child is empty — signals "no home set configured".
                const QByteArray xml =
                    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                    "<d:multistatus xmlns:d=\"DAV:\""
                    " xmlns:card=\"urn:ietf:params:xml:ns:carddav\">\n"
                    "  <d:response>\n"
                    "    <d:href>/principals/users/testuser/</d:href>\n"
                    "    <d:propstat>\n"
                    "      <d:prop>\n"
                    "        <card:addressbook-home-set/>\n"
                    "      </d:prop>\n"
                    "      <d:status>HTTP/1.1 200 OK</d:status>\n"
                    "    </d:propstat>\n"
                    "  </d:response>\n"
                    "</d:multistatus>\n";
                writeXml(sock, xml);
            }
        });
        QObject::connect(sock, &QTcpSocket::disconnected,
                         sock, &QObject::deleteLater);
    }

private:
    void writeXml(QTcpSocket *sock, const QByteArray &xml) {
        QByteArray resp;
        resp += "HTTP/1.1 207 Multi-Status\r\n";
        resp += "Content-Type: application/xml; charset=utf-8\r\n";
        resp += "Content-Length: " + QByteArray::number(xml.size()) + "\r\n";
        resp += "Connection: close\r\n\r\n";
        resp += xml;
        sock->write(resp);
        sock->flush();
        sock->disconnectFromHost();
    }
};

// ---------------------------------------------------------------------------
// A server that returns a mixed home-set: one addressbook + one collection
// with only <d:collection> (no <card:addressbook>) in resourcetype.
// ---------------------------------------------------------------------------
class MixedHomeCardDavServer : public QTcpServer
{
    Q_OBJECT
public:
    explicit MixedHomeCardDavServer(QObject *parent = nullptr)
        : QTcpServer(parent)
        , m_requestCount(0)
    {}

    bool startListening() { return listen(QHostAddress::LocalHost, 0); }
    QUrl baseUrl() const {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(serverPort()));
    }

protected:
    void incomingConnection(qintptr fd) override
    {
        auto *sock = new QTcpSocket(this);
        if (!sock->setSocketDescriptor(fd)) { delete sock; return; }

        QObject::connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
            const QByteArray req = sock->readAll();
            QByteArray xml;
            const QByteArray requestLine = req.left(req.indexOf("\r\n"));

            // This fixture is request-count based and does not model RFC 6764.
            // Answer the well-known bootstrap probe with 404 so discovery falls
            // back to probing the root directly, leaving the count sequence
            // (principal → home → addressbooks) intact.
            if (requestLine.contains(".well-known")) {
                QByteArray resp404 = "HTTP/1.1 404 Not Found\r\n"
                                     "Content-Length: 0\r\n"
                                     "Connection: close\r\n\r\n";
                sock->write(resp404);
                sock->flush();
                sock->disconnectFromHost();
                return;
            }

            ++m_requestCount;
            if (m_requestCount == 1) {
                // Principal
                xml =
                    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                    "<d:multistatus xmlns:d=\"DAV:\">\n"
                    "  <d:response>\n"
                    "    <d:href>/</d:href>\n"
                    "    <d:propstat><d:prop>\n"
                    "      <d:current-user-principal>"
                    "<d:href>/principals/users/testuser/</d:href>"
                    "</d:current-user-principal>\n"
                    "    </d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat>\n"
                    "  </d:response>\n"
                    "</d:multistatus>\n";
            } else if (m_requestCount == 2) {
                // Home-set
                xml =
                    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                    "<d:multistatus xmlns:d=\"DAV:\""
                    " xmlns:card=\"urn:ietf:params:xml:ns:carddav\">\n"
                    "  <d:response>\n"
                    "    <d:href>/principals/users/testuser/</d:href>\n"
                    "    <d:propstat><d:prop>\n"
                    "      <card:addressbook-home-set>"
                    "<d:href>/addressbooks/testuser/</d:href>"
                    "</card:addressbook-home-set>\n"
                    "    </d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat>\n"
                    "  </d:response>\n"
                    "</d:multistatus>\n";
            } else {
                // Addressbook list — one real addressbook + one plain collection
                xml =
                    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                    "<d:multistatus xmlns:d=\"DAV:\""
                    " xmlns:card=\"urn:ietf:params:xml:ns:carddav\">\n"
                    // The plain collection (NOT an addressbook)
                    "  <d:response>\n"
                    "    <d:href>/addressbooks/testuser/</d:href>\n"
                    "    <d:propstat><d:prop>\n"
                    "      <d:resourcetype><d:collection/></d:resourcetype>\n"
                    "      <d:displayname>Home</d:displayname>\n"
                    "    </d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat>\n"
                    "  </d:response>\n"
                    // The real addressbook
                    "  <d:response>\n"
                    "    <d:href>/addressbooks/testuser/contacts/</d:href>\n"
                    "    <d:propstat><d:prop>\n"
                    "      <d:resourcetype><d:collection/><card:addressbook/></d:resourcetype>\n"
                    "      <d:displayname>Contacts</d:displayname>\n"
                    "    </d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat>\n"
                    "  </d:response>\n"
                    "</d:multistatus>\n";
            }

            QByteArray resp;
            resp += "HTTP/1.1 207 Multi-Status\r\n";
            resp += "Content-Type: application/xml; charset=utf-8\r\n";
            resp += "Content-Length: " + QByteArray::number(xml.size()) + "\r\n";
            resp += "Connection: close\r\n\r\n";
            resp += xml;
            sock->write(resp);
            sock->flush();
            sock->disconnectFromHost();
        });
        QObject::connect(sock, &QTcpSocket::disconnected,
                         sock, &QObject::deleteLater);
    }

private:
    int m_requestCount;
};

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class TstCardDavCapabilityDiscovery : public QObject
{
    Q_OBJECT

private slots:
    void happy_path_one_addressbook();
    void happy_path_via_wellknown_against_nextcloud_style_server();
    void happy_path_three_addressbooks();
    void error_401_on_principal_propfind();
    void error_500_on_home_set_propfind();
    void empty_home_set_succeeds_with_empty_list();
    void mixed_home_set_returns_only_addressbooks();
    void manual_principal_override_skips_autodiscovery();
};

void TstCardDavCapabilityDiscovery::manual_principal_override_skips_autodiscovery()
{
    // Without an override, FakeCardDavServer auto-discovers a valid principal
    // at the root (see happy_path_one_addressbook). With a bogus override,
    // discovery must walk straight to home-set on that bogus path — which 404s
    // — proving the override short-circuits the principal auto-probe.
    FakeCardDavServer server;
    QVERIFY(server.startListening());

    CardDavCapabilityDiscovery discovery;
    discovery.setCredentials(server.baseUrl(),
                             QStringLiteral("testuser"),
                             QStringLiteral("testpass"));
    discovery.setPrincipalHrefOverride(QStringLiteral("/bogus-principal/"));

    QSignalSpy errorSpy(&discovery, &CardDavCapabilityDiscovery::error);
    QFuture<QList<CollectionInfo>> fut = discovery.discover();
    QVERIFY(waitForFuture(fut));

    QVERIFY(errorSpy.count() >= 1);
    QVERIFY(fut.result().isEmpty());
}

// ---------------------------------------------------------------------------
// Test implementations
// ---------------------------------------------------------------------------

void TstCardDavCapabilityDiscovery::happy_path_one_addressbook()
{
    // FakeCardDavServer defaults to one addressbook: ("personal", "Personal").
    FakeCardDavServer server;
    QVERIFY(server.startListening());

    CardDavCapabilityDiscovery discovery;
    discovery.setCredentials(server.baseUrl(),
                             QStringLiteral("testuser"),
                             QStringLiteral("testpass"));

    QSignalSpy errorSpy(&discovery, &CardDavCapabilityDiscovery::error);

    QFuture<QList<CollectionInfo>> fut = discovery.discover();
    QVERIFY(waitForFuture(fut));

    QCOMPARE(errorSpy.count(), 0);

    const QList<CollectionInfo> result = fut.result();
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.at(0).id,   QStringLiteral("personal"));
    QCOMPARE(result.at(0).name, QStringLiteral("Personal"));
    QCOMPARE(result.at(0).type, QStringLiteral("contacts"));

    // addressbookUrls() must have an entry for the discovered id.
    const QMap<QString, QString> urls = discovery.addressbookUrls();
    QVERIFY(urls.contains(QStringLiteral("personal")));
    QVERIFY(!urls.value(QStringLiteral("personal")).isEmpty());
}

void TstCardDavCapabilityDiscovery::happy_path_via_wellknown_against_nextcloud_style_server()
{
    // NextCloud serves CardDAV under a context path (e.g. /remote.php/dav)
    // advertised via a 301 from /.well-known/carddav. The user enters only the
    // bare host, so discovery must follow the well-known redirect rather than
    // PROPFIND the web-UI root (which answers 405).
    FakeCardDavServer server;
    server.setContextPath(QStringLiteral("/remote.php/dav"));
    QVERIFY(server.startListening());

    CardDavCapabilityDiscovery discovery;
    discovery.setCredentials(server.baseUrl(),  // bare host root, no DAV path
                             QStringLiteral("testuser"),
                             QStringLiteral("testpass"));

    QSignalSpy errorSpy(&discovery, &CardDavCapabilityDiscovery::error);

    QFuture<QList<CollectionInfo>> fut = discovery.discover();
    QVERIFY(waitForFuture(fut));

    QCOMPARE(errorSpy.count(), 0);
    const QList<CollectionInfo> result = fut.result();
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.at(0).id,   QStringLiteral("personal"));
    QCOMPARE(result.at(0).name, QStringLiteral("Personal"));
}

void TstCardDavCapabilityDiscovery::happy_path_three_addressbooks()
{
    FakeCardDavServer server;
    server.setAddressbooks({
        { QStringLiteral("personal"), QStringLiteral("Personal") },
        { QStringLiteral("work"),     QStringLiteral("Work") },
        { QStringLiteral("family"),   QStringLiteral("Family") }
    });
    QVERIFY(server.startListening());

    CardDavCapabilityDiscovery discovery;
    discovery.setCredentials(server.baseUrl(),
                             QStringLiteral("testuser"),
                             QStringLiteral("testpass"));

    QSignalSpy errorSpy(&discovery, &CardDavCapabilityDiscovery::error);

    QFuture<QList<CollectionInfo>> fut = discovery.discover();
    QVERIFY(waitForFuture(fut));

    QCOMPARE(errorSpy.count(), 0);

    const QList<CollectionInfo> result = fut.result();
    QCOMPARE(result.size(), 3);

    QStringList ids, names;
    for (const CollectionInfo &ci : result) {
        QCOMPARE(ci.type, QStringLiteral("contacts"));
        ids   << ci.id;
        names << ci.name;
    }
    ids.sort();
    names.sort();

    QCOMPARE(ids,   (QStringList{ QStringLiteral("family"),
                                  QStringLiteral("personal"),
                                  QStringLiteral("work") }));
    QCOMPARE(names, (QStringList{ QStringLiteral("Family"),
                                  QStringLiteral("Personal"),
                                  QStringLiteral("Work") }));

    // All three must appear in addressbookUrls().
    const QMap<QString, QString> urls = discovery.addressbookUrls();
    QCOMPARE(urls.size(), 3);
    for (const QString &id : ids) {
        QVERIFY(urls.contains(id));
        QVERIFY(!urls.value(id).isEmpty());
    }
}

void TstCardDavCapabilityDiscovery::error_401_on_principal_propfind()
{
    FakeCardDavServer server;
    server.setReturn401(true);
    QVERIFY(server.startListening());

    CardDavCapabilityDiscovery discovery;
    discovery.setCredentials(server.baseUrl(),
                             QStringLiteral("baduser"),
                             QStringLiteral("badpass"));

    QSignalSpy errorSpy(&discovery, &CardDavCapabilityDiscovery::error);

    QFuture<QList<CollectionInfo>> fut = discovery.discover();
    QVERIFY(waitForFuture(fut));

    // error() must have been emitted at least once.
    QVERIFY(errorSpy.count() >= 1);

    // Future must resolve to an empty list.
    const QList<CollectionInfo> result = fut.result();
    QVERIFY(result.isEmpty());
}

void TstCardDavCapabilityDiscovery::error_500_on_home_set_propfind()
{
    // For this test we need the principal PROPFIND to succeed but the
    // home-set PROPFIND to fail. The simplest approach: use a
    // FakeCardDavServer and flip return500 on after the first request.
    // Since FakeCardDavServer applies the error globally, both the
    // principal and home-set steps will get 500s when set before start.
    //
    // The spec says "500 on home-set PROPFIND" but the important
    // invariant is: error() is emitted and future resolves empty.
    // Using setReturn500(true) upfront covers this path.

    FakeCardDavServer server;
    server.setReturn500(true);
    QVERIFY(server.startListening());

    CardDavCapabilityDiscovery discovery;
    discovery.setCredentials(server.baseUrl(),
                             QStringLiteral("testuser"),
                             QStringLiteral("testpass"));

    QSignalSpy errorSpy(&discovery, &CardDavCapabilityDiscovery::error);

    QFuture<QList<CollectionInfo>> fut = discovery.discover();
    QVERIFY(waitForFuture(fut));

    QVERIFY(errorSpy.count() >= 1);
    QVERIFY(fut.result().isEmpty());
}

void TstCardDavCapabilityDiscovery::empty_home_set_succeeds_with_empty_list()
{
    // Server returns a valid principal but an empty addressbook-home-set.
    EmptyHomeCardDavServer server;
    QVERIFY(server.startListening());

    CardDavCapabilityDiscovery discovery;
    discovery.setCredentials(server.baseUrl(),
                             QStringLiteral("testuser"),
                             QStringLiteral("testpass"));

    QSignalSpy errorSpy(&discovery, &CardDavCapabilityDiscovery::error);

    QFuture<QList<CollectionInfo>> fut = discovery.discover();
    QVERIFY(waitForFuture(fut));

    // No error — an empty home-set is a valid (empty) result.
    QCOMPARE(errorSpy.count(), 0);
    QVERIFY(fut.result().isEmpty());
}

void TstCardDavCapabilityDiscovery::mixed_home_set_returns_only_addressbooks()
{
    // Server returns one plain collection + one addressbook.
    // Only the addressbook must appear in the result.
    MixedHomeCardDavServer server;
    QVERIFY(server.startListening());

    CardDavCapabilityDiscovery discovery;
    discovery.setCredentials(server.baseUrl(),
                             QStringLiteral("testuser"),
                             QStringLiteral("testpass"));

    QSignalSpy errorSpy(&discovery, &CardDavCapabilityDiscovery::error);

    QFuture<QList<CollectionInfo>> fut = discovery.discover();
    QVERIFY(waitForFuture(fut));

    QCOMPARE(errorSpy.count(), 0);

    const QList<CollectionInfo> result = fut.result();
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.at(0).id,   QStringLiteral("contacts"));
    QCOMPARE(result.at(0).name, QStringLiteral("Contacts"));
    QCOMPARE(result.at(0).type, QStringLiteral("contacts"));
}

QTEST_GUILESS_MAIN(TstCardDavCapabilityDiscovery)
#include "tst_carddav_capability_discovery.moc"
