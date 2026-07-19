#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QCryptographicHash>

#include "mockblobbackend.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::MockBlobBackend;

namespace {

BackendRecord makeRecord(const QString &id, const QString &data)
{
    BackendRecord r;
    r.id = id;
    r.type = QStringLiteral("memo");
    r.displayName = id;
    r.data = data.toUtf8();
    r.contentHash = QStringLiteral("hash-of-%1").arg(data);
    r.lastModified = QDateTime::currentDateTimeUtc();
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

} // namespace

class TestMockBlobBackend : public QObject
{
    Q_OBJECT
private slots:
    void identityAndAvailability();
    void emptyBackendReportsEmpty();
    void createCollectionAndQuery();
    void recordCrudRoundTrip();
    void modifiedSinceFiltersByTime();
    void deletedSinceTracksDeletions();
    void failureInjectionOnLoadRecords();
    void failureInjectionOnCreateRecord();
    void recordCreatedSignalFires();
    void loadRecordsOrError_reportsInjectedFailure();
    void wipeCollection_defaultImpl_emptiesCollection();
    void computesContentHashWhenIncomingEmpty();  // O47
};

void TestMockBlobBackend::identityAndAvailability()
{
    MockBlobBackend b;
    QVERIFY(!b.backendId().isEmpty());
    QVERIFY(!b.displayName().isEmpty());
    QVERIFY(b.isAvailable());
    QVERIFY(b.supportsDeleteTracking());
    QVERIFY(!b.supportsBatch());
}

void TestMockBlobBackend::emptyBackendReportsEmpty()
{
    MockBlobBackend b;
    QVERIFY(b.availableCollections().isEmpty());
    QVERIFY(b.loadRecords(QStringLiteral("anything")).isEmpty());
    QVERIFY(!b.loadRecord(QStringLiteral("anything")).has_value());
    QVERIFY(b.modifiedSince(QStringLiteral("anything"), {}).isEmpty());
    QVERIFY(b.deletedSince(QStringLiteral("anything"), {}).isEmpty());
}

void TestMockBlobBackend::createCollectionAndQuery()
{
    MockBlobBackend b;
    QCOMPARE(b.createCollection(makeCollection(QStringLiteral("memos"))),
             QStringLiteral("memos"));
    QCOMPARE(b.availableCollections().size(), 1);
    QCOMPARE(b.collectionInfo(QStringLiteral("memos")).name,
             QStringLiteral("memos"));
}

void TestMockBlobBackend::recordCrudRoundTrip()
{
    MockBlobBackend b;
    b.createCollection(makeCollection(QStringLiteral("memos")));

    const auto rec = makeRecord(QStringLiteral("r-1"), QStringLiteral("hello"));
    QCOMPARE(b.createRecord(QStringLiteral("memos"), rec), QStringLiteral("r-1"));

    const auto loaded = b.loadRecord(QStringLiteral("r-1"));
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->data, QByteArrayLiteral("hello"));

    auto updated = rec;
    updated.data = QByteArrayLiteral("goodbye");
    updated.contentHash = QStringLiteral("hash-of-goodbye");
    QVERIFY(b.updateRecord(updated));
    QCOMPARE(b.loadRecord(QStringLiteral("r-1"))->data, QByteArrayLiteral("goodbye"));

    QVERIFY(b.deleteRecord(QStringLiteral("r-1")));
    QVERIFY(!b.loadRecord(QStringLiteral("r-1")).has_value());
}

