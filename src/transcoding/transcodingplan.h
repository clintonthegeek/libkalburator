// src/transcoding/transcodingplan.h
#ifndef KALBURATOR_TRANSCODINGPLAN_H
#define KALBURATOR_TRANSCODINGPLAN_H

#include <QList>
#include <QString>
#include <QStringList>
#include <KCalendarCore/Incidence>

namespace Kalburator::Sync {

class PropertyTranscoder;

/// Decision produced by TranscodingRouter and consumed by SyncBackend
/// write methods. Borrowed (non-owning) pointers — the transcoders are
/// owned by TranscodingRegistry. A plan must not outlive the registry
/// it was sourced from. In normal use, plans are built at the start of
/// an applyChanges() invocation and discarded at its end.
struct TranscodingPlan {
    QList<PropertyTranscoder*> transcoders;
    QString routingDecision;  // diagnostic only ("source=X target=Y, N transcoders")

    bool isEmpty() const { return transcoders.isEmpty(); }
};

/// Result of executing a plan against an incidence.
struct TranscodingResult {
    KCalendarCore::Incidence::Ptr incidence;  // the (possibly cloned and transcoded) incidence
    QStringList warnings;                     // empty if lossless or plan was empty
};

/// Execute the plan against `original`. If the plan is empty, returns
/// `{original, {}}` with no clone. Otherwise clones the incidence,
/// runs each transcoder in order, accumulates warnings from lossy
/// transcoders, and returns the transcoded clone.
TranscodingResult executeTranscodingPlan(
    const TranscodingPlan& plan,
    const KCalendarCore::Incidence::Ptr& original);

} // namespace Kalburator::Sync

#endif // KALBURATOR_TRANSCODINGPLAN_H
