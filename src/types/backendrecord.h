#ifndef KALBURATOR_TYPES_BACKENDRECORD_H
#define KALBURATOR_TYPES_BACKENDRECORD_H

#include <QByteArray>
#include <QDateTime>
#include <QString>

namespace Kalburator::Sync {

/// Opaque record in a blob backend. Backends serialize/deserialize
/// their native formats to/from `data`. Lives at the library root
/// because both the blob engine and any consumer needing shared
/// vocabulary reads it.
struct BackendRecord {
    QString    id;                 ///< Backend-assigned unique id
                                   ///  (file path, CalDAV href, PalmID, …).
    QString    type;               ///< "memo", "contact", "event", "todo",
                                   ///  "binary", … — host-interpreted.
    QString    displayName;        ///< Human-readable, for UI/logs.
    QByteArray data;               ///< Opaque bytes.
    QString    contentHash;        ///< Backend-computed; algorithm is
                                   ///  backend's choice (SHA-256 for
                                   ///  LocalBlobBackend).
    QDateTime  lastModified;
    bool       isDeleted = false;

    QString description() const
    { return displayName.isEmpty() ? id : displayName; }

    bool operator==(const BackendRecord &other) const = default;
};

} // namespace Kalburator::Sync

#endif
