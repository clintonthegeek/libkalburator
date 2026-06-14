/// G.8 Task 61 — GenericSqliteBackend round-trip tests.

#include <QtTest/QtTest>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QtConcurrent>

#include <atomic>

#include "genericsqlitebackend.h"
#include "collectioninfo.h"
#include "backendrecord.h"
#include "shape.h"

using Kalburator::Sinks::GenericSqliteBackend;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::Shape;

namespace {

CollectionInfo makeCollection(const QString &id, const QString &name,
                              const QString &type = QStringLiteral("memos"))
{
    CollectionInfo ci;
    ci.id = id;
    ci.name = name;
    ci.type = type;
    return ci;
}

// K.9: universal sinks now require a per-collection shape. Storage
// round-trip tests don't care about shape semantics.
const Shape kTestShape{ DomainId{"test"}, EncodingId{"raw"} };

BackendRecord makeRecord(const QString &id, const QByteArray &data)
{
    BackendRecord r;
    r.id = id;
    r.displayName = id;
    r.data = data;
    r.contentHash = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    r.lastModified = QDateTime::currentDateTimeUtc();
    r.type = QStringLiteral("raw");
    return r;
}

} // namespace

class TestGenericSqliteBackend : public QObject
{
    Q_OBJECT
private slots:
    void isAvailable_falseForUnopenedDb();
    void createCollection_opensSqlite();
    void availableCollections_reflectsCreated();
    void createAndLoadRecord_roundTrip();
    void updateRecord_modifiesExisting();
    void deleteRecord_removesRow();
    void loadRecords_returnsAll();
    void clearCollection_emptiesTable();
    void clearCollection_reportsFailure_whenTableMissing();
    void multipleCollections_separateTables();
    void persistsAcrossInstances();
    void concurrentShapeForVsCreateCollection();
    void wipeCollection_emptiesTable_leavesCollectionIntact();
    void wipeCollection_survivorIsolation();

    // Sync::ChangeDetection (hub-side skip-unchanged support).
    void revision_stableUntilWrite();
    void revision_changesOnCreateUpdateDelete();
    void revision_primeCachedRoundTripAcrossInstances();
    void revision_unknownCollectionReturnsEmpty();
};

void TestGenericSqliteBackend::isAvailable_falseForUnopenedDb()
{
    GenericSqliteBackend b(QStringLiteral("/tmp/this_dir_does_not_exist_xyz/test.db"));
    QVERIFY(!b.isAvailable());
}

void TestGenericSqliteBackend::createCollection_opensSqlite()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    GenericSqliteBackend b(tmp.filePath(QStringLiteral("test.db")));
    const QString id = b.createCollection(
        makeCollection(QStringLiteral("memo+plaintext"), QStringLiteral("Memos")),
        kTestShape);
    QCOMPARE(id, QStringLiteral("memo+plaintext"));
    QVERIFY(b.isAvailable());
}

void TestGenericSqliteBackend::availableCollections_reflectsCreated()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    GenericSqliteBackend b(tmp.filePath(QStringLiteral("test.db")));
    b.createCollection(
        makeCollection(QStringLiteral("memo+plaintext"), QStringLiteral("Memos")),
        kTestShape);
    b.createCollection(
        makeCollection(QStringLiteral("contacts+vcard"),
                       QStringLiteral("Contacts"), QStringLiteral("contacts")),
        kTestShape);
    const auto cols = b.availableCollections();
    QCOMPARE(cols.size(), 2);
    QStringList ids;
    for (const auto &c : cols) ids << c.id;
    ids.sort();
    QCOMPARE(ids, QStringList({QStringLiteral("contacts+vcard"), QStringLiteral("memo+plaintext")}));
}

void TestGenericSqliteBackend::createAndLoadRecord_roundTrip()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    GenericSqliteBackend b(tmp.filePath(QStringLiteral("test.db")));
    const QString colId = QStringLiteral("memo+plaintext");
    b.createCollection(makeCollection(colId, QStringLiteral("Memos")), kTestShape);

    const QByteArray data = QByteArrayLiteral("hello sqlite");
    const QString id = b.createRecord(colId, makeRecord(QStringLiteral("r1"), data));
    QVERIFY(!id.isEmpty());

    const auto loaded = b.loadRecord(id);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->data, data);
}

