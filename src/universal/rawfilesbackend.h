#pragma once

#include <QHash>
#include <QString>

#include "syncbackend.h"
#include "shape.h"

namespace Kalburator::Sinks {

/// Universal file-backed sink. Each collection declares its own native
/// shape at createCollection() time; the backend stores records as
///   <rootPath>/<sanitized-record-id>.<encoding>.<domain>
///
/// Phase K.9: shape is required per-collection. The pre-K.9 contract
/// (one RawFilesBackend declaring Shape::Any() for everything) was
/// rejected by SyncEngine::dispatchSync's cross-domain check the
/// moment it tried to sync with a typed source. Universal sinks now
/// commit to a shape per collection; the engine looks it up via the
/// inherited SyncBackendBase::shapeFor virtual.
///
/// Persistence: shape declarations live in-memory only. Consumers must
/// re-declare via createCollection(info, shape) on every construction
/// (PalmRuntime does this on every device connect). The on-disk
/// _shapes.json manifest still persists CollectionInfo (name/type)
/// for discovery, but not shape.
class RawFilesBackend : public Kalburator::Sync::SyncBackend {
    Q_OBJECT
public:
    explicit RawFilesBackend(QString rootPath, QObject *parent = nullptr);

    QString backendType() const override { return QStringLiteral("raw-files"); }

    /// Dedup'd union of the shapes declared across all collections.
    /// Engines that want the per-mapping shape should use shapeFor().
    QList<Kalburator::Shape::Shape> nativeShapes() const override;

    /// Per-collection shape lookup. Engine uses this in dispatchSync.
    Kalburator::Shape::Shape shapeFor(const QString &collectionId) const override;

    QString resourceId() const override
        { return QStringLiteral("raw-files:") + m_rootPath; }

    // ---- IBlobBackend ----
    QString displayName() const override { return QStringLiteral("Raw Files Backend"); }
    bool isAvailable() const override;

    QList<Kalburator::Sync::CollectionInfo> availableCollections() override;
    Kalburator::Sync::CollectionInfo collectionInfo(const QString &collectionId) override;

    /// K.9: shape-aware createCollection. Callers MUST use this; the
    /// inherited 1-arg createCollection from IBlobBackend is unsafe on
    /// a universal sink (no shape would mean dispatchSync bails).
    QString createCollection(const Kalburator::Sync::CollectionInfo &info,
                             const Kalburator::Shape::Shape &shape);

    void deleteCollection(const QString &collectionId);
    void clearCollection(const QString &collectionId);

    QList<Kalburator::Sync::BackendRecord> loadRecords(const QString &collectionId) override;
    std::optional<Kalburator::Sync::BackendRecord> loadRecord(const QString &recordId) override;
    QString createRecord(const QString &collectionId,
                         const Kalburator::Sync::BackendRecord &record) override;
    bool updateRecord(const Kalburator::Sync::BackendRecord &record) override;
    bool deleteRecord(const QString &recordId) override;

private:
    QString filePathFor(const QString &collectionId, const QString &recordId) const;
    Kalburator::Sync::BackendRecord readFile(const QString &absPath) const;

    void loadManifest();
    void saveManifest() const;

    static QString sanitize(const QString &id);
    static QString suffixFor(const QString &collectionId);

    QString m_rootPath;
    QHash<QString, Kalburator::Sync::CollectionInfo> m_collections;
    QHash<QString, Kalburator::Shape::Shape> m_shapeByCollection;
};

} // namespace Kalburator::Sinks
