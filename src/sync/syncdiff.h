#ifndef SYNCDIFF_H
#define SYNCDIFF_H

#include "synctypes.h"

#include <KCalendarCore/Incidence>
#include <QString>
#include <QList>
#include <QMap>
#include <QColor>
#include <QDateTime>

namespace Kalburator::Sync {

/**
 * @file syncdiff.h
 * @brief Types for computing sync differences between two backends.
 *
 * The sync algorithm uses 3-way comparison:
 * - Baseline: Last synced state (stored in SyncStore)
 * - Source: Current state on source backend
 * - Target: Current state on target backend
 *
 * By comparing each side to the baseline, we can determine:
 * - What changed on source since last sync
 * - What changed on target since last sync
 * - What conflicts exist (both sides changed the same item)
 */

/**
 * @brief Represents a single incidence record for sync comparison.
 */
struct SyncRecord {
    QString uid;                    ///< Incidence UID
    QString calendarId;             ///< Calendar ID
    QString backendId;              ///< Backend ID
    QString versionHash;            ///< Content hash for change detection
    QString icalData;               ///< Full iCal representation
    KCalendarCore::Incidence::Ptr incidence;  ///< Parsed incidence (may be null)
    QDateTime lastModified;         ///< Last modified timestamp
    QDateTime recurrenceId;          ///< RECURRENCE-ID for exception instances

    bool isValid() const { return !uid.isEmpty(); }

    /**
     * @brief Compute content hash from iCal data.
     *
     * This hash is used for change detection. Two incidences with
     * the same hash are considered identical.
     *
     * @deprecated Use computeSemanticHash() instead for cross-implementation compatibility.
     */
    static QString computeHash(const QString &icalData);

    /**
     * @brief Compute a semantic hash from an incidence.
     *
     * Unlike computeHash() which hashes raw iCal data, this function
     * computes a hash based only on *meaningful* content, ignoring:
     * - PRODID (differs between applications)
     * - DTSTAMP (regenerated on serialization)
     * - CREATED, LAST-MODIFIED (timestamps, not content)
     * - SEQUENCE (revision counter, handled separately)
     * - Property ordering differences
     * - Line folding differences
     * - X-properties that are implementation-specific
     *
     * This ensures that the "same" incidence stored by different
     * applications (local files, CalDAV servers, Org-mode) produces
     * the same hash despite serialization differences.
     */
    static QString computeSemanticHash(const KCalendarCore::Incidence::Ptr &inc);

    /**
     * @brief Create a SyncRecord from an incidence.
     */
    static SyncRecord fromIncidence(const KCalendarCore::Incidence::Ptr &inc,
                                     const QString &calendarId,
                                     const QString &backendId);
};

/**
 * @brief Classification of what happened to a record.
 */
enum class SyncChangeType {
    Unchanged,  ///< No change since baseline
    Created,    ///< New record (not in baseline)
    Modified,   ///< Modified since baseline
    Deleted     ///< Deleted (in baseline but not in current)
};

/**
 * @brief A single change detected during diff computation.
 */
struct SyncChange {
    SyncChangeType type = SyncChangeType::Unchanged;
    QString uid;
    SyncRecord sourceRecord;   ///< Current source state (may be empty for deletes)
    SyncRecord targetRecord;   ///< Current target state (may be empty for deletes)
    SyncRecord baselineRecord; ///< Baseline state (may be empty for creates)

    bool isConflict = false;   ///< True if both sides changed
    ConflictInfo conflictInfo; ///< Details if conflict
};

/**
 * @brief Result of computing a sync diff between two backends.
 */
struct SyncDiff {
    // Changes to apply to target backend
    QList<SyncChange> toTarget;

    // Changes to apply to source backend (for two-way sync)
    QList<SyncChange> toSource;

    // Detected conflicts
    QList<ConflictInfo> conflicts;

    // UIDs that are unchanged on both sides
    QStringList unchangedUids;

