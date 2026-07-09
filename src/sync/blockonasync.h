#ifndef KALBURATOR_SYNC_BLOCKONASYNC_H
#define KALBURATOR_SYNC_BLOCKONASYNC_H

#include <QEventLoop>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QThread>

#include <functional>
#include <memory>

namespace Kalburator::Sync {

/**
 * @brief Blocks the CALLING thread on an async continuation delivered on
 * `owner`'s thread (E11 / audit B7).
 *
 * Generalizes the heap-owned, mutex-guarded `Rendezvous` pattern the E5.2/A6
 * engine fast-path uses to block the worker thread on
 * `ChangeDetection::collectionRevisionsAsync` without a nested `QEventLoop`
 * on the backend thread (`SyncEngine::prepareFastPath`,
 * `src/engine/syncengine.cpp`). Hardened for teardown races the same way
 * that call site was (O43, v0.90.1): the loop pointer is nulled under the
 * mutex before the frame dies, so a continuation that lands after this
 * function has returned (e.g. because `owner`'s thread was mid-teardown)
 * drops its result instead of writing into a dangling stack frame.
 *
 * `asyncCall` is posted to `owner`'s thread via `Qt::QueuedConnection` and
 * must eventually invoke the `std::function<void(T)>` it is given, exactly
 * once, from whatever thread the async chain completes on. This works
 * whether `owner`'s thread is or isn't the calling thread: `loop.exec()`
 * drains the calling thread's own queue, so the same-thread case still
 * delivers the posted call and returns correctly (no self-deadlock).
 *
 * NEVER call this from `owner`'s own thread while `owner` is already
 * suspended inside a call originating from this same helper (or any other
 * nested-loop path) — that is the exact B7 re-entrancy hazard this phase
 * closes. Callers should be the worker or GUI thread, never a backend
 * thread mid-operation.
 */
template <typename T>
T blockOnAsync(QObject *owner, std::function<void(std::function<void(T)>)> asyncCall)
{
    struct Rendezvous {
        QMutex mutex;
        QEventLoop *loop = nullptr; // guarded by mutex
        T *result = nullptr;        // guarded by mutex
    };
    auto rv = std::make_shared<Rendezvous>();
    T result{};
    QEventLoop loop;
    rv->loop = &loop;
    rv->result = &result;

    QMetaObject::invokeMethod(owner, [rv, asyncCall]() {
        asyncCall([rv](T value) {
            // May run on any thread (typically owner's). Hand off to the
            // rendezvous's loop thread so `result`/`loop` are only ever
            // touched from the thread that owns their stack frame.
            QMutexLocker lock(&rv->mutex);
            if (!rv->loop)
                return;
            QMetaObject::invokeMethod(rv->loop, [rv, value = std::move(value)]() {
                QMutexLocker lock(&rv->mutex);
                if (rv->result)
                    *rv->result = value;
                if (rv->loop)
                    rv->loop->quit();
            }, Qt::QueuedConnection);
        });
    }, Qt::QueuedConnection);

    loop.exec();

    // Invalidate BEFORE ~loop so a pending continuation either posts while
    // the loop is provably alive or drops cleanly (O43 teardown-safety).
    QMutexLocker lock(&rv->mutex);
    rv->loop = nullptr;
    rv->result = nullptr;

    return result;
}

/**
 * @brief Marshals a plain (non-async-callback) call onto `owner`'s thread
 * and blocks the caller for its return value (E11 / audit B7).
 *
 * For entry points that are already synchronous-but-cheap (e.g.
 * `pushItems`/`deleteItems`, which construct + queue an operation object
 * without doing network I/O inline) yet still need the sanctioned
 * marshal-first discipline instead of a raw cross-thread call — same rule
 * as `blockOnAsync`, minus the `done`-callback plumbing. Same same-thread
 * short-circuit as PlanStan's `queryBackendBlocking`
 * (`PlanStan::queryBackendBlocking`, `src/sync/backendinvoke.h`): same-
 * thread callers run `fn()` directly (no self-deadlock via
 * `Qt::BlockingQueuedConnection` posting to your own queue).
 */
template <typename T>
T callOnOwnerThreadBlocking(QObject *owner, std::function<T()> fn)
{
    if (!owner)
        return T{};
    if (owner->thread() == QThread::currentThread())
        return fn();
    T result{};
    QMetaObject::invokeMethod(
        owner, [&]() { result = fn(); }, Qt::BlockingQueuedConnection);
    return result;
}

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNC_BLOCKONASYNC_H
