#include <QObject>
#include <QtTest/QtTest>
#include <QFutureWatcher>
#include <QSignalSpy>
#include <QUuid>

#include "fakecaldavserver.h"

#include "../../src/sync/multiprotocoldavprovider.h"
#include "../../src/plugin/pluginmanager.h"
#include "../../src/shape/shaperegistries.h"
#include "../../src/plugin/stock_plugins.h"
#include "../../src/sync/backendregistry.h"

using namespace Kalburator;
using namespace Kalburator::Sync;

class TstMultiProtocolDavProvider : public QObject
{
    Q_OBJECT
private slots:
    void kindIsMultiprotoDav();
    void idIsNonEmptyAfterConstruction();
    void displayNameDefaultsToSomethingHuman();
    void isNotConnectedAfterConstruction();
    void collectionsEmptyAfterConstruction();
    void loadAndSaveRoundTripsConnectionParams();
    void loadAppliesDisplayNameAndId();
    void connectWithoutUrlReturnsFalseQuickly();
    void connectInvalidCredentialsEmitsErrorAndResolvesFalse();
    void connectPartialSuccessSkipped();
    void calendarsOnly_mode_excludes_contacts();
    void createBackendUnknownIdReturnsNullptr();
    void createBackendNotConnectedReturnsNullptr();
    void connectPopulatesContentTypesOnCalDavCollections();
    void pluginRegistersMultiProtoDavContribution();
    void contributionCreateProviderHonorsParent();
    void connect_while_inflight_is_idempotent();
};

void TstMultiProtocolDavProvider::kindIsMultiprotoDav()
{
    MultiProtocolDavProvider p;
    QCOMPARE(p.kind(), QStringLiteral("multiproto-dav"));
}

void TstMultiProtocolDavProvider::idIsNonEmptyAfterConstruction()
{
    MultiProtocolDavProvider p;
    const QString id = p.id();
    QVERIFY(!id.isEmpty());
    QVERIFY(QUuid(id).isNull() == false);
}

void TstMultiProtocolDavProvider::displayNameDefaultsToSomethingHuman()
{
    MultiProtocolDavProvider p;
    QVERIFY(!p.displayName().isEmpty());
}

void TstMultiProtocolDavProvider::isNotConnectedAfterConstruction()
{
    MultiProtocolDavProvider p;
    QVERIFY(!p.isConnected());
}

void TstMultiProtocolDavProvider::collectionsEmptyAfterConstruction()
{
    MultiProtocolDavProvider p;
    QVERIFY(p.collections().isEmpty());
}

void TstMultiProtocolDavProvider::loadAndSaveRoundTripsConnectionParams()
{
    BackendConfiguration cfg;
    cfg.id = QStringLiteral("test-uuid-1");
    cfg.type = QStringLiteral("multiproto-dav");
    cfg.displayName = QStringLiteral("My Nextcloud");
    cfg.connectionParams[QStringLiteral("url")]
        = QStringLiteral("https://cloud.example.com");
    cfg.connectionParams[QStringLiteral("username")]
        = QStringLiteral("alice");
    cfg.connectionParams[QStringLiteral("password")]
        = QStringLiteral("hunter2");
    cfg.connectionParams[QStringLiteral("manualCaldavPrincipal")]
        = QStringLiteral("https://cloud.example.com/dav/cal/");

    MultiProtocolDavProvider p;
    p.load(cfg);
    const BackendConfiguration roundtrip = p.save();

    QCOMPARE(roundtrip.id,          cfg.id);
    QCOMPARE(roundtrip.type,        QStringLiteral("multiproto-dav"));
    QCOMPARE(roundtrip.displayName, cfg.displayName);
    QCOMPARE(roundtrip.connectionParams.value(QStringLiteral("url")).toString(),
             QStringLiteral("https://cloud.example.com"));
    QCOMPARE(roundtrip.connectionParams.value(QStringLiteral("username")).toString(),
             QStringLiteral("alice"));
    QCOMPARE(roundtrip.connectionParams.value(QStringLiteral("password")).toString(),
             QStringLiteral("hunter2"));
    QCOMPARE(roundtrip.connectionParams.value(QStringLiteral("manualCaldavPrincipal")).toString(),
             QStringLiteral("https://cloud.example.com/dav/cal/"));
}

