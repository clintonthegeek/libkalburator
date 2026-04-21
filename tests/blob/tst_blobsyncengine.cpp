#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "blobsyncengine.h"
#include "iblobbackend.h"
#include "mockblobbackend.h"
#include "blobbaselinestore.h"
#include "conflicthandlerregistry.h"
#include "conflictpolicy.h"
#include "conflictrecord.h"
#include "conflictstore.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::BlobBaselineStore;
using Kalburator::Sync::BlobSyncEngine;
using Kalburator::Sync::BlobSyncResult;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::MockBlobBackend;

// Helper: MockBlobBackend subclass that returns a configurable backend ID,
// so conflict-handler dispatch tests can route by distinct IDs.
class IdentifiedMock : public MockBlobBackend {
public:
    explicit IdentifiedMock(const QString &id, QObject *p = nullptr)
        : MockBlobBackend(p), m_id(id) {}
    QString backendId() const override { return m_id; }
private:
    QString m_id;
};

// Helper: ConflictHandler that records invocations and returns a
// configurable decision.
class TestHandler : public Kalburator::Sync::QSyncCore::ConflictHandler
{
public:
    int invocations = 0;
    Kalburator::Sync::QSyncCore::ConflictDecision decision =
        Kalburator::Sync::QSyncCore::ConflictDecision::UseSource;

    Kalburator::Sync::QSyncCore::ConflictDecision handleConflict(
        Kalburator::Sync::QSyncCore::ConflictRecord &,
        const Kalburator::Sync::QSyncCore::ConflictPolicy &) override
    {
        invocations++;
        return decision;
    }
};

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
    void twoWayWithBaseline_noChanges();
    void twoWayWithBaseline_modifiedOnAOnly();
    void twoWayWithBaseline_modifiedOnBOnly();
    void twoWayWithBaseline_deletedOnA();
    void twoWayWithBaseline_deletedOnB();
    void twoWayWithBaseline_newOnA();
    void twoWayWithBaseline_newOnB();
    void twoWayWithBaseline_conflictInvokesHandler();
    void twoWayWithBaseline_conflictSkipPersists();
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

// ---- twoWayWithBaseline tests ----

namespace {

QString dbIn(const QTemporaryDir &d)
{
    return d.filePath(QStringLiteral(".planstan-sync.db"));
}

BackendRecord hashedRecord(const QString &id, const QString &data,
                           const QString &hash)
{
    BackendRecord r = makeRecord(id, data);
    r.contentHash = hash;
    return r;
}

} // namespace

