#pragma once

#include "transformationedge.h"

namespace Kalburator::Contacts {

/// (contacts, vcard4) → (contacts, canon)
/// Lossless promote: parses a vCard4 byte string and emits canon JSON bytes.
class VCard4ToCanonStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& vcardBytes) const override;
};

/// (contacts, canon) → (contacts, vcard4)
/// Demote direction: serializes canon JSON bytes back to a vCard4 byte string.
/// Core fields round-trip losslessly; Google-only fields (occupations,
/// interests, etc.) are silently dropped (no vCard4 representation).
class CanonToVCard4Stage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& canonBytes) const override;
};

}  // namespace Kalburator::Contacts
