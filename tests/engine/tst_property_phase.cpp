// Phase Ia.5 Task 7: tests for the generic property-phase helper.
//
// This file currently exercises only the pure `computeMapDiff` helper.
// Coverage for the orchestrator `SyncEngineWorker::runPropertyPhase`
// is deferred to Task 13's integration tests, which will already have
// the dispatch wiring needed to drive the engine end-to-end. The
// orchestrator is small (~30 lines) and its non-trivial logic lives
// in `computeMapDiff`, so unit-testing the helper plus integration
// coverage downstream is the proportional choice.

#include <QtTest/QtTest>

#include "propertydiff.h"

using namespace Kalburator::Sync;

class TestPropertyPhase : public QObject
{
    Q_OBJECT

private slots:
    // All inputs empty: no changes, no conflicts.
    void computeMapDiff_emptyAllReturnsNothing();

    // Source added a key the baseline lacked; target unchanged.
    void computeMapDiff_srcAddedKey_propagatesToTarget();

    // Target added a key the baseline lacked; source unchanged.
    void computeMapDiff_tgtAddedKey_propagatesToSource();

    // Both src and tgt independently set a key to the same new value.
    void computeMapDiff_bothChangedSameValue_noConflict();

    // Both diverged from baseline to different values: conflict.
    void computeMapDiff_bothChangedDifferently_yieldsConflict();

    // Neither changed from baseline: no diff entries.
    void computeMapDiff_neitherChanged_noDiff();

    // Source modified an existing baseline key.
    void computeMapDiff_srcModifiedKey_propagatesToTarget();
};

void TestPropertyPhase::computeMapDiff_emptyAllReturnsNothing()
{
    const PropertyDiff diff = computeMapDiff({}, {}, {});
    QVERIFY(diff.toApplyToTarget.isEmpty());
    QVERIFY(diff.toApplyToSource.isEmpty());
    QVERIFY(diff.conflicts.isEmpty());
    QVERIFY(!diff.hasChanges());
}

void TestPropertyPhase::computeMapDiff_srcAddedKey_propagatesToTarget()
{
    QVariantMap src;
    src.insert(QStringLiteral("color"), QStringLiteral("#ff0000"));
    const QVariantMap tgt;
    const QVariantMap base;

    const PropertyDiff diff = computeMapDiff(src, tgt, base);

    QCOMPARE(diff.toApplyToTarget.size(), 1);
    QCOMPARE(diff.toApplyToTarget.value(QStringLiteral("color")).toString(),
             QStringLiteral("#ff0000"));
    QVERIFY(diff.toApplyToSource.isEmpty());
    QVERIFY(diff.conflicts.isEmpty());
    QVERIFY(diff.hasChanges());
}

void TestPropertyPhase::computeMapDiff_tgtAddedKey_propagatesToSource()
{
    const QVariantMap src;
    QVariantMap tgt;
    tgt.insert(QStringLiteral("description"), QStringLiteral("Project plan"));
    const QVariantMap base;

    const PropertyDiff diff = computeMapDiff(src, tgt, base);

    QVERIFY(diff.toApplyToTarget.isEmpty());
    QCOMPARE(diff.toApplyToSource.size(), 1);
    QCOMPARE(diff.toApplyToSource.value(QStringLiteral("description")).toString(),
             QStringLiteral("Project plan"));
    QVERIFY(diff.conflicts.isEmpty());
    QVERIFY(diff.hasChanges());
}

void TestPropertyPhase::computeMapDiff_bothChangedSameValue_noConflict()
{
    QVariantMap src;
    src.insert(QStringLiteral("color"), QStringLiteral("#00ff00"));
    QVariantMap tgt;
    tgt.insert(QStringLiteral("color"), QStringLiteral("#00ff00"));
    QVariantMap base;
    base.insert(QStringLiteral("color"), QStringLiteral("#0000ff"));

    const PropertyDiff diff = computeMapDiff(src, tgt, base);

    // Already converged: no apply needed in either direction.
    QVERIFY(diff.toApplyToTarget.isEmpty());
    QVERIFY(diff.toApplyToSource.isEmpty());
    QVERIFY(diff.conflicts.isEmpty());
    QVERIFY(!diff.hasChanges());
}

void TestPropertyPhase::computeMapDiff_bothChangedDifferently_yieldsConflict()
{
    QVariantMap src;
    src.insert(QStringLiteral("color"), QStringLiteral("#ff0000"));
    QVariantMap tgt;
    tgt.insert(QStringLiteral("color"), QStringLiteral("#0000ff"));
    QVariantMap base;
    base.insert(QStringLiteral("color"), QStringLiteral("#00ff00"));

    const PropertyDiff diff = computeMapDiff(src, tgt, base);

    QVERIFY(diff.toApplyToTarget.isEmpty());
    QVERIFY(diff.toApplyToSource.isEmpty());
    QCOMPARE(diff.conflicts.size(), 1);
    QCOMPARE(diff.conflicts.first(), QStringLiteral("color"));
}

void TestPropertyPhase::computeMapDiff_neitherChanged_noDiff()
{
    QVariantMap src;
    src.insert(QStringLiteral("color"), QStringLiteral("#abcdef"));
    src.insert(QStringLiteral("description"), QStringLiteral("Notes"));
    const QVariantMap tgt = src;
    const QVariantMap base = src;

    const PropertyDiff diff = computeMapDiff(src, tgt, base);

    QVERIFY(diff.toApplyToTarget.isEmpty());
    QVERIFY(diff.toApplyToSource.isEmpty());
    QVERIFY(diff.conflicts.isEmpty());
    QVERIFY(!diff.hasChanges());
}

void TestPropertyPhase::computeMapDiff_srcModifiedKey_propagatesToTarget()
{
    QVariantMap src;
    src.insert(QStringLiteral("description"), QStringLiteral("Updated"));
    QVariantMap tgt;
    tgt.insert(QStringLiteral("description"), QStringLiteral("Original"));
    QVariantMap base;
    base.insert(QStringLiteral("description"), QStringLiteral("Original"));

    const PropertyDiff diff = computeMapDiff(src, tgt, base);

    QCOMPARE(diff.toApplyToTarget.size(), 1);
    QCOMPARE(diff.toApplyToTarget.value(QStringLiteral("description")).toString(),
             QStringLiteral("Updated"));
    QVERIFY(diff.toApplyToSource.isEmpty());
    QVERIFY(diff.conflicts.isEmpty());
}

QTEST_GUILESS_MAIN(TestPropertyPhase)
#include "tst_property_phase.moc"
