#include "outlinediffer.h"
#include "canonenvelope.h"

#include <QJsonObject>

using namespace Kalburator::Shape;

namespace Kalburator::Outline {

namespace {
const char* const kProps[] = { "title", "created", "lastModified", "attributes", "children" };
}

QSet<PropertyId> OutlineDiffer::diff(const CanonicalRecord& source,
                                     const CanonicalRecord& baseline) const
{
    if (source.data == baseline.data)
        return {};

    const QJsonObject a = CanonEnvelope::parse(source.data);
    const QJsonObject b = CanonEnvelope::parse(baseline.data);
    QSet<PropertyId> changed;
    for (const char* k : kProps) {
        if (!CanonEnvelope::valuesEqual(a.value(k), b.value(k)))
            changed.insert(PropertyId{QString::fromLatin1(k)});
    }
    return changed;
}

bool OutlineDiffer::equal(const CanonicalRecord& a, const CanonicalRecord& b) const
{
    if (a.data == b.data)
        return true;
    return diff(a, b).isEmpty();
}

}  // namespace Kalburator::Outline
