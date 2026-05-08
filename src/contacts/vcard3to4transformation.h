#pragma once

#include "transformationedge.h"

namespace Kalburator::Contacts {

/// (contacts, vcard3) → (contacts, vcard4)
/// Lossless under KContacts::Addressee pivot (v3 is a subset of v4).
class VCard3To4Stage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& sourceBytes) const override;
};

/// (contacts, vcard4) → (contacts, vcard3)
/// Lossy: drops v4-only properties. See vcard4ToVcard3Loss().
class VCard4To3Stage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& sourceBytes) const override;
};

/// Loss profile for the vcard4 → vcard3 direction. Exact list deferred
/// to plan-execution time per design §3.1: read RFC 6350 §A.1 and verify
/// against KContacts::VCardConverter::v3_0 round-trip behavior with a
/// representative vCard before populating.
Kalburator::Shape::LossProfile vcard4ToVcard3Loss();

} // namespace Kalburator::Contacts
