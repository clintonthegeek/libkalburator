#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "blobbaselinestore.h"

using Kalburator::Sync::BlobBaselineStore;

class TestBlobBaselineStorePerRecordKeys : public QObject
{
    Q_OBJECT

private slots:
    void tripleKey_isolatesByBackend();
    void tripleKey_isolatesByCollection();
    void flatAndTripleAreIndependent();
    void tripleKey_bulkCommit_returnsAll();
    void tripleKey_clearCollection_isolated();
    void existingFlatTable_unaffectedByTripleWrites();
};

// ---------------------------------------------------------------------------
// Slot 1: same recordId in different backends doesn't collide.
// ---------------------------------------------------------------------------
void TestBlobBaselineStorePerRecordKeys::tripleKey_isolatesByBackend()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BlobBaselineStore store(dir.filePath(QStringLiteral("test.kalburator-sync.db")));
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
    BlobBaselineStore store(dir.filePath(QStringLiteral("test.kalburator-sync.db")));
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
// Slot 3: flat and triple APIs operate on independent storage tables.
// ---------------------------------------------------------------------------
void TestBlobBaselineStorePerRecordKeys::flatAndTripleAreIndependent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BlobBaselineStore store(dir.filePath(QStringLiteral("test.kalburator-sync.db")));
    QVERIFY2(store.isOpen(), qUtf8Printable(store.lastError()));

    const QString mappingId  = QStringLiteral("mapping-flat");
    const QString recordId   = QStringLiteral("rec-shared");
    const QString hashFlat   = QStringLiteral("sha256:flat");
    const QString hashTriple = QStringLiteral("sha256:triple");

    // Write via flat API.
    QVERIFY(store.setBaseline(mappingId, recordId, hashFlat));

    // Read via triple API with empty backend/collection — should return empty.
    QCOMPARE(store.baselineHash(QStringLiteral(""), QStringLiteral(""), recordId), QString());

    // Write via triple API using empty backend/collection keys.
    QVERIFY(store.setBaseline(QStringLiteral(""), QStringLiteral(""), recordId, hashTriple));

    // Flat read must still return hashFlat (triple write didn't touch flat table).
    QCOMPARE(store.baselineHash(mappingId, recordId), hashFlat);

    // Triple read must return hashTriple.
    QCOMPARE(store.baselineHash(QStringLiteral(""), QStringLiteral(""), recordId), hashTriple);
}

// ---------------------------------------------------------------------------
// Slot 4: bulk commit via triple API; readback via baselineRecordIds + hash.
// ---------------------------------------------------------------------------
void TestBlobBaselineStorePerRecordKeys::tripleKey_bulkCommit_returnsAll()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BlobBaselineStore store(dir.filePath(QStringLiteral("test.kalburator-sync.db")));
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
// Slot 5: clearCollection removes only the targeted collection.
// ---------------------------------------------------------------------------
void TestBlobBaselineStorePerRecordKeys::tripleKey_clearCollection_isolated()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BlobBaselineStore store(dir.filePath(QStringLiteral("test.kalburator-sync.db")));
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

// ---------------------------------------------------------------------------
// Slot 6: exercising the triple API leaves the flat table's behavior unchanged.
// ---------------------------------------------------------------------------
void TestBlobBaselineStorePerRecordKeys::existingFlatTable_unaffectedByTripleWrites()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BlobBaselineStore store(dir.filePath(QStringLiteral("test.kalburator-sync.db")));
    QVERIFY2(store.isOpen(), qUtf8Printable(store.lastError()));

    // Seed the flat table.
    QVERIFY(store.setBaseline(QStringLiteral("m1"), QStringLiteral("r1"),
                              QStringLiteral("h-flat-1")));
    QVERIFY(store.setBaseline(QStringLiteral("m1"), QStringLiteral("r2"),
                              QStringLiteral("h-flat-2")));

    // Exercise the triple table.
    QVERIFY(store.setBaseline(QStringLiteral("be"), QStringLiteral("coll"),
                              QStringLiteral("r1"), QStringLiteral("h-triple")));

    QMap<QString, QString> bulk;
    bulk[QStringLiteral("r3")] = QStringLiteral("h3");
    QVERIFY(store.commitBaselines(QStringLiteral("be"),
                                  QStringLiteral("coll"), bulk));

    QVERIFY(store.clearCollection(QStringLiteral("be"),
                                  QStringLiteral("coll")));

    // Flat table must be completely unchanged.
    QCOMPARE(store.baselineHash(QStringLiteral("m1"), QStringLiteral("r1")),
             QStringLiteral("h-flat-1"));
    QCOMPARE(store.baselineHash(QStringLiteral("m1"), QStringLiteral("r2")),
             QStringLiteral("h-flat-2"));

    QStringList ids = store.baselineRecordIds(QStringLiteral("m1"));
    std::sort(ids.begin(), ids.end());
    QCOMPARE(ids.size(), 2);
    QCOMPARE(ids[0], QStringLiteral("r1"));
    QCOMPARE(ids[1], QStringLiteral("r2"));
}

QTEST_GUILESS_MAIN(TestBlobBaselineStorePerRecordKeys)
#include "tst_blob_baseline_store_per_record_keys.moc"
