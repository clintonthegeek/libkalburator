#include <QObject>
#include <QtTest/QtTest>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QLayout>
#include <QMenu>
#include <QPromise>
#include <QPushButton>
#include <QSignalSpy>

#include "../../src/ui/providerconfigdialog.h"
#include "../../src/sync/providermanager.h"
#include "../../src/sync/backendregistry.h"
#include "../../src/sync/backendcontribution.h"
#include "../../src/sync/caldavbackendcontribution.h"
#include "../../src/sync/iprovider.h"
#include "../../src/sync/iproviderconfigwidget.h"

using namespace Kalburator;
using namespace Kalburator::Sync;

// A config widget (implementing the IProviderConfigWidget contract) that always
// reports a non-empty URL, plus a provider that only "connects" if it actually
// received that URL via load(). Together they prove the dialog bridges the
// widget's edited config into the provider before calling connect().
class BridgeStubConfigWidget : public QWidget, public IProviderConfigWidget {
public:
    BackendConfiguration configuration() const override {
        BackendConfiguration c;
        c.type = QStringLiteral("bridge-stub");
        c.displayName = QStringLiteral("Bridged");
        c.connectionParams[QStringLiteral("url")] = QStringLiteral("http://example.test/");
        return c;
    }
    void setConfiguration(const BackendConfiguration &) override {}
};

class BridgeStubProvider : public IProvider {
    Q_OBJECT
public:
    QString id() const override { return QStringLiteral("bridge-stub"); }
    QString kind() const override { return QStringLiteral("bridge-stub"); }
    QString displayName() const override { return QStringLiteral("Bridge Stub"); }
    void load(const BackendConfiguration &c) override
    { m_url = c.connectionParams.value(QStringLiteral("url")).toString(); }
    BackendConfiguration save() const override {
        BackendConfiguration c; c.type = kind();
        c.connectionParams[QStringLiteral("url")] = m_url;
        return c;
    }
    QWidget *createConfigWidget(QWidget *) override { return new BridgeStubConfigWidget; }
    QFuture<bool> connect() override {
        const bool ok = !m_url.isEmpty();
        if (!ok) emit error(QStringLiteral("no server URL configured"));
        QPromise<bool> p; auto f = p.future();
        p.start(); p.addResult(ok); p.finish();
        return f;
    }
    void disconnect() override {}
    bool isConnected() const override { return false; }
    QList<CollectionInfo> collections() const override { return {}; }
    std::vector<ProviderBackendSpec> createBackends() override { return {}; }
private:
    QString m_url;
};

class BridgeStubContribution : public BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("bridge-stub"); }
    QString displayName() const override { return QStringLiteral("Bridge Stub"); }
    QList<Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<IProvider> createProvider(QObject * = nullptr) const override
    { return std::make_unique<BridgeStubProvider>(); }
};

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
    std::vector<ProviderBackendSpec> createBackends() override { return {}; }
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

// A config widget that records what setConfiguration() was handed, and reports
// it back via configuration(). Lets a test prove autofill pushed values in.
class CapturingConfigWidget : public QWidget, public IProviderConfigWidget {
    Q_OBJECT
public:
    BackendConfiguration configuration() const override { return m_cfg; }
    void setConfiguration(const BackendConfiguration &c) override { m_cfg = c; }
private:
    BackendConfiguration m_cfg;
};

class CapturingProvider : public IProvider {
    Q_OBJECT
public:
    QString id() const override { return QStringLiteral("cap-dav"); }
    QString kind() const override { return QStringLiteral("cap-dav"); }
    QString displayName() const override { return QStringLiteral("Capturing DAV"); }
    void load(const BackendConfiguration &) override {}
    BackendConfiguration save() const override { BackendConfiguration c; c.type = kind(); return c; }
    QWidget *createConfigWidget(QWidget *) override { return new CapturingConfigWidget; }
    QFuture<bool> connect() override {
        QPromise<bool> p; auto f = p.future(); p.start(); p.addResult(true); p.finish(); return f;
    }
    void disconnect() override {}
    bool isConnected() const override { return false; }
    QList<CollectionInfo> collections() const override { return {}; }
    std::vector<ProviderBackendSpec> createBackends() override { return {}; }
};

