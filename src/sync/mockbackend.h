#ifndef MOCKBACKEND_H
#define MOCKBACKEND_H

#include "syncbackend.h"
#include <QHash>
#include <QStringList>
#include <KCalendarCore/Incidence>
#include <KCalendarCore/MemoryCalendar>

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
    ~MockBackend() override = default;

    // =========================================================================
    // SyncBackend Interface
    // =========================================================================

    static const QString BackendTypeName;
    QString backendType() const override;

    void loadCalendars(const QString &collectionId) override;

    void loadItems(KCalendarCore::MemoryCalendar* cal, bool suppressSignals = false) override;

    void storeCalendars(const QString &collectionId,
                        const QList<KCalendarCore::MemoryCalendar*> &calendars) override;

    void storeItems(KCalendarCore::MemoryCalendar* cal,
                    const QList<KCalendarCore::Incidence::Ptr> &items) override;

    void updateItem(KCalendarCore::MemoryCalendar* cal,
                    const KCalendarCore::Incidence::Ptr &item,
                    const QString &icalData) override;

    void startSync(const QString &collectionId,
                   KCalendarCore::MemoryCalendar* calendar,
                   const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
                   const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
                   const QMap<QString, QString> &stagedDeletions) override;

    void removeItem(const QString &calId, const QString &itemUid) override;

    // Operation-based API
    FetchOperation* fetchItems(const QString &calendarId) override;
    PushOperation* pushItems(const QString &calendarId,
                              const QList<KCalendarCore::Incidence::Ptr> &items) override;
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

private:
    void logOperation(const QString &operation,
                      const QString &calendarId,
                      const QString &uid = QString());
    bool shouldFail(FailurePoint point);
    void applyDelay();
    QString computeHash(const KCalendarCore::Incidence::Ptr &incidence) const;

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
};

#endif // MOCKBACKEND_H
