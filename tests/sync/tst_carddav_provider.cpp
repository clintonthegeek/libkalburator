// Phase Ib Task 8 — CardDavProvider tests against a fake CardDAV server.
//
// These tests exercise CardDavProvider end-to-end without any external
// server: a QTcpServer-based fixture (FakeCardDavServer) handles the
// three PROPFIND requests CardDavCapabilityDiscovery walks and provides
// configurable failure modes for negative paths.

#include <QtTest/QtTest>
#include <QFutureWatcher>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTcpServer>
#include <QUrl>

#include "fakecarddavserver.h"

#include "backendconfiguration.h"
#include "carddavprovider.h"
#include "collectioninfo.h"
#include "iblobbackend.h"
#include "remotecontactsbackend.h"

using namespace Kalburator::Sync;

namespace {

// Wait for a QFuture<bool> to finish by spinning the event loop via a
// QFutureWatcher signal spy. Per the project FINDINGS, do NOT call
// future.waitForFinished() — Qt6's blocking wait does not spin the
// nested event loop the QNAM async I/O lives on.
bool waitForFutureBool(QFuture<bool> f, int timeoutMs = 5000)
{
    if (f.isFinished()) return true;
    QFutureWatcher<bool> w;
    QSignalSpy doneSpy(&w, &QFutureWatcher<bool>::finished);
    w.setFuture(f);
    if (f.isFinished()) return true;
    return doneSpy.wait(timeoutMs);
}

BackendConfiguration makeConfig(const QUrl &serverUrl,
                                const QString &username = QStringLiteral("testuser"),
                                const QString &password = QStringLiteral("testpass"))
{
    BackendConfiguration cfg;
    cfg.id = QStringLiteral("test-account");
    cfg.type = QStringLiteral("carddav");
    cfg.displayName = QStringLiteral("Fake Account");
    cfg.connectionParams.insert(QStringLiteral("url"), serverUrl.toString());
    cfg.connectionParams.insert(QStringLiteral("username"), username);
    cfg.connectionParams.insert(QStringLiteral("password"), password);
    return cfg;
}

} // namespace

class TstCardDavProvider : public QObject
{
    Q_OBJECT
private slots:
    // Test 1: load() populates URL/username/password (verified via save round-trip)
    void load_populates_credentials();
    // Test 2: connect() runs discovery; emits connectionStateChanged(true)
    void connect_succeeds_against_fake_server();
    // Test 3: collections() returns discovered list after connect
    void connect_populates_collections();

    // Collection rows advertise the VCARD content type (WildPalms RFC
    // 2026-06-09 symmetry: CalDAV rows carry VEVENT/VTODO, CardDAV VCARD).
    void connect_populates_vcard_contenttype();
    // Test 4: createBackend returns non-null RemoteContactsBackend configured with correct URL
    void createBackend_returns_remote_backend_for_known_collection();
    // Additional: createBackend returns nullptr for unknown collection
    void createBackend_returns_nullptr_for_unknown_collection();
    // Test 5: disconnect() flips state, emits connectionStateChanged(false)
    void disconnect_clears_collections();
    // Test 6: save() round-trips through load() losslessly
    void save_roundtrips_through_load();
    // Test 7: 401 → error() emitted, connect() future resolves false
    void connect_fails_on_401();
    void connect_fails_on_500();
    void connect_fails_on_unreachable_server();
    // Test 8: kind() == "carddav"
    void kind_is_carddav();
    // createConfigWidget() now returns a real CardDavConfigWidget; that path is
    // covered (needs a GUI QApplication) by tst_carddav_config_widget, so it is
    // not tested here in this QTEST_GUILESS_MAIN target.
    // Additional edge cases mirrored from tst_caldav_provider
    void connect_when_already_connected_is_noop();
    void connect_with_empty_url_emits_error_immediately();
    void connect_with_invalid_url_emits_error_immediately();
    void disconnect_mid_flight_resolves_promise_false();
    void createBackend_when_not_connected_returns_nullptr();
    void createBackend_after_disconnect_returns_nullptr();
};

// --- Test 1 ------------------------------------------------------------------

void TstCardDavProvider::load_populates_credentials()
{
    FakeCardDavServer server;
    QVERIFY(server.startListening());

    CardDavProvider provider;

    BackendConfiguration cfg = makeConfig(server.baseUrl());
    provider.load(cfg);

    // Verify via save() that load() stored the values.
    BackendConfiguration saved = provider.save();
    QCOMPARE(saved.id, cfg.id);
    QCOMPARE(saved.connectionParams.value(QStringLiteral("url")).toString(),
             server.baseUrl().toString());
    QCOMPARE(saved.connectionParams.value(QStringLiteral("username")).toString(),
             QStringLiteral("testuser"));
    QCOMPARE(saved.connectionParams.value(QStringLiteral("password")).toString(),
             QStringLiteral("testpass"));
}

