// CardDavConfigWidget tests — the lean config-widget pattern.
//
// Mirrors the config round-trip half of tst_caldav_config_widget.cpp; the lean
// widget has no Test button, so there are no connect/status assertions. The
// integration assertion (createConfigWidget returns a conforming widget) pins
// the gap this widget was added to close.

#include <QObject>
#include <QtTest/QtTest>
#include <QLineEdit>

#include "../../src/sync/carddavconfigwidget.h"
#include "../../src/sync/carddavprovider.h"
#include "../../src/sync/iproviderconfigwidget.h"

using namespace Kalburator::Sync;

class TstCardDavConfigWidget : public QObject
{
    Q_OBJECT
private slots:
    void setConfiguration_populatesFields();
    void configuration_reportsCardDavType();
    void roundTripsConnectionParams();
    void providerCreatesConformingWidget();
};

void TstCardDavConfigWidget::setConfiguration_populatesFields()
{
    CardDavConfigWidget w;
    BackendConfiguration cfg;
    cfg.displayName = QStringLiteral("Contacts");
    cfg.connectionParams[QStringLiteral("url")]      = QStringLiteral("https://nc.example/");
    cfg.connectionParams[QStringLiteral("username")] = QStringLiteral("alice");
    cfg.connectionParams[QStringLiteral("password")] = QStringLiteral("secret");

    w.setConfiguration(cfg);
    const BackendConfiguration out = w.configuration();

    QCOMPARE(out.displayName, QStringLiteral("Contacts"));
    QCOMPARE(out.connectionParams[QStringLiteral("url")].toString(),
             QStringLiteral("https://nc.example/"));
    QCOMPARE(out.connectionParams[QStringLiteral("username")].toString(),
             QStringLiteral("alice"));
    QCOMPARE(out.connectionParams[QStringLiteral("password")].toString(),
             QStringLiteral("secret"));
}

void TstCardDavConfigWidget::configuration_reportsCardDavType()
{
    CardDavConfigWidget w;
    QCOMPARE(w.configuration().type, QStringLiteral("carddav"));
}

void TstCardDavConfigWidget::roundTripsConnectionParams()
{
    CardDavConfigWidget w;
    BackendConfiguration cfg;
    cfg.connectionParams[QStringLiteral("url")]      = QStringLiteral("https://x/");
    cfg.connectionParams[QStringLiteral("username")] = QStringLiteral("u");
    cfg.connectionParams[QStringLiteral("password")] = QStringLiteral("p");

    w.setConfiguration(cfg);
    QCOMPARE(w.configuration().connectionParams, cfg.connectionParams);
}

void TstCardDavConfigWidget::providerCreatesConformingWidget()
{
    CardDavProvider provider;
    QWidget *w = provider.createConfigWidget(nullptr);
    QVERIFY2(w != nullptr,
             "CardDavProvider::createConfigWidget() must return a real widget");
    QVERIFY2(dynamic_cast<IProviderConfigWidget *>(w) != nullptr,
             "the widget must implement IProviderConfigWidget so consumers can bridge it");
    delete w;
}

QTEST_MAIN(TstCardDavConfigWidget)
#include "tst_carddav_config_widget.moc"
