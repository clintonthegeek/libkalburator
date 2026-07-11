// Phase H Task 6 — CalDavProvider tests against a fake CalDAV server.
//
// These tests exercise CalDavProvider end-to-end without any external
// server: a QTcpServer-based fixture (FakeCalDavServer) handles the
// three PROPFIND requests CalDavCapabilityDiscovery walks and provides
// configurable failure modes for negative paths.
//
// PHASE2-TASK2.1 — All slots below exercise the v1 createBackend() path.
// When Phase 2.2 / 2.3 lands (CardDAV and multi-protocol fanout + the
// manager-side spec-driven registration flip in Phase 2.4+), add tests
// for CalDavProvider::createBackends() covering:
//   * Returns one Calendar spec for a connected/known collection
//   * backendId = "<providerId>:<collectionId>:<stableSlug>"
//   * Slug derives from last path segment of the href (sanitised)
//   * {} for an empty / unknown / disconnected state

#include <QtTest/QtTest>
#include <QFutureWatcher>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTcpServer>
#include <QUrl>

#include "fakecaldavserver.h"

#include "backendconfiguration.h"
#include "caldavcapabilitydiscovery.h"
#include "caldavprovider.h"
#include "collectioninfo.h"
#include "iblobbackend.h"
#include "remotecalendarbackend.h"

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
    void connect_succeeds_via_wellknown_against_nextcloud_style_server();
    void connect_populates_collections();
    void connect_populates_readonly_from_discovered_writability();
    void connect_populates_contenttypes_from_component_sets();
    void createBackend_returns_remote_backend_for_known_collection();
    void createBackend_returns_nullptr_for_unknown_collection();
    void connect_fails_on_401();
    void connect_fails_on_500();
    void connect_fails_on_unreachable_server();
    void disconnect_clears_collections();
    void connect_when_already_connected_is_noop();
    void connect_with_empty_url_emits_error_immediately();
    void connect_with_invalid_url_emits_error_immediately();
    void disconnect_mid_flight_resolves_promise_false();
    void createBackend_when_not_connected_returns_nullptr();
    void createBackend_after_disconnect_returns_nullptr();
    void manual_principal_override_skips_autodiscovery();
    void connect_while_inflight_is_idempotent();
};

void TstCalDavProvider::manual_principal_override_skips_autodiscovery()
{
    // Without an override, discovery auto-finds a valid principal at the root
    // (connect_succeeds_against_fake_server). With a bogus override, discovery
    // walks straight to calendar-home-set on that path — which 404s — proving
    // the override short-circuits the principal auto-probe.
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    CalDavCapabilityDiscovery disc(server.baseUrl(),
                                   QStringLiteral("testuser"),
                                   QStringLiteral("testpass"));
    disc.setPrincipalUrlOverride(QStringLiteral("/bogus-principal/"));

    QSignalSpy finishedSpy(&disc, &CalDavCapabilityDiscovery::finished);
    disc.start();
    QVERIFY(finishedSpy.wait(5000));
    QCOMPARE(finishedSpy.first().at(0).toBool(), false);
}

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

