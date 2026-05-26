#include <QObject>
#include <QtTest/QtTest>
#include <QComboBox>
#include <QLabel>
#include <QLayout>
#include <QPromise>
#include <QSignalSpy>

#include "../../src/ui/providerconfigdialog.h"
#include "../../src/sync/providermanager.h"
#include "../../src/sync/backendregistry.h"
#include "../../src/sync/backendcontribution.h"
#include "../../src/sync/caldavbackendcontribution.h"
#include "../../src/sync/iprovider.h"

using namespace Kalburator;
using namespace Kalburator::Sync;

// A provider whose connect() fails and emits a specific error() message, so
// we can assert the dialog surfaces that message to the user.
class FailingStubProvider : public IProvider {
    Q_OBJECT
public:
    explicit FailingStubProvider(QString msg) : m_msg(std::move(msg)) {}
    QString id() const override { return QStringLiteral("failing-stub"); }
    QString kind() const override { return QStringLiteral("failing-stub"); }
    QString displayName() const override { return QStringLiteral("Failing Stub"); }
    void load(const BackendConfiguration &) override {}
    BackendConfiguration save() const override { return {}; }
    QWidget *createConfigWidget(QWidget *) override { return nullptr; }
    QFuture<bool> connect() override {
        emit error(m_msg);
        QPromise<bool> p; auto f = p.future();
        p.start(); p.addResult(false); p.finish();
        return f;
    }
    void disconnect() override {}
    bool isConnected() const override { return false; }
    QList<CollectionInfo> collections() const override { return {}; }
    std::unique_ptr<IBlobBackend> createBackend(const QString &) override { return nullptr; }
private:
    QString m_msg;
};

class FailingStubContribution : public BackendContribution {
public:
    explicit FailingStubContribution(QString msg) : m_msg(std::move(msg)) {}
    QString backendType() const override { return QStringLiteral("failing-stub"); }
    QString displayName() const override { return QStringLiteral("Failing Stub"); }
    QList<Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<IProvider> createProvider(QObject * = nullptr) const override
    { return std::make_unique<FailingStubProvider>(m_msg); }
private:
    QString m_msg;
};

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
    void comboPopulatedFromRegistry();
    void switchingComboEmbedHostPersists();
    void embedHostBuildsConfigWidgetWhenContributionRegistered();
    void takeProviderMovesOwnership();
    void dynamicKindsConstructor_populatesFromRegistry();
    void dynamicKindsConstructor_reactsToContributionRegistered();
    void failedTestConnection_surfacesErrorMessage();
};

void TstProviderConfigDialog::failedTestConnection_surfacesErrorMessage()
{
    const QString kMsg = QStringLiteral(
        "Calendar discovery failed: Failed to discover principal (HTTP 401)");

    BackendRegistry registry;
    registry.registerContribution(
        std::make_shared<FailingStubContribution>(kMsg));
    ProviderManager pm(&registry);
    Ui::ProviderConfigDialog dlg(&pm, &registry,
                                 Ui::ProviderConfigDialog::AddNew, {});

    // Trigger the Test-connection flow.
    QMetaObject::invokeMethod(&dlg, "onTestClicked");

    // A status label must exist and display the provider's error message,
    // not just an opaque "Failed".
    auto *status = dlg.findChild<QLabel*>(QStringLiteral("testStatusLabel"));
    QVERIFY2(status != nullptr, "dialog must expose a testStatusLabel");
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(kMsg), 3000);
}

void TstProviderConfigDialog::comboPopulatedFromRegistry()
{
    BackendRegistry registry;
    registry.registerContribution(
        std::make_shared<StubContribution>(QStringLiteral("caldav")));
    registry.registerContribution(
        std::make_shared<StubContribution>(QStringLiteral("carddav")));
    registry.registerContribution(
        std::make_shared<StubContribution>(QStringLiteral("multiproto-dav")));
    ProviderManager pm(&registry);
    Ui::ProviderConfigDialog dlg(&pm, &registry, Ui::ProviderConfigDialog::AddNew, {});
    auto *combo = dlg.findChild<QComboBox*>(QStringLiteral("providerCombo"));
    QVERIFY(combo != nullptr);
    QCOMPARE(combo->count(), 3);
}

void TstProviderConfigDialog::switchingComboEmbedHostPersists()
{
    BackendRegistry registry;
    registry.registerContribution(
        std::make_shared<StubContribution>(QStringLiteral("caldav")));
    registry.registerContribution(
        std::make_shared<StubContribution>(QStringLiteral("multiproto-dav")));
    ProviderManager pm(&registry);
    Ui::ProviderConfigDialog dlg(&pm, &registry, Ui::ProviderConfigDialog::AddNew, {});
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
    Ui::ProviderConfigDialog dlg(&pm, &registry, Ui::ProviderConfigDialog::AddNew, {});
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
    Ui::ProviderConfigDialog dlg(&pm, &registry, Ui::ProviderConfigDialog::AddNew, {});
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
