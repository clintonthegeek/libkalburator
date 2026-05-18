#ifndef KALBURATOR_TYPES_COLLECTIONINFO_H
#define KALBURATOR_TYPES_COLLECTIONINFO_H

#include <QString>
#include <QStringList>

namespace Kalburator::Sync {

/// Lower-layer collection description. Unchanged from WP donor shape.
struct CollectionInfo {
    QString id;                ///< Unique identifier.
    QString name;              ///< Display name.
    QString path;              ///< Filesystem path if file-based (optional).
    QString type;              ///< "memos", "contacts", "calendar", "todos".
    bool    isDefault = false;

    /// O.1.5: capability hints. Populated by providers from their
    /// discovered server capabilities; rendered as chips in
    /// CollectionPickerWidget.
    bool        readOnly     = false;
    QStringList contentTypes;          ///< "VEVENT", "VTODO", "VCARD" subset.
    int         estimatedSizeBytes = -1;  ///< Approx storage size in bytes; -1 = unknown.

    bool operator==(const CollectionInfo &other) const = default;
};

} // namespace Kalburator::Sync

#endif
