#include "filteredcollectionbackend.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrl>

#include "backendregistry.h"  // Kalburator::Sync::BackendRegistry

namespace Kalburator::Sinks {

using Kalburator::Shape::Shape;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;

FilteredCollectionBackend::FilteredCollectionBackend(
        Kalburator::Sync::SyncBackendBase* parentBackend,
        QString parentBackendId,
        QString parentCollectionId,
        QString virtualCollectionId,
        Kalburator::Shape::RecordFilter filter,
        Kalburator::Sync::BackendRegistry* registry,
        QString displayNameOverride,
        QObject* parent)
    : Kalburator::Sync::SyncBackendBase(parent)
    , m_parent(parentBackend)
    , m_parentBackendId(std::move(parentBackendId))
    , m_parentColId(std::move(parentCollectionId))
    , m_virtualColId(std::move(virtualCollectionId))
    , m_filter(std::move(filter))
    , m_displayNameOverride(std::move(displayNameOverride))
{
    if (registry && !m_parentBackendId.isEmpty()) {
        // BackendRegistry mutation is assumed single-threaded (same thread as
        // this FCB). DirectConnection makes the assumption explicit: any cross-
        // thread emit would fire the lambda inline on the emitter's thread,
        // surfacing the contract violation rather than queueing silently.
        connect(registry, &Kalburator::Sync::BackendRegistry::backendInstanceUnregistered,
                this, [this](const QString& backendId) {
            if (backendId == m_parentBackendId) {
                m_parent = nullptr;
            }
        }, Qt::DirectConnection);
    }
}

QString FilteredCollectionBackend::displayName() const
{
    if (!m_displayNameOverride.isEmpty())
        return m_displayNameOverride;
    if (!m_parent)
        return defaultComposedDisplayName(QString());
    const CollectionInfo info = const_cast<Kalburator::Sync::SyncBackendBase*>(m_parent)
                                    ->collectionInfo(m_parentColId);
    return defaultComposedDisplayName(info.name);
}

QString FilteredCollectionBackend::filterDescription() const
{
    using Op = Kalburator::Shape::RecordFilter::Op;
    const QString prop = m_filter.property.toString();
    const QString val  = m_filter.value.toString();
    if (m_filter.op == Op::Contains)
        return QStringLiteral("%1 ∋ %2").arg(prop, val);  // " ∋ "
    return QStringLiteral("%1 = %2").arg(prop, val);
}

QString FilteredCollectionBackend::defaultComposedDisplayName(const QString& parentName) const
{
    if (parentName.isEmpty())
        return QStringLiteral("[%1]").arg(filterDescription());
    return QStringLiteral("%1 [%2]").arg(parentName, filterDescription());
}

bool FilteredCollectionBackend::isAvailable() const
{
    return m_parent != nullptr && m_parent->isAvailable();
}

QList<Shape> FilteredCollectionBackend::nativeShapes() const
{
    if (!m_parent) return {};
    return { m_parent->shapeFor(m_parentColId) };
}

Shape FilteredCollectionBackend::shapeFor(const QString& collectionId) const
{
    if (!m_parent) return Shape::Any();
    if (collectionId != m_virtualColId) return Shape::Any();
    return m_parent->shapeFor(m_parentColId);
}

CollectionInfo FilteredCollectionBackend::composeCollectionInfo() const
{
    CollectionInfo out;
    if (!m_parent) {
        out.id = m_virtualColId;
        out.name = displayName();
        return out;
    }
    const CollectionInfo parentInfo = const_cast<Kalburator::Sync::SyncBackendBase*>(m_parent)
                                          ->collectionInfo(m_parentColId);
    out = parentInfo;             // inherit type, color (if any), readOnly, contentTypes
    out.id = m_virtualColId;
    out.name = displayName();
    return out;
}

QList<CollectionInfo> FilteredCollectionBackend::availableCollections()
{
    return { composeCollectionInfo() };
}

CollectionInfo FilteredCollectionBackend::collectionInfo(const QString& collectionId)
{
    if (collectionId != m_virtualColId) return CollectionInfo{};
    return composeCollectionInfo();
}

QByteArray FilteredCollectionBackend::canonJsonOfValue(const QVariant& value)
{
    // QJsonDocument cannot serialize scalars at the root; wrap in a
    // single-element array, serialize compactly (keys auto-sorted for
    // objects), and strip the brackets.
    QJsonArray wrapper;
    wrapper.append(QJsonValue::fromVariant(value));
    const QByteArray bytes = QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
    // bytes looks like "[\"Work\"]" — strip outer [ and ].
    if (bytes.size() >= 2 && bytes.startsWith('[') && bytes.endsWith(']'))
        return bytes.mid(1, bytes.size() - 2);
    return bytes;
}

QString FilteredCollectionBackend::opToken(Kalburator::Shape::RecordFilter::Op op)
{
    using Op = Kalburator::Shape::RecordFilter::Op;
    switch (op) {
    case Op::Contains: return QStringLiteral("contains");
    case Op::Equals:   return QStringLiteral("equals");
    }
    Q_UNREACHABLE_RETURN(QStringLiteral("unknown"));
}

QString FilteredCollectionBackend::resourceId() const
{
    const QString parentRes = m_parent ? m_parent->resourceId() : QString();
    const QString encodedProp  = QString::fromUtf8(
        QUrl::toPercentEncoding(m_filter.property.toString()));
    const QString encodedColId = QString::fromUtf8(
        QUrl::toPercentEncoding(m_parentColId));
    const QString encodedValue = QString::fromUtf8(
        QUrl::toPercentEncoding(QString::fromUtf8(canonJsonOfValue(m_filter.value))));
    return QStringLiteral("filtered-view:%1/%2?p=%3&op=%4&v=%5")
        .arg(parentRes,
             encodedColId,
             encodedProp,
             opToken(m_filter.op),
             encodedValue);
}

bool FilteredCollectionBackend::discoveredWritable(const QString& collectionId) const
{
    if (!m_parent) return false;
    if (collectionId != m_virtualColId) return false;
    return m_parent->discoveredWritable(m_parentColId);
}

QList<BackendRecord> FilteredCollectionBackend::loadRecords(const QString& collectionId)
{
    if (!m_parent || collectionId != m_virtualColId) return {};
    QList<BackendRecord> all = m_parent->loadRecords(m_parentColId);
    QList<BackendRecord> filtered;
    filtered.reserve(all.size());
    for (int i = 0; i < all.size(); ++i) {
        if (m_filter.matches(all[i].data))
            filtered.append(std::move(all[i]));
    }
    filtered.squeeze();
    return filtered;
}

std::optional<BackendRecord> FilteredCollectionBackend::loadRecord(const QString& recordId)
{
    if (!m_parent) return std::nullopt;
    auto rec = m_parent->loadRecord(recordId);
    if (!rec.has_value()) return std::nullopt;
    if (!m_filter.matches(rec->data)) return std::nullopt;
    return rec;
}

QByteArray FilteredCollectionBackend::stampFilterValue(const QByteArray& payload) const
{
    using Op = Kalburator::Shape::RecordFilter::Op;
    QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject())
        return payload;
    QJsonObject obj = doc.object();
    const QString key = m_filter.property.toString();
    if (key.isEmpty())
        return payload;

