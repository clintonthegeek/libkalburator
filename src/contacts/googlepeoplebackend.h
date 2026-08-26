#ifndef KALBURATOR_CONTACTS_GOOGLEPEOPLEBACKEND_H
#define KALBURATOR_CONTACTS_GOOGLEPEOPLEBACKEND_H

// B2C P2.d — Google People API v1 contacts backend on the transport
// foundation (`GoogleApiClient`, P1/P2.b mock-tested). Inherits
// SyncBackendBase directly (contacts domain); mirrors the P2.c
// GraphContactsBackend structure.
//
// Wire truths pinned by the P2 design pass and vendor-rest-api-wire-notes:
//   - ONE implicit collection ("connections"; People exposes a single
//     connections list per account — no folder discovery in v1 scope).
//   - READS walk /v1/people/me/connections?pageSize=&personFields=&
//     requestSyncToken=true, projecting every field the promote stage
//     reads (single shared constant). Incremental walks present the
//     stored token as `sync_token`; HTTP 410 Gone self-heals with ONE
//     fresh initial walk (O42 pattern). Every completed walk commits and
//     reports the FULL merged set — engine diffs expect whole views.
//   - Record id = `resourceName` verbatim ("people/c123").
//   - WRITES: create = POST /v1/people/me:createContact with etag/
//     metadata/nextSyncToken stripped (clientData carriers RIDE INLINE —
//     live-Reversible channel per the O66 verdict table; unlike Graph
//     contacts there is no nav-POST channel here); update = PATCH
//     /v1/people/{id}:updateContact?updatePersonFields=<top-level keys of
//     the body> (merge-in-place); delete accepts 200/204 and treats 404
//     as already-gone ⇒ success (idempotent; People deletes are not
//     flaky, so no confirming re-list — deliberate deviation from the
//     Graph pin).
//   - Create responses mint server-side resourceNames ("people/c<N>") →
//     WriteOperation::addIdAlias bridges requested→stored (O55 machinery).
//   - Defensive union-merge (O69 lesson): a served projection lacking
//     keys a cached copy has is enriched FROM the cache instead of
//     clobbering it.

#include "syncbackendbase.h"
#include "writeoperation.h"
#include "writerbatch.h"

#include <QHash>
#include <QString>

namespace Kalburator::Google {
class GoogleApiClient;
}

namespace Kalburator::Sync {

class GooglePeopleBackend : public SyncBackendBase
{
    Q_OBJECT
public:
    explicit GooglePeopleBackend(QObject *parent = nullptr);
    ~GooglePeopleBackend() override;

    /// Point the transport at an API root (live people.googleapis.com or
    /// the mock).
    void setBaseUrl(const QString &baseUrl);
    void setAccessToken(const QString &token);
    /// Persist the merged cache + sync token under this directory (JSON,
    /// atomic replace; file google-people-state.json, keyed to the EXACT
    /// listing query template — a template change ⇒ documented full
    /// resync). Unset ⇒ in-memory only.
    void setCacheDir(const QString &dir);

    /// The single implicit People connections collection.
    static QString defaultCollectionId();

    // ==== identity ====
    QString backendType() const override;
    QList<Kalburator::Shape::Shape> nativeShapes() const override;

    // ==== discovery ====
    QList<Kalburator::Sync::CollectionInfo> availableCollections() override;

    // ==== read path ====
    SyncOperation *fetchItems(const QString &collectionId) override;
    bool recordsFromLastFetch(const QString &collectionId,
                              QList<BackendRecord> &records,
                              QString &errorMessage) override;
    QList<BackendRecord> loadRecords(const QString &collectionId) override;

    // ==== write path ====
    WriteOperation *applyRecords(const QString &collectionId,
                                 const WriterBatch &batch) override;

private:
    static QByteArray stripNonCreatableFields(const QByteArray &wireJson);

    /// Heap-owned page-walk state for one fetchItems() pass (O62: async
    /// continuations outlive the enqueueOperation functor frame). The
    /// People listing speaks `connections[]` rather than the calendar
    /// transport's `items[]`, so the walk lives here over rawRequest.
    struct FetchState;
    void startConnectionsWalk(std::shared_ptr<FetchState> st);
    void finishFetch(std::shared_ptr<FetchState> st);

    /// Heap-owned sequential-apply state (O62).
    struct ApplyState;
    void applyStep(std::shared_ptr<ApplyState> st);

    Kalburator::Google::GoogleApiClient *m_client;

    // Last successful fetch's memo, served once by recordsFromLastFetch()
    // (H5/O23 contract); loadRecords() serves it without consuming.
    QHash<QString, QList<BackendRecord>> m_lastFetchRecords;

    // Sync machinery: merged collection view + per-collection resume token.
    QHash<QString, QHash<QString, BackendRecord>> m_cache;
    QHash<QString, QString> m_syncTokens;

    QString m_cacheDir;
    bool m_persistenceLoaded = false;
    void ensurePersistedStateLoaded();
    void persistState() const;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_CONTACTS_GOOGLEPEOPLEBACKEND_H
