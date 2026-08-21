#include "conflictmanager.h"
#include "iconflictpresenter.h"
#include "syncconflictstore.h"
#include "iconflictresolver.h"

#include <QDebug>

namespace Kalburator::Sync {

ConflictManager::ConflictManager(QObject *parent)
    : QObject(parent)
{
}

void ConflictManager::setWorkflowMode(WorkflowMode mode)
{
    m_workflowMode = mode;
}

void ConflictManager::setAutoResolutionPolicy(ConflictResolution policy)
{
    m_autoPolicy = policy;
}

void ConflictManager::setHybridThreshold(int threshold)
{
    m_hybridThreshold = qMax(1, threshold);
}

void ConflictManager::setSyncConflictStore(SyncConflictStore *store)
{
    m_syncStore = store;
}

void ConflictManager::setParentWidget(QWidget *parent)
{
    m_parentWidget = parent;
}

void ConflictManager::setConflictPresenter(IConflictPresenter *presenter)
{
    m_conflictPresenter = presenter;
    // Signal wiring (conflictResolved) is done by the app shell
    // to avoid QObject diamond inheritance with QWidget-based presenters.
}

void ConflictManager::setConflictResolver(IConflictResolver *resolver)
{
    m_conflictResolver.reset(resolver);
}

IConflictResolver *ConflictManager::conflictResolver() const
{
    return m_conflictResolver.get();
}

ConflictResolution ConflictManager::handleConflict(const ConflictInfo &conflict)
{
    switch (m_workflowMode) {
        case WorkflowMode::Immediate:
            return showImmediateDialog(conflict);

        case WorkflowMode::Deferred:
            queueForDeferred(conflict);
            return ConflictResolution::AskUser;  // Indicates deferred

        case WorkflowMode::Hybrid:
            // For single conflict, use immediate
            return showImmediateDialog(conflict);

        case WorkflowMode::AutoResolve:
            return applyAutoPolicy(conflict);
    }

    return ConflictResolution::Skip;
}

QMap<QString, ConflictResolution> ConflictManager::handleConflicts(const QList<ConflictInfo> &conflicts)
{
    QMap<QString, ConflictResolution> results;

    if (conflicts.isEmpty()) {
        return results;
    }

    // Decide mode based on conflict count for hybrid
    bool useImmediate = true;
    if (m_workflowMode == WorkflowMode::Deferred) {
        useImmediate = false;
    } else if (m_workflowMode == WorkflowMode::Hybrid) {
        useImmediate = conflicts.size() <= m_hybridThreshold;
    } else if (m_workflowMode == WorkflowMode::AutoResolve) {
        // Apply auto policy to all
        for (const ConflictInfo &conflict : conflicts) {
            ConflictResolution resolution = applyAutoPolicy(conflict);
            if (!conflict.conflictId.isEmpty()) {
                results[conflict.conflictId] = resolution;
            }
        }
        return results;
    }

    if (useImmediate) {
        // Show dialog for each conflict
        for (int idx = 0; idx < conflicts.size(); ++idx) {
            const ConflictInfo &conflict = conflicts[idx];
            ConflictResolution resolution = showImmediateDialog(conflict);
            if (!conflict.conflictId.isEmpty()) {
                results[conflict.conflictId] = resolution;
            }

            // If user cancelled, stop processing remaining conflicts
            if (resolution == ConflictResolution::Skip) {
                // Queue remaining conflicts
                for (int i = idx + 1; i < conflicts.size(); ++i) {
                    queueForDeferred(conflicts[i]);
                }
                break;
            }
        }
    } else {
        // Queue all for deferred resolution
        for (const ConflictInfo &conflict : conflicts) {
            queueForDeferred(conflict);
        }
    }

    return results;
}

ConflictResolution ConflictManager::showImmediateDialog(const ConflictInfo &conflict)
{
    // Record conflict first so it appears in dock if dialog is cancelled
    QString conflictId = conflict.conflictId;
    if (m_syncStore && conflictId.isEmpty()) {
        conflictId = m_syncStore->recordConflict(conflict);
    }

    if (!m_conflictResolver) {
        qWarning() << "ConflictManager: no conflict resolver set, skipping conflict";
        return ConflictResolution::Skip;
    }

    // Use resolver (dialog in production, mock in tests)
    ConflictResolution resolution = m_conflictResolver->resolveConflict(
        conflict, m_parentWidget);

    // Bug B (docs/2026-08-21-conflict-info-canonical-data-and-unmonitored-
    // resolution-handoff.md §B): capture the merged payload PER CONFLICT, here,
    // while it is still this conflict's. lastMergedIcalData() is last-call
    // scoped, so in the batch loop (handleConflicts) it has already been
    // overwritten by the time anyone reacts to this conflict's
    // conflictResolved(). A non-CustomMerge resolution clears any stale entry.
    if (!conflictId.isEmpty()) {
        if (resolution == ConflictResolution::CustomMerge)
            m_mergedByConflictId.insert(conflictId,
                                        m_conflictResolver->lastMergedIcalData());
        else
            m_mergedByConflictId.remove(conflictId);
    }

    // If resolved (not skipped), mark as resolved.
    //
    // Bug B: conflictResolved is now emitted for EVERY real resolution, not
    // only when a SyncConflictStore happens to be attached. It is the single
    // channel by which a user's choice reaches SyncEngine (which is what
    // finally applies it — see the applyResolution() comment below), so a
    // host with no conflict store used to get no application at all. The
    // STORE WRITE stays conditional; only the signal became unconditional.
    if (resolution != ConflictResolution::Skip) {
        if (m_syncStore && !conflictId.isEmpty()) {
            m_syncStore->resolveConflict(conflictId, resolution);
        }
        emit conflictResolved(conflictId, resolution);
        emit unresolvedCountChanged(unresolvedConflictCount());
    }

    return resolution;
}

void ConflictManager::queueForDeferred(const ConflictInfo &conflict)
{
    // Record in store
    QString conflictId = conflict.conflictId;
    if (m_syncStore && conflictId.isEmpty()) {
        conflictId = m_syncStore->recordConflict(conflict);
    }

    // Refresh conflict presenter
    if (m_conflictPresenter) {
        m_conflictPresenter->refreshConflicts();
    }

    // Create copy with ID for signal
    ConflictInfo conflictWithId = conflict;
    conflictWithId.conflictId = conflictId;

    emit conflictQueued(conflictWithId);
    emit unresolvedCountChanged(unresolvedConflictCount());
}

ConflictResolution ConflictManager::applyAutoPolicy(const ConflictInfo &conflict)
{
    ConflictResolution resolution = m_autoPolicy;

    // Handle LastWriteWins
    if (resolution == ConflictResolution::LastWriteWins) {
        if (conflict.sourceModified.isValid() && conflict.targetModified.isValid()) {
            resolution = conflict.sourceModified > conflict.targetModified
                ? ConflictResolution::SourceWins
                : ConflictResolution::TargetWins;
        } else {
            // Can't determine, fall back to SourceWins
            resolution = ConflictResolution::SourceWins;
        }
    }

    // Record and immediately resolve.
    //
    // Bug B: this path had the identical "store-only, no data write" shape as
    // showImmediateDialog — the handoff suspected as much and it is confirmed.
    // It already emitted conflictResolved unconditionally, so it needed no
    // change here: SyncEngine's new listener is what turns the emission into
    // an actual write. Left emitting even for a Skip/AskUser auto-policy (a
    // pre-existing quirk, and a consumer may be counting on the signal); the
    // engine ignores those, since neither names a version to keep.
    QString conflictId = conflict.conflictId;
    if (m_syncStore) {
        if (conflictId.isEmpty()) {
            conflictId = m_syncStore->recordConflict(conflict);
        }
        m_syncStore->resolveConflict(conflictId, resolution);
    }
    m_mergedByConflictId.remove(conflictId);

    emit conflictResolved(conflictId, resolution);
    return resolution;
}

bool ConflictManager::applyResolution(const QString &conflictId, ConflictResolution resolution)
{
    if (!m_syncStore || conflictId.isEmpty()) {
        return false;
    }

    // Get conflict info
    ConflictInfo conflict = m_syncStore->conflict(conflictId);
    if (conflict.conflictId.isEmpty()) {
        qWarning() << "ConflictManager::applyResolution: Conflict not found:" << conflictId;
        return false;
    }

    // ConflictManager only marks resolutions in the store; SyncEngine reads
    // the resolution and applies the data modifications.
    //
    // Bug B (docs/2026-08-21-conflict-info-canonical-data-and-unmonitored-
    // resolution-handoff.md §B): that sentence described an intent nobody had
    // wired up — nothing in SyncEngine ever read a resolution back, so a
    // choice made here (the deferred/dock path) changed one DB column and
    // nothing else, forever. As of conflict-resolution-repair Task 3 it is
    // true: SyncEngine listens to conflictResolved below, stores the choice as
    // a PendingConflictResolution, and the next dispatchSync for that mapping
    // replays it through the engine's normal write path.

    m_syncStore->resolveConflict(conflictId, resolution);
    emit conflictResolved(conflictId, resolution);
    emit unresolvedCountChanged(unresolvedConflictCount());

    return true;
}

int ConflictManager::unresolvedConflictCount() const
{
    if (!m_syncStore) {
        return 0;
    }
    return m_syncStore->unresolvedConflictCount();
}

QString ConflictManager::mergedDataFor(const QString &conflictId) const
{
    return m_mergedByConflictId.value(conflictId);
}

QString ConflictManager::lastMergedIcalData() const
{
    if (m_conflictResolver) {
        return m_conflictResolver->lastMergedIcalData();
    }
    return QString();
}


} // namespace Kalburator::Sync
