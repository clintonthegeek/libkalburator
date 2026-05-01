#pragma once

#include "irecordmerger.h"

namespace Kalburator::Contacts {

/// IRecordMerger for (contacts, vcard). Performs 3-way property-level
/// merge between vCard canonical records using KContacts::Addressee.
class IRecordMergerVCard : public Kalburator::Shape::IRecordMerger {
public:
    Kalburator::Shape::CanonicalRecord merge(
        const Kalburator::Shape::CanonicalRecord& source,
        const Kalburator::Shape::CanonicalRecord& target,
        const Kalburator::Shape::CanonicalRecord& baseline,
        const Kalburator::Sync::QSyncCore::ConflictPolicy& policy) const override;
};

} // namespace Kalburator::Contacts