// --- Test 2 ------------------------------------------------------------------

void TstCardDavProvider::connect_succeeds_against_fake_server()
{
    FakeCardDavServer server;
    QVERIFY(server.startListening());

    CardDavProvider provider;
    provider.load(makeConfig(server.baseUrl()));

    QSignalSpy stateSpy(&provider, &IProvider::connectionStateChanged);
    QFuture<bool> fut = provider.connect();
    QVERIFY(waitForFutureBool(fut));
    QCOMPARE(fut.result(), true);
    QVERIFY(provider.isConnected());
    QCOMPARE(stateSpy.count(), 1);
    QCOMPARE(stateSpy.first().at(0).toBool(), true);
}

// --- Test 3 ------------------------------------------------------------------

void TstCardDavProvider::connect_populates_collections()
{
    FakeCardDavServer server;
    server.setAddressbooks({
        { QStringLiteral("personal"), QStringLiteral("Personal") },
        { QStringLiteral("work"),     QStringLiteral("Work") }
    });
    QVERIFY(server.startListening());

    CardDavProvider provider;
    provider.load(makeConfig(server.baseUrl()));

    QSignalSpy collSpy(&provider, &IProvider::collectionsChanged);
    QFuture<bool> fut = provider.connect();
    QVERIFY(waitForFutureBool(fut));
    QCOMPARE(fut.result(), true);

    const auto cols = provider.collections();
    QCOMPARE(cols.size(), 2);
    QCOMPARE(collSpy.count(), 1);

    QStringList names;
    for (const auto &c : cols) {
        QVERIFY(!c.id.isEmpty());
        QCOMPARE(c.type, QStringLiteral("contacts"));
        names << c.name;
    }
    std::sort(names.begin(), names.end());
    QCOMPARE(names, (QStringList{ QStringLiteral("Personal"), QStringLiteral("Work") }));
}

void TstCardDavProvider::connect_populates_vcard_contenttype()
{
    FakeCardDavServer server;
    server.setAddressbooks({
        { QStringLiteral("personal"), QStringLiteral("Personal") }
    });
    QVERIFY(server.startListening());

    CardDavProvider provider;
    provider.load(makeConfig(server.baseUrl()));

    QFuture<bool> fut = provider.connect();
    QVERIFY(waitForFutureBool(fut));
    QCOMPARE(fut.result(), true);

    const auto cols = provider.collections();
    QCOMPARE(cols.size(), 1);
    QCOMPARE(cols.first().contentTypes,
             (QStringList{ QStringLiteral("VCARD") }));
}

// --- Test 4 ------------------------------------------------------------------

void TstCardDavProvider::createBackend_returns_remote_backend_for_known_collection()
{
    FakeCardDavServer server;
    QVERIFY(server.startListening());

    CardDavProvider provider;
    provider.load(makeConfig(server.baseUrl()));

    QFuture<bool> fut = provider.connect();
    QVERIFY(waitForFutureBool(fut));
    QCOMPARE(fut.result(), true);

    const auto cols = provider.collections();
    QVERIFY(!cols.isEmpty());
    const QString collId = cols.first().id;

    std::unique_ptr<IBlobBackend> backend = provider.createBackend(collId);
    QVERIFY(backend != nullptr);

    // The unique_ptr<IBlobBackend> upcast must yield an instance whose
    // dynamic type is RemoteContactsBackend (which inherits SyncBackend which
    // inherits IBlobBackend).
    auto *remote = dynamic_cast<RemoteContactsBackend*>(backend.get());
    QVERIFY(remote != nullptr);
}

void TstCardDavProvider::createBackend_returns_nullptr_for_unknown_collection()
{
    FakeCardDavServer server;
    QVERIFY(server.startListening());

    CardDavProvider provider;
    provider.load(makeConfig(server.baseUrl()));

    QFuture<bool> fut = provider.connect();
    QVERIFY(waitForFutureBool(fut));
    QCOMPARE(fut.result(), true);

    auto backend = provider.createBackend(QStringLiteral("not-a-collection"));
    QVERIFY(backend == nullptr);
}

// --- Test 5 ------------------------------------------------------------------

