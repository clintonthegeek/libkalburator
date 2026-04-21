#include <QtTest/QtTest>
#include <QSignalSpy>

#include "blobsyncengine.h"
#include "iblobbackend.h"
#include "mockblobbackend.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::BlobSyncEngine;
using Kalburator::Sync::BlobSyncResult;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::MockBlobBackend;

namespace {

BackendRecord makeRecord(const QString &id, const QString &data,
                         qint64 mtimeOffsetSecs = 0)
{
    BackendRecord r;
    r.id = id;
    r.type = QStringLiteral("memo");
    r.displayName = id;
    r.data = data.toUtf8();
    r.contentHash = QStringLiteral("hash-of-%1").arg(data);
    r.lastModified = QDateTime::currentDateTimeUtc().addSecs(mtimeOffsetSecs);
    return r;
}

CollectionInfo makeCollection(const QString &id)
{
    CollectionInfo c;
    c.id = id;
    c.name = id;
    c.type = QStringLiteral("memos");
    return c;
}

void seed(MockBlobBackend &b, const QString &cid)
{
    b.createCollection(makeCollection(cid));
}

} // namespace

class TestBlobSyncEngine : public QObject
{
    Q_OBJECT
private slots:
    void mirrorCopiesSourceToEmptyTarget();
    void mirrorLeavesMatchingRecordsAlone();
    void mirrorUpdatesChangedRecords();
    void mirrorDeletesOrphansInTarget();
    void mirrorNullBackendReturnsFailure();
    void mirrorReportsSourceErrorsAsTargetErrors();
    void twoWayNaiveCopiesDisjoint();
    void twoWayNaiveNewerSideWins();
    void twoWayNaiveDoesNotPropagateDeletions();
    void progressSignalFiresDuringMirror();
};

void TestBlobSyncEngine::mirrorCopiesSourceToEmptyTarget()
{
    MockBlobBackend src, tgt;
    seed(src, QStringLiteral("memos"));
    seed(tgt, QStringLiteral("memos"));
    src.createRecord(QStringLiteral("memos"),
                     makeRecord(QStringLiteral("r-1"), QStringLiteral("a")));
    src.createRecord(QStringLiteral("memos"),
                     makeRecord(QStringLiteral("r-2"), QStringLiteral("b")));

    BlobSyncEngine eng;
    const auto result = eng.mirror(&src, &tgt, QStringLiteral("memos"));
    QVERIFY(result.success);
    QCOMPARE(result.targetStats.created, 2);
    QCOMPARE(tgt.recordsIn(QStringLiteral("memos")).size(), 2);
}

void TestBlobSyncEngine::mirrorLeavesMatchingRecordsAlone()
{
    MockBlobBackend src, tgt;
    seed(src, QStringLiteral("memos"));
    seed(tgt, QStringLiteral("memos"));
    const auto rec = makeRecord(QStringLiteral("r-1"), QStringLiteral("same"));
    src.createRecord(QStringLiteral("memos"), rec);
    tgt.createRecord(QStringLiteral("memos"), rec);

    BlobSyncEngine eng;
    const auto result = eng.mirror(&src, &tgt, QStringLiteral("memos"));
    QCOMPARE(result.targetStats.unchanged, 1);
    QCOMPARE(result.targetStats.updated, 0);
    QCOMPARE(result.targetStats.created, 0);
}

void TestBlobSyncEngine::mirrorUpdatesChangedRecords()
{
    MockBlobBackend src, tgt;
    seed(src, QStringLiteral("memos"));
    seed(tgt, QStringLiteral("memos"));
    src.createRecord(QStringLiteral("memos"),
                     makeRecord(QStringLiteral("r-1"), QStringLiteral("new")));
    tgt.createRecord(QStringLiteral("memos"),
                     makeRecord(QStringLiteral("r-1"), QStringLiteral("old")));

    BlobSyncEngine eng;
    const auto result = eng.mirror(&src, &tgt, QStringLiteral("memos"));
    QCOMPARE(result.targetStats.updated, 1);
    QCOMPARE(tgt.recordsIn(QStringLiteral("memos")).value(QStringLiteral("r-1")).data,
             QByteArrayLiteral("new"));
}

void TestBlobSyncEngine::mirrorDeletesOrphansInTarget()
{
    MockBlobBackend src, tgt;
    seed(src, QStringLiteral("memos"));
    seed(tgt, QStringLiteral("memos"));
    tgt.createRecord(QStringLiteral("memos"),
                     makeRecord(QStringLiteral("r-orphan"), QStringLiteral("x")));

    BlobSyncEngine eng;
    const auto result = eng.mirror(&src, &tgt, QStringLiteral("memos"));
    QCOMPARE(result.targetStats.deleted, 1);
    QVERIFY(tgt.recordsIn(QStringLiteral("memos")).isEmpty());
}

