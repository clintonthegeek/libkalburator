#ifndef DECSYNCACTIVECONTROLLER_H
#define DECSYNCACTIVECONTROLLER_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QStringList>

#include "decsynclib.h"
#include "decsynccontrollerstore.h"

namespace Kalburator::Sync {

/**
 * @brief Result of a per-UID merge operation.
 *
 * Returned by the private mergeEntries() helper so that both preprocessFetch()
 * and runActiveSync() can interpret the outcome.
 */
struct MergeResult {
    enum class Type {
        Unchanged,   ///< Only one app has data — pass through as-is
        Merged,      ///< Properties merged without conflict
        Conflict,    ///< Same property changed to different values by multiple apps
        Deletion     ///< All apps have null value for this UID
    };

    Type type = Type::Unchanged;

    QString mergedIcalData;     ///< Final merged iCal (valid for Merged)
    QString winnerIcalData;     ///< LWW winner (valid for Conflict)
    QString loserIcalData;      ///< LWW loser  (valid for Conflict)
    QString baselineIcalData;   ///< Baseline at time of merge (valid for Merged/Conflict)
};

/**
 * @brief Active DecSync controller: property-level merge across multiple apps.
 *
 * DecSync's native model is last-write-wins at the whole-blob level.  This
 * controller reads per-app entries, performs three-way property-level diffing
 * against a stored baseline, and either silently merges non-conflicting changes
 * or surfaces true conflicts — depending on the calling context.
 *
 * Two entry points:
 *
 *  preprocessFetch()  — Cases A/B (cross-backend sync).  Resolves every
 *                       conflict via LWW and returns a flat uid→entry map in
 *                       the same format as DecSyncCollection::readAllResources().
 *
 *  runActiveSync()    — Case C (DecSync-only / standalone).  Emits signals for
 *                       updates, deletions, and true conflicts (surfaced to the
 *                       user via ConflictManager).
 */
class DecSyncActiveController : public QObject
{
    Q_OBJECT

public:
    explicit DecSyncActiveController(DecSyncCollection *collection,
                                     DecSyncControllerStore *store,
                                     const QString &ownAppId,
                                     const QString &collectionId,
                                     QObject *parent = nullptr);

    /**
     * @brief Merge all per-app entries and return a flat uid→entry map.
     *
     * Resolves ALL conflicts via Last-Write-Wins.  Non-conflicting changes
     * (different properties modified by different apps) are silently merged.
     * The authoritative merged entry is written back to the collection so
     * other apps can read it, and the baseline is updated.
     *
     * UIDs present in the baseline but absent from all app entries are
     * returned with a null value (deletion marker).
     *
     * The controller's own app-id entries are included in the result for
     * UIDs not already covered by other-app merges.
     *
     * @return uid → DecSyncEntry, same shape as readAllResources() but with
     *         null entries for deletions included.
     */
    QMap<QString, DecSyncEntry> preprocessFetch();

    /**
     * @brief Run a full merge cycle, emitting signals for each outcome.
     *
     * Unlike preprocessFetch(), true conflicts are emitted via
     * conflictDetected() rather than resolved automatically — the
     * ConflictManager is responsible for resolution.
     *
     * @param currentItems  Optional map of uid → iCal that the local store
     *                      currently holds.  Pass an empty map to skip the
     *                      "nothing changed" early-return optimisation.
     */
    void runActiveSync(const QMap<QString, QString> &currentItems = {});

    /**
     * @brief Check if this is the first load (no baselines exist yet).
     *
     * When true, fetchItems() should use readAllResources() instead of
     * preprocessFetch() since there's nothing to merge against.
     */
    bool isInitialLoad() const;

signals:
    /** Emitted when a UID has been successfully merged (or was already up to date). */
    void itemUpdated(const QString &calendarId, const QString &uid,
                     const QString &mergedIcalData);

    /** Emitted when all apps have deleted a UID. */
    void itemDeleted(const QString &calendarId, const QString &uid);

    /**
     * @brief Emitted when the same property was changed to different values.
     *
     * Only emitted by runActiveSync(); preprocessFetch() uses LWW instead.
     */
    void conflictDetected(const QString &uid,
                          const QString &sourceIcalData,
                          const QString &targetIcalData,
                          const QString &baselineIcalData);

    /** Progress indicator for long-running sync runs. */
    void progressChanged(int current, int total);

private:
    /**
     * @brief Core merge logic for a single UID.
     *
     * @param uid        The calendar UID being merged.
     * @param appEntries Map of appId → DecSyncEntry for this uid (may include
     *                   entries with null values = deletion markers).
     * @return MergeResult describing what happened.
     */
    MergeResult mergeEntries(const QString &uid,
                             const QMap<QString, DecSyncEntry> &appEntries);

    /**
     * @brief Rebuild a merged iCal string from a template + property overrides.
     *
     * Uses the newest entry's iCal as the template, then applies each winning
     * property value via IncidenceDiff::applyPropertyToIncidence().  Falls back
     * to the raw template string on parse failure.
     *
     * @param templateIcal   iCal string to use as the merge base.
     * @param winningProps   Map of propertyName → winning value to apply.
     * @return Rebuilt iCal string.
     */
    QString rebuildIcal(const QString &templateIcal,
                        const QMap<QString, QString> &winningProps);

    /**
     * @brief Record the latest-seen timestamp from each app to the store.
     *
     * @param perAppData  The full per-app map from readPerAppResources().
     */
    void updateAppActivity(const QMap<QString, QMap<QString, DecSyncEntry>> &perAppData);

    // Properties ignored during diff (housekeeping / auto-generated fields)
    static const QStringList s_ignoredProperties;

    DecSyncCollection        *m_collection  = nullptr;
    DecSyncControllerStore   *m_store       = nullptr;
    QString                   m_ownAppId;
    QString                   m_collectionId;
};

} // namespace Kalburator::Sync

#endif // DECSYNCACTIVECONTROLLER_H
