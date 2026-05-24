#ifndef MOCKBACKEND_H
#define MOCKBACKEND_H

#include "syncbackend.h"
#include <optional>
#include <QHash>
#include <QList>
#include <QPair>
#include <QSemaphore>
#include <QStringList>
#include <KCalendarCore/Incidence>
#include <KCalendarCore/MemoryCalendar>

namespace Kalburator::Sync {

/**
 * @brief In-memory backend for fast automated testing.
 *
 * MockBackend provides:
 * - In-memory storage (no disk I/O)
 * - Failure injection for error path testing
 * - Operation logging for verification
 * - Latency injection for race condition testing
 * - Deterministic mode for reproducible tests
 *
 * Usage:
 * @code
 * MockBackend backend;
 * backend.setOperationDelay(100);  // 100ms delay per operation
 * backend.setFailurePoint(MockBackend::FailurePoint::OnPush, 3);  // Fail 3rd push
 *
 * // Use like any other backend
 * backend.loadCalendars("collection1");
 * @endcode
 */
class MockBackend : public SyncBackend
{
    Q_OBJECT

public:
    explicit MockBackend(QObject *parent = nullptr);
    explicit MockBackend(const QString &backendId, QObject *parent = nullptr);
    ~MockBackend() override = default;

    // =========================================================================
    // SyncBackend Interface
    // =========================================================================

    static const QString BackendTypeName;
    QString backendType() const override;
    QList<Kalburator::Shape::Shape> nativeShapes() const override;

    /// Override the shape reported by shapeFor() / nativeShapes().
    /// Default: {calendar, ical}. Tests can call this to route the
    /// engine through a different canon-to-peer pipeline (e.g. org-ical).
    void setShape(const Kalburator::Shape::Shape &shape) { m_shape = shape; }

    void loadCalendars(const QString &collectionId) override;

    void storeCalendars(const QString &collectionId,
                        const QList<KCalendarCore::MemoryCalendar*> &calendars) override;

    void startSync(const QString &collectionId,
                   KCalendarCore::MemoryCalendar* calendar,
                   const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
                   const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
                   const QMap<QString, QString> &stagedDeletions,
                   const TranscodingPlan& plan = TranscodingPlan{}) override;

    void removeItem(const QString &calId, const QString &itemUid) override;

    // Operation-based API
    FetchOperation* fetchItems(const QString &calendarId) override;

    PushOperation* pushItems(const QString &calendarId,
                             const QList<KCalendarCore::Incidence::Ptr> &items,
                             const TranscodingPlan &plan) override;

    DeleteOperation* deleteItems(const QString &calendarId,
                                  const QStringList &uids) override;

    // Calendar management
    bool supportsCalendarCreation() const override { return true; }
    bool createCalendar(const QString &collectionId,
                        const QString &calendarId,
                        const QString &name,
                        CalendarType type = CalendarType::Hybrid) override;
    bool deleteCalendar(const QString &collectionId, const QString &calendarId) override;

    // =========================================================================
    // Test Configuration
    // =========================================================================

    /**
     * @brief Points where failures can be injected.
     */
    enum class FailurePoint {
        None,
        OnLoadCalendars,
        OnLoadItems,
        OnStoreItems,
        OnPush,
        OnDelete,
        OnFetch,
        OnCreateCalendar,
        OnDeleteCalendar,
        OnStartSync
    };

    /**
     * @brief Set a failure injection point.
     *
     * @param point Which operation type should fail
     * @param afterNOperations Fail after this many successful operations (0 = fail immediately)
     * @param errorMessage Error message to return
     */
    void setFailurePoint(FailurePoint point,
                         int afterNOperations = 0,
                         const QString &errorMessage = QString());

    /**
     * @brief Clear all failure injection.
     */
    void clearFailurePoint();

    /**
     * @brief Set operation delay for race condition testing.
     *
     * @param milliseconds Delay before completing each operation
     */
    void setOperationDelay(int milliseconds) { m_operationDelayMs = milliseconds; }

    /**
     * @brief Enable deterministic mode.
     *
     * In deterministic mode:
     * - Timestamps are fixed
     * - UIDs are predictable
     * - No randomness
     */
    void setDeterministicMode(bool enabled) { m_deterministicMode = enabled; }

    // =========================================================================
    // F2 Task 22 — Blockable fetch / push (test fixture)
    // =========================================================================

    /// F2 Task 22 test fixture: when set true, fetchItems() returns
    /// a FetchOperation that blocks on m_fetchBlocker until
    /// releaseFetchBlocker() is called. Use to deterministically
    /// cancel a fetch mid-flight in cancellation tests (C2, C3).
    /// Default false; existing tests are unaffected.
    void setFetchBlocking(bool blocking) { m_fetchBlocking = blocking; }

    /// F2 Task 22 test fixture: wake any fetch operation currently
    /// blocked on m_fetchBlocker. Idempotent (releasing when no
    /// blocker is waiting just bumps the semaphore counter; the
    /// next blocking fetch consumes it immediately). Tests that
    /// use this should pair each setFetchBlocking(true) + start
    /// with exactly one releaseFetchBlocker() at the end.
    void releaseFetchBlocker() { m_fetchBlocker.release(); }

