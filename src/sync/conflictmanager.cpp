#include "conflictmanager.h"
#include "iconflictpresenter.h"
#include "syncstore.h"
#include "iconflictresolver.h"

#include <QDebug>

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

void ConflictManager::setSyncStore(SyncStore *store)
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

    // If resolved (not skipped), mark as resolved
    if (resolution != ConflictResolution::Skip && m_syncStore && !conflictId.isEmpty()) {
        m_syncStore->resolveConflict(conflictId, resolution);
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

    // Record and immediately resolve
    QString conflictId = conflict.conflictId;
    if (m_syncStore) {
        if (conflictId.isEmpty()) {
            conflictId = m_syncStore->recordConflict(conflict);
        }
        m_syncStore->resolveConflict(conflictId, resolution);
    }

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

    // Note: ConflictManager only marks resolutions in the store.
    // SyncCoordinator reads the resolution and applies data modifications.

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

QString ConflictManager::lastMergedIcalData() const
{
    if (m_conflictResolver) {
        return m_conflictResolver->lastMergedIcalData();
    }
    return QString();
}