void TestGenericSqliteBackend::updateRecord_modifiesExisting()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    GenericSqliteBackend b(tmp.filePath(QStringLiteral("test.db")));
    const QString colId = QStringLiteral("memo+plaintext");
    b.createCollection(makeCollection(colId, QStringLiteral("Memos")), kTestShape);

    const QString id = b.createRecord(colId,
        makeRecord(QStringLiteral("r1"), QByteArrayLiteral("original")));
    QVERIFY(!id.isEmpty());

    BackendRecord updated;
    updated.id = id;
    updated.data = QByteArrayLiteral("modified");
    updated.contentHash = QStringLiteral("new-hash");
    updated.lastModified = QDateTime::currentDateTimeUtc();
    QVERIFY(b.updateRecord(updated));

    const auto loaded = b.loadRecord(id);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->data, QByteArrayLiteral("modified"));
}

void TestGenericSqliteBackend::deleteRecord_removesRow()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    GenericSqliteBackend b(tmp.filePath(QStringLiteral("test.db")));
    const QString colId = QStringLiteral("memo+plaintext");
    b.createCollection(makeCollection(colId, QStringLiteral("Memos")), kTestShape);

    const QString id = b.createRecord(colId,
        makeRecord(QStringLiteral("r1"), QByteArrayLiteral("data")));
    QVERIFY(!id.isEmpty());
    QVERIFY(b.loadRecord(id).has_value());

    QVERIFY(b.deleteRecord(id));
    QVERIFY(!b.loadRecord(id).has_value());
}

void TestGenericSqliteBackend::loadRecords_returnsAll()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    GenericSqliteBackend b(tmp.filePath(QStringLiteral("test.db")));
    const QString colId = QStringLiteral("memo+plaintext");
    b.createCollection(makeCollection(colId, QStringLiteral("Memos")), kTestShape);

    b.createRecord(colId, makeRecord(QStringLiteral("r1"), QByteArrayLiteral("one")));
    b.createRecord(colId, makeRecord(QStringLiteral("r2"), QByteArrayLiteral("two")));
    b.createRecord(colId, makeRecord(QStringLiteral("r3"), QByteArrayLiteral("three")));

    QCOMPARE(b.loadRecords(colId).size(), 3);
}

void TestGenericSqliteBackend::clearCollection_emptiesTable()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    GenericSqliteBackend b(tmp.filePath(QStringLiteral("test.db")));
    const QString colId = QStringLiteral("memo+plaintext");
    b.createCollection(makeCollection(colId, QStringLiteral("Memos")), kTestShape);

    b.createRecord(colId, makeRecord(QStringLiteral("r1"), QByteArrayLiteral("a")));
    b.createRecord(colId, makeRecord(QStringLiteral("r2"), QByteArrayLiteral("b")));
    b.clearCollection(colId);

    QCOMPARE(b.loadRecords(colId).size(), 0);
}

void TestGenericSqliteBackend::clearCollection_reportsFailure_whenTableMissing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    GenericSqliteBackend be(dir.filePath(QStringLiteral("test.sqlite")));

    // No collection "ghost" was ever created, so its table does not exist;
    // DELETE FROM "ghost" must fail and clearCollection must report it.
    QVERIFY(!be.clearCollection(QStringLiteral("ghost")));
    QVERIFY(!be.deleteCollection(QStringLiteral("ghost")));  // clearCollection leg fails -> false

    // Sanity: clearing a real, empty collection succeeds, and deleting it succeeds.
    be.createCollection(
        makeCollection(QStringLiteral("real"), QStringLiteral("Real"), QStringLiteral("memo")),
        kTestShape);
    QVERIFY(be.clearCollection(QStringLiteral("real")));
    QVERIFY(be.deleteCollection(QStringLiteral("real")));
}

