#ifndef KALBURATOR_TODO_GOOGLETASKSBACKEND_H
#define KALBURATOR_TODO_GOOGLETASKSBACKEND_H

// B2C P3.c — Google Tasks API v1 todo backend on the transport foundation
// (`GoogleApiClient`, P3.b mock-tested). Inherits SyncBackendBase directly
// (todo domain); the P2 contacts backends are the structural template.
//
// Wire truths pinned by the P3 design pass and vendor-rest-api-wire-notes:
//   - Discovery walks /v1/users/me/lists; every taskList becomes an
//     available collection (v1: all writable). DiscoveredCalendar-style
//     surfacing (supportsVTodo=true, supportsVEvent=false) via
//     discoveredTaskList().
//   - The Tasks API has NO sync tokens: READS are FULL paged listings of
//     /v1/lists/{id}/tasks?showCompleted=true&showHidden=true&maxResults=100
//     EVERY fetch. Both visibility flags are mandatory — default listings
//     OMIT completed (status:"completed") and hidden/deleted ("deleted":true)
//     rows, and deleted rows must arrive so they can be tombstoned from the
//     cache. Every completed walk commits and reports the FULL merged set.
//   - Record id = server task id verbatim.
//   - WRITES: create = POST /v1/lists/{id}/tasks with top-level
//     created/updated/id STRIPPED (O68 family: Tasks insert rejects all
//     three); the server mints the transport id → WriteOperation::addIdAlias
//     bridges requested→stored (O55 machinery). Update = PATCH in place
//     under the existing id (partial bodies ride verbatim, matching the
//     GoogleCalendarBackend update convention). Delete accepts 200/204 and
//     treats 404 as already-gone ⇒ success (idempotent). No carrier channel
//     exists (O66(c)) — nothing extra to route.
//   - Defensive union-merge (O69 lesson): a served row lacking keys a cached
//     copy has is enriched FROM the cache instead of clobbering it.
//
// Persistence: no sync token exists to persist; only the merged record
// caches are written (<cacheDir>/google-tasks-state.json, atomic replace),
// exactly the GraphContactsBackend rationale.

#include "discoveredcalendar.h"
#include "syncbackendbase.h"
#include "writeoperation.h"
#include "writerbatch.h"

#include <QHash>
#include <QString>

namespace Kalburator::Google {
class GoogleApiClient;
}

namespace Kalburator::Sync {

class GoogleTasksBackend : public SyncBackendBase
{
    Q_OBJECT
public:
    explicit GoogleTasksBackend(QObject *parent = nullptr);
    ~GoogleTasksBackend() override;

    /// Point the transport at an API root (live tasks.googleapis.com or the
    /// mock). Version-less by convention; paths author /v1/... verbatim.
    void setBaseUrl(const QString &baseUrl);
    void setAccessToken(const QString &token);
    /// Persist merged record caches under this directory (JSON, atomic
    /// replace; file google-tasks-state.json). No sync tokens — full-listing
    /// strategy needs none. Unset ⇒ in-memory only.
    void setCacheDir(const QString &dir);

    // ==== discovery ====
    /// GET /v1/users/me/lists; every taskList becomes an available
    /// collection (writable v1). Emits listDiscovered per list, then
    /// listsLoadFinished exactly once.
    void loadTaskLists(const QString &requestId);
    QList<Kalburator::Sync::CollectionInfo> availableCollections() override;
    /// DiscoveredCalendar-style aggregate facts for one task list.
    Kalburator::Sync::DiscoveredCalendar discoveredTaskList(
        const QString &collectionId) const;

    // ==== identity ====
    QString backendType() const override;
    QList<Kalburator::Shape::Shape> nativeShapes() const override;

    // ==== read path ====
    SyncOperation *fetchItems(const QString &collectionId) override;
    bool recordsFromLastFetch(const QString &collectionId,
                              QList<BackendRecord> &records,
                              QString &errorMessage) override;
    QList<BackendRecord> loadRecords(const QString &collectionId) override;

    // ==== write path ====
    WriteOperation *applyRecords(const QString &collectionId,
                                 const WriterBatch &batch) override;

Q_SIGNALS:
    void listDiscovered(const QString &requestId, const QString &listId);
    void listsLoadFinished(const QString &requestId, bool success,
                           const QString &errorMessage = QString());

private:
    struct ListMeta {
        QString name;
        QString colorHex;
    };

    static QString tasksPathFor(const QString &collectionId);

    /// Strip O68-rejected read-only fields from an authored create body.
    static QByteArray stripNonCreatableFields(const QByteArray &wireJson);

    /// Heap-owned full-listing walk state for one fetchItems() pass (O62:
    /// async continuations outlive the enqueueOperation functor frame).
    struct FetchState;
    void startListingFetch(std::shared_ptr<FetchState> st);
    void finishFetch(std::shared_ptr<FetchState> st);

    /// Heap-owned sequential-apply state (O62).
    struct ApplyState;
    void applyStep(std::shared_ptr<ApplyState> st);

    Kalburator::Google::GoogleApiClient *m_client;

    // Last successful fetch's memo, served once by recordsFromLastFetch()
    // (H5/O23 contract); loadRecords() serves it without consuming.
    QHash<QString, QList<BackendRecord>> m_lastFetchRecords;

    // Merged per-collection views keyed by task-list id; persisted for
    // resume.
    QHash<QString, QHash<QString, BackendRecord>> m_cache;

    QHash<QString, ListMeta> m_lists;

    QString m_cacheDir;
    bool m_persistenceLoaded = false;
    void ensurePersistedStateLoaded();
    void persistState() const;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_TODO_GOOGLETASKSBACKEND_H
