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
        Kalburator::Sync::SyncBackend* parentBackend,
        QString parentCollectionId,
        QString virtualCollectionId,
        Kalburator::Shape::RecordFilter filter,
        Kalburator::Sync::BackendRegistry* registry,
        QString displayNameOverride,
        QObject* parent)
    : Kalburator::Sync::SyncBackend(parent)
    , m_parent(parentBackend)
    , m_parentBackendId(parentBackend ? parentBackend->backendId() : QString())
    , m_parentColId(std::move(parentCollectionId))
    , m_virtualColId(std::move(virtualCollectionId))
    , m_filter(std::move(filter))
    , m_displayNameOverride(std::move(displayNameOverride))
{
    Q_UNUSED(registry); // wired in Task 7
}

QString FilteredCollectionBackend::displayName() const
{
    if (!m_displayNameOverride.isEmpty())
        return m_displayNameOverride;
    if (!m_parent)
        return defaultComposedDisplayName(QString());
    const CollectionInfo info = const_cast<Kalburator::Sync::SyncBackend*>(m_parent)
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
    // Task 7 will tighten this to also follow parent's isAvailable.
    return m_parent != nullptr;
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
    const CollectionInfo parentInfo = const_cast<Kalburator::Sync::SyncBackend*>(m_parent)
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

QString FilteredCollectionBackend::resourceId() const
{
    // Implemented in Task 6.
    if (!m_parent) return QStringLiteral("filtered-view:?");
    return QStringLiteral("filtered-view:") + m_parent->resourceId()
         + QLatin1Char('/') + m_parentColId;
}

bool FilteredCollectionBackend::discoveredWritable(const QString& calendarId) const
{
    // Implemented in Task 5.
    Q_UNUSED(calendarId);
    return m_parent != nullptr;
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

QString FilteredCollectionBackend::createRecord(const QString& collectionId,
                                                const BackendRecord& record)
{
    Q_UNUSED(collectionId);
    Q_UNUSED(record);
    return {};  // implemented in Task 4
}

bool FilteredCollectionBackend::updateRecord(const BackendRecord& record)
{
    Q_UNUSED(record);
    return false;  // implemented in Task 4
}

bool FilteredCollectionBackend::deleteRecord(const QString& recordId)
{
    Q_UNUSED(recordId);
    return false;  // implemented in Task 5
}

} // namespace Kalburator::Sinks
