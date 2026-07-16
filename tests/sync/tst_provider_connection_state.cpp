// Task 4 (sync-graph redesign Phase 1) — providers emit fine-grained
// connection state (Connecting/Connected/Error) via the new
// IProvider::connectionStateChanged(ProviderConnectionState) overload, and
// lastError() is populated on failure.
//
// Drives MultiProtocolDavProvider (representative of the three DAV-family
// providers — CalDavProvider and CardDavProvider share the same connect()
// shape and are covered by their own provider test files) against
// FakeCalDavServer for the success path and against a closed TCP port for
// the error path.

#include <QtTest/QtTest>
#include <QFutureWatcher>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTcpServer>
#include <QUrl>

#include "fakecaldavserver.h"

#include "backendconfiguration.h"
#include "iprovider.h"
#include "multiprotocoldavprovider.h"

using namespace Kalburator::Sync;

namespace {

// Same idiom as tst_caldav_provider.cpp / tst_multiprotocoldavprovider.cpp:
// spin the event loop via a QFutureWatcher signal spy rather than calling
// future.waitForFinished(), which does not pump the QNAM async I/O loop.
bool waitForFutureBool(QFuture<bool> f, int timeoutMs = 5000)
{
    if (f.isFinished()) return true;
    QFutureWatcher<bool> w;
    QSignalSpy doneSpy(&w, &QFutureWatcher<bool>::finished);
    w.setFuture(f);
    if (f.isFinished()) return true;
    return doneSpy.wait(timeoutMs);
}

} // namespace

class TstProviderConnectionState : public QObject
{
    Q_OBJECT

private slots:
    void connect_success_emits_connecting_then_connected();
    void connect_failure_emits_connecting_then_error_with_lasterror();
    void reconnect_via_applyConfig_emits_full_cycle_again();
};

void TstProviderConnectionState::connect_success_emits_connecting_then_connected()
{
    FakeCalDavServer server;
    server.setCalendars({
        { QStringLiteral("Personal"), QStringLiteral("/calendars/testuser/personal/") }
    });
    QVERIFY(server.startListening());

    BackendConfiguration cfg;
    cfg.id = QStringLiteral("state-success");
    cfg.connectionParams.insert(QStringLiteral("url"), server.baseUrl().toString());
    cfg.connectionParams.insert(QStringLiteral("username"), QStringLiteral("testuser"));
    cfg.connectionParams.insert(QStringLiteral("password"), QStringLiteral("testpass"));

    MultiProtocolDavProvider provider(true);   // calendarsOnly
    provider.load(cfg);

    QSignalSpy stateSpy(&provider,
        qOverload<ProviderConnectionState>(&IProvider::connectionStateChanged));

    QFuture<bool> fut = provider.connect();
    QVERIFY(waitForFutureBool(fut, 20000));
    QCOMPARE(fut.result(), true);
    QVERIFY(provider.isConnected());

    // Ordering: Connecting, then Connected. No Error in between.
    QVERIFY2(stateSpy.count() >= 2,
             qPrintable(QStringLiteral("expected >= 2 state emissions, got %1")
                            .arg(stateSpy.count())));
    QCOMPARE(stateSpy.at(0).at(0).value<ProviderConnectionState>(),
             ProviderConnectionState::Connecting);
    QCOMPARE(stateSpy.at(1).at(0).value<ProviderConnectionState>(),
             ProviderConnectionState::Connected);
    for (const auto &call : stateSpy) {
        QVERIFY(call.at(0).value<ProviderConnectionState>()
                != ProviderConnectionState::Error);
    }
    QVERIFY(provider.lastError().isEmpty());
}

