#ifndef SYNCTYPES_H
#define SYNCTYPES_H

#include <QString>
#include <QDateTime>
#include <QStringList>
#include <QVariantMap>
#include <QJsonObject>
#include <QJsonArray>

namespace Kalburator::Sync {

/**
 * @file synctypes.h
 * @brief Common sync types designed for future extraction to qsynccore shared library.
 *
 * These types are intentionally designed to match the patterns from QPilotSync
 * and the SyncLibraryFeasability.md specification, enabling future code sharing.
 */

/**
 * @brief Conflict resolution policies for sync operations.
 *
 * Uses backend-neutral "source/target" terminology to support any combination
 * of backends (local-to-local, remote-to-remote, or mixed) and N-way sync.
 */
enum class ConflictResolution {
    SourceWins,     ///< Source version overwrites target
    TargetWins,     ///< Target version overwrites source
    Duplicate,      ///< Keep both versions with new IDs
    Skip,           ///< Leave conflict unresolved
    AskUser,        ///< Prompt user for decision
    LastWriteWins,  ///< Use most recently modified version
    CustomMerge     ///< Use property-level merged result
};

/**
 * @brief Sync topology preset for multi-backend collections.
 *
 * Determines how sync mappings are auto-generated from logical calendar bindings:
 * - Star: Primary is hub, each sync binding maps to/from primary only.
 * - Mirror: Every backend syncs with every other backend (all-to-all).
 * - Chain: Backends sync sequentially in order (A<->B, B<->C, etc.).
 */
enum class SyncTopology {
    Star,    ///< Hub-and-spoke: all sync through primary (default, recommended)
    Mirror,  ///< Full mesh: every backend syncs with every other
    Chain    ///< Sequential: each backend syncs with the next in order
};

/**
 * @brief Type of conflict detected during sync.
 */
enum class ConflictType {
    BothModified,   ///< Both sides modified the same item
    ModifyDelete,   ///< One side modified, other deleted
    BothCreated     ///< Both sides created item with same UID (rare)
};

/**
 * @brief Information about a detected conflict.
 *
 * ConflictInfo carries all data needed to display and resolve a conflict.
 * Uses backend-neutral "source/target" terminology - these map to the
 * sourceBackend/targetBackend of the SyncMapping that detected the conflict.
 *
 * The payload data fields (sourceIcalData/targetIcalData/baselineIcalData)
 * are optional for signaling but required for UI display.
 */
struct ConflictInfo {
    // Identity
    QString conflictId;         ///< Unique ID (set by SyncConflictStore when recorded)
    QString mappingId;          ///< Sync mapping that detected this conflict

    // Item identification (using neutral source/target terminology)
    QString sourceId;           ///< Source incidence UID
    QString targetId;           ///< Target incidence identifier
    QString calendarId;         ///< Calendar where conflict occurred

    // Backend identification (for UI display of actual backend names)
    QString sourceBackendId;    ///< Source backend ID (e.g., "org-backend")
    QString targetBackendId;    ///< Target backend ID (e.g., "local-backend")

    // Human-readable backend names (resolved from config, for UI labels)
    QString sourceBackendDisplayName;  ///< e.g., "CalDAV Server" or user's custom name
    QString targetBackendDisplayName;  ///< e.g., "Local Storage" or user's custom name

    // Conflict metadata
    ConflictType type = ConflictType::BothModified;
    QString sourceDescription;  ///< Summary for quick display (source version)
    QString targetDescription;  ///< Summary for quick display (target version)
    QDateTime sourceModified;   ///< Last modified time of source version
    QDateTime targetModified;   ///< Last modified time of target version
    QDateTime detectedAt;

    // Full record data for diff display (optional, may be empty).
    //
    // These carry the NATIVE ENCODING OF THE RESPECTIVE BACKEND'S SHAPE —
    // iCal for every real calendar backend today, but in general whatever
    // that backend's Shape::encoding says (see sourceEncoding/targetEncoding
    // below). They are NOT the engine's canonical Shape JSON: the engine
    // promotes both sides to canonical to diff them and demotes them back
    // here. The `*IcalData` names are kept for source compatibility with
    // PlanStan and WildPalms; see Bug A in
    // docs/2026-08-21-conflict-info-canonical-data-and-unmonitored-resolution-handoff.md.
    //
    // Either side can legitimately be empty: a ModifyDelete conflict has no
    // data on the deleted side, and baselineIcalData is empty whenever the
    // engine has no baseline bytes for the record (which, since baselines
    // became per-side hashes in Phase B4, is currently always — see
    // docs/campaign/FINDINGS.md O48).
    QString sourceIcalData;     ///< Source version, in sourceEncoding
    QString targetIcalData;     ///< Target version, in targetEncoding
    QString baselineIcalData;   ///< Baseline (for 3-way diff), in sourceEncoding

