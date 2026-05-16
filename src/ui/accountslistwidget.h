#ifndef KALBURATOR_UI_ACCOUNTSLISTWIDGET_H
#define KALBURATOR_UI_ACCOUNTSLISTWIDGET_H

#include "backendconfiguration.h"

#include <QWidget>
#include <QList>

namespace Kalburator::Ui {

class AccountsListWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AccountsListWidget(QWidget *parent = nullptr);

    void setAccounts(const QList<Sync::BackendConfiguration> &accounts);

signals:
    void accountAddRequested();
    void accountEditRequested(const QString &id);
    void accountRemoved(const QString &id);
    void accountEnabledChanged(const QString &id, bool enabled);

private:
    void rebuild();
    QList<Sync::BackendConfiguration> m_accounts;
};

} // namespace Kalburator::Ui

#endif
