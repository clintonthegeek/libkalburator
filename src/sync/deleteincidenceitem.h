#ifndef DELETEINCIDENCEITEM_H
#define DELETEINCIDENCEITEM_H

#include "synctransactionitem.h"
#include <KCalendarCore/Incidence>

class SyncBackend;
class FetchOperation;

/**
 * @brief Transaction item for deleting an incidence.
 *
 * Simulation checks that the incidence exists.
 * Commit deletes the incidence from the backend.
 * Rollback recreates the deleted incidence.
 */
class DeleteIncidenceItem : public SyncTransactionItem
{
    Q_OBJECT

public:
    /**
     * @brief Construct a delete item.
     *
     * @param calendarId Calendar containing the incidence
     * @param uid UID of the incidence to delete
     * @param deletedIncidence The incidence being deleted (for rollback)
     * @param backend Backend to operate on
     * @param parent Parent QObject
     */
    DeleteIncidenceItem(const QString &calendarId,
                        const QString &uid,
                        KCalendarCore::Incidence::Ptr deletedIncidence,
                        SyncBackend *backend,
                        QObject *parent = nullptr);

    ~DeleteIncidenceItem() override;

    // SyncTransactionItem interface
    void simulate() override;
    bool commit() override;
    bool rollback() override;
    QString description() const override;
    QJsonObject toJson() const override;

    /**
     * @brief Get the incidence being deleted (saved for rollback).
     */
    KCalendarCore::Incidence::Ptr deletedIncidence() const { return m_deletedIncidence; }

private slots:
    void onFetchFinished();

private:
    KCalendarCore::Incidence::Ptr m_deletedIncidence;
    FetchOperation *m_fetchOp = nullptr;
};

#endif // DELETEINCIDENCEITEM_H
