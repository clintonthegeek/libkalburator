#ifndef KALBURATOR_MAPPINGQUEUE_H
#define KALBURATOR_MAPPINGQUEUE_H

#include "synctypes.h"

#include <QList>
#include <QSet>
#include <QString>

#include <optional>

namespace Kalburator::Engine {

using Kalburator::Sync::SyncMapping;
using Kalburator::Sync::SyncResult;

/**
 * @brief Per-run queue state for a SyncEngine sync session.
 *
 * Owns the mapping iteration cursor, the per-run result accumulator,
 * the subset filter, and the lost-resource book-keeping for a single
 * sync run. Extracted from SyncEngine as part of Architectural-redress
 * Plan 1 Task 3 to give the queue its own home and stop the engine
 * from being one-class-with-seven-reasons-to-change (INVARIANTS §4).
 *
 * **Thread affinity.** MappingQueue is NOT a QObject and holds no
 * locks. All mutation happens on the SyncEngine's thread:
 *
 *   - `prime()` and `primeSingle()` on `runSync*Future` entry,
 *   - `next()` and resource-lost queries on dispatch (advanceQueue),
 *   - `recordResult()` on worker-completed signals,
 *   - `drain()` once at run end.
 *
 * It does not cross to the worker thread. If a future change requires
 * the worker to read queue state, add an explicit accessor and a
 * documented synchronization story — do not silently widen the
 * affinity contract.
 *
 * **Lifetime.** Value-semantics collaborator owned by SyncEngine.
 * One queue instance is reused across runs via `prime()` (which
 * resets all internal state).
 *
 * **`prime()` is one-shot per run.** Calling `prime()` mid-run
 * (before the previous run finishes) is a programming error and
 * is guarded by the engine's `m_isSyncing` flag at the call site.
 * `prime()` itself does not assert because the queue cannot know
 * whether the engine has already drained or is still mid-run.
 *
 * **Plan 1 Task 4 + Task 5 outcome (2026-05-29):** the canonical
 * runSync entry point now takes a `SyncRequest` (behavior + mapping
 * filter + optional execution override); the override flows as a
 * method parameter through `processSingleMapping`, not as queue
 * state. Behavior remains on
 * the engine as `m_currentSyncBehavior` (per-run, set on entry).
 * `prime()` itself stays minimal — it does not carry behavior or
 * override, which would be unused for the multi-mapping iteration
 * it drives.
 */
class MappingQueue {
public:
    /**
     * @brief F2 Task 21: which entry-path drove the current run.
     *
     * Moved into MappingQueue from SyncEngine as part of Plan 1 Task 3
     * because it is per-run state that lives and dies with the queue.
     * The engine reads this in worker-completed slots to decide whether
     * to finish a single-mapping future or advance the queue.
     */
    enum class DispatchMode {
        None,    ///< No sync in flight.
        Single,  ///< runSync(SyncRequest) — single mapping
        Queue    ///< runSync(SyncRequest) — all-enabled or subset
    };

    MappingQueue() = default;

    /**
     * @brief Initialize the queue for a multi-mapping (Queue) run.
     *
     * Resets all internal state: clears the result accumulator, clears
     * lost resources, resets the index cursor to "before the first
     * mapping", sets DispatchMode to Queue, and stores the mapping
     * list + filter for iteration via `next()`.
     *
     * @param mappings   The full set of mapping candidates for this run.
     *                   `next()` skips entries with `enabled=false` and
     *                   (when `filter` is engaged) entries whose `id` is
     *                   not in the filter set.
     * @param filter     `std::nullopt` = no filter (process all enabled
     *                   mappings); `set` = process only those mapping
     *                   IDs (still respecting `enabled`). An *empty*
     *                   set with `filter` engaged means "run nothing"
     *                   (preserves the G.6 Task 43 semantics that
     *                   `m_hasMappingFilter` used to encode).
     */
    void prime(QList<SyncMapping> mappings,
               std::optional<QSet<QString>> filter);

    /**
     * @brief Initialize the queue for a single-mapping (Single) run.
     *
     * Single-mapping dispatch does not iterate the queue — the engine
     * dispatches one mapping by id and waits for the worker. This
     * helper resets per-run state (lost resources, results) and sets
     * DispatchMode::Single.
     *
     * `next()` returns `nullopt` after `primeSingle()` (single mode
     * doesn't iterate), `recordResult` is a no-op (single mode reports
     * directly via the QFutureInterface), and `drain()` returns an
     * empty list.
     */
    void primeSingle();