void TstCalDavProvider::connect_succeeds_via_wellknown_against_nextcloud_style_server()
{
    // NextCloud (and any RFC 6764 server) serves DAV under a context path
    // such as /remote.php/dav and advertises it via a 301 from
    // /.well-known/caldav. The user naturally enters only the bare host they
    // log into, so discovery must follow the well-known redirect rather than
    // PROPFIND the web-UI root (which answers 405).
    FakeCalDavServer server;
    server.setContextPath(QStringLiteral("/remote.php/dav"));
    QVERIFY(server.startListening());

    CalDavProvider provider;
    provider.load(makeConfig(server.baseUrl()));  // bare host root, no DAV path

    QFuture<bool> fut = provider.connect();
    QVERIFY(waitForFutureBool(fut));
    QCOMPARE(fut.result(), true);
    QVERIFY(provider.isConnected());

    const auto cols = provider.collections();
    QCOMPARE(cols.size(), 1);
    QCOMPARE(cols.first().name, QStringLiteral("Personal"));
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

void TstCalDavProvider::connect_populates_readonly_from_discovered_writability()
{
    // A calendar whose server ACL advertises no write privilege must surface
    // as CollectionInfo.readOnly == true; a normal calendar stays writable.
    // (Phase 2C: the new-collection wizard excludes read-only remotes.)
    FakeCalDavServer server;
    server.setCalendars({
        { QStringLiteral("Personal"), QStringLiteral("/calendars/testuser/personal/") },
        { QStringLiteral("Shared"),   QStringLiteral("/calendars/testuser/shared/") }
    });
    server.setReadOnlyCalendars({ QStringLiteral("/calendars/testuser/shared/") });
    QVERIFY(server.startListening());

    CalDavProvider provider;
    provider.load(makeConfig(server.baseUrl()));

    QFuture<bool> fut = provider.connect();
    QVERIFY(waitForFutureBool(fut));
    QCOMPARE(fut.result(), true);

    const auto cols = provider.collections();
    QCOMPARE(cols.size(), 2);

    bool sawPersonal = false;
    bool sawShared = false;
    for (const auto &c : cols) {
        if (c.name == QStringLiteral("Personal")) {
            sawPersonal = true;
            QVERIFY2(!c.readOnly, "writable calendar must not be marked readOnly");
        } else if (c.name == QStringLiteral("Shared")) {
            sawShared = true;
            QVERIFY2(c.readOnly, "read-only ACL calendar must be marked readOnly");
        }
    }
    QVERIFY(sawPersonal);
    QVERIFY(sawShared);
}

void TstCalDavProvider::connect_populates_contenttypes_from_component_sets()
{
    // Discovery records supportsVEvent/supportsVTodo per calendar; the
    // CollectionInfo rows exposed via collections() must carry them as
    // contentTypes ("VEVENT"/"VTODO") so consumers can bind task lists to
    // todo conduits (WildPalms RFC 2026-06-09). One events-only, one
    // tasks-only, one mixed calendar.
    FakeCalDavServer server;
    server.setCalendars({
        { QStringLiteral("Events"), QStringLiteral("/calendars/testuser/events/") },
        { QStringLiteral("Tasks"),  QStringLiteral("/calendars/testuser/tasks/") },
        { QStringLiteral("Mixed"),  QStringLiteral("/calendars/testuser/mixed/") }
    });
    server.setCalendarComponents(QStringLiteral("/calendars/testuser/events/"),
                                 { QStringLiteral("VEVENT") });
    server.setCalendarComponents(QStringLiteral("/calendars/testuser/tasks/"),
                                 { QStringLiteral("VTODO") });
    server.setCalendarComponents(QStringLiteral("/calendars/testuser/mixed/"),
                                 { QStringLiteral("VEVENT"), QStringLiteral("VTODO") });
    QVERIFY(server.startListening());

    CalDavProvider provider;
    provider.load(makeConfig(server.baseUrl()));

    QFuture<bool> fut = provider.connect();
    QVERIFY(waitForFutureBool(fut));
    QCOMPARE(fut.result(), true);

    const auto cols = provider.collections();
    QCOMPARE(cols.size(), 3);

    QHash<QString, QStringList> typesByName;
    for (const auto &c : cols)
        typesByName.insert(c.name, c.contentTypes);

    QCOMPARE(typesByName.value(QStringLiteral("Events")),
             (QStringList{ QStringLiteral("VEVENT") }));
    QCOMPARE(typesByName.value(QStringLiteral("Tasks")),
             (QStringList{ QStringLiteral("VTODO") }));
    QCOMPARE(typesByName.value(QStringLiteral("Mixed")),
             (QStringList{ QStringLiteral("VEVENT"), QStringLiteral("VTODO") }));
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
    // dynamic type is RemoteCalendarBackend (which inherits SyncBackend which
    // inherits IBlobBackend).
    auto *remote = dynamic_cast<RemoteCalendarBackend*>(backend.get());
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

void TstCalDavProvider::connect_when_already_connected_is_noop()
{
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    CalDavProvider provider;
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

void TstCalDavProvider::connect_with_empty_url_emits_error_immediately()
{
    CalDavProvider provider;

    QSignalSpy errSpy(&provider, &IProvider::error);
    QFuture<bool> fut = provider.connect();

    QVERIFY(fut.isFinished());
    QCOMPARE(fut.result(), false);
    QVERIFY(!provider.isConnected());
    QCOMPARE(errSpy.count(), 1);
    QVERIFY(!errSpy.first().at(0).toString().isEmpty());
}

void TstCalDavProvider::connect_with_invalid_url_emits_error_immediately()
{
    CalDavProvider provider;
    BackendConfiguration cfg;
    cfg.id = QStringLiteral("test");
    cfg.connectionParams.insert(QStringLiteral("url"),
                                QStringLiteral("not-a-url"));
    cfg.connectionParams.insert(QStringLiteral("username"), QStringLiteral("u"));
    cfg.connectionParams.insert(QStringLiteral("password"), QStringLiteral("p"));
    provider.load(cfg);

    QSignalSpy errSpy(&provider, &IProvider::error);
    QFuture<bool> fut = provider.connect();

    QVERIFY(fut.isFinished());
    QCOMPARE(fut.result(), false);
    QVERIFY(!provider.isConnected());
    QCOMPARE(errSpy.count(), 1);
}

void TstCalDavProvider::disconnect_mid_flight_resolves_promise_false()
{
    QTcpServer hungServer;
    QVERIFY(hungServer.listen(QHostAddress::LocalHost, 0));
    const QUrl hungUrl(
        QStringLiteral("http://127.0.0.1:%1/").arg(hungServer.serverPort()));

    CalDavProvider provider;
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

void TstCalDavProvider::createBackend_when_not_connected_returns_nullptr()
{
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    CalDavProvider provider;
    provider.load(makeConfig(server.baseUrl()));

    auto backend = provider.createBackend(QStringLiteral("any-id"));
    QVERIFY(backend == nullptr);
}

void TstCalDavProvider::createBackend_after_disconnect_returns_nullptr()
{
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    CalDavProvider provider;
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

void TstCalDavProvider::connect_while_inflight_is_idempotent()
{
    // v0.61: second connect() while in-flight must return the in-flight future,
    // not start a second discovery and overwrite m_connectPromise (which would
    // cancel the first future and crash any watcher::result() observer).
    QTcpServer hungServer;
    QVERIFY(hungServer.listen(QHostAddress::LocalHost, 0));
    const QUrl hungUrl(
        QStringLiteral("http://127.0.0.1:%1/").arg(hungServer.serverPort()));

    CalDavProvider provider;
    provider.load(makeConfig(hungUrl));

    QFuture<bool> fut1 = provider.connect();
    QVERIFY(!fut1.isFinished());

    // Second connect() while in-flight: must return the same future.
    QFuture<bool> fut2 = provider.connect();
    QVERIFY(!fut2.isFinished());

    // Both futures share the same state: finishing one finishes both.
    QSignalSpy stateSpy(&provider, &IProvider::connectionStateChanged);
    provider.disconnect();
    QVERIFY(fut1.isFinished());
    QVERIFY(fut2.isFinished());
    QCOMPARE(fut1.result(), false);
    QCOMPARE(fut2.result(), false);
    // connectionStateChanged must NOT fire: disconnect from non-connected state.
    QCOMPARE(stateSpy.count(), 0);
}

QTEST_GUILESS_MAIN(TstCalDavProvider)
#include "tst_caldav_provider.moc"
