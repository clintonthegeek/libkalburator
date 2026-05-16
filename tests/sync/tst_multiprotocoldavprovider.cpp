#include <QObject>
#include <QtTest/QtTest>
#include <QUuid>

#include "../../src/sync/multiprotocoldavprovider.h"

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

QTEST_GUILESS_MAIN(TstMultiProtocolDavProvider)
#include "tst_multiprotocoldavprovider.moc"
