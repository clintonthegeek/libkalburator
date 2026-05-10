#pragma once

#include "recorddiffer.h"

namespace Kalburator::Contacts {

/// IRecordDiffer for (contacts, vcard). Uses KContacts::VCardConverter
/// to compute per-property differences between two vCard records.
class RecordDifferVCard : public Kalburator::Shape::RecordDiffer {
public:
    QSet<Kalburator::Shape::PropertyId> diff(
        const Kalburator::Shape::CanonicalRecord& source,
        const Kalburator::Shape::CanonicalRecord& baseline) const override;

    bool equal(const Kalburator::Shape::CanonicalRecord& a,
               const Kalburator::Shape::CanonicalRecord& b) const override;
};

} // namespace Kalburator::Contacts
