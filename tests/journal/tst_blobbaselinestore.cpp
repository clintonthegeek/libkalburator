#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "blobbaselinestore.h"

using Kalburator::Sync::BlobBaselineStore;

class TestBlobBaselineStore : public QObject
{
    Q_OBJECT
private slots:
    void opensOnValidPath();

    // (More slots added in subsequent tasks.)

private:
    QString dbPathIn(const QTemporaryDir &dir) const {
        return dir.filePath(QStringLiteral(".planstan-sync.db"));
    }
};

void TestBlobBaselineStore::opensOnValidPath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    BlobBaselineStore store(dbPathIn(dir));
    QVERIFY2(store.isOpen(),
             qUtf8Printable(store.lastError()));
    QCOMPARE(store.databasePath(), dbPathIn(dir));
}

QTEST_MAIN(TestBlobBaselineStore)
#include "tst_blobbaselinestore.moc"
