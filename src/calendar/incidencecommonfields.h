#pragma once

#include <KCalendarCore/Incidence>
#include <QByteArray>
#include <QJsonObject>
#include <QSet>
#include <QString>

// IP.6 (incidence-parity campaign) — fields whose promote/demote treatment
// is genuinely shared across two or more of {VEVENT, VTODO, VJOURNAL},
// extracted here so a future fix (or bug) is made once instead of copied.
// Every function operates on KCalendarCore::Incidence's own base API (the
// class VEVENT/VTODO/VJOURNAL all derive) — never on a subclass-specific
// accessor — so the same code is genuinely reusable, not merely relocated.
//
// Ownership rule per function (see docs/campaign/incidence-parity/PLAN.md
// IP.6 + Amendment 1 §A.3.2 + the IP.6 return receipt for the full
// per-field argument): a function documented "VEVENT + VTODO" is
// deliberately NOT called from journalcanonfields.cpp yet — VJOURNAL's
// wiring for organizer/attendees/attachments/relatedTo is IP.10's job
// (PLAN.md's IP.10 body: "VJOURNAL should get [these] essentially for
// free" once this module exists) so it lands together with IP.10's
// RECURRENCE-ID identity fix rather than piecemeal. comments/contacts are
// the one deliberate exception — see the IP.6 receipt's judgment-call
// section — wired to all three kinds here because RFC 5545 §3.6.3's
// jourprop permits both on VJOURNAL and the fix is a mechanical one-line
// call, not new field-specific code.
namespace Kalburator::Calendar {

// ---------------------------------------------------------------------
// created / lastModified — O41 literal-presence guard (commit 1: already
// identical across all three kinds today).
// ---------------------------------------------------------------------

/// Promote: only emits "created"/"lastModified" when the property is
/// LITERALLY present in `originalBytes` (see extractICalPropertyLiteral's
/// doc comment for why the KCalendarCore accessors cannot be trusted here).
void promoteTimestamps(QJsonObject& obj, const QByteArray& originalBytes);

/// Tracks whether canon carried "created"/"lastModified" so the demote
/// caller can strip KCalendarCore's injected "now" default post-
/// serialization (O41 write-side fix) when the corresponding key was
/// absent.
struct TimestampPresence {
    bool hadCreated = false;
    bool hadLastModified = false;
};

/// Demote: sets created()/lastModified() on `inc` when canon carries them.
TimestampPresence demoteTimestamps(const QJsonObject& obj,
                                    const KCalendarCore::Incidence::Ptr& inc);

/// Strips the CREATED/LAST-MODIFIED lines KCalendarCore::ICalFormat
/// unconditionally injects, for whichever of the two `presence` says canon
/// never carried. Thin wrapper around stripICalPropertyLine so every demote
/// call site performs the O41 write-side fix identically.
QByteArray stripInjectedTimestamps(QByteArray icalBytes, const TimestampPresence& presence);

// ---------------------------------------------------------------------
// summary / description (commit 1: already identical across all three).
// descriptionHtml is DELIBERATELY NOT here — VJOURNAL does not carry it
// today and PLAN.md's IP.10 section explicitly owns that decision; see the
// IP.6 receipt.
// ---------------------------------------------------------------------

void promoteSummaryDescription(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);
void demoteSummaryDescription(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);

// ---------------------------------------------------------------------
// categories (commit 1: already identical across all three).
// ---------------------------------------------------------------------

void promoteCategories(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);
void demoteCategories(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);

// ---------------------------------------------------------------------
// Generic X- custom-property passthrough into providerExtras (commit 1:
// parameterized by sub-key name + skip-list so the extraction is zero
// behaviour change — VEVENT/VJOURNAL use "x-ical" with VEVENT alone
// skipping the two properties it already promotes by name; VTODO uses
// "x-vtodo" with no skip list and additionally stamps providerExtrasDigest
// itself, which stays local to vtodocanonfields.cpp — O80/IP.5 scope, not
// this item's).
// ---------------------------------------------------------------------

/// Builds the raw sub-object (NOT wrapped in providerExtras[subkey]) from
/// `inc`'s customProperties(), skipping any key in `skipKeys`. Empty when
/// there is nothing to carry — caller decides whether/how to wrap it.
QJsonObject promoteCustomPropertyPassthrough(const KCalendarCore::Incidence::Ptr& inc,
                                              const QSet<QByteArray>& skipKeys = {});

/// Reverse: applies every key in `subObj` as a non-KDE custom property on
/// `inc`.
void demoteCustomPropertyPassthrough(const QJsonObject& subObj,
                                      const KCalendarCore::Incidence::Ptr& inc);

// ---------------------------------------------------------------------
// sequence (commit 2 — VTODO gains it per O83; VEVENT/VJOURNAL already
// treat it identically today, routed through here too so there is exactly
// one copy once all three agree).
// ---------------------------------------------------------------------

void promoteSequence(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);
void demoteSequence(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);

// ---------------------------------------------------------------------
// classification (commit 2 — VTODO gains it per O83). VEVENT + VTODO
// ONLY: VJOURNAL keeps its own separate, untouched implementation, whose
// unconditional-insert "phantom key" bug (journalcanonfields.cpp) is
// explicitly IP.10's to fix (PLAN.md's IP.10 body) — routing VJOURNAL
// through this guarded implementation now would silently fix that bug as
// a byproduct of this item, which is exactly the kind of undeclared scope
// creep PLAN.md §1 prohibits.
// ---------------------------------------------------------------------

void promoteClassification(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);
void demoteClassification(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);

// ---------------------------------------------------------------------
// color / url (commit 2 — VTODO gains both per O83; VEVENT/VJOURNAL
// already treat them identically today).
// ---------------------------------------------------------------------

void promoteColor(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);
void demoteColor(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);

void promoteUrl(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);
void demoteUrl(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);

// ---------------------------------------------------------------------
// organizer / attendees / attachments (commit 2 — VTODO gains all three
// per O83). VEVENT + VTODO ONLY — see the file-level comment on VJOURNAL's
// deferred wiring (IP.10).
// ---------------------------------------------------------------------

void promoteOrganizer(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);
void demoteOrganizer(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);

void promoteAttendees(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);
void demoteAttendees(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);

void promoteAttachments(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);
void demoteAttachments(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);

// ---------------------------------------------------------------------
// relatedTo (commit 2 — VEVENT gains it per Amendment 1 §A.3.2; VTODO
// already has it, extracted verbatim). VEVENT + VTODO ONLY — VJOURNAL's
// wiring is IP.10's (see the file-level comment).
// ---------------------------------------------------------------------

void promoteRelatedTo(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);
void demoteRelatedTo(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);

// ---------------------------------------------------------------------
// comments / contacts (commit 2 — O91; wired to VEVENT + VTODO + VJOURNAL,
// see the file-level comment on the judgment call).
// ---------------------------------------------------------------------

void promoteComments(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);
void demoteComments(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);

void promoteContacts(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);
void demoteContacts(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);

// ---------------------------------------------------------------------
// resources (commit 2 — O91). VEVENT + VTODO ONLY: RFC 5545 §3.6.3's
// jourprop grammar does not permit RESOURCES on VJOURNAL at all, so its
// absence there is RFC-correct, not a drop.
//
// O94 (new, filed this item): correct on the OBJECT MODEL (resources()/
// setResources() work exactly as documented — verified directly), but
// KCalendarCore::ICalFormat 6.29.0 never reads OR writes a RESOURCES
// property on the wire — parsing a source RESOURCES line leaves
// resources() empty, and setResources() followed by toICalString() never
// emits a RESOURCES line either. This contradicts O91's claim that
// resources() "round-trips fine through KCalendarCore's own ICalFormat" —
// that claim is wrong for RESOURCES specifically (verified against
// COMMENT/CONTACT, which DO round-trip correctly through the same
// ICalFormat call). These functions are kept and called anyway: they are
// correct against the object model, useful for any non-ICalFormat caller,
// and forward-compatible with a future kcalendarcore fix. The {calendar,
// ical}/{todo,ical-vtodo} wire edges declare `resources: Dropped` in their
// loss profiles to be honest about today's actual behaviour — see the
// IP.6 return receipt.
// ---------------------------------------------------------------------

void promoteResources(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);
void demoteResources(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);

}  // namespace Kalburator::Calendar
