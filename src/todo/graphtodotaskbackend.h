#ifndef KALBURATOR_TODO_GRAPHTODOTASKBACKEND_H
#define KALBURATOR_TODO_GRAPHTODOTASKBACKEND_H

// B2C P3.d — Microsoft Graph todoTask backend on the transport foundation
// (`GraphApiClient`). Inherits SyncBackendBase directly (todo domain);
// the P2 GraphContactsBackend is the structural template — same discipline
// pointed at todoTask resources.
//
// Wire truths pinned by FINDINGS O66/O69/O73, the P3 design pass, and the
// P3.f live checkpoint (2026-08-26):
//   - The v1.0 todoTask wire property is `title` — create REQUIRES it,
//     listings deliver it; there is no `subject` anywhere.
//   - Open-extension ids minted on todoTask carry the
//     microsoft.graph.openTypeExtension.* prefix (NOT contacts'
//     Microsoft.OutlookServices.*); a filtered expand naming an
//     OutlookServices-prefixed Id 500s deterministically on /me/todo.
//   - READS = expanded FULL listings, NEVER delta (O69: consumer delta pages
//     deliver SKELETON projections — carriers unreachable). Every fetch walks
//     <list>/tasks?$expand=extensions($filter=Id eq '…kalburator.canon') and
//     reports the whole merged set — engine diffs expect whole views.
//   - Record id = raw todoTask id, NEVER URL-encoded even when it ends '='
//     (O66(d)); paths are authored verbatim against the version-less base.
//   - WRITES: create = POST /me/todo/lists/{id}/tasks with extensions[]
//     STRIPPED (inline-create is a wire-lie — echoed, never persisted),
//     then one nav POST per stripped carrier row to
//     …/tasks/{storedId}/extensions (UPSERT keyed on extensionName, O73);
//     update = PATCH-in-place with plain fields only (a PATCH-borne
//     extensions[] key is rejected server-side), carrier changes routed
//     through the nav channel; delete accepts 204/200, and a 404 triggers
//     ONE confirming re-list — absent ⇒ success, still-present ⇒ fail loud.
//   - O66(b) fail-loud rule: a create/update body carrying a non-null
//     `recurrence` without a `dueDateTime` fails THAT record BEFORE any
//     network call. Dates are never fabricated.
//   - Create responses mint server-side ids ('='-suffixed on the mock and
//     real consumer Outlook alike) → WriteOperation::addIdAlias bridges
//     requested→stored (O55 machinery).
//   - Defensive union-merge (O69 lesson): a served row lacking keys a cached
//     copy has is enriched FROM the cache instead of clobbering it.
//
// Persistence: no sync token exists to persist (expanded-listing strategy);
// only the merged record caches are written (<cacheDir>/msgraph-todo-state.json,
// atomic replace), exactly the GraphContactsBackend rationale.

#include "discoveredcalendar.h"
#include "syncbackendbase.h"
#include "writeoperation.h"
#include "writerbatch.h"

#include <QHash>
#include <QString>

namespace Kalburator::Graph {
class GraphApiClient;
}

namespace Kalburator::Sync {

class GraphTodoTaskBackend : public SyncBackendBase
{
    Q_OBJECT
public:
    explicit GraphTodoTaskBackend(QObject *parent = nullptr);
    ~GraphTodoTaskBackend() override;

    /// Point the transport at an API root (live Graph or the mock).
    /// Version-less by convention; paths author /v1.0/... verbatim.
    void setBaseUrl(const QString &baseUrl);
    void setAccessToken(const QString &token);
    /// Persist merged record caches under this directory (JSON, atomic
    /// replace; file msgraph-todo-state.json). No sync tokens — the
    /// full-listing strategy needs none. Unset ⇒ in-memory only.
    void setCacheDir(const QString &dir);

    // ==== discovery ====
    /// GET /me/todo/lists; every list becomes an available collection
    /// (v1: all writable). Emits listDiscovered per list, then
    /// listsLoadFinished exactly once.
    void loadTaskLists(const QString &requestId);
    QList<Kalburator::Sync::CollectionInfo> availableCollections() override;
    /// DiscoveredCalendar-style aggregate facts for one todo list.
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
        QString wellknownListName;
    };

    QString collectionPathFor(const QString &collectionId) const;
    static QString expandedListingPath(const QString &basePath);

    /// O66(b): a body with a non-null `recurrence` but no (non-null)
    /// `dueDateTime` may not hit the wire — dates are never fabricated.
    static bool violatesRecurrenceDueRule(const QJsonObject &body);

    /// Heap-owned full-listing walk state for one fetchItems() pass (O62:
    /// async continuations outlive the enqueueOperation functor frame).
    struct FetchState;
    void startListingFetch(std::shared_ptr<FetchState> st);

    /// Heap-owned sequential-apply state (O62).
    struct ApplyState;
    void applyStep(std::shared_ptr<ApplyState> st);
    void postNextCarrier(std::shared_ptr<ApplyState> st);

    Kalburator::Graph::GraphApiClient *m_client;

    // Last successful fetch's memo, served once by recordsFromLastFetch()
    // (H5/O23 contract); loadRecords() serves it without consuming.
    QHash<QString, QList<BackendRecord>> m_lastFetchRecords;

    // Merged per-collection views keyed by todo-list id; persisted for
    // resume.
    QHash<QString, QHash<QString, BackendRecord>> m_cache;

    QHash<QString, ListMeta> m_lists;

    QString m_cacheDir;
    bool m_persistenceLoaded = false;
    void ensurePersistedStateLoaded();
    void persistState() const;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_TODO_GRAPHTODOTASKBACKEND_H
