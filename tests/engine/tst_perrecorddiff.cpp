#include <QtTest/QtTest>

#include "backendrecord.h"
#include "baselineentry.h"
#include "blobdomaindefinition.h"
#include "enginediff.h"
#include "perrecorddiff.h"
#include "recorddiffer.h"
#include "synctypes.h"

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

// Pre-B4-style baseline: identical hash recorded for both sides. This is
// what every test in this file meant before the per-side split (none of
// them cross backend serializations), so it keeps the pre-existing cases
// asserting the same thing they always did.
BaselineEntry makeBaseline(const BackendRecord &rec)
{
    BaselineEntry e;
    e.id = rec.id;
    e.sourceHash = rec.contentHash;
    e.targetHash = rec.contentHash;
    return e;
}

// Phase B4: a baseline whose source/target hashes were captured from two
// DIFFERENT native serializations of the same logical record — modeling a
// LocalBackend vs. a CalDAV backend's byte-for-byte-different (but
// semantically identical) serialization of one calendar event.
BaselineEntry makeSplitBaseline(const QString &id,
                                const QString &sourceHash,
                                const QString &targetHash)
{
    BaselineEntry e;
    e.id = id;
    e.sourceHash = sourceHash;
    e.targetHash = targetHash;
    return e;
}

} // namespace

class TstPerRecordDiff : public QObject {
    Q_OBJECT
private slots:
    void unchangedRecord_returnsEmptyDiff();
    void createOnlySource_emitsToTargetCreate();
    void updateOnSource_emitsToTargetUpdate();
    void deleteOnSource_emitsToTargetDelete();
    void mirrorAToB_pushesSourceToTarget();
    void mirrorBToA_pushesTargetToSource();

    // Phase B4 (N2 fix): per-side baseline hashes. Each case below uses
    // DIFFERENT source/target native serializations of equal content — the
    // exact scenario that made every record read "modified" forever
    // pre-fix, since the old single-hash baseline could never match both
    // sides' native bytes at once.
    void perSideBaseline_bothSidesMatchOwnHash_noOp();
    void perSideBaseline_onlySourceBytesChanged_emitsToTargetUpdate();
    void perSideBaseline_onlyTargetBytesChanged_emitsToSourceUpdate();
    void perSideBaseline_bothSidesChanged_emitsConflict();
    void perSideBaseline_sourceDeletedTargetUnchanged_emitsToTargetDelete();
    void perSideBaseline_targetDeletedSourceUnchanged_emitsToSourceDelete();
};

void TstPerRecordDiff::unchangedRecord_returnsEmptyDiff()
{
    const auto rec = makeRecord(QStringLiteral("r"), QStringLiteral("v"));
    BlobDomainDefinition dom;
    auto differ = dom.createCanonicalDiffer();
    const EngineDiff d = perRecordDiff(
        {rec}, {rec}, {makeBaseline(rec)}, dom.canonicalShape(), *differ);
    QCOMPARE(d.totalOperations(), 0);
    QVERIFY(d.toSource.isEmpty());
    QVERIFY(d.toTarget.isEmpty());
}

void TstPerRecordDiff::createOnlySource_emitsToTargetCreate()
{
    const auto rec = makeRecord(QStringLiteral("new"), QStringLiteral("hi"));
    BlobDomainDefinition dom;
    auto differ = dom.createCanonicalDiffer();
    const EngineDiff d = perRecordDiff(
        {rec}, {}, {}, dom.canonicalShape(), *differ);
    QCOMPARE(d.toSource.size(), 0);
    QCOMPARE(d.toTarget.size(), 1);
    QCOMPARE(d.toTarget.first().kind, EngineDiffOp::Kind::Create);
    QCOMPARE(d.toTarget.first().record.id, rec.id);
}

void TstPerRecordDiff::updateOnSource_emitsToTargetUpdate()
{
    const auto v1 = makeRecord(QStringLiteral("r"), QStringLiteral("v1"));
    const auto v2 = makeRecord(QStringLiteral("r"), QStringLiteral("v2"));
    BlobDomainDefinition dom;
    auto differ = dom.createCanonicalDiffer();
    const EngineDiff d = perRecordDiff(
        {v2}, {v1}, {makeBaseline(v1)}, dom.canonicalShape(), *differ);
    QCOMPARE(d.toSource.size(), 0);
    QCOMPARE(d.toTarget.size(), 1);
    QCOMPARE(d.toTarget.first().kind, EngineDiffOp::Kind::Update);
    QCOMPARE(d.toTarget.first().record.data, v2.data);
}

