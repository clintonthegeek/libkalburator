#pragma once

/**
 * @file identitystore.h
 * @brief SQLite-backed registry mapping (domain, record-uid) → entity-id
 * (EEE campaign §5 — the identity layer, done survivably).
 *
 * Identity is NOT conversion: it is a link index above the shape-graph
 * domains. Entities LINK records; they never collapse them. Deleting a
 * record dissolves only its own link; peer records are untouched.
 *
 * Schema v1:
 *   record_links(domain, record_uid) PK → entity_id   (+ entity index)
 *   email_index(email) PK               → entity_id   (the first resolver's
 *   rule: contacts' emails[].value ↔ calendar attendees[].email /
 *   organizer.email share an entity)
 *
 * Not thread-safe. Not a QObject — RAII connection ownership, value
 * lifetime (BaselineStore template).
 */

#include <QList>
#include <QString>
#include <QStringList>

namespace Kalburator::Identity {

struct EntityLink {
    QString domain;     ///< shape-graph domain id ("contacts", "calendar", …)
    QString recordUid;  ///< canon uid within that domain
    QString entityId;   ///< minted stable entity id ("ent-…")
};

class IdentityStore {
public:
    explicit IdentityStore(const QString& dbPath);
    ~IdentityStore();

    IdentityStore(const IdentityStore&) = delete;
    IdentityStore& operator=(const IdentityStore&) = delete;
    IdentityStore(IdentityStore&&) = delete;
    IdentityStore& operator=(IdentityStore&&) = delete;

    bool isOpen() const { return m_isOpen; }
    QString lastError() const { return m_lastError; }
    QString databasePath() const { return m_dbPath; }

    /// Resolve-or-mint: links (domain, recordUid) to an entity. When any of
    /// `emails` is already indexed to an entity, that entity is adopted
    /// (first hit in sorted-email order — deterministic, never a merge of
    /// two entities). Otherwise a fresh entity id is minted. Idempotent:
    /// re-linking an already-linked record keeps its entity unless email
    /// evidence points elsewhere.
    /// Returns the entity id, or empty on error/invalid input.
    QString linkRecord(const QString& domain, const QString& recordUid,
                       const QStringList& emails);

    /// Empty string when the record has no link.
    QString entityIdFor(const QString& domain, const QString& recordUid) const;

    /// All records linked to one entity ("this meeting's people are these
    /// contacts").
    QList<EntityLink> recordsForEntity(const QString& entityId) const;

    /// Emails currently indexed to the entity.
    QStringList emailsForEntity(const QString& entityId) const;

    /// Remove ONE record's link. Peer links on the entity are untouched;
    /// email-index rows whose entity lost its last record are pruned so a
    /// later same-email record mints fresh rather than resurrecting.
    void unlinkRecord(const QString& domain, const QString& recordUid);

private:
    bool ensureSchemaAndVersion();

    QString m_dbPath;
    QString m_connName;
    bool m_isOpen = false;
    mutable QString m_lastError;
};

}  // namespace Kalburator::Identity
