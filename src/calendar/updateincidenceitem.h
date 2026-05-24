#ifndef UPDATEINCIDENCEITEM_H
#define UPDATEINCIDENCEITEM_H

#include "synctransactionitem.h"
#include <KCalendarCore/Incidence>
#include <KCalendarCore/MemoryCalendar>

namespace Kalburator::Sync {

class SyncBackend;
class FetchOperation;

/**
 * @brief Transaction item for updating an existing incidence.
 *
 * Simulation checks that the incidence exists and hasn't been modified
 * concurrently (if version hash is provided).
 * Commit pushes the new version to the backend.
 * Rollback restores the old version.
 */
class UpdateIncidenceItem : public SyncTransactionItem
{
    Q_OBJECT

public:
    /**
     * @brief Construct an update item.
     *
     * @param calendarId Calendar containing the incidence
     * @param oldIncidence The original incidence (for rollback)
     * @param newIncidence The updated incidence to commit
     * @param calendar MemoryCalendar used by the backend (must outlive commit)
     * @param backend Backend to operate on
     * @param parent Parent QObject
     */
    UpdateIncidenceItem(const QString &calendarId,
                        KCalendarCore::Incidence::Ptr oldIncidence,
                        KCalendarCore::Incidence::Ptr newIncidence,
                        KCalendarCore::MemoryCalendar *calendar,
                        SyncBackend *backend,
                        QObject *parent = nullptr);

    /**
     * @brief Construct an update item with expected version hash.
     *
     * If provided, simulation will verify the current version matches.
     */
    UpdateIncidenceItem(const QString &calendarId,
                        KCalendarCore::Incidence::Ptr oldIncidence,
                        KCalendarCore::Incidence::Ptr newIncidence,
                        const QString &expectedVersionHash,
                        KCalendarCore::MemoryCalendar *calendar,
                        SyncBackend *backend,
                        QObject *parent = nullptr);

    ~UpdateIncidenceItem() override;

    // SyncTransactionItem interface
    void simulate() override;
    bool commit() override;
    bool rollback() override;
    QString description() const override;
    QJsonObject toJson() const override;

    /**
     * @brief Get the old (original) incidence.
     */
    KCalendarCore::Incidence::Ptr oldIncidence() const { return m_oldIncidence; }

    /**
     * @brief Get the new (updated) incidence.
     */
    KCalendarCore::Incidence::Ptr newIncidence() const { return m_newIncidence; }

private slots:
    void onFetchFinished();

private:
    KCalendarCore::Incidence::Ptr m_oldIncidence;
    KCalendarCore::Incidence::Ptr m_newIncidence;
    QString m_expectedVersionHash;
    KCalendarCore::MemoryCalendar *m_calendar = nullptr;
    FetchOperation *m_fetchOp = nullptr;
};

} // namespace Kalburator::Sync

#endif // UPDATEINCIDENCEITEM_H
