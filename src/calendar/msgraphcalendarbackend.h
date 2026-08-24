#ifndef KALBURATOR_MSGRAPHCALENDARBACKEND_H
#define KALBURATOR_MSGRAPHCALENDARBACKEND_H

// EEE Phase 7.C — Microsoft Graph calendar backend on the transport
// foundation (`GraphApiClient`, Stage-D tested). v1 scope:
//   - ONE collection ("me-events"): the caller's default events folder,
//     walked with pagination; delta rides a later slice.
//   - Records carry RAW ms-event wire JSON in BackendRecord::data and the
//     backend declares nativeShapes = {{calendar, ms-event}} — the engine
//     promotes/demotes through the registered Phase 7.B edge itself. The
//     Incidence::Ptr legacy surface converts inside this backend via
//     ms-event → canon → iCal.
//   - Writes: POST/PATCH/DELETE against <collectionPath>[/{id}]. Per
//     FINDINGS O61(e) carriers do not survive creates on consumer
//     Outlook.com — updates MUST go out as PATCH (never delete+re-create),
//     which is what this class does structurally.
//   - Create responses mint server-side Graph ids → WriteOperation
//     idAliases bridge requested→stored ids (the O55 join machinery).

#include "syncbackend.h"
#include "syncoperation.h"

namespace Kalburator::Graph {
class GraphApiClient;
}

namespace Kalburator::Sync {

class MSGraphCalendarBackend : public SyncBackend
{
    Q_OBJECT
public:
    explicit MSGraphCalendarBackend(QObject *parent = nullptr);
    ~MSGraphCalendarBackend() override;

    /// Point the transport at an API root (live Graph or the Stage D mock).
    void setBaseUrl(const QString &baseUrl);
    void setAccessToken(const QString &token);
    /// Events-folder path; default "/me/events". Tests point it at any
    /// mock-server collection path.
    void setCollectionPath(const QString &path);

    // ==== identity ====
    QString backendType() const override;
    QList<Kalburator::Shape::Shape> nativeShapes() const override;

    // ==== read path ====
    FetchOperation *fetchItems(const QString &calendarId) override;
    bool recordsFromLastFetch(const QString &collectionId,
                              QList<BackendRecord> &records,
                              QString &errorMessage) override;
    QList<BackendRecord> loadRecords(const QString &collectionId) override;

    // ==== write path ====
    WriteOperation *applyRecords(const QString &collectionId,
                                 const WriterBatch &batch) override;

private:
    QList<KCalendarCore::Incidence::Ptr> incidencesForRecord(
        const QByteArray &wireJson) const;

    /// Heap-owned sequential-apply state: async continuations outlive the
    /// enqueueOperation functor frame, so ALL mutable walk state lives here.
    struct ApplyState;
    void applyStep(std::shared_ptr<ApplyState> st);

    Kalburator::Graph::GraphApiClient *m_client;
    QString m_collectionPath = QStringLiteral("/me/events");
    // Last successful fetch's memo, served once by recordsFromLastFetch()
    // (H5/O23 contract) and by loadRecords() until superseded.
    QHash<QString, QList<BackendRecord>> m_lastFetchRecords;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_MSGRAPHCALENDARBACKEND_H
