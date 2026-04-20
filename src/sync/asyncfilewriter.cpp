#include "asyncfilewriter.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QTimeZone>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

// ============================================================================
// AsyncFileWriterWorker Implementation
// ============================================================================

AsyncFileWriterWorker::AsyncFileWriterWorker(QObject *parent)
    : QObject(parent)
{
}

void AsyncFileWriterWorker::enqueue(const WriteRequest &request)
{
    QMutexLocker locker(&m_mutex);
    m_queue.enqueue(request);
    m_totalQueued++;
    m_condition.wakeOne();
}

void AsyncFileWriterWorker::setFinishing(bool finishing)
{
    QMutexLocker locker(&m_mutex);
    m_finishing = finishing;
    m_condition.wakeOne();
}

void AsyncFileWriterWorker::stop()
{
    QMutexLocker locker(&m_mutex);
    m_stopped = true;
    m_condition.wakeOne();
}

void AsyncFileWriterWorker::reset()
{
    QMutexLocker locker(&m_mutex);
    m_queue.clear();
    m_finishing = false;
    m_stopped = false;
    m_totalQueued = 0;
}

int AsyncFileWriterWorker::pendingCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_queue.size();
}

void AsyncFileWriterWorker::processQueue()
{
    int successCount = 0;
    int failCount = 0;
    int completed = 0;

    while (true) {
        WriteRequest request;
        bool hasRequest = false;
        bool shouldFinish = false;

        {
            QMutexLocker locker(&m_mutex);

            // Wait for work or stop signal
            while (m_queue.isEmpty() && !m_finishing && !m_stopped) {
                m_condition.wait(&m_mutex);
            }

            if (m_stopped) {
                break;
            }

            if (!m_queue.isEmpty()) {
                request = m_queue.dequeue();
                hasRequest = true;
            } else if (m_finishing && m_queue.isEmpty()) {
                shouldFinish = true;
            }
        }

        if (hasRequest) {
            // Perform the actual file write (this is what we want off the main thread)
            bool success = false;
            QString errorMessage;

            // Get data to write - either pre-serialized or serialize now
            QByteArray dataToWrite;
            if (request.needsSerialization && request.incidence) {
                // Serialize incidence in worker thread (not main thread!)
                KCalendarCore::ICalFormat icalFormat;
                auto tempCal = QSharedPointer<KCalendarCore::MemoryCalendar>(
                    new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone())
                );
                tempCal->addIncidence(request.incidence);
                QString icalData = icalFormat.toString(tempCal);
                dataToWrite = icalData.toUtf8();
            } else {
                dataToWrite = request.data;
            }

            // Ensure parent directory exists
            QFileInfo fileInfo(request.filePath);
            QDir dir = fileInfo.dir();
            if (!dir.exists()) {
                dir.mkpath(".");
            }

            // Use QFile instead of QSaveFile for bulk writes - much faster
            // (QSaveFile does fsync per file which is very slow for 500+ files)
            // For sync operations, corrupted files get fixed on next sync anyway
            QFile file(request.filePath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                if (file.write(dataToWrite) != -1) {
                    file.close();
                    success = true;
                } else {
                    errorMessage = QStringLiteral("Failed to write: %1").arg(file.errorString());
                }
            } else {
                errorMessage = QStringLiteral("Failed to open: %1").arg(file.errorString());
            }

            if (success) {
                successCount++;
            } else {
                failCount++;
                qWarning() << "AsyncFileWriter: Write failed for" << request.filePath << ":" << errorMessage;
            }

            completed++;

            // Only emit writeCompleted for failures (reduces signal spam)
            if (!success) {
                emit writeCompleted(request.filePath, request.identifier, success, errorMessage);
            }

            int total;
            {
                QMutexLocker locker(&m_mutex);
                total = m_totalQueued;
            }

            // Batch progress updates: emit every 10 items or on last item
            if (completed % 10 == 0 || completed == total) {
                emit progressChanged(completed, total);
            }
        }

        if (shouldFinish) {
            emit allWritesCompleted(successCount, failCount);
            break;
        }
    }
}

// ============================================================================
// AsyncFileWriter Implementation
// ============================================================================

AsyncFileWriter::AsyncFileWriter(QObject *parent)
    : QObject(parent)
{
    m_worker = new AsyncFileWriterWorker();
    m_worker->moveToThread(&m_workerThread);

    // Connect worker signals to this object's signals (cross-thread)
    connect(m_worker, &AsyncFileWriterWorker::writeCompleted,
            this, &AsyncFileWriter::writeCompleted, Qt::QueuedConnection);
    connect(m_worker, &AsyncFileWriterWorker::allWritesCompleted,
            this, &AsyncFileWriter::allWritesCompleted, Qt::QueuedConnection);
    connect(m_worker, &AsyncFileWriterWorker::progressChanged,
            this, &AsyncFileWriter::progressChanged, Qt::QueuedConnection);

    // Start processing when thread starts
    connect(&m_workerThread, &QThread::started,
            m_worker, &AsyncFileWriterWorker::processQueue);

    // Note: Worker is NOT deleted when thread finishes - we reuse it across multiple batches
    // Worker is deleted in AsyncFileWriter destructor
}

AsyncFileWriter::~AsyncFileWriter()
{
    stop();
    // After stop() returns, the thread's event loop has stopped and it's safe
    // to delete the worker directly. Note: moveToThread() cannot be used here
    // because you can only "push" objects to another thread from the thread
    // they're currently on (we're on main thread, worker is on worker thread).
    if (m_worker) {
        delete m_worker;
        m_worker = nullptr;
    }
}

void AsyncFileWriter::start()
{
    if (!m_workerThread.isRunning()) {
        // Reset worker state before starting a new batch
        // (clears queue, counters, and stopped/finishing flags from previous batch)
        if (m_worker) {
            m_worker->reset();
        }
        m_workerThread.start();
    }
}

void AsyncFileWriter::queueWrite(const QString &filePath, const QByteArray &data,
                                  const QString &identifier)
{
    if (!m_workerThread.isRunning()) {
        qWarning() << "AsyncFileWriter::queueWrite called but worker not started";
        return;
    }

    AsyncFileWriterWorker::WriteRequest request;
    request.filePath = filePath;
    request.data = data;
    request.identifier = identifier;
    request.needsSerialization = false;

    m_worker->enqueue(request);
}

void AsyncFileWriter::queueIncidenceWrite(const QString &filePath,
                                           const KCalendarCore::Incidence::Ptr &incidence,
                                           const QString &identifier)
{
    if (!m_workerThread.isRunning()) {
        qWarning() << "AsyncFileWriter::queueIncidenceWrite called but worker not started";
        return;
    }

    AsyncFileWriterWorker::WriteRequest request;
    request.filePath = filePath;
    request.incidence = incidence;
    request.identifier = identifier;
    request.needsSerialization = true;

    m_worker->enqueue(request);
}

void AsyncFileWriter::finishWrites()
{
    if (m_worker) {
        m_worker->setFinishing(true);
    }
}

void AsyncFileWriter::stop()
{
    if (m_workerThread.isRunning()) {
        if (m_worker) {
            m_worker->stop();
        }
        m_workerThread.quit();
        m_workerThread.wait();
    }
}

bool AsyncFileWriter::isRunning() const
{
    return m_workerThread.isRunning();
}

int AsyncFileWriter::pendingCount() const
{
    return m_worker ? m_worker->pendingCount() : 0;
}