    const QJsonValue filterValue = QJsonValue::fromVariant(m_filter.value);
    switch (m_filter.op) {
    case Op::Contains: {
        QJsonArray arr = obj.value(key).toArray();
        bool found = false;
        for (const QJsonValue& v : arr) {
            if (v == filterValue) { found = true; break; }
        }
        if (!found) arr.append(filterValue);
        obj.insert(key, arr);
        break;
    }
    case Op::Equals:
        obj.insert(key, filterValue);
        break;
    }
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QString FilteredCollectionBackend::createRecord(const QString& collectionId,
                                                const BackendRecord& record)
{
    if (!m_parent || collectionId != m_virtualColId) return {};
    BackendRecord stamped = record;
    stamped.data = stampFilterValue(record.data);
    return m_parent->createRecord(m_parentColId, stamped);
}

bool FilteredCollectionBackend::updateRecord(const BackendRecord& record)
{
    if (!m_parent) return false;
    BackendRecord stamped = record;
    stamped.data = stampFilterValue(record.data);
    return m_parent->updateRecord(stamped);
}

bool FilteredCollectionBackend::deleteRecord(const QString& recordId)
{
    if (!m_parent) return false;
    return m_parent->deleteRecord(recordId);
}

// ---- Sync::ChangeDetection ----
// All four delegate to the parent, translating the virtual collection id to
// m_parentColId. Guarded for a null parent (post-unregister) and for a parent
// that doesn't implement ChangeDetection — both return the "can't answer"
// values, which the engine treats as changed (so the view re-syncs).

QString FilteredCollectionBackend::collectionRevision(const QString& collectionId)
{
    if (collectionId != m_virtualColId) return {};
    auto* cd = parentChangeDetection();
    return cd ? cd->collectionRevision(m_parentColId) : QString();
}

void FilteredCollectionBackend::collectionRevisionsAsync(
    const QStringList& collectionIds,
    std::function<void(QMap<QString, QString>)> done)
{
    auto* cd = parentChangeDetection();
    if (!cd) {
        // Parent gone or not a ChangeDetection: "can't answer" (empty map) —
        // engine treats the view as changed. Matches the singular override.
        done({});
        return;
    }
    // A filtered view changes iff its parent collection changes, so query the
    // parent for m_parentColId and stamp the answer under every requested
    // virtual id (only m_virtualColId is ever really ours; the default plural
    // loop over the singular override behaves identically, just synchronously).
    cd->collectionRevisionsAsync(
        {m_parentColId},
        [collectionIds, virtualId = m_virtualColId, parentId = m_parentColId,
         done = std::move(done)](QMap<QString, QString> revs) {
            QMap<QString, QString> out;
            const QString rev = revs.value(parentId);
            if (!rev.isEmpty()) {
                for (const QString& id : collectionIds) {
                    if (id == virtualId)
                        out.insert(id, rev);
                }
            }
            done(out);
        });
}

QString FilteredCollectionBackend::cachedCollectionRevision(const QString& collectionId) const
{
    if (collectionId != m_virtualColId) return {};
    auto* cd = parentChangeDetection();
    return cd ? cd->cachedCollectionRevision(m_parentColId) : QString();
}

bool FilteredCollectionBackend::persistsCollectionRevisions() const
{
    auto* cd = parentChangeDetection();
    return cd ? cd->persistsCollectionRevisions() : true;
}

} // namespace Kalburator::Sinks
