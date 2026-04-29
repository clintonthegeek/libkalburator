#ifndef KALBURATOR_BLOB_LOCALBLOBBACKEND_H
#define KALBURATOR_BLOB_LOCALBLOBBACKEND_H

#include <QDir>
#include <QObject>
#include <QString>

#include "iblobbackend.h"

namespace Kalburator::Sync {

/**
 * @brief Disk-backed IBlobBackend reference impl.
 *
 * Storage: <basePath>/<collectionId>/<slug-<short-hash-of-id>>.<ext>
 * where <ext> is derived from CollectionInfo::type:
 *   "memos"    → .md
 *   "contacts" → .vcf
 *   "calendar" → .ics
 *   "todos"    → .ics
 *   anything else → .bin
 *
 * contentHash: SHA-256 of data, computed on every read (no sidecar).
 * lastModified: filesystem mtime.
 * isDeleted: not tracked (file-based backend; supportsDeleteTracking
 * returns false).
 */
class LocalBlobBackend : public QObject, public IBlobBackend {
    Q_OBJECT
public:
    explicit LocalBlobBackend(const QString &basePath,
                              QObject *parent = nullptr);
    ~LocalBlobBackend() override;

    QString backendId() const override { return QStringLiteral("local-blob"); }
    QString displayName() const override { return QStringLiteral("Local Blob Backend"); }
    bool    isAvailable() const override;

    QList<CollectionInfo> availableCollections() override;
    CollectionInfo collectionInfo(const QString &collectionId) override;
    QString createCollection(const CollectionInfo &info) override;

    QList<BackendRecord> loadRecords(const QString &collectionId) override;
    std::optional<BackendRecord> loadRecord(const QString &recordId) override;
    QString createRecord(const QString &collectionId,
                         const BackendRecord &record) override;
    bool updateRecord(const BackendRecord &record) override;
    bool deleteRecord(const QString &recordId) override;

    QList<BackendRecord> modifiedSince(const QString &collectionId,
                                       const QDateTime &since) override;
    QStringList deletedSince(const QString &collectionId,
                             const QDateTime &since) override;

    QString basePath() const { return m_basePath; }

Q_SIGNALS:
    void recordCreated(const QString &recordId);
    void recordUpdated(const QString &recordId);
    void recordDeleted(const QString &recordId);
    void errorOccurred(const QString &error);
    void progressUpdated(int current, int total, const QString &message);

private:
    QString extensionForType(const QString &type) const;
    QString filenameFor(const BackendRecord &record, const QString &type) const;
    QString pathFromRecordId(const QString &recordId) const;
    BackendRecord readFile(const QString &absolutePath, const QString &recordId) const;
    bool writeAtomic(const QString &absolutePath, const QByteArray &data);

    QString m_basePath;
    // Map: known collection type by id (populated from createCollection
    // calls so loadRecords knows what extension to expect).
    QHash<QString, QString> m_collectionTypes;
};

} // namespace Kalburator::Sync

#endif
