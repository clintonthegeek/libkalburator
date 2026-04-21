#ifndef SYNCTESTHOOKS_H
#define SYNCTESTHOOKS_H

#include <functional>
#include <QString>

namespace Kalburator::Sync {

// Only compile test hooks in debug/test builds
#ifdef PLANSTAN_TESTING

class ConflictInfo;

/**
 * @brief Global hooks for sync engine testing and debugging.
 *
 * SyncTestHooks provides injection points throughout the sync engine
 * for observability and failure simulation. Only available when
 * compiled with PLANSTAN_TESTING defined.
 *
 * Usage:
 * @code
 * // Set up observation
 * SyncTestHooks::instance().onTransactionStart = [](const QString &txId) {
 *     qDebug() << "Transaction started:" << txId;
 * };
 *
 * // Set up failure injection
 * int commitCount = 0;
 * SyncTestHooks::instance().shouldFailCommit = [&](const QString &, int) {
 *     return ++commitCount == 3;  // Fail 3rd commit
 * };
 *
 * // Run sync...
 *
 * // Clean up
 * SyncTestHooks::instance().reset();
 * @endcode
 */
class SyncTestHooks
{
public:
    static SyncTestHooks& instance()
    {
        static SyncTestHooks hooks;
        return hooks;
    }

    // =========================================================================
    // Observation Hooks (called during sync operations)
    // =========================================================================

    /**
     * @brief Called when a SyncTransaction starts.
     * @param txId Transaction ID
     */
    std::function<void(const QString &txId)> onTransactionStart;

    /**
     * @brief Called before committing a transaction item.
     * @param txId Transaction ID
     * @param itemIndex Index of the item being committed
     */
    std::function<void(const QString &txId, int itemIndex)> onBeforeItemCommit;

    /**
     * @brief Called after committing a transaction item.
     * @param txId Transaction ID
     * @param itemIndex Index of the item committed
     * @param success Whether commit succeeded
     */
    std::function<void(const QString &txId, int itemIndex, bool success)> onAfterItemCommit;

    /**
     * @brief Called when a SyncTransaction ends.
     * @param txId Transaction ID
     * @param success Whether transaction succeeded
     */
    std::function<void(const QString &txId, bool success)> onTransactionEnd;

    /**
     * @brief Called before simulation of a transaction item.
     * @param txId Transaction ID
     * @param itemIndex Index of the item being simulated
     */
    std::function<void(const QString &txId, int itemIndex)> onBeforeItemSimulate;

    /**
     * @brief Called after simulation of a transaction item.
     * @param txId Transaction ID
     * @param itemIndex Index of the item simulated
     * @param result Simulation result (0=pending, 1=success, 2=conflict, 3=error)
     */
    std::function<void(const QString &txId, int itemIndex, int result)> onAfterItemSimulate;

    /**
     * @brief Called when a conflict is detected.
     * @param conflict The conflict info
     */
    std::function<void(const ConflictInfo &conflict)> onConflictDetected;

    /**
     * @brief Called when a backend loads items.
     * @param backendId Backend ID
     * @param calendarId Calendar ID
     */
    std::function<void(const QString &backendId, const QString &calendarId)> onBackendLoad;

    /**
     * @brief Called when a backend pushes items.
     * @param backendId Backend ID
     * @param calendarId Calendar ID
     * @param itemCount Number of items pushed
     */
    std::function<void(const QString &backendId, const QString &calendarId, int itemCount)> onBackendPush;

    /**
     * @brief Called when a backend deletes items.
     * @param backendId Backend ID
     * @param calendarId Calendar ID
     * @param itemCount Number of items deleted
     */
    std::function<void(const QString &backendId, const QString &calendarId, int itemCount)> onBackendDelete;

    /**
     * @brief Called when SyncCoordinator starts syncing a mapping.
     * @param mappingId Mapping ID
     */
    std::function<void(const QString &mappingId)> onSyncMappingStart;

    /**
     * @brief Called when SyncCoordinator finishes syncing a mapping.
     * @param mappingId Mapping ID
     * @param success Whether sync succeeded
     */
    std::function<void(const QString &mappingId, bool success)> onSyncMappingEnd;

