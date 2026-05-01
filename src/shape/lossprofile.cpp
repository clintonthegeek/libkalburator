#include "lossprofile.h"

#include <QStringList>
#include <algorithm>

namespace Kalburator::Shape {

LossProfile LossProfile::compose(const LossProfile& downstream) const {
    LossProfile out;
    out.level = std::max(level, downstream.level);
    out.dropped = dropped;
    out.dropped.unite(downstream.dropped);
    return out;
}

namespace {
QString levelName(LossLevel l) {
    switch (l) {
    case LossLevel::Lossless:              return QStringLiteral("lossless");
    case LossLevel::IntraDomainLossy:      return QStringLiteral("intra-lossy");
    case LossLevel::InterDomainProjection: return QStringLiteral("inter-projection");
    case LossLevel::Degenerate:            return QStringLiteral("degenerate");
    }
    return QStringLiteral("unknown");
}
}  // namespace

QString LossProfile::summary() const {
    if (isLossless() && dropped.isEmpty()) {
        return QStringLiteral("lossless");
    }
    QStringList names;
    names.reserve(dropped.size());
    for (const auto& p : dropped) {
        names.append(p.toString());
    }
    std::sort(names.begin(), names.end());
    if (names.isEmpty()) {
        return levelName(level);
    }
    return levelName(level) + QStringLiteral(": drops ") + names.join(QStringLiteral(", "));
}

}  // namespace Kalburator::Shape
