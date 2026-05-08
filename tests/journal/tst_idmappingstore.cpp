#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include "idmappingstore.h"

namespace Kalburator::Sync {}
using namespace Kalburator::Sync;

class TestIDMappingStore : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void test_open_fresh_db();
    void test_basic_roundtrip();
    void test_remove_by_source();
    void test_clear_backend_scoped();
    void test_recurrence_exceptions();
    void test_remove_master_keeps_exceptions();
    void test_all_mappings_returns_full_structs();
    void test_roundtrip_with_categories();
    void test_archived_flag();
    void test_unused_backend_returns_empty();
    void test_migration_from_pre_c4_schema();

private:
    QTemporaryDir *m_tempDir = nullptr;

    QString dbPath() const;
    QSqlDatabase openRawConnection(const QString &connName) const;
    QStringList columnsOf(const QString &table,
                          QSqlDatabase db) const;
};

void TestIDMappingStore::init()
{
    m_tempDir = new QTemporaryDir();
    QVERIFY(m_tempDir->isValid());
}

void TestIDMappingStore::cleanup()
{
    delete m_tempDir;
    m_tempDir = nullptr;
}

QString TestIDMappingStore::dbPath() const
{
    return m_tempDir->filePath(QStringLiteral(".kalburator-sync.db"));
}

QSqlDatabase TestIDMappingStore::openRawConnection(const QString &connName) const
{
    QSqlDatabase db = QSqlDatabase::addDatabase(
        QStringLiteral("QSQLITE"), connName);
    db.setDatabaseName(dbPath());
    [](bool ok) { Q_ASSERT(ok); }(db.open());
    return db;
}

QStringList TestIDMappingStore::columnsOf(const QString &table,
                                          QSqlDatabase db) const
{
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table));
    QStringList cols;
    while (q.next()) {
        cols << q.value(1).toString();
    }
    return cols;
}

// ====================================================================

void TestIDMappingStore::test_open_fresh_db()
{
    IDMappingStore store(dbPath());
    QVERIFY(store.isOpen());
    QVERIFY(QFile::exists(dbPath()));
    QCOMPARE(store.databasePath(), dbPath());
    QVERIFY(store.lastError().isEmpty());

    // Verify full column set exists, including the 4 new WP columns.
    const QString conn = QStringLiteral("test_check_open_fresh_db");
    {
        QSqlDatabase db = openRawConnection(conn);
        QStringList cols = columnsOf(QStringLiteral("sync_id_mappings"), db);
        QVERIFY(cols.contains(QStringLiteral("backend_id")));
        QVERIFY(cols.contains(QStringLiteral("local_uid")));
        QVERIFY(cols.contains(QStringLiteral("recurrence_id")));
        QVERIFY(cols.contains(QStringLiteral("remote_id")));
        QVERIFY(cols.contains(QStringLiteral("calendar_id")));
        QVERIFY(cols.contains(QStringLiteral("created_at")));
        QVERIFY(cols.contains(QStringLiteral("last_synced")));
        QVERIFY(cols.contains(QStringLiteral("source_category")));
        QVERIFY(cols.contains(QStringLiteral("target_categories")));
        QVERIFY(cols.contains(QStringLiteral("archived")));

        // user_version stamped to 3 on fresh-DB creation.
        QSqlQuery pv(db);
        QVERIFY(pv.exec(QStringLiteral("PRAGMA user_version")));
        QVERIFY(pv.next());
        QCOMPARE(pv.value(0).toInt(), 3);

        db.close();
    }
    QSqlDatabase::removeDatabase(conn);
}

void TestIDMappingStore::test_basic_roundtrip()
{
    IDMappingStore store(dbPath());
    store.setIdMapping(QStringLiteral("caldav"),
                       QStringLiteral("uid1"),
                       QString(),
                       QStringLiteral("https://remote/1"),
                       QStringLiteral("cal-a"));

    QCOMPARE(store.targetIdForSourceUid(QStringLiteral("caldav"),
                                        QStringLiteral("uid1")),
             QStringLiteral("https://remote/1"));
    QCOMPARE(store.sourceUidForTargetId(QStringLiteral("caldav"),
                                        QStringLiteral("https://remote/1")),
             QStringLiteral("uid1"));
}

void TestIDMappingStore::test_remove_by_source()
{
    IDMappingStore store(dbPath());
    store.setIdMapping(QStringLiteral("caldav"),
                       QStringLiteral("uid1"),
                       QString(),
                       QStringLiteral("remote-1"));
    store.setIdMapping(QStringLiteral("caldav"),
                       QStringLiteral("uid2"),
                       QString(),
                       QStringLiteral("remote-2"));

    store.removeIdMapping(QStringLiteral("caldav"),
                          QStringLiteral("uid1"));

    QVERIFY(store.targetIdForSourceUid(QStringLiteral("caldav"),
                                       QStringLiteral("uid1")).isEmpty());
    QCOMPARE(store.targetIdForSourceUid(QStringLiteral("caldav"),
                                        QStringLiteral("uid2")),
             QStringLiteral("remote-2"));
}