void TstProviderConnectionState::connect_failure_emits_connecting_then_error_with_lasterror()
{
    // Bind a free port, then release it immediately — nothing is listening,
    // so any connection attempt is refused right away (unlike a "hung"
    // server that accepts but never responds, used elsewhere for
    // idempotency tests).
    QTcpServer probe;
    QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
    const quint16 closedPort = probe.serverPort();
    probe.close();
    const QUrl closedUrl(QStringLiteral("http://127.0.0.1:%1/").arg(closedPort));

    BackendConfiguration cfg;
    cfg.id = QStringLiteral("state-failure");
    cfg.connectionParams.insert(QStringLiteral("url"), closedUrl.toString());
    cfg.connectionParams.insert(QStringLiteral("username"), QStringLiteral("testuser"));
    cfg.connectionParams.insert(QStringLiteral("password"), QStringLiteral("testpass"));

    MultiProtocolDavProvider provider(true);   // calendarsOnly
    provider.load(cfg);

    QSignalSpy stateSpy(&provider,
        qOverload<ProviderConnectionState>(&IProvider::connectionStateChanged));

    QFuture<bool> fut = provider.connect();
    QVERIFY(waitForFutureBool(fut, 20000));
    QCOMPARE(fut.result(), false);
    QVERIFY(!provider.isConnected());

    // Ordering: Connecting, then Error. No Connected in between.
    QVERIFY2(stateSpy.count() >= 2,
             qPrintable(QStringLiteral("expected >= 2 state emissions, got %1")
                            .arg(stateSpy.count())));
    QCOMPARE(stateSpy.at(0).at(0).value<ProviderConnectionState>(),
             ProviderConnectionState::Connecting);
    QCOMPARE(stateSpy.at(1).at(0).value<ProviderConnectionState>(),
             ProviderConnectionState::Error);
    for (const auto &call : stateSpy) {
        QVERIFY(call.at(0).value<ProviderConnectionState>()
                != ProviderConnectionState::Connected);
    }
    QVERIFY(!provider.lastError().isEmpty());
}

void TstProviderConnectionState::reconnect_via_applyConfig_emits_full_cycle_again()
{
    // applyConfig()'s default IProvider impl (disconnect() -> load() ->
    // connect()) is the "reconnect" path the brief calls out separately.
    // Since none of the three DAV providers override applyConfig(), driving
    // connect() twice with a config change in between exercises the same
    // Connecting->Connected cycle firing again from a second attempt.
    FakeCalDavServer server;
    server.setCalendars({
        { QStringLiteral("Personal"), QStringLiteral("/calendars/testuser/personal/") }
    });
    QVERIFY(server.startListening());

    BackendConfiguration cfg;
    cfg.id = QStringLiteral("state-reconnect");
    cfg.connectionParams.insert(QStringLiteral("url"), server.baseUrl().toString());
    cfg.connectionParams.insert(QStringLiteral("username"), QStringLiteral("testuser"));
    cfg.connectionParams.insert(QStringLiteral("password"), QStringLiteral("testpass"));

    MultiProtocolDavProvider provider(true);
    provider.load(cfg);

    QFuture<bool> fut1 = provider.connect();
    QVERIFY(waitForFutureBool(fut1, 20000));
    QVERIFY(provider.isConnected());

    QSignalSpy stateSpy(&provider,
        qOverload<ProviderConnectionState>(&IProvider::connectionStateChanged));

    // applyConfig() default impl: disconnect() then load(cfg) then connect()
    // again (fire-and-forget). Re-apply the same config to force the
    // reconnect path without changing server behavior.
    provider.applyConfig(cfg);

    QTRY_VERIFY_WITH_TIMEOUT(provider.isConnected(), 20000);

    QVERIFY2(stateSpy.count() >= 2,
             qPrintable(QStringLiteral("expected >= 2 state emissions on reconnect, got %1")
                            .arg(stateSpy.count())));
    QCOMPARE(stateSpy.at(0).at(0).value<ProviderConnectionState>(),
             ProviderConnectionState::Connecting);
    QCOMPARE(stateSpy.at(1).at(0).value<ProviderConnectionState>(),
             ProviderConnectionState::Connected);
}

QTEST_GUILESS_MAIN(TstProviderConnectionState)
#include "tst_provider_connection_state.moc"
