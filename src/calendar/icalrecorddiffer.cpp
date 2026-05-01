#include "icalrecorddiffer.h"

#include "incidencediff.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Incidence>

using namespace Kalburator::Shape;
using Kalburator::Sync::IncidenceDiff;
using Kalburator::Sync::PropertyDiff;

namespace {

KCalendarCore::Incidence::Ptr parseIcal(const QByteArray& data)
{
    if (data.isEmpty())
        return {};
    KCalendarCore::ICalFormat fmt;
    return fmt.fromString(QString::fromUtf8(data));
}

} // namespace

namespace Kalburator::Calendar {

QSet<PropertyId> IRecordDifferICal::diff(
    const CanonicalRecord& source,
    const CanonicalRecord& baseline) const
{
    const auto srcInc  = parseIcal(source.data);
    const auto baseInc = parseIcal(baseline.data);

    if (!srcInc && !baseInc)
        return {};

    QSet<PropertyId> changed;

    if (!srcInc || !baseInc) {
        // One side is missing/unparseable — treat all catalogue properties changed.
        changed.insert(PropertyId{"uid"});
        changed.insert(PropertyId{"summary"});
        return changed;
    }

    const QList<PropertyDiff> diffs = IncidenceDiff::compare(srcInc, baseInc);
    for (const auto& d : diffs) {
        if (d.state != PropertyDiff::Identical) {
            changed.insert(PropertyId{ d.propertyName.toLower() });
        }
    }
    return changed;
}

bool IRecordDifferICal::equal(const CanonicalRecord& a,
                               const CanonicalRecord& b) const
{
    if (a.data == b.data)
        return true;

    const auto incA = parseIcal(a.data);
    const auto incB = parseIcal(b.data);

    if (!incA && !incB)
        return true;
    if (!incA || !incB)
        return false;

    const QList<PropertyDiff> diffs = IncidenceDiff::compare(incA, incB);
    return diffs.isEmpty();
}

} // namespace Kalburator::Calendar
