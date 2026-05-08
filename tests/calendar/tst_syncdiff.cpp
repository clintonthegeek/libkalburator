#include <QTest>
#include <QTemporaryDir>

#include "syncdiff.h"
#include "synctypes.h"

#include <KCalendarCore/Event>
#include <KCalendarCore/Todo>
#include <KCalendarCore/ICalFormat>

namespace Kalburator::Sync {}
using namespace Kalburator::Sync;


using namespace KCalendarCore;

class TestSyncDiff : public QObject
{
    Q_OBJECT

private:
    Event::Ptr createEvent(const QString &uid, const QString &summary,
                           const QDateTime &start = QDateTime::currentDateTime())
    {
        Event::Ptr event = Event::Ptr::create();
        event->setUid(uid);
        event->setSummary(summary);
        event->setDtStart(start);
        event->setDtEnd(start.addSecs(3600));
        return event;
    }

    QString serializeIncidence(const Incidence::Ptr &inc)
    {
        KCalendarCore::ICalFormat format;
        return format.toICalString(inc);
    }

private slots:
    void initTestCase()
    {
        qRegisterMetaType<SyncRecord>();
        qRegisterMetaType<SyncDiff>();
    }

    // ========================================================================
    // SyncRecord Tests
    // ========================================================================

    void testSyncRecordFromIncidence()
    {
        Event::Ptr event = createEvent("test-uid-1", "Test Event");
        SyncRecord record = SyncRecord::fromIncidence(event, "calendar1", "local");

        QCOMPARE(record.uid, QStringLiteral("test-uid-1"));
        QCOMPARE(record.calendarId, QStringLiteral("calendar1"));
        QCOMPARE(record.backendId, QStringLiteral("local"));
        QVERIFY(!record.icalData.isEmpty());
        QVERIFY(!record.versionHash.isEmpty());
        QVERIFY(record.isValid());
    }