    // Shape encoding each payload above is written in, e.g. "ical". Empty
    // when the engine could not name it. TRANSPORT-ONLY: SyncConflictStore
    // does NOT persist these (adding columns would need a schema migration
    // and no consumer asked for persistence), so a ConflictInfo read back
    // out of the store has them empty even when the payloads are present.
    QString sourceEncoding;     ///< Encoding of sourceIcalData (transport-only)
    QString targetEncoding;     ///< Encoding of targetIcalData (transport-only)

    bool hasFullData() const {
        return !sourceIcalData.isEmpty() || !targetIcalData.isEmpty();
    }
};

/**
 * @brief A conflict resolution the user has already chosen, waiting to be
 *        applied by a later sync run.
 *
 * Bug B (docs/2026-08-21-conflict-info-canonical-data-and-unmonitored-
 * resolution-handoff.md §B): in SyncBehavior::Unmonitored — the only mode a
 * real PlanStan run ever uses — the conflict dialog is presented AFTER the
 * run that detected the conflict has finished. The engine's only code that
 * turns a ConflictResolution into an actual write
 * (SyncEngineWorker::applyConflictResolution) runs inside a live diff walk,
 * against per-run state that no longer exists by then. So the choice used to
 * write one column in SyncConflictStore and nothing else, and the identical
 * conflict re-detected forever.
 *
 * The repair (locked decision 1) is injection: SyncEngine remembers the
 * choice here, hands it to the NEXT dispatchSync() for that mapping on
 * SyncEngineWorker::Request, and the AskUser branch of the diff walk replays
 * it through the existing write/transcode/baseline path. No second write
 * mechanism (campaign INVARIANTS §1).
 *
 * sourceModified/targetModified are the two records' lastModified AT
 * DETECTION TIME. The injection point compares them against the live records
 * and DISCARDS the resolution if either side moved (locked decision 3) — a
 * stale "Keep Local" must never clobber an edit made after the dialog was
 * answered.
 */
struct PendingConflictResolution {
    QString conflictId;         ///< SyncConflictStore row id (or a synthesized uuid for store-less hosts)
    QString recordId;           ///< The conflicting record's id (ConflictInfo::sourceId)
    ConflictResolution resolution = ConflictResolution::AskUser;
    /// CustomMerge only: the user's hand-merged payload in the SOURCE
    /// backend's native encoding. NOT persisted by SyncConflictStore, so a
    /// resolution rehydrated after a restart always has this empty and falls
    /// back to the automatic merger (see FINDINGS O52).
    QString mergedNative;
    QDateTime sourceModified;   ///< op.record.lastModified when the conflict was detected
    QDateTime targetModified;   ///< op.targetRecord.lastModified when the conflict was detected
};

/**
 * @brief Statistics for a sync operation.
 *
 * Tracks counts of created, updated, deleted items etc.
 * Designed to match QPilotSync's SyncStats for future sharing.
 */
struct SyncStats {
    int created = 0;
    int updated = 0;
    int deleted = 0;
    int unchanged = 0;
    int conflicts = 0;
    int errors = 0;

    QString summary() const {
        return QString("+%1 ~%2 -%3 =%4 !%5 E%6")
            .arg(created).arg(updated).arg(deleted)
            .arg(unchanged).arg(conflicts).arg(errors);
    }

    SyncStats& operator+=(const SyncStats &other) {
        created += other.created;
        updated += other.updated;
        deleted += other.deleted;
        unchanged += other.unchanged;
        conflicts += other.conflicts;
        errors += other.errors;
        return *this;
    }

    bool hasChanges() const {
        return created > 0 || updated > 0 || deleted > 0;
    }

    bool hasErrors() const {
        return errors > 0;
    }

    int total() const {
        return created + updated + deleted + unchanged;
    }
};

/**
 * @brief Result of a sync operation.
 */
struct SyncResult {
    bool success = true;
    QString errorMessage;
    QDateTime startTime;
    QDateTime endTime;
    SyncStats sourceStats;  ///< Changes made to source side
    SyncStats targetStats;  ///< Changes made to target side
    QStringList warnings;
    QList<ConflictInfo> unresolvedConflicts;

