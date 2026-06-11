// Phase 5 step 2: SyncRunCoordinator
// Extracted from PlanStan::CollectionController per spec
// 2026-05-22-collectioncontroller-decomp-and-akonadi-api-design.md

#include "syncruncoordinator.h"

#include "syncengine.h"
#include "syncrequest.h"
#include "synctypes.h"

#include <QDebug>

namespace Kalburator::Sync {

SyncRunCoordinator::SyncRunCoordinator(Kalburator::Engine::SyncEngine *engine,
                                       Kalburator::Storage::BaselineStore *baselineStore,
                                       Kalburator::Sync::SyncConflictStore *conflictStore,
                                       Kalburator::Sync::ConflictManager *conflictMgr,
                                       QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_baselineStore(baselineStore)
    , m_conflictStore(conflictStore)
    , m_conflictMgr(conflictMgr)
{
    Q_ASSERT(m_engine);
}

SyncRunCoordinator::~SyncRunCoordinator() = default;

void SyncRunCoordinator::runSync(Kalburator::Engine::SyncEngine::SyncBehavior behavior)
{
    if (!m_engine) {
        qWarning() << "SyncRunCoordinator::runSync: engine is null (single backend?)";
        return;
    }

    if (!m_engine->hasSyncWork()) {
        qDebug() << "SyncRunCoordinator::runSync: no sync work configured";
        return;
    }

    if (m_engine->isSyncing()) {
        qDebug() << "SyncRunCoordinator::runSync: sync already in progress";
        return;
    }

    qDebug() << "SyncRunCoordinator: starting sync (behavior="
             << (behavior == Kalburator::Engine::SyncEngine::SyncBehavior::Monitored
                 ? "Monitored" : "Unmonitored")
             << ")";

    // F2 (Task 37): use runSync(SyncRequest) + QFutureWatcher rather than
    // the void runSync() overload + allSyncsCompleted signal.
    if (m_watcher) {
        m_watcher->disconnect(this);
        m_watcher->deleteLater();
        m_watcher = nullptr;
    }

    Kalburator::Engine::SyncRequest req;
    req.behavior = behavior;
    auto future = m_engine->runSync(req);
    m_watcher = new QFutureWatcher<QList<SyncResult>>(this);
    connect(m_watcher, &QFutureWatcher<QList<SyncResult>>::finished,
            this, &SyncRunCoordinator::onSyncRunFinished);
    m_watcher->setFuture(future);

    emit syncRunStarted();
}

QFuture<QList<Kalburator::Sync::SyncResult>> SyncRunCoordinator::currentSyncFuture() const
{
    if (m_watcher) {
        return m_watcher->future();
    }
    return QFuture<QList<SyncResult>>();
}

void SyncRunCoordinator::onSyncRunFinished()
{
    if (!m_watcher) return;

    auto future = m_watcher->future();

    // Per FINDINGS / Task 23: future.results() returns empty after a cancel;
    // use future.resultAt(0) when not canceled.
    QList<SyncResult> results;
    if (!future.isCanceled() && future.resultCount() > 0) {
        results = future.resultAt(0);
    }

    // Build aggregate SyncResult mirroring the prior allSyncsCompleted payload.
    SyncResult aggregate;
    aggregate.success = !future.isCanceled();
    for (const SyncResult &r : results) {
        qDebug() << "SyncRunCoordinator: mapping completed:"
                 << "success:" << r.success
                 << "source:" << r.sourceStats.summary()
                 << "target:" << r.targetStats.summary();
        if (!r.success) {
            aggregate.success = false;
            if (aggregate.errorMessage.isEmpty() && !r.errorMessage.isEmpty()) {
                aggregate.errorMessage = r.errorMessage;
            }
        }
    }
    if (future.isCanceled() && aggregate.errorMessage.isEmpty()) {
        aggregate.errorMessage = QStringLiteral("Sync canceled");
    }

    qDebug() << "SyncRunCoordinator: all syncs completed, success:" << aggregate.success;
    if (!aggregate.errorMessage.isEmpty()) {
        qWarning() << "  Error:" << aggregate.errorMessage;
    }

    emit syncRunFinished(aggregate);
    emit allSyncsFinished(aggregate.success);
}

} // namespace Kalburator::Sync
