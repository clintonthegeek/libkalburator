#include <QtTest/QtTest>

#include "backendcapabilities.h"
#include "backendrecord.h"
#include "blobbatchdiff.h"
#include "enginediff.h"
#include "synctypes.h"

using Kalburator::Sync::BackendCapabilities;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::blobBatchDiff;
using Kalburator::Sync::ConflictResolution;
using Kalburator::Engine::EngineDiff;
using Kalburator::Engine::EngineDiffOp;

namespace {

BackendRecord makeRecord(const QString &id, const QString &payload)
{
    BackendRecord r;
    r.id = id;
    r.type = QStringLiteral("memo");
    r.displayName = id;
    r.data = payload.toUtf8();
    r.contentHash = QStringLiteral("hash-of-%1").arg(payload);
    r.lastModified = QDateTime::currentDateTimeUtc();
    return r;
}

} // namespace

/// Phase Ia.5 Task 16: BlobDomainAdapter was folded into the free
/// function blobBatchDiff() in src/blob/blobbatchdiff.{h,cpp}. This
/// test pins the diff's behavior at the new home; the test file name
/// is preserved so phase tags / commit history line up.
class TstBlobDomainAdapter : public QObject {
    Q_OBJECT

private slots:
    void hashEqualityDetection_returnsUnchanged();
    void createOnlyDiff_returnsToTargetCreate();
    void updateDiff_returnsToTargetUpdate();
    void deleteDiff_returnsToTargetDelete();
};

void TstBlobDomainAdapter::hashEqualityDetection_returnsUnchanged()
{
    const auto rec = makeRecord(QStringLiteral("rec-1"), QStringLiteral("v1"));

    const QList<BackendRecord> source   = {rec};
    const QList<BackendRecord> target   = {rec};
    const QList<BackendRecord> baseline = {rec};

    const EngineDiff d = blobBatchDiff(source, target, baseline,
                                       BackendCapabilities{}, BackendCapabilities{});

    QCOMPARE(d.totalOperations(), 0);
    QVERIFY(!d.hasConflicts());
    QVERIFY(d.toSource.isEmpty());
    QVERIFY(d.toTarget.isEmpty());
}

void TstBlobDomainAdapter::createOnlyDiff_returnsToTargetCreate()
{
    const auto rec = makeRecord(QStringLiteral("rec-new"),
                                QStringLiteral("hello"));

    const QList<BackendRecord> source   = {rec};
    const QList<BackendRecord> target   = {};
    const QList<BackendRecord> baseline = {};

    const EngineDiff d = blobBatchDiff(source, target, baseline,
                                       BackendCapabilities{}, BackendCapabilities{});

    QCOMPARE(d.toSource.size(), 0);
    QCOMPARE(d.toTarget.size(), 1);
    QCOMPARE(d.toTarget.first().kind, EngineDiffOp::Kind::Create);
    QCOMPARE(d.toTarget.first().record.id, rec.id);
    QCOMPARE(d.toTarget.first().record.contentHash, rec.contentHash);
}

void TstBlobDomainAdapter::updateDiff_returnsToTargetUpdate()
{
    const auto v1 = makeRecord(QStringLiteral("rec-1"), QStringLiteral("v1"));
    const auto v2 = makeRecord(QStringLiteral("rec-1"), QStringLiteral("v2"));

    // Source has v2, target still has v1, baseline is v1.
    const QList<BackendRecord> source   = {v2};
    const QList<BackendRecord> target   = {v1};
    const QList<BackendRecord> baseline = {v1};

    const EngineDiff d = blobBatchDiff(source, target, baseline,
                                       BackendCapabilities{}, BackendCapabilities{});

    QCOMPARE(d.toSource.size(), 0);
    QCOMPARE(d.toTarget.size(), 1);
    QCOMPARE(d.toTarget.first().kind, EngineDiffOp::Kind::Update);
    QCOMPARE(d.toTarget.first().record.id, v2.id);
    QCOMPARE(d.toTarget.first().record.contentHash, v2.contentHash);
    QCOMPARE(d.toTarget.first().baselineRecord.contentHash, v1.contentHash);
    QVERIFY(!d.hasConflicts());
}

void TstBlobDomainAdapter::deleteDiff_returnsToTargetDelete()
{
    const auto rec = makeRecord(QStringLiteral("rec-doomed"),
                                QStringLiteral("v1"));

    // Source absent, target still has record, baseline shows it existed.
    const QList<BackendRecord> source   = {};
    const QList<BackendRecord> target   = {rec};
    const QList<BackendRecord> baseline = {rec};

    const EngineDiff d = blobBatchDiff(source, target, baseline,
                                       BackendCapabilities{}, BackendCapabilities{});

    QCOMPARE(d.toSource.size(), 0);
    QCOMPARE(d.toTarget.size(), 1);
    QCOMPARE(d.toTarget.first().kind, EngineDiffOp::Kind::Delete);
    QCOMPARE(d.toTarget.first().record.id, rec.id);
    QCOMPARE(d.toTarget.first().baselineRecord.id, rec.id);
    QVERIFY(!d.hasConflicts());
}

QTEST_GUILESS_MAIN(TstBlobDomainAdapter)
#include "tst_blob_domain_adapter.moc"
