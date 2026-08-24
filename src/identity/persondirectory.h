#pragma once

/**
 * @file persondirectory.h
 * @brief The Nepomuk moment, done survivably (EEE proposal §5 payoff).
 *
 * PersonDirectory composes the pieces the campaign built — vendor edges,
 * canon, and the identity link index — into an answer to a HUMAN question:
 * "who is in this meeting?"
 *
 * Feed it canon records via observe() (any domain; contacts project a
 * display name), then hand it an event record: its roster resolves every
 * organizer/attendee email to an entity and joins any linked contact's
 * display name across vendors. A Google contact and a Microsoft event
 * attendee sharing an email become ONE person with a NAME. Unresolved
 * emails come back as strangers — never invented.
 *
 * Never a merge: entities link records; the directory only reads links.
 */

#include "identityresolver.h"
#include "identitystore.h"

#include "../shape/canonenvelope.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace Kalburator::Identity {

struct RosterEntry {
    QString email;        ///< the attendee/organizer email
    QString entityId;     ///< minted entity id; EMPTY = unresolved stranger
    QString displayName;  ///< joined from a linked contact; empty if unknown
};

class PersonDirectory {
public:
    explicit PersonDirectory(IdentityStore& store) : m_store(store) {}

    /// Observe a canon record: extracts keys, links it through the store
    /// (idempotent), and stores the display-name projection for contacts.
    /// Returns the entity id (empty when nothing resolvable).
    QString observe(const QByteArray& canonBytes)
    {
        return linkCanonRecord(m_store, canonBytes);
    }

    /// The meeting roster: organizer + attendees of a calendar-canon
    /// event, in order, deduplicated. Each entry resolves through the
    /// email half of the resolver rule; resolved entries join the display
    /// name of the first (uid-sorted) linked CONTACT record.
    QList<RosterEntry> eventRoster(const QByteArray& eventCanonBytes) const
    {
        using Kalburator::Shape::CanonEnvelope::canonKey;

        QList<RosterEntry> roster;
        QStringList seen;
        const auto addEmail = [&](const QString& email) {
            if (email.isEmpty() || seen.contains(email.toLower()))
                return;
            seen << email.toLower();
            RosterEntry entry;
            entry.email = email;
            entry.entityId = m_store.entityIdForEmail(email);
            if (!entry.entityId.isEmpty()) {
                for (const EntityLink& link :
                     m_store.recordsForEntity(entry.entityId)) {
                    const QString name =
                        m_store.displayNameFor(link.domain, link.recordUid);
                    if (!name.isEmpty()) {
                        entry.displayName = name;
                        break;
                    }
                }
            }
            roster.append(entry);
        };

        const QJsonObject event =
            Kalburator::Shape::CanonEnvelope::parse(eventCanonBytes);
        addEmail(event.value(QStringLiteral("organizer"))
                     .toObject()
                     .value(QStringLiteral("email"))
                     .toString());
        for (const auto& av :
             event.value(QStringLiteral("attendees")).toArray())
            addEmail(av.toObject().value(QStringLiteral("email")).toString());
        return roster;
    }

    /// Direct lookup convenience.
    QString entityIdForRecord(const QString& domain,
                              const QString& recordUid) const
    {
        return m_store.entityIdFor(domain, recordUid);
    }

private:
    IdentityStore& m_store;
};

}  // namespace Kalburator::Identity