void TstPerRecordDiff::deleteOnSource_emitsToTargetDelete()
{
    const auto rec = makeRecord(QStringLiteral("r"), QStringLiteral("v"));
    BlobDomainDefinition dom;
    auto differ = dom.createCanonicalDiffer();
    const EngineDiff d = perRecordDiff(
        {}, {rec}, {makeBaseline(rec)}, dom.canonicalShape(), *differ);
    QCOMPARE(d.toTarget.size(), 1);
    QCOMPARE(d.toTarget.first().kind, EngineDiffOp::Kind::Delete);
}

void TstPerRecordDiff::perSideBaseline_bothSidesMatchOwnHash_noOp()
{
    // Same logical content, DIFFERENT native bytes/hashes per side (as if
    // one came from LocalBackend, the other from a CalDAV server) — but
    // each side's current hash matches ITS OWN baseline hash. Pre-fix, a
    // single shared baseline hash could never equal both native hashes at
    // once, so this case always regressed to "both changed" -> conflict.
    BackendRecord src = makeRecord(QStringLiteral("r"), QStringLiteral("v"));
    src.contentHash = QStringLiteral("local-hash-v");
    BackendRecord tgt = makeRecord(QStringLiteral("r"), QStringLiteral("v"));
    tgt.contentHash = QStringLiteral("caldav-hash-v");
    const auto baseline = makeSplitBaseline(QStringLiteral("r"),
                                            QStringLiteral("local-hash-v"),
                                            QStringLiteral("caldav-hash-v"));

    BlobDomainDefinition dom;
    auto differ = dom.createCanonicalDiffer();
    const EngineDiff d = perRecordDiff(
        {src}, {tgt}, {baseline}, dom.canonicalShape(), *differ);

    QCOMPARE(d.totalOperations(), 0);
    QVERIFY(!d.hasConflicts());
}

void TstPerRecordDiff::perSideBaseline_onlySourceBytesChanged_emitsToTargetUpdate()
{
    BackendRecord src = makeRecord(QStringLiteral("r"), QStringLiteral("v2"));
    src.contentHash = QStringLiteral("local-hash-v2");   // changed from baseline
    BackendRecord tgt = makeRecord(QStringLiteral("r"), QStringLiteral("v1"));
    tgt.contentHash = QStringLiteral("caldav-hash-v1");  // unchanged from baseline
    const auto baseline = makeSplitBaseline(QStringLiteral("r"),
                                            QStringLiteral("local-hash-v1"),
                                            QStringLiteral("caldav-hash-v1"));

    BlobDomainDefinition dom;
    auto differ = dom.createCanonicalDiffer();
    const EngineDiff d = perRecordDiff(
        {src}, {tgt}, {baseline}, dom.canonicalShape(), *differ);

    QCOMPARE(d.toSource.size(), 0);
    QCOMPARE(d.toTarget.size(), 1);
    QCOMPARE(d.toTarget.first().kind, EngineDiffOp::Kind::Update);
    QVERIFY(!d.hasConflicts());
}

void TstPerRecordDiff::perSideBaseline_onlyTargetBytesChanged_emitsToSourceUpdate()
{
    BackendRecord src = makeRecord(QStringLiteral("r"), QStringLiteral("v1"));
    src.contentHash = QStringLiteral("local-hash-v1");   // unchanged from baseline
    BackendRecord tgt = makeRecord(QStringLiteral("r"), QStringLiteral("v2"));
    tgt.contentHash = QStringLiteral("caldav-hash-v2");  // changed from baseline
    const auto baseline = makeSplitBaseline(QStringLiteral("r"),
                                            QStringLiteral("local-hash-v1"),
                                            QStringLiteral("caldav-hash-v1"));

    BlobDomainDefinition dom;
    auto differ = dom.createCanonicalDiffer();
    const EngineDiff d = perRecordDiff(
        {src}, {tgt}, {baseline}, dom.canonicalShape(), *differ);

    QCOMPARE(d.toTarget.size(), 0);
    QCOMPARE(d.toSource.size(), 1);
    QCOMPARE(d.toSource.first().kind, EngineDiffOp::Kind::Update);
    QVERIFY(!d.hasConflicts());
}

