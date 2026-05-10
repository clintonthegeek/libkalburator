#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "blobbaselinestore.h"

using Kalburator::Storage::BaselineStore;

class TestBlobBaselineStorePerRecordKeys : public QObject
{
    Q_OBJECT

private slots:
    void tripleKey_isolatesByBackend();
    void tripleKey_isolatesByCollection();
    void tripleKey_bulkCommit_returnsAll();
    void tripleKey_clearCollection_isolated();
};

// ---------------------------------------------------------------------------
// Slot 1: same recordId in different backends doesn't collide.
// ---------------------------------------------------------------------------
void TestBlobBaselineStorePerRecordKeys::tripleKey_isolatesByBackend()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BaselineStore store(dir.filePath(QStringLiteral("test.kalburator-sync.db")));
    QVERIFY2(store.isOpen(), qUtf8Printable(store.lastError()));

    const QString coll   = QStringLiteral("coll1");
    const QString rec    = QStringLiteral("rec1");
    const QString hashA  = QStringLiteral("sha256:aaaa");
    const QString hashB  = QStringLiteral("sha256:bbbb");

    QVERIFY(store.setBaseline(QStringLiteral("backendA"), coll, rec, hashA));
    QVERIFY(store.setBaseline(QStringLiteral("backendB"), coll, rec, hashB));

    QCOMPARE(store.baselineHash(QStringLiteral("backendA"), coll, rec), hashA);
    QCOMPARE(store.baselineHash(QStringLiteral("backendB"), coll, rec), hashB);
}

// ---------------------------------------------------------------------------
// Slot 2: same recordId in different collections doesn't collide.
// ---------------------------------------------------------------------------
void TestBlobBaselineStorePerRecordKeys::tripleKey_isolatesByCollection()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BaselineStore store(dir.filePath(QStringLiteral("test.kalburator-sync.db")));
    QVERIFY2(store.isOpen(), qUtf8Printable(store.lastError()));

    const QString backend = QStringLiteral("backend1");
    const QString rec     = QStringLiteral("rec1");
    const QString hashX   = QStringLiteral("sha256:xxxx");
    const QString hashY   = QStringLiteral("sha256:yyyy");

    QVERIFY(store.setBaseline(backend, QStringLiteral("collA"), rec, hashX));
    QVERIFY(store.setBaseline(backend, QStringLiteral("collB"), rec, hashY));

    QCOMPARE(store.baselineHash(backend, QStringLiteral("collA"), rec), hashX);
    QCOMPARE(store.baselineHash(backend, QStringLiteral("collB"), rec), hashY);
}

// ---------------------------------------------------------------------------
// Slot 3: bulk commit via triple API; readback via baselineRecordIds + hash.
// ---------------------------------------------------------------------------
void TestBlobBaselineStorePerRecordKeys::tripleKey_bulkCommit_returnsAll()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BaselineStore store(dir.filePath(QStringLiteral("test.kalburator-sync.db")));
    QVERIFY2(store.isOpen(), qUtf8Printable(store.lastError()));

    const QString backend = QStringLiteral("backend-bulk");
    const QString coll    = QStringLiteral("coll-bulk");

    QMap<QString, QString> batch;
    batch[QStringLiteral("uid-1")] = QStringLiteral("h1");
    batch[QStringLiteral("uid-2")] = QStringLiteral("h2");
    batch[QStringLiteral("uid-3")] = QStringLiteral("h3");

    QVERIFY(store.commitBaselines(backend, coll, batch));

    QStringList ids = store.baselineRecordIds(backend, coll);
    std::sort(ids.begin(), ids.end());
    QCOMPARE(ids.size(), 3);
    QCOMPARE(ids[0], QStringLiteral("uid-1"));
    QCOMPARE(ids[1], QStringLiteral("uid-2"));
    QCOMPARE(ids[2], QStringLiteral("uid-3"));

    QCOMPARE(store.baselineHash(backend, coll, QStringLiteral("uid-1")),
             QStringLiteral("h1"));
    QCOMPARE(store.baselineHash(backend, coll, QStringLiteral("uid-2")),
             QStringLiteral("h2"));
    QCOMPARE(store.baselineHash(backend, coll, QStringLiteral("uid-3")),
             QStringLiteral("h3"));
}

// ---------------------------------------------------------------------------
// Slot 4: clearCollection removes only the targeted collection.
// ---------------------------------------------------------------------------
void TestBlobBaselineStorePerRecordKeys::tripleKey_clearCollection_isolated()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BaselineStore store(dir.filePath(QStringLiteral("test.kalburator-sync.db")));
    QVERIFY2(store.isOpen(), qUtf8Printable(store.lastError()));

    const QString backend = QStringLiteral("backend-clear");
    const QString collA   = QStringLiteral("collA");
    const QString collB   = QStringLiteral("collB");

    QVERIFY(store.setBaseline(backend, collA, QStringLiteral("rec-a1"),
                              QStringLiteral("hA1")));
    QVERIFY(store.setBaseline(backend, collA, QStringLiteral("rec-a2"),
                              QStringLiteral("hA2")));
    QVERIFY(store.setBaseline(backend, collB, QStringLiteral("rec-b1"),
                              QStringLiteral("hB1")));

    QVERIFY(store.clearCollection(backend, collA));

    // collA must be empty.
    QVERIFY(store.baselineRecordIds(backend, collA).isEmpty());

    // collB must be untouched.
    const QStringList idsB = store.baselineRecordIds(backend, collB);
    QCOMPARE(idsB.size(), 1);
    QCOMPARE(idsB.first(), QStringLiteral("rec-b1"));
    QCOMPARE(store.baselineHash(backend, collB, QStringLiteral("rec-b1")),
             QStringLiteral("hB1"));
}

QTEST_GUILESS_MAIN(TestBlobBaselineStorePerRecordKeys)
#include "tst_baseline_store_per_record_keys.moc"
