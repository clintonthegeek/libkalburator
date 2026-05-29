#ifndef KALBURATOR_SYNC_SYNCOPERATION_H
#define KALBURATOR_SYNC_SYNCOPERATION_H

#include <QObject>
#include <QString>
#include <atomic>

namespace Kalburator::Sync {

/**
 * @brief Domain-neutral base for trackable async sync operations.
 *
 * Lifted out of calendar/syncoperation.h (architectural-redress Plan 3) so the
 * sync/ orchestration layer and engine depend on a base with ZERO KCalendarCore.
 * Calendar-typed subclasses (FetchOperation/PushOperation/DeleteOperation) live
 * in calendar/syncoperation.h and inherit this base.
 */
class SyncOperation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)

public:
    enum State { Pending, Running, Succeeded, Failed, Cancelled };
    Q_ENUM(State)

    explicit SyncOperation(const QString &calendarId, QObject *parent = nullptr);
    ~SyncOperation() override;

    QString operationId() const { return m_operationId; }
    QString calendarId() const { return m_calendarId; }
    State state() const noexcept { return m_state.load(std::memory_order_acquire); }
    int progress() const { return m_progress; }
    QString errorString() const { return m_errorString; }
    bool isFinished() const noexcept;

    virtual void cancel();
    void fail(const QString &errorString);
    void setState(State newState);
    void setProgress(int percent);
    void complete();

signals:
    void started();
    void stateChanged(SyncOperation::State newState);
    void progressChanged(int percent);
    void finished();

protected:
    void setErrorString(const QString &error);
    void setError(const QString &message);
    bool cancelRequested() const noexcept;
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

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNC_SYNCOPERATION_H
