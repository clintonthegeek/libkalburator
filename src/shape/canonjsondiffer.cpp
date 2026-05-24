#include "canonjsondiffer.h"

#include <QJsonObject>

#include "canonenvelope.h"

namespace Kalburator::Shape {

CanonJsonDiffer::CanonJsonDiffer(QList<PropertyId> properties)
    : m_properties(std::move(properties))
{
}

QSet<PropertyId> CanonJsonDiffer::diff(const CanonicalRecord& source,
                                       const CanonicalRecord& baseline) const
{
    const QJsonObject s = CanonEnvelope::parse(source.data);
    const QJsonObject b = CanonEnvelope::parse(baseline.data);
    QSet<PropertyId> changed;
    for (const PropertyId& id : m_properties) {
        const QString k = id.toString();
        if (!CanonEnvelope::valuesEqual(s.value(k), b.value(k)))
            changed.insert(id);
    }
    return changed;
}

bool CanonJsonDiffer::equal(const CanonicalRecord& a, const CanonicalRecord& b) const
{
    const QJsonObject ja = CanonEnvelope::parse(a.data);
    const QJsonObject jb = CanonEnvelope::parse(b.data);
    for (const PropertyId& id : m_properties) {
        const QString k = id.toString();
        if (!CanonEnvelope::valuesEqual(ja.value(k), jb.value(k)))
            return false;
    }
    return true;
}

}  // namespace Kalburator::Shape
