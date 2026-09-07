#ifndef KALBURATOR_BLOB_BACKENDCAPABILITYINTERFACES_H
#define KALBURATOR_BLOB_BACKENDCAPABILITYINTERFACES_H

#include <optional>

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <kalburator/types/backendrecord.h>
#include <kalburator/types/collectioninfo.h>
#include <kalburator/blob/recordloadresult.h>

namespace Kalburator::Sync {

class WriteOperation;
struct WriterBatch;

/// Read-only record access.  A backend may expose this without supporting
/// direct record CRUD (for example an operation-backed vendor transport).
class IBackendRecordReader {
public:
    virtual ~IBackendRecordReader() = default;
    virtual QList<BackendRecord> loadRecords(const QString &collectionId) = 0;
    virtual RecordLoadResult loadRecordsResult(const QString &collectionId) = 0;
    virtual std::optional<BackendRecord> loadRecord(const QString &recordId) = 0;
};

/// Direct record CRUD.  This is deliberately separate from reading and from
/// asynchronous batch application: a caller must acquire it before invoking
/// create/update/delete.
class IBackendRecordMutator {
public:
    virtual ~IBackendRecordMutator() = default;
    virtual QString createRecord(const QString &collectionId,
                                 const BackendRecord &record) = 0;
    virtual bool updateRecord(const BackendRecord &record) = 0;
    virtual bool deleteRecord(const QString &recordId) = 0;
};

/// Asynchronous classified record application used by the sync engine.
class IBackendRecordApplier {
public:
    virtual ~IBackendRecordApplier() = default;
    virtual WriteOperation *applyRecords(const QString &collectionId,
                                         const WriterBatch &batch) = 0;
};

/// Collection-wide record removal.  Kept separate from direct CRUD because
/// some backends provide an atomic or backend-specific wipe implementation.
class IBackendCollectionWiper {
public:
    virtual ~IBackendCollectionWiper() = default;
    virtual bool wipeCollection(const QString &collectionId) = 0;
};

class IBackendCollections {
public:
    virtual ~IBackendCollections() = default;
    virtual QList<CollectionInfo> availableCollections() = 0;
    virtual CollectionInfo collectionInfo(const QString &collectionId) = 0;
    virtual QString createCollection(const CollectionInfo &info) = 0;
};

/// Physical collection metadata and lifecycle operations.  Backends that do
/// not support one of these operations must return false; callers must not
/// infer support from the record or discovery interfaces.
class IBackendCollectionMutator {
public:
    virtual ~IBackendCollectionMutator() = default;
    virtual bool updateCollectionMetadata(const QString &collectionId,
                                          const QVariantMap &metadata) = 0;
    virtual bool renamePhysicalCollection(const QString &collectionId,
                                          const QString &newCollectionId) = 0;
    virtual bool deletePhysicalCollection(const QString &collectionId) = 0;
};

class IBackendRecords : public IBackendRecordReader,
                        public IBackendRecordMutator {
public:
    virtual ~IBackendRecords() = default;
};

class IBackendChangeTracking {
public:
    virtual ~IBackendChangeTracking() = default;
    virtual QList<BackendRecord> modifiedSince(const QString &collectionId,
                                               const QDateTime &since) = 0;
    virtual QStringList deletedSince(const QString &collectionId,
                                     const QDateTime &since) = 0;
    virtual bool supportsDeleteTracking() const = 0;
};

class IBackendBatch {
public:
    virtual ~IBackendBatch() = default;
    virtual void beginBatch() = 0;
    virtual bool commitBatch() = 0;
    virtual void rollbackBatch() = 0;
    virtual bool supportsBatch() const = 0;
};

} // namespace Kalburator::Sync

#endif
