#ifndef KALBURATOR_STORAGE_BASELINESTORE_H
#define KALBURATOR_STORAGE_BASELINESTORE_H

/**
 * @file baselinestore.h
 * @brief SQLite-backed baseline store for the blob sync engine.
 *
 * Two table generations co-exist:
 *
 * v2 (post-F1, user_version=3): `blob_baselines`
 *   Keyed by (backend_id, collection_id, record_id) → content_hash.
 *   Legacy API; deprecated in G.4.
 *
 * v3 (G.4, user_version=4): `blob_baselines_v3`
 *   Keyed by (mapping_id, record_id) → canonical Shape + bytes.
 *   New preferred API. On first open after upgrade, data is migrated
 *   from blob_baselines via a mapping resolver supplied by the engine.
 *
 * Not thread-safe. Callers must serialize access to a given instance.
 * Not a QObject — pure value-lifetime class with RAII connection
 * ownership.
 */

#include <functional>
#include <optional>

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "canonicalrecord.h"

namespace Kalburator::Storage {

class BaselineStore
{
public:
    explicit BaselineStore(const QString &dbPath);
    ~BaselineStore();

    BaselineStore(const BaselineStore &) = delete;
    BaselineStore &operator=(const BaselineStore &) = delete;
    BaselineStore(BaselineStore &&) = delete;
    BaselineStore &operator=(BaselineStore &&) = delete;

    bool    isOpen() const;
    QString lastError() const;
    QString databasePath() const;

    // -----------------------------------------------------------------------
    // Mapping resolver (G.4)
    //
    // The engine calls setMappingResolver() during init so the v2→v3
    // migration can discover which mapping_ids reference a given
    // (backend_id, collection_id) pair.  Signature:
    //   fn(backendId, collectionId) → list of mapping IDs that include
    //   that pair as source or target collection.
    // -----------------------------------------------------------------------
    using MappingResolver =
        std::function<QStringList(const QString &backendId,
                                  const QString &collectionId)>;
    void setMappingResolver(MappingResolver fn);

    /// Perform v2→v3 data migration using the current mapping resolver.
    /// Safe to call multiple times (idempotent after user_version==4).
    /// Returns true on success or if already migrated.
    bool migrateV3();

    // -----------------------------------------------------------------------
    // v3 mapping-keyed API — keyed by (mappingId, recordId).
    // Stored in blob_baselines_v3.
    // -----------------------------------------------------------------------

    bool setBaselineV3(const QString &mappingId,
                       const Kalburator::Shape::CanonicalRecord &rec);

    std::optional<Kalburator::Shape::CanonicalRecord>
    baselineV3(const QString &mappingId, const QString &recordId) const;

    QList<Kalburator::Shape::CanonicalRecord>
    baselinesForMappingV3(const QString &mappingId) const;

    bool removeBaselineV3(const QString &mappingId, const QString &recordId);

    bool clearMappingV3(const QString &mappingId);

    // -----------------------------------------------------------------------
    // Collection-baseline API (K.5, schema v5).
    //
    // Per (mappingId, collectionId) → QVariantMap of
    // domain-plugin-declared property snapshots (e.g. color, description
    // for calendars). Stored in collection_baselines.
    // -----------------------------------------------------------------------

    bool setCollectionBaseline(const QString &mappingId,
                               const QString &collectionId,
                               const QVariantMap &props);

    QVariantMap collectionBaseline(const QString &mappingId,
                                   const QString &collectionId) const;

    bool removeCollectionBaseline(const QString &mappingId,
                                  const QString &collectionId);

    // -----------------------------------------------------------------------
    // Mapping-metadata API (K.5, schema v5). Per-mappingId scalars.
    // Currently: last-sync timestamp.
    // -----------------------------------------------------------------------

    bool      setLastSyncTime(const QString &mappingId, const QDateTime &when);
    QDateTime lastSyncTime(const QString &mappingId) const;

    // -----------------------------------------------------------------------
    // Triple-keyed API — keyed by (backendId, collectionId, recordId).
    // Stored in blob_baselines.
    // @deprecated Use the v3 mapping-keyed API instead.
    // -----------------------------------------------------------------------

    [[deprecated("Use setBaselineV3() / mapping-keyed API (G.4).")]]
    bool setBaseline(const QString &backendId,
                     const QString &collectionId,
                     const QString &recordId,
                     const QString &contentHash);

    [[deprecated("Use baselineV3() / mapping-keyed API (G.4).")]]
    QString baselineHash(const QString &backendId,
                         const QString &collectionId,
                         const QString &recordId) const;

    [[deprecated("Use setBaselineV3() / mapping-keyed API (G.4).")]]
    bool commitBaselines(const QString &backendId,
                         const QString &collectionId,
                         const QMap<QString, QString> &recordIdToHash);

    [[deprecated("Use baselinesForMappingV3() / mapping-keyed API (G.4).")]]
    QStringList baselineRecordIds(const QString &backendId,
                                  const QString &collectionId) const;

    [[deprecated("Use clearMappingV3() / mapping-keyed API (G.4).")]]
    bool clearCollection(const QString &backendId,
                         const QString &collectionId);

private:
    static int s_connectionCounter;

    QString         m_dbPath;
    QString         m_connName;
    bool            m_isOpen = false;
    bool            m_needsV3Migration = false;
    mutable QString m_lastError;
    MappingResolver m_mappingResolver;

    bool ensureSchemaAndVersion();
    bool ensureSchemaV3();
    bool ensureSchemaV5();
    void setError(const QString &message) const;
};

} // namespace Kalburator::Storage

#endif // KALBURATOR_STORAGE_BASELINESTORE_H
