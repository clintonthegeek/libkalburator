#include <QtTest/QtTest>

#include "backendrecord.h"
#include "blobdomaindefinition.h"
#include "enginediff.h"
#include "perrecorddiff.h"
#include "recorddiffer.h"
#include "synctypes.h"

using Kalburator::Sync::BackendRecord;
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
};

void TstPerRecordDiff::unchangedRecord_returnsEmptyDiff()
{
    const auto rec = makeRecord(QStringLiteral("r"), QStringLiteral("v"));
    BlobDomainDefinition dom;
    auto differ = dom.createCanonicalDiffer();
    const EngineDiff d = perRecordDiff(
        {rec}, {rec}, {rec}, dom.canonicalShape(), *differ);
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
        {v2}, {v1}, {v1}, dom.canonicalShape(), *differ);
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
        {}, {rec}, {rec}, dom.canonicalShape(), *differ);
    QCOMPARE(d.toTarget.size(), 1);
    QCOMPARE(d.toTarget.first().kind, EngineDiffOp::Kind::Delete);
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
