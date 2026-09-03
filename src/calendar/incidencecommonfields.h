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
// IP.6/IP.10 + Amendment 1 §A.3.2 + the IP.6/IP.10 return receipts for the
// full per-field argument): a function documented below as "VEVENT + VTODO"
// records that history — journalcanonfields.cpp did NOT call it before
// IP.10 landed (2026-09-02) — but IP.10 verified the wiring was genuinely
// free (no new field-specific logic needed) and now calls
// promoteOrganizer/demoteOrganizer, promoteAttendees/demoteAttendees,
// promoteAttachments/demoteAttachments, promoteRelatedTo/demoteRelatedTo and
// promoteClassification/demoteClassification too, alongside IP.10's own new
// journal-specific RECURRENCE-ID identity + verbatim-recurrence-lines code.
// The per-function doc comments below are left as written (accurate
// history, not stale — they describe why the function was built, not who
// calls it today); journalcanonfields.cpp is the place to check for
// current call sites. comments/contacts were the one exception landed
// early, by IP.6 itself — see its receipt's judgment-call section — wired
// to all three kinds because RFC 5545 §3.6.3's jourprop permits both on
// VJOURNAL and the fix was a mechanical one-line call, not new
// field-specific code.
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
// O90 / IP.12 — demote purity: strip the heap-address-derived ATTENDEE
// X-UID parameter KCalendarCore::ICalFormat stamps into every serialized
// ATTENDEE line, so demote(canon) is a pure function of canon again (two
// demotes of the same canon in different PROCESSES are byte-identical, not
// merely two demotes in the same process). ORGANIZER is unaffected —
// verified: KCalendarCore::Person (which backs organizer()) carries no uid
// property, so ICalFormat never has anything heap-derived to stamp there.
// Thin wrapper around stripICalPropertyParameter, same shape as
// stripInjectedTimestamps above, so every demote call site performs this
// strip identically instead of three copies of the same regex call.
// ---------------------------------------------------------------------

QByteArray stripAttendeeXUid(QByteArray icalBytes);

// ---------------------------------------------------------------------
// summary / description (commit 1: already identical across all three).
// descriptionHtml is DELIBERATELY NOT here — it rides an inline three-line
// X-ALT-DESC read/write in each of eventcanonfields.cpp/vtodocanonfields.cpp/
// journalcanonfields.cpp (not extracted, per the IP.6 receipt's judgment
// call: three near-identical one-liners were not worth a shared function).
// IP.10 wired VJOURNAL's copy 2026-09-02.
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
// classification (commit 2 — VTODO gains it per O83; IP.10 wired VJOURNAL
// too, 2026-09-02, closing its unconditional-insert "phantom key" bug —
// journalcanonfields.cpp used to insert `classification: "public"` even
// when no CLASS property was present at all).
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
// per O83; IP.10 wired VJOURNAL too, 2026-09-02 — see the file-level
// comment).
// ---------------------------------------------------------------------

void promoteOrganizer(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);
void demoteOrganizer(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);

void promoteAttendees(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);
void demoteAttendees(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);

void promoteAttachments(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);
void demoteAttachments(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc);

// ---------------------------------------------------------------------
// relatedTo (commit 2 — VEVENT gains it per Amendment 1 §A.3.2; VTODO
// already has it, extracted verbatim; IP.10 wired VJOURNAL too, 2026-09-02
// — see the file-level comment).
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
