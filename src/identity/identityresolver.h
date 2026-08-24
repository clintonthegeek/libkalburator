#pragma once

#include "identitystore.h"

#include "../shape/canonenvelope.h"

#include <QJsonArray>
#include <QJsonObject>

namespace Kalburator::Identity {

/// The first resolver's key extraction (EEE campaign §5, one rule):
///   contacts  → canon["emails"][i]["value"] (+ names[0].formatted as the
///               display-name projection)
///   calendar  → NO email keys (FINDINGS O65): an event's attendee/
///               organizer emails are ROSTER QUERIES, never identity
///               evidence — indexing them would fuse every participant
///               onto the meeting's entity. Events join by uid alone;
///               participants converge at RESOLUTION time via the
///               contact-owned email_index.
///   todo      → no rule yet
/// Returns domain/uid from the canon envelope.
struct CanonKeys {
    QString domain;
    QString uid;
    QStringList emails;
    QString displayName;  ///< contacts only: names[0].formatted
};

inline CanonKeys extractCanonKeys(const QJsonObject& canon)
{
    using Kalburator::Shape::CanonEnvelope::canonKey;
    using Kalburator::Shape::CanonEnvelope::uidKey;

    CanonKeys out;
    out.domain = canon.value(canonKey())
                     .toObject()
                     .value(QStringLiteral("domain"))
                     .toString();
    out.uid = canon.value(uidKey()).toString();

    if (out.domain == QLatin1String("contacts")) {
        for (const auto& ev : canon.value(QStringLiteral("emails")).toArray()) {
            const QString v = ev.toObject().value(QStringLiteral("value")).toString();
            if (!v.isEmpty())
                out.emails << v;
        }
        out.displayName =
            canon.value(QStringLiteral("names"))
                .toArray()
                .at(0)
                .toObject()
                .value(QStringLiteral("formatted"))
                .toString();
    }
    // calendar/todo: deliberately no emails (O65).
    return out;
}

/// Convenience: extract keys from canon JSON bytes and link through `store`.
/// Any record with a valid domain+uid gets an entity (people exist without
/// emails); emails are evidence for CROSS-record convergence only.
inline QString linkCanonRecord(IdentityStore& store, const QByteArray& canonBytes)
{
    const QJsonObject canon =
        Kalburator::Shape::CanonEnvelope::parse(canonBytes);
    const CanonKeys keys = extractCanonKeys(canon);
    return store.linkRecord(keys.domain, keys.uid, keys.emails,
                            keys.displayName);
}

}  // namespace Kalburator::Identity
