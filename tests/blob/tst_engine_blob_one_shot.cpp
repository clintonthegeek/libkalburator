/// F1 Task 6 — SyncEngine's one-shot blob facade
///
/// Mirrors the scenarios in tst_blobsyncengine but drives them through
/// Kalburator::Sync::SyncEngine::runBlobTwoWay / runBlobMirror. The
/// bodies were lifted from BlobSyncEngine; behavior parity is the
/// contract pinned here. Tasks 9-10 migrate WildPalms to this API
/// and delete BlobSyncEngine.

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "syncengine.h"
#include "iblobbackend.h"
#include "mockblobbackend.h"
#include "blobbaselinestore.h"
#include "conflicthandlerregistry.h"
#include "conflictpolicy.h"
#include "conflictrecord.h"
#include "conflictstore.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::BlobBaselineStore;
using Kalburator::Sync::BlobSyncResult;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::MockBlobBackend;
using Kalburator::Sync::SyncEngine;

namespace {

class IdentifiedMock : public MockBlobBackend
{
public:
    explicit IdentifiedMock(const QString &id, QObject *p = nullptr)
        : MockBlobBackend(p), m_id(id) {}
    QString backendId() const override { return m_id; }
private:
    QString m_id;
};

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

BackendRecord hashedRecord(const QString &id, const QString &data,
                           const QString &hash)
{
    BackendRecord r = makeRecord(id, data);
    r.contentHash = hash;
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

QString dbIn(const QTemporaryDir &d)
{
    return d.filePath(QStringLiteral(".kalburator-sync.db"));
}

} // namespace

class TestEngineBlobOneShot : public QObject
{
    Q_OBJECT
private slots:
    // runBlobMirror
    void mirror_copiesSourceToEmptyTarget();
    void mirror_leavesMatchingRecordsAlone();
    void mirror_updatesChangedRecords();
    void mirror_deletesOrphansInTarget();
    void mirror_nullBackendReturnsFailure();
    void mirror_reportsTargetCreateErrors();

    // runBlobTwoWay
    void twoWay_noChanges();
    void twoWay_modifiedOnAOnly();
    void twoWay_modifiedOnBOnly();
    void twoWay_deletedOnA();
    void twoWay_deletedOnB();
    void twoWay_newOnA();
    void twoWay_newOnB();
    void twoWay_conflictInvokesHandler();
    void twoWay_conflictSkipPersistsToStore();
    void twoWay_nullBaselineReturnsFailure();
};

void TestEngineBlobOneShot::mirror_copiesSourceToEmptyTarget()
{
    MockBlobBackend src, tgt;
    seed(src, QStringLiteral("memos"));
    seed(tgt, QStringLiteral("memos"));
    src.createRecord(QStringLiteral("memos"),
                     makeRecord(QStringLiteral("r-1"), QStringLiteral("a")));
    src.createRecord(QStringLiteral("memos"),
                     makeRecord(QStringLiteral("r-2"), QStringLiteral("b")));

    SyncEngine eng(nullptr, nullptr);
    const auto r = eng.runBlobMirror(&src, &tgt, QStringLiteral("memos"));
    QVERIFY(r.success);
    QCOMPARE(r.targetStats.created, 2);
    QCOMPARE(tgt.recordsIn(QStringLiteral("memos")).size(), 2);
}

void TestEngineBlobOneShot::mirror_leavesMatchingRecordsAlone()
{
    MockBlobBackend src, tgt;
    seed(src, QStringLiteral("memos"));
    seed(tgt, QStringLiteral("memos"));
    const auto rec = makeRecord(QStringLiteral("r-1"), QStringLiteral("same"));
    src.createRecord(QStringLiteral("memos"), rec);
    tgt.createRecord(QStringLiteral("memos"), rec);

    SyncEngine eng(nullptr, nullptr);
    const auto r = eng.runBlobMirror(&src, &tgt, QStringLiteral("memos"));
    QCOMPARE(r.targetStats.unchanged, 1);
    QCOMPARE(r.targetStats.updated, 0);
    QCOMPARE(r.targetStats.created, 0);
}

void TestEngineBlobOneShot::mirror_updatesChangedRecords()
{
    MockBlobBackend src, tgt;
    seed(src, QStringLiteral("memos"));
    seed(tgt, QStringLiteral("memos"));
    src.createRecord(QStringLiteral("memos"),
                     makeRecord(QStringLiteral("r-1"), QStringLiteral("new")));
    tgt.createRecord(QStringLiteral("memos"),
                     makeRecord(QStringLiteral("r-1"), QStringLiteral("old")));

    SyncEngine eng(nullptr, nullptr);
    const auto r = eng.runBlobMirror(&src, &tgt, QStringLiteral("memos"));
    QCOMPARE(r.targetStats.updated, 1);
    QCOMPARE(tgt.recordsIn(QStringLiteral("memos"))
                 .value(QStringLiteral("r-1")).data,
             QByteArrayLiteral("new"));
}

void TestEngineBlobOneShot::mirror_deletesOrphansInTarget()
{
    MockBlobBackend src, tgt;
    seed(src, QStringLiteral("memos"));
    seed(tgt, QStringLiteral("memos"));
    tgt.createRecord(QStringLiteral("memos"),
                     makeRecord(QStringLiteral("r-orphan"), QStringLiteral("x")));

    SyncEngine eng(nullptr, nullptr);
    const auto r = eng.runBlobMirror(&src, &tgt, QStringLiteral("memos"));
    QCOMPARE(r.targetStats.deleted, 1);
    QVERIFY(tgt.recordsIn(QStringLiteral("memos")).isEmpty());
}

void TestEngineBlobOneShot::mirror_nullBackendReturnsFailure()
{
    SyncEngine eng(nullptr, nullptr);
    MockBlobBackend tgt;
    const auto r = eng.runBlobMirror(nullptr, &tgt, QStringLiteral("memos"));
    QVERIFY(!r.success);
    QVERIFY(!r.errorMessage.isEmpty());
}

void TestEngineBlobOneShot::mirror_reportsTargetCreateErrors()
{
    MockBlobBackend src, tgt;
    seed(src, QStringLiteral("memos"));
    seed(tgt, QStringLiteral("memos"));
    src.createRecord(QStringLiteral("memos"),
                     makeRecord(QStringLiteral("r-1"), QStringLiteral("a")));
    tgt.setFailNext(MockBlobBackend::FailurePoint::OnCreateRecord, 1);

    SyncEngine eng(nullptr, nullptr);
    const auto r = eng.runBlobMirror(&src, &tgt, QStringLiteral("memos"));
    QVERIFY(!r.success);
    QCOMPARE(r.targetStats.errors, 1);
}

void TestEngineBlobOneShot::twoWay_noChanges()
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
    QVERIFY(base.setBaseline(QStringLiteral("a"), QStringLiteral("col"),
                             QStringLiteral("r1"), QStringLiteral("h1")));

    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;

