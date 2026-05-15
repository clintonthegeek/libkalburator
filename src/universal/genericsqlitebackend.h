#pragma once

#include <QHash>
#include <QSqlDatabase>
#include <QString>

#include "syncbackend.h"
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
class GenericSqliteBackend : public Kalburator::Sync::SyncBackend {
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

    void deleteCollection(const QString &collectionId);
    void clearCollection(const QString &collectionId);

    QList<Kalburator::Sync::BackendRecord> loadRecords(const QString &collectionId) override;
    std::optional<Kalburator::Sync::BackendRecord> loadRecord(const QString &recordId) override;
    QString createRecord(const QString &collectionId,
                         const Kalburator::Sync::BackendRecord &record) override;
    bool updateRecord(const Kalburator::Sync::BackendRecord &record) override;
    bool deleteRecord(const QString &recordId) override;

private:
    bool ensureOpen();
    bool ensureSchema();
    bool ensureTableFor(const QString &collectionId);
    static QString tableNameFor(const QString &collectionId);

    // RecordId format: "<collectionId>/<originalId>"
    static QString encodeRecordId(const QString &collectionId, const QString &id);
    static bool decodeRecordId(const QString &recordId,
                               QString *collectionId, QString *id);

    QString m_dbPath;
    QString m_connectionName;
    QHash<QString, Kalburator::Sync::CollectionInfo> m_collections;
    QHash<QString, Kalburator::Shape::Shape> m_shapeByCollection;
    bool m_open = false;
};

} // namespace Kalburator::Sinks