    /// True iff QFuture::cancel() was observed during this run.
    /// Distinct from success: success is "ran to completion without
    /// errors"; cancelled is "ran for a while then stopped on
    /// caller request". A cancelled SyncResult typically has
    /// errorMessage empty; partial sourceStats/targetStats reflect
    /// work done before cancellation took effect.
    bool cancelled = false;

    /// True iff this slot in a multi-mapping queue never started
    /// (e.g. cancellation arrived after mapping 2 of 5 finished;
    /// mappings 3-5 land here with skipped=true). Mutually
    /// exclusive with success=true.
    bool skipped = false;

    /// E9.2 successor (parallel-sync Task 1): the settled WriteOperation's
    /// resultRevision() from this mapping's most recent apply on each side.
    /// Empty when the backend computed none, or when no apply happened on
    /// that side.
    ///
    /// These previously lived on SyncEngineWorker as
    /// lastAppliedSourceRevision()/lastAppliedTargetRevision(), read by
    /// SyncEngine::onWorkerSyncCompleted off *the* worker — the engine's
    /// only single-in-flight-mapping assumption. Carrying them on the
    /// result makes them correct when N mappings are in flight.
    QString appliedSourceRevision;
    QString appliedTargetRevision;

    /// Bug B (conflict-resolution-repair Task 3): conflict ids whose stored
    /// PendingConflictResolution this run actually APPLIED — i.e. the
    /// resolution was folded into the merge AND the write that carried it
    /// succeeded. SyncEngine consumes exactly these: it drops them from its
    /// pending map and deletes their SyncConflictStore rows, so a resolution
    /// applies once and can never silently auto-apply to a future GENUINE
    /// conflict for the same record. The worker only populates this on the
    /// successful-write branch (same reasoning as "only save baselines on
    /// successful writes"): a failed apply leaves the resolution pending for
    /// the next run to retry.
    QStringList appliedConflictIds;

    /// Bug B, staleness guard (locked decision 3): conflict ids whose stored
    /// resolution was DISCARDED because a record moved between the dialog
    /// being answered and this run reading it. Reported regardless of write
    /// outcome — a stale resolution is wrong no matter what else happened —
    /// and the conflict is re-presented fresh in unresolvedConflicts.
    QStringList staleConflictIds;

    qint64 durationMs() const {
        return startTime.msecsTo(endTime);
    }

    bool hasWarnings() const {
        return !warnings.isEmpty();
    }

    bool hasUnresolvedConflicts() const {
        return !unresolvedConflicts.isEmpty();
    }
};

/**
 * @brief Sync direction/mode for a sync mapping.
 */
enum class SyncMode {
    Disabled,           ///< Sync is disabled for this mapping
    OneWayUpload,       ///< Source -> Target only
    OneWayDownload,     ///< Target -> Source only
    TwoWay              ///< Bidirectional sync
};

/**
 * @brief Defines a sync edge between two backend-calendar pairs.
 *
 * Part of the sync routing graph defined in .kalb configuration.
 */
enum class WhenLossWouldOccur {
    Abort,    ///< Refuse to sync if the composed pipeline is lossy
    Warn,     ///< Sync but emit SyncBackendBase/SyncEngine::transcodingWarning (default)
    Proceed,  ///< Sync silently even if data would be dropped
};

inline QString whenLossWouldOccurToString(WhenLossWouldOccur v) {
    switch (v) {
        case WhenLossWouldOccur::Abort:   return QStringLiteral("abort");
        case WhenLossWouldOccur::Warn:    return QStringLiteral("warn");
        case WhenLossWouldOccur::Proceed: return QStringLiteral("proceed");
    }
    return QStringLiteral("warn");
}

inline WhenLossWouldOccur whenLossWouldOccurFromString(const QString &s) {
    if (s == QLatin1String("abort"))   return WhenLossWouldOccur::Abort;
    if (s == QLatin1String("proceed")) return WhenLossWouldOccur::Proceed;
    return WhenLossWouldOccur::Warn;
}

struct SyncMapping {
    QString id;                 ///< Unique mapping ID
    QString sourceBackend;      ///< Backend ID (e.g. "local", "caldav")
    QString sourceCalendar;     ///< Calendar ID on source backend
    QString targetBackend;      ///< Backend ID
    QString targetCalendar;     ///< Calendar ID on target backend
    SyncMode mode = SyncMode::TwoWay;
    ConflictResolution conflictPolicy = ConflictResolution::AskUser;
    WhenLossWouldOccur lossPolicy = WhenLossWouldOccur::Warn;
    bool enabled = true;

