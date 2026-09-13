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
    //
    // KindDemuxBackend must stay put for the same reason: for an account with
    // any VTODO-bearing calendar, MultiProtocolDavProvider registers one demux
    // per domain ("cal", "todo") routing into a SHARED RemoteCalendarBackend
    // transport that lives on this thread. Threading the demux made the
    // engine marshal onto the demux's private thread, which then called the
    // GUI-thread transport directly — its network manager, KDAV jobs and
    // SQLite content cache all used cross-thread, and two demux threads
    // racing one non-thread-safe transport. Observed against Nextcloud as
    // spurious "Invalid username/password (401)" list failures, "Cannot
    // create children for a parent that is in a different thread", SQLite
    // "database does not belong to the calling thread", and a first sync
    // that never finished.
    const QString className = QString::fromLatin1(object->metaObject()->className());
    if (className.endsWith(QStringLiteral("RemoteCalendarBackend"))
        || className.endsWith(QStringLiteral("KindDemuxBackend")))
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