    /**
     * @brief Pop and return the next mapping to dispatch.
     *
     * Returns `std::nullopt` when the queue is exhausted (no more
     * enabled-and-in-filter mappings, or DispatchMode != Queue).
     * Caller is responsible for dispatching the mapping to the worker;
     * the queue does not.
     *
     * The current iteration cursor is observable via `currentIndex()`
     * for progress reporting (the engine emits
     * `progressUpdated(currentIndex+1, totalSize(), ...)`).
     */
    std::optional<SyncMapping> next();

    /**
     * @brief Record a per-mapping result during a Queue run.
     *
     * Called from `onWorkerSyncCompleted` (queue mode), and from the
     * resource-lost / skip-unchanged code paths in `advanceQueue`
     * that append synthetic results without dispatching the worker.
     * No-op for Single / None dispatch mode (single-mapping runs
     * report directly on the QFutureInterface).
     */
    void recordResult(SyncResult result);

    /**
     * @brief Return the accumulated results and clear the accumulator.
     *
     * Called once when the run completes (queue drained, cancelled,
     * or early-exit). After `drain()`, the result accumulator is empty
     * but other state (dispatch mode, lost resources, filter, mapping
     * list) is retained until the next `prime*()` call. The engine
     * calls `reset()` separately if it wants to fully scrub state.
     */
    QList<SyncResult> drain();

    /**
     * @brief Reset all per-run state to the post-construction defaults.
     *
     * Idempotent. Intended for terminal paths that want to drop all
     * state at once (results, lost resources, filter, mapping list,
     * index, exhausted flag, DispatchMode::None).
     */
    void reset();

    /**
     * @brief True when iteration has walked past the last mapping.
     *
     * Returns `true` only after at least one `next()` call has
     * returned `std::nullopt` (i.e., the candidate list was exhausted
     * during a Queue run). False before `prime()`, after `prime()` but
     * before any `next()` call, and after `primeSingle()` (which does
     * not iterate).
     */
    bool isExhausted() const { return m_exhausted; }

    // --- Lost-resource tracking ---------------------------------------------

    /**
     * @brief Record that a resource became unavailable mid-queue.
     *
     * G.6 Task 46 semantics: the engine reads this set when picking
     * the next mapping; any mapping whose source or target backend's
     * `resourceId()` matches a lost id is skipped with a cancelled
     * SyncResult (added via `recordResult`).
     */
    void markResourceLost(const QString &resourceId);

    /**
     * @brief True if the given resource id was marked lost on this run.
     */
    bool isResourceLost(const QString &resourceId) const;

    /**
     * @brief True if any resource has been marked lost on this run.
     *
     * Allows the engine to skip the per-mapping resource-id lookup
     * entirely when no resources have been lost (the common case).
     */
    bool hasLostResources() const { return !m_lostResources.isEmpty(); }

    // --- Dispatch-mode tracking ---------------------------------------------

    /// Set the dispatch mode for the current run.
    void setDispatchMode(DispatchMode mode) { m_dispatchMode = mode; }

    /// Current dispatch mode.
    DispatchMode dispatchMode() const { return m_dispatchMode; }

    // --- Accessors ----------------------------------------------------------

    /**
     * @brief Index of the most recently returned mapping, for progress.
     *
     * `-1` before the first `next()` call; otherwise the 0-based index
     * into the candidate list passed to `prime()`. Skipped entries
     * (not enabled, not in filter) are counted in the index so it
     * remains the "position within the candidate list" — this matches
     * the pre-extraction `m_currentMappingIndex` semantics that
     * `progressUpdated` consumed.
     */
    int currentIndex() const { return m_currentIndex; }

    /**
     * @brief Size of the candidate list passed to `prime()`.
     *
     * Used for progress reporting alongside `currentIndex()`.
     */
    int totalSize() const { return m_mappings.size(); }

private:
    /// The candidate list for this run; copied in at `prime()` time.
    QList<SyncMapping> m_mappings;

    /// Per-run subset filter; `std::nullopt` means no filter.
    std::optional<QSet<QString>> m_filter;

    /// Accumulated per-mapping results during a Queue run.
    QList<SyncResult> m_results;

    /// Resources marked lost mid-queue (G.6 Task 46).
    QSet<QString> m_lostResources;

    /// Iteration cursor; -1 before the first `next()`.
    int m_currentIndex = -1;

    /// True once `next()` returns `nullopt` because the candidate
    /// list was walked. Reset by `prime*()` / `reset()`.
    bool m_exhausted = false;

    /// Single vs Queue vs None (no run in flight).
    DispatchMode m_dispatchMode = DispatchMode::None;
};

} // namespace Kalburator::Engine

#endif // KALBURATOR_MAPPINGQUEUE_H
