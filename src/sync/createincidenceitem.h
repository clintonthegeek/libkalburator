#ifndef CREATEINCIDENCEITEM_H
#define CREATEINCIDENCEITEM_H

#include "synctransactionitem.h"
#include <KCalendarCore/Incidence>

class SyncBackend;
class FetchOperation;

/**
 * @brief Transaction item for creating a new incidence.
 *
 * Simulation checks that no incidence with the same UID exists.
 * Commit pushes the incidence to the backend.
 * Rollback deletes the created incidence.
 */
class CreateIncidenceItem : public SyncTransactionItem
{
    Q_OBJECT

public:
    /**
     * @brief Construct a create item.
     *
     * @param calendarId Calendar to create the incidence in
     * @param incidence The incidence to create
     * @param backend Backend to operate on
     * @param parent Parent QObject
     */
    CreateIncidenceItem(const QString &calendarId,
                        KCalendarCore::Incidence::Ptr incidence,
                        SyncBackend *backend,
                        QObject *parent = nullptr);

    ~CreateIncidenceItem() override;

    // SyncTransactionItem interface
    void simulate() override;
    bool commit() override;
    bool rollback() override;
    QString description() const override;
    QJsonObject toJson() const override;

    /**
     * @brief Get the incidence being created.
     */
    KCalendarCore::Incidence::Ptr incidence() const { return m_incidence; }

private slots:
    void onFetchFinished();

private:
    KCalendarCore::Incidence::Ptr m_incidence;
    FetchOperation *m_fetchOp = nullptr;
};

#endif // CREATEINCIDENCEITEM_H
