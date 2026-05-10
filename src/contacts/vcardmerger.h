#pragma once

#include "recordmerger.h"

namespace Kalburator::Contacts {

/// IRecordMerger for (contacts, vcard). Performs 3-way property-level
/// merge between vCard canonical records using KContacts::Addressee.
class RecordMergerVCard : public Kalburator::Shape::RecordMerger {
public:
    Kalburator::Shape::CanonicalRecord merge(
        const Kalburator::Shape::CanonicalRecord& source,
        const Kalburator::Shape::CanonicalRecord& target,
        const Kalburator::Shape::CanonicalRecord& baseline,
        const Kalburator::Conflict::ConflictPolicy& policy) const override;
};

} // namespace Kalburator::Contacts