    SyncEngine eng(nullptr, nullptr);
    const auto r = eng.runBlobTwoWay(
        &a, &b, QStringLiteral("col"), QStringLiteral("m"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(r.sourceStats.unchanged + r.targetStats.unchanged, 2);
    QCOMPARE(r.sourceStats.conflicts + r.targetStats.conflicts, 0);
}

void TestEngineBlobOneShot::twoWay_modifiedOnAOnly()
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
    QVERIFY(base.setBaseline(QStringLiteral("a"), QStringLiteral("col"),
                             QStringLiteral("r1"), QStringLiteral("h-v1")));

    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;

    SyncEngine eng(nullptr, nullptr);
    const auto r = eng.runBlobTwoWay(
        &a, &b, QStringLiteral("col"), QStringLiteral("m"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    auto bRecs = b.loadRecords(QStringLiteral("col"));
    QCOMPARE(bRecs.size(), 1);
    QCOMPARE(bRecs.first().contentHash, QStringLiteral("h-v2"));
    QCOMPARE(r.targetStats.updated, 1);
}

void TestEngineBlobOneShot::twoWay_modifiedOnBOnly()
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
    QVERIFY(base.setBaseline(QStringLiteral("a"), QStringLiteral("col"),
                             QStringLiteral("r1"), QStringLiteral("h-v1")));

    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;

    SyncEngine eng(nullptr, nullptr);
    const auto r = eng.runBlobTwoWay(
        &a, &b, QStringLiteral("col"), QStringLiteral("m"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    auto aRecs = a.loadRecords(QStringLiteral("col"));
    QCOMPARE(aRecs.size(), 1);
    QCOMPARE(aRecs.first().contentHash, QStringLiteral("h-v2"));
    QCOMPARE(r.sourceStats.updated, 1);
}

void TestEngineBlobOneShot::twoWay_deletedOnA()
{
    using namespace Kalburator::Sync::QSyncCore;
    QTemporaryDir dir; QVERIFY(dir.isValid());
    IdentifiedMock a(QStringLiteral("a")), b(QStringLiteral("b"));
    seed(a, QStringLiteral("col")); seed(b, QStringLiteral("col"));

    b.createRecord(QStringLiteral("col"),
                   hashedRecord(QStringLiteral("r1"), QStringLiteral("v1"),
                                QStringLiteral("h-v1")));

    BlobBaselineStore base(dbIn(dir));
    QVERIFY(base.setBaseline(QStringLiteral("a"), QStringLiteral("col"),
                             QStringLiteral("r1"), QStringLiteral("h-v1")));

    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;

    SyncEngine eng(nullptr, nullptr);
    const auto r = eng.runBlobTwoWay(
        &a, &b, QStringLiteral("col"), QStringLiteral("m"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(b.loadRecords(QStringLiteral("col")).size(), 0);
    QCOMPARE(r.targetStats.deleted, 1);
}

void TestEngineBlobOneShot::twoWay_deletedOnB()
{
    using namespace Kalburator::Sync::QSyncCore;
    QTemporaryDir dir; QVERIFY(dir.isValid());
    IdentifiedMock a(QStringLiteral("a")), b(QStringLiteral("b"));
    seed(a, QStringLiteral("col")); seed(b, QStringLiteral("col"));

    a.createRecord(QStringLiteral("col"),
                   hashedRecord(QStringLiteral("r1"), QStringLiteral("v1"),
                                QStringLiteral("h-v1")));

    BlobBaselineStore base(dbIn(dir));
    QVERIFY(base.setBaseline(QStringLiteral("a"), QStringLiteral("col"),
                             QStringLiteral("r1"), QStringLiteral("h-v1")));

    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;

    SyncEngine eng(nullptr, nullptr);
    const auto r = eng.runBlobTwoWay(
        &a, &b, QStringLiteral("col"), QStringLiteral("m"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(a.loadRecords(QStringLiteral("col")).size(), 0);
    QCOMPARE(r.sourceStats.deleted, 1);
}

void TestEngineBlobOneShot::twoWay_newOnA()
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

    SyncEngine eng(nullptr, nullptr);
    const auto r = eng.runBlobTwoWay(
        &a, &b, QStringLiteral("col"), QStringLiteral("m"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(b.loadRecords(QStringLiteral("col")).size(), 1);
    QCOMPARE(r.targetStats.created, 1);
}

void TestEngineBlobOneShot::twoWay_newOnB()
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

    SyncEngine eng(nullptr, nullptr);
    const auto r = eng.runBlobTwoWay(
        &a, &b, QStringLiteral("col"), QStringLiteral("m"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(a.loadRecords(QStringLiteral("col")).size(), 1);
    QCOMPARE(r.sourceStats.created, 1);
}

void TestEngineBlobOneShot::twoWay_conflictInvokesHandler()
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
    QVERIFY(base.setBaseline(QStringLiteral("a"), QStringLiteral("col"),
                             QStringLiteral("r1"), QStringLiteral("h-v1")));

    ConflictHandlerRegistry reg;
    TestHandler handler;
    handler.decision = ConflictDecision::UseSource;
    reg.registerHandler(QStringLiteral("a"), &handler);

    ConflictStore store;
    ConflictPolicy policy;

    SyncEngine eng(nullptr, nullptr);
    const auto r = eng.runBlobTwoWay(
        &a, &b, QStringLiteral("col"), QStringLiteral("m"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(handler.invocations, 1);
    auto bRecs = b.loadRecords(QStringLiteral("col"));
    QCOMPARE(bRecs.size(), 1);
    QCOMPARE(bRecs.first().contentHash, QStringLiteral("h-v2a"));
}

void TestEngineBlobOneShot::twoWay_conflictSkipPersistsToStore()
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
    QVERIFY(base.setBaseline(QStringLiteral("a"), QStringLiteral("col"),
                             QStringLiteral("r1"), QStringLiteral("h-v1")));

    ConflictHandlerRegistry reg;
    TestHandler handler;
    handler.decision = ConflictDecision::Skip;
    reg.registerHandler(QStringLiteral("a"), &handler);

    ConflictStore store;
    ConflictPolicy policy;

    SyncEngine eng(nullptr, nullptr);
    const auto r = eng.runBlobTwoWay(
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

void TestEngineBlobOneShot::twoWay_nullBaselineReturnsFailure()
{
    using namespace Kalburator::Sync::QSyncCore;
    IdentifiedMock a(QStringLiteral("a")), b(QStringLiteral("b"));
    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;

    SyncEngine eng(nullptr, nullptr);
    const auto r = eng.runBlobTwoWay(
        &a, &b, QStringLiteral("col"), QStringLiteral("m"),
        nullptr, &reg, &store, policy);

    QVERIFY(!r.success);
    QVERIFY(!r.errorMessage.isEmpty());
}

QTEST_MAIN(TestEngineBlobOneShot)
#include "tst_engine_blob_one_shot.moc"
