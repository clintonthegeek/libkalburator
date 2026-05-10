#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>

#include "baselinestore.h"
#include "canonicalrecord.h"

using Kalburator::Storage::BaselineStore;
using Kalburator::Shape::CanonicalRecord;
using Kalburator::Shape::Shape;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;

namespace {

CanonicalRecord makeRecord(const QString &recordId,
                            const QByteArray &data = "bytes")
{
    CanonicalRecord r;
    r.recordId = recordId;
    r.shape    = Shape{DomainId{QStringLiteral("calendar")},
                        EncodingId{QStringLiteral("ical")}};
    r.data     = data;
    return r;
}

} // namespace

class TestBlobBaselineStoreV3 : public QObject
{
    Q_OBJECT

private slots:
    void freshDb_hasV3Table();
    void setAndGet_roundtrip();
    void multipleRecordsPerMapping();
    void remove_byRecord();
    void clearMapping_removesAll();
    void differentMappings_isolated();
    void migrate_fromV2_withResolver();
    void migrate_fromV2_noResolver_dropsData();
    void migrate_idempotent_reopenSafe();
    void migrate_orphanRow_skipped();
};

// ───────────────────────────────────────────────────────────────────────────
// 1. Fresh DB: blob_baselines_v3 table exists and user_version == 5.
// ───────────────────────────────────────────────────────────────────────────
void TestBlobBaselineStoreV3::freshDb_hasV3Table()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("test.db"));

    {
        BaselineStore store(dbPath);
        QVERIFY2(store.isOpen(), qUtf8Printable(store.lastError()));
    }

    // Inspect directly via a separate Qt SQL connection.
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), QStringLiteral("inspect_fresh"));
        db.setDatabaseName(dbPath);
        QVERIFY(db.open());
        {
            QSqlQuery q(db);
            q.exec(QStringLiteral("PRAGMA user_version"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 5);  // K.5: schema v5 (collection_baselines added)

            q.exec(QStringLiteral(
                "SELECT name FROM sqlite_master WHERE type='table' "
                "AND name='blob_baselines_v3'"));
            QVERIFY(q.next());
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("inspect_fresh"));
}

// ───────────────────────────────────────────────────────────────────────────
// 2. Write one record; read it back.
// ───────────────────────────────────────────────────────────────────────────
void TestBlobBaselineStoreV3::setAndGet_roundtrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BaselineStore store(dir.filePath(QStringLiteral("t.db")));
    QVERIFY(store.isOpen());

    const QString mid = QStringLiteral("mapping-1");
    const auto rec = makeRecord(QStringLiteral("rec-1"), "icaldata");

    QVERIFY(store.setBaselineV3(mid, rec));

    const auto got = store.baselineV3(mid, QStringLiteral("rec-1"));
    QVERIFY(got.has_value());
    QCOMPARE(got->recordId, rec.recordId);
    QCOMPARE(got->data,     rec.data);
    QCOMPARE(got->shape.domain.toString(),   rec.shape.domain.toString());
    QCOMPARE(got->shape.encoding.toString(), rec.shape.encoding.toString());
}

// ───────────────────────────────────────────────────────────────────────────
// 3. Multiple records per mapping; baselinesForMappingV3 returns all.
// ───────────────────────────────────────────────────────────────────────────
void TestBlobBaselineStoreV3::multipleRecordsPerMapping()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BaselineStore store(dir.filePath(QStringLiteral("t.db")));
    QVERIFY(store.isOpen());

    const QString mid = QStringLiteral("mapping-bulk");
    QVERIFY(store.setBaselineV3(mid, makeRecord(QStringLiteral("r1"), "d1")));
    QVERIFY(store.setBaselineV3(mid, makeRecord(QStringLiteral("r2"), "d2")));
    QVERIFY(store.setBaselineV3(mid, makeRecord(QStringLiteral("r3"), "d3")));

    const auto all = store.baselinesForMappingV3(mid);
    QCOMPARE(all.size(), 3);

    QStringList ids;
    for (const auto &r : all) ids.append(r.recordId);
    ids.sort();
    QCOMPARE(ids, QStringList({"r1", "r2", "r3"}));
}

