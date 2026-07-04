#include <QtTest/QtTest>

#include "backendrecord.h"
#include "baselineentry.h"
#include "blobdomaindefinition.h"
#include "enginediff.h"
#include "perrecorddiff.h"
#include "recorddiffer.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Engine::BaselineEntry;
using Kalburator::Engine::EngineDiff;
using Kalburator::Engine::EngineDiffOp;
using Kalburator::Engine::perRecordDiff;
using Kalburator::Blob::BlobDomainDefinition;

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

// Pre-B4-style baseline: same hash on both sides (what every caller in this
// file meant before the per-side split — these tests never exercised
// cross-serialization).
BaselineEntry makeBaseline(const BackendRecord &rec)
{
    BaselineEntry e;
    e.id = rec.id;
    e.sourceHash = rec.contentHash;
    e.targetHash = rec.contentHash;
    return e;
}

} // namespace

/// Phase Ia.5 Task 16: BlobDomainAdapter was folded into the free
/// function blobBatchDiff() in src/blob/blobbatchdiff.{h,cpp}. This
/// test pins the diff's behavior at the new home; the test file name
/// is preserved so phase tags / commit history line up.
///
/// Phase N.1: migrated from blobBatchDiff to perRecordDiff.
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

    BlobDomainDefinition dom;
    auto differ = dom.createCanonicalDiffer();
    const EngineDiff d = perRecordDiff(
        {rec}, {rec}, {makeBaseline(rec)}, dom.canonicalShape(), *differ);

    QCOMPARE(d.totalOperations(), 0);
    QVERIFY(!d.hasConflicts());
    QVERIFY(d.toSource.isEmpty());
    QVERIFY(d.toTarget.isEmpty());
}

void TstBlobDomainAdapter::createOnlyDiff_returnsToTargetCreate()
{
    const auto rec = makeRecord(QStringLiteral("rec-new"),
                                QStringLiteral("hello"));

    BlobDomainDefinition dom;
    auto differ = dom.createCanonicalDiffer();
    const EngineDiff d = perRecordDiff(
        {rec}, {}, {}, dom.canonicalShape(), *differ);

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
    BlobDomainDefinition dom;
    auto differ = dom.createCanonicalDiffer();
    const EngineDiff d = perRecordDiff(
        {v2}, {v1}, {makeBaseline(v1)}, dom.canonicalShape(), *differ);

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
    BlobDomainDefinition dom;
    auto differ = dom.createCanonicalDiffer();
    const EngineDiff d = perRecordDiff(
        {}, {rec}, {makeBaseline(rec)}, dom.canonicalShape(), *differ);

    QCOMPARE(d.toSource.size(), 0);
    QCOMPARE(d.toTarget.size(), 1);
    QCOMPARE(d.toTarget.first().kind, EngineDiffOp::Kind::Delete);
    QCOMPARE(d.toTarget.first().record.id, rec.id);
    QCOMPARE(d.toTarget.first().baselineRecord.id, rec.id);
    QVERIFY(!d.hasConflicts());
}

QTEST_GUILESS_MAIN(TstBlobDomainAdapter)
#include "tst_blob_domain_adapter.moc"