void TestGenericSqliteBackend::multipleCollections_separateTables()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    GenericSqliteBackend b(tmp.filePath(QStringLiteral("test.db")));

    const QString memoCol = QStringLiteral("memo+plaintext");
    const QString todoCol = QStringLiteral("todo+ical");
    b.createCollection(makeCollection(memoCol, QStringLiteral("Memos")), kTestShape);
    b.createCollection(
        makeCollection(todoCol, QStringLiteral("Todos"), QStringLiteral("todos")),
        kTestShape);

    b.createRecord(memoCol, makeRecord(QStringLiteral("m1"), QByteArrayLiteral("memo")));
    b.createRecord(todoCol, makeRecord(QStringLiteral("t1"), QByteArrayLiteral("todo")));

    QCOMPARE(b.loadRecords(memoCol).size(), 1);
    QCOMPARE(b.loadRecords(todoCol).size(), 1);

    b.clearCollection(memoCol);
    QCOMPARE(b.loadRecords(memoCol).size(), 0);
    QCOMPARE(b.loadRecords(todoCol).size(), 1);
}

void TestGenericSqliteBackend::persistsAcrossInstances()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath = tmp.filePath(QStringLiteral("persist.db"));
    {
        GenericSqliteBackend b(dbPath);
        b.createCollection(
            makeCollection(QStringLiteral("memo+plaintext"), QStringLiteral("Memos")),
            kTestShape);
        b.createRecord(QStringLiteral("memo+plaintext"),
                       makeRecord(QStringLiteral("r1"), QByteArrayLiteral("persisted")));
    }
    GenericSqliteBackend b2(dbPath);
    const auto cols = b2.availableCollections();
    QCOMPARE(cols.size(), 1);
    const auto records = b2.loadRecords(QStringLiteral("memo+plaintext"));
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.first().data, QByteArrayLiteral("persisted"));
}

void TestGenericSqliteBackend::concurrentShapeForVsCreateCollection()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    GenericSqliteBackend be(dir.filePath(QStringLiteral("test.sqlite")));

    std::atomic<bool> stop{false};
    QFuture<void> reader = QtConcurrent::run([&] {
        while (!stop.load(std::memory_order_acquire)) {
            be.shapeFor(QStringLiteral("c-7"));
            be.nativeShapes();
        }
    });
    for (int i = 0; i < 200; ++i) {
        be.createCollection(
            makeCollection(QStringLiteral("c-%1").arg(i), QStringLiteral("C"),
                           QStringLiteral("memo")),
            Shape::Any());
    }
    stop.store(true, std::memory_order_release);
    reader.waitForFinished();
    QVERIFY(true);  // reaching here without crash/TSan report is the assertion
}

void TestGenericSqliteBackend::wipeCollection_emptiesTable_leavesCollectionIntact()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    GenericSqliteBackend b(tmp.filePath(QStringLiteral("test.db")));
    const QString colId = QStringLiteral("memo+plaintext");
    b.createCollection(makeCollection(colId, QStringLiteral("Memos")), kTestShape);

    b.createRecord(colId, makeRecord(QStringLiteral("w1"), QByteArrayLiteral("x")));
    b.createRecord(colId, makeRecord(QStringLiteral("w2"), QByteArrayLiteral("y")));
    QCOMPARE(b.loadRecords(colId).size(), 2);

    QVERIFY(b.wipeCollection(colId));
    QCOMPARE(b.loadRecords(colId).size(), 0);
    // Collection itself must still exist (wipe ≠ delete).
    const auto cols = b.availableCollections();
    QVERIFY(std::any_of(cols.begin(), cols.end(),
                        [&](const CollectionInfo &c){ return c.id == colId; }));
}

void TestGenericSqliteBackend::wipeCollection_survivorIsolation()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    GenericSqliteBackend b(tmp.filePath(QStringLiteral("test.db")));
    const QString wiped   = QStringLiteral("col+a");
    const QString survivor = QStringLiteral("col+b");
    b.createCollection(makeCollection(wiped,    QStringLiteral("A")), kTestShape);
    b.createCollection(makeCollection(survivor, QStringLiteral("B")), kTestShape);

    b.createRecord(wiped,    makeRecord(QStringLiteral("w1"), QByteArrayLiteral("gone")));
    b.createRecord(survivor, makeRecord(QStringLiteral("s1"), QByteArrayLiteral("safe")));

    QVERIFY(b.wipeCollection(wiped));
    QCOMPARE(b.loadRecords(wiped).size(),    0);
    QCOMPARE(b.loadRecords(survivor).size(), 1);
    QCOMPARE(b.loadRecords(survivor).first().data, QByteArrayLiteral("safe"));
}

// ---- Sync::ChangeDetection ----

