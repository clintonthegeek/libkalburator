#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "blobbaselinestore.h"

using Kalburator::Sync::BlobBaselineStore;

class TestBlobBaselineStore : public QObject
{
    Q_OBJECT
private slots:
    void opensOnValidPath();
    void setBaselineAndReadBack();
    void baselineHashMissingReturnsEmpty();
    void setBaselineOverwritesExistingHash();

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

void TestBlobBaselineStore::setBaselineAndReadBack()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BlobBaselineStore store(dbPathIn(dir));
    QVERIFY(store.isOpen());

    QVERIFY(store.setBaseline(QStringLiteral("mapping-a"),
                              QStringLiteral("rec-1"),
                              QStringLiteral("sha256:abc")));
    QCOMPARE(store.baselineHash(QStringLiteral("mapping-a"),
                                QStringLiteral("rec-1")),
             QStringLiteral("sha256:abc"));
}

void TestBlobBaselineStore::baselineHashMissingReturnsEmpty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BlobBaselineStore store(dbPathIn(dir));
    QVERIFY(store.isOpen());

    QCOMPARE(store.baselineHash(QStringLiteral("nope"),
                                QStringLiteral("nope")),
             QString());
}

void TestBlobBaselineStore::setBaselineOverwritesExistingHash()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BlobBaselineStore store(dbPathIn(dir));
    QVERIFY(store.isOpen());

    QVERIFY(store.setBaseline(QStringLiteral("m"), QStringLiteral("r"),
                              QStringLiteral("v1")));
    QVERIFY(store.setBaseline(QStringLiteral("m"), QStringLiteral("r"),
                              QStringLiteral("v2")));
    QCOMPARE(store.baselineHash(QStringLiteral("m"),
                                QStringLiteral("r")),
             QStringLiteral("v2"));
}

QTEST_MAIN(TestBlobBaselineStore)
#include "tst_blobbaselinestore.moc"
