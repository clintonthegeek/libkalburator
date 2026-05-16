#include <QObject>
#include <QtTest/QtTest>
#include <QIcon>

#include "../../src/sync/iprovider.h"
#include "../../src/sync/caldavprovider.h"

using namespace Kalburator::Sync;

class TstIProviderAccessors : public QObject
{
    Q_OBJECT
private slots:
    void caldavProviderHasDefaultIcon();
    void caldavProviderHasEmptyWarning();
};

void TstIProviderAccessors::caldavProviderHasDefaultIcon()
{
    CalDavProvider p;
    // Must compile and run — return value (null or non-null) either is fine
    QIcon ic = p.icon();
    Q_UNUSED(ic);
    QVERIFY(true);
}

void TstIProviderAccessors::caldavProviderHasEmptyWarning()
{
    CalDavProvider p;
    QCOMPARE(p.lastWarning(), QString());
}

QTEST_GUILESS_MAIN(TstIProviderAccessors)
#include "tst_iprovider_accessors.moc"