class CapturingContribution : public BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("cap-dav"); }
    QString displayName() const override { return QStringLiteral("Capturing DAV"); }
    QList<Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<IProvider> createProvider(QObject * = nullptr) const override
    { return std::make_unique<CapturingProvider>(); }
};

// §4.3 — provider whose connect() succeeds but returns 0 calendar collections.
class ZeroCalendarsProvider : public IProvider {
    Q_OBJECT
public:
    QString id() const override { return QStringLiteral("zero-cal"); }
    QString kind() const override { return QStringLiteral("zero-cal"); }
    QString displayName() const override { return QStringLiteral("Zero Calendars"); }
    void load(const BackendConfiguration &) override {}
    BackendConfiguration save() const override {
        BackendConfiguration c; c.type = kind(); return c;
    }
    QWidget *createConfigWidget(QWidget *) override { return nullptr; }
    QFuture<bool> connect() override {
        QPromise<bool> p; auto f = p.future(); p.start(); p.addResult(true); p.finish(); return f;
    }
    void disconnect() override {}
    bool isConnected() const override { return false; }
    QList<CollectionInfo> collections() const override { return {}; }
    std::vector<ProviderBackendSpec> createBackends() override { return {}; }
};

class ZeroCalendarsContribution : public BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("zero-cal"); }
    QString displayName() const override { return QStringLiteral("Zero Calendars"); }
    QList<Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<IProvider> createProvider(QObject * = nullptr) const override
    { return std::make_unique<ZeroCalendarsProvider>(); }
};

// §4.3 — provider whose connect() succeeds and returns calendar collections.
class WithCalendarsProvider : public IProvider {
    Q_OBJECT
public:
    QString id() const override { return QStringLiteral("with-cal"); }
    QString kind() const override { return QStringLiteral("with-cal"); }
    QString displayName() const override { return QStringLiteral("With Calendars"); }
    void load(const BackendConfiguration &) override {}
    BackendConfiguration save() const override {
        BackendConfiguration c; c.type = kind(); return c;
    }
    QWidget *createConfigWidget(QWidget *) override { return nullptr; }
    QFuture<bool> connect() override {
        QPromise<bool> p; auto f = p.future(); p.start(); p.addResult(true); p.finish(); return f;
    }
    void disconnect() override {}
    bool isConnected() const override { return false; }
    QList<CollectionInfo> collections() const override {
        CollectionInfo a; a.id = QStringLiteral("cal-work");   a.name = QStringLiteral("Work");
        a.type = QStringLiteral("calendar");
        CollectionInfo b; b.id = QStringLiteral("cal-home");   b.name = QStringLiteral("Home");
        b.type = QStringLiteral("calendar");
        return { a, b };
    }
    std::vector<ProviderBackendSpec> createBackends() override { return {}; }
};

class WithCalendarsContribution : public BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("with-cal"); }
    QString displayName() const override { return QStringLiteral("With Calendars"); }
    QList<Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<IProvider> createProvider(QObject * = nullptr) const override
    { return std::make_unique<WithCalendarsProvider>(); }
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
    void testConnection_bridgesWidgetConfigIntoProvider();
    void autofillButtonAbsentWithoutProfiles();
    void autofillAppliesProfileToEmbeddedWidget();

    // WP-D7: §4.3 dialog coverage
    void zeroCalendars_showsPanelAndDisablesSave();
    void happyPath_result_and_selectedCollectionIds_roundTrip();
};

