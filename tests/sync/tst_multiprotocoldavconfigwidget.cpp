#include <QObject>
#include <QtTest/QtTest>
#include <QLineEdit>

#include "../../src/sync/multiprotocoldavconfigwidget.h"

using namespace Kalburator::Sync;

class TstMultiProtocolDavConfigWidget : public QObject
{
    Q_OBJECT
private slots:
    void roundTripsConnectionParams();
    void advancedFieldsHiddenByDefault();
};

void TstMultiProtocolDavConfigWidget::roundTripsConnectionParams()
{
    MultiProtocolDavConfigWidget w;
    BackendConfiguration cfg;
    cfg.displayName = QStringLiteral("Test NC");
    cfg.connectionParams[QStringLiteral("url")]      = QStringLiteral("https://nc.example/");
    cfg.connectionParams[QStringLiteral("username")] = QStringLiteral("alice");
    cfg.connectionParams[QStringLiteral("password")] = QStringLiteral("secret");

    w.setConfiguration(cfg);
    const BackendConfiguration out = w.configuration();

    QCOMPARE(out.displayName, cfg.displayName);
    QCOMPARE(out.connectionParams[QStringLiteral("url")].toString(),
             QStringLiteral("https://nc.example/"));
    QCOMPARE(out.connectionParams[QStringLiteral("username")].toString(),
             QStringLiteral("alice"));
    QCOMPARE(out.connectionParams[QStringLiteral("password")].toString(),
             QStringLiteral("secret"));
}

void TstMultiProtocolDavConfigWidget::advancedFieldsHiddenByDefault()
{
    MultiProtocolDavConfigWidget w;
    auto *manualCalDav = w.findChild<QLineEdit*>(QStringLiteral("manualCalDavEdit"));
    QVERIFY(manualCalDav != nullptr);
    QVERIFY(!manualCalDav->isVisible());  // hidden inside collapsed Advanced section
}

QTEST_MAIN(TstMultiProtocolDavConfigWidget)
#include "tst_multiprotocoldavconfigwidget.moc"
