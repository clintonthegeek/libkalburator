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
    void commitBaselinesBulkInsert();
    void commitBaselinesIsAtomic();

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

void TestBlobBaselineStore::commitBaselinesBulkInsert()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BlobBaselineStore store(dbPathIn(dir));
    QVERIFY(store.isOpen());

    QMap<QString, QString> batch;
    batch[QStringLiteral("rec-1")] = QStringLiteral("h1");
    batch[QStringLiteral("rec-2")] = QStringLiteral("h2");
    batch[QStringLiteral("rec-3")] = QStringLiteral("h3");

    QVERIFY(store.commitBaselines(QStringLiteral("m"), batch));

    QCOMPARE(store.baselineHash(QStringLiteral("m"),
                                QStringLiteral("rec-1")),
             QStringLiteral("h1"));
    QCOMPARE(store.baselineHash(QStringLiteral("m"),
                                QStringLiteral("rec-2")),
             QStringLiteral("h2"));
    QCOMPARE(store.baselineHash(QStringLiteral("m"),
                                QStringLiteral("rec-3")),
             QStringLiteral("h3"));
}

void TestBlobBaselineStore::commitBaselinesIsAtomic()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BlobBaselineStore store(dbPathIn(dir));
    QVERIFY(store.isOpen());

    // Seed a row.
    QVERIFY(store.setBaseline(QStringLiteral("m"),
                              QStringLiteral("existing"),
                              QStringLiteral("h-orig")));

    // Commit replaces the existing row and adds new ones.
    QMap<QString, QString> batch;
    batch[QStringLiteral("existing")] = QStringLiteral("h-new");
    batch[QStringLiteral("new-rec")]  = QStringLiteral("h-new2");
    QVERIFY(store.commitBaselines(QStringLiteral("m"), batch));

    QCOMPARE(store.baselineHash(QStringLiteral("m"),
                                QStringLiteral("existing")),
             QStringLiteral("h-new"));
    QCOMPARE(store.baselineHash(QStringLiteral("m"),
                                QStringLiteral("new-rec")),
             QStringLiteral("h-new2"));
}

QTEST_MAIN(TestBlobBaselineStore)
#include "tst_blobbaselinestore.moc"
