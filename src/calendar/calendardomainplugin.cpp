#include "calendardomainplugin.h"

#include <QStringList>

#include "calendarplugin_writer.h"
#include "syncbackend.h"
#include "domainregistry.h"
#include "icalproperties.h"
#include "icalrecorddiffer.h"
#include "icalrecordmerger.h"
#include "recordwriter.h"
#include "transformationregistry.h"

// Bring non-conflicting names into scope. Note: we do NOT do
// `using namespace Kalburator::Shape` here because the struct Shape and
// the namespace Kalburator::Shape share the same unqualified name; Clang
// rejects unqualified `Shape` as a type in that situation.  Use explicit
// using-declarations instead.
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::PropertyCatalogue;
using Kalburator::Shape::RecordDiffer;
using Kalburator::Shape::RecordMerger;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::TransformationEdge;
using Kalburator::Shape::TransformationRegistry;
using Kalburator::Shape::IdentityStage;

namespace Kalburator::Calendar {

CalendarDomainPlugin::CalendarDomainPlugin(QObject *parent)
    : QObject(parent)
{
}

DomainId CalendarDomainPlugin::domain() const
{
    return DomainId{"calendar"};
}

Kalburator::Shape::Shape CalendarDomainPlugin::canonicalShape() const
{
    return { DomainId{"calendar"}, EncodingId{"ical"} };
}

QList<Kalburator::Shape::Shape> CalendarDomainPlugin::peerShapes() const
{
    return {};
}

PropertyCatalogue CalendarDomainPlugin::canonicalCatalogue() const
{
    return makeICalCatalogue();
}

PropertyCatalogue CalendarDomainPlugin::catalogueFor(
    const Kalburator::Shape::Shape& s) const
{
    if (s == canonicalShape())
        return makeICalCatalogue();
    return {};
}

std::unique_ptr<RecordDiffer> CalendarDomainPlugin::createCanonicalDiffer() const
{
    return std::make_unique<RecordDifferICal>();
}

std::unique_ptr<RecordMerger> CalendarDomainPlugin::createCanonicalMerger() const
{
    return std::make_unique<RecordMergerICal>();
}

void CalendarDomainPlugin::registerEdges(TransformationRegistry& registry)
{
    const auto canonical = canonicalShape();
    registry.registerShape(canonical, canonicalCatalogue());
    registry.declareCanonical(domain(), canonical);
    registry.registerEdge(TransformationEdge{
        canonical, canonical,
        LossProfile{},
        std::make_shared<IdentityStage>()
    });
}

int CalendarDomainPlugin::richnessRank(
    const Kalburator::Shape::Shape& s) const
{
    return s == canonicalShape() ? 10 : 0;
}

std::unique_ptr<Kalburator::Shape::RecordWriter>
CalendarDomainPlugin::createWriter(Kalburator::Sync::SyncBackend *backend) const
{
    return std::make_unique<Kalburator::Calendar::CalendarPluginWriter>(backend);
}

QVariantMap CalendarDomainPlugin::collectionProperties(
    Kalburator::Sync::SyncBackend *backend,
    const QString &collectionId) const
{
    if (!backend) return {};
    QVariantMap m;
    // Note: SyncBackend::calendarColor/Description take only calendarId.
    // For calendar, collectionId IS the calendarId (they're identical
    // in the engine's mapping representation). Match the existing
    // fetchCalendarProperties() pattern in syncengine.cpp.
    const QColor c = backend->calendarColor(collectionId);
    if (c.isValid()) m[QStringLiteral("color")] = c;
    const QString d = backend->calendarDescription(collectionId);
    if (!d.isEmpty()) m[QStringLiteral("description")] = d;
    return m;
}

void CalendarDomainPlugin::applyCollectionProperties(
    Kalburator::Sync::SyncBackend *backend,
    const QString &collectionId,
    const QVariantMap &props) const
{
    if (!backend || props.isEmpty()) return;
    backend->updateCalendar(collectionId, /*calendarId=*/collectionId, props);
}

QStringList CalendarDomainPlugin::baselineProperties() const
{
    return { QStringLiteral("color"), QStringLiteral("description") };
}

} // namespace Kalburator::Calendar

namespace {

struct CalendarPluginRegistrar {
    CalendarPluginRegistrar() {
        Kalburator::Shape::DomainRegistry::instance().registerDomain(
            std::make_shared<Kalburator::Calendar::CalendarDomainPlugin>());
    }
};

static CalendarPluginRegistrar s_calendarPluginRegistrar;

} // namespace