void TstProviderConfigDialog::testConnection_bridgesWidgetConfigIntoProvider()
{
    // The provider only connects if it received a URL via load(). The dialog
    // must pull that URL from the embedded config widget before connect();
    // otherwise the provider is empty and "connect" fails.
    BackendRegistry registry;
    registry.registerContribution(std::make_shared<BridgeStubContribution>());
    ProviderManager pm(&registry);
    Ui::ProviderConfigDialog dlg(&pm, &registry,
                                 Ui::ProviderConfigDialog::AddNew, {});

    QMetaObject::invokeMethod(&dlg, "onTestClicked");

    auto *status = dlg.findChild<QLabel*>(QStringLiteral("testStatusLabel"));
    QVERIFY(status != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("Connected")), 3000);
}

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

    // §4.4 error-details contract: the status label shows a human failure
    // summary, and the provider's raw error message is exposed through the
    // collapsible Details disclosure — not just an opaque "Failed".
    auto *status = dlg.findChild<QLabel*>(QStringLiteral("testStatusLabel"));
    QVERIFY2(status != nullptr, "dialog must expose a testStatusLabel");
    QTRY_VERIFY_WITH_TIMEOUT(
        status->text().contains(QStringLiteral("Connection failed")), 3000);

    auto *detailsBtn = dlg.findChild<QToolButton*>(QStringLiteral("testDetailsButton"));
    QVERIFY2(detailsBtn != nullptr, "dialog must expose a testDetailsButton");
    QVERIFY2(!detailsBtn->isHidden(), "Details disclosure must be offered on failure");

    auto *detailsText = dlg.findChild<QPlainTextEdit*>(QStringLiteral("testDetailsText"));
    QVERIFY2(detailsText != nullptr, "dialog must expose a testDetailsText");
    QVERIFY2(detailsText->toPlainText().contains(kMsg),
             "raw provider error must be surfaced in the details view");
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

void TstProviderConfigDialog::autofillButtonAbsentWithoutProfiles()
{
    BackendRegistry registry;
    registry.registerContribution(std::make_shared<CapturingContribution>());
    ProviderManager pm(&registry);
    Ui::ProviderConfigDialog dlg(&pm, &registry, Ui::ProviderConfigDialog::AddNew, {});
    // No setAutofillProfiles() call -> no button.
    QVERIFY(dlg.findChild<QPushButton*>(QStringLiteral("autofillButton")) == nullptr);
}

void TstProviderConfigDialog::autofillAppliesProfileToEmbeddedWidget()
{
    BackendRegistry registry;
    registry.registerContribution(std::make_shared<CapturingContribution>());
    ProviderManager pm(&registry);
    Ui::ProviderConfigDialog dlg(&pm, &registry, Ui::ProviderConfigDialog::AddNew, {});

    BackendConfiguration cfg;
    cfg.type = QStringLiteral("cap-dav");
    cfg.connectionParams[QStringLiteral("url")] = QStringLiteral("http://localhost:5232/testuser1/");
    cfg.connectionParams[QStringLiteral("username")] = QStringLiteral("testuser1");
    dlg.setAutofillProfiles({ { QStringLiteral("CalDAV (Radicale)"), cfg } });

    auto *btn = dlg.findChild<QPushButton*>(QStringLiteral("autofillButton"));
    QVERIFY(btn != nullptr);
    QVERIFY(btn->menu() != nullptr);
    QCOMPARE(btn->menu()->actions().size(), 1);

    // Trigger the menu action -> applyAutofillProfile.
    btn->menu()->actions().first()->trigger();

    auto *embed = dlg.findChild<QWidget*>(QStringLiteral("providerConfigEmbed"));
    QVERIFY(embed != nullptr);
    auto *cw = embed->findChild<CapturingConfigWidget*>();
    QVERIFY2(cw != nullptr, "embedded capturing widget expected after combo selects cap-dav");
    QCOMPARE(cw->configuration().connectionParams.value(QStringLiteral("url")).toString(),
             QStringLiteral("http://localhost:5232/testuser1/"));
}

// ---------------------------------------------------------------------------
// WP-D7: §4.3 coverage
// ---------------------------------------------------------------------------

