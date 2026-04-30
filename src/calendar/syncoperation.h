#ifndef SYNCOPERATION_H
#define SYNCOPERATION_H

#include <QObject>
#include <QString>
#include <QList>
#include <KCalendarCore/Incidence>
#include <atomic>

namespace Kalburator::Sync {

/**
 * @brief Base class for trackable async sync operations.
 *
 * SyncOperation provides a uniform interface for tracking async operations
 * across different backends (LocalBackend, RemoteBackend, OrgBackend, etc.).
 * Each backend wraps its native async mechanism (KJob, file I/O, etc.) in
 * a SyncOperation subclass.
 *
 * Key design principles:
 * - Operations work with calendar IDs, not raw pointers
 * - Operations are trackable (state, progress, cancellation)
 * - Incidence::Ptr (QSharedPointer) is safe to pass through
 * - Calendar lookup happens only when applying results
 *
 * Usage:
 * @code
 * SyncOperation *op = backend->fetchItems("my-calendar");
 * connect(op, &SyncOperation::finished, this, [op]() {
 *     if (op->state() == SyncOperation::Succeeded) {
 *         auto *fetchOp = qobject_cast<FetchOperation*>(op);
 *         for (const auto &inc : fetchOp->fetchedItems()) {
 *             // Apply to calendar...
 *         }
 *     }
 *     op->deleteLater();
 * });
 * @endcode
 */
class SyncOperation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)

public:
    enum State {
        Pending,    ///< Operation created but not started
        Running,    ///< Operation in progress
        Succeeded,  ///< Operation completed successfully
        Failed,     ///< Operation failed with error
        Cancelled   ///< Operation was cancelled
    };
    Q_ENUM(State)

    explicit SyncOperation(const QString &calendarId, QObject *parent = nullptr);
    ~SyncOperation() override;

    /**
     * @brief Unique identifier for this operation.
     */
    QString operationId() const { return m_operationId; }

    /**
     * @brief Calendar ID this operation targets.
     *
     * NOT a pointer - the calendar is looked up by ID when results are applied.
     */
    QString calendarId() const { return m_calendarId; }

    /**
     * @brief Current state of the operation.
     */
    State state() const noexcept { return m_state.load(std::memory_order_acquire); }

    /**
     * @brief Progress percentage (0-100), or -1 if indeterminate.
     */
    int progress() const { return m_progress; }

    /**
     * @brief Error string if state is Failed.
     */
    QString errorString() const { return m_errorString; }

    /**
     * @brief Whether the operation has completed (success, failure, or cancelled).
     */
    bool isFinished() const noexcept;

    /**
     * @brief Request cancellation of this operation.
     *
     * The default implementation sets the atomic m_cancelRequested flag and
     * (for backward compatibility with existing backend run-bodies that
     * check op->state() == Cancelled) transitions to Cancelled if not yet
     * finished. Subclasses may override (e.g. to call QNetworkReply::abort())
     * but should still set the flag. Idempotent.
     */
    virtual void cancel();

    /**
     * @brief Mark operation as failed with error message.
     *
     * Can be called by backend implementations or external code to mark
     * an operation as failed. This is public because backends may need to
     * fail operations in various contexts.
     */
    void fail(const QString &errorString);

    /**
     * @brief Set operation state. Emits stateChanged and finished as appropriate.
     *
     * Called by backends to transition operation state (e.g., Pending -> Running).
     */
    void setState(State newState);

    /**
     * @brief Set progress percentage.
     */
    void setProgress(int percent);

    /**
     * @brief Mark operation as successfully completed.
     *
     * Called by backends when async work finishes successfully.
     */
    void complete();

signals:
    /**
     * @brief Emitted exactly once when the operation transitions Pending -> Running.
     *
     * Part of the F2 standardised SyncOperation contract used by the
     * await<Op>() helper.
     */
    void started();

    /**
     * @brief Emitted when operation state changes.
     */
    void stateChanged(SyncOperation::State newState);

    /**
     * @brief Emitted when progress updates.
     */
    void progressChanged(int percent);

    /**
     * @brief Emitted when operation completes (any terminal state).
     *
     * Emitted exactly once, regardless of which terminal state was reached.
     * Check state() to determine success/failure/cancellation.
     */
    void finished();

