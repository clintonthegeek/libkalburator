#ifndef KALBURATOR_BLOB_IBLOBBACKEND_H
#define KALBURATOR_BLOB_IBLOBBACKEND_H

#include <optional>

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "backendrecord.h"
#include "collectioninfo.h"

namespace Kalburator::Sync {

/**
 * @brief Abstract lower-layer (blob-typed) sync backend.
 *
 * Stores and retrieves opaque `BackendRecord`s organized into
 * `CollectionInfo`-keyed collections. Host-neutral; no calendar,
 * contact, or other domain knowledge.
 *
 * The upper calendar-typed layer (`SyncBackend` etc.) is independent
 * of this interface in Phase B2. A later phase bridges the two via
 * per-backend adapters.
 */
class IBlobBackend : public QObject {
    Q_OBJECT
public:
    explicit IBlobBackend(QObject *parent = nullptr);
    ~IBlobBackend() override;

    // --- Identity ---
    virtual QString backendId() const = 0;
    virtual QString displayName() const = 0;
    virtual bool    isAvailable() const = 0;

    // --- Collections ---
    virtual QList<CollectionInfo> availableCollections() = 0;
    virtual CollectionInfo collectionInfo(const QString &collectionId) = 0;
    virtual QString createCollection(const CollectionInfo &info) = 0;

    // --- Records ---
    virtual QList<BackendRecord> loadRecords(const QString &collectionId) = 0;
    virtual std::optional<BackendRecord> loadRecord(const QString &recordId) = 0;
    virtual QString createRecord(const QString &collectionId,
                                 const BackendRecord &record) = 0;
    virtual bool    updateRecord(const BackendRecord &record) = 0;
    virtual bool    deleteRecord(const QString &recordId) = 0;

    // --- Change detection ---
    virtual QList<BackendRecord> modifiedSince(const QString &collectionId,
                                               const QDateTime &since) = 0;
    virtual QStringList deletedSince(const QString &collectionId,
                                     const QDateTime &since) = 0;
    virtual bool supportsDeleteTracking() const { return false; }

    // --- Batch / transaction ---
    virtual void beginBatch()       {}
    virtual bool commitBatch()      { return true; }
    virtual void rollbackBatch()    {}
    virtual bool supportsBatch() const { return false; }

Q_SIGNALS:
    void recordCreated(const QString &recordId);
    void recordUpdated(const QString &recordId);
    void recordDeleted(const QString &recordId);
    void errorOccurred(const QString &error);
    void progressUpdated(int current, int total, const QString &message);
};

} // namespace Kalburator::Sync

#endif
