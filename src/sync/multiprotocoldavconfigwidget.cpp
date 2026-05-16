#include "multiprotocoldavconfigwidget.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QVBoxLayout>

namespace Kalburator::Sync {

MultiProtocolDavConfigWidget::MultiProtocolDavConfigWidget(QWidget *parent)
    : QWidget(parent)
    , m_displayNameEdit(new QLineEdit(this))
    , m_urlEdit(new QLineEdit(this))
    , m_usernameEdit(new QLineEdit(this))
    , m_passwordEdit(new QLineEdit(this))
    , m_advancedGroup(new QGroupBox(tr("Advanced"), this))
    , m_manualCalDavEdit(new QLineEdit(m_advancedGroup))
    , m_manualCardDavEdit(new QLineEdit(m_advancedGroup))
{
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_displayNameEdit->setPlaceholderText(tr("My Nextcloud"));
    m_urlEdit->setPlaceholderText(tr("https://cloud.example.com"));

    m_manualCalDavEdit->setObjectName(QStringLiteral("manualCalDavEdit"));
    m_manualCardDavEdit->setObjectName(QStringLiteral("manualCardDavEdit"));
    m_manualCalDavEdit->setPlaceholderText(
        tr("Override CalDAV principal URL (leave blank for auto-probe)"));
    m_manualCardDavEdit->setPlaceholderText(
        tr("Override CardDAV principal URL (leave blank for auto-probe)"));

    // Advanced section: checkable group box; children hidden when unchecked.
    auto *advLayout = new QFormLayout(m_advancedGroup);
    advLayout->addRow(tr("CalDAV principal:"),  m_manualCalDavEdit);
    advLayout->addRow(tr("CardDAV principal:"), m_manualCardDavEdit);
    m_advancedGroup->setCheckable(true);
    m_advancedGroup->setChecked(false);

    // Qt's checkable QGroupBox only disables children, it doesn't hide them.
    // Explicitly hide the line edits so tests can assert isVisible() == false.
    m_manualCalDavEdit->setVisible(false);
    m_manualCardDavEdit->setVisible(false);
    QObject::connect(m_advancedGroup, &QGroupBox::toggled, this, [this](bool checked) {
        m_manualCalDavEdit->setVisible(checked);
        m_manualCardDavEdit->setVisible(checked);
    });

    auto *main = new QFormLayout(this);
    main->addRow(tr("Display name:"), m_displayNameEdit);
    main->addRow(tr("Server URL:"),   m_urlEdit);
    main->addRow(tr("Username:"),     m_usernameEdit);
    main->addRow(tr("Password:"),     m_passwordEdit);
    main->addRow(m_advancedGroup);
}

void MultiProtocolDavConfigWidget::setConfiguration(const BackendConfiguration &cfg)
{
    m_displayNameEdit->setText(cfg.displayName);
    const auto &p = cfg.connectionParams;
    m_urlEdit->setText(p.value(QStringLiteral("url")).toString());
    m_usernameEdit->setText(p.value(QStringLiteral("username")).toString());
    m_passwordEdit->setText(p.value(QStringLiteral("password")).toString());
    const QString mcal  = p.value(QStringLiteral("manualCaldavPrincipal")).toString();
    const QString mcard = p.value(QStringLiteral("manualCarddavPrincipal")).toString();
    m_manualCalDavEdit->setText(mcal);
    m_manualCardDavEdit->setText(mcard);
    if (!mcal.isEmpty() || !mcard.isEmpty()) {
        m_advancedGroup->setChecked(true);
        m_manualCalDavEdit->setVisible(true);
        m_manualCardDavEdit->setVisible(true);
    }
}

BackendConfiguration MultiProtocolDavConfigWidget::configuration() const
{
    BackendConfiguration cfg;
    cfg.type = QStringLiteral("multiproto-dav");
    cfg.displayName = m_displayNameEdit->text();
    cfg.connectionParams[QStringLiteral("url")]      = m_urlEdit->text();
    cfg.connectionParams[QStringLiteral("username")] = m_usernameEdit->text();
    cfg.connectionParams[QStringLiteral("password")] = m_passwordEdit->text();
    if (!m_manualCalDavEdit->text().isEmpty())
        cfg.connectionParams[QStringLiteral("manualCaldavPrincipal")]  = m_manualCalDavEdit->text();
    if (!m_manualCardDavEdit->text().isEmpty())
        cfg.connectionParams[QStringLiteral("manualCarddavPrincipal")] = m_manualCardDavEdit->text();
    return cfg;
}

} // namespace Kalburator::Sync