void TestMockBlobBackend::modifiedSinceFiltersByTime()
{
    MockBlobBackend b;
    b.createCollection(makeCollection(QStringLiteral("memos")));

    auto old = makeRecord(QStringLiteral("r-old"), QStringLiteral("o"));
    old.lastModified = QDateTime::currentDateTimeUtc().addSecs(-3600);
    b.createRecord(QStringLiteral("memos"), old);

    auto fresh = makeRecord(QStringLiteral("r-new"), QStringLiteral("n"));
    fresh.lastModified = QDateTime::currentDateTimeUtc();
    b.createRecord(QStringLiteral("memos"), fresh);

    const auto since = QDateTime::currentDateTimeUtc().addSecs(-60);
    const auto filtered = b.modifiedSince(QStringLiteral("memos"), since);
    QCOMPARE(filtered.size(), 1);
    QCOMPARE(filtered.first().id, QStringLiteral("r-new"));
}

void TestMockBlobBackend::deletedSinceTracksDeletions()
{
    MockBlobBackend b;
    b.createCollection(makeCollection(QStringLiteral("memos")));
    b.createRecord(QStringLiteral("memos"), makeRecord(QStringLiteral("r-1"), QStringLiteral("x")));
    b.deleteRecord(QStringLiteral("r-1"));
    const auto deleted = b.deletedSince(QStringLiteral("memos"), {});
    QCOMPARE(deleted, QStringList{QStringLiteral("r-1")});
}

void TestMockBlobBackend::failureInjectionOnLoadRecords()
{
    MockBlobBackend b;
    b.createCollection(makeCollection(QStringLiteral("memos")));
    b.createRecord(QStringLiteral("memos"), makeRecord(QStringLiteral("r-1"), QStringLiteral("x")));

    QSignalSpy errSpy(&b, &Kalburator::Sync::MockBlobBackend::errorOccurred);
    b.setFailNext(MockBlobBackend::FailurePoint::OnLoadRecords, 1);

    QVERIFY(b.loadRecords(QStringLiteral("memos")).isEmpty()); // first call fails
    QCOMPARE(errSpy.size(), 1);
    QCOMPARE(b.loadRecords(QStringLiteral("memos")).size(), 1); // second call succeeds
}

void TestMockBlobBackend::failureInjectionOnCreateRecord()
{
    MockBlobBackend b;
    b.createCollection(makeCollection(QStringLiteral("memos")));
    b.setFailNext(MockBlobBackend::FailurePoint::OnCreateRecord, 2);
    QVERIFY(b.createRecord(QStringLiteral("memos"), makeRecord(QStringLiteral("r-1"), QStringLiteral("x"))).isEmpty());
    QVERIFY(b.createRecord(QStringLiteral("memos"), makeRecord(QStringLiteral("r-2"), QStringLiteral("y"))).isEmpty());
    QCOMPARE(b.createRecord(QStringLiteral("memos"), makeRecord(QStringLiteral("r-3"), QStringLiteral("z"))),
             QStringLiteral("r-3"));
}

void TestMockBlobBackend::recordCreatedSignalFires()
{
    MockBlobBackend b;
    b.createCollection(makeCollection(QStringLiteral("memos")));
    QSignalSpy spy(&b, &Kalburator::Sync::MockBlobBackend::recordCreated);
    b.createRecord(QStringLiteral("memos"), makeRecord(QStringLiteral("r-1"), QStringLiteral("x")));
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.first().first().toString(), QStringLiteral("r-1"));
}

void TestMockBlobBackend::loadRecordsOrError_reportsInjectedFailure()
{
    MockBlobBackend b;
    b.createCollection(makeCollection(QStringLiteral("memos")));
    b.createRecord(QStringLiteral("memos"),
                   makeRecord(QStringLiteral("r-1"), QStringLiteral("x")));

    QSignalSpy errSpy(&b, &Kalburator::Sync::MockBlobBackend::errorOccurred);
    b.setFailNext(MockBlobBackend::FailurePoint::OnLoadRecords, 1);

    QList<BackendRecord> records;
    QString error;
    const bool ok = b.loadRecordsOrError(QStringLiteral("memos"), records, error);

    // Before the fix: ok == true, error empty, records empty — a silent false-green.
    QVERIFY(!ok);
    QVERIFY(!error.isEmpty());
    QVERIFY(records.isEmpty());
    QCOMPARE(errSpy.size(), 1); // failure path must emit errorOccurred

    // Failure was one-shot: the next call succeeds and returns the record.
    QVERIFY(b.loadRecordsOrError(QStringLiteral("memos"), records, error));
    QVERIFY(error.isEmpty());
    QCOMPARE(records.size(), 1);
    QCOMPARE(errSpy.size(), 1); // success path emits no new signal
}

