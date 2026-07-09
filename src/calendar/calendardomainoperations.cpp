#include "calendardomainoperations.h"

#include <QColor>
#include <QDebug>

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
    // E11 (audit B7 / FINDINGS O39): this method's caller (SyncEngine's
    // per-mapping property-sync step) already marshals it onto the
    // backend's own thread before calling in; the old synchronous
    // updateCalendar() then spun davSyncRequest's nested QEventLoop THERE —
    // the B7 hazard. updateCalendarAsync's continuation lands on this same
    // thread's normal event loop instead, so no blocking wrapper is needed
    // here: this method was always fire-and-forget from the caller's
    // perspective (void, result never consumed), and stays that way.
    syncBackend->updateCalendarAsync(collectionId, collectionId, props,
        [collectionId](bool ok) {
            if (!ok) {
                qWarning() << "CalendarDomainOperations::applyCollectionProperties:"
                           << "updateCalendarAsync failed for" << collectionId;
            }
        });
}

} // namespace Kalburator::Calendar