void TstCardDavProvider::disconnect_clears_collections()
{
    FakeCardDavServer server;
    QVERIFY(server.startListening());

    CardDavProvider provider;
    provider.load(makeConfig(server.baseUrl()));

    QFuture<bool> fut = provider.connect();
    QVERIFY(waitForFutureBool(fut));
    QCOMPARE(fut.result(), true);
    QVERIFY(!provider.collections().isEmpty());

    QSignalSpy stateSpy(&provider, &IProvider::connectionStateChanged);
    provider.disconnect();
    QVERIFY(provider.collections().isEmpty());
    QVERIFY(!provider.isConnected());
    QCOMPARE(stateSpy.count(), 1);
    QCOMPARE(stateSpy.first().at(0).toBool(), false);
}

// --- Test 6 ------------------------------------------------------------------

void TstCardDavProvider::save_roundtrips_through_load()
{
    CardDavProvider provider;

    BackendConfiguration original;
    original.id = QStringLiteral("my-uuid-1234");
    original.type = QStringLiteral("carddav");
    original.displayName = QStringLiteral("Work Account");
    original.connectionParams.insert(QStringLiteral("url"),
                                     QStringLiteral("https://carddav.example.com/"));
    original.connectionParams.insert(QStringLiteral("username"),
                                     QStringLiteral("alice"));
    original.connectionParams.insert(QStringLiteral("password"),
                                     QStringLiteral("s3cr3t"));

    provider.load(original);
    BackendConfiguration roundtripped = provider.save();

    QCOMPARE(roundtripped.id, original.id);
    QCOMPARE(roundtripped.type, QStringLiteral("carddav"));
    QCOMPARE(roundtripped.displayName, original.displayName);
    QCOMPARE(roundtripped.connectionParams.value(QStringLiteral("url")).toString(),
             original.connectionParams.value(QStringLiteral("url")).toString());
    QCOMPARE(roundtripped.connectionParams.value(QStringLiteral("username")).toString(),
             original.connectionParams.value(QStringLiteral("username")).toString());
    QCOMPARE(roundtripped.connectionParams.value(QStringLiteral("password")).toString(),
             original.connectionParams.value(QStringLiteral("password")).toString());
}

// --- Test 7 ------------------------------------------------------------------

void TstCardDavProvider::connect_fails_on_401()
{
    FakeCardDavServer server;
    server.setReturn401(true);
    QVERIFY(server.startListening());

    CardDavProvider provider;
    provider.load(makeConfig(server.baseUrl()));

    QSignalSpy errSpy(&provider, &IProvider::error);
    QFuture<bool> fut = provider.connect();
    QVERIFY(waitForFutureBool(fut));
    QCOMPARE(fut.result(), false);
    QVERIFY(!provider.isConnected());
    QVERIFY(errSpy.count() >= 1);
    QVERIFY(!errSpy.first().at(0).toString().isEmpty());
}

void TstCardDavProvider::connect_fails_on_500()
{
    FakeCardDavServer server;
    server.setReturn500(true);
    QVERIFY(server.startListening());

    CardDavProvider provider;
    provider.load(makeConfig(server.baseUrl()));

    QSignalSpy errSpy(&provider, &IProvider::error);
    QFuture<bool> fut = provider.connect();
    QVERIFY(waitForFutureBool(fut));
    QCOMPARE(fut.result(), false);
    QVERIFY(!provider.isConnected());
    QVERIFY(errSpy.count() >= 1);
    QVERIFY(!errSpy.first().at(0).toString().isEmpty());
}

void TstCardDavProvider::connect_fails_on_unreachable_server()
{
    // Bind a server, capture its port, then immediately stop listening.
    // The freshly-released port is unlikely to be re-bound by any other
    // process during the test, so connect() should refuse.
    quint16 port = 0;
    {
        FakeCardDavServer probe;
        QVERIFY(probe.startListening());
        port = probe.serverPort();
        probe.close();
    }
    QVERIFY(port != 0);

    const QUrl deadUrl(QStringLiteral("http://127.0.0.1:%1/").arg(port));

    CardDavProvider provider;
    provider.load(makeConfig(deadUrl));

    QSignalSpy errSpy(&provider, &IProvider::error);
    QFuture<bool> fut = provider.connect();
    QVERIFY(waitForFutureBool(fut, 10000));
    QCOMPARE(fut.result(), false);
    QVERIFY(!provider.isConnected());
    QVERIFY(errSpy.count() >= 1);
    QVERIFY(!errSpy.first().at(0).toString().isEmpty());
}

// --- Test 8 ------------------------------------------------------------------

void TstCardDavProvider::kind_is_carddav()
{
    CardDavProvider provider;
    QCOMPARE(provider.kind(), QStringLiteral("carddav"));
}

// --- Edge cases (mirrored from tst_caldav_provider) --------------------------

