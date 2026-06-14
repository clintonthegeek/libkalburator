#ifndef KALBURATOR_CONTACTS_AKONADICONTACTSBACKEND_H
#define KALBURATOR_CONTACTS_AKONADICONTACTSBACKEND_H

#ifdef HAVE_AKONADI

#include "syncbackend.h"
#include "syncoperation.h"
#include "backendrecord.h"
#include "collectioninfo.h"
#include "../sync/changedetection.h"
#include "akonadirevisionstore.h"

#include <Akonadi/Session>
#include <Akonadi/Monitor>
#include <Akonadi/Collection>
#include <Akonadi/Item>

#include <KContacts/Addressee>

#include <QMap>
#include <QSet>
#include <memory>

namespace Kalburator::Sync {

/**
 * @brief Akonadi client backend for KContacts::Addressee payloads.
 *
 * Acts as an Akonadi client, letting consumers read/write addressbooks
 * managed by Akonadi resources (e.g. EWS, KCard, Google Contacts).
 *
 * Collection ID scheme: "akonadi-<collectionId>" (e.g., "akonadi-42") —
 * the same scheme AkonadiProvider emits for every collection regardless of
 * type, and the same one the calendar backend uses. Stable across sessions
 * because Akonadi::Collection::Id persists.
 *
 * Uses Akonadi::Monitor to watch for external changes and maps those
 * to SyncBackend signals. Writes use a dedicated Session that the
 * Monitor ignores to prevent feedback loops.
 *
 * Read and write ops (loadRecords, createRecord, updateRecord, deleteRecord,
 * createCollection) and change detection are real, bridging async Akonadi
 * jobs via KJob::exec(). Mirror AkonadiBackend (calendar counterpart).
 */
class AkonadiContactsBackend : public SyncBackend,
                               public Kalburator::Sync::ChangeDetection
{
    Q_OBJECT

public:
    explicit AkonadiContactsBackend(QObject *parent = nullptr);
    ~AkonadiContactsBackend() override;

    /**
     * @brief Factory method for BackendRegistry.
     *
     * Config keys:
     *   akonadiCollectionId — optional; scopes the backend to one collection
     *                         (set by AkonadiProvider::createBackend() for contacts).
     */
    static SyncBackend* create(const QVariantMap &config, QObject *parent);

    // === Core Backend Identity ===
    static const QString BackendTypeName;
    QString backendType() const override;
    QList<Kalburator::Shape::Shape> nativeShapes() const override;

    // === Operation-Based Async API ===
    FetchOperation*  fetchItems(const QString &collectionId) override;

    PushOperation*   pushItems(const QString &collectionId,
                               const QList<KCalendarCore::Incidence::Ptr> &items) override;

    DeleteOperation* deleteItems(const QString &collectionId,
                                 const QStringList &uids) override;

    // =========================================================================
    // IBlobBackend overrides.
    //
    // recordId     = vCard UID (cross-backend-stable; Akonadi::Item::id() is
    //                local-only and is never exposed as the record id)
    // collectionId = "akonadi-<Akonadi::Collection::Id>" (provider scheme)
    // data         = serialized vCard bytes (KContacts::VCardConverter v4_0)
    // contentHash  = SHA-256 of vCard bytes
    // lastModified = Akonadi::Item::modificationTime()
    //
    // The per-record ops (createRecord/updateRecord/deleteRecord),
    // createCollection, loadRecords/loadRecord, and the Sync::ChangeDetection
    // methods are real (bridge async Akonadi jobs via KJob::exec()). Require a
    // running Akonadi server.
    // See docs/2026-05-26-akonadi-full-functionality-design.md.
    // =========================================================================

    // Identity
    QString backendId()   const override;
    QString displayName() const override;
    bool    isAvailable() const override;

    // Collections
    QList<CollectionInfo> availableCollections() override;
    CollectionInfo        collectionInfo(const QString &collectionId) override;
    QString               createCollection(const CollectionInfo &info) override;

    // Records — real implementations; KJob::exec() bridge; Akonadi server required
    QList<BackendRecord>         loadRecords(const QString &collectionId) override;
    std::optional<BackendRecord> loadRecord(const QString &recordId) override;
    QString                      createRecord(const QString &collectionId,
                                              const BackendRecord &record) override;
    bool                         updateRecord(const BackendRecord &record) override;
    bool                         deleteRecord(const QString &recordId) override;

    // Change detection — real; filters in-memory cache by modification time
    QList<BackendRecord> modifiedSince(const QString &collectionId,
                                       const QDateTime &since) override;
    QStringList          deletedSince(const QString &collectionId,
                                      const QDateTime &since) override;
    bool                 supportsDeleteTracking() const override { return false; }

    /// Test-only passthrough to addresseeFromRecord (no Akonadi server needed).
    KContacts::Addressee addresseeFromRecordForTest(const BackendRecord &record) const
    { return addresseeFromRecord(record); }

    // Batch — no-op; Akonadi has its own transaction layer
    void beginBatch()    override {}
    bool commitBatch()   override { return true; }
    void rollbackBatch() override {}
    bool supportsBatch() const override { return false; }

    // === Sync::ChangeDetection ===
    QString collectionRevision(const QString &collectionId) override;
    QString cachedCollectionRevision(const QString &collectionId) const override;
    void    primeRevisionCache(const QMap<QString, QString> &cache) override;

private slots:
    // Monitor callbacks for external changes
    void onItemAdded(const Akonadi::Item &item, const Akonadi::Collection &col);
    void onItemChanged(const Akonadi::Item &item, const QSet<QByteArray> &parts);
    void onItemRemoved(const Akonadi::Item &item);
    void onCollectionAdded(const Akonadi::Collection &col, const Akonadi::Collection &parent);
    void onCollectionChanged(const Akonadi::Collection &col, const QSet<QByteArray> &attrs);
    void onCollectionRemoved(const Akonadi::Collection &col);

private:
    void setupMonitor();

    /// Inverse of loadRecords serialization: parse BackendRecord.data
    /// (vCard bytes) into a KContacts::Addressee.
    KContacts::Addressee addresseeFromRecord(const BackendRecord &record) const;

    /// Convert Akonadi Collection::Id to our collection ID string
    QString collectionIdForAkonadiId(Akonadi::Collection::Id id) const;

    /// Convert our collection ID string to Akonadi Collection::Id
    Akonadi::Collection::Id akonadiIdForCollection(const QString &collectionId) const;

    /// Phase L.5: lazily seed m_collections with the single scoped collection
    /// so a per-collection scoped contacts backend can resolve it for
    /// fetch/create. No-op for non-scoped backends or non-scoped ids.
    void ensureScopedCollection(const QString &collectionId);

    /// Look up the cached Akonadi::Item for a given collection + uid
    Akonadi::Item findItemByUid(const QString &collectionId, const QString &uid) const;

    /// Resolve a cross-backend record id (vCard UID) to its cached Akonadi
    /// item and owning collection id. Returns an invalid Item if not found.
    Akonadi::Item findCachedItem(const QString &uid, QString *outCollectionId) const;

    Akonadi::Session  *m_session  = nullptr;  // For our writes (ignored by monitor)
    Akonadi::Monitor  *m_monitor  = nullptr;  // Watches external changes

    /// When non-empty, this backend is scoped to a single collection
    /// (set by create() via "akonadiCollectionId" config key).
    QString m_scopedCollectionId;

    // Collection ID <-> Akonadi Collection mapping
    QMap<Akonadi::Collection::Id, QString> m_collectionToContactId;
    QMap<QString, Akonadi::Collection>     m_collections;

    // Item tracking: collectionId -> (uid -> Akonadi::Item)
    QMap<QString, QMap<QString, Akonadi::Item>> m_itemsByCollection;

    /// Lazily-constructed persistent revision token store.
    Kalburator::Sync::AkonadiRevisionStore *revisionStore() const;
    mutable std::unique_ptr<Kalburator::Sync::AkonadiRevisionStore> m_revisionStore;

    /// uid -> (Akonadi item revision, cached contentHash). Lets loadRecords
    /// skip re-serializing+re-hashing an item whose revision is unchanged.
    mutable QMap<QString, QPair<int, QString>> m_hashMemo;
};

} // namespace Kalburator::Sync

#endif // HAVE_AKONADI

#endif // KALBURATOR_CONTACTS_AKONADICONTACTSBACKEND_H