    /**
     * @brief Check if there are any changes to sync.
     */
    bool hasChanges() const {
        return !toTarget.isEmpty() || !toSource.isEmpty();
    }

    /**
     * @brief Check if there are any conflicts.
     */
    bool hasConflicts() const {
        return !conflicts.isEmpty();
    }

    /**
     * @brief Get summary statistics.
     */
    SyncStats targetStats() const;
    SyncStats sourceStats() const;
};

/**
 * @brief Represents calendar-level properties for sync comparison.
 *
 * Unlike SyncRecord which tracks incidences, this tracks calendar
 * metadata (color, description) that needs separate sync handling.
 */
struct CalendarPropertyRecord {
    QString backendId;
    QString calendarId;
    QColor color;              ///< Invalid QColor if not set
    QString description;       ///< Empty if not set
    QString versionHash;       ///< Hash of serialized properties for change detection

    bool isValid() const { return !calendarId.isEmpty(); }

    /**
     * @brief Compute hash from properties for change detection.
     *
     * Creates a hash from the calendar properties to detect when
     * they've changed, similar to SyncRecord::computeHash().
     */
    static QString computeHash(const CalendarPropertyRecord &record);

    /**
     * @brief Serialize properties to JSON string.
     */
    QString toJson() const;

    /**
     * @brief Deserialize properties from JSON string.
     */
    static CalendarPropertyRecord fromJson(const QString &json,
                                           const QString &backendId,
                                           const QString &calendarId);
};

/**
 * @brief Describes property changes to apply during sync.
 *
 * After 3-way merge of calendar properties, this structure
 * contains the resolved changes to apply to one or both backends.
 */
struct CalendarPropertyDiff {
    bool colorChanged = false;
    bool descriptionChanged = false;
    QColor newColor;           ///< Color to apply (if colorChanged)
    QString newDescription;    ///< Description to apply (if descriptionChanged)

    /**
     * @brief Check if there are any property changes.
     */
    bool hasChanges() const { return colorChanged || descriptionChanged; }
};

/**
 * @brief Computes the sync diff between source and target records.
 *
 * This is the core 3-way merge algorithm:
 *
 * For each UID found in source, target, or baseline:
 *   1. Compare source to baseline to detect source changes
 *   2. Compare target to baseline to detect target changes
 *   3. Based on sync mode, determine what action to take
 *   4. If both sides changed, it's a conflict
 *
 * @param sourceRecords Current records from source backend
 * @param targetRecords Current records from target backend
 * @param baselines Map of uid -> baseline iCal data
 * @param mode Sync mode (one-way or two-way)
 * @return Computed diff with changes and conflicts
 */
SyncDiff computeSyncDiff(const QList<SyncRecord> &sourceRecords,
                         const QList<SyncRecord> &targetRecords,
                         const QMap<QString, QString> &baselines,
                         SyncMode mode);

/**
 * @brief Computes a fast 2-way diff without baselines.
 *
 * This is a simplified algorithm for:
 * - First sync (no baselines exist)
 * - One-way sync mode (no conflicts possible)
 * - User requests "quick sync"
 *
 * Algorithm:
 * - Items only on source: create on target
 * - Items only on target: create on source (for two-way)
 * - Items on both with different hashes: use LastWriteWins, no conflict detection
 * - Items on both with same hash: unchanged
 *
 * @param sourceRecords Current records from source backend
 * @param targetRecords Current records from target backend
 * @param mode Sync mode (one-way or two-way)
 * @return Computed diff (conflicts list will always be empty)
 */
SyncDiff computeQuickDiff(const QList<SyncRecord> &sourceRecords,
                          const QList<SyncRecord> &targetRecords,
                          SyncMode mode);

/**
 * @brief Helper to convert incidence list to sync records.
 */
QList<SyncRecord> incidencesToSyncRecords(
    const QList<KCalendarCore::Incidence::Ptr> &incidences,
    const QString &calendarId,
    const QString &backendId);

} // namespace Kalburator::Sync

#endif // SYNCDIFF_H
