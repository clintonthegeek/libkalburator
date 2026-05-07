#include "caldavconfigwidget.h"

#include "caldavprovider.h"
#include "backendconfiguration.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QFutureWatcher>

namespace Kalburator::Sync {

CalDavConfigWidget::CalDavConfigWidget(CalDavProvider *provider, QWidget *parent)
    : QWidget(parent)
    , m_provider(provider)
    , m_displayNameEdit(new QLineEdit(this))
    , m_urlEdit(new QLineEdit(this))
    , m_usernameEdit(new QLineEdit(this))
    , m_passwordEdit(new QLineEdit(this))
    , m_testButton(new QPushButton(tr("Test Connection"), this))
    , m_statusLabel(new QLabel(this))
{
    Q_ASSERT(m_provider);

    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_urlEdit->setPlaceholderText(tr("https://nextcloud.example.com/remote.php/dav"));

    auto *form = new QFormLayout;
    form->addRow(tr("Display name:"), m_displayNameEdit);
    form->addRow(tr("Server URL:"),   m_urlEdit);
    form->addRow(tr("Username:"),     m_usernameEdit);
    form->addRow(tr("Password:"),     m_passwordEdit);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_testButton);
    layout->addWidget(m_statusLabel);
    layout->addStretch();

    QObject::connect(m_testButton, &QPushButton::clicked,
                     this, &CalDavConfigWidget::onTestClicked);

    readFromProvider();
}

CalDavConfigWidget::~CalDavConfigWidget() = default;

void CalDavConfigWidget::readFromProvider() {
    const auto cfg = m_provider->save();
    m_displayNameEdit->setText(cfg.displayName);
    m_urlEdit->setText(cfg.connectionParams.value(QStringLiteral("url")).toString());
    m_usernameEdit->setText(cfg.connectionParams.value(QStringLiteral("username")).toString());
    m_passwordEdit->setText(cfg.connectionParams.value(QStringLiteral("password")).toString());
}

void CalDavConfigWidget::applyToProvider() {
    BackendConfiguration cfg = m_provider->save();
    cfg.displayName = m_displayNameEdit->text();
    cfg.connectionParams[QStringLiteral("url")]      = m_urlEdit->text();
    cfg.connectionParams[QStringLiteral("username")] = m_usernameEdit->text();
    cfg.connectionParams[QStringLiteral("password")] = m_passwordEdit->text();
    m_provider->load(cfg);
}

void CalDavConfigWidget::onTestClicked() {
    // Persist the current form values to the provider so connect() uses them.
    applyToProvider();

    m_testButton->setEnabled(false);
    m_statusLabel->setText(tr("Testing…"));

    auto *watcher = new QFutureWatcher<bool>(this);
    QObject::connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher]() {
        const bool ok = watcher->result();
        onTestFinished(ok);
        watcher->deleteLater();
    });
    watcher->setFuture(m_provider->connect());
}

void CalDavConfigWidget::onTestFinished(bool success) {
    m_testButton->setEnabled(true);
    m_statusLabel->setText(success
                           ? tr("Connected — %1 collection(s) found")
                                .arg(m_provider->collections().size())
                           : tr("Connection failed"));
}

QLineEdit  *CalDavConfigWidget::displayNameEditForTesting() const { return m_displayNameEdit; }
QLineEdit  *CalDavConfigWidget::urlEditForTesting() const         { return m_urlEdit; }
QLineEdit  *CalDavConfigWidget::usernameEditForTesting() const    { return m_usernameEdit; }
QLineEdit  *CalDavConfigWidget::passwordEditForTesting() const    { return m_passwordEdit; }
QPushButton *CalDavConfigWidget::testButtonForTesting() const     { return m_testButton; }
QLabel     *CalDavConfigWidget::statusLabelForTesting() const     { return m_statusLabel; }

} // namespace Kalburator::Sync
