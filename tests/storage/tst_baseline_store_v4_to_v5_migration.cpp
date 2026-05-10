#include <QObject>
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

#include "baselinestore.h"

using Kalburator::Storage::BaselineStore;

class TstBaselineStoreV4ToV5Migration : public QObject {
    Q_OBJECT
private slots:
    void migrationCreatesNewTables();
    void migrationStampsVersion5();
    void migrationPreservesV3Data();
    void migrationIsIdempotent();
};

namespace {
void seedV4Database(const QString &path) {
    const QString conn = QStringLiteral("seedV4_%1").arg(qHash(path));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery q(db);
        q.exec("CREATE TABLE blob_baselines (backend_id TEXT, collection_id TEXT, "
               "record_id TEXT, content_hash TEXT, updated_at TEXT, "
               "PRIMARY KEY(backend_id, collection_id, record_id))");
        q.exec("CREATE TABLE blob_baselines_v3 (mapping_id TEXT, record_id TEXT, "
               "canonical_shape_domain TEXT, canonical_shape_encoding TEXT, "
               "canonical_bytes BLOB, updated_at INTEGER, "
               "PRIMARY KEY(mapping_id, record_id))");
        q.exec("INSERT INTO blob_baselines_v3 VALUES "
               "('m1','r1','calendar','ical','BYTES1',1)");
        q.exec("PRAGMA user_version = 4");
        db.close();
    }
    QSqlDatabase::removeDatabase(conn);
}
} // namespace

void TstBaselineStoreV4ToV5Migration::migrationCreatesNewTables() {
    QTemporaryDir dir;
    const QString path = dir.filePath("k5.db");
    seedV4Database(path);

    BaselineStore store(path);
    QVERIFY(store.isOpen());

    // collection_baselines and mapping_metadata should now be queryable.
    QVERIFY(store.setCollectionBaseline("m1", "cal1", {{"k","v"}}));
    QVERIFY(store.setLastSyncTime("m1", QDateTime::fromSecsSinceEpoch(123)));
}

void TstBaselineStoreV4ToV5Migration::migrationStampsVersion5() {
    QTemporaryDir dir;
    const QString path = dir.filePath("k5.db");
    seedV4Database(path);
    {
        BaselineStore store(path);
        QVERIFY(store.isOpen());
    }

    // Re-open as a raw SQL connection and check user_version.
    const QString conn = QStringLiteral("verify_%1").arg(qHash(path));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery q(db);
        q.exec("PRAGMA user_version");
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 5);
    }
    QSqlDatabase::removeDatabase(conn);
}

void TstBaselineStoreV4ToV5Migration::migrationPreservesV3Data() {
    QTemporaryDir dir;
    const QString path = dir.filePath("k5.db");
    seedV4Database(path);

    BaselineStore store(path);
    auto rec = store.baselineV3("m1", "r1");
    QVERIFY(rec.has_value());
}

void TstBaselineStoreV4ToV5Migration::migrationIsIdempotent() {
    QTemporaryDir dir;
    const QString path = dir.filePath("k5.db");
    seedV4Database(path);
    { BaselineStore s(path); }
    { BaselineStore s(path); }   // re-open against v5 — no-op
    { BaselineStore s2(path); }  // and again
    BaselineStore store(path);
    QVERIFY(store.isOpen());
    QVERIFY(store.setCollectionBaseline("m1", "cal1", {{"k","v"}}));
    QCOMPARE(store.collectionBaseline("m1", "cal1").value("k").toString(),
             QStringLiteral("v"));
}

QTEST_MAIN(TstBaselineStoreV4ToV5Migration)
#include "tst_baseline_store_v4_to_v5_migration.moc"
