#ifndef KALBURATOR_GOOGLECALENDARBACKEND_H
#define KALBURATOR_GOOGLECALENDARBACKEND_H

// B2C P1 — Google Calendar API v3 backend on the transport foundation
// (`GoogleApiClient`, mock-tested). v1 scope:
//   - calendarList discovery (/users/me/calendarList); events per calendar
//     via /calendars/<id>/events with syncToken incremental walks
//     (nextSyncToken persisted per collection; HTTP 410 Gone self-heals to
//     a fresh initial listing, O42 pattern). Every walk reports the FULL
//     merged set — engine diffs expect whole-collection views.
//   - Deletions surface as status:"cancelled" items on incremental listings
//     (Google has no @removed annotation) — tombstoned from the cache.
//   - Records carry RAW google-event wire JSON in BackendRecord::data;
//     nativeShapes = {{calendar, google-event}} (the Phase-2 edge owns all
//     mapping). Incidence::Ptr legacy surface converts via
//     google-event → canon → iCal.
//   - Writes (O67 rules): create POST strips read-only top-level
//     created/updated (Google rejects them with 400) while keeping the
//     client iCalUID anchor (honored server-side); updates are PATCH in
//     place, never delete+re-create; deletes accept 200/204 and treat
//     410 as success (idempotent delete semantics).
//   - Create responses mint server-side transport ids → WriteOperation
//     idAliases bridge requested→stored ids (O55 machinery).

#include "syncbackend.h"
#include "syncoperation.h"

namespace Kalburator::Google {
class GoogleApiClient;
}

namespace Kalburator::Sync {

class GoogleCalendarBackend : public SyncBackend
{
    Q_OBJECT
public:
    explicit GoogleCalendarBackend(QObject *parent = nullptr);
    ~GoogleCalendarBackend() override;

    /// Point the transport at an API root (live googleapis or the mock).
    /// Default https://www.googleapis.com/calendar/v3.
    void setBaseUrl(const QString &baseUrl);
    void setAccessToken(const QString &token);
    /// Persist sync tokens + merged record caches under this directory
    /// (JSON, atomic replace). Unset ⇒ in-memory only.
    void setCacheDir(const QString &dir);

    // ==== discovery ====
    void loadCalendars(const QString &collectionId) override;
    QList<Kalburator::Sync::CollectionInfo> availableCollections() override;
    Kalburator::Sync::DiscoveredCalendar discoveredCalendar(
        const QString &calendarId) const override;

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

    /// Strip O67-rejected read-only fields from an authored wire object.
    static QByteArray stripReadOnlyFields(const QByteArray &wireJson);

    /// Heap-owned sequential-apply state (O62): async continuations outlive
    /// the enqueueOperation functor frame.
    struct ApplyState;
    void applyStep(std::shared_ptr<ApplyState> st);

    /// Heap-owned sync-walk state for one fetchItems() pass.
    struct FetchState;
    void startSyncFetch(std::shared_ptr<FetchState> st);
    void finishFetch(std::shared_ptr<FetchState> st);

    Kalburator::Google::GoogleApiClient *m_client;
    QString m_lastFetchMemoOwner;   // single memo slot keyed by collection

    QHash<QString, QList<BackendRecord>> m_lastFetchRecords;

    // Sync machinery: merged collection view + per-collection resume token.
    QHash<QString, QHash<QString, BackendRecord>> m_cache;
    QHash<QString, QString> m_syncTokens;

    // Discovery metadata (calendarList wire objects keyed by calendar id).
    struct CalMeta {
        QString name;
        QString colorHex;
        bool canEdit = true;
        bool isDefault = false;
    };
    QHash<QString, CalMeta> m_calendars;

    QString m_cacheDir;
    bool m_persistenceLoaded = false;
    void ensurePersistedStateLoaded();
    void persistState() const;
    QString eventsPathForCalendar(const QString &calendarId) const;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_GOOGLECALENDARBACKEND_H
