#include "icalrecordmerger.h"

#include "incidencediff.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Incidence>

using namespace Kalburator::Shape;
using namespace Kalburator::Sync;
using namespace Kalburator::Conflict;

namespace {

KCalendarCore::Incidence::Ptr parseIcal(const QByteArray& data)
{
    if (data.isEmpty())
        return {};
    KCalendarCore::ICalFormat fmt;
    return fmt.fromString(QString::fromUtf8(data));
}

QByteArray serializeIcal(const KCalendarCore::Incidence::Ptr& incidence)
{
    if (!incidence)
        return {};
    KCalendarCore::ICalFormat fmt;
    return fmt.toICalString(incidence).toUtf8();
}

} // namespace

namespace Kalburator::Calendar {

CanonicalRecord RecordMergerICal::merge(
    const CanonicalRecord& source,
    const CanonicalRecord& target,
    const CanonicalRecord& baseline,
    const ConflictPolicy& policy) const
{
    const auto srcInc  = parseIcal(source.data);
    const auto tgtInc  = parseIcal(target.data);
    const auto baseInc = parseIcal(baseline.data);

    // Edge cases: missing sides
    if (!srcInc && !tgtInc) {
        // Both unparseable — return baseline as-is
        return baseline;
    }
    if (!srcInc) {
        return target;
    }
    if (!tgtInc) {
        return source;
    }

    // Choose merge base: prefer baseline, fall back to source
    const KCalendarCore::Incidence::Ptr mergeBase = baseInc ? baseInc : srcInc;

    // Compute per-property diffs (3-way if baseline present, 2-way otherwise)
    QList<PropertyDiff> diffs = IncidenceDiff::compare(srcInc, tgtInc, baseInc);

    // Resolve each diff
    for (auto& d : diffs) {
        switch (d.state) {
        case PropertyDiff::Identical:
        case PropertyDiff::BothChangedSame:
            // Either side is fine; pick source (A)
            d.resolution = PropertyDiff::UseA;
            break;

        case PropertyDiff::AMatchesBaseline:
            // Only target (B) changed — take target
            d.resolution = PropertyDiff::UseB;
            break;

        case PropertyDiff::BMatchesBaseline:
            // Only source (A) changed — take source
            d.resolution = PropertyDiff::UseA;
            break;

        case PropertyDiff::OnlyInA:
            d.resolution = PropertyDiff::UseA;
            break;

        case PropertyDiff::OnlyInB:
            d.resolution = PropertyDiff::UseB;
            break;

        case PropertyDiff::BothDifferent:
        case PropertyDiff::BothChangedDifferent: {
            // True conflict — consult policy
            switch (policy.autoResolve) {
            case AutoResolveStrategy::SourceAlwaysWins:
                d.resolution = PropertyDiff::UseA;
                break;
            case AutoResolveStrategy::TargetAlwaysWins:
                d.resolution = PropertyDiff::UseB;
                break;
            case AutoResolveStrategy::NewerWins: {
                // Compare lastModified; source wins on tie
                const QDateTime srcMod = srcInc->lastModified();
                const QDateTime tgtMod = tgtInc->lastModified();
                d.resolution = (srcMod >= tgtMod)
                    ? PropertyDiff::UseA
                    : PropertyDiff::UseB;
                break;
            }
            default:
                // Leave Unresolved — merge() will keep baseline value
                d.resolution = PropertyDiff::Unresolved;
                break;
            }
            break;
        }
        } // switch d.state
    }

    // Remove unresolved diffs so merge() skips them (baseline value kept)
    QList<PropertyDiff> resolvedDiffs;
    resolvedDiffs.reserve(diffs.size());
    for (const auto& d : diffs) {
        if (d.resolution != PropertyDiff::Unresolved)
            resolvedDiffs.append(d);
    }

    // If nothing to merge, return the baseline (or source if no baseline)
    if (resolvedDiffs.isEmpty()) {
        CanonicalRecord result;
        result.shape    = baseline.shape;
        result.recordId = baseline.recordId;
        result.data     = baseline.data.isEmpty() ? source.data : baseline.data;
        return result;
    }

    const KCalendarCore::Incidence::Ptr merged =
        IncidenceDiff::merge(mergeBase, resolvedDiffs);

    CanonicalRecord result;
    result.shape    = baseline.shape;
    result.recordId = baseline.recordId;
    result.data     = serializeIcal(merged);
    return result;
}

} // namespace Kalburator::Calendar
