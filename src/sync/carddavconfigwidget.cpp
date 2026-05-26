#include "carddavconfigwidget.h"

#include <QFormLayout>
#include <QLineEdit>

namespace Kalburator::Sync {

CardDavConfigWidget::CardDavConfigWidget(QWidget *parent)
    : QWidget(parent)
    , m_displayNameEdit(new QLineEdit(this))
    , m_urlEdit(new QLineEdit(this))
    , m_usernameEdit(new QLineEdit(this))
    , m_passwordEdit(new QLineEdit(this))
{
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_displayNameEdit->setPlaceholderText(tr("My contacts"));
    // Bare host is fine — RFC 6764 .well-known discovery resolves the context path.
    m_urlEdit->setPlaceholderText(tr("https://cloud.example.com"));

    auto *form = new QFormLayout(this);
    form->addRow(tr("Display name:"), m_displayNameEdit);
    form->addRow(tr("Server URL:"),   m_urlEdit);
    form->addRow(tr("Username:"),     m_usernameEdit);
    form->addRow(tr("Password:"),     m_passwordEdit);
}

void CardDavConfigWidget::setConfiguration(const BackendConfiguration &cfg)
{
    m_displayNameEdit->setText(cfg.displayName);
    const auto &p = cfg.connectionParams;
    m_urlEdit->setText(p.value(QStringLiteral("url")).toString());
    m_usernameEdit->setText(p.value(QStringLiteral("username")).toString());
    m_passwordEdit->setText(p.value(QStringLiteral("password")).toString());
}

BackendConfiguration CardDavConfigWidget::configuration() const
{
    BackendConfiguration cfg;
    cfg.type        = QStringLiteral("carddav");   // matches CardDavProvider::kind()
    cfg.displayName = m_displayNameEdit->text();
    cfg.connectionParams[QStringLiteral("url")]      = m_urlEdit->text();
    cfg.connectionParams[QStringLiteral("username")] = m_usernameEdit->text();
    cfg.connectionParams[QStringLiteral("password")] = m_passwordEdit->text();
    // cfg.id left empty on purpose: IProvider::load() only overwrites the
    // provider's id when cfg.id is non-empty, so the provider keeps its UUID.
    return cfg;
}

} // namespace Kalburator::Sync
