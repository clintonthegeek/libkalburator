#ifndef KALBURATOR_SYNC_AKONADICOLLECTIONID_H
#define KALBURATOR_SYNC_AKONADICOLLECTIONID_H

#include <QString>
#include <QStringView>
#include <QLatin1StringView>

namespace Kalburator::Sync {

// Single source of truth for the Akonadi collection-id scheme shared by
// AkonadiProvider (the producer) and the Akonadi calendar/contacts backends
// (the consumers): "akonadi-<numericCollectionId>" (e.g. "akonadi-184").
//
// Akonadi collection ids are globally unique across the whole collection tree,
// so there is NO per-type prefix — calendar and contacts use the identical
// scheme. Keeping the to/from pair here (instead of a private prefix constant in
// each backend) makes producer<->consumer agreement structural rather than a
// convention that can silently drift — which it did: the contacts backend once
// parsed "akonadi-contacts-<id>" while the provider only ever emitted
// "akonadi-<id>" (2026-06-14 prefix-mismatch handoff).
//
// Deliberately free of Akonadi headers (plain QString/qlonglong, qlonglong ==
// Akonadi::Collection::Id) so it compiles and is unit-tested in the default
// (Akonadi-OFF) profile.

// Defined once so the producer and every consumer share the exact literal.
inline constexpr char AKONADI_COLLECTION_ID_PREFIX[] = "akonadi-";

/// "akonadi-<id>" for an Akonadi::Collection::Id (qlonglong).
inline QString akonadiCollectionIdToString(qlonglong id)
{
    return QLatin1String(AKONADI_COLLECTION_ID_PREFIX) + QString::number(id);
}

/// The numeric collection id, or -1 if `s` is not in the "akonadi-<id>" scheme
/// (foreign prefix, empty suffix, or non-numeric suffix — e.g. the old
/// "akonadi-contacts-<id>" form whose suffix is non-numeric).
inline qlonglong akonadiCollectionIdFromString(const QString &s)
{
    const QLatin1String prefix(AKONADI_COLLECTION_ID_PREFIX);
    if (!s.startsWith(prefix))
        return -1;
    bool ok = false;
    const qlonglong id = QStringView{s}.sliced(prefix.size()).toLongLong(&ok);
    return ok ? id : -1;
}

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNC_AKONADICOLLECTIONID_H