// v0.65 — IBlobBackend::wipeCollection default implementation
// (loadRecords + deleteRecord). MockBlobBackend does not override it, so
// this exercises the interface-provided default: N records in, empty out,
// collection itself still present and usable.
void TestMockBlobBackend::wipeCollection_defaultImpl_emptiesCollection()
{
    MockBlobBackend b;
    b.createCollection(makeCollection(QStringLiteral("memos")));
    for (int i = 0; i < 5; ++i) {
        b.createRecord(QStringLiteral("memos"),
                       makeRecord(QStringLiteral("r-%1").arg(i),
                                  QStringLiteral("data-%1").arg(i)));
    }
    QCOMPARE(b.loadRecords(QStringLiteral("memos")).size(), 5);

    QVERIFY(b.wipeCollection(QStringLiteral("memos")));

    // Records gone; collection still exists and accepts new records.
    QVERIFY(b.loadRecords(QStringLiteral("memos")).isEmpty());
    QCOMPARE(b.collectionInfo(QStringLiteral("memos")).id, QStringLiteral("memos"));
    QCOMPARE(b.createRecord(QStringLiteral("memos"),
                            makeRecord(QStringLiteral("fresh"), QStringLiteral("x"))),
             QStringLiteral("fresh"));
}

// O47 — the mock must populate BackendRecord::contentHash like the production
// blob backends (LocalBlobBackend/GenericSqliteBackend) do, so mock-based sync
// tests exercise the real per-record diff instead of always hitting
// perrecorddiff's fail-loud empty-hash branch (spurious BothModified conflicts
// on the second pass of a TwoWay/AskUser sync). See
// docs/2026-07-19-consumer-coordination-status.md O47.
void TestMockBlobBackend::computesContentHashWhenIncomingEmpty()
{
    MockBlobBackend b;
    b.createCollection(makeCollection(QStringLiteral("memos")));

    const QByteArray data = QByteArrayLiteral("hello world");
    const QString expected = QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());

    // Incoming record has an EMPTY contentHash — the mock must compute it.
    BackendRecord rec;
    rec.id = QStringLiteral("r-1");
    rec.data = data;
    rec.contentHash.clear();
    b.createRecord(QStringLiteral("memos"), rec);

    const auto created = b.loadRecord(QStringLiteral("r-1"));
    QVERIFY(created.has_value());
    QCOMPARE(created->contentHash, expected);

    // updateRecord with a fresh body + empty hash must recompute, too.
    BackendRecord upd = rec;
    upd.data = QByteArrayLiteral("goodbye world");
    upd.contentHash.clear();
    const QString expectedUpd = QString::fromLatin1(
        QCryptographicHash::hash(upd.data, QCryptographicHash::Sha256).toHex());
    QVERIFY(b.updateRecord(upd));
    QCOMPARE(b.loadRecord(QStringLiteral("r-1"))->contentHash, expectedUpd);

    // A caller-supplied non-empty hash is preserved (never clobbered).
    BackendRecord pre;
    pre.id = QStringLiteral("r-2");
    pre.data = QByteArrayLiteral("anything");
    pre.contentHash = QStringLiteral("caller-supplied");
    b.createRecord(QStringLiteral("memos"), pre);
    QCOMPARE(b.loadRecord(QStringLiteral("r-2"))->contentHash,
             QStringLiteral("caller-supplied"));
}

QTEST_MAIN(TestMockBlobBackend)
#include "tst_mockblobbackend.moc"
