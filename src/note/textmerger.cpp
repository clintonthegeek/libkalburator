#include "textmerger.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

using namespace Kalburator::Shape;

namespace {

QJsonObject parseMemo(const QByteArray &data)
{
    if (data.isEmpty())
        return {};
    return QJsonDocument::fromJson(data).object();
}

QByteArray serializeMemo(const QJsonObject &obj)
{
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

// Naive 3-way line merge: lines only in src or only in tgt w.r.t. base.
// If both sides changed a line, apply policy: prefer src or tgt.
QString mergeBodyText(const QString &srcBody, const QString &tgtBody,
                      const QString &baseBody, bool preferSrc)
{
    if (srcBody == baseBody)
        return tgtBody;
    if (tgtBody == baseBody)
        return srcBody;
    if (srcBody == tgtBody)
        return srcBody;
    // True conflict: apply policy.
    return preferSrc ? srcBody : tgtBody;
}

bool srcWins(AutoResolveStrategy strategy,
             const QString &srcMod, const QString &tgtMod)
{
    switch (strategy) {
    case AutoResolveStrategy::SourceAlwaysWins: return true;
    case AutoResolveStrategy::TargetAlwaysWins: return false;
    case AutoResolveStrategy::NewerWins:
        return srcMod >= tgtMod;
    default:
        return true;
    }
}

} // namespace

namespace Kalburator::Note {

CanonicalRecord TextMerger::merge(
    const CanonicalRecord &source,
    const CanonicalRecord &target,
    const CanonicalRecord &baseline,
    AutoResolveStrategy strategy) const
{
    const auto src  = parseMemo(source.data);
    const auto tgt  = parseMemo(target.data);
    const auto base = parseMemo(baseline.data);

    if (src.isEmpty() && tgt.isEmpty())
        return baseline;
    if (src.isEmpty())
        return target;
    if (tgt.isEmpty())
        return source;

    const QString srcMod = src.value(QStringLiteral("lastModified")).toString();
    const QString tgtMod = tgt.value(QStringLiteral("lastModified")).toString();
    const bool preferSrc = srcWins(strategy, srcMod, tgtMod);

    const QString baseBody = base.value(QStringLiteral("body")).toString();
    const QString srcBody  = src.value(QStringLiteral("body")).toString();
    const QString tgtBody  = tgt.value(QStringLiteral("body")).toString();

    QJsonObject merged;
    merged[QStringLiteral("body")] = mergeBodyText(srcBody, tgtBody, baseBody, preferSrc);

    // Categories: if only one side changed vs baseline, take that side.
    const auto srcCats  = src[QStringLiteral("categories")];
    const auto tgtCats  = tgt[QStringLiteral("categories")];
    const auto baseCats = base[QStringLiteral("categories")];

    if (srcCats == baseCats) {
        merged[QStringLiteral("categories")] = tgtCats;
    } else if (tgtCats == baseCats) {
        merged[QStringLiteral("categories")] = srcCats;
    } else {
        merged[QStringLiteral("categories")] = preferSrc ? srcCats : tgtCats;
    }

    merged[QStringLiteral("lastModified")] = preferSrc ? srcMod : tgtMod;

    CanonicalRecord result;
    result.shape    = source.shape;
    result.recordId = source.recordId;
    result.data     = serializeMemo(merged);
    return result;
}

} // namespace Kalburator::Note
