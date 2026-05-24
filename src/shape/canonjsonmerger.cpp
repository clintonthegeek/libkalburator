#include "canonjsonmerger.h"

#include <QJsonObject>

#include "canonenvelope.h"
#include "conflictpolicy.h"

using Kalburator::Conflict::AutoResolveStrategy;

namespace Kalburator::Shape {

CanonJsonMerger::CanonJsonMerger(QString domain, QList<PropertyId> properties)
    : m_domain(std::move(domain)), m_properties(std::move(properties)) {}

CanonicalRecord CanonJsonMerger::merge(const CanonicalRecord& source,
                                       const CanonicalRecord& target,
                                       const CanonicalRecord& baseline,
                                       const Kalburator::Conflict::ConflictPolicy& policy) const
{
    const QJsonObject s = CanonEnvelope::parse(source.data);
    const QJsonObject t = CanonEnvelope::parse(target.data);
    const QJsonObject b = CanonEnvelope::parse(baseline.data);

    // Mirror RecordMergerVCard::srcWins: SourceAlwaysWins => source, TargetAlwaysWins
    // => target, all other strategies (None, NewerWins, OlderWins, ...) => source.
    // NewerWins falls back to source-wins here because the generic canon JSON merger
    // has no per-domain revision() field to compare (the vCard merger's NewerWins
    // path is domain-specific). No canon-level test exercises NewerWins field-merge.
    const bool preferSource =
        policy.autoResolve == AutoResolveStrategy::TargetAlwaysWins ? false : true;

    QJsonObject out = t;  // start from target; override per-property below
    bool tookSourceForAny = false;

    for (const PropertyId& id : m_properties) {
        const QString k = id.toString();
        const bool srcChanged = !CanonEnvelope::valuesEqual(s.value(k), b.value(k));
        const bool tgtChanged = !CanonEnvelope::valuesEqual(t.value(k), b.value(k));

        if (srcChanged && !tgtChanged) {
            out.insert(k, s.value(k));            // take source
            tookSourceForAny = true;
        } else if (!srcChanged && tgtChanged) {
            // keep target (already in out)
        } else if (srcChanged && tgtChanged) {
            if (preferSource) { out.insert(k, s.value(k)); tookSourceForAny = true; }
            // else keep target
        }
        // else neither changed — keep target value (== source == baseline).
    }

    // providerExtras follows the chosen origin (schema §5): if any property took
    // source, the record's provenance is source; else target.
    const QString peKey = CanonEnvelope::providerExtrasKey();
    const QJsonObject& origin = tookSourceForAny ? s : t;
    if (origin.contains(peKey))
        out.insert(peKey, origin.value(peKey));
    else
        out.remove(peKey);

    const QString mergedUid = CanonEnvelope::uid(t).isEmpty()
                                  ? CanonEnvelope::uid(s) : CanonEnvelope::uid(t);
    CanonEnvelope::stampEnvelope(out, m_domain, mergedUid);

    CanonicalRecord merged = target;
    merged.data = CanonEnvelope::serialize(out);
    return merged;
}

}  // namespace Kalburator::Shape
