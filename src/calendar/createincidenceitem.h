#ifndef CREATEINCIDENCEITEM_H
#define CREATEINCIDENCEITEM_H

#include "synctransactionitem.h"
#include "transcodingplan.h"
#include <KCalendarCore/Incidence>
#include <KCalendarCore/MemoryCalendar>

namespace Kalburator::Sync {

class SyncBackend;
class FetchOperation;

/**
 * @brief Transaction item for creating a new incidence.
 *
 * Simulation checks that no incidence with the same UID exists.
 * Commit calls backend->pushItems() with the transcoding plan so the
 * backend applies transcoding and emits transcodingWarning if needed,
 * and observes the returned PushOperation handle to detect failure.
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
     * @param calendar MemoryCalendar used by the backend (must outlive commit)
     * @param backend Backend to operate on
     * @param plan Transcoding plan for write; empty plan is a no-op
     * @param parent Parent QObject
     */
    CreateIncidenceItem(const QString &calendarId,
                        KCalendarCore::Incidence::Ptr incidence,
                        KCalendarCore::MemoryCalendar *calendar,
                        SyncBackend *backend,
                        const TranscodingPlan &plan = TranscodingPlan{},
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
    KCalendarCore::MemoryCalendar *m_calendar = nullptr;
    TranscodingPlan m_plan;
    FetchOperation *m_fetchOp = nullptr;
};

} // namespace Kalburator::Sync

#endif // CREATEINCIDENCEITEM_H
