#pragma once
#include "transformationedge.h"

namespace Kalburator::Outline {

/// (outline, opml) → (outline, canon). Maps <outline> XML containment to the
/// node tree; `text`→text, `created`→created, `category`→tags, all other
/// attributes → node `attributes`. <head><title> → doc title.
class OpmlToCanonStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& sourceBytes) const override;
};

/// (outline, canon) → (outline, opml). Emits nested <outline text=…> with
/// `created`/`category` and node `attributes` as XML attributes. Task fields
/// (done/status/priority/progress/start/due/completed) are DROPPED (no OPML
/// representation); `note` → an `_note` attribute (Reversible).
class CanonToOpmlStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& sourceBytes) const override;
};

}  // namespace Kalburator::Outline
