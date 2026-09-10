#ifndef KALBURATOR_SYNC_BACKENDEXECUTOR_H
#define KALBURATOR_SYNC_BACKENDEXECUTOR_H

#include <QThread>

#include <memory>
#include <utility>

#include <kalburator/blob/iblobbackend.h>

namespace Kalburator::Sync {

/** Owns the execution thread and lifetime of one QObject-backed backend. */
class BackendExecutor final
{
public:
    explicit BackendExecutor(std::unique_ptr<IBlobBackend> backend);
    ~BackendExecutor();

    BackendExecutor(const BackendExecutor &) = delete;
    BackendExecutor &operator=(const BackendExecutor &) = delete;

    bool start();
    bool shutdown(int timeoutMs = 30000);
    bool isRunning() const { return m_started && (!m_threaded || m_thread.isRunning()); }
    IBlobBackend *backend() const { return m_backend.get(); }
    QObject *backendObject() const { return dynamic_cast<QObject *>(m_backend.get()); }
    QThread *thread() { return &m_thread; }

    template <typename Callable>
    bool invoke(Callable &&callable)
    {
        if (!m_backend) return false;
        auto *object = dynamic_cast<QObject *>(m_backend.get());
        if (!object) return false;
        if (QThread::currentThread() == object->thread()) {
            std::forward<Callable>(callable)();
            return true;
        }
        return QMetaObject::invokeMethod(object,
                                         std::forward<Callable>(callable),
                                         Qt::BlockingQueuedConnection);
    }

private:
    std::unique_ptr<IBlobBackend> m_backend;
    QThread m_thread;
    bool m_started = false;
    bool m_threaded = false;
};

} // namespace Kalburator::Sync

#endif