protected:
    /**
     * @brief Set error string (call before setState(Failed)).
     */
    void setErrorString(const QString &error);

    /**
     * @brief Set the error string and transition to Failed in one step.
     *
     * Equivalent to setErrorString(message) followed by setState(Failed).
     * Part of the F2 standardised SyncOperation contract.
     */
    void setError(const QString &message);

    /**
     * @brief Whether cancel() has been called on this operation.
     *
     * run() bodies and the F2 await<Op>() helper poll this between work
     * units to observe cancellation requests.
     */
    bool cancelRequested() const noexcept;

    /**
     * @brief Mark operation as started (Pending -> Running).
     */
    void start();

private:
    QString m_operationId;
    QString m_calendarId;
    std::atomic<State> m_state{Pending};
    std::atomic<bool> m_cancelRequested{false};
    int m_progress = -1;
    QString m_errorString;

    static int s_nextOperationId;
};

/**
 * @brief Operation that fetches items from a backend.
 *
 * Results are available via fetchedItems() after operation succeeds.
 */
class FetchOperation : public SyncOperation
{
    Q_OBJECT

public:
    explicit FetchOperation(const QString &calendarId, QObject *parent = nullptr);

    /**
     * @brief Items fetched from the backend.
     *
     * Only valid after state() == Succeeded.
     */
    QList<KCalendarCore::Incidence::Ptr> fetchedItems() const { return m_fetchedItems; }

    /**
     * @brief Set fetched items (call before complete()).
     */
    void setFetchedItems(const QList<KCalendarCore::Incidence::Ptr> &items);

private:
    QList<KCalendarCore::Incidence::Ptr> m_fetchedItems;
};

/**
 * @brief Operation that pushes items to a backend.
 *
 * Tracks which items succeeded and which failed.
 */
class PushOperation : public SyncOperation
{
    Q_OBJECT

public:
    explicit PushOperation(const QString &calendarId,
                          const QList<KCalendarCore::Incidence::Ptr> &items,
                          QObject *parent = nullptr);

    /**
     * @brief Items that were requested to be pushed.
     */
    QList<KCalendarCore::Incidence::Ptr> requestedItems() const { return m_requestedItems; }

    /**
     * @brief UIDs of items that were successfully pushed.
     */
    QStringList succeededUids() const { return m_succeededUids; }

    /**
     * @brief UIDs of items that failed to push.
     */
    QStringList failedUids() const { return m_failedUids; }

    // Modification methods (called by backends)
    void addSucceededUid(const QString &uid);
    void addFailedUid(const QString &uid);
    void setSucceededUids(const QStringList &uids);
    void setFailedUids(const QStringList &uids);

private:
    QList<KCalendarCore::Incidence::Ptr> m_requestedItems;
    QStringList m_succeededUids;
    QStringList m_failedUids;
};

/**
 * @brief Operation that deletes items from a backend.
 */
class DeleteOperation : public SyncOperation
{
    Q_OBJECT

public:
    explicit DeleteOperation(const QString &calendarId,
                            const QStringList &uids,
                            QObject *parent = nullptr);

    /**
     * @brief UIDs that were requested for deletion.
     */
    QStringList requestedUids() const { return m_requestedUids; }

    /**
     * @brief UIDs that were successfully deleted.
     */
    QStringList succeededUids() const { return m_succeededUids; }

    /**
     * @brief UIDs that failed to delete.
     */
    QStringList failedUids() const { return m_failedUids; }

    // Modification methods (called by backends)
    void addSucceededUid(const QString &uid);
    void addFailedUid(const QString &uid);
    void setSucceededUids(const QStringList &uids);
    void setFailedUids(const QStringList &uids);

private:
    QStringList m_requestedUids;
    QStringList m_succeededUids;
    QStringList m_failedUids;
};

} // namespace Kalburator::Sync

#endif // SYNCOPERATION_H
