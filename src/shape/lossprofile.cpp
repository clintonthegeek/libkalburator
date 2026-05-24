#include "lossprofile.h"

#include <QStringList>

namespace Kalburator::Shape {

int lossKindSeverity(LossKind k) noexcept {
    switch (k) {
        case LossKind::Reversible: return 0;
        case LossKind::Degraded:   return 1;
        case LossKind::Simplified: return 2;
        case LossKind::Dropped:    return 3;
    }
    return 0;
}

LossProfile LossProfile::compose(const LossProfile& downstream) const {
    LossProfile out;
    out.affected = affected;
    for (auto it = downstream.affected.constBegin(); it != downstream.affected.constEnd(); ++it) {
        const auto existing = out.affected.constFind(it.key());
        if (existing == out.affected.constEnd()
            || lossKindSeverity(it.value()) > lossKindSeverity(existing.value())) {
            out.affected.insert(it.key(), it.value());
        }
    }
    return out;
}

QSet<PropertyId> LossProfile::droppedProperties() const {
    QSet<PropertyId> s;
    for (auto it = affected.constBegin(); it != affected.constEnd(); ++it)
        if (it.value() == LossKind::Dropped) s.insert(it.key());
    return s;
}

QString LossProfile::summary() const {
    if (affected.isEmpty()) return QStringLiteral("lossless");
    QStringList drop, simp, rev, deg;
    for (auto it = affected.constBegin(); it != affected.constEnd(); ++it) {
        const QString id = it.key().toString();
        switch (it.value()) {
            case LossKind::Dropped:    drop << id; break;
            case LossKind::Simplified: simp << id; break;
            case LossKind::Reversible: rev  << id; break;
            case LossKind::Degraded:   deg  << id; break;
        }
    }
    QStringList parts;
    const auto add = [&](const QString& verb, QStringList& l) {
        if (!l.isEmpty()) { l.sort(); parts << verb + QStringLiteral(" ") + l.join(QStringLiteral(", ")); }
    };
    add(QStringLiteral("drops"), drop);
    add(QStringLiteral("simplifies"), simp);
    add(QStringLiteral("stashes"), rev);
    add(QStringLiteral("degrades"), deg);
    return parts.join(QStringLiteral("; "));
}

}  // namespace Kalburator::Shape
