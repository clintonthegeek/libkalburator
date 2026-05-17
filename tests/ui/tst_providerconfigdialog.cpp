#include <QObject>
#include <QtTest/QtTest>
#include <QComboBox>
#include <QLayout>
#include <QSignalSpy>

#include "../../src/ui/providerconfigdialog.h"
#include "../../src/sync/providermanager.h"
#include "../../src/sync/backendregistry.h"
#include "../../src/sync/caldavbackendcontribution.h"

using namespace Kalburator;
using namespace Kalburator::Sync;

class TstProviderConfigDialog : public QObject
{
    Q_OBJECT
private slots:
    void comboPopulatedFromKindsList();
    void switchingComboEmbedHostPersists();
    void embedHostBuildsConfigWidgetWhenContributionRegistered();
};

void TstProviderConfigDialog::comboPopulatedFromKindsList()
{
    BackendRegistry registry;
    ProviderManager pm(&registry);
    QList<Ui::ProviderConfigDialog::ProviderKind> kinds {
        { QStringLiteral("caldav"),         QStringLiteral("CalDAV") },
        { QStringLiteral("carddav"),        QStringLiteral("CardDAV") },
        { QStringLiteral("multiproto-dav"), QStringLiteral("Multi-protocol DAV") },
    };
    Ui::ProviderConfigDialog dlg(&pm, kinds, Ui::ProviderConfigDialog::AddNew);
    auto *combo = dlg.findChild<QComboBox*>(QStringLiteral("providerCombo"));
    QVERIFY(combo != nullptr);
    QCOMPARE(combo->count(), 3);
}

void TstProviderConfigDialog::switchingComboEmbedHostPersists()
{
    BackendRegistry registry;
    ProviderManager pm(&registry);
    QList<Ui::ProviderConfigDialog::ProviderKind> kinds {
        { QStringLiteral("caldav"),         QStringLiteral("CalDAV") },
        { QStringLiteral("multiproto-dav"), QStringLiteral("Multi-protocol DAV") },
    };
    Ui::ProviderConfigDialog dlg(&pm, kinds, Ui::ProviderConfigDialog::AddNew);
    auto *combo = dlg.findChild<QComboBox*>(QStringLiteral("providerCombo"));
    QVERIFY(combo != nullptr);
    if (combo->count() < 2)
        QSKIP("Need at least 2 providers for switching test");
    combo->setCurrentIndex(0);
    QVERIFY(dlg.findChild<QWidget*>(QStringLiteral("providerConfigEmbed")) != nullptr);
    combo->setCurrentIndex(1);
    QVERIFY(dlg.findChild<QWidget*>(QStringLiteral("providerConfigEmbed")) != nullptr);
}

void TstProviderConfigDialog::embedHostBuildsConfigWidgetWhenContributionRegistered()
{
    BackendRegistry registry;
    registry.registerContribution(std::make_shared<CalDavBackendContribution>());
    ProviderManager pm(&registry);
    QList<Ui::ProviderConfigDialog::ProviderKind> kinds {
        { QStringLiteral("caldav"), QStringLiteral("CalDAV") },
    };
    Ui::ProviderConfigDialog dlg(&pm, kinds, Ui::ProviderConfigDialog::AddNew);
    auto *embedHost = dlg.findChild<QWidget*>(QStringLiteral("providerConfigEmbed"));
    QVERIFY(embedHost != nullptr);
    QVERIFY2(embedHost->layout()->count() > 0,
             "expected provider's config widget to be embedded after wiring");
}

QTEST_MAIN(TstProviderConfigDialog)
#include "tst_providerconfigdialog.moc"
