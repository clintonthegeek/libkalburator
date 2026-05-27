#include <QtTest>
#include <QTemporaryDir>
#include "akonadirevisionstore.h"
using namespace Kalburator::Sync;
class TestRevisionStore : public QObject {
    Q_OBJECT
private slots:
    void persistsAcrossInstances();
    void missingIsEmpty();
};
void TestRevisionStore::persistsAcrossInstances() {
    QTemporaryDir dir;
    const QString path = dir.filePath("rev.ini");
    { AkonadiRevisionStore s(path); s.setToken("akonadi-1", "tok-abc"); }
    AkonadiRevisionStore s2(path);
    QCOMPARE(s2.token("akonadi-1"), QStringLiteral("tok-abc"));
}
void TestRevisionStore::missingIsEmpty() {
    QTemporaryDir dir;
    AkonadiRevisionStore s(dir.filePath("rev.ini"));
    QCOMPARE(s.token("nope"), QString());
}
QTEST_GUILESS_MAIN(TestRevisionStore)
#include "tst_akonadirevisionstore.moc"
