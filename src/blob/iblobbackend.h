#ifndef KALBURATOR_BLOB_IBLOBBACKEND_H
#define KALBURATOR_BLOB_IBLOBBACKEND_H

#include <optional>

#include <QDateTime>
#include <QList>
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
 * This is a pure abstract interface (no QObject). Concrete implementations
 * supply QObject and its signal machinery through their own inheritance
 * (e.g. `class MockBlobBackend : public IBlobBackend` with Q_OBJECT).
 * The split is necessary so `SyncBackend`, which already inherits QObject,
 * can add IBlobBackend as a second base without creating a QObject diamond.
 *
 * Signals expected from implementations (declare in each concrete class):
 *   void recordCreated(const QString &recordId);
 *   void recordUpdated(const QString &recordId);
 *   void recordDeleted(const QString &recordId);
 *   void errorOccurred(const QString &error);
 *   void progressUpdated(int current, int total, const QString &message);
 */
class IBlobBackend {
public:
    virtual ~IBlobBackend() = default;

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
    // Error-reporting overload: returns false and sets `error` on failure.
    // Default delegates to loadRecords() with no error reporting. Override
    // in test fakes to simulate fetch failures without changing the interface.
    virtual bool loadRecordsOrError(const QString &collectionId,
                                    QList<BackendRecord> &records,
                                    QString &error) {
        records = loadRecords(collectionId);
        error.clear();
        return true;
    }
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
};

} // namespace Kalburator::Sync

#endif
