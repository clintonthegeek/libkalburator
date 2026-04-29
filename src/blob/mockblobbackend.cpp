#include "mockblobbackend.h"

namespace Kalburator::Sync {

MockBlobBackend::MockBlobBackend(QObject *parent)
    : QObject(parent)
{
}

MockBlobBackend::~MockBlobBackend() = default;

bool MockBlobBackend::consumeFailure(FailurePoint point, const QString &context)
{
    auto it = m_failures.find(point);
    if (it == m_failures.end() || it.value() <= 0) {
        return false;
    }
    --(it.value());
    if (it.value() <= 0) {
        m_failures.erase(it);
    }
    Q_EMIT errorOccurred(QStringLiteral("injected failure: %1").arg(context));
    return true;
}

QList<CollectionInfo> MockBlobBackend::availableCollections()
{
    return m_collections.values();
}

CollectionInfo MockBlobBackend::collectionInfo(const QString &collectionId)
{
    return m_collections.value(collectionId);
}

QString MockBlobBackend::createCollection(const CollectionInfo &info)
{
    if (consumeFailure(FailurePoint::OnCreateCollection, QStringLiteral("createCollection"))) {
        return {};
    }
    if (info.id.isEmpty()) {
        return {};
    }
    m_collections.insert(info.id, info);
    return info.id;
}

QList<BackendRecord> MockBlobBackend::loadRecords(const QString &collectionId)
{
    if (consumeFailure(FailurePoint::OnLoadRecords, QStringLiteral("loadRecords"))) {
        return {};
    }
    return m_records.value(collectionId).values();
}

std::optional<BackendRecord> MockBlobBackend::loadRecord(const QString &recordId)
{
    if (consumeFailure(FailurePoint::OnLoadRecord, QStringLiteral("loadRecord"))) {
        return std::nullopt;
    }
    const QString cid = m_recordCollection.value(recordId);
    if (cid.isEmpty()) {
        return std::nullopt;
    }
    const auto &bucket = m_records.value(cid);
    auto it = bucket.constFind(recordId);
    if (it == bucket.constEnd()) {
        return std::nullopt;
    }
    return it.value();
}

QString MockBlobBackend::createRecord(const QString &collectionId,
                                      const BackendRecord &record)
{
    if (consumeFailure(FailurePoint::OnCreateRecord, QStringLiteral("createRecord"))) {
        return {};
    }
    if (record.id.isEmpty() || collectionId.isEmpty()) {
        return {};
    }
    m_records[collectionId].insert(record.id, record);
    m_recordCollection.insert(record.id, collectionId);
    Q_EMIT recordCreated(record.id);
    return record.id;
}

bool MockBlobBackend::updateRecord(const BackendRecord &record)
{
    if (consumeFailure(FailurePoint::OnUpdateRecord, QStringLiteral("updateRecord"))) {
        return false;
    }
    const QString cid = m_recordCollection.value(record.id);
    if (cid.isEmpty()) {
        return false;
    }
    m_records[cid].insert(record.id, record);
    Q_EMIT recordUpdated(record.id);
    return true;
}

bool MockBlobBackend::deleteRecord(const QString &recordId)
{
    if (consumeFailure(FailurePoint::OnDeleteRecord, QStringLiteral("deleteRecord"))) {
        return false;
    }
    const QString cid = m_recordCollection.take(recordId);
    if (cid.isEmpty()) {
        return false;
    }
    if (!m_records[cid].remove(recordId)) {
        return false;
    }
    m_deleted[cid].append(recordId);
    Q_EMIT recordDeleted(recordId);
    return true;
}

QList<BackendRecord> MockBlobBackend::modifiedSince(const QString &collectionId,
                                                    const QDateTime &since)
{
    if (consumeFailure(FailurePoint::OnModifiedSince, QStringLiteral("modifiedSince"))) {
        return {};
    }
    QList<BackendRecord> out;
    for (const auto &r : m_records.value(collectionId)) {
        if (!since.isValid() || r.lastModified >= since) {
            out.append(r);
        }
    }
    return out;
}

QStringList MockBlobBackend::deletedSince(const QString &collectionId,
                                          const QDateTime &since)
{
    Q_UNUSED(since);
    return m_deleted.value(collectionId);
}

void MockBlobBackend::setFailNext(FailurePoint point, int count)
{
    if (count <= 0 || point == FailurePoint::None) {
        return;
    }
    m_failures[point] = count;
}

void MockBlobBackend::clearFailures()
{
    m_failures.clear();
}

QHash<QString, BackendRecord> MockBlobBackend::recordsIn(const QString &collectionId) const
{
    return m_records.value(collectionId);
}

} // namespace Kalburator::Sync