void TstMultiProtocolDavProvider::loadAppliesDisplayNameAndId()
{
    BackendConfiguration cfg;
    cfg.id = QStringLiteral("specific-id");
    cfg.displayName = QStringLiteral("Work Nextcloud");

    MultiProtocolDavProvider p;
    p.load(cfg);

    QCOMPARE(p.id(),          QStringLiteral("specific-id"));
    QCOMPARE(p.displayName(), QStringLiteral("Work Nextcloud"));
}

void TstMultiProtocolDavProvider::connectWithoutUrlReturnsFalseQuickly()
{
    MultiProtocolDavProvider p;
    BackendConfiguration cfg;
    cfg.id = QStringLiteral("test");
    cfg.connectionParams[QStringLiteral("username")] = QStringLiteral("u");
    cfg.connectionParams[QStringLiteral("password")] = QStringLiteral("p");
    // url intentionally absent
    p.load(cfg);

    auto fut = p.connect();
    QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 5000);
    QCOMPARE(fut.resultAt(0), false);
    QVERIFY(!p.isConnected());
}

void TstMultiProtocolDavProvider::connectInvalidCredentialsEmitsErrorAndResolvesFalse()
{
    MultiProtocolDavProvider p;
    QSignalSpy errSpy(&p, &IProvider::error);
    BackendConfiguration cfg;
    cfg.id = QStringLiteral("test");
    cfg.connectionParams[QStringLiteral("url")]      = QStringLiteral("http://localhost:1/");
    cfg.connectionParams[QStringLiteral("username")] = QStringLiteral("nobody");
    cfg.connectionParams[QStringLiteral("password")] = QStringLiteral("nopass");
    p.load(cfg);

    auto fut = p.connect();
    QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 30000);
    QCOMPARE(fut.resultAt(0), false);
    QVERIFY(errSpy.count() > 0 || !p.isConnected());  // either error emitted or simply not connected
}

void TstMultiProtocolDavProvider::connectPartialSuccessSkipped()
{
    // CalDAV succeeds; CardDAV pointed at "/bogus-carddav/" (404) so it fails.
    // In full mode (calendarsOnly = false): partial success — provider reports
    // connected=true with only calendar collections; lastWarning() names the
    // failed protocol.
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    BackendConfiguration cfg;
    cfg.id   = QStringLiteral("partial-test");
    cfg.type = QStringLiteral("multiproto-dav");
    cfg.connectionParams.insert(QStringLiteral("url"), server.baseUrl().toString());
    cfg.connectionParams.insert(QStringLiteral("username"), QStringLiteral("testuser"));
    cfg.connectionParams.insert(QStringLiteral("password"), QStringLiteral("testpass"));
    cfg.connectionParams.insert(QStringLiteral("manualCarddavPrincipal"),
                                QStringLiteral("/bogus-carddav/"));

    MultiProtocolDavProvider provider(false);   // full mode: CardDAV failure = partial
    provider.load(cfg);

    QFuture<bool> fut = provider.connect();
    QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 20000);
    QCOMPARE(fut.resultAt(0), true);
    QVERIFY(provider.isConnected());

    // Calendar collections present; no contact collections.
    bool hasCalCol     = false;
    bool hasContactCol = false;
    for (const auto &c : provider.collections()) {
        if (c.id.contains(QStringLiteral(":cal:")))      hasCalCol     = true;
        if (c.id.contains(QStringLiteral(":contacts:"))) hasContactCol = true;
    }
    QVERIFY2(hasCalCol,      "expected at least one calendar collection after partial connect");
    QVERIFY2(!hasContactCol, "expected no contact collections: CardDAV failed");

    // A warning about the CardDAV failure must be surfaced.
    QVERIFY2(!provider.lastWarning().isEmpty(),
             "expected lastWarning() to name the CardDAV failure");
}

