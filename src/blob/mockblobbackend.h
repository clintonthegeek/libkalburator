#ifndef KALBURATOR_BLOB_MOCKBLOBBACKEND_H
#define KALBURATOR_BLOB_MOCKBLOBBACKEND_H

#include <QHash>
#include <QList>
#include <QObject>

#include "iblobbackend.h"

namespace Kalburator::Sync {

/**
 * @brief In-memory IBlobBackend for tests.
 *
 * Minimal test support — CRUD plus failure injection. Intentionally
 * does NOT include the latency injection, operation log, or
 * deterministic-mode features of the calendar-layer MockBackend;
 * those are deferred until a specific test needs them.
 */
class MockBlobBackend : public QObject, public IBlobBackend {
    Q_OBJECT
public:
    enum class FailurePoint {
        None,
        OnLoadRecords,
        OnLoadRecord,
        OnCreateRecord,
        OnUpdateRecord,
        OnDeleteRecord,
        OnModifiedSince,
        OnCreateCollection,
    };

    explicit MockBlobBackend(QObject *parent = nullptr);
    ~MockBlobBackend() override;

    // IBlobBackend
    QString backendId() const override { return QStringLiteral("mock-blob"); }
    QString displayName() const override { return QStringLiteral("Mock Blob Backend"); }
    bool    isAvailable() const override { return true; }

    QList<CollectionInfo> availableCollections() override;
    CollectionInfo collectionInfo(const QString &collectionId) override;
    QString createCollection(const CollectionInfo &info) override;

    QList<BackendRecord> loadRecords(const QString &collectionId) override;
    bool loadRecordsOrError(const QString &collectionId,
                            QList<BackendRecord> &records,
                            QString &error) override;
    std::optional<BackendRecord> loadRecord(const QString &recordId) override;
    QString createRecord(const QString &collectionId,
                         const BackendRecord &record) override;
    bool updateRecord(const BackendRecord &record) override;
    bool deleteRecord(const QString &recordId) override;

    QList<BackendRecord> modifiedSince(const QString &collectionId,
                                       const QDateTime &since) override;
    QStringList deletedSince(const QString &collectionId,
                             const QDateTime &since) override;
    bool supportsDeleteTracking() const override { return true; }

    // Test configuration
    void setFailNext(FailurePoint point, int count = 1);
    void clearFailures();

    // Direct store access for test assertions
    QHash<QString, BackendRecord> recordsIn(const QString &collectionId) const;

Q_SIGNALS:
    void recordCreated(const QString &recordId);
    void recordUpdated(const QString &recordId);
    void recordDeleted(const QString &recordId);
    void errorOccurred(const QString &error);
    void progressUpdated(int current, int total, const QString &message);

private:
    bool consumeFailure(FailurePoint point, const QString &context);

    QHash<QString, CollectionInfo> m_collections;
    QHash<QString, QHash<QString, BackendRecord>> m_records; // collectionId -> recordId -> record
    QHash<QString, QString> m_recordCollection;              // recordId -> collectionId
    QHash<QString, QStringList> m_deleted;                   // collectionId -> deleted recordIds
    QHash<FailurePoint, int> m_failures;
};

} // namespace Kalburator::Sync

#endif