void TestBlobSyncEngine::twoWayWithBaseline_noChanges()
{
    using namespace Kalburator::Sync::QSyncCore;
    QTemporaryDir dir; QVERIFY(dir.isValid());
    IdentifiedMock a(QStringLiteral("a")), b(QStringLiteral("b"));
    seed(a, QStringLiteral("col")); seed(b, QStringLiteral("col"));

    BackendRecord rec = hashedRecord(QStringLiteral("r1"),
                                     QStringLiteral("payload"),
                                     QStringLiteral("h1"));
    a.createRecord(QStringLiteral("col"), rec);
    b.createRecord(QStringLiteral("col"), rec);

    BlobBaselineStore base(dbIn(dir));
    QVERIFY(base.setBaseline(QStringLiteral("m"), QStringLiteral("r1"),
                             QStringLiteral("h1")));

    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;

    BlobSyncEngine eng;
    BlobSyncResult r = eng.twoWayWithBaseline(
        &a, &b, QStringLiteral("col"), QStringLiteral("m"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(r.sourceStats.unchanged + r.targetStats.unchanged, 2);
    QCOMPARE(r.sourceStats.conflicts + r.targetStats.conflicts, 0);
}

void TestBlobSyncEngine::twoWayWithBaseline_modifiedOnAOnly()
{
    using namespace Kalburator::Sync::QSyncCore;
    QTemporaryDir dir; QVERIFY(dir.isValid());
    IdentifiedMock a(QStringLiteral("a")), b(QStringLiteral("b"));
    seed(a, QStringLiteral("col")); seed(b, QStringLiteral("col"));

    a.createRecord(QStringLiteral("col"),
                   hashedRecord(QStringLiteral("r1"), QStringLiteral("v2"),
                                QStringLiteral("h-v2")));
    b.createRecord(QStringLiteral("col"),
                   hashedRecord(QStringLiteral("r1"), QStringLiteral("v1"),
                                QStringLiteral("h-v1")));

    BlobBaselineStore base(dbIn(dir));
    QVERIFY(base.setBaseline(QStringLiteral("m"), QStringLiteral("r1"),
                             QStringLiteral("h-v1")));

    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;

    BlobSyncEngine eng;
    BlobSyncResult r = eng.twoWayWithBaseline(
        &a, &b, QStringLiteral("col"), QStringLiteral("m"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    auto bRecs = b.loadRecords(QStringLiteral("col"));
    QCOMPARE(bRecs.size(), 1);
    QCOMPARE(bRecs.first().contentHash, QStringLiteral("h-v2"));
    QCOMPARE(r.targetStats.updated, 1);
}

void TestBlobSyncEngine::twoWayWithBaseline_modifiedOnBOnly()
{
    using namespace Kalburator::Sync::QSyncCore;
    QTemporaryDir dir; QVERIFY(dir.isValid());
    IdentifiedMock a(QStringLiteral("a")), b(QStringLiteral("b"));
    seed(a, QStringLiteral("col")); seed(b, QStringLiteral("col"));

    a.createRecord(QStringLiteral("col"),
                   hashedRecord(QStringLiteral("r1"), QStringLiteral("v1"),
                                QStringLiteral("h-v1")));
    b.createRecord(QStringLiteral("col"),
                   hashedRecord(QStringLiteral("r1"), QStringLiteral("v2"),
                                QStringLiteral("h-v2")));

    BlobBaselineStore base(dbIn(dir));
    QVERIFY(base.setBaseline(QStringLiteral("m"), QStringLiteral("r1"),
                             QStringLiteral("h-v1")));

    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;

    BlobSyncEngine eng;
    BlobSyncResult r = eng.twoWayWithBaseline(
        &a, &b, QStringLiteral("col"), QStringLiteral("m"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    auto aRecs = a.loadRecords(QStringLiteral("col"));
    QCOMPARE(aRecs.size(), 1);
    QCOMPARE(aRecs.first().contentHash, QStringLiteral("h-v2"));
    QCOMPARE(r.sourceStats.updated, 1);
}

void TestBlobSyncEngine::twoWayWithBaseline_deletedOnA()
{
    using namespace Kalburator::Sync::QSyncCore;
    QTemporaryDir dir; QVERIFY(dir.isValid());
    IdentifiedMock a(QStringLiteral("a")), b(QStringLiteral("b"));
    seed(a, QStringLiteral("col")); seed(b, QStringLiteral("col"));

    b.createRecord(QStringLiteral("col"),
                   hashedRecord(QStringLiteral("r1"), QStringLiteral("v1"),
                                QStringLiteral("h-v1")));

    BlobBaselineStore base(dbIn(dir));
    QVERIFY(base.setBaseline(QStringLiteral("m"), QStringLiteral("r1"),
                             QStringLiteral("h-v1")));

    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;

    BlobSyncEngine eng;
    BlobSyncResult r = eng.twoWayWithBaseline(
        &a, &b, QStringLiteral("col"), QStringLiteral("m"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(b.loadRecords(QStringLiteral("col")).size(), 0);
    QCOMPARE(r.targetStats.deleted, 1);
}

void TestBlobSyncEngine::twoWayWithBaseline_deletedOnB()
{
    using namespace Kalburator::Sync::QSyncCore;
    QTemporaryDir dir; QVERIFY(dir.isValid());
    IdentifiedMock a(QStringLiteral("a")), b(QStringLiteral("b"));
    seed(a, QStringLiteral("col")); seed(b, QStringLiteral("col"));

    a.createRecord(QStringLiteral("col"),
                   hashedRecord(QStringLiteral("r1"), QStringLiteral("v1"),
                                QStringLiteral("h-v1")));

    BlobBaselineStore base(dbIn(dir));
    QVERIFY(base.setBaseline(QStringLiteral("m"), QStringLiteral("r1"),
                             QStringLiteral("h-v1")));

    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;

    BlobSyncEngine eng;
    BlobSyncResult r = eng.twoWayWithBaseline(
        &a, &b, QStringLiteral("col"), QStringLiteral("m"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(a.loadRecords(QStringLiteral("col")).size(), 0);
    QCOMPARE(r.sourceStats.deleted, 1);
}

void TestBlobSyncEngine::twoWayWithBaseline_newOnA()
{
    using namespace Kalburator::Sync::QSyncCore;
    QTemporaryDir dir; QVERIFY(dir.isValid());
    IdentifiedMock a(QStringLiteral("a")), b(QStringLiteral("b"));
    seed(a, QStringLiteral("col")); seed(b, QStringLiteral("col"));

    a.createRecord(QStringLiteral("col"),
                   hashedRecord(QStringLiteral("r1"), QStringLiteral("v1"),
                                QStringLiteral("h-v1")));

    BlobBaselineStore base(dbIn(dir));
    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;

    BlobSyncEngine eng;
    BlobSyncResult r = eng.twoWayWithBaseline(
        &a, &b, QStringLiteral("col"), QStringLiteral("m"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(b.loadRecords(QStringLiteral("col")).size(), 1);
    QCOMPARE(r.targetStats.created, 1);
}

void TestBlobSyncEngine::twoWayWithBaseline_newOnB()
{
    using namespace Kalburator::Sync::QSyncCore;
    QTemporaryDir dir; QVERIFY(dir.isValid());
    IdentifiedMock a(QStringLiteral("a")), b(QStringLiteral("b"));
    seed(a, QStringLiteral("col")); seed(b, QStringLiteral("col"));

    b.createRecord(QStringLiteral("col"),
                   hashedRecord(QStringLiteral("r1"), QStringLiteral("v1"),
                                QStringLiteral("h-v1")));

    BlobBaselineStore base(dbIn(dir));
    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;

    BlobSyncEngine eng;
    BlobSyncResult r = eng.twoWayWithBaseline(
        &a, &b, QStringLiteral("col"), QStringLiteral("m"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(a.loadRecords(QStringLiteral("col")).size(), 1);
    QCOMPARE(r.sourceStats.created, 1);
}

void TestBlobSyncEngine::twoWayWithBaseline_conflictInvokesHandler()
{
    using namespace Kalburator::Sync::QSyncCore;
    QTemporaryDir dir; QVERIFY(dir.isValid());
    IdentifiedMock a(QStringLiteral("a")), b(QStringLiteral("b"));
    seed(a, QStringLiteral("col")); seed(b, QStringLiteral("col"));

    a.createRecord(QStringLiteral("col"),
                   hashedRecord(QStringLiteral("r1"), QStringLiteral("v2a"),
                                QStringLiteral("h-v2a")));
    b.createRecord(QStringLiteral("col"),
                   hashedRecord(QStringLiteral("r1"), QStringLiteral("v2b"),
                                QStringLiteral("h-v2b")));

    BlobBaselineStore base(dbIn(dir));
    QVERIFY(base.setBaseline(QStringLiteral("m"), QStringLiteral("r1"),
                             QStringLiteral("h-v1")));

    ConflictHandlerRegistry reg;
    TestHandler handler;
    handler.decision = ConflictDecision::UseSource;
    reg.registerHandler(QStringLiteral("a"), &handler);

    ConflictStore store;
    ConflictPolicy policy;

    BlobSyncEngine eng;
    BlobSyncResult r = eng.twoWayWithBaseline(
        &a, &b, QStringLiteral("col"), QStringLiteral("m"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(handler.invocations, 1);
    auto bRecs = b.loadRecords(QStringLiteral("col"));
    QCOMPARE(bRecs.size(), 1);
    QCOMPARE(bRecs.first().contentHash, QStringLiteral("h-v2a"));
}

void TestBlobSyncEngine::twoWayWithBaseline_conflictSkipPersists()
{
    using namespace Kalburator::Sync::QSyncCore;
    QTemporaryDir dir; QVERIFY(dir.isValid());
    IdentifiedMock a(QStringLiteral("a")), b(QStringLiteral("b"));
    seed(a, QStringLiteral("col")); seed(b, QStringLiteral("col"));

    a.createRecord(QStringLiteral("col"),
                   hashedRecord(QStringLiteral("r1"), QStringLiteral("v2a"),
                                QStringLiteral("h-v2a")));
    b.createRecord(QStringLiteral("col"),
                   hashedRecord(QStringLiteral("r1"), QStringLiteral("v2b"),
                                QStringLiteral("h-v2b")));

    BlobBaselineStore base(dbIn(dir));
    QVERIFY(base.setBaseline(QStringLiteral("m"), QStringLiteral("r1"),
                             QStringLiteral("h-v1")));

    ConflictHandlerRegistry reg;
    TestHandler handler;
    handler.decision = ConflictDecision::Skip;
    reg.registerHandler(QStringLiteral("a"), &handler);

    ConflictStore store;
    ConflictPolicy policy;

    BlobSyncEngine eng;
    BlobSyncResult r = eng.twoWayWithBaseline(
        &a, &b, QStringLiteral("col"), QStringLiteral("m"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(handler.invocations, 1);
    QCOMPARE(r.sourceStats.conflicts, 1);
    QCOMPARE(store.pendingConflicts().size(), 1);

    QCOMPARE(a.loadRecords(QStringLiteral("col")).first().contentHash,
             QStringLiteral("h-v2a"));
    QCOMPARE(b.loadRecords(QStringLiteral("col")).first().contentHash,
             QStringLiteral("h-v2b"));
}

QTEST_MAIN(TestBlobSyncEngine)
#include "tst_blobsyncengine.moc"
