#include "canonjsonmerger.h"

#include <QDebug>
#include <QJsonObject>

#include "canonenvelope.h"

namespace Kalburator::Shape {

CanonJsonMerger::CanonJsonMerger(QString domain, QList<PropertyId> properties)
    : m_domain(std::move(domain)), m_properties(std::move(properties)) {}

CanonicalRecord CanonJsonMerger::merge(const CanonicalRecord& source,
                                       const CanonicalRecord& target,
                                       const CanonicalRecord& baseline,
                                       AutoResolveStrategy strategy) const
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
        strategy == AutoResolveStrategy::TargetAlwaysWins ? false : true;

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

    // O84 fix: thread the component kind through the re-stamp instead of
    // dropping it. The 3-arg stampEnvelope() this used to call builds a
    // FRESH _canon object and inserts `kind` only when given one — calling
    // it with no kind argument therefore ERASED whatever kind the record
    // arrived with, so a merged {calendar,canon} VTODO/VJOURNAL demoted as
    // a VEVENT (CanonToICalStage treats an absent kind as vevent for v1
    // back-compat — icalcanonstages.cpp).
    //
    // Agreement (or only one side carrying a kind) is the easy case:
    // target's kind wins when present, else source's — mirroring
    // mergedUid's own target-preferred-with-source-fallback rule directly
    // above.
    //
    // Disagreement — source and target both carry a NON-EMPTY kind and it
    // differs — is not an ordinary merge choice: the same uid promoted to
    // two different iCalendar component types (a VEVENT on one side, a
    // VTODO on the other) means something upstream is already wrong, the
    // same class of problem O55 named "identity conflicts" and treated as
    // fail-loud rather than silently resolved. A genuine abort-the-sync
    // fail-loud would need an error channel threaded through
    // RecordMerger::merge()'s return type (CanonicalRecord, unconditional)
    // and onward through the engine's merge stage — a RecordMerger
    // interface change, out of IP.3's scope (this item touches the
    // catalogue/envelope seam, not the merger interface or engine dispatch
    // chain). So: loud, not silent, and not "fail" in the interface sense
    // it correctly is for O55 — qWarning() making the corruption visible
    // in logs, then a deliberate precedence rule (target's kind wins,
    // consistent with this function's target-primary bias: `out = t`
    // above) rather than an unannounced pick. See FINDINGS.md O84 and the
    // IP.3 return receipt for the follow-up recommendation (a proper
    // fail-loud engine-level guard, not built here).
    const QString sourceKind = CanonEnvelope::kind(s);
    const QString targetKind = CanonEnvelope::kind(t);
    QString mergedKind;
    if (!sourceKind.isEmpty() && !targetKind.isEmpty() && sourceKind != targetKind) {
        qWarning() << "CanonJsonMerger::merge: kind mismatch for uid" << mergedUid
                   << "in domain" << m_domain << "- source kind" << sourceKind
                   << "target kind" << targetKind
                   << "- an identity conflict (O84); keeping target's kind";
        mergedKind = targetKind;
    } else {
        mergedKind = !targetKind.isEmpty() ? targetKind : sourceKind;
    }
    CanonEnvelope::stampEnvelope(out, m_domain, mergedUid, mergedKind);

    CanonicalRecord merged = target;
    merged.data = CanonEnvelope::serialize(out);
    return merged;
}

}  // namespace Kalburator::Shape