void TestIDMappingStore::test_clear_backend_scoped()
{
    IDMappingStore store(dbPath());
    store.setIdMapping(QStringLiteral("caldav"),
                       QStringLiteral("uid1"), QString(),
                       QStringLiteral("remote-1"));
    store.setIdMapping(QStringLiteral("caldav"),
                       QStringLiteral("uid2"), QString(),
                       QStringLiteral("remote-2"));
    store.setIdMapping(QStringLiteral("local"),
                       QStringLiteral("uid3"), QString(),
                       QStringLiteral("file:3"));

    store.clearIdMappings(QStringLiteral("caldav"));

    QVERIFY(store.allMappings(QStringLiteral("caldav")).isEmpty());
    QCOMPARE(store.allMappings(QStringLiteral("local")).size(), 1);
}

void TestIDMappingStore::test_recurrence_exceptions()
{
    IDMappingStore store(dbPath());
    store.setIdMapping(QStringLiteral("caldav"),
                       QStringLiteral("uid-series"),
                       QString(), // master
                       QStringLiteral("href-master"));
    store.setIdMapping(QStringLiteral("caldav"),
                       QStringLiteral("uid-series"),
                       QStringLiteral("20260101T100000Z"),
                       QStringLiteral("href-exception-1"));
    store.setIdMapping(QStringLiteral("caldav"),
                       QStringLiteral("uid-series"),
                       QStringLiteral("20260102T100000Z"),
                       QStringLiteral("href-exception-2"));

    QCOMPARE(store.targetIdForSourceUid(
                 QStringLiteral("caldav"),
                 QStringLiteral("uid-series"),
                 QString()),
             QStringLiteral("href-master"));
    QCOMPARE(store.targetIdForSourceUid(
                 QStringLiteral("caldav"),
                 QStringLiteral("uid-series"),
                 QStringLiteral("20260101T100000Z")),
             QStringLiteral("href-exception-1"));
    QCOMPARE(store.targetIdForSourceUid(
                 QStringLiteral("caldav"),
                 QStringLiteral("uid-series"),
                 QStringLiteral("20260102T100000Z")),
             QStringLiteral("href-exception-2"));

    QCOMPARE(store.allMappings(QStringLiteral("caldav")).size(), 3);
}

void TestIDMappingStore::test_remove_master_keeps_exceptions()
{
    IDMappingStore store(dbPath());
    store.setIdMapping(QStringLiteral("caldav"),
                       QStringLiteral("uid-series"), QString(),
                       QStringLiteral("href-master"));
    store.setIdMapping(QStringLiteral("caldav"),
                       QStringLiteral("uid-series"),
                       QStringLiteral("20260101T100000Z"),
                       QStringLiteral("href-ex1"));

    store.removeIdMapping(QStringLiteral("caldav"),
                          QStringLiteral("uid-series"),
                          QString()); // remove master only

    QVERIFY(store.targetIdForSourceUid(
                QStringLiteral("caldav"),
                QStringLiteral("uid-series"),
                QString()).isEmpty());
    QCOMPARE(store.targetIdForSourceUid(
                 QStringLiteral("caldav"),
                 QStringLiteral("uid-series"),
                 QStringLiteral("20260101T100000Z")),
             QStringLiteral("href-ex1"));
}

void TestIDMappingStore::test_all_mappings_returns_full_structs()
{
    IDMappingStore store(dbPath());
    store.setIdMapping(QStringLiteral("caldav"),
                       QStringLiteral("uid1"), QString(),
                       QStringLiteral("rem-1"),
                       QStringLiteral("cal-a"));
    store.setIdMapping(QStringLiteral("caldav"),
                       QStringLiteral("uid2"), QString(),
                       QStringLiteral("rem-2"),
                       QStringLiteral("cal-b"));

    QList<IDMapping> rows = store.allMappings(QStringLiteral("caldav"));
    QCOMPARE(rows.size(), 2);
    for (const IDMapping &m : rows) {
        QVERIFY(m.isValid());
        QCOMPARE(m.backendId, QStringLiteral("caldav"));
        QVERIFY(!m.calendarId.isEmpty());
        QVERIFY(m.lastSynced.isValid());
    }
}

