#include "textdiffer.h"

#include <QJsonDocument>
#include <QJsonObject>

using namespace Kalburator::Shape;

namespace {

// CanonicalRecord.data for the memo domain is a JSON object:
// {"body":"<text>","categories":["a","b"],"lastModified":"<iso>"}
QJsonObject parseMemo(const QByteArray &data)
{
    if (data.isEmpty())
        return {};
    return QJsonDocument::fromJson(data).object();
}

} // namespace

namespace Kalburator::Memo {

QSet<PropertyId> TextDiffer::diff(
    const CanonicalRecord &source,
    const CanonicalRecord &baseline) const
{
    if (source.data == baseline.data)
        return {};

    const auto src  = parseMemo(source.data);
    const auto base = parseMemo(baseline.data);

    QSet<PropertyId> changed;

    if (src["body"] != base["body"])
        changed.insert(PropertyId{"body"});
    if (src["categories"] != base["categories"])
        changed.insert(PropertyId{"categories"});
    if (src["lastModified"] != base["lastModified"])
        changed.insert(PropertyId{"lastmodified"});

    return changed;
}

bool TextDiffer::equal(
    const CanonicalRecord &a,
    const CanonicalRecord &b) const
{
    if (a.data == b.data)
        return true;
    return diff(a, b).isEmpty();
}

} // namespace Kalburator::Memo