void TstPerRecordDiff::perSideBaseline_bothSidesChanged_emitsConflict()
{
    BackendRecord src = makeRecord(QStringLiteral("r"), QStringLiteral("v2"));
    src.contentHash = QStringLiteral("local-hash-v2");
    BackendRecord tgt = makeRecord(QStringLiteral("r"), QStringLiteral("v3"));
    tgt.contentHash = QStringLiteral("caldav-hash-v3");
    const auto baseline = makeSplitBaseline(QStringLiteral("r"),
                                            QStringLiteral("local-hash-v1"),
                                            QStringLiteral("caldav-hash-v1"));

    BlobDomainDefinition dom;
    auto differ = dom.createCanonicalDiffer();
    const EngineDiff d = perRecordDiff(
        {src}, {tgt}, {baseline}, dom.canonicalShape(), *differ);

    QVERIFY(d.hasConflicts());
    QCOMPARE(d.toTarget.size(), 1);
    QCOMPARE(d.toTarget.first().kind, EngineDiffOp::Kind::Conflict);
}

void TstPerRecordDiff::perSideBaseline_sourceDeletedTargetUnchanged_emitsToTargetDelete()
{
    BackendRecord tgt = makeRecord(QStringLiteral("r"), QStringLiteral("v1"));
    tgt.contentHash = QStringLiteral("caldav-hash-v1");  // matches baseline's targetHash
    const auto baseline = makeSplitBaseline(QStringLiteral("r"),
                                            QStringLiteral("local-hash-v1"),
                                            QStringLiteral("caldav-hash-v1"));

    BlobDomainDefinition dom;
    auto differ = dom.createCanonicalDiffer();
    const EngineDiff d = perRecordDiff(
        {}, {tgt}, {baseline}, dom.canonicalShape(), *differ);

    QCOMPARE(d.toTarget.size(), 1);
    QCOMPARE(d.toTarget.first().kind, EngineDiffOp::Kind::Delete);
    QVERIFY(!d.hasConflicts());
}

void TstPerRecordDiff::perSideBaseline_targetDeletedSourceUnchanged_emitsToSourceDelete()
{
    BackendRecord src = makeRecord(QStringLiteral("r"), QStringLiteral("v1"));
    src.contentHash = QStringLiteral("local-hash-v1");  // matches baseline's sourceHash
    const auto baseline = makeSplitBaseline(QStringLiteral("r"),
                                            QStringLiteral("local-hash-v1"),
                                            QStringLiteral("caldav-hash-v1"));

    BlobDomainDefinition dom;
    auto differ = dom.createCanonicalDiffer();
    const EngineDiff d = perRecordDiff(
        {src}, {}, {baseline}, dom.canonicalShape(), *differ);

    QCOMPARE(d.toSource.size(), 1);
    QCOMPARE(d.toSource.first().kind, EngineDiffOp::Kind::Delete);
    QVERIFY(!d.hasConflicts());
}

void TstPerRecordDiff::mirrorAToB_pushesSourceToTarget()
{
    using Kalburator::Engine::mergeMirrorAToB;
    EngineDiff d;
    EngineDiffOp op;
    op.kind = EngineDiffOp::Kind::Create;
    op.record = makeRecord(QStringLiteral("r"), QStringLiteral("v"));
    d.toTarget.append(op);
    const auto m = mergeMirrorAToB(d);
    QCOMPARE(m.finalTarget.size(), 1);
    QCOMPARE(m.finalTarget.first().id, op.record.id);
}

void TstPerRecordDiff::mirrorBToA_pushesTargetToSource()
{
    using Kalburator::Engine::mergeMirrorBToA;
    EngineDiff d;
    EngineDiffOp op;
    op.kind = EngineDiffOp::Kind::Create;
    op.record = makeRecord(QStringLiteral("r"), QStringLiteral("v"));
    d.toSource.append(op);
    const auto m = mergeMirrorBToA(d);
    QCOMPARE(m.finalSource.size(), 1);
    QCOMPARE(m.finalSource.first().id, op.record.id);
}

QTEST_GUILESS_MAIN(TstPerRecordDiff)
#include "tst_perrecorddiff.moc"