void TestIDMappingStore::test_roundtrip_with_categories()
{
    IDMappingStore store(dbPath());
    store.setIdMapping(QStringLiteral("palm"),
                       QStringLiteral("palm-42"), QString(),
                       QStringLiteral("local-path/x.md"));
    store.updateCategories(QStringLiteral("palm"),
                           QStringLiteral("palm-42"),
                           QString(),
                           QStringLiteral("Business"),
                           QStringList{QStringLiteral("work"),
                                       QStringLiteral("urgent")});

    IDMapping m = store.getMapping(QStringLiteral("palm"),
                                   QStringLiteral("palm-42"));
    QCOMPARE(m.sourceCategory, QStringLiteral("Business"));
    QCOMPARE(m.targetCategories,
             (QStringList{QStringLiteral("work"), QStringLiteral("urgent")}));
}

void TestIDMappingStore::test_archived_flag()
{
    IDMappingStore store(dbPath());
    store.setIdMapping(QStringLiteral("caldav"),
                       QStringLiteral("uid1"), QString(),
                       QStringLiteral("remote-1"));
    QCOMPARE(store.getMapping(QStringLiteral("caldav"),
                              QStringLiteral("uid1")).archived,
             false);

    store.setArchived(QStringLiteral("caldav"),
                      QStringLiteral("uid1"),
                      QString(),
                      true);

    QCOMPARE(store.getMapping(QStringLiteral("caldav"),
                              QStringLiteral("uid1")).archived,
             true);
}

void TestIDMappingStore::test_unused_backend_returns_empty()
{
    IDMappingStore store(dbPath());
    QVERIFY(store.targetIdForSourceUid(
                QStringLiteral("nonexistent"),
                QStringLiteral("uid1")).isEmpty());
    QVERIFY(store.sourceUidForTargetId(
                QStringLiteral("nonexistent"),
                QStringLiteral("remote")).isEmpty());
    QVERIFY(store.allMappings(QStringLiteral("nonexistent")).isEmpty());
}

void TestIDMappingStore::test_migration_from_pre_c4_schema()
{
    // Hand-create a DB with pre-C.4 schema (6 columns only, no WP additions).
    {
        const QString conn = QStringLiteral("test_prec4_writer");
        {
            QSqlDatabase db = openRawConnection(conn);
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE sync_id_mappings ("
                "  backend_id TEXT NOT NULL,"
                "  local_uid TEXT NOT NULL,"
                "  recurrence_id TEXT DEFAULT '',"
                "  remote_id TEXT NOT NULL,"
                "  calendar_id TEXT,"
                "  created_at TEXT DEFAULT (datetime('now')),"
                "  PRIMARY KEY (backend_id, local_uid, recurrence_id)"
                ")")));
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO sync_id_mappings "
                "(backend_id, local_uid, recurrence_id, remote_id, calendar_id) "
                "VALUES ('caldav', 'legacy-uid', '', 'legacy-remote', 'cal-x')")));
            db.close();
        }
        QSqlDatabase::removeDatabase(conn);
    }

    // Now open an IDMappingStore over that DB. Migration must add the
    // four new columns without dropping the existing row.
    IDMappingStore store(dbPath());
    QVERIFY(store.isOpen());

    const QString verifyConn = QStringLiteral("test_prec4_verify");
    {
        QSqlDatabase db = openRawConnection(verifyConn);
        QStringList cols = columnsOf(QStringLiteral("sync_id_mappings"), db);
        QVERIFY(cols.contains(QStringLiteral("last_synced")));
        QVERIFY(cols.contains(QStringLiteral("source_category")));
        QVERIFY(cols.contains(QStringLiteral("target_categories")));
        QVERIFY(cols.contains(QStringLiteral("archived")));
        db.close();
    }
    QSqlDatabase::removeDatabase(verifyConn);

    // Legacy row survives the migration.
    QCOMPARE(store.targetIdForSourceUid(QStringLiteral("caldav"),
                                        QStringLiteral("legacy-uid")),
             QStringLiteral("legacy-remote"));

    // New columns are null/0 for legacy rows.
    IDMapping m = store.getMapping(QStringLiteral("caldav"),
                                   QStringLiteral("legacy-uid"));
    QVERIFY(!m.lastSynced.isValid());
    QVERIFY(m.sourceCategory.isEmpty());
    QVERIFY(m.targetCategories.isEmpty());
    QCOMPARE(m.archived, false);

    // Writing new rows populates the new columns correctly.
    store.setIdMapping(QStringLiteral("caldav"),
                       QStringLiteral("fresh-uid"), QString(),
                       QStringLiteral("fresh-remote"));
    IDMapping m2 = store.getMapping(QStringLiteral("caldav"),
                                    QStringLiteral("fresh-uid"));
    QVERIFY(m2.lastSynced.isValid());
}

QTEST_MAIN(TestIDMappingStore)
#include "tst_idmappingstore.moc"
