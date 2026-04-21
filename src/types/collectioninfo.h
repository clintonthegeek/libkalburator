#ifndef KALBURATOR_TYPES_COLLECTIONINFO_H
#define KALBURATOR_TYPES_COLLECTIONINFO_H

#include <QString>

namespace Kalburator::Sync {

/// Lower-layer collection description. Unchanged from WP donor shape.
struct CollectionInfo {
    QString id;                ///< Unique identifier.
    QString name;              ///< Display name.
    QString path;              ///< Filesystem path if file-based (optional).
    QString type;              ///< "memos", "contacts", "calendar", "todos".
    bool    isDefault = false;

    bool operator==(const CollectionInfo &other) const = default;
};

} // namespace Kalburator::Sync

#endif
