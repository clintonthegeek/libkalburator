#include <QTest>

#include "defaultblobwriter.h"
#include "mockblobbackend.h"

using namespace Kalburator::Shape;
using namespace Kalburator::Sync;

class TestDefaultBlobWriter : public QObject {
    Q_OBJECT
private slots:
    void appliesCreate();
    void appliesUpdate();
    void appliesDelete();
    void returnsFalseOnFailure();
};

void TestDefaultBlobWriter::appliesCreate()
{
    MockBlobBackend mock;
    DefaultBlobWriter w(&mock);

    BackendRecord r;
    r.id = QStringLiteral("rec-1");
    r.data = QByteArrayLiteral("hello");

    QVERIFY(w.apply(QStringLiteral("col"), {r}, {}, {}));
    QCOMPARE(mock.recordsIn(QStringLiteral("col")).size(), 1);
    QCOMPARE(mock.recordsIn(QStringLiteral("col")).value(QStringLiteral("rec-1")).data,
             QByteArrayLiteral("hello"));
}

void TestDefaultBlobWriter::appliesUpdate()
{
    MockBlobBackend mock;
    DefaultBlobWriter w(&mock);

    // Create the record first so updateRecord can find it.
    BackendRecord r;
    r.id = QStringLiteral("rec-1");
    r.data = QByteArrayLiteral("initial");
    QVERIFY(w.apply(QStringLiteral("col"), {r}, {}, {}));

    // Now update it via the update path.
    r.data = QByteArrayLiteral("updated");
    QVERIFY(w.apply(QStringLiteral("col"), {}, {r}, {}));

    QCOMPARE(mock.recordsIn(QStringLiteral("col")).size(), 1);
    QCOMPARE(mock.recordsIn(QStringLiteral("col")).value(QStringLiteral("rec-1")).data,
             QByteArrayLiteral("updated"));
}

void TestDefaultBlobWriter::appliesDelete()
{
    MockBlobBackend mock;
    DefaultBlobWriter w(&mock);

    // Create a record then delete it.
    BackendRecord r;
    r.id = QStringLiteral("rec-1");
    r.data = QByteArrayLiteral("bye");
    QVERIFY(w.apply(QStringLiteral("col"), {r}, {}, {}));
    QCOMPARE(mock.recordsIn(QStringLiteral("col")).size(), 1);

    QVERIFY(w.apply(QStringLiteral("col"), {}, {}, {QStringLiteral("rec-1")}));
    QCOMPARE(mock.recordsIn(QStringLiteral("col")).size(), 0);
}

void TestDefaultBlobWriter::returnsFalseOnFailure()
{
    MockBlobBackend mock;
    DefaultBlobWriter w(&mock);

    mock.setFailNext(MockBlobBackend::FailurePoint::OnCreateRecord, 1);

    BackendRecord r;
    r.id = QStringLiteral("rec-1");
    r.data = QByteArrayLiteral("hello");

    QVERIFY(!w.apply(QStringLiteral("col"), {r}, {}, {}));
    QCOMPARE(mock.recordsIn(QStringLiteral("col")).size(), 0);
}

QTEST_GUILESS_MAIN(TestDefaultBlobWriter)
#include "tst_default_blob_writer.moc"
