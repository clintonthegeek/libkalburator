#include <QtTest/QtTest>
#include <QHash>
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
    void migrate_idempotent_reopenSafe();

    // Phase B4 (N2 fix): per-side baseline hashes, schema v6.
    void legacyRow_singleHash_loadsAsBothSidesEqual();
    void newRow_perSideHashesPersistIndependently();
    void perSideHashes_roundTripAcrossReopen();
    void legacyAndPerSideRows_coexistInSameMapping();
};

// ───────────────────────────────────────────────────────────────────────────
// 1. Fresh DB: blob_baselines_v3 table exists and user_version == 7.
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
            QCOMPARE(q.value(0).toInt(), 7);  // H3: schema v7 (sync_tokens table added)

            q.exec(QStringLiteral(
                "SELECT name FROM sqlite_master WHERE type='table' "
                "AND name='blob_baselines_v3'"));
            QVERIFY(q.next());
        }
        {
            // B4: blob_baselines_v3 gained nullable source_hash/target_hash
            // columns on a fresh DB too (not just on migration).
            QSqlQuery info(db);
            info.exec(QStringLiteral("PRAGMA table_info(blob_baselines_v3)"));
            bool hasSourceHash = false, hasTargetHash = false;
            while (info.next()) {
                const QString col = info.value(1).toString();
                if (col == QLatin1String("source_hash")) hasSourceHash = true;
                if (col == QLatin1String("target_hash")) hasTargetHash = true;
            }
            QVERIFY(hasSourceHash);
            QVERIFY(hasTargetHash);
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
// 7. (Deleted in fanout-collapse Task 3.1: the v2→v3 data-migration coder
// path. The campaign's locked "break + recreate" decision (spec §A/B)
// removes the migration reach — there is no v2 schema to migrate from
// once the pre-collapse triple-keyed code is gone.)
// ───────────────────────────────────────────────────────────────────────────

// ───────────────────────────────────────────────────────────────────────────
// 8. Idempotent: open, close, reopen a v3 DB — second open is a no-op.
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
// Phase B4 (N2 fix): per-side baseline hashes, schema v6.
//
// blob_baselines_v3 gained nullable source_hash/target_hash columns. A
// legacy row (written via the pre-B4 setBaselineV3() API, which never
// touches those columns) must transparently load as "both sides equal" so
// the first post-upgrade sync re-diffs exactly as it did before this
// migration; a fresh per-side write must persist and round-trip each
// side's hash independently.
// ───────────────────────────────────────────────────────────────────────────

void TestBlobBaselineStoreV3::legacyRow_singleHash_loadsAsBothSidesEqual()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BaselineStore store(dir.filePath(QStringLiteral("test.db")));
    QVERIFY(store.isOpen());

    const QString mid = QStringLiteral("m1");

    // Simulate a pre-B4 baseline row: written via the old single-hash API,
    // domain "blob" (the unified engine's steady-state marker for a
    // hash-only baseline row — see syncengine.cpp), source_hash/target_hash
    // left untouched (NULL).
    CanonicalRecord legacy;
    legacy.recordId = QStringLiteral("r1");
    legacy.shape    = Shape{DomainId{QStringLiteral("blob")}, EncodingId{QStringLiteral("raw")}};
    legacy.data     = QByteArrayLiteral("legacy-shared-hash");
    QVERIFY(store.setBaselineV3(mid, legacy));

    const auto single = store.baselineHashesV4(mid, QStringLiteral("r1"));
    QVERIFY(single.has_value());
    QCOMPARE(single->sourceHash, QStringLiteral("legacy-shared-hash"));
    QCOMPARE(single->targetHash, QStringLiteral("legacy-shared-hash"));

    const auto all = store.baselineHashesForMappingV4(mid);
    QCOMPARE(all.size(), 1);
    QCOMPARE(all.first().sourceHash, QStringLiteral("legacy-shared-hash"));
    QCOMPARE(all.first().targetHash, QStringLiteral("legacy-shared-hash"));
}

void TestBlobBaselineStoreV3::newRow_perSideHashesPersistIndependently()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BaselineStore store(dir.filePath(QStringLiteral("test.db")));
    QVERIFY(store.isOpen());

    const QString mid = QStringLiteral("m1");
    QVERIFY(store.setBaselineHashesV4(mid, QStringLiteral("r1"),
                                      QStringLiteral("local-hash"),
                                      QStringLiteral("caldav-hash")));

    const auto got = store.baselineHashesV4(mid, QStringLiteral("r1"));
    QVERIFY(got.has_value());
    QCOMPARE(got->sourceHash, QStringLiteral("local-hash"));
    QCOMPARE(got->targetHash, QStringLiteral("caldav-hash"));
    QVERIFY(got->sourceHash != got->targetHash);
}

void TestBlobBaselineStoreV3::perSideHashes_roundTripAcrossReopen()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("test.db"));
    const QString mid = QStringLiteral("m1");

    {
        BaselineStore store(dbPath);
        QVERIFY(store.isOpen());
        QVERIFY(store.setBaselineHashesV4(mid, QStringLiteral("r1"),
                                          QStringLiteral("src-hash-1"),
                                          QStringLiteral("tgt-hash-1")));
    }
    {
        BaselineStore store2(dbPath);
        QVERIFY(store2.isOpen());
        const auto got = store2.baselineHashesV4(mid, QStringLiteral("r1"));
        QVERIFY(got.has_value());
        QCOMPARE(got->sourceHash, QStringLiteral("src-hash-1"));
        QCOMPARE(got->targetHash, QStringLiteral("tgt-hash-1"));
    }
}

void TestBlobBaselineStoreV3::legacyAndPerSideRows_coexistInSameMapping()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BaselineStore store(dir.filePath(QStringLiteral("test.db")));
    QVERIFY(store.isOpen());

    const QString mid = QStringLiteral("m1");

    // r1: legacy single-hash row (pre-B4 sync, never migrated by any data
    // pass — just left alone per the design).
    CanonicalRecord legacy;
    legacy.recordId = QStringLiteral("r1");
    legacy.shape    = Shape{DomainId{QStringLiteral("blob")}, EncodingId{QStringLiteral("raw")}};
    legacy.data     = QByteArrayLiteral("legacy-hash");
    QVERIFY(store.setBaselineV3(mid, legacy));

    // r2: fresh per-side row (post-B4 sync).
    QVERIFY(store.setBaselineHashesV4(mid, QStringLiteral("r2"),
                                      QStringLiteral("src-2"), QStringLiteral("tgt-2")));

    const auto all = store.baselineHashesForMappingV4(mid);
    QCOMPARE(all.size(), 2);
    QHash<QString, Kalburator::Storage::BaselineStore::BaselineHashes> byId;
    for (const auto &h : all) byId.insert(h.recordId, h);

    QVERIFY(byId.contains(QStringLiteral("r1")));
    QCOMPARE(byId.value(QStringLiteral("r1")).sourceHash, QStringLiteral("legacy-hash"));
    QCOMPARE(byId.value(QStringLiteral("r1")).targetHash, QStringLiteral("legacy-hash"));

    QVERIFY(byId.contains(QStringLiteral("r2")));
    QCOMPARE(byId.value(QStringLiteral("r2")).sourceHash, QStringLiteral("src-2"));
    QCOMPARE(byId.value(QStringLiteral("r2")).targetHash, QStringLiteral("tgt-2"));
}

QTEST_MAIN(TestBlobBaselineStoreV3)
#include "tst_baseline_store_v3.moc"