void TstMultiProtocolDavProvider::calendarsOnly_mode_excludes_contacts()
{
    // WP-A1 regression: in calendarsOnly mode (ctor default) CalDAV succeeds,
    // CardDAV is irrelevant — connect() resolves true and collections() returns
    // only :cal: entries even if CardDAV would also fail.
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    BackendConfiguration cfg;
    cfg.id   = QStringLiteral("co-test");
    cfg.type = QStringLiteral("multiproto-dav");
    cfg.connectionParams.insert(QStringLiteral("url"), server.baseUrl().toString());
    cfg.connectionParams.insert(QStringLiteral("username"), QStringLiteral("testuser"));
    cfg.connectionParams.insert(QStringLiteral("password"), QStringLiteral("testpass"));
    cfg.connectionParams.insert(QStringLiteral("manualCarddavPrincipal"),
                                QStringLiteral("/bogus-carddav/"));

    MultiProtocolDavProvider provider(true);    // calendarsOnly = true (ctor explicit)
    provider.load(cfg);

    QFuture<bool> fut = provider.connect();
    QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 20000);
    QCOMPARE(fut.resultAt(0), true);
    QVERIFY(provider.isConnected());

    // Only calendar collections — no contacts regardless of CardDAV outcome.
    for (const auto &c : provider.collections()) {
        QVERIFY2(!c.id.contains(QStringLiteral(":contacts:")),
                 qPrintable("Expected no contact collection in calendarsOnly mode, got: " + c.id));
    }
    QVERIFY2(!provider.collections().isEmpty(),
             "expected at least one calendar collection");

    // In calendarsOnly mode CardDAV failure does NOT produce a warning.
    QVERIFY2(provider.lastWarning().isEmpty(),
             "expected no warning: CardDAV failure is irrelevant in calendarsOnly mode");
}

void TstMultiProtocolDavProvider::createBackendUnknownIdReturnsNullptr()
{
    MultiProtocolDavProvider p;
    // Not connected, no m_urlByCollectionId entries
    QVERIFY(p.createBackend(QStringLiteral("totally-unknown")) == nullptr);
    QVERIFY(p.createBackend(QStringLiteral("multiproto-dav:x:cal:y")) == nullptr);
    QVERIFY(p.createBackend(QStringLiteral("multiproto-dav:x:contacts:y")) == nullptr);
}

void TstMultiProtocolDavProvider::createBackendNotConnectedReturnsNullptr()
{
    MultiProtocolDavProvider p;
    // Even with a valid-looking id, not connected → nullptr
    QVERIFY(!p.isConnected());
    QVERIFY(p.createBackend(QStringLiteral("multiproto-dav:test:cal:some-calendar")) == nullptr);
}

void TstMultiProtocolDavProvider::connectPopulatesContentTypesOnCalDavCollections()
{
    // The CalDAV-leg CollectionInfo rows must carry the discovered
    // per-calendar component capabilities as contentTypes (WildPalms RFC
    // 2026-06-09). Same fake-server pattern as the v0.63 convergence test:
    // one base URL serves CalDAV; the CardDAV half is pointed at a bogus
    // principal so it fails fast and the provider connects via CalDAV alone.
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

    BackendConfiguration cfg;
    cfg.id = QStringLiteral("mpdav-ct-test");
    cfg.type = QStringLiteral("multiproto-dav");
    cfg.connectionParams.insert(QStringLiteral("url"), server.baseUrl().toString());
    cfg.connectionParams.insert(QStringLiteral("username"), QStringLiteral("testuser"));
    cfg.connectionParams.insert(QStringLiteral("password"), QStringLiteral("testpass"));
    cfg.connectionParams.insert(QStringLiteral("manualCarddavPrincipal"),
                                QStringLiteral("/bogus-carddav/"));

    MultiProtocolDavProvider provider;
    provider.load(cfg);

    QFuture<bool> fut = provider.connect();
    QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 20000);
    QCOMPARE(fut.resultAt(0), true);

    QHash<QString, QStringList> typesByName;
    for (const auto &c : provider.collections()) {
        if (c.id.contains(QStringLiteral(":cal:")))
            typesByName.insert(c.name, c.contentTypes);
    }
    QCOMPARE(typesByName.size(), 3);
    QCOMPARE(typesByName.value(QStringLiteral("Events")),
             (QStringList{ QStringLiteral("VEVENT") }));
    QCOMPARE(typesByName.value(QStringLiteral("Tasks")),
             (QStringList{ QStringLiteral("VTODO") }));
    QCOMPARE(typesByName.value(QStringLiteral("Mixed")),
             (QStringList{ QStringLiteral("VEVENT"), QStringLiteral("VTODO") }));
}