void TstProviderConfigDialog::zeroCalendars_showsPanelAndDisablesSave()
{
    // §4.3: when connect() succeeds but no calendars are returned, the dialog
    // must show the noCalendarsPanel and disable the Save button.
    BackendRegistry registry;
    registry.registerContribution(std::make_shared<ZeroCalendarsContribution>());
    ProviderManager pm(&registry);
    Ui::ProviderConfigDialog dlg(&pm, &registry, Ui::ProviderConfigDialog::AddNew, {});

    QMetaObject::invokeMethod(&dlg, "onTestClicked");

    // Wait for the synchronous connect() result to be processed
    auto *status = dlg.findChild<QLabel*>(QStringLiteral("testStatusLabel"));
    QVERIFY(status != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(
        status->text().contains(QStringLiteral("no calendars")), 3000);

    // noCalendarsPanel must not be hidden (dialog itself isn't show()'n;
    // use !isHidden() which reflects the widget's own visibility flag).
    auto *panel = dlg.findChild<QFrame*>(QStringLiteral("noCalendarsPanel"));
    QVERIFY2(panel != nullptr, "noCalendarsPanel must be created after 0-calendar connect");
    QVERIFY2(!panel->isHidden(), "noCalendarsPanel must not be hidden");

    // Save button must be disabled
    auto *bb = dlg.findChild<QDialogButtonBox*>();
    QPushButton *saveBtn = nullptr;
    if (bb) saveBtn = bb->button(QDialogButtonBox::Save);
    QVERIFY2(saveBtn != nullptr, "dialog must have a Save button");
    QVERIFY2(!saveBtn->isEnabled(), "Save must be disabled when 0 calendars");
}

void TstProviderConfigDialog::happyPath_result_and_selectedCollectionIds_roundTrip()
{
    // §4.3 happy path: connect() succeeds with calendars → picker shown, Save
    // enabled. Selecting collection IDs via the picker and calling
    // selectedCollectionIds() returns them; result() returns the provider config.
    BackendRegistry registry;
    registry.registerContribution(std::make_shared<WithCalendarsContribution>());
    ProviderManager pm(&registry);
    Ui::ProviderConfigDialog dlg(&pm, &registry, Ui::ProviderConfigDialog::AddNew, {});

    QMetaObject::invokeMethod(&dlg, "onTestClicked");

    auto *status = dlg.findChild<QLabel*>(QStringLiteral("testStatusLabel"));
    QVERIFY(status != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(
        status->text().contains(QStringLiteral("Connected")), 3000);

    // Picker must be visible and Save enabled
    auto *picker = dlg.findChild<QWidget*>(QStringLiteral("collectionPicker"));
    QVERIFY2(picker != nullptr && !picker->isHidden(), "picker must not be hidden after connect");

    auto *bb = dlg.findChild<QDialogButtonBox*>();
    QPushButton *saveBtn = nullptr;
    if (bb) saveBtn = bb->button(QDialogButtonBox::Save);
    QVERIFY2(saveBtn != nullptr && saveBtn->isEnabled(), "Save must be enabled with calendars");

    // Select "cal-work" by checking its checkbox
    auto *chk = dlg.findChild<QCheckBox*>(QStringLiteral("collection-cal-work"));
    QVERIFY2(chk != nullptr, "checkbox for cal-work must exist in picker");
    chk->setChecked(true);

    // selectedCollectionIds() must return the checked ID
    const QStringList ids = dlg.selectedCollectionIds();
    QVERIFY2(ids.contains(QStringLiteral("cal-work")), "cal-work must be in selectedCollectionIds");

    // result() must return a valid BackendConfiguration from the provider
    const Sync::BackendConfiguration cfg = dlg.result();
    QCOMPARE(cfg.type, QStringLiteral("with-cal"));
}

QTEST_MAIN(TstProviderConfigDialog)
#include "tst_providerconfigdialog.moc"
