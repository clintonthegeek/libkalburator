#include <QObject>
#include <QtTest/QtTest>
#include <QComboBox>
#include <QLayout>
#include <QSignalSpy>

#include "../../src/ui/providerconfigdialog.h"
#include "../../src/sync/providermanager.h"
#include "../../src/sync/backendregistry.h"
#include "../../src/sync/backendcontribution.h"
#include "../../src/sync/caldavbackendcontribution.h"
#include "../../src/sync/iprovider.h"

using namespace Kalburator;
using namespace Kalburator::Sync;

// Minimal stub BackendContribution for O.1.4 dynamic-kinds tests.
class StubContribution : public BackendContribution {
public:
    explicit StubContribution(QString type) : m_type(std::move(type)) {}
    QString backendType() const override { return m_type; }
    QString displayName() const override
    { return QStringLiteral("Stub (%1)").arg(m_type); }
    QList<Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<IProvider> createProvider(QObject * /*parent*/ = nullptr) const override
    { return nullptr; }
private:
    QString m_type;
};

class TstProviderConfigDialog : public QObject
{
    Q_OBJECT
private slots:
    void comboPopulatedFromKindsList();
    void switchingComboEmbedHostPersists();
    void embedHostBuildsConfigWidgetWhenContributionRegistered();
    void takeProviderMovesOwnership();
    void dynamicKindsConstructor_populatesFromRegistry();
    void dynamicKindsConstructor_reactsToContributionRegistered();
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

void TstProviderConfigDialog::takeProviderMovesOwnership()
{
    BackendRegistry registry;
    registry.registerContribution(std::make_shared<CalDavBackendContribution>());
    ProviderManager pm(&registry);
    QList<Ui::ProviderConfigDialog::ProviderKind> kinds {
        { QStringLiteral("caldav"), QStringLiteral("CalDAV") },
    };
    Ui::ProviderConfigDialog dlg(&pm, kinds, Ui::ProviderConfigDialog::AddNew);
    auto p = dlg.takeProvider();
    QVERIFY2(p.get() != nullptr, "first takeProvider() must yield the constructed provider");
    auto p2 = dlg.takeProvider();
    QVERIFY2(p2.get() == nullptr, "second takeProvider() must yield null");
}

void TstProviderConfigDialog::dynamicKindsConstructor_populatesFromRegistry()
{
    BackendRegistry reg;
    reg.registerContribution(
        std::make_shared<StubContribution>(QStringLiteral("alpha")));
    reg.registerContribution(
        std::make_shared<StubContribution>(QStringLiteral("beta")));
    ProviderManager mgr(&reg);

    Ui::ProviderConfigDialog dlg(&mgr, &reg, Ui::ProviderConfigDialog::AddNew, {});
    const auto *combo = dlg.findChild<QComboBox*>(QStringLiteral("providerCombo"));
    QVERIFY(combo);
    QCOMPARE(combo->count(), 2);
    QCOMPARE(combo->itemText(0), QStringLiteral("Stub (alpha)"));
    QCOMPARE(combo->itemText(1), QStringLiteral("Stub (beta)"));
}

void TstProviderConfigDialog::dynamicKindsConstructor_reactsToContributionRegistered()
{
    BackendRegistry reg;
    reg.registerContribution(
        std::make_shared<StubContribution>(QStringLiteral("alpha")));
    ProviderManager mgr(&reg);

    Ui::ProviderConfigDialog dlg(&mgr, &reg, Ui::ProviderConfigDialog::AddNew, {});
    auto *combo = dlg.findChild<QComboBox*>(QStringLiteral("providerCombo"));
    QCOMPARE(combo->count(), 1);

    reg.registerContribution(
        std::make_shared<StubContribution>(QStringLiteral("beta")));
    QCOMPARE(combo->count(), 2);   // O.1.1 signal repopulated the combo

    reg.unregisterContribution(QStringLiteral("alpha"));
    QCOMPARE(combo->count(), 1);
}

QTEST_MAIN(TstProviderConfigDialog)
#include "tst_providerconfigdialog.moc"
