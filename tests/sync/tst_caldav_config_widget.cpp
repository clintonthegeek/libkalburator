// Phase H Task 7 — CalDavConfigWidget UI tests under offscreen QPA.
//
// Exercises the form widget: provider state populates the form on
// construct, applyToProvider() writes it back, and the Test Connection
// button drives provider.connect() against the FakeCalDavServer fixture
// (reused from Task 6).

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

#include "caldavconfigwidget.h"
#include "caldavprovider.h"
#include "backendconfiguration.h"
#include "fakecaldavserver.h"

using namespace Kalburator::Sync;

namespace {

BackendConfiguration makeConfig(const QString &url,
                                const QString &username,
                                const QString &password,
                                const QString &displayName)
{
    BackendConfiguration cfg;
    cfg.id = QStringLiteral("test-account");
    cfg.type = QStringLiteral("caldav");
    cfg.displayName = displayName;
    cfg.connectionParams.insert(QStringLiteral("url"), url);
    cfg.connectionParams.insert(QStringLiteral("username"), username);
    cfg.connectionParams.insert(QStringLiteral("password"), password);
    return cfg;
}

} // namespace

class TstCalDavConfigWidget : public QObject
{
    Q_OBJECT
private slots:
    void widget_shows_provider_state_on_construct();
    void apply_writes_form_values_back_to_provider();
    void edit_then_apply_round_trips();
    void test_button_against_fake_server_succeeds();
    void test_button_against_fake_server_fails_on_401();
};

void TstCalDavConfigWidget::widget_shows_provider_state_on_construct()
{
    CalDavProvider provider;
    provider.load(makeConfig(QStringLiteral("http://example.com/"),
                             QStringLiteral("alice"),
                             QStringLiteral("secret"),
                             QStringLiteral("My Server")));

    CalDavConfigWidget widget(&provider);

    QCOMPARE(widget.displayNameEditForTesting()->text(), QStringLiteral("My Server"));
    QCOMPARE(widget.urlEditForTesting()->text(),         QStringLiteral("http://example.com/"));
    QCOMPARE(widget.usernameEditForTesting()->text(),    QStringLiteral("alice"));
    QCOMPARE(widget.passwordEditForTesting()->text(),    QStringLiteral("secret"));
    // Password field uses Password echo mode but the actual text is preserved.
    QCOMPARE(widget.passwordEditForTesting()->echoMode(), QLineEdit::Password);
}

void TstCalDavConfigWidget::apply_writes_form_values_back_to_provider()
{
    CalDavProvider provider;
    // Empty starting config — load() is required so the provider has a
    // baseline id; otherwise the auto-generated UUID id round-trips.
    provider.load(makeConfig(QString(), QString(), QString(), QString()));

    CalDavConfigWidget widget(&provider);
    widget.displayNameEditForTesting()->setText(QStringLiteral("Renamed"));
    widget.urlEditForTesting()->setText(QStringLiteral("https://srv/dav"));
    widget.usernameEditForTesting()->setText(QStringLiteral("bob"));
    widget.passwordEditForTesting()->setText(QStringLiteral("hunter2"));

    widget.applyToProvider();

    const auto cfg = provider.save();
    QCOMPARE(cfg.displayName, QStringLiteral("Renamed"));
    QCOMPARE(cfg.connectionParams.value(QStringLiteral("url")).toString(),
             QStringLiteral("https://srv/dav"));
    QCOMPARE(cfg.connectionParams.value(QStringLiteral("username")).toString(),
             QStringLiteral("bob"));
    QCOMPARE(cfg.connectionParams.value(QStringLiteral("password")).toString(),
             QStringLiteral("hunter2"));
}

void TstCalDavConfigWidget::edit_then_apply_round_trips()
{
    CalDavProvider provider;
    provider.load(makeConfig(QStringLiteral("http://example.com/"),
                             QStringLiteral("alice"),
                             QStringLiteral("secret"),
                             QStringLiteral("Original")));

    CalDavConfigWidget widget(&provider);
    widget.displayNameEditForTesting()->setText(QStringLiteral("Updated"));
    widget.applyToProvider();

    const auto cfg = provider.save();
    QCOMPARE(cfg.displayName, QStringLiteral("Updated"));
    // Untouched fields preserved.
    QCOMPARE(cfg.connectionParams.value(QStringLiteral("url")).toString(),
             QStringLiteral("http://example.com/"));
    QCOMPARE(cfg.connectionParams.value(QStringLiteral("username")).toString(),
             QStringLiteral("alice"));
    QCOMPARE(cfg.connectionParams.value(QStringLiteral("password")).toString(),
             QStringLiteral("secret"));
}

void TstCalDavConfigWidget::test_button_against_fake_server_succeeds()
{
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    CalDavProvider provider;
    provider.load(makeConfig(server.baseUrl().toString(),
                             QStringLiteral("testuser"),
                             QStringLiteral("testpass"),
                             QStringLiteral("Account")));

    CalDavConfigWidget widget(&provider);
    QPushButton *btn  = widget.testButtonForTesting();
    QLabel      *lbl  = widget.statusLabelForTesting();

    QTest::mouseClick(btn, Qt::LeftButton);

    // Wait until the watcher reports finished and the label flips to
    // a "Connected" message. QTRY_VERIFY spins the event loop.
    QTRY_VERIFY_WITH_TIMEOUT(lbl->text().contains(QLatin1String("Connected")), 5000);
    QVERIFY(btn->isEnabled());
    QVERIFY(provider.isConnected());
}

void TstCalDavConfigWidget::test_button_against_fake_server_fails_on_401()
{
    FakeCalDavServer server;
    server.setReturn401(true);
    QVERIFY(server.startListening());

    CalDavProvider provider;
    provider.load(makeConfig(server.baseUrl().toString(),
                             QStringLiteral("testuser"),
                             QStringLiteral("testpass"),
                             QStringLiteral("Account")));

    CalDavConfigWidget widget(&provider);
    QPushButton *btn = widget.testButtonForTesting();
    QLabel      *lbl = widget.statusLabelForTesting();

    QTest::mouseClick(btn, Qt::LeftButton);

    QTRY_VERIFY_WITH_TIMEOUT(lbl->text().contains(QLatin1String("failed")), 5000);
    QVERIFY(btn->isEnabled());
    QVERIFY(!provider.isConnected());
}

QTEST_MAIN(TstCalDavConfigWidget)
#include "tst_caldav_config_widget.moc"
