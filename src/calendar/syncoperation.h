#ifndef SYNCOPERATION_H
#define SYNCOPERATION_H

#include <QObject>
#include <QString>
#include <QList>
#include <KCalendarCore/Incidence>
#include <atomic>

#include "../sync/syncoperation.h"

namespace Kalburator::Sync {

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