void TestGenericSqliteBackend::revision_stableUntilWrite()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    GenericSqliteBackend b(tmp.filePath(QStringLiteral("test.db")));
    const QString colId = QStringLiteral("memo+plaintext");
    b.createCollection(makeCollection(colId, QStringLiteral("Memos")), kTestShape);

    // A freshly-created collection reports a stable, non-empty token, and
    // repeated reads without an intervening write do not change it.
    const QString r0 = b.collectionRevision(colId);
    QVERIFY(!r0.isEmpty());
    QCOMPARE(b.collectionRevision(colId), r0);
    QCOMPARE(b.collectionRevision(colId), r0);
}

void TestGenericSqliteBackend::revision_changesOnCreateUpdateDelete()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    GenericSqliteBackend b(tmp.filePath(QStringLiteral("test.db")));
    const QString colId = QStringLiteral("memo+plaintext");
    b.createCollection(makeCollection(colId, QStringLiteral("Memos")), kTestShape);

    const QString rStart = b.collectionRevision(colId);

    const QString id = b.createRecord(colId,
        makeRecord(QStringLiteral("r1"), QByteArrayLiteral("one")));
    QVERIFY(!id.isEmpty());
    const QString rAfterCreate = b.collectionRevision(colId);
    QVERIFY2(rAfterCreate != rStart, "create must bump the revision");

    BackendRecord upd = makeRecord(QStringLiteral("r1"), QByteArrayLiteral("two"));
    upd.id = id;
    QVERIFY(b.updateRecord(upd));
    const QString rAfterUpdate = b.collectionRevision(colId);
    QVERIFY2(rAfterUpdate != rAfterCreate, "update must bump the revision");

    QVERIFY(b.deleteRecord(id));
    const QString rAfterDelete = b.collectionRevision(colId);
    QVERIFY2(rAfterDelete != rAfterUpdate, "delete must bump the revision");

    // clearCollection also bumps.
    b.createRecord(colId, makeRecord(QStringLiteral("r2"), QByteArrayLiteral("x")));
    const QString rBeforeClear = b.collectionRevision(colId);
    QVERIFY(b.clearCollection(colId));
    QVERIFY2(b.collectionRevision(colId) != rBeforeClear,
             "clearCollection must bump the revision");
}

void TestGenericSqliteBackend::revision_primeCachedRoundTripAcrossInstances()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath = tmp.filePath(QStringLiteral("rev-persist.db"));
    const QString colId = QStringLiteral("memo+plaintext");

    QString fresh;
    {
        GenericSqliteBackend b(dbPath);
        b.createCollection(makeCollection(colId, QStringLiteral("Memos")), kTestShape);
        b.createRecord(colId, makeRecord(QStringLiteral("r1"), QByteArrayLiteral("a")));

        // No baseline yet → cached is empty (engine treats as changed).
        QVERIFY(b.cachedCollectionRevision(colId).isEmpty());

        // The engine primes the cache with the fresh token after a sync.
        fresh = b.collectionRevision(colId);
        QVERIFY(!fresh.isEmpty());
        b.primeRevisionCache({{colId, fresh}});
        QCOMPARE(b.cachedCollectionRevision(colId), fresh);
    }

    // New instance over the same file: the primed baseline persists, and
    // since nothing changed the fresh token still equals the cached one
    // (the skip condition the engine checks).
    GenericSqliteBackend b2(dbPath);
    QCOMPARE(b2.cachedCollectionRevision(colId), fresh);
    QCOMPARE(b2.collectionRevision(colId), fresh);

    // A write makes fresh diverge from cached again.
    b2.createRecord(colId, makeRecord(QStringLiteral("r2"), QByteArrayLiteral("b")));
    QVERIFY(b2.collectionRevision(colId) != b2.cachedCollectionRevision(colId));
}

void TestGenericSqliteBackend::revision_unknownCollectionReturnsEmpty()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    GenericSqliteBackend b(tmp.filePath(QStringLiteral("test.db")));
    // A collection that was never created → "can't answer" → empty token.
    QVERIFY(b.collectionRevision(QStringLiteral("never+made")).isEmpty());
    QVERIFY(b.cachedCollectionRevision(QStringLiteral("never+made")).isEmpty());
}

QTEST_MAIN(TestGenericSqliteBackend)
#include "tst_generic_sqlite_backend.moc"
