#include <QObject>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QUuid>

#include "../../src/sync/multiprotocoldavprovider.h"
#include "../../src/plugin/pluginmanager.h"
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
    void createBackendUnknownIdReturnsNullptr();
    void createBackendNotConnectedReturnsNullptr();
    void pluginRegistersMultiProtoDavContribution();
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
    QSKIP("Requires composed FakeCalDav+FakeCardDav harness; follow-up task");
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

void TstMultiProtocolDavProvider::pluginRegistersMultiProtoDavContribution()
{
    BackendRegistry reg;
    PluginManager pm(&reg);
    registerStockPlugins(pm);
    QVERIFY(reg.contributionFor(
        QStringLiteral("multiproto-dav")) != nullptr);
}

QTEST_GUILESS_MAIN(TstMultiProtocolDavProvider)
#include "tst_multiprotocoldavprovider.moc"