    bool isValid() const {
        return !id.isEmpty() &&
               !sourceBackend.isEmpty() &&
               !sourceCalendar.isEmpty() &&
               !targetBackend.isEmpty() &&
               !targetCalendar.isEmpty();
    }
};

/**
 * @brief Context passed to sync operations.
 *
 * Contains references to mapping stores, baseline stores, and sync policies.
 * Designed for future integration with qsynccore's SyncContext.
 */
struct SyncContext {
    // Future: IDMappingStore *mappings = nullptr;
    // Future: BaselineStore *baseline = nullptr;
    ConflictResolution conflictPolicy = ConflictResolution::AskUser;
    bool isFirstSync = false;
    bool cancelled = false;

    // Extensible via QVariantMap for app-specific data
    QVariantMap extras;
};

// ============================================================================
// String conversion helpers for JSON serialization
// ============================================================================

/**
 * @brief Convert SyncMode to string for JSON serialization.
 */
inline QString syncModeToString(SyncMode mode) {
    switch (mode) {
        case SyncMode::Disabled:        return QStringLiteral("disabled");
        case SyncMode::OneWayUpload:    return QStringLiteral("one-way-upload");
        case SyncMode::OneWayDownload:  return QStringLiteral("one-way-download");
        case SyncMode::TwoWay:          return QStringLiteral("two-way");
    }
    return QStringLiteral("two-way");
}

/**
 * @brief Parse SyncMode from string.
 */
inline SyncMode syncModeFromString(const QString &str) {
    if (str == QLatin1String("disabled"))         return SyncMode::Disabled;
    if (str == QLatin1String("one-way-upload"))   return SyncMode::OneWayUpload;
    if (str == QLatin1String("one-way-download")) return SyncMode::OneWayDownload;
    if (str == QLatin1String("two-way"))          return SyncMode::TwoWay;
    return SyncMode::TwoWay;  // Default
}

/**
 * @brief Convert ConflictResolution to string for JSON serialization.
 */
inline QString conflictResolutionToString(ConflictResolution res) {
    switch (res) {
        case ConflictResolution::SourceWins:     return QStringLiteral("source-wins");
        case ConflictResolution::TargetWins:     return QStringLiteral("target-wins");
        case ConflictResolution::Duplicate:      return QStringLiteral("duplicate");
        case ConflictResolution::Skip:           return QStringLiteral("skip");
        case ConflictResolution::AskUser:        return QStringLiteral("ask-user");
        case ConflictResolution::LastWriteWins:  return QStringLiteral("last-write-wins");
    }
    return QStringLiteral("ask-user");
}

/**
 * @brief Parse ConflictResolution from string.
 *
 * Supports both new terminology (source-wins/target-wins) and legacy
 * terminology (local-wins/remote-wins) for backward compatibility.
 */
inline ConflictResolution conflictResolutionFromString(const QString &str) {
    // New terminology
    if (str == QLatin1String("source-wins"))      return ConflictResolution::SourceWins;
    if (str == QLatin1String("target-wins"))      return ConflictResolution::TargetWins;
    // Legacy terminology (backward compatibility)
    if (str == QLatin1String("local-wins"))       return ConflictResolution::SourceWins;
    if (str == QLatin1String("remote-wins"))      return ConflictResolution::TargetWins;
    // Other options
    if (str == QLatin1String("duplicate"))        return ConflictResolution::Duplicate;
    if (str == QLatin1String("skip"))             return ConflictResolution::Skip;
    if (str == QLatin1String("ask-user"))         return ConflictResolution::AskUser;
    if (str == QLatin1String("last-write-wins"))  return ConflictResolution::LastWriteWins;
    return ConflictResolution::AskUser;  // Default
}

// ============================================================================
// SyncMapping JSON serialization
// ============================================================================

/**
 * @brief Serialize SyncMapping to JSON object.
 */
inline QJsonObject syncMappingToJson(const SyncMapping &mapping) {
    QJsonObject obj;
    obj[QStringLiteral("id")] = mapping.id;
    obj[QStringLiteral("sourceBackend")] = mapping.sourceBackend;
    obj[QStringLiteral("sourceCalendar")] = mapping.sourceCalendar;
    obj[QStringLiteral("targetBackend")] = mapping.targetBackend;
    obj[QStringLiteral("targetCalendar")] = mapping.targetCalendar;
    obj[QStringLiteral("mode")] = syncModeToString(mapping.mode);
    obj[QStringLiteral("conflictResolution")] = conflictResolutionToString(mapping.conflictPolicy);
    obj[QStringLiteral("lossPolicy")] = whenLossWouldOccurToString(mapping.lossPolicy);
    obj[QStringLiteral("enabled")] = mapping.enabled;
    return obj;
}

/**
 * @brief Parse SyncMapping from JSON object.
 */
inline SyncMapping syncMappingFromJson(const QJsonObject &obj) {
    SyncMapping mapping;
    mapping.id = obj.value(QStringLiteral("id")).toString();
    mapping.sourceBackend = obj.value(QStringLiteral("sourceBackend")).toString();
    mapping.sourceCalendar = obj.value(QStringLiteral("sourceCalendar")).toString();
    mapping.targetBackend = obj.value(QStringLiteral("targetBackend")).toString();
    mapping.targetCalendar = obj.value(QStringLiteral("targetCalendar")).toString();
    mapping.mode = syncModeFromString(obj.value(QStringLiteral("mode")).toString());
    mapping.conflictPolicy = conflictResolutionFromString(
        obj.value(QStringLiteral("conflictResolution")).toString());
    mapping.lossPolicy = whenLossWouldOccurFromString(
        obj.value(QStringLiteral("lossPolicy")).toString(QStringLiteral("warn")));
    mapping.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
    return mapping;
}

/**
 * @brief Serialize list of SyncMappings to JSON array.
 */
inline QJsonArray syncMappingsToJson(const QList<SyncMapping> &mappings) {
    QJsonArray arr;
    for (const auto &mapping : mappings) {
        arr.append(syncMappingToJson(mapping));
    }
    return arr;
}

/**
 * @brief Parse list of SyncMappings from JSON array.
 */
inline QList<SyncMapping> syncMappingsFromJson(const QJsonArray &arr) {
    QList<SyncMapping> mappings;
    for (const auto &val : arr) {
        if (val.isObject()) {
            mappings.append(syncMappingFromJson(val.toObject()));
        }
    }
    return mappings;
}

/// Per-call execution override for runSync(). Lets callers request
/// one-way mirror semantics for a mapping that's otherwise configured
/// for bidirectional sync, or a clobber (wipe target + re-push).
struct ExecutionOverride {
    enum class Direction {
        Default,      ///< Use the mapping's stored direction (today: bidirectional).
        MirrorAToB,   ///< One-way: source overwrites target; target-only records deleted.
        MirrorBToA,   ///< One-way: target overwrites source; source-only records deleted.
    };
    Direction direction = Direction::Default;

