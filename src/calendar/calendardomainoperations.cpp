#include "calendardomainoperations.h"

#include <QColor>

#include "recordwriter.h"
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
    // Calendar uses the uniform DefaultBlobWriter record path. Returning nullptr
    // signals the engine to build it (O15 convergence). Type-aware work (typed
    // diff, loss model, conflict detection) is upstream in the canon/diff/merge
    // layer, not the writer.
    Q_UNUSED(backend);
    return nullptr;
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
