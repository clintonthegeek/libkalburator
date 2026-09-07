#include <kalburator/sync/backendexecutor.h>

#include <QDebug>
#include <QMetaObject>

namespace Kalburator::Sync {

BackendExecutor::BackendExecutor(std::unique_ptr<IBlobBackend> backend)
    : m_backend(std::move(backend))
{
    m_thread.setObjectName(QStringLiteral("kalburator-backend"));
}

BackendExecutor::~BackendExecutor()
{
    shutdown();
}

bool BackendExecutor::start()
{
    if (!m_backend || m_thread.isRunning()) return false;
    auto *object = dynamic_cast<QObject *>(m_backend.get());
    if (!object || object->parent() || object->thread() != QThread::currentThread())
        return false;
    object->moveToThread(&m_thread);
    m_thread.start();
    return true;
}

bool BackendExecutor::shutdown(int timeoutMs)
{
    if (!m_backend) return true;
    auto *object = dynamic_cast<QObject *>(m_backend.get());
    if (m_thread.isRunning()) {
        if (object && QThread::currentThread() != object->thread()) {
            QMetaObject::invokeMethod(object, [object]() { delete object; },
                                      Qt::BlockingQueuedConnection);
            m_backend.release();
        } else {
            delete object;
            m_backend.release();
        }
        m_thread.quit();
        if (!m_thread.wait(timeoutMs)) {
            qWarning() << "BackendExecutor: shutdown exceeded deadline";
            m_thread.wait();
            return false;
        }
    } else {
        m_backend.reset();
    }
    return true;
}

} // namespace Kalburator::Sync
