#ifndef DECSYNCGARBAGECOLLECTOR_H
#define DECSYNCGARBAGECOLLECTOR_H

#include <QObject>
#include <QString>

#include "decsynclib.h"
#include "decsynccontrollerstore.h"

namespace Kalburator::Sync {

/**
 * @brief Garbage collector for DecSync entry files.
 *
 * Handles four maintenance operations, ordered from safest to most aggressive:
 *
 *  1. selfCompact()             — Deduplicate own entry files (always safe).
 *  2. onboardNewApps()          — Re-send authoritative state to newly-seen apps.
 *  3. compactInactiveApps()     — Compact files of apps inactive > N days.
 *  4. purgeDeadApps()           — Remove directories of apps inactive > M days.
 *
 * The GC is decoupled from sync cycles and is intended to run independently
 * on a timer or on demand. It relies on DecSyncControllerStore for app
 * activity data and baselines.
 *
 * @note After modifying another app's hash file (compaction), the
 *       corresponding sequence number is incremented so other readers
 *       re-read the compacted file. Since compaction only removes/deduplicates
 *       entries (never adds new ones), the forced re-read is harmless.
 */
class DecSyncGarbageCollector : public QObject
{
    Q_OBJECT

public:
    explicit DecSyncGarbageCollector(DecSyncCollection *collection,
                                     DecSyncControllerStore *store,
                                     const QString &ownAppId,
                                     const QString &collectionId,
                                     QObject *parent = nullptr);

    /**
     * @brief Compact own entry files (Tier 1, always safe).
     *
     * For each hash file in the own appId directory (skipping "sequences"):
     * reads all entries, deduplicates by path key (keeping newest per path),
     * and writes back if the file shrank.
     */
    void selfCompact();

    /**
     * @brief Compact files of apps inactive longer than @p inactiveDays (Tier 2).
     *
     * For each inactive app (excluding own appId):
     * - Reads hash files and deduplicates entries.
     * - Also drops entries where the controller baseline has a newer written_at.
     * - Writes back compacted file if it shrank.
     * - Increments the sequence number in the sequences file.
     * - Records compaction timestamp in the store.
     *
     * @param inactiveDays  Threshold in days (default: 7).
     */
    void compactInactiveApps(int inactiveDays = 7);

    /**
     * @brief Remove directories of apps inactive longer than @p deadDays (Tier 1).
     *
     * For each qualifying app (excluding own appId):
     * - Removes the entire v2/{appId}/ directory recursively.
     * - Removes the app's activity record from the store.
     *
     * @param deadDays  Threshold in days (default: 30).
     */
    void purgeDeadApps(int deadDays = 30);

    /**
     * @brief Detect new appIds and re-send authoritative state (Tier 1).
     *
     * Lists current apps via collection->listAppIds(), finds new ones via
     * store->newApps(), and if any are found:
     * - Writes all baselines + active deletions as entries via collection->setEntries().
     * - Records new apps in the store.
     */
    void onboardNewApps();

    /**
     * @brief Run all four operations in sequence.
     *
     * Order: selfCompact → onboardNewApps → compactInactiveApps → purgeDeadApps.
     * Emits gcCompleted(0, 0, 0) when done.
     *
     * @param inactiveDays  Threshold for compactInactiveApps (default: 7).
     * @param deadDays      Threshold for purgeDeadApps (default: 30).
     */
    void runAll(int inactiveDays = 7, int deadDays = 30);

signals:
    /**
     * @brief Emitted when runAll() completes.
     *
     * @param compacted  Number of hash files compacted (currently always 0).
     * @param purged     Number of app directories purged (currently always 0).
     * @param onboarded  Number of new apps onboarded (currently always 0).
     */
    void gcCompleted(int compacted, int purged, int onboarded);

private:
    /**
     * @brief Compute the dedup key for an entry.
     *
     * Resource entries: path joined with "/" (e.g., "resources/event-001").
     * Info entries:     path joined with "/" + "|" + key.toString().
     */
    static QString dedupKey(const DecSyncEntry &entry);

    /**
     * @brief Compact a single hash file in the given app directory.
     *
     * Reads all entries, deduplicates by dedupKey (keeping newest), and
     * optionally drops entries superseded by a controller baseline.
     *
     * @param appDir         Full path to the v2/{appId}/ directory.
     * @param hashName       Name of the hash file to compact.
     * @param checkBaseline  If true, also drop entries older than the baseline.
     * @return true if the file was modified (shrank), false otherwise.
     */
    bool compactHashFile(const QString &appDir,
                         const QString &hashName,
                         bool checkBaseline);

    DecSyncCollection      *m_collection  = nullptr;
    DecSyncControllerStore *m_store       = nullptr;
    QString                 m_ownAppId;
    QString                 m_collectionId;
};

} // namespace Kalburator::Sync

#endif // DECSYNCGARBAGECOLLECTOR_H