// ───────────────────────────────────────────────────────────────────────────
// 4. removeBaselineV3 removes one record.
// ───────────────────────────────────────────────────────────────────────────
void TestBlobBaselineStoreV3::remove_byRecord()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BaselineStore store(dir.filePath(QStringLiteral("t.db")));
    QVERIFY(store.isOpen());

    const QString mid = QStringLiteral("m");
    QVERIFY(store.setBaselineV3(mid, makeRecord(QStringLiteral("r1"))));
    QVERIFY(store.setBaselineV3(mid, makeRecord(QStringLiteral("r2"))));

    QVERIFY(store.removeBaselineV3(mid, QStringLiteral("r1")));
    QVERIFY(!store.baselineV3(mid, QStringLiteral("r1")).has_value());
    QVERIFY(store.baselineV3(mid, QStringLiteral("r2")).has_value());
}

// ───────────────────────────────────────────────────────────────────────────
// 5. clearMappingV3 removes all records for the mapping.
// ───────────────────────────────────────────────────────────────────────────
void TestBlobBaselineStoreV3::clearMapping_removesAll()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BaselineStore store(dir.filePath(QStringLiteral("t.db")));
    QVERIFY(store.isOpen());

    const QString mid = QStringLiteral("m");
    QVERIFY(store.setBaselineV3(mid, makeRecord(QStringLiteral("r1"))));
    QVERIFY(store.setBaselineV3(mid, makeRecord(QStringLiteral("r2"))));

    QVERIFY(store.clearMappingV3(mid));
    QVERIFY(store.baselinesForMappingV3(mid).isEmpty());
}

// ───────────────────────────────────────────────────────────────────────────
// 6. Different mappings are isolated; same record_id doesn't collide.
// ───────────────────────────────────────────────────────────────────────────
void TestBlobBaselineStoreV3::differentMappings_isolated()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BaselineStore store(dir.filePath(QStringLiteral("t.db")));
    QVERIFY(store.isOpen());

    QVERIFY(store.setBaselineV3(QStringLiteral("m1"),
                                 makeRecord(QStringLiteral("rec"), "aaa")));
    QVERIFY(store.setBaselineV3(QStringLiteral("m2"),
                                 makeRecord(QStringLiteral("rec"), "bbb")));

    const auto got1 = store.baselineV3(QStringLiteral("m1"), QStringLiteral("rec"));
    const auto got2 = store.baselineV3(QStringLiteral("m2"), QStringLiteral("rec"));
    QVERIFY(got1.has_value());
    QVERIFY(got2.has_value());
    QCOMPARE(got1->data, QByteArray("aaa"));
    QCOMPARE(got2->data, QByteArray("bbb"));
}

// ───────────────────────────────────────────────────────────────────────────
// 7. Migrate from v2 DB with a resolver: rows appear in blob_baselines_v3.
// ───────────────────────────────────────────────────────────────────────────
void TestBlobBaselineStoreV3::migrate_fromV2_withResolver()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("t.db"));

    // Manually construct a "v2" DB (user_version=3, blob_baselines triple-keyed).
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), QStringLiteral("setup_v2"));
        db.setDatabaseName(dbPath);
        QVERIFY(db.open());
        {
            QSqlQuery q(db);
            q.exec(QStringLiteral(
                "CREATE TABLE blob_baselines ("
                "  backend_id TEXT, collection_id TEXT, record_id TEXT,"
                "  content_hash TEXT, updated_at TEXT,"
                "  PRIMARY KEY(backend_id, collection_id, record_id))"));
            q.exec(QStringLiteral(
                "INSERT INTO blob_baselines VALUES "
                "('b1','c1','r1','hash1',datetime('now'))"));
            q.exec(QStringLiteral(
                "INSERT INTO blob_baselines VALUES "
                "('b1','c1','r2','hash2',datetime('now'))"));
            q.exec(QStringLiteral("PRAGMA user_version = 3"));
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("setup_v2"));

    // Open with a resolver that maps (b1, c1) → mapping-A.
    BaselineStore store(dbPath);
    QVERIFY2(store.isOpen(), qUtf8Printable(store.lastError()));

    store.setMappingResolver([](const QString &bId, const QString &cId) {
        if (bId == QLatin1String("b1") && cId == QLatin1String("c1"))
            return QStringList{QStringLiteral("mapping-A")};
        return QStringList{};
    });
    QVERIFY(store.migrateV3());

    const auto all = store.baselinesForMappingV3(QStringLiteral("mapping-A"));
    QCOMPARE(all.size(), 2);
    QStringList ids;
    for (const auto &r : all) ids.append(r.recordId);
    ids.sort();
    QCOMPARE(ids, QStringList({"r1", "r2"}));
}

