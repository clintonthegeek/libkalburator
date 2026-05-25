#pragma once

#include "rawfilesbackend.h"

namespace Kalburator::Sinks {

/// RawFilesBackend specialised for human-readable Markdown notes:
///   - file suffix is ".md" (not "<encoding>.<domain>")
///   - filename stem is the first non-empty body line (after any YAML
///     frontmatter), sanitised, with a "note_<recordId>" fallback.
/// The bytes written are the (note, markdown) peer encoding verbatim.
class MarkdownFilesBackend : public RawFilesBackend {
    Q_OBJECT
public:
    explicit MarkdownFilesBackend(QString rootPath, QObject *parent = nullptr)
        : RawFilesBackend(std::move(rootPath), parent) {}

    QString backendType() const override { return QStringLiteral("markdown-files"); }
    QString displayName() const override { return QStringLiteral("Markdown Files Backend"); }

protected:
    QString suffixFor(const QString &collectionId) const override;
    QString recordStem(const QString &collectionId,
                       const Kalburator::Sync::BackendRecord &record) const override;
};

} // namespace Kalburator::Sinks
