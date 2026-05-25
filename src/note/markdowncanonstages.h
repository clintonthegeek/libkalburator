#pragma once

#include "transformationedge.h"

namespace Kalburator::Note {

/// (note, markdown) → (note, canon). Stores the body verbatim and the leading
/// YAML frontmatter block verbatim in providerExtras["frontmatter"]; peeks at
/// the first `id:` line to set the canon uid. Never interprets the body.
class MarkdownToCanonStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& sourceBytes) const override;
};

/// (note, canon) → (note, markdown). Re-emits providerExtras["frontmatter"]
/// verbatim (fenced) followed by the body (normalised to one trailing newline).
class CanonToMarkdownStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& sourceBytes) const override;
};

} // namespace Kalburator::Note