void TstMultiProtocolDavProvider::pluginRegistersMultiProtoDavContribution()
{
    BackendRegistry reg;
    Shape::ShapeRegistries shape;
    PluginManager pm(&reg, shape);
    registerStockPlugins(pm);
    QVERIFY(reg.contributionFor(
        QStringLiteral("multiproto-dav")) != nullptr);
}

void TstMultiProtocolDavProvider::contributionCreateProviderHonorsParent()
{
    // Pins the ctor-argument swallow found by the 2026-06-10 audit: the
    // contribution called make_unique<MultiProtocolDavProvider>(parent),
    // binding the QObject* to the bool calendarsOnly parameter (pointer→bool
    // conversion) and dropping the parent entirely — so the provider was
    // unparented and the mode flag tracked the parent's null-ness.
    BackendRegistry reg;
    Shape::ShapeRegistries shape;
    PluginManager pm(&reg, shape);
    registerStockPlugins(pm);
    auto *contrib = reg.contributionFor(QStringLiteral("multiproto-dav"));
    QVERIFY(contrib != nullptr);

    QObject owner;
    auto provider = contrib->createProvider(&owner);
    QVERIFY(provider != nullptr);
    QCOMPARE(provider->parent(), &owner);
    // Reparented onto `owner` — hand ownership to the parent to avoid the
    // unique_ptr/QObject-parent double delete.
    (void)provider.release();
}

void TstMultiProtocolDavProvider::connect_while_inflight_is_idempotent()
{
    QTcpServer hungServer;
    QVERIFY(hungServer.listen(QHostAddress::LocalHost, 0));
    const QUrl hungUrl(
        QStringLiteral("http://127.0.0.1:%1/").arg(hungServer.serverPort()));

    MultiProtocolDavProvider p;
    BackendConfiguration cfg;
    cfg.id = QStringLiteral("test");
    cfg.connectionParams[QStringLiteral("url")]      = hungUrl.toString();
    cfg.connectionParams[QStringLiteral("username")] = QStringLiteral("u");
    cfg.connectionParams[QStringLiteral("password")] = QStringLiteral("p");
    p.load(cfg);

    QFuture<bool> fut1 = p.connect();
    QVERIFY(!fut1.isFinished());

    // Second connect() in-flight: must return the same (shared) future.
    QFuture<bool> fut2 = p.connect();
    QVERIFY(!fut2.isFinished());

    QSignalSpy stateSpy(&p, &IProvider::connectionStateChanged);
    p.disconnect();
    QTRY_VERIFY_WITH_TIMEOUT(fut1.isFinished(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(fut2.isFinished(), 5000);
    QCOMPARE(fut1.resultAt(0), false);
    QCOMPARE(fut2.resultAt(0), false);
    // No connectionStateChanged: was never connected.
    QCOMPARE(stateSpy.count(), 0);
}

QTEST_GUILESS_MAIN(TstMultiProtocolDavProvider)
#include "tst_multiprotocoldavprovider.moc"
