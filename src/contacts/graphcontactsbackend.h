#ifndef KALBURATOR_CONTACTS_GRAPHCONTACTSBACKEND_H
#define KALBURATOR_CONTACTS_GRAPHCONTACTSBACKEND_H

// B2C P2.c — Microsoft Graph contacts backend on the transport foundation
// (`GraphApiClient`, P2.b mock-tested). Inherits SyncBackendBase directly
// (no calendar-typed subclass in the contacts domain); the P1 calendar
// backends are the structural template.
//
// Wire truths pinned by FINDINGS O66/O70 and the P2 design pass:
//   - READS = expanded FULL listings, NEVER delta (O70: contacts delta
//     rejects $expand/$top/... so carriers are unreachable on delta pages).
//     Every fetch walks <collection>/contacts?$expand=extensions(...) and
//     reports the whole merged set — engine diffs expect whole views.
//   - Record id = raw Graph contact id, NEVER URL-encoded even when it ends
//     '=' (O66(d)); GraphApiClient carries paths verbatim.
//   - WRITES: create = POST /me/contacts with extensions[] STRIPPED (never
//     inline-at-create), then one nav POST per stripped carrier row to
//     /me/contacts/{storedId}/extensions; update = PATCH-in-place with
//     plain fields only, carrier changes routed through the nav channel;
//     delete accepts 204/200, and a 404 triggers ONE confirming re-list —
//     absent ⇒ success (idempotent under O66(f) flaky deletes),
//     still-present ⇒ fail loud.
//   - Create responses mint server-side ids ('='-suffixed on the mock and
//     real consumer Outlook alike) → WriteOperation::addIdAlias bridges
//     requested→stored (O55 machinery). The create echo is never trusted;
//     the next fetch's expand re-delivers carriers.
//   - Defensive union-merge (O69 lesson): a listing item that omits keys a
//     cached copy has is merged OVER the cached record instead of clobbering.

#include "syncbackendbase.h"
#include "writeoperation.h"
#include "writerbatch.h"

#include <QHash>
#include <QString>

namespace Kalburator::Graph {
class GraphApiClient;
}

namespace Kalburator::Sync {

class GraphContactsBackend : public SyncBackendBase
{
    Q_OBJECT
public:
    explicit GraphContactsBackend(QObject *parent = nullptr);
    ~GraphContactsBackend() override;

    /// Point the transport at an API root (live Graph or the mock).
    void setBaseUrl(const QString &baseUrl);
    void setAccessToken(const QString &token);
    /// Persist merged record caches under this directory (JSON, atomic
    /// replace; file msgraph-contacts-state.json). No sync tokens — the
    /// full-listing strategy needs none. Unset ⇒ in-memory only.
    void setCacheDir(const QString &dir);

    /// The default contacts collection (/me/contacts) when no folder
    /// discovery has run or the caller wants the account-wide view.
    static QString defaultCollectionId();

    // ==== discovery ====
    /// GET /me/contactFolders; every folder becomes an available collection
    /// (v1: all writable). Emits folderDiscovered per folder, then
    /// foldersLoadFinished exactly once.
    void loadFolders(const QString &requestId);
    QList<Kalburator::Sync::CollectionInfo> availableCollections() override;

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
    void folderDiscovered(const QString &requestId, const QString &folderId);
    void foldersLoadFinished(const QString &requestId, bool success,
                             const QString &errorMessage = QString());

private:
    struct FolderMeta {
        QString name;
    };

    QString collectionPathFor(const QString &collectionId) const;
    static QString expandedListingPath(const QString &basePath);

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

    // Merged per-collection views keyed by collection id (folder id or
    // defaultCollectionId()); persisted for resume.
    QHash<QString, QHash<QString, BackendRecord>> m_cache;

    QHash<QString, FolderMeta> m_folders;

    QString m_cacheDir;
    bool m_persistenceLoaded = false;
    void ensurePersistedStateLoaded();
    void persistState() const;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_CONTACTS_GRAPHCONTACTSBACKEND_H
