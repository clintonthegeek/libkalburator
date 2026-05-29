#pragma once

#include <QHash>
#include <QMutex>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

#include "syncbackendbase.h"
#include "shape.h"

namespace Kalburator::Sinks {

/// Universal SQLite-backed sink. Each collection declares its native
/// shape at createCollection() time. On first push of a new shape the
/// backend creates a table <domain>_<encoding> with:
///   record_id TEXT PRIMARY KEY
///   data BLOB NOT NULL
///   content_hash TEXT NOT NULL
///   last_modified TEXT NOT NULL
///
/// Phase K.9: same per-collection shape contract as RawFilesBackend.
/// Universal sinks no longer declare Shape::Any(); each collection
/// commits to a shape at creation time so SyncEngine::dispatchSync
/// can resolve cross-domain mappings correctly.
///
/// Thread safety: each call site opens a per-thread SQLite connection
/// (unique name = base + "_" + thread-address) so the backend is safe
/// to use from multiple QThread contexts (e.g. the SyncEngine worker
/// thread and the main thread simultaneously). All per-thread connection
/// names are tracked in m_openConnections (guarded by m_connMutex) for
/// cleanup in the destructor.
class GenericSqliteBackend : public Kalburator::Sync::SyncBackendBase {
    Q_OBJECT
public:
    explicit GenericSqliteBackend(QString dbPath, QObject *parent = nullptr);
    ~GenericSqliteBackend() override;

    QString backendType() const override { return QStringLiteral("generic-sqlite"); }

    /// Dedup'd union of the shapes declared across all collections.
    QList<Kalburator::Shape::Shape> nativeShapes() const override;

    /// Per-collection shape lookup. Engine uses this in dispatchSync.
    Kalburator::Shape::Shape shapeFor(const QString &collectionId) const override;

    QString resourceId() const override
        { return QStringLiteral("generic-sqlite:") + m_dbPath; }

    // ---- IBlobBackend ----
    QString displayName() const override { return QStringLiteral("Generic SQLite Backend"); }
    bool isAvailable() const override;

    QList<Kalburator::Sync::CollectionInfo> availableCollections() override;
    Kalburator::Sync::CollectionInfo collectionInfo(const QString &collectionId) override;

    /// K.9: shape-aware createCollection. Callers MUST use this.
    QString createCollection(const Kalburator::Sync::CollectionInfo &info,
                             const Kalburator::Shape::Shape &shape);

    bool deleteCollection(const QString &collectionId);
    bool clearCollection(const QString &collectionId);

    QList<Kalburator::Sync::BackendRecord> loadRecords(const QString &collectionId) override;
    std::optional<Kalburator::Sync::BackendRecord> loadRecord(const QString &recordId) override;
    QString createRecord(const QString &collectionId,
                         const Kalburator::Sync::BackendRecord &record) override;
    bool updateRecord(const Kalburator::Sync::BackendRecord &record) override;
    bool deleteRecord(const QString &recordId) override;

private:
    /// Returns (and lazily opens) the per-thread SQLite connection.
    QSqlDatabase threadDb();
    bool ensureOpen();
    bool ensureSchema(QSqlDatabase &db);
    bool ensureTableFor(const QString &collectionId);
    static QString tableNameFor(const QString &collectionId);

    // RecordId format: "<collectionId>/<originalId>"
    static QString encodeRecordId(const QString &collectionId, const QString &id);
    static bool decodeRecordId(const QString &recordId,
                               QString *collectionId, QString *id);

    QString m_dbPath;
    QString m_baseConnectionName; ///< unique base; thread suffix appended per call
    QHash<QString, Kalburator::Sync::CollectionInfo> m_collections;
    QHash<QString, Kalburator::Shape::Shape> m_shapeByCollection;
    mutable QMutex m_connMutex;       ///< guards m_openConnections
    QStringList    m_openConnections; ///< all per-thread conn names (for destructor cleanup)
    bool m_open = false;
};

} // namespace Kalburator::Sinks
