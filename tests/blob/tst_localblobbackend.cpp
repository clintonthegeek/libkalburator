#include <QtTest/QtTest>
#include <QCryptographicHash>
#include <QDir>
#include <QTemporaryDir>

#include "localblobbackend.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::LocalBlobBackend;

namespace {

BackendRecord makeRecord(const QString &id, const QString &data)
{
    BackendRecord r;
    r.id = id;
    r.displayName = id;
    r.type = QStringLiteral("memo");
    r.data = data.toUtf8();
    return r;
}

CollectionInfo makeCollection(const QString &id, const QString &type)
{
    CollectionInfo c;
    c.id = id;
    c.name = id;
    c.type = type;
    return c;
}

} // namespace

class TestLocalBlobBackend : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { m_dir.setAutoRemove(true); QVERIFY(m_dir.isValid()); }

    void isAvailableTrueForExistingDir();
    void isAvailableFalseForMissingDir();
    void createCollectionMakesSubdir();
    void createRecordWritesFileWithCorrectExtension();
    void loadRecordsReadsAllFilesInCollection();
    void updateRecordRewritesFile();
    void deleteRecordRemovesFile();
    void contentHashIsSha256OfData();
    void modifiedSinceFiltersByMtime();

private:
    QTemporaryDir m_dir;
    QString base() const { return m_dir.path(); }
};

void TestLocalBlobBackend::isAvailableTrueForExistingDir()
{
    LocalBlobBackend b(base());
    QVERIFY(b.isAvailable());
}

void TestLocalBlobBackend::isAvailableFalseForMissingDir()
{
    LocalBlobBackend b(QStringLiteral("/no/such/path/kalburator/test"));
    QVERIFY(!b.isAvailable());
}

void TestLocalBlobBackend::createCollectionMakesSubdir()
{
    LocalBlobBackend b(base());
    QCOMPARE(b.createCollection(makeCollection(QStringLiteral("memos"), QStringLiteral("memos"))),
             QStringLiteral("memos"));
    QVERIFY(QDir(base() + QStringLiteral("/memos")).exists());
    QCOMPARE(b.availableCollections().size(), 1);
}

void TestLocalBlobBackend::createRecordWritesFileWithCorrectExtension()
{
    LocalBlobBackend b(base());
    b.createCollection(makeCollection(QStringLiteral("memos"), QStringLiteral("memos")));
    const QString recPath = b.createRecord(QStringLiteral("memos"),
                                           makeRecord(QStringLiteral("r-1"), QStringLiteral("hello")));
    QVERIFY(!recPath.isEmpty());
    QVERIFY(recPath.endsWith(QStringLiteral(".md")));
    QVERIFY(QFile::exists(recPath));
}

void TestLocalBlobBackend::loadRecordsReadsAllFilesInCollection()
{
    LocalBlobBackend b(base());
    b.createCollection(makeCollection(QStringLiteral("memos"), QStringLiteral("memos")));
    b.createRecord(QStringLiteral("memos"), makeRecord(QStringLiteral("r-1"), QStringLiteral("a")));
    b.createRecord(QStringLiteral("memos"), makeRecord(QStringLiteral("r-2"), QStringLiteral("b")));
    QCOMPARE(b.loadRecords(QStringLiteral("memos")).size(), 2);
}

void TestLocalBlobBackend::updateRecordRewritesFile()
{
    LocalBlobBackend b(base());
    b.createCollection(makeCollection(QStringLiteral("memos"), QStringLiteral("memos")));
    const QString recPath = b.createRecord(QStringLiteral("memos"),
                                           makeRecord(QStringLiteral("r-1"), QStringLiteral("before")));
    auto updated = makeRecord(recPath, QStringLiteral("after"));
    QVERIFY(b.updateRecord(updated));
    const auto loaded = b.loadRecord(recPath);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->data, QByteArrayLiteral("after"));
}

void TestLocalBlobBackend::deleteRecordRemovesFile()
{
    LocalBlobBackend b(base());
    b.createCollection(makeCollection(QStringLiteral("memos"), QStringLiteral("memos")));
    const QString recPath = b.createRecord(QStringLiteral("memos"),
                                           makeRecord(QStringLiteral("r-1"), QStringLiteral("x")));
    QVERIFY(QFile::exists(recPath));
    QVERIFY(b.deleteRecord(recPath));
    QVERIFY(!QFile::exists(recPath));
}

void TestLocalBlobBackend::contentHashIsSha256OfData()
{
    LocalBlobBackend b(base());
    b.createCollection(makeCollection(QStringLiteral("memos"), QStringLiteral("memos")));
    const QString recPath = b.createRecord(QStringLiteral("memos"),
                                           makeRecord(QStringLiteral("r-1"), QStringLiteral("payload")));
    const auto loaded = b.loadRecord(recPath);
    QVERIFY(loaded.has_value());
    const QByteArray expected = QCryptographicHash::hash(
        QByteArrayLiteral("payload"), QCryptographicHash::Sha256).toHex();
    QCOMPARE(loaded->contentHash, QString::fromLatin1(expected));
}

void TestLocalBlobBackend::modifiedSinceFiltersByMtime()
{
    LocalBlobBackend b(base());
    b.createCollection(makeCollection(QStringLiteral("memos"), QStringLiteral("memos")));
    const QString p1 = b.createRecord(QStringLiteral("memos"),
                                      makeRecord(QStringLiteral("r-old"), QStringLiteral("o")));
    QTest::qSleep(1100); // advance mtime resolution beyond 1s
    const QDateTime cutoff = QDateTime::currentDateTimeUtc();
    QTest::qSleep(100);
    const QString p2 = b.createRecord(QStringLiteral("memos"),
                                      makeRecord(QStringLiteral("r-new"), QStringLiteral("n")));
    Q_UNUSED(p1);
    Q_UNUSED(p2);

    const auto filtered = b.modifiedSince(QStringLiteral("memos"), cutoff);
    QCOMPARE(filtered.size(), 1);
    QVERIFY(filtered.first().data == QByteArrayLiteral("n"));
}

QTEST_MAIN(TestLocalBlobBackend)
#include "tst_localblobbackend.moc"
