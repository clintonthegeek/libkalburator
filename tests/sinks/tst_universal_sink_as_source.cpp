/// G.8 Task 61 — Universal sink used as source: write+restore round-trip.
/// Verifies that both RawFilesBackend and GenericSqliteBackend can be
/// written to by one backend instance and read back by a second instance
/// pointing at the same storage, simulating the source role in a sync pair.

#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "rawfilesbackend.h"
#include "genericsqlitebackend.h"
#include "collectioninfo.h"
#include "backendrecord.h"
#include "shape.h"

using Kalburator::Sinks::GenericSqliteBackend;
using Kalburator::Sinks::RawFilesBackend;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::Shape;

namespace {

CollectionInfo makeCollection(const QString &id)
{
    CollectionInfo ci;
    ci.id = id;
    ci.name = id;
    ci.type = QStringLiteral("memos");
    return ci;
}

// K.9: universal sinks now require a per-collection shape. These
// storage round-trip tests don't care about shape semantics, so use a
// single synthetic test shape.
const Shape kTestShape{ DomainId{"test"}, EncodingId{"raw"} };

BackendRecord makeRecord(const QString &id, const QByteArray &data)
{
    BackendRecord r;
    r.id = id;
    r.displayName = id;
    r.data = data;
    r.contentHash = QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    r.lastModified = QDateTime::currentDateTimeUtc();
    r.type = QStringLiteral("raw");
    return r;
}

} // namespace

class TestUniversalSinkAsSource : public QObject
{
    Q_OBJECT
private slots:
    void rawFiles_writeReadRoundTrip();
    void genericSqlite_writeReadRoundTrip();
    void rawFiles_multiShape_isolatedCollections();
    void genericSqlite_multiShape_isolatedCollections();
};

void TestUniversalSinkAsSource::rawFiles_writeReadRoundTrip()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString colId = QStringLiteral("memo+plaintext");

    // Writer instance
    {
        RawFilesBackend writer(tmp.path());
        writer.createCollection(makeCollection(colId), kTestShape);
        writer.createRecord(colId, makeRecord(QStringLiteral("rec-a"), QByteArrayLiteral("alpha")));
        writer.createRecord(colId, makeRecord(QStringLiteral("rec-b"), QByteArrayLiteral("beta")));
        writer.createRecord(colId, makeRecord(QStringLiteral("rec-c"), QByteArrayLiteral("gamma")));
    }

    // Reader (source) instance
    RawFilesBackend reader(tmp.path());
    const auto cols = reader.availableCollections();
    QCOMPARE(cols.size(), 1);
    QCOMPARE(cols.first().id, colId);

    const auto records = reader.loadRecords(colId);
    QCOMPARE(records.size(), 3);

    QList<QByteArray> recovered;
    for (const auto &r : records)
        recovered << r.data;
    std::sort(recovered.begin(), recovered.end());
    QCOMPARE(recovered, QList<QByteArray>({
        QByteArrayLiteral("alpha"),
        QByteArrayLiteral("beta"),
        QByteArrayLiteral("gamma")}));
}

void TestUniversalSinkAsSource::genericSqlite_writeReadRoundTrip()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath = tmp.filePath(QStringLiteral("sink.db"));
    const QString colId = QStringLiteral("memo+plaintext");

    // Writer instance
    {
        GenericSqliteBackend writer(dbPath);
        writer.createCollection(makeCollection(colId), kTestShape);
        writer.createRecord(colId, makeRecord(QStringLiteral("r1"), QByteArrayLiteral("one")));
        writer.createRecord(colId, makeRecord(QStringLiteral("r2"), QByteArrayLiteral("two")));
    }

    // Reader (source) instance
    GenericSqliteBackend reader(dbPath);
    const auto cols = reader.availableCollections();
    QCOMPARE(cols.size(), 1);
    QCOMPARE(cols.first().id, colId);

    const auto records = reader.loadRecords(colId);
    QCOMPARE(records.size(), 2);

    QList<QByteArray> recovered;
    for (const auto &r : records)
        recovered << r.data;
    std::sort(recovered.begin(), recovered.end());
    QCOMPARE(recovered, QList<QByteArray>({QByteArrayLiteral("one"), QByteArrayLiteral("two")}));
}

void TestUniversalSinkAsSource::rawFiles_multiShape_isolatedCollections()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    RawFilesBackend b(tmp.path());
    b.createCollection(makeCollection(QStringLiteral("memo+plaintext")), kTestShape);
    b.createCollection(makeCollection(QStringLiteral("todo+ical")), kTestShape);

    b.createRecord(QStringLiteral("memo+plaintext"),
                   makeRecord(QStringLiteral("m1"), QByteArrayLiteral("memo-data")));
    b.createRecord(QStringLiteral("todo+ical"),
                   makeRecord(QStringLiteral("t1"), QByteArrayLiteral("todo-data")));

    // Confirm isolation
    QCOMPARE(b.loadRecords(QStringLiteral("memo+plaintext")).size(), 1);
    QCOMPARE(b.loadRecords(QStringLiteral("todo+ical")).size(), 1);
    QCOMPARE(b.loadRecords(QStringLiteral("memo+plaintext")).first().data,
             QByteArrayLiteral("memo-data"));
    QCOMPARE(b.loadRecords(QStringLiteral("todo+ical")).first().data,
             QByteArrayLiteral("todo-data"));
}

void TestUniversalSinkAsSource::genericSqlite_multiShape_isolatedCollections()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    GenericSqliteBackend b(tmp.filePath(QStringLiteral("ms.db")));
    b.createCollection(makeCollection(QStringLiteral("memo+plaintext")), kTestShape);
    b.createCollection(makeCollection(QStringLiteral("contacts+vcard")), kTestShape);

    b.createRecord(QStringLiteral("memo+plaintext"),
                   makeRecord(QStringLiteral("m1"), QByteArrayLiteral("memo-bytes")));
    b.createRecord(QStringLiteral("contacts+vcard"),
                   makeRecord(QStringLiteral("c1"), QByteArrayLiteral("vcard-bytes")));

    QCOMPARE(b.loadRecords(QStringLiteral("memo+plaintext")).size(), 1);
    QCOMPARE(b.loadRecords(QStringLiteral("contacts+vcard")).size(), 1);
    QCOMPARE(b.loadRecords(QStringLiteral("memo+plaintext")).first().data,
             QByteArrayLiteral("memo-bytes"));
    QCOMPARE(b.loadRecords(QStringLiteral("contacts+vcard")).first().data,
             QByteArrayLiteral("vcard-bytes"));
}

QTEST_MAIN(TestUniversalSinkAsSource)
#include "tst_universal_sink_as_source.moc"