    // =========================================================================
    // Failure Injection Hooks (return true to trigger failure)
    // =========================================================================

    /**
     * @brief Return true to fail a transaction item commit.
     * @param txId Transaction ID
     * @param itemIndex Index of the item
     * @return true to simulate failure
     */
    std::function<bool(const QString &txId, int itemIndex)> shouldFailCommit;

    /**
     * @brief Return true to fail a transaction item simulation.
     * @param txId Transaction ID
     * @param itemIndex Index of the item
     * @return true to simulate failure
     */
    std::function<bool(const QString &txId, int itemIndex)> shouldFailSimulate;

    /**
     * @brief Return true to fail a backend operation.
     * @param backendId Backend ID
     * @param operation Operation type ("load", "push", "delete")
     * @return true to simulate failure
     */
    std::function<bool(const QString &backendId, const QString &operation)> shouldFailBackendOperation;

    /**
     * @brief Return true to inject a conflict for an item.
     * @param uid Item UID
     * @return true to force conflict detection
     */
    std::function<bool(const QString &uid)> shouldForceConflict;

    // =========================================================================
    // Delay Injection (for race condition testing)
    // =========================================================================

    /**
     * @brief Return milliseconds to delay before an operation.
     * @param operation Operation name
     * @return Delay in milliseconds (0 = no delay)
     */
    std::function<int(const QString &operation)> getOperationDelay;

    // =========================================================================
    // Test State
    // =========================================================================

    /**
     * @brief Counter incremented on each transaction start.
     */
    int transactionCount = 0;

    /**
     * @brief Counter incremented on each item commit.
     */
    int commitCount = 0;

    /**
     * @brief Counter incremented on each conflict detected.
     */
    int conflictCount = 0;

    /**
     * @brief Last transaction ID seen.
     */
    QString lastTransactionId;

    /**
     * @brief Last error message captured.
     */
    QString lastErrorMessage;

    // =========================================================================
    // Control
    // =========================================================================

    /**
     * @brief Reset all hooks and counters.
     */
    void reset()
    {
        onTransactionStart = nullptr;
        onBeforeItemCommit = nullptr;
        onAfterItemCommit = nullptr;
        onTransactionEnd = nullptr;
        onBeforeItemSimulate = nullptr;
        onAfterItemSimulate = nullptr;
        onConflictDetected = nullptr;
        onBackendLoad = nullptr;
        onBackendPush = nullptr;
        onBackendDelete = nullptr;
        onSyncMappingStart = nullptr;
        onSyncMappingEnd = nullptr;

        shouldFailCommit = nullptr;
        shouldFailSimulate = nullptr;
        shouldFailBackendOperation = nullptr;
        shouldForceConflict = nullptr;
        getOperationDelay = nullptr;

        transactionCount = 0;
        commitCount = 0;
        conflictCount = 0;
        lastTransactionId.clear();
        lastErrorMessage.clear();
    }

    /**
     * @brief Check if any hooks are installed.
     */
    bool hasHooks() const
    {
        return onTransactionStart || onBeforeItemCommit || onAfterItemCommit ||
               onTransactionEnd || onConflictDetected || shouldFailCommit ||
               shouldFailSimulate || shouldFailBackendOperation;
    }

private:
    SyncTestHooks() = default;
    SyncTestHooks(const SyncTestHooks&) = delete;
    SyncTestHooks& operator=(const SyncTestHooks&) = delete;
};

// Convenience macros for invoking hooks
#define SYNC_HOOK_CALL(hook, ...) \
    do { \
        auto &hooks = SyncTestHooks::instance(); \
        if (hooks.hook) { \
            hooks.hook(__VA_ARGS__); \
        } \
    } while (0)

#define SYNC_HOOK_CHECK_FAIL(hook, ...) \
    (SyncTestHooks::instance().hook && SyncTestHooks::instance().hook(__VA_ARGS__))

#else // !PLANSTAN_TESTING

// No-op stubs when testing is disabled
#define SYNC_HOOK_CALL(hook, ...) do {} while (0)
#define SYNC_HOOK_CHECK_FAIL(hook, ...) false

#endif // PLANSTAN_TESTING

} // namespace Kalburator::Sync

#endif // SYNCTESTHOOKS_H