void TestBlobSyncEngine::mirrorNullBackendReturnsFailure()
{
    BlobSyncEngine eng;
    MockBlobBackend tgt;
    const auto result = eng.mirror(nullptr, &tgt, QStringLiteral("memos"));
    QVERIFY(!result.success);
    QVERIFY(!result.errorMessage.isEmpty());
}

void TestBlobSyncEngine::mirrorReportsSourceErrorsAsTargetErrors()
{
    MockBlobBackend src, tgt;
    seed(src, QStringLiteral("memos"));
    seed(tgt, QStringLiteral("memos"));
    src.createRecord(QStringLiteral("memos"),
                     makeRecord(QStringLiteral("r-1"), QStringLiteral("a")));
    tgt.setFailNext(MockBlobBackend::FailurePoint::OnCreateRecord, 1);

    BlobSyncEngine eng;
    const auto result = eng.mirror(&src, &tgt, QStringLiteral("memos"));
    QVERIFY(!result.success);
    QCOMPARE(result.targetStats.errors, 1);
}

void TestBlobSyncEngine::twoWayNaiveCopiesDisjoint()
{
    MockBlobBackend a, b;
    seed(a, QStringLiteral("memos"));
    seed(b, QStringLiteral("memos"));
    a.createRecord(QStringLiteral("memos"),
                   makeRecord(QStringLiteral("only-a"), QStringLiteral("x")));
    b.createRecord(QStringLiteral("memos"),
                   makeRecord(QStringLiteral("only-b"), QStringLiteral("y")));

    BlobSyncEngine eng;
    const auto result = eng.twoWayNaive(&a, &b, QStringLiteral("memos"));
    QVERIFY(result.success);
    QCOMPARE(a.recordsIn(QStringLiteral("memos")).size(), 2);
    QCOMPARE(b.recordsIn(QStringLiteral("memos")).size(), 2);
}

void TestBlobSyncEngine::twoWayNaiveNewerSideWins()
{
    MockBlobBackend a, b;
    seed(a, QStringLiteral("memos"));
    seed(b, QStringLiteral("memos"));
    a.createRecord(QStringLiteral("memos"),
                   makeRecord(QStringLiteral("r-1"), QStringLiteral("old"), -3600));
    b.createRecord(QStringLiteral("memos"),
                   makeRecord(QStringLiteral("r-1"), QStringLiteral("new"), 0));

    BlobSyncEngine eng;
    const auto result = eng.twoWayNaive(&a, &b, QStringLiteral("memos"));
    QVERIFY(result.success);
    QCOMPARE(a.recordsIn(QStringLiteral("memos")).value(QStringLiteral("r-1")).data,
             QByteArrayLiteral("new"));
}

void TestBlobSyncEngine::twoWayNaiveDoesNotPropagateDeletions()
{
    // A has r-1, B does not. twoWayNaive copies r-1 to B. It does NOT
    // infer that "B doesn't have r-1" means "B deleted r-1".
    MockBlobBackend a, b;
    seed(a, QStringLiteral("memos"));
    seed(b, QStringLiteral("memos"));
    a.createRecord(QStringLiteral("memos"),
                   makeRecord(QStringLiteral("r-1"), QStringLiteral("x")));

    BlobSyncEngine eng;
    eng.twoWayNaive(&a, &b, QStringLiteral("memos"));
    QVERIFY(a.recordsIn(QStringLiteral("memos")).contains(QStringLiteral("r-1")));
    QVERIFY(b.recordsIn(QStringLiteral("memos")).contains(QStringLiteral("r-1")));
}

void TestBlobSyncEngine::progressSignalFiresDuringMirror()
{
    MockBlobBackend src, tgt;
    seed(src, QStringLiteral("memos"));
    seed(tgt, QStringLiteral("memos"));
    src.createRecord(QStringLiteral("memos"),
                     makeRecord(QStringLiteral("r-1"), QStringLiteral("x")));

    BlobSyncEngine eng;
    QSignalSpy spy(&eng, &BlobSyncEngine::progressChanged);
    eng.mirror(&src, &tgt, QStringLiteral("memos"));
    QVERIFY(spy.size() >= 1);
}

QTEST_MAIN(TestBlobSyncEngine)
#include "tst_blobsyncengine.moc"
