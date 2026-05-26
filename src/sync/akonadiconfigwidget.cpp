#ifdef HAVE_AKONADI

#include "akonadiconfigwidget.h"
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

namespace Kalburator::Sync {

AkonadiConfigWidget::AkonadiConfigWidget(QWidget *parent)
    : QWidget(parent)
    , m_name(new QLineEdit(QStringLiteral("Local Akonadi"), this))
{
    auto *form = new QFormLayout(this);
    form->addRow(QStringLiteral("Display name:"), m_name);
    auto *info = new QLabel(
        QStringLiteral("Discovers calendars and address books from your local "
                        "Akonadi service. Configure resources via System Settings "
                        "→ KDE PIM → Akonadi Resources."),
        this);
    info->setWordWrap(true);
    form->addRow(info);
}

BackendConfiguration AkonadiConfigWidget::configuration() const {
    BackendConfiguration cfg;
    cfg.type        = QStringLiteral("akonadi");
    cfg.displayName = m_name->text();
    return cfg;
}

void AkonadiConfigWidget::setConfiguration(const BackendConfiguration &cfg) {
    m_name->setText(cfg.displayName);
}

QString AkonadiConfigWidget::displayName() const { return m_name->text(); }
void AkonadiConfigWidget::setDisplayName(const QString &name) { m_name->setText(name); }

} // namespace Kalburator::Sync

#endif // HAVE_AKONADI
