#pragma once

#include "identitystore.h"

#include "../shape/canonenvelope.h"

#include <QJsonArray>
#include <QJsonObject>

namespace Kalburator::Identity {

/// The first resolver's key extraction (EEE campaign §5, one rule):
///   contacts  → canon["emails"][i]["value"]
///   calendar  → canon["organizer"]["email"] + canon["attendees"][i]["email"]
///   (todo: no rule yet)
/// Empty-email rows are skipped (every vendor edge already drops them at
/// promote time). Returns domain/uid from the canon envelope.
struct CanonKeys {
    QString domain;
    QString uid;
    QStringList emails;
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
    } else if (out.domain == QLatin1String("calendar")) {
        const QString org = canon.value(QStringLiteral("organizer"))
                                .toObject()
                                .value(QStringLiteral("email"))
                                .toString();
        if (!org.isEmpty())
            out.emails << org;
        for (const auto& av :
             canon.value(QStringLiteral("attendees")).toArray()) {
            const QString e = av.toObject().value(QStringLiteral("email")).toString();
            if (!e.isEmpty())
                out.emails << e;
        }
    }
    return out;
}

/// Convenience: extract keys from canon JSON bytes and link through `store`.
/// Returns the entity id (empty when the record carries no resolvable keys
/// or linking failed).
inline QString linkCanonRecord(IdentityStore& store, const QByteArray& canonBytes)
{
    const QJsonObject canon =
        Kalburator::Shape::CanonEnvelope::parse(canonBytes);
    const CanonKeys keys = extractCanonKeys(canon);
    return store.linkRecord(keys.domain, keys.uid, keys.emails);
}

}  // namespace Kalburator::Identity
