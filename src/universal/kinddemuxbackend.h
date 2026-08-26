#pragma once

#include <QHash>
#include <QList>
#include <QMap>
#include <QStringList>
#include <memory>

#include "syncbackendbase.h"       // Kalburator::Sync::SyncBackendBase
#include "changedetection.h"      // Kalburator::Sync::ChangeDetection
#include "collectioninfo.h"       // Kalburator::Sync::CollectionInfo

namespace Kalburator::Sinks {

/// Per-collection routing backend for a demuxed DAV domain (B2C P3.e
/// kind-demux). One transport collection that hosts mixed iCal component
/// kinds (VEVENT+VTODO in ONE CalDAV collection) must surface in TWO
/// ProviderBackendSpecs — the calendar domain and the todo domain — while
/// every view keeps the SAME collection id. A single spec, however, carries
/// exactly ONE backend, so when a domain's hosted set mixes direct
/// (unfiltered) collections with FilteredCollectionBackend views over the
/// same underlying transport, something has to route by collection id.
/// That is this class: a thin SyncBackendBase whose every collection-keyed
/// call is forwarded to the child backend registered for that collection.
///
/// Rectification rule (binding): transport grouping never crosses a domain
/// boundary. The demux exists precisely so each domain spec sees only its
/// own records even though all children share one physical transport.
///
/// Routes are (collectionId -> child) pairs; children are BORROWED except
/// that `underlyingLifetime` (a shared_ptr to the shared transport backend,
/// when the caller built one) is held so the last demux referencing it
/// keeps it alive. Record-id-keyed calls (loadRecord / updateRecord /
/// deleteRecord) try children in route order and stop at the first hit —
/// sound because all children here project the SAME underlying store.
///
/// fetchItems()/deleteItems() intentionally keep the SyncBackendBase
/// defaults (NotSupported): children project read-only through loadRecords()
/// exactly like a bare FilteredCollectionBackend does, and the engine's
/// fetch gate falls back to loadRecordsOrError() for such backends.
/// applyRecords() likewise uses the base synchronous adapter, which routes
/// through the routed create/update/delete virtuals.
class KindDemuxBackend : public Kalburator::Sync::SyncBackendBase,
                         public Kalburator::Sync::ChangeDetection {
    Q_OBJECT
public:
    /// @param routes  ordered collectionId -> child backend pairs (borrowed).
    /// @param underlyingLifetime  optional shared ownership of the shared
    ///   transport backend some routes point into; kept alive as long as any
    ///   demux holding a reference lives. Pass null when routes fully own
    ///   their children elsewhere.
    KindDemuxBackend(const QList<QPair<QString, Kalburator::Sync::SyncBackendBase*>>& routes,
                     std::shared_ptr<Kalburator::Sync::SyncBackendBase> underlyingLifetime,
                     QObject* parent = nullptr);

    QString backendType() const override;
    QString displayName() const override;
    bool    isAvailable() const override;

    QList<Kalburator::Shape::Shape> nativeShapes() const override;
    Kalburator::Shape::Shape shapeFor(const QString& collectionId) const override;
    bool discoveredWritable(const QString& collectionId) const override;
    int  maxConcurrentOperations() const override;

    QList<Kalburator::Sync::CollectionInfo> availableCollections() override;
    Kalburator::Sync::CollectionInfo        collectionInfo(const QString& collectionId) override;

    QList<Kalburator::Sync::BackendRecord>         loadRecords(const QString& collectionId) override;
    std::optional<Kalburator::Sync::BackendRecord> loadRecord(const QString& recordId) override;
    QString createRecord(const QString& collectionId,
                         const Kalburator::Sync::BackendRecord& record) override;
    bool    updateRecord(const Kalburator::Sync::BackendRecord& record) override;
    bool    deleteRecord(const QString& recordId) override;

    QList<Kalburator::Sync::BackendRecord> modifiedSince(
        const QString& collectionId, const QDateTime& since) override;
    QStringList deletedSince(
        const QString& collectionId, const QDateTime& since) override;

    // ---- Sync::ChangeDetection ----
    // Every collection on this demux resolves through its child's own
    // ChangeDetection (FilteredCollectionBackend forwards to the shared
    // transport; direct routes hit it directly), so revisions are identical
    // across the domains that share a collection — correct, since the views
    // change together.
    QString collectionRevision(const QString& collectionId) override;
    void    collectionRevisionsAsync(
        const QStringList& collectionIds,
        std::function<void(QMap<QString, QString>)> done) override;
    QString cachedCollectionRevision(const QString& collectionId) const override;
    bool    persistsCollectionRevisions() const override;

private:
    struct Route {
        QString collectionId;
        Kalburator::Sync::SyncBackendBase* child = nullptr;
    };

    Kalburator::Sync::SyncBackendBase* childFor(const QString& collectionId) const;
    /// Distinct children in first-appearance order — the ordered trial list
    /// for record-id-keyed calls.
    QList<Kalburator::Sync::SyncBackendBase*> distinctChildren() const;
    Kalburator::Sync::ChangeDetection* childChangeDetection(
        Kalburator::Sync::SyncBackendBase* child) const;

    QList<Route> m_routes;
    // Shared ownership of the underlying transport (when the provider built
    // one); empty when routes borrow children owned elsewhere.
    std::shared_ptr<Kalburator::Sync::SyncBackendBase> m_underlying;
};

} // namespace Kalburator::Sinks