    /// When true, the engine treats this mapping as a clobber:
    ///   1. Skip baseline load (treat as first sync).
    ///   2. Skip the mass-delete-guard hook (no deletes will be computed —
    ///      the wipe replaces the diff; clobber IS the user's authorization).
    ///   3. Call targetBackend->wipeCollection(targetCollectionId) after the
    ///      source fetch succeeds (a target is never wiped when the source
    ///      can't be read) and before the target fetch.
    ///   4. Push source records to the now-empty target.
    ///   5. Write a fresh baseline at end-of-sync as normal.
    ///
    /// `direction` is silently ignored when `clobber == true`; effective
    /// direction is always source → target.
    ///
    /// Unlike `direction`, this flag also applies on multi-mapping (subset
    /// and all-enabled) dispatch — see SyncRequest::executionOverride.
    bool clobber = false;
};

// Declare metatypes for Qt signal/slot usage

} // namespace Kalburator::Sync

Q_DECLARE_METATYPE(Kalburator::Sync::ConflictResolution)
Q_DECLARE_METATYPE(Kalburator::Sync::ConflictType)
Q_DECLARE_METATYPE(Kalburator::Sync::ConflictInfo)
Q_DECLARE_METATYPE(Kalburator::Sync::SyncStats)
Q_DECLARE_METATYPE(Kalburator::Sync::SyncResult)
Q_DECLARE_METATYPE(Kalburator::Sync::SyncMode)
Q_DECLARE_METATYPE(Kalburator::Sync::SyncMapping)
Q_DECLARE_METATYPE(Kalburator::Sync::ExecutionOverride)

#endif // SYNCTYPES_H
