#include "calendardomainoperations.h"

#include <QColor>

#include "calendarplugin_writer.h"
#include "syncbackend.h"

using Kalburator::Shape::DomainId;

namespace Kalburator::Calendar {

DomainId CalendarDomainOperations::targetDomain() const
{
    return DomainId{QStringLiteral("calendar")};
}

std::unique_ptr<Kalburator::Shape::RecordWriter>
CalendarDomainOperations::createWriter(Kalburator::Sync::SyncBackendBase *backend) const
{
    auto *syncBackend = qobject_cast<Kalburator::Sync::SyncBackend *>(backend);
    if (!syncBackend) return nullptr;
    return std::make_unique<Kalburator::Calendar::CalendarPluginWriter>(syncBackend);
}

QVariantMap CalendarDomainOperations::collectionProperties(
    Kalburator::Sync::SyncBackendBase *backend,
    const QString &collectionId) const
{
    auto *syncBackend = qobject_cast<Kalburator::Sync::SyncBackend *>(backend);
    if (!syncBackend) return {};
    QVariantMap m;
    const QColor c = syncBackend->calendarColor(collectionId);
    if (c.isValid()) m[QStringLiteral("color")] = c;
    const QString d = syncBackend->calendarDescription(collectionId);
    if (!d.isEmpty()) m[QStringLiteral("description")] = d;
    return m;
}

void CalendarDomainOperations::applyCollectionProperties(
    Kalburator::Sync::SyncBackendBase *backend,
    const QString &collectionId,
    const QVariantMap &props) const
{
    auto *syncBackend = qobject_cast<Kalburator::Sync::SyncBackend *>(backend);
    if (!syncBackend || props.isEmpty()) return;
    syncBackend->updateCalendar(collectionId, collectionId, props);
}

} // namespace Kalburator::Calendar
