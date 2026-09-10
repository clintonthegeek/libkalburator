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
    if (!m_backend || m_started) return false;
    auto *object = dynamic_cast<QObject *>(m_backend.get());
    if (!object || object->parent() || object->thread() != QThread::currentThread())
        return false;
    // KDAV creates network children through a main-thread-affine manager.
    // Moving its calendar backend to a private executor thread makes those
    // children illegal and can strand a fetch forever after reconnect.
    if (QString::fromLatin1(object->metaObject()->className())
            .endsWith(QStringLiteral("RemoteCalendarBackend")))
    {
        m_started = true;
        return true;
    }
    object->moveToThread(&m_thread);
    m_thread.start();
    m_threaded = true;
    m_started = true;
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
        if (object && QThread::currentThread() != object->thread()) {
            QMetaObject::invokeMethod(object, [object]() { delete object; },
                                      Qt::BlockingQueuedConnection);
            m_backend.release();
        } else {
            m_backend.reset();
        }
    }
    m_started = false;
    m_threaded = false;
    return true;
}

} // namespace Kalburator::Sync