    void testSyncRecordHashConsistency()
    {
        // Same content should produce same hash
        Event::Ptr event1 = createEvent("uid", "Test", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));
        Event::Ptr event2 = createEvent("uid", "Test", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));

        SyncRecord rec1 = SyncRecord::fromIncidence(event1, "cal", "local");
        SyncRecord rec2 = SyncRecord::fromIncidence(event2, "cal", "local");

        QCOMPARE(rec1.versionHash, rec2.versionHash);
    }

    void testSyncRecordHashDiffers()
    {
        // Different content should produce different hash
        Event::Ptr event1 = createEvent("uid", "Test Event 1");
        Event::Ptr event2 = createEvent("uid", "Test Event 2");

        SyncRecord rec1 = SyncRecord::fromIncidence(event1, "cal", "local");
        SyncRecord rec2 = SyncRecord::fromIncidence(event2, "cal", "local");

        QVERIFY(rec1.versionHash != rec2.versionHash);
    }

    // ========================================================================
    // Sync Diff - No Changes
    // ========================================================================

    void testEmptyDiff()
    {
        QList<SyncRecord> source;
        QList<SyncRecord> target;
        QMap<QString, QString> baselines;

        SyncDiff diff = computeSyncDiff(source, target, baselines, SyncMode::TwoWay);

        QVERIFY(diff.toTarget.isEmpty());
        QVERIFY(diff.toSource.isEmpty());
        QVERIFY(diff.conflicts.isEmpty());
        QVERIFY(!diff.hasChanges());
    }

    void testNoChanges()
    {
        Event::Ptr event = createEvent("uid1", "Test", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));
        SyncRecord record = SyncRecord::fromIncidence(event, "cal", "local");

        QList<SyncRecord> source = {record};
        QList<SyncRecord> target = {record};
        QMap<QString, QString> baselines;
        baselines["uid1"] = record.icalData;

        SyncDiff diff = computeSyncDiff(source, target, baselines, SyncMode::TwoWay);

        QVERIFY(diff.toTarget.isEmpty());
        QVERIFY(diff.toSource.isEmpty());
        QVERIFY(diff.conflicts.isEmpty());
        QVERIFY(!diff.hasChanges());
        QCOMPARE(diff.unchangedUids.size(), 1);
    }

    // ========================================================================
    // Sync Diff - One-Way Upload (Source -> Target)
    // ========================================================================

    void testOneWayUploadNewItem()
    {
        Event::Ptr event = createEvent("uid1", "New Event");
        SyncRecord record = SyncRecord::fromIncidence(event, "cal", "local");

        QList<SyncRecord> source = {record};
        QList<SyncRecord> target;  // Empty target
        QMap<QString, QString> baselines;  // No baseline

        SyncDiff diff = computeSyncDiff(source, target, baselines, SyncMode::OneWayUpload);

        QCOMPARE(diff.toTarget.size(), 1);
        QCOMPARE(diff.toTarget[0].type, SyncChangeType::Created);
        QCOMPARE(diff.toTarget[0].uid, QStringLiteral("uid1"));
        QVERIFY(diff.toSource.isEmpty());  // One-way: no changes to source
    }

    void testOneWayUploadModifiedItem()
    {
        Event::Ptr oldEvent = createEvent("uid1", "Original", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));
        Event::Ptr newEvent = createEvent("uid1", "Modified", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));

        SyncRecord oldRecord = SyncRecord::fromIncidence(oldEvent, "cal", "local");
        SyncRecord newRecord = SyncRecord::fromIncidence(newEvent, "cal", "local");

        QList<SyncRecord> source = {newRecord};  // Modified on source
        QList<SyncRecord> target = {oldRecord};  // Old on target
        QMap<QString, QString> baselines;
        baselines["uid1"] = oldRecord.icalData;  // Baseline is old state

        SyncDiff diff = computeSyncDiff(source, target, baselines, SyncMode::OneWayUpload);

        QCOMPARE(diff.toTarget.size(), 1);
        QCOMPARE(diff.toTarget[0].type, SyncChangeType::Modified);
        QVERIFY(diff.toSource.isEmpty());
    }

    void testOneWayUploadDeletedItem()
    {
        Event::Ptr event = createEvent("uid1", "Will Be Deleted");
        SyncRecord record = SyncRecord::fromIncidence(event, "cal", "local");

        QList<SyncRecord> source;  // Deleted on source
        QList<SyncRecord> target = {record};  // Still on target
        QMap<QString, QString> baselines;
        baselines["uid1"] = record.icalData;

        SyncDiff diff = computeSyncDiff(source, target, baselines, SyncMode::OneWayUpload);

        QCOMPARE(diff.toTarget.size(), 1);
        QCOMPARE(diff.toTarget[0].type, SyncChangeType::Deleted);
        QVERIFY(diff.toSource.isEmpty());
    }

    // ========================================================================
    // Sync Diff - One-Way Download (Target -> Source)
    // ========================================================================

    void testOneWayDownloadNewItem()
    {
        Event::Ptr event = createEvent("uid1", "New Event");
        SyncRecord record = SyncRecord::fromIncidence(event, "cal", "remote");

        QList<SyncRecord> source;  // Empty source
        QList<SyncRecord> target = {record};  // New on target
        QMap<QString, QString> baselines;

        SyncDiff diff = computeSyncDiff(source, target, baselines, SyncMode::OneWayDownload);

        QCOMPARE(diff.toSource.size(), 1);
        QCOMPARE(diff.toSource[0].type, SyncChangeType::Created);
        QVERIFY(diff.toTarget.isEmpty());  // One-way: no changes to target
    }

    // ========================================================================
    // Sync Diff - Two-Way Sync
    // ========================================================================

    void testTwoWayBothSidesCreate()
    {
        Event::Ptr sourceEvent = createEvent("uid-source", "Source Event");
        Event::Ptr targetEvent = createEvent("uid-target", "Target Event");

        SyncRecord sourceRecord = SyncRecord::fromIncidence(sourceEvent, "cal", "local");
        SyncRecord targetRecord = SyncRecord::fromIncidence(targetEvent, "cal", "remote");

        QList<SyncRecord> source = {sourceRecord};
        QList<SyncRecord> target = {targetRecord};
        QMap<QString, QString> baselines;

        SyncDiff diff = computeSyncDiff(source, target, baselines, SyncMode::TwoWay);

        // Source item should go to target
        QCOMPARE(diff.toTarget.size(), 1);
        QCOMPARE(diff.toTarget[0].uid, QStringLiteral("uid-source"));

        // Target item should go to source
        QCOMPARE(diff.toSource.size(), 1);
        QCOMPARE(diff.toSource[0].uid, QStringLiteral("uid-target"));
    }

    // ========================================================================
    // 3-Way Merge Decision Matrix (TestInvariants.md Section 2.1)
    // Tests all 10 combinations from SyncEngineArchitecture.md
    // ========================================================================

    void testMergeMatrix_UnchangedBoth()
    {
        // Case 1: Both unchanged since baseline → No action
        Event::Ptr event = createEvent("uid1", "Same", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));
        SyncRecord record = SyncRecord::fromIncidence(event, "cal", "local");

        QList<SyncRecord> source = {record};
        QList<SyncRecord> target = {record};
        QMap<QString, QString> baselines;
        baselines["uid1"] = record.icalData;

        SyncDiff diff = computeSyncDiff(source, target, baselines, SyncMode::TwoWay);

        QVERIFY2(diff.toTarget.isEmpty(), "No changes to target expected");
        QVERIFY2(diff.toSource.isEmpty(), "No changes to source expected");
        QVERIFY2(!diff.hasConflicts(), "No conflict expected");
        QCOMPARE(diff.unchangedUids.size(), 1);
    }

    void testMergeMatrix_SourceModified()
    {
        // Case 2: Source modified, target unchanged → Push source to target
        Event::Ptr baseline = createEvent("uid1", "Original", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));
        Event::Ptr modified = createEvent("uid1", "Source Modified", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));

        SyncRecord baselineRec = SyncRecord::fromIncidence(baseline, "cal", "local");
        SyncRecord sourceRec = SyncRecord::fromIncidence(modified, "cal", "local");
        SyncRecord targetRec = SyncRecord::fromIncidence(baseline, "cal", "remote");  // Unchanged

        QList<SyncRecord> source = {sourceRec};
        QList<SyncRecord> target = {targetRec};
        QMap<QString, QString> baselines;
        baselines["uid1"] = baselineRec.icalData;

        SyncDiff diff = computeSyncDiff(source, target, baselines, SyncMode::TwoWay);

        QCOMPARE(diff.toTarget.size(), 1);
        QCOMPARE(diff.toTarget[0].type, SyncChangeType::Modified);
        QCOMPARE(diff.toTarget[0].uid, QStringLiteral("uid1"));
        QVERIFY2(diff.toSource.isEmpty(), "No changes to source - it was the one modified");
        QVERIFY2(!diff.hasConflicts(), "No conflict - only source changed");
    }

    void testMergeMatrix_TargetModified()
    {
        // Case 3: Source unchanged, target modified → Push target to source
        Event::Ptr baseline = createEvent("uid1", "Original", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));
        Event::Ptr modified = createEvent("uid1", "Target Modified", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));

        SyncRecord baselineRec = SyncRecord::fromIncidence(baseline, "cal", "local");
        SyncRecord sourceRec = SyncRecord::fromIncidence(baseline, "cal", "local");  // Unchanged
        SyncRecord targetRec = SyncRecord::fromIncidence(modified, "cal", "remote");

        QList<SyncRecord> source = {sourceRec};
        QList<SyncRecord> target = {targetRec};
        QMap<QString, QString> baselines;
        baselines["uid1"] = baselineRec.icalData;

        SyncDiff diff = computeSyncDiff(source, target, baselines, SyncMode::TwoWay);

        QVERIFY2(diff.toTarget.isEmpty(), "No changes to target - it was the one modified");
        QCOMPARE(diff.toSource.size(), 1);
        QCOMPARE(diff.toSource[0].type, SyncChangeType::Modified);
        QCOMPARE(diff.toSource[0].uid, QStringLiteral("uid1"));
        QVERIFY2(!diff.hasConflicts(), "No conflict - only target changed");
    }

    void testMergeMatrix_BothModifiedSameContent()
    {
        // Case 4: Both modified to SAME content (hashes match) → No action
        Event::Ptr baseline = createEvent("uid1", "Original", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));
        Event::Ptr modified = createEvent("uid1", "Both Modified Same", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));

        SyncRecord baselineRec = SyncRecord::fromIncidence(baseline, "cal", "local");
        SyncRecord modifiedRec = SyncRecord::fromIncidence(modified, "cal", "local");

        QList<SyncRecord> source = {modifiedRec};
        QList<SyncRecord> target = {modifiedRec};  // Same content
        QMap<QString, QString> baselines;
        baselines["uid1"] = baselineRec.icalData;

        SyncDiff diff = computeSyncDiff(source, target, baselines, SyncMode::TwoWay);

        QVERIFY2(diff.toTarget.isEmpty(), "No changes needed - same content");
        QVERIFY2(diff.toSource.isEmpty(), "No changes needed - same content");
        QVERIFY2(!diff.hasConflicts(), "No conflict - identical changes");
        QCOMPARE(diff.unchangedUids.size(), 1);
    }

    void testMergeMatrix_BothModifiedDifferentContent()
    {
        // Case 5: Both modified with DIFFERENT content → CONFLICT
        Event::Ptr baseline = createEvent("uid1", "Original", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));
        Event::Ptr sourceModified = createEvent("uid1", "Source Version", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));
        Event::Ptr targetModified = createEvent("uid1", "Target Version", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));

        SyncRecord baselineRec = SyncRecord::fromIncidence(baseline, "cal", "local");
        SyncRecord sourceRec = SyncRecord::fromIncidence(sourceModified, "cal", "local");
        SyncRecord targetRec = SyncRecord::fromIncidence(targetModified, "cal", "remote");

        QList<SyncRecord> source = {sourceRec};
        QList<SyncRecord> target = {targetRec};
        QMap<QString, QString> baselines;
        baselines["uid1"] = baselineRec.icalData;

        SyncDiff diff = computeSyncDiff(source, target, baselines, SyncMode::TwoWay);

        QVERIFY2(diff.hasConflicts(), "MUST detect conflict when both modified differently");
        QCOMPARE(diff.conflicts.size(), 1);
        QCOMPARE(diff.conflicts[0].sourceId, QStringLiteral("uid1"));
        QCOMPARE(diff.conflicts[0].type, ConflictType::BothModified);
    }

    void testMergeMatrix_SourceNew()
    {
        // Case 6: New on source, missing on target (no baseline) → Create on target
        Event::Ptr newEvent = createEvent("uid1", "Brand New Event");
        SyncRecord sourceRec = SyncRecord::fromIncidence(newEvent, "cal", "local");

        QList<SyncRecord> source = {sourceRec};
        QList<SyncRecord> target;  // Empty
        QMap<QString, QString> baselines;  // No baseline

        SyncDiff diff = computeSyncDiff(source, target, baselines, SyncMode::TwoWay);

        QCOMPARE(diff.toTarget.size(), 1);
        QCOMPARE(diff.toTarget[0].type, SyncChangeType::Created);
        QCOMPARE(diff.toTarget[0].uid, QStringLiteral("uid1"));
        QVERIFY2(diff.toSource.isEmpty(), "Nothing to create on source");
        QVERIFY2(!diff.hasConflicts(), "New item is not a conflict");
    }

    void testMergeMatrix_TargetNew()
    {
        // Case 7: Missing on source, new on target (no baseline) → Create on source
        Event::Ptr newEvent = createEvent("uid1", "Brand New Event");
        SyncRecord targetRec = SyncRecord::fromIncidence(newEvent, "cal", "remote");

        QList<SyncRecord> source;  // Empty
        QList<SyncRecord> target = {targetRec};
        QMap<QString, QString> baselines;  // No baseline

        SyncDiff diff = computeSyncDiff(source, target, baselines, SyncMode::TwoWay);

        QVERIFY2(diff.toTarget.isEmpty(), "Nothing to create on target");
        QCOMPARE(diff.toSource.size(), 1);
        QCOMPARE(diff.toSource[0].type, SyncChangeType::Created);
        QCOMPARE(diff.toSource[0].uid, QStringLiteral("uid1"));
        QVERIFY2(!diff.hasConflicts(), "New item is not a conflict");
    }

    void testMergeMatrix_SourceDeleted()
    {
        // Case 8: Deleted on source, unchanged on target → Delete from target
        Event::Ptr event = createEvent("uid1", "Will Be Deleted", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));
        SyncRecord record = SyncRecord::fromIncidence(event, "cal", "local");

        QList<SyncRecord> source;  // Deleted from source
        QList<SyncRecord> target = {record};  // Still on target (unchanged)
        QMap<QString, QString> baselines;
        baselines["uid1"] = record.icalData;  // Baseline exists

        SyncDiff diff = computeSyncDiff(source, target, baselines, SyncMode::TwoWay);

        QCOMPARE(diff.toTarget.size(), 1);
        QCOMPARE(diff.toTarget[0].type, SyncChangeType::Deleted);
        QCOMPARE(diff.toTarget[0].uid, QStringLiteral("uid1"));
        QVERIFY2(diff.toSource.isEmpty(), "Nothing to delete on source - already deleted");
        QVERIFY2(!diff.hasConflicts(), "Delete vs unchanged is not a conflict");
    }

    void testMergeMatrix_TargetDeleted()
    {
        // Case 9: Unchanged on source, deleted on target → Delete from source
        Event::Ptr event = createEvent("uid1", "Will Be Deleted", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));
        SyncRecord record = SyncRecord::fromIncidence(event, "cal", "local");

        QList<SyncRecord> source = {record};  // Still on source (unchanged)
        QList<SyncRecord> target;  // Deleted from target
        QMap<QString, QString> baselines;
        baselines["uid1"] = record.icalData;  // Baseline exists

        SyncDiff diff = computeSyncDiff(source, target, baselines, SyncMode::TwoWay);

        QVERIFY2(diff.toTarget.isEmpty(), "Nothing to delete on target - already deleted");
        QCOMPARE(diff.toSource.size(), 1);
        QCOMPARE(diff.toSource[0].type, SyncChangeType::Deleted);
        QCOMPARE(diff.toSource[0].uid, QStringLiteral("uid1"));
        QVERIFY2(!diff.hasConflicts(), "Unchanged vs delete is not a conflict");
    }

    void testMergeMatrix_DeleteVsModify()
    {
        // Case 10a: Deleted on source, modified on target → CONFLICT
        Event::Ptr baseline = createEvent("uid1", "Original", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));
        Event::Ptr modified = createEvent("uid1", "Target Modified", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));

        SyncRecord baselineRec = SyncRecord::fromIncidence(baseline, "cal", "local");
        SyncRecord targetRec = SyncRecord::fromIncidence(modified, "cal", "remote");

        QList<SyncRecord> source;  // Deleted
        QList<SyncRecord> target = {targetRec};  // Modified
        QMap<QString, QString> baselines;
        baselines["uid1"] = baselineRec.icalData;

        SyncDiff diff = computeSyncDiff(source, target, baselines, SyncMode::TwoWay);

        QVERIFY2(diff.hasConflicts(), "MUST detect conflict: delete vs modify");
        QCOMPARE(diff.conflicts.size(), 1);
        QCOMPARE(diff.conflicts[0].sourceId, QStringLiteral("uid1"));
        // Type should indicate delete vs modify conflict
    }

    void testMergeMatrix_ModifyVsDelete()
    {
        // Case 10b: Modified on source, deleted on target → CONFLICT
        Event::Ptr baseline = createEvent("uid1", "Original", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));
        Event::Ptr modified = createEvent("uid1", "Source Modified", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));

        SyncRecord baselineRec = SyncRecord::fromIncidence(baseline, "cal", "local");
        SyncRecord sourceRec = SyncRecord::fromIncidence(modified, "cal", "local");

        QList<SyncRecord> source = {sourceRec};  // Modified
        QList<SyncRecord> target;  // Deleted
        QMap<QString, QString> baselines;
        baselines["uid1"] = baselineRec.icalData;

        SyncDiff diff = computeSyncDiff(source, target, baselines, SyncMode::TwoWay);

        QVERIFY2(diff.hasConflicts(), "MUST detect conflict: modify vs delete");
        QCOMPARE(diff.conflicts.size(), 1);
        QCOMPARE(diff.conflicts[0].sourceId, QStringLiteral("uid1"));
        // Type should indicate modify vs delete conflict
    }

    // ========================================================================
    // Sync Diff - Conflict Detection (Original Tests)
    // ========================================================================

    void testConflictDetection()
    {
        // Both sides modify the same item differently
        Event::Ptr baseline = createEvent("uid1", "Original", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));
        Event::Ptr sourceModified = createEvent("uid1", "Source Modified", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));
        Event::Ptr targetModified = createEvent("uid1", "Target Modified", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));

        SyncRecord baselineRecord = SyncRecord::fromIncidence(baseline, "cal", "local");
        SyncRecord sourceRecord = SyncRecord::fromIncidence(sourceModified, "cal", "local");
        SyncRecord targetRecord = SyncRecord::fromIncidence(targetModified, "cal", "remote");

        QList<SyncRecord> source = {sourceRecord};
        QList<SyncRecord> target = {targetRecord};
        QMap<QString, QString> baselines;
        baselines["uid1"] = baselineRecord.icalData;

        SyncDiff diff = computeSyncDiff(source, target, baselines, SyncMode::TwoWay);

        // Should detect conflict
        QVERIFY(diff.hasConflicts());
        QCOMPARE(diff.conflicts.size(), 1);
        QCOMPARE(diff.conflicts[0].sourceId, QStringLiteral("uid1"));
    }

    void testNoConflictWhenSameChange()
    {
        // Both sides make the SAME change (identical result)
        Event::Ptr baseline = createEvent("uid1", "Original", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));
        Event::Ptr modified = createEvent("uid1", "Modified Same", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));

        SyncRecord baselineRecord = SyncRecord::fromIncidence(baseline, "cal", "local");
        SyncRecord modifiedRecord = SyncRecord::fromIncidence(modified, "cal", "local");

        QList<SyncRecord> source = {modifiedRecord};
        QList<SyncRecord> target = {modifiedRecord};  // Same modification
        QMap<QString, QString> baselines;
        baselines["uid1"] = baselineRecord.icalData;

        SyncDiff diff = computeSyncDiff(source, target, baselines, SyncMode::TwoWay);

        // No conflict - both made same change
        QVERIFY(!diff.hasConflicts());
        QVERIFY(!diff.hasChanges());
        QCOMPARE(diff.unchangedUids.size(), 1);
    }

    // ========================================================================
    // Sync Diff - Disabled Mode
    // ========================================================================

    void testDisabledMode()
    {
        Event::Ptr event = createEvent("uid1", "Test");
        SyncRecord record = SyncRecord::fromIncidence(event, "cal", "local");

        QList<SyncRecord> source = {record};
        QList<SyncRecord> target;
        QMap<QString, QString> baselines;

        SyncDiff diff = computeSyncDiff(source, target, baselines, SyncMode::Disabled);

        QVERIFY(diff.toTarget.isEmpty());
        QVERIFY(diff.toSource.isEmpty());
        QVERIFY(!diff.hasChanges());
    }

    // ========================================================================
    // Stats Computation
    // ========================================================================

    // ========================================================================
    // Recurrence ID Tests
    // ========================================================================

    void testRecurrenceIdInSyncRecord()
    {
        auto master = KCalendarCore::Event::Ptr(new KCalendarCore::Event);
        master->setUid("uid1");
        master->setSummary("Weekly");

        auto exception = KCalendarCore::Event::Ptr(new KCalendarCore::Event);
        exception->setUid("uid1");
        exception->setSummary("Weekly (moved)");
        exception->setRecurrenceId(QDateTime(QDate(2026, 3, 10), QTime(10, 0), QTimeZone::utc()));

        auto masterRec = SyncRecord::fromIncidence(master, "cal1", "local");
        auto exceptionRec = SyncRecord::fromIncidence(exception, "cal1", "local");

        QVERIFY(!masterRec.recurrenceId.isValid());
        QVERIFY(exceptionRec.recurrenceId.isValid());

        auto masterHash = SyncRecord::computeSemanticHash(master);
        auto exceptionHash = SyncRecord::computeSemanticHash(exception);
        QVERIFY(masterHash != exceptionHash);
    }

    void testQuickDiffWithRecurrenceExceptions()
    {
        auto master = KCalendarCore::Event::Ptr(new KCalendarCore::Event);
        master->setUid("uid1");
        master->setSummary("Weekly");
        master->setDtStart(QDateTime(QDate(2026, 3, 3), QTime(10, 0), QTimeZone::utc()));

        auto exception = KCalendarCore::Event::Ptr(new KCalendarCore::Event);
        exception->setUid("uid1");
        exception->setSummary("Weekly (moved)");
        exception->setDtStart(QDateTime(QDate(2026, 3, 10), QTime(14, 0), QTimeZone::utc()));
        exception->setRecurrenceId(QDateTime(QDate(2026, 3, 10), QTime(10, 0), QTimeZone::utc()));

        QList<SyncRecord> source;
        QList<SyncRecord> target;
        target << SyncRecord::fromIncidence(master, "cal1", "akonadi");
        target << SyncRecord::fromIncidence(exception, "cal1", "akonadi");

        auto diff = computeQuickDiff(source, target, SyncMode::TwoWay);
        QCOMPARE(diff.toSource.size(), 2);
    }

    // ========================================================================
    // Stats Computation
    // ========================================================================

    void testStatsComputation()
    {
        // Create a diff with various changes
        // Scenario: One-way upload from source to target
        //   uid1: new on source (not on target, no baseline) -> create
        //   uid2: modified on source (baseline exists, target has old version) -> update
        //   uid3: deleted on source (baseline exists, target has old version) -> delete

        Event::Ptr e1 = createEvent("uid1", "New");
        Event::Ptr e2Old = createEvent("uid2", "Original");
        Event::Ptr e2Modified = createEvent("uid2", "Modified Version");
        Event::Ptr e3 = createEvent("uid3", "ToDelete");

        SyncRecord rec1 = SyncRecord::fromIncidence(e1, "cal", "local");
        SyncRecord rec2Old = SyncRecord::fromIncidence(e2Old, "cal", "local");
        SyncRecord rec2Modified = SyncRecord::fromIncidence(e2Modified, "cal", "local");
        SyncRecord rec3 = SyncRecord::fromIncidence(e3, "cal", "local");

        // Source: has uid1 (new), uid2 (modified), but NOT uid3 (deleted from source)
        QList<SyncRecord> source = {rec1, rec2Modified};

        // Target: has uid2 (old), uid3 (exists, will be deleted)
        QList<SyncRecord> target = {rec2Old, rec3};

        // Baseline: uid2 and uid3 existed before
        QMap<QString, QString> baselines;
        baselines["uid2"] = rec2Old.icalData;  // Baseline = old version
        baselines["uid3"] = rec3.icalData;     // Baseline for uid3

        SyncDiff diff = computeSyncDiff(source, target, baselines, SyncMode::OneWayUpload);

        SyncStats stats = diff.targetStats();
        QCOMPARE(stats.created, 1);   // uid1
        QCOMPARE(stats.updated, 1);   // uid2
        QCOMPARE(stats.deleted, 1);   // uid3
    }
};

QTEST_MAIN(TestSyncDiff)
#include "tst_syncdiff.moc"