    /// F2 Task 22 test fixture: same shape for pushItems. Used by
    /// C3 (cancel during apply) which needs a PushOperation that
    /// hangs until the test releases it.
    void setPushBlocking(bool blocking) { m_pushBlocking = blocking; }
    void releasePushBlocker() { m_pushBlocker.release(); }

    // =========================================================================
    // State Inspection (for test verification)
    // =========================================================================

    /**
     * @brief Get all calendar IDs.
     */
    QStringList calendarIds() const;

    /**
     * @brief Get all UIDs in a calendar.
     */
    QStringList allUids(const QString &calendarId) const;

    /**
     * @brief Get an incidence by UID.
     */
    KCalendarCore::Incidence::Ptr incidence(const QString &calendarId,
                                             const QString &uid) const;

    /**
     * @brief Get hash of an incidence for change detection.
     */
    QString incidenceHash(const QString &calendarId, const QString &uid) const;

    /**
     * @brief Get total operation count.
     */
    int operationCount() const { return m_operationLog.size(); }

    /**
     * @brief Get operation log for verification.
     *
     * Format: "OPERATION:calendarId:uid" or "OPERATION:calendarId"
     * Examples: "LOAD:work", "PUSH:work:event-123", "DELETE:personal:todo-456"
     */
    QStringList operationLog() const { return m_operationLog; }

    /**
     * @brief Clear operation log.
     */
    void clearOperationLog() { m_operationLog.clear(); }

    /**
     * @brief Get all calendars with their items.
     */
    QHash<QString, QHash<QString, KCalendarCore::Incidence::Ptr>> allData() const
    {
        return m_calendars;
    }

    /**
     * @brief Directly set calendar data (for test setup).
     */
    void setCalendarData(const QString &calendarId,
                         const QList<KCalendarCore::Incidence::Ptr> &items);

    /**
     * @brief Add a single incidence directly (for test setup).
     */
    void addIncidence(const QString &calendarId,
                      const KCalendarCore::Incidence::Ptr &incidence);

    /**
     * @brief Clear all data.
     */
    void clearAllData();

    // =========================================================================
    // IBlobBackend Overrides
    // =========================================================================

    // Identity
    QString backendId() const override   { return m_backendId; }
    QString displayName() const override { return m_backendId; }
    bool    isAvailable() const override { return true; }

    // Collections
    QList<CollectionInfo> availableCollections() override;
    CollectionInfo        collectionInfo(const QString &collectionId) override;
    QString               createCollection(const CollectionInfo &info) override;

    // Records
    QList<BackendRecord>          loadRecords(const QString &collectionId) override;
    bool                          loadRecordsOrError(const QString &collectionId,
                                                     QList<BackendRecord> &records,
                                                     QString &error) override;
    std::optional<BackendRecord>  loadRecord(const QString &recordId) override;
    QString                       createRecord(const QString &collectionId,
                                               const BackendRecord &record) override;
    bool                          updateRecord(const BackendRecord &record) override;
    bool                          deleteRecord(const QString &recordId) override;

    // Change detection
    QList<BackendRecord> modifiedSince(const QString &collectionId,
                                       const QDateTime &since) override;
    QStringList          deletedSince(const QString &collectionId,
                                      const QDateTime &since) override;
    bool supportsDeleteTracking() const override { return true; }

    // Batch (no-op — in-memory operations are atomic)
    void beginBatch()  override {}
    bool commitBatch() override { return true; }
    void rollbackBatch() override {}
    bool supportsBatch() const override { return false; }

private:
    void logOperation(const QString &operation,
                      const QString &calendarId,
                      const QString &uid = QString());
    bool shouldFail(FailurePoint point);
    void applyDelay();
    QString computeHash(const KCalendarCore::Incidence::Ptr &incidence) const;

    // Tracks which UIDs were deleted (per calendarId), for deletedSince()
    // Key: calendarId, Value: list of (uid, deletionTime) pairs
    QHash<QString, QList<QPair<QString, QDateTime>>> m_deletionLog;

    // Backend identity (set at construction, used for backendId())
    QString m_backendId;

    // Configurable shape (set via setShape(); default {calendar, ical}).
    Kalburator::Shape::Shape m_shape{
        Kalburator::Shape::DomainId{QString::fromLatin1("calendar")},
        Kalburator::Shape::EncodingId{QString::fromLatin1("ical")} };

    // In-memory storage: calendarId -> (uid -> incidence)
    QHash<QString, QHash<QString, KCalendarCore::Incidence::Ptr>> m_calendars;

    // Calendar names
    QHash<QString, QString> m_calendarNames;

    // Operation logging
    QStringList m_operationLog;

    // Failure injection
    FailurePoint m_failurePoint = FailurePoint::None;
    int m_failAfterN = 0;
    int m_operationCountForFailure = 0;
    QString m_failureMessage;

    // Timing
    int m_operationDelayMs = 0;

    // Determinism
    bool m_deterministicMode = false;

    // F2 Task 22: blockable fetch / push test fixture state. Default
    // off; the worker threads spawned in fetchItems()/pushItems()
    // when these flags are true acquire the corresponding semaphore
    // before completing, letting tests cancel mid-flight.
    bool m_fetchBlocking = false;
    bool m_pushBlocking  = false;
    QSemaphore m_fetchBlocker;
    QSemaphore m_pushBlocker;
};

} // namespace Kalburator::Sync

#endif // MOCKBACKEND_H
