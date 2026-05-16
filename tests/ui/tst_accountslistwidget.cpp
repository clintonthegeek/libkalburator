#include <QObject>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QCheckBox>
#include <QPushButton>

#include "accountslistwidget.h"
#include "backendconfiguration.h"

using namespace Kalburator;
using namespace Kalburator::Sync;

class TstAccountsListWidget : public QObject
{
    Q_OBJECT
private slots:
    void rendersOneRowPerAccount();
    void toggleEmitsEnabledChanged();
    void removeButtonEmitsRemoved();
};

static QList<BackendConfiguration> makeFixture()
{
    BackendConfiguration a, b;
    a.id = QStringLiteral("acct-1");
    a.type = QStringLiteral("caldav");
    a.displayName = QStringLiteral("Work CalDAV");
    a.enabled = true;
    b.id = QStringLiteral("acct-2");
    b.type = QStringLiteral("multiproto-dav");
    b.displayName = QStringLiteral("Nextcloud");
    b.enabled = false;
    return { a, b };
}

void TstAccountsListWidget::rendersOneRowPerAccount()
{
    Ui::AccountsListWidget w;
    w.setAccounts(makeFixture());
    QVERIFY(w.findChild<QPushButton*>(QStringLiteral("remove-acct-1")) != nullptr);
    QVERIFY(w.findChild<QPushButton*>(QStringLiteral("remove-acct-2")) != nullptr);
}

void TstAccountsListWidget::toggleEmitsEnabledChanged()
{
    Ui::AccountsListWidget w;
    w.setAccounts(makeFixture());
    QSignalSpy spy(&w, &Ui::AccountsListWidget::accountEnabledChanged);
    auto *cb = w.findChild<QCheckBox*>(QStringLiteral("enabled-acct-1"));
    QVERIFY(cb != nullptr);
    cb->setChecked(false);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("acct-1"));
    QCOMPARE(spy.first().at(1).toBool(),   false);
}

void TstAccountsListWidget::removeButtonEmitsRemoved()
{
    Ui::AccountsListWidget w;
    w.setAccounts(makeFixture());
    QSignalSpy spy(&w, &Ui::AccountsListWidget::accountRemoved);
    auto *btn = w.findChild<QPushButton*>(QStringLiteral("remove-acct-2"));
    QVERIFY(btn != nullptr);
    btn->click();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("acct-2"));
}

QTEST_MAIN(TstAccountsListWidget)
#include "tst_accountslistwidget.moc"
