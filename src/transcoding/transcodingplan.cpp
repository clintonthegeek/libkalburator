// src/transcoding/transcodingplan.cpp
#include "transcodingplan.h"
#include "propertytranscoder.h"

namespace Kalburator::Sync {

TranscodingResult executeTranscodingPlan(
    const TranscodingPlan& plan,
    const KCalendarCore::Incidence::Ptr& original)
{
    if (plan.isEmpty() || !original) {
        return TranscodingResult{original, {}};
    }

    auto transcoded = KCalendarCore::Incidence::Ptr(original->clone());
    QStringList warnings;
    for (auto* transcoder : plan.transcoders) {
        if (!transcoder) {
            continue;
        }
        if (transcoder->transcode(transcoded)) {
            if (transcoder->fidelity() != TranscodingFidelity::Lossless) {
                warnings.append(transcoder->description());
            }
        }
    }
    return TranscodingResult{transcoded, warnings};
}

} // namespace Kalburator::Sync