void TstCardDavProvider::connect_when_already_connected_is_noop()
{
    FakeCardDavServer server;
    QVERIFY(server.startListening());

    CardDavProvider provider;
    provider.load(makeConfig(server.baseUrl()));

    QVERIFY(waitForFutureBool(provider.connect()));
    QVERIFY(provider.isConnected());

    QSignalSpy stateSpy(&provider, &IProvider::connectionStateChanged);
    QSignalSpy collSpy(&provider, &IProvider::collectionsChanged);

    QFuture<bool> fut2 = provider.connect();
    QVERIFY(waitForFutureBool(fut2));
    QCOMPARE(fut2.result(), true);
    QCOMPARE(stateSpy.count(), 0);
    QCOMPARE(collSpy.count(), 0);
    QVERIFY(provider.isConnected());
}

void TstCardDavProvider::connect_with_empty_url_emits_error_immediately()
{
    CardDavProvider provider;

    QSignalSpy errSpy(&provider, &IProvider::error);
    QFuture<bool> fut = provider.connect();

    QVERIFY(fut.isFinished());
    QCOMPARE(fut.result(), false);
    QVERIFY(!provider.isConnected());
    QCOMPARE(errSpy.count(), 1);
    QVERIFY(!errSpy.first().at(0).toString().isEmpty());
}

void TstCardDavProvider::connect_with_invalid_url_emits_error_immediately()
{
    CardDavProvider provider;
    BackendConfiguration cfg;
    cfg.id = QStringLiteral("test");
    cfg.connectionParams.insert(QStringLiteral("url"),
                                QStringLiteral("not-a-url"));
    cfg.connectionParams.insert(QStringLiteral("username"), QStringLiteral("u"));
    cfg.connectionParams.insert(QStringLiteral("password"), QStringLiteral("p"));
    provider.load(cfg);

    QSignalSpy errSpy(&provider, &IProvider::error);
    QFuture<bool> fut = provider.connect();

    // Bug: CardDavProvider does not validate the URL scheme/format before
    // dispatching to QNAM. A relative/scheme-less string like "not-a-url"
    // is passed to QNAM which treats it as a (broken) relative URL and
    // starts an async network request instead of rejecting synchronously.
    // Only a truly empty URL is caught early. See FINDINGS.md.
    QEXPECT_FAIL("", "connect() with non-scheme URL is not synchronous (CardDavProvider does not pre-validate URL scheme)", Continue);
    QVERIFY(fut.isFinished());
    if (!fut.isFinished())
        return; // future is in-flight; remaining assertions would block — skip them
    QCOMPARE(fut.result(), false);
    QVERIFY(!provider.isConnected());
    QCOMPARE(errSpy.count(), 1);
}

void TstCardDavProvider::disconnect_mid_flight_resolves_promise_false()
{
    QTcpServer hungServer;
    QVERIFY(hungServer.listen(QHostAddress::LocalHost, 0));
    const QUrl hungUrl(
        QStringLiteral("http://127.0.0.1:%1/").arg(hungServer.serverPort()));

    CardDavProvider provider;
    provider.load(makeConfig(hungUrl));

    QFuture<bool> fut = provider.connect();
    QVERIFY(!fut.isFinished());

    QSignalSpy stateSpy(&provider, &IProvider::connectionStateChanged);

    provider.disconnect();

    QVERIFY(fut.isFinished());
    QCOMPARE(fut.result(), false);
    QVERIFY(!provider.isConnected());
    QCOMPARE(stateSpy.count(), 0);
}

void TstCardDavProvider::createBackend_when_not_connected_returns_nullptr()
{
    FakeCardDavServer server;
    QVERIFY(server.startListening());

    CardDavProvider provider;
    provider.load(makeConfig(server.baseUrl()));

    auto backend = provider.createBackend(QStringLiteral("any-id"));
    QVERIFY(backend == nullptr);
}

void TstCardDavProvider::createBackend_after_disconnect_returns_nullptr()
{
    FakeCardDavServer server;
    QVERIFY(server.startListening());

    CardDavProvider provider;
    provider.load(makeConfig(server.baseUrl()));

    QFuture<bool> fut = provider.connect();
    QVERIFY(waitForFutureBool(fut));
    QVERIFY(provider.isConnected());
    QVERIFY(!provider.collections().isEmpty());
    const QString collId = provider.collections().first().id;

    provider.disconnect();
    QVERIFY(!provider.isConnected());

    auto backend = provider.createBackend(collId);
    QVERIFY(backend == nullptr);
}

QTEST_GUILESS_MAIN(TstCardDavProvider)
#include "tst_carddav_provider.moc"
