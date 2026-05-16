#include "accountslistwidget.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace Kalburator::Ui {

AccountsListWidget::AccountsListWidget(QWidget *parent)
    : QWidget(parent)
{
    setLayout(new QVBoxLayout(this));
}

void AccountsListWidget::setAccounts(const QList<Sync::BackendConfiguration> &a)
{
    m_accounts = a;
    rebuild();
}

void AccountsListWidget::rebuild()
{
    while (auto *child = layout()->takeAt(0)) {
        delete child->widget();
        delete child;
    }

    for (const auto &cfg : m_accounts) {
        auto *row = new QWidget(this);
        auto *h = new QHBoxLayout(row);

        auto *enabled = new QCheckBox(row);
        enabled->setObjectName(QStringLiteral("enabled-%1").arg(cfg.id));
        enabled->setChecked(cfg.enabled);
        const QString rowId = cfg.id;
        QObject::connect(enabled, &QCheckBox::toggled, this,
            [this, rowId](bool on){ emit accountEnabledChanged(rowId, on); });
        h->addWidget(enabled);

        auto *label = new QLabel(
            QStringLiteral("%1 (%2)").arg(cfg.displayName, cfg.type), row);
        h->addWidget(label, /*stretch*/ 1);

        auto *editBtn = new QPushButton(tr("Edit…"), row);
        editBtn->setObjectName(QStringLiteral("edit-%1").arg(cfg.id));
        QObject::connect(editBtn, &QPushButton::clicked, this,
            [this, rowId]{ emit accountEditRequested(rowId); });
        h->addWidget(editBtn);

        auto *rmBtn = new QPushButton(tr("Remove"), row);
        rmBtn->setObjectName(QStringLiteral("remove-%1").arg(cfg.id));
        QObject::connect(rmBtn, &QPushButton::clicked, this,
            [this, rowId]{ emit accountRemoved(rowId); });
        h->addWidget(rmBtn);

        layout()->addWidget(row);
    }

    auto *addBtn = new QPushButton(tr("Add account…"), this);
    addBtn->setObjectName(QStringLiteral("addAccount"));
    QObject::connect(addBtn, &QPushButton::clicked, this,
                     &AccountsListWidget::accountAddRequested);
    layout()->addWidget(addBtn);
}

} // namespace Kalburator::Ui
