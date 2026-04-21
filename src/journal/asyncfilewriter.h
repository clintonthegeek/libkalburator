#ifndef ASYNCFILEWRITER_H
#define ASYNCFILEWRITER_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <QString>
#include <QByteArray>
#include <KCalendarCore/Incidence>

namespace Kalburator::Sync {

/**
 * @brief Internal worker class for AsyncFileWriter.
 *
 * Runs in a background thread and processes file write requests.
 */
class AsyncFileWriterWorker : public QObject {
    Q_OBJECT
public:
    struct WriteRequest {
        QString filePath;
        QByteArray data;           // Pre-serialized data (if available)
        QString identifier;
        KCalendarCore::Incidence::Ptr incidence;  // For deferred serialization
        bool needsSerialization = false;
    };

    explicit AsyncFileWriterWorker(QObject *parent = nullptr);

    void enqueue(const WriteRequest &request);
    void setFinishing(bool finishing);
    void stop();
    void reset();  // Reset state for reuse
    int pendingCount() const;

public slots:
    void processQueue();

signals:
    void writeCompleted(const QString &filePath, const QString &identifier,
                        bool success, const QString &errorMessage);
    void allWritesCompleted(int successCount, int failCount);
    void progressChanged(int completed, int total);

private:
    mutable QMutex m_mutex;
    QWaitCondition m_condition;
    QQueue<WriteRequest> m_queue;
    bool m_finishing = false;
    bool m_stopped = false;
    int m_totalQueued = 0;
};

/**
 * @brief Asynchronous file writer that runs in a background thread.
 *
 * This class allows file writes to happen off the main thread, preventing
 * UI freezes during bulk write operations like sync. Files are written
 * in a dedicated worker thread, with completion signals emitted back to
 * the main thread.
 *
 * Usage:
 *   AsyncFileWriter *writer = new AsyncFileWriter(this);
 *   connect(writer, &AsyncFileWriter::writeCompleted, this, &MyClass::onWriteComplete);
 *   connect(writer, &AsyncFileWriter::allWritesCompleted, this, &MyClass::onAllComplete);
 *   writer->start();
 *   writer->queueWrite(filePath, data);
 *   // ... more writes
 *   writer->finishWrites(); // Signal that no more writes will be queued
 */
class AsyncFileWriter : public QObject
{
    Q_OBJECT

public:
    explicit AsyncFileWriter(QObject *parent = nullptr);
    ~AsyncFileWriter() override;

    /**
     * @brief Start the worker thread.
     * Must be called before queueWrite().
     */
    void start();

    /**
     * @brief Queue a file write operation with pre-serialized data.
     * @param filePath Full path to write to
     * @param data Content to write
     * @param identifier Optional identifier for tracking (e.g., UID)
     */
    void queueWrite(const QString &filePath, const QByteArray &data,
                    const QString &identifier = QString());

    /**
     * @brief Queue an incidence for serialization and writing.
     * Serialization happens in the worker thread, not the main thread.
     * @param filePath Full path to write to
     * @param incidence The incidence to serialize and write
     * @param identifier Optional identifier for tracking (e.g., UID)
     */
    void queueIncidenceWrite(const QString &filePath,
                              const KCalendarCore::Incidence::Ptr &incidence,
                              const QString &identifier = QString());

    /**
     * @brief Signal that no more writes will be queued.
     * The worker will finish pending writes and emit allWritesCompleted.
     */
    void finishWrites();

    /**
     * @brief Stop the worker thread and wait for it to finish.
     */
    void stop();

    /**
     * @brief Check if the worker is currently running.
     */
    bool isRunning() const;

    /**
     * @brief Get number of pending writes in the queue.
     */
    int pendingCount() const;

signals:
    /**
     * @brief Emitted when a single file write completes.
     * @param filePath Path that was written
     * @param identifier The identifier passed to queueWrite
     * @param success True if write succeeded
     * @param errorMessage Error details if failed
     */
    void writeCompleted(const QString &filePath, const QString &identifier,
                        bool success, const QString &errorMessage);

    /**
     * @brief Emitted when all queued writes are complete and finishWrites() was called.
     * @param successCount Number of successful writes
     * @param failCount Number of failed writes
     */
    void allWritesCompleted(int successCount, int failCount);

    /**
     * @brief Emitted to report progress during batch writes.
     * @param completed Number of writes completed so far
     * @param total Total number of writes queued
     */
    void progressChanged(int completed, int total);

private:
    QThread m_workerThread;
    AsyncFileWriterWorker *m_worker = nullptr;
};

} // namespace Kalburator::Sync

#endif // ASYNCFILEWRITER_H
