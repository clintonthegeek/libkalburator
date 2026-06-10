/// G.8 Task 61 — RawFilesBackend round-trip tests.

#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtConcurrent>

#include <atomic>

#include "rawfilesbackend.h"
#include "collectioninfo.h"
#include "backendrecord.h"
#include "shape.h"

using Kalburator::Sinks::RawFilesBackend;
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

// K.9 requires every collection in a universal sink to declare a shape.
// These unit tests exercise storage semantics only — the engine isn't
// involved — so a single synthetic test shape suffices.
const Shape kTestShape{ DomainId{"test"}, EncodingId{"raw"} };

BackendRecord makeRecord(const QString &id, const QByteArray &data)
{
    BackendRecord r;
    r.id = id;
    r.displayName = id;
    r.data = data;
    r.contentHash = QStringLiteral("hash-%1").arg(id);
    r.lastModified = QDateTime::currentDateTimeUtc();
    r.type = QStringLiteral("raw");
    return r;
}

} // namespace

class TestRawFilesBackend : public QObject
{
    Q_OBJECT
private slots:
    void isAvailable_falseForMissingDir();
    void createCollection_createsDir();
    void availableCollections_reflectsCreated();
    void createAndLoadRecord_roundTrip();
    void updateRecord_overwritesData();
    void deleteRecord_removesFile();
    void loadRecords_returnsAllForCollection();
    void clearCollection_deletesAllRecords();
    void multipleCollections_noInterference();
    void manifestPersists_acrossInstances();
    void concurrentShapeForVsCreateCollection();
    void wipeCollection_emptiesFiles_leavesCollectionIntact();
    void wipeCollection_survivorIsolation();
};

void TestRawFilesBackend::isAvailable_falseForMissingDir()
{
    RawFilesBackend b(QStringLiteral("/nonexistent/path/that/should/not/exist/xyz123"));
    QVERIFY(!b.isAvailable());
}

void TestRawFilesBackend::createCollection_createsDir()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    RawFilesBackend b(tmp.path());
    QVERIFY(b.isAvailable());
    const QString id = b.createCollection(
        makeCollection(QStringLiteral("memo+plaintext"), QStringLiteral("Memos")),
        kTestShape);
    QCOMPARE(id, QStringLiteral("memo+plaintext"));
}

void TestRawFilesBackend::availableCollections_reflectsCreated()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    RawFilesBackend b(tmp.path());
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

void TestRawFilesBackend::createAndLoadRecord_roundTrip()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    RawFilesBackend b(tmp.path());
    const QString colId = QStringLiteral("memo+plaintext");
    b.createCollection(makeCollection(colId, QStringLiteral("Memos")), kTestShape);

    const QByteArray data = QByteArrayLiteral("hello world");
    const QString id = b.createRecord(colId, makeRecord(QStringLiteral("r1"), data));
    QVERIFY(!id.isEmpty());

    const auto loaded = b.loadRecord(id);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->data, data);
}

void TestRawFilesBackend::updateRecord_overwritesData()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    RawFilesBackend b(tmp.path());
    const QString colId = QStringLiteral("memo+plaintext");
    b.createCollection(makeCollection(colId, QStringLiteral("Memos")), kTestShape);

    const QString id = b.createRecord(colId,
        makeRecord(QStringLiteral("r1"), QByteArrayLiteral("original")));
    QVERIFY(!id.isEmpty());

    BackendRecord updated;
    updated.id = id;
    updated.data = QByteArrayLiteral("updated");
    updated.contentHash = QStringLiteral("new-hash");
    updated.lastModified = QDateTime::currentDateTimeUtc();
    QVERIFY(b.updateRecord(updated));

    const auto loaded = b.loadRecord(id);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->data, QByteArrayLiteral("updated"));
}

void TestRawFilesBackend::deleteRecord_removesFile()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    RawFilesBackend b(tmp.path());
    const QString colId = QStringLiteral("memo+plaintext");
    b.createCollection(makeCollection(colId, QStringLiteral("Memos")), kTestShape);

    const QString id = b.createRecord(colId,
        makeRecord(QStringLiteral("r1"), QByteArrayLiteral("data")));
    QVERIFY(!id.isEmpty());
    QVERIFY(b.loadRecord(id).has_value());

    QVERIFY(b.deleteRecord(id));
    QVERIFY(!b.loadRecord(id).has_value());
}

void TestRawFilesBackend::loadRecords_returnsAllForCollection()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    RawFilesBackend b(tmp.path());
    const QString colId = QStringLiteral("memo+plaintext");
    b.createCollection(makeCollection(colId, QStringLiteral("Memos")), kTestShape);

    b.createRecord(colId, makeRecord(QStringLiteral("r1"), QByteArrayLiteral("one")));
    b.createRecord(colId, makeRecord(QStringLiteral("r2"), QByteArrayLiteral("two")));
    b.createRecord(colId, makeRecord(QStringLiteral("r3"), QByteArrayLiteral("three")));

    const auto records = b.loadRecords(colId);
    QCOMPARE(records.size(), 3);
}

void TestRawFilesBackend::clearCollection_deletesAllRecords()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    RawFilesBackend b(tmp.path());
    const QString colId = QStringLiteral("memo+plaintext");
    b.createCollection(makeCollection(colId, QStringLiteral("Memos")), kTestShape);

    b.createRecord(colId, makeRecord(QStringLiteral("r1"), QByteArrayLiteral("a")));
    b.createRecord(colId, makeRecord(QStringLiteral("r2"), QByteArrayLiteral("b")));
    b.clearCollection(colId);

    QCOMPARE(b.loadRecords(colId).size(), 0);
}

void TestRawFilesBackend::multipleCollections_noInterference()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    RawFilesBackend b(tmp.path());

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

void TestRawFilesBackend::manifestPersists_acrossInstances()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    {
        RawFilesBackend b(tmp.path());
        b.createCollection(
            makeCollection(QStringLiteral("memo+plaintext"), QStringLiteral("Memos")),
            kTestShape);
        b.createRecord(QStringLiteral("memo+plaintext"),
                       makeRecord(QStringLiteral("r1"), QByteArrayLiteral("persisted")));
    }
    // Second instance should see the manifest and be able to load records.
    RawFilesBackend b2(tmp.path());
    const auto cols = b2.availableCollections();
    QCOMPARE(cols.size(), 1);
    const auto records = b2.loadRecords(QStringLiteral("memo+plaintext"));
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.first().data, QByteArrayLiteral("persisted"));
}

void TestRawFilesBackend::concurrentShapeForVsCreateCollection()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RawFilesBackend be(dir.path());

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

void TestRawFilesBackend::wipeCollection_emptiesFiles_leavesCollectionIntact()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    RawFilesBackend b(tmp.path());
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

void TestRawFilesBackend::wipeCollection_survivorIsolation()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    RawFilesBackend b(tmp.path());
    const QString wiped    = QStringLiteral("col+a");
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

QTEST_MAIN(TestRawFilesBackend)
#include "tst_rawfiles_backend.moc"
