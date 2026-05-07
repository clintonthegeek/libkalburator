// Phase H Task 6 — CalDavProvider tests against a fake CalDAV server.
//
// These tests exercise CalDavProvider end-to-end without any external
// server: a QTcpServer-based fixture (FakeCalDavServer) handles the
// three PROPFIND requests CalDavCapabilityDiscovery walks and provides
// configurable failure modes for negative paths.

#include <QtTest/QtTest>
#include <QFutureWatcher>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QUrl>

#include "fakecaldavserver.h"

#include "backendconfiguration.h"
#include "caldavprovider.h"
#include "collectioninfo.h"
#include "iblobbackend.h"
#include "remotebackend.h"

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
    cfg.type = QStringLiteral("caldav");
    cfg.displayName = QStringLiteral("Fake Account");
    cfg.connectionParams.insert(QStringLiteral("url"), serverUrl.toString());
    cfg.connectionParams.insert(QStringLiteral("username"), username);
    cfg.connectionParams.insert(QStringLiteral("password"), password);
    return cfg;
}

} // namespace

class TstCalDavProvider : public QObject
{
    Q_OBJECT
private slots:
    void connect_succeeds_against_fake_server();
    void connect_populates_collections();
    void createBackend_returns_remote_backend_for_known_collection();
    void createBackend_returns_nullptr_for_unknown_collection();
    void connect_fails_on_401();
    void connect_fails_on_500();
    void connect_fails_on_unreachable_server();
    void disconnect_clears_collections();
};

void TstCalDavProvider::connect_succeeds_against_fake_server()
{
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    CalDavProvider provider;
    provider.load(makeConfig(server.baseUrl()));

    QSignalSpy stateSpy(&provider, &IProvider::connectionStateChanged);
    QFuture<bool> fut = provider.connect();
    QVERIFY(waitForFutureBool(fut));
    QCOMPARE(fut.result(), true);
    QVERIFY(provider.isConnected());
    QCOMPARE(stateSpy.count(), 1);
    QCOMPARE(stateSpy.first().at(0).toBool(), true);
}

void TstCalDavProvider::connect_populates_collections()
{
    FakeCalDavServer server;
    server.setCalendars({
        { QStringLiteral("Personal"), QStringLiteral("/calendars/testuser/personal/") },
        { QStringLiteral("Work"),     QStringLiteral("/calendars/testuser/work/") }
    });
    QVERIFY(server.startListening());

    CalDavProvider provider;
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
        QCOMPARE(c.type, QStringLiteral("calendar"));
        names << c.name;
    }
    std::sort(names.begin(), names.end());
    QCOMPARE(names, (QStringList{ QStringLiteral("Personal"), QStringLiteral("Work") }));
}

void TstCalDavProvider::createBackend_returns_remote_backend_for_known_collection()
{
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    CalDavProvider provider;
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
    // dynamic type is RemoteBackend (which inherits SyncBackend which
    // inherits IBlobBackend).
    auto *remote = dynamic_cast<RemoteBackend*>(backend.get());
    QVERIFY(remote != nullptr);
}

void TstCalDavProvider::createBackend_returns_nullptr_for_unknown_collection()
{
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    CalDavProvider provider;
    provider.load(makeConfig(server.baseUrl()));

    QFuture<bool> fut = provider.connect();
    QVERIFY(waitForFutureBool(fut));
    QCOMPARE(fut.result(), true);

    auto backend = provider.createBackend(QStringLiteral("not-a-collection"));
    QVERIFY(backend == nullptr);
}

void TstCalDavProvider::connect_fails_on_401()
{
    FakeCalDavServer server;
    server.setReturn401(true);
    QVERIFY(server.startListening());

    CalDavProvider provider;
    provider.load(makeConfig(server.baseUrl()));

    QSignalSpy errSpy(&provider, &IProvider::error);
    QFuture<bool> fut = provider.connect();
    QVERIFY(waitForFutureBool(fut));
    QCOMPARE(fut.result(), false);
    QVERIFY(!provider.isConnected());
    QVERIFY(errSpy.count() >= 1);
    QVERIFY(!errSpy.first().at(0).toString().isEmpty());
}

void TstCalDavProvider::connect_fails_on_500()
{
    FakeCalDavServer server;
    server.setReturn500(true);
    QVERIFY(server.startListening());

    CalDavProvider provider;
    provider.load(makeConfig(server.baseUrl()));

    QSignalSpy errSpy(&provider, &IProvider::error);
    QFuture<bool> fut = provider.connect();
    QVERIFY(waitForFutureBool(fut));
    QCOMPARE(fut.result(), false);
    QVERIFY(!provider.isConnected());
    QVERIFY(errSpy.count() >= 1);
    QVERIFY(!errSpy.first().at(0).toString().isEmpty());
}

void TstCalDavProvider::connect_fails_on_unreachable_server()
{
    // Bind a server, capture its port, then immediately stop listening.
    // The freshly-released port is unlikely to be re-bound by any other
    // process during the test, so connect() should refuse.
    quint16 port = 0;
    {
        FakeCalDavServer probe;
        QVERIFY(probe.startListening());
        port = probe.serverPort();
        probe.close();
    }
    QVERIFY(port != 0);

    const QUrl deadUrl(QStringLiteral("http://127.0.0.1:%1/").arg(port));

    CalDavProvider provider;
    provider.load(makeConfig(deadUrl));

    QSignalSpy errSpy(&provider, &IProvider::error);
    QFuture<bool> fut = provider.connect();
    QVERIFY(waitForFutureBool(fut, 10000));
    QCOMPARE(fut.result(), false);
    QVERIFY(!provider.isConnected());
    QVERIFY(errSpy.count() >= 1);
    QVERIFY(!errSpy.first().at(0).toString().isEmpty());
}

void TstCalDavProvider::disconnect_clears_collections()
{
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    CalDavProvider provider;
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

QTEST_GUILESS_MAIN(TstCalDavProvider)
#include "tst_caldav_provider.moc"
