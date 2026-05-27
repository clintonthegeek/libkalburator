#include <QtTest>
#include "akonadirevisiondigest.h"
using namespace Kalburator::Sync;
class TestRevisionDigest : public QObject {
    Q_OBJECT
private slots:
    void stableRegardlessOfOrder();
    void changesWhenRevisionChanges();
    void emptyIsEmpty();
};
void TestRevisionDigest::stableRegardlessOfOrder() {
    QList<QPair<qint64,int>> a{{1,3},{2,5},{3,1}};
    QList<QPair<qint64,int>> b{{3,1},{1,3},{2,5}};
    QCOMPARE(computeRevisionDigest(a), computeRevisionDigest(b));
    QVERIFY(!computeRevisionDigest(a).isEmpty());
}
void TestRevisionDigest::changesWhenRevisionChanges() {
    QList<QPair<qint64,int>> a{{1,3},{2,5}};
    QList<QPair<qint64,int>> b{{1,3},{2,6}};
    QVERIFY(computeRevisionDigest(a) != computeRevisionDigest(b));
}
void TestRevisionDigest::emptyIsEmpty() {
    QCOMPARE(computeRevisionDigest({}), QString());
}
QTEST_GUILESS_MAIN(TestRevisionDigest)
#include "tst_akonadirevisiondigest.moc"