// ───────────────────────────────────────────────────────────────────────────
// 8. Migrate from v2 DB without resolver: no data, but store is usable.
// ───────────────────────────────────────────────────────────────────────────
void TestBlobBaselineStoreV3::migrate_fromV2_noResolver_dropsData()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("t.db"));

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), QStringLiteral("setup_v2b"));
        db.setDatabaseName(dbPath);
        QVERIFY(db.open());
        {
            QSqlQuery q(db);
            q.exec(QStringLiteral(
                "CREATE TABLE blob_baselines ("
                "  backend_id TEXT, collection_id TEXT, record_id TEXT,"
                "  content_hash TEXT, updated_at TEXT,"
                "  PRIMARY KEY(backend_id, collection_id, record_id))"));
            q.exec(QStringLiteral(
                "INSERT INTO blob_baselines VALUES "
                "('b1','c1','r1','h1',datetime('now'))"));
            q.exec(QStringLiteral("PRAGMA user_version = 3"));
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("setup_v2b"));

    BaselineStore store(dbPath);
    QVERIFY(store.isOpen());
    // No resolver set — migrateV3 must succeed (graceful degradation).
    QVERIFY(store.migrateV3());
    // No rows migrated, but new API still works.
    QVERIFY(store.baselinesForMappingV3(QStringLiteral("any")).isEmpty());
    // Can still write new records.
    QVERIFY(store.setBaselineV3(QStringLiteral("m"), makeRecord(QStringLiteral("new"))));
}

// ───────────────────────────────────────────────────────────────────────────
// 9. Idempotent: open, close, reopen a v3 DB — second open is a no-op.
// ───────────────────────────────────────────────────────────────────────────
void TestBlobBaselineStoreV3::migrate_idempotent_reopenSafe()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("t.db"));

    // First open — fresh, user_version → 5 (K.5 schema).
    {
        BaselineStore store(dbPath);
        QVERIFY(store.isOpen());
        QVERIFY(store.setBaselineV3(QStringLiteral("m"),
                                     makeRecord(QStringLiteral("r1"), "v1")));
    }

    // Second open — should not migrate or alter data.
    {
        BaselineStore store(dbPath);
        QVERIFY2(store.isOpen(), qUtf8Printable(store.lastError()));
        const auto got = store.baselineV3(QStringLiteral("m"), QStringLiteral("r1"));
        QVERIFY(got.has_value());
        QCOMPARE(got->data, QByteArray("v1"));
    }
}

// ───────────────────────────────────────────────────────────────────────────
// 10. Orphan v2 row (no mapping) is skipped; other rows are migrated.
// ───────────────────────────────────────────────────────────────────────────
void TestBlobBaselineStoreV3::migrate_orphanRow_skipped()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("t.db"));

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), QStringLiteral("setup_orphan"));
        db.setDatabaseName(dbPath);
        QVERIFY(db.open());
        {
            QSqlQuery q(db);
            q.exec(QStringLiteral(
                "CREATE TABLE blob_baselines ("
                "  backend_id TEXT, collection_id TEXT, record_id TEXT,"
                "  content_hash TEXT, updated_at TEXT,"
                "  PRIMARY KEY(backend_id, collection_id, record_id))"));
            q.exec(QStringLiteral(
                "INSERT INTO blob_baselines VALUES "
                "('b1','c1','r1','h1',datetime('now'))"));
            q.exec(QStringLiteral(
                "INSERT INTO blob_baselines VALUES "
                "('b2','c2','r9','h9',datetime('now'))"));
            q.exec(QStringLiteral("PRAGMA user_version = 3"));
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("setup_orphan"));

    BaselineStore store(dbPath);
    QVERIFY(store.isOpen());
    store.setMappingResolver([](const QString &bId, const QString &) {
        if (bId == QLatin1String("b1")) return QStringList{QStringLiteral("m")};
        return QStringList{};
    });
    QVERIFY(store.migrateV3());

    const auto all = store.baselinesForMappingV3(QStringLiteral("m"));
    QCOMPARE(all.size(), 1);
    QCOMPARE(all.first().recordId, QStringLiteral("r1"));
}

QTEST_MAIN(TestBlobBaselineStoreV3)
#include "tst_baseline_store_v3.moc"
