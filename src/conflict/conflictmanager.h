#ifndef CONFLICTMANAGER_H
#define CONFLICTMANAGER_H

#include "synctypes.h"
#include "iconflictresolver.h"

#include <QObject>
#include <QHash>
#include <memory>

class QWidget;

namespace Kalburator::Sync {

class IConflictPresenter;
class SyncConflictStore;

/**
 * @brief Coordinates conflict resolution workflow.
 *
 * ConflictManager is the central coordinator for handling sync conflicts.
 * It decides whether to show immediate modal dialogs or queue conflicts
 * for later resolution via the dock widget, based on user settings.
 *
 * Workflow modes:
 * - Immediate (Blocking): Shows modal dialog for each conflict during sync.
 *   User must resolve before sync continues. Good for small conflict counts.
 *
 * - Deferred (Non-blocking): Records conflicts and continues sync.
 *   User resolves conflicts later via the dock widget. Good for large syncs
 *   or background sync operations.
 *
 * - Hybrid: Shows immediate dialog if conflict count is low (e.g., < 3),
 *   otherwise defers to dock widget.
 *
 * The manager also applies automatic resolution policies (LastWriteWins,
 * SourceWins, TargetWins) when configured.
 */
class ConflictManager : public QObject
{
    Q_OBJECT

public:
    explicit ConflictManager(QObject *parent = nullptr);

    /**
     * @brief Resolution workflow mode.
     */
    enum class WorkflowMode {
        Immediate,      ///< Always show modal dialogs
        Deferred,       ///< Always queue for dock widget
        Hybrid,         ///< Modal for few conflicts, deferred for many
        AutoResolve     ///< Apply automatic policy, no UI
    };
    Q_ENUM(WorkflowMode)

    /**
     * @brief Set the workflow mode.
     */
    void setWorkflowMode(WorkflowMode mode);
    WorkflowMode workflowMode() const { return m_workflowMode; }

    /**
     * @brief Set the automatic resolution policy.
     * Used when mode is AutoResolve or as fallback.
     */
    void setAutoResolutionPolicy(ConflictResolution policy);
    ConflictResolution autoResolutionPolicy() const { return m_autoPolicy; }

    /**
     * @brief Set the threshold for hybrid mode.
     * If conflict count <= threshold, use immediate mode.
     */
    void setHybridThreshold(int threshold);
    int hybridThreshold() const { return m_hybridThreshold; }

    /**
     * @brief Set the SyncConflictStore for recording/querying conflicts.
     */
    void setSyncConflictStore(SyncConflictStore *store);
    SyncConflictStore *syncConflictStore() const { return m_syncStore; }

    /**
     * @brief Set the parent widget for modal dialogs.
     */
    void setParentWidget(QWidget *parent);

    /**
     * @brief Set the conflict presenter for deferred resolution.
     */
    void setConflictPresenter(IConflictPresenter *presenter);

    /**
     * @brief Set the conflict resolver implementation.
     *
     * For production use, DialogConflictResolver shows the standard dialog.
     * For testing, inject a mock resolver that returns predetermined resolutions.
     *
     * If not set, conflicts in Immediate/Hybrid mode are skipped with a warning.
     * The app shell is responsible for setting a resolver before sync starts.
     * ConflictManager takes ownership of the resolver.
     *
     * @param resolver The resolver to use (ConflictManager takes ownership)
     */
    void setConflictResolver(IConflictResolver *resolver);

    /**
     * @brief Get the current conflict resolver.
     * @return The resolver, or nullptr if using default (dialog)
     */
    IConflictResolver *conflictResolver() const;

    /**
     * @brief Handle a newly detected conflict.
     *
     * Based on workflow mode, either shows a modal dialog,
     * queues for deferred resolution, or applies automatic policy.
     *
     * @param conflict The conflict to handle
     * @return The resolution that was applied, or AskUser if deferred
     */
    ConflictResolution handleConflict(const ConflictInfo &conflict);

    /**
     * @brief Handle multiple conflicts from a sync operation.
     *
     * Uses hybrid logic to decide between immediate and deferred modes.
     *
     * @param conflicts List of conflicts to handle
     * @return Map of conflictId -> resolution (empty for deferred conflicts)
     */
    QMap<QString, ConflictResolution> handleConflicts(const QList<ConflictInfo> &conflicts);

    /**
     * @brief Apply a resolution to a conflict.
     *
     * This performs the actual resolution action (not just recording it).
     * Should be called by UI components after user makes a choice.
     *
     * @param conflictId The conflict ID
     * @param resolution The resolution to apply
     * @return true if resolution was successfully applied
     */
    bool applyResolution(const QString &conflictId, ConflictResolution resolution);

    /**
     * @brief Get count of unresolved conflicts.
     */
    int unresolvedConflictCount() const;

    /**
     * @brief Get the merged iCal data from the last CustomMerge resolution.
     *
     * Only valid if the last handleConflict() returned CustomMerge.
     * Used by SyncEngine to get the actual merged incidence data.
     *
     * @return The merged iCal data, or empty if not applicable
     */
    QString lastMergedIcalData() const;

    /**
     * @brief The merged payload captured for one specific conflict.
     *
     * Bug B (docs/2026-08-21-conflict-info-canonical-data-and-unmonitored-
     * resolution-handoff.md): lastMergedIcalData() is last-call-scoped, which
     * is useless to the batch path — handleConflicts() loops over N conflicts
     * and by the time the caller reacts to conflict number 1's
     * conflictResolved(), the resolver has already been asked about number N.
     * So the manager now captures the payload per conflict as each
     * resolveConflict() returns, and SyncEngine reads it here inside its
     * conflictResolved() slot.
     *
     * Additive on purpose: conflictResolved's signature is NOT changed —
     * PlanStan and WildPalms connect to it and neither can absorb a signature
     * change in this fix.
     *
     * @return The merged payload for @p conflictId, or empty if that
     *         conflict's resolution was not CustomMerge (or produced nothing).
     */
    QString mergedDataFor(const QString &conflictId) const;

signals:
    /**
     * @brief Emitted when a conflict is resolved.
     * @param conflictId The conflict ID
     * @param resolution The resolution applied
     */
    void conflictResolved(const QString &conflictId, ConflictResolution resolution);

    /**
     * @brief Emitted when a conflict is queued for deferred resolution.
     */
    void conflictQueued(const ConflictInfo &conflict);

    /**
     * @brief Emitted when the unresolved count changes.
     */
    void unresolvedCountChanged(int count);

private:
    ConflictResolution showImmediateDialog(const ConflictInfo &conflict);
    void queueForDeferred(const ConflictInfo &conflict);
    ConflictResolution applyAutoPolicy(const ConflictInfo &conflict);

    WorkflowMode m_workflowMode = WorkflowMode::Immediate;
    ConflictResolution m_autoPolicy = ConflictResolution::AskUser;
    int m_hybridThreshold = 3;

    /// Bug B: conflictId -> the merged payload the resolver produced for it.
    /// Written only by showImmediateDialog (the only path that calls a
    /// resolver); a non-CustomMerge resolution REMOVES the entry so a stale
    /// merge can never be picked up by a later resolution of the same id.
    QHash<QString, QString> m_mergedByConflictId;

    SyncConflictStore *m_syncStore = nullptr;
    IConflictPresenter *m_conflictPresenter = nullptr;
    QWidget *m_parentWidget = nullptr;
    std::unique_ptr<IConflictResolver> m_conflictResolver;
};

} // namespace Kalburator::Sync

#endif // CONFLICTMANAGER_H
