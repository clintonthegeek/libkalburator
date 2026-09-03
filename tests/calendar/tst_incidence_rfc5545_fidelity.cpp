// IP.8 (incidence-parity campaign) — RFC 5545 round-trip fidelity gate.
//
// IP.1's gate (tst_calendar_kind_dispatch.cpp) asks "does the catalogue
// know what the emitter emits?" — agreement between two of OUR OWN
// artifacts. This file asks a different question: "does the emitter
// honour RFC 5545?" — agreement between our emitter and the STANDARD.
// Every fixture below is built by reading RFC 5545 §3.6.1 (VEVENT),
// §3.6.2 (VTODO), §3.6.3 (VJOURNAL) and §3.6.6 (VALARM) directly — every
// property the grammar PERMITS on that component, not every property our
// emitters happen to handle. A fixture built from emitter capability would
// make this gate vacuous in exactly the way that let O78/O83/O84/O85/O86/
// O87 accumulate unnoticed (see PLAN.md Amendment 1 §A.1). Where this
// item's own "maximal" fixtures found MORE loss than the pre-flight audit
// declared (COMMENT/CONTACT/RESOURCES/REQUEST-STATUS), that is logged as
// FINDINGS O91, not silently pinned and not fixed here (PLAN.md §1's
// "no fix while passing through" prohibition).
//
// New file rather than an extension of tst_calendar_kind_dispatch.cpp:
// argued in the IP.8 return receipt (docs/campaign/incidence-parity/
// 2026-09-02-ip8-return-receipt.md §1) — different question, different
// fixture-construction discipline (RFC-first, not emitter-first), and
// IP.1's own file already carries 12 slots for a different gate.
//
// Placement of promote/demote: reuses Kalburator::Calendar::
// ICalToCanonStage / CanonToICalStage — the same {calendar,ical}<->
// {calendar,canon} pair the pre-flight audit's probe
// (docs/campaign/incidence-parity/probes/incidence-audit-probe.cpp) used,
// and the same pair tst_calendar_kind_dispatch.cpp's vtodo/vjournal round
// trip slots already use. VTODO rides this pair too (icalcanonstages.cpp
// kind-dispatches to the shared Todo::todoFieldsToCanon() emitter) — this
// is deliberate: O83/O85/O86's undeclared drops are live on exactly this
// path, which PlanStan disclosed is their PRIMARY and DEFAULT task
// representation (STATUS.md 2026-09-02 entry).

#include <QTest>
#include <QDate>
#include <QDateTime>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTime>
#include <QTimeZone>

#include "icalcanonstages.h"
#include "journalcanonfields.h"
#include "lossprofile.h"

#include <KCalendarCore/Alarm>
#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Todo>

namespace {

// ---------------------------------------------------------------------------
// RFC 5545 §3.1 line unfolding, THEN property-name extraction.
//
// Two traps already cost the pre-flight audit real time (probes/README.md):
// (1) a per-line parse reports ATTENDEE as lost when its line folds before
// the colon — KCalendarCore folds at 75 octets and ATTENDEE lines routinely
// do — so lines MUST be unfolded first; (2) libical drops the ENTIRE
// ATTENDEE property for a single-label mail domain ("a@x"), so every
// fixture below uses a multi-label domain (example.com). Both traps are
// re-encoded here rather than assumed away, following
// incidence-audit-probe.cpp's icalPropertyNames() exactly (same mechanism,
// same comments) since that function is the one this campaign already
// verified correct against both traps.
QSet<QString> icalPropertyNames(const QByteArray& bytes)
{
    QList<QByteArray> logical;
    for (const QByteArray& raw : bytes.split('\n')) {
        QByteArray line = raw;
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.isEmpty())
            continue;
        if ((line.startsWith(' ') || line.startsWith('\t')) && !logical.isEmpty()) {
            logical.last() += line.mid(1);   // continuation of the previous line
            continue;
        }
        logical.append(line);
    }

    QSet<QString> names;
    for (const QByteArray& line : logical) {
        const int cut = line.indexOf(':');
        if (cut < 0)
            continue;
        QByteArray name = line.left(cut);
        const int semi = name.indexOf(';');
        if (semi >= 0)
            name = name.left(semi);   // strip parameters
        const QString n = QString::fromLatin1(name).trimmed().toUpper();
        // VCALENDAR wrapper properties and KCalendarCore's own bookkeeping
        // are not RFC 5545 component properties under test here.
        if (n == QStringLiteral("BEGIN") || n == QStringLiteral("END")
            || n == QStringLiteral("VERSION") || n == QStringLiteral("PRODID")
            || n == QStringLiteral("DTSTAMP") || n.startsWith(QStringLiteral("X-KDE")))
            continue;
        names.insert(n);
    }
    return names;
}

QStringList sortedList(const QSet<QString>& s)
{
    QStringList l(s.begin(), s.end());
    l.sort();
    return l;
}

// ---------------------------------------------------------------------------
// Declared allow-list (PLAN.md IP.8: "declared data, not scattered
// literals"). IP.9 CLOSES the TODO(IP.9) that stood here for vtodo/vjournal:
// their `expectedLost` lists below are now DERIVED from the real per-kind
// LossProfile (Kalburator::Calendar::canonToVtodoIcalLoss() /
// canonToVjournalLoss(), both new/repopulated by IP.9 — see
// icalcanonstages.{h,cpp} and journalcanonfields.{h,cpp}), via
// droppedRfcNames() below, rather than hand-typed literals.
//
// vevent's list STAYS a literal — deliberately not wired, even after IP.6.
// Its real profile (canonToIcalLoss()) is a DIFFERENT vocabulary of loss
// (canon-JSON vendor keys — onlineMeeting, guestsCan*, ... — with no
// RFC-name counterpart at all) from the RFC-property-name drops this gate
// measures. IP.6 added two RFC-named Dropped entries to canonToIcalLoss()
// (geo, requestStatus) specifically so this literal has something honest
// to check against, but wiring the whole profile through droppedRfcNames()
// the way vtodo/vjournal are would require the translation table to also
// cover the vendor-only vocabulary, which has no RFC-name counterpart at
// all — not worth building for two entries. Revisit if a future item finds
// reason to.
struct KindFidelityExpectation {
    QStringList expectedLost;   // RFC 5545 property NAMES, uppercase, sorted
    bool expectFixpoint = true; // promote->demote->promote canon bytes equal
};

// Canon PropertyId -> RFC 5545 property NAME(s). Necessarily hand-declared
// here: canon ids (LossProfile::affected's key space) and RFC property
// names (this gate's measurement space) are two different vocabularies
// with no shared catalogue to derive the mapping from — a canon id can
// expand to MORE than one RFC name (e.g. the single verbatim-RFC5545-lines
// carrier "recurrence" covers RRULE/RDATE/EXDATE at once, invariant 3).
// Scoped to exactly the ids IP.9's two new/repopulated Dropped profiles
// use; extend it if a future item adds another kind-scoped Dropped id.
const QHash<QString, QStringList>& propertyIdToRfcNames()
{
    static const QHash<QString, QStringList> table = {
        { QStringLiteral("attachments"),    { QStringLiteral("ATTACH") } },
        { QStringLiteral("attendees"),      { QStringLiteral("ATTENDEE") } },
        { QStringLiteral("classification"), { QStringLiteral("CLASS") } },
        { QStringLiteral("color"),          { QStringLiteral("COLOR") } },
        { QStringLiteral("comments"),       { QStringLiteral("COMMENT") } },
        { QStringLiteral("contacts"),       { QStringLiteral("CONTACT") } },
        { QStringLiteral("geo"),            { QStringLiteral("GEO") } },
        { QStringLiteral("organizer"),      { QStringLiteral("ORGANIZER") } },
        { QStringLiteral("requestStatus"),  { QStringLiteral("REQUEST-STATUS") } },
        { QStringLiteral("resources"),      { QStringLiteral("RESOURCES") } },
        { QStringLiteral("sequence"),       { QStringLiteral("SEQUENCE") } },
        { QStringLiteral("url"),            { QStringLiteral("URL") } },
        { QStringLiteral("relatedTo"),      { QStringLiteral("RELATED-TO") } },
        { QStringLiteral("recurrenceId"),   { QStringLiteral("RECURRENCE-ID") } },
        { QStringLiteral("recurrence"),     { QStringLiteral("RRULE"), QStringLiteral("RDATE"),
                                               QStringLiteral("EXDATE") } },
    };
    return table;
}

// Translates a LossProfile's Dropped entries to their RFC 5545 property
// NAME(s), sorted. Only `Dropped` is translated here — this gate measures
// property-NAME loss (a name either survives the round trip or it does
// not), which is exactly what `LossKind::Dropped` means. `Reversible`/
// `Simplified` both keep the property's NAME present in the output
// (recoverable/reduced form — nothing for a before/after NAME-SET diff to
// see), so Dropped-only translation is a complete mapping onto this gate's
// axis. (IP.6 note: before this item, VTODO's `geo` was `Degraded` here —
// NAME survives, VALUE corrupted by O86, caught only by the fixpoint check
// below, not this list. IP.6 removed GEO from vtodocanonfields.cpp
// entirely, so it is now cleanly `Dropped` like every other kind — the
// Degraded case this comment used to describe no longer exists in either
// profile.)
QStringList droppedRfcNames(const Kalburator::Shape::LossProfile& profile)
{
    QSet<QString> names;
    const auto& xlate = propertyIdToRfcNames();
    QStringList untranslated;
    for (const auto& id : profile.droppedProperties()) {
        const auto it = xlate.constFind(id.toString());
        if (it != xlate.constEnd())
            names.unite(QSet<QString>(it->begin(), it->end()));
        else
            untranslated << id.toString();   // would silently under-report — surfaced below
    }
    // Every id either of IP.9's two profiles currently declares as Dropped
    // is in propertyIdToRfcNames() (checked by hand against both profiles
    // when they were written; see the IP.9 return receipt). This assertion
    // is the forcing function for the NEXT edit that adds one and forgets
    // the translation entry — silent under-reporting here would otherwise
    // make this gate pass for the wrong reason.
    Q_ASSERT_X(untranslated.isEmpty(), "droppedRfcNames",
               qPrintable(QStringLiteral("no RFC-name translation for: %1")
                              .arg(untranslated.join(QStringLiteral(", ")))));
    return sortedList(names);
}

// vjournal's expected-lost list, DERIVED from canonToVjournalLoss() (the
// canon->vjournal DEMOTE direction's own declared drops) PLUS one hand-added
// entry, RELATED-TO, that profile structurally cannot express — see
// FINDINGS O95. journalcanonfields.cpp's promoteRelatedTo()/demoteRelatedTo()
// are correctly wired (IP.10), and canonObjectToJournalBytes()'s WRITE side
// genuinely serializes RELATED-TO losslessly (probe-verified) — the defect
// is upstream, in the OPPOSITE direction: KCalendarCore::ICalFormat's
// VJOURNAL parser never populates relatedTo() from a source RELATED-TO line
// (VEVENT/VTODO's parsers do; verified by direct probe against ICalFormat
// alone, no libkalburator code involved). canonToVjournalLoss() cannot
// declare this: its own doc comment scopes it to the demote direction, and
// the shape graph has NO promote-direction loss-profile mechanism at all
// (calendarstockshapes.cpp's ical->canon edge is hard-coded LossProfile{}
// for every kind, for every incidence kind — a structural gap, not this
// item's to build). Building that mechanism is out of IP.10's scope.
QStringList vjournalExpectedLostList()
{
    QStringList names = droppedRfcNames(Kalburator::Calendar::canonToVjournalLoss());
    names << QStringLiteral("RELATED-TO");
    names.sort();
    return names;
}

const QHash<QString, KindFidelityExpectation>& expectedLossTable()
{
    static const QHash<QString, KindFidelityExpectation> table = {
        // IP.6 (2026-09-02) landed: RELATED-TO now round-trips (Amendment 1
        // §A.3.2), and COMMENT/CONTACT now round-trip (O91, common-field
        // extraction). What remains is exactly three permanent, ratified
        // drops — GEO (O86: dropped entirely rather than hand-serialize
        // around the upstream kcalendarcore corruption), REQUEST-STATUS
        // (O91: upstream, KCalendarCore has no accessor for it at all, not
        // fixable in any emitter), and RESOURCES (O94, new, filed by this
        // item: KCalendarCore::ICalFormat 6.29.0 never reads or writes a
        // RESOURCES line at all, even though the object model's
        // resources()/setResources() work correctly — this CORRECTS O91's
        // claim that resources() "round-trips fine through KCalendarCore's
        // own ICalFormat", which was verified wrong specifically for
        // RESOURCES; see incidencecommonfields.h's promoteResources() doc
        // comment and the IP.6 return receipt for the full probe).
        // Deliberately literal, not derived — see the note above
        // expectedLossTable().
        { QStringLiteral("vevent"), {
            { QStringLiteral("GEO"), QStringLiteral("REQUEST-STATUS"),
              QStringLiteral("RESOURCES") },
            true
        } },
        // O83 CLOSED by IP.6 commit 2 (all seven properties now round-trip
        // via incidencecommonfields.cpp), DERIVED from
        // Kalburator::Calendar::canonToVtodoIcalLoss(). GEO is now cleanly
        // Dropped (removed from vtodocanonfields.cpp entirely, O86) rather
        // than Degraded — its NAME no longer survives either, so
        // promote->demote->promote is a fixpoint again (expectFixpoint
        // flipped to true below).
        { QStringLiteral("vtodo"), {
            droppedRfcNames(Kalburator::Calendar::canonToVtodoIcalLoss()),
            true    // O86 RESOLVED: GEO removed entirely, no more fixpoint break
        } },
        // O87 CLOSED by IP.10 (2026-09-02) — RECURRENCE-ID identity, RRULE/
        // RDATE/EXDATE (the single "recurrence" PropertyId covers all
        // three), ORGANIZER, ATTENDEE and ATTACH all round-trip now via
        // IP.6's incidencecommonfields.cpp plus new journal-specific
        // recurrence/recurrenceId code. RELATED-TO is the one exception:
        // wired identically (promoteRelatedTo/demoteRelatedTo) but blocked
        // upstream on the PROMOTE side only — see vjournalExpectedLostList()
        // above and FINDINGS O95. What remains, permanently: REQUEST-STATUS
        // (upstream, no KCalendarCore accessor exists at all).
        { QStringLiteral("vjournal"), {
            vjournalExpectedLostList(),
            true
        } },
    };
    return table;
}

// ---------------------------------------------------------------------------
// Maximal RFC 5545 fixtures — one "master" (a standalone, non-exception
// instance) plus one "exception" (a detached RECURRENCE-ID occurrence) per
// kind, mirroring tst_calendar_kind_dispatch.cpp's established convention:
// a real exception instance does not also carry its own RRULE, so the two
// physically-realistic shapes are covered by two fixtures whose SOURCE
// property sets are unioned before computing loss — the gate only needs
// each RFC-permitted property to appear in SOME promoted instance, not all
// at once in one instance that would not occur on the wire.
//
// Every property below is taken directly from the RFC 5545 grammar for
// that component (not from what eventcanonfields.cpp / vtodocanonfields.cpp
// / journalcanonfields.cpp currently read) — see the ABNF cross-check in
// the IP.8 return receipt. VALARM here carries a single plain start-
// relative trigger deliberately: all FOUR trigger forms plus the `enabled`
// flag get their own dedicated sub-gate below (PLAN.md's explicit ask),
// so the main fixture does not need to duplicate that coverage.

const QByteArray kVeventMaster = QByteArrayLiteral(
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//IP8//EN\r\nBEGIN:VEVENT\r\n"
    "UID:ip8-ev-1\r\nDTSTAMP:20260101T000000Z\r\nCREATED:20260101T000000Z\r\n"
    "LAST-MODIFIED:20260102T000000Z\r\nSEQUENCE:3\r\nSUMMARY:Ev\r\nDESCRIPTION:D\r\n"
    "LOCATION:L\r\nSTATUS:CONFIRMED\r\nCLASS:PRIVATE\r\nTRANSP:OPAQUE\r\n"
    "DTSTART:20260201T100000Z\r\nDTEND:20260201T110000Z\r\nPRIORITY:5\r\n"
    "RRULE:FREQ=DAILY;COUNT=3\r\nRDATE:20260301T100000Z\r\nEXDATE:20260202T100000Z\r\n"
    "CATEGORIES:a,b\r\nURL:http://example.com/e\r\n"
    "COLOR:red\r\nGEO:1.5;2.5\r\nRELATED-TO:parent-1\r\n"
    "ORGANIZER:mailto:o@example.com\r\nATTENDEE;CN=A:mailto:a@example.com\r\n"
    "ATTACH:http://example.com/f.pdf\r\n"
    "COMMENT:a comment\r\nCONTACT:Jane Doe\\, +1-555-0100\r\n"
    "RESOURCES:Projector,VCR\r\nREQUEST-STATUS:2.0;Success\r\n"
    "BEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-PT15M\r\nDESCRIPTION:r\r\nEND:VALARM\r\n"
    "X-CUSTOM-THING:zz\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");

const QByteArray kVeventException = QByteArrayLiteral(
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//IP8//EN\r\nBEGIN:VEVENT\r\n"
    "UID:ip8-ev-1\r\nDTSTAMP:20260101T000000Z\r\n"
    "RECURRENCE-ID;RANGE=THISANDFUTURE:20260608T100000Z\r\n"
    "SUMMARY:Ev (moved)\r\nDTSTART:20260608T110000Z\r\nDTEND:20260608T120000Z\r\n"
    "END:VEVENT\r\nEND:VCALENDAR\r\n");

const QByteArray kVtodoMaster = QByteArrayLiteral(
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//IP8//EN\r\nBEGIN:VTODO\r\n"
    "UID:ip8-td-1\r\nDTSTAMP:20260101T000000Z\r\nCREATED:20260101T000000Z\r\n"
    "LAST-MODIFIED:20260102T000000Z\r\nSEQUENCE:4\r\nSUMMARY:Td\r\nDESCRIPTION:D\r\n"
    "LOCATION:L\r\nSTATUS:IN-PROCESS\r\nCLASS:CONFIDENTIAL\r\nPERCENT-COMPLETE:40\r\n"
    "DTSTART:20260201T100000Z\r\nDUE:20260205T100000Z\r\nPRIORITY:2\r\n"
    "RRULE:FREQ=WEEKLY;COUNT=5\r\nRDATE:20260215T100000Z\r\nEXDATE:20260208T100000Z\r\n"
    "CATEGORIES:a,b\r\nURL:http://example.com/t\r\n"
    "COLOR:blue\r\nGEO:1.5;2.5\r\nRELATED-TO:parent-1\r\n"
    "ORGANIZER:mailto:o@example.com\r\nATTENDEE;CN=B:mailto:b@example.com\r\n"
    "ATTACH:http://example.com/f.pdf\r\n"
    "COMMENT:a comment\r\nCONTACT:Jane Doe\\, +1-555-0100\r\n"
    "RESOURCES:Projector,VCR\r\nREQUEST-STATUS:2.0;Success\r\n"
    "BEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-PT30M\r\nDESCRIPTION:r\r\nEND:VALARM\r\n"
    "X-CUSTOM-THING:qq\r\nEND:VTODO\r\nEND:VCALENDAR\r\n");

const QByteArray kVtodoException = QByteArrayLiteral(
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//IP8//EN\r\nBEGIN:VTODO\r\n"
    "UID:ip8-td-1\r\nDTSTAMP:20260101T000000Z\r\n"
    "RECURRENCE-ID;RANGE=THISANDFUTURE:20260608T100000Z\r\n"
    "SUMMARY:Td (moved)\r\nDUE:20260609T100000Z\r\n"
    "END:VTODO\r\nEND:VCALENDAR\r\n");

// VJOURNAL per RFC 5545 §3.6.3 jourprop: no GEO, LOCATION, PRIORITY,
// RESOURCES, TRANSP, DTEND/DUE/DURATION, PERCENT-COMPLETE, COMPLETED, or
// VALARM — none of those are in the grammar for this component, so (unlike
// the other two kinds) their absence here is RFC-correct, not a missed
// property.
const QByteArray kVjournalMaster = QByteArrayLiteral(
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//IP8//EN\r\nBEGIN:VJOURNAL\r\n"
    "UID:ip8-jr-1\r\nDTSTAMP:20260101T000000Z\r\nCREATED:20260101T000000Z\r\n"
    "LAST-MODIFIED:20260102T000000Z\r\nSEQUENCE:2\r\nSUMMARY:Jr\r\nDESCRIPTION:D\r\n"
    "STATUS:FINAL\r\nCLASS:PRIVATE\r\nDTSTART:20260201T100000Z\r\n"
    "RRULE:FREQ=WEEKLY;BYDAY=MO\r\nRDATE:20260302T100000Z\r\nEXDATE:20260615T090000Z\r\n"
    "CATEGORIES:a,b\r\nURL:http://example.com/j\r\nCOLOR:green\r\n"
    "ORGANIZER:mailto:o@example.com\r\nATTENDEE;CN=C:mailto:c@example.com\r\n"
    "ATTACH:http://example.com/f.pdf\r\nRELATED-TO:other-uid\r\n"
    "COMMENT:a comment\r\nCONTACT:Jane Doe\\, +1-555-0100\r\n"
    "REQUEST-STATUS:2.0;Success\r\n"
    "X-CUSTOM-THING:jj\r\nEND:VJOURNAL\r\nEND:VCALENDAR\r\n");

const QByteArray kVjournalException = QByteArrayLiteral(
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//IP8//EN\r\nBEGIN:VJOURNAL\r\n"
    "UID:ip8-jr-1\r\nDTSTAMP:20260101T000000Z\r\n"
    "RECURRENCE-ID:20260608T100000Z\r\n"
    "SUMMARY:Jr (moved)\r\nDTSTART:20260609T100000Z\r\n"
    "END:VJOURNAL\r\nEND:VCALENDAR\r\n");

// ---------------------------------------------------------------------------
// VALARM sub-gate fixtures: one VEVENT and one VTODO, each carrying all
// four RFC 5545 §3.6.6 trigger forms in one component (start-relative,
// END-relative, absolute VALUE=DATE-TIME, and a REPEAT/DURATION pair) plus
// the `enabled` flag (implicitly true — no source has ever set a
// KDE-specific DISABLED marker, matching a realistic incoming ICS from any
// non-KDE client).

const QByteArray kVeventAlarms = QByteArrayLiteral(
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//IP8//EN\r\nBEGIN:VEVENT\r\n"
    "UID:ip8-al-ev\r\nDTSTAMP:20260101T000000Z\r\nSUMMARY:S\r\n"
    "DTSTART:20260601T090000Z\r\nDTEND:20260601T100000Z\r\n"
    "BEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:rel-start\r\nTRIGGER:-PT15M\r\nEND:VALARM\r\n"
    "BEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:rel-end\r\nTRIGGER;RELATED=END:-PT5M\r\nEND:VALARM\r\n"
    "BEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:absolute\r\nTRIGGER;VALUE=DATE-TIME:20260531T080000Z\r\nEND:VALARM\r\n"
    "BEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:snooze\r\nTRIGGER:-PT30M\r\nREPEAT:3\r\nDURATION:PT5M\r\nEND:VALARM\r\n"
    "END:VEVENT\r\nEND:VCALENDAR\r\n");

const QByteArray kVtodoAlarms = QByteArrayLiteral(
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//IP8//EN\r\nBEGIN:VTODO\r\n"
    "UID:ip8-al-td\r\nDTSTAMP:20260101T000000Z\r\nSUMMARY:S\r\n"
    "DTSTART:20260601T090000Z\r\nDUE:20260601T100000Z\r\n"
    "BEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:rel-start\r\nTRIGGER:-PT15M\r\nEND:VALARM\r\n"
    "BEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:rel-end\r\nTRIGGER;RELATED=END:-PT5M\r\nEND:VALARM\r\n"
    "BEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:absolute\r\nTRIGGER;VALUE=DATE-TIME:20260531T080000Z\r\nEND:VALARM\r\n"
    "BEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:snooze\r\nTRIGGER:-PT30M\r\nREPEAT:3\r\nDURATION:PT5M\r\nEND:VALARM\r\n"
    "END:VTODO\r\nEND:VCALENDAR\r\n");

enum class TriggerForm { StartRelative, EndRelative, Absolute, RepeatDuration };

QString triggerFormLabel(TriggerForm f)
{
    switch (f) {
    case TriggerForm::StartRelative: return QStringLiteral("start-relative");
    case TriggerForm::EndRelative:   return QStringLiteral("end-relative");
    case TriggerForm::Absolute:      return QStringLiteral("absolute");
    case TriggerForm::RepeatDuration:return QStringLiteral("repeat/duration");
    }
    return {};
}

/// Index of each VALARM's DESCRIPTION in kVeventAlarms/kVtodoAlarms, in
/// source order: 0=rel-start, 1=rel-end, 2=absolute, 3=snooze(repeat/dur).
KCalendarCore::Alarm::Ptr nthAlarm(const QByteArray& icalBytes, int n, bool isTodo)
{
    KCalendarCore::ICalFormat fmt;
    const auto inc = fmt.fromString(QString::fromUtf8(icalBytes));
    if (!inc)
        return {};
    const auto alarms = inc->alarms();
    if (n < 0 || n >= alarms.size())
        return {};
    return alarms.at(n);
}

/// True iff `alarm`'s trigger form matches `form` AND carries the
/// RFC-correct value for that form (not merely "some start-relative
/// alarm" — O79's corruption specifically produces a start-relative
/// alarm with a WRONG offset, so form identity alone would not catch it).
bool triggerFormMatches(const KCalendarCore::Alarm::Ptr& alarm, TriggerForm form)
{
    if (!alarm)
        return false;
    switch (form) {
    case TriggerForm::StartRelative:
        return alarm->hasStartOffset() && alarm->startOffset().asSeconds() == -900;
    case TriggerForm::EndRelative:
        return alarm->hasEndOffset() && alarm->endOffset().asSeconds() == -300;
    case TriggerForm::Absolute:
        return alarm->hasTime()
            && alarm->time().toUTC() == QDateTime(QDate(2026, 5, 31), QTime(8, 0), QTimeZone::utc());
    case TriggerForm::RepeatDuration:
        // The pairing rides on top of a start-relative trigger (-PT30M).
        return alarm->hasStartOffset() && alarm->startOffset().asSeconds() == -1800
            && alarm->repeatCount() == 3 && alarm->snoozeTime().asSeconds() == 300;
    }
    return false;
}

} // namespace

class TestIncidenceRfc5545Fidelity : public QObject {
    Q_OBJECT
private slots:

    // -------------------------------------------------------------------
    // Property-loss gate: PLAN.md IP.8 steps 1-3 + 5.
    // -------------------------------------------------------------------

    void veventRfc5545RoundTrip()
    {
        runKindCase(QStringLiteral("vevent"), kVeventMaster, kVeventException);
    }

    void vtodoRfc5545RoundTrip()
    {
        runKindCase(QStringLiteral("vtodo"), kVtodoMaster, kVtodoException);
    }

    void vjournalRfc5545RoundTrip()
    {
        runKindCase(QStringLiteral("vjournal"), kVjournalMaster, kVjournalException);
    }

    // -------------------------------------------------------------------
    // VALARM sub-gate: four trigger forms x {VEVENT, VTODO} x
    // {form preserved, enabled preserved}. VJOURNAL takes no VALARM per
    // RFC 5545 (§3.6.3) and is correctly excluded — see O79's note that
    // this is by design, not an oversight.
    // -------------------------------------------------------------------

    // IP.4: all eight slots' O79 QEXPECT_FAILs (VEVENT end-relative/
    // absolute/repeat-duration corruption) and O85 QEXPECT_FAILs (every
    // alarm coming back disabled) are REMOVED — both are fixed by the new
    // shared alarmshape module. Real green, not vacuous: this file's
    // runAlarmCase() would fail for real if either regressed.
    void veventAlarmStartRelative() { runAlarmCase(false, 0, TriggerForm::StartRelative); }
    void veventAlarmEndRelative()   { runAlarmCase(false, 1, TriggerForm::EndRelative);   }
    void veventAlarmAbsolute()      { runAlarmCase(false, 2, TriggerForm::Absolute);      }
    void veventAlarmRepeatDuration(){ runAlarmCase(false, 3, TriggerForm::RepeatDuration);}

    void vtodoAlarmStartRelative()  { runAlarmCase(true,  0, TriggerForm::StartRelative); }
    void vtodoAlarmEndRelative()    { runAlarmCase(true,  1, TriggerForm::EndRelative);   }
    void vtodoAlarmAbsolute()       { runAlarmCase(true,  2, TriggerForm::Absolute);      }
    void vtodoAlarmRepeatDuration() { runAlarmCase(true,  3, TriggerForm::RepeatDuration);}

private:
    void runKindCase(const QString& kindName, const QByteArray& master, const QByteArray& exception)
    {
        using Kalburator::Calendar::ICalToCanonStage;
        using Kalburator::Calendar::CanonToICalStage;
        ICalToCanonStage promote;
        CanonToICalStage demote;

        const QByteArray c1m = promote.transform(master);
        QVERIFY2(!c1m.isEmpty(), qPrintable(kindName + QStringLiteral(": master must promote to non-empty canon")));
        const QByteArray ical2 = demote.transform(c1m);
        const QByteArray c2m   = promote.transform(ical2);

        const QByteArray c1e = promote.transform(exception);
        QVERIFY2(!c1e.isEmpty(), qPrintable(kindName + QStringLiteral(": exception instance must promote to non-empty canon")));
        const QByteArray icalE2 = demote.transform(c1e);

        const QSet<QString> before = icalPropertyNames(master) + icalPropertyNames(exception);
        const QSet<QString> after  = icalPropertyNames(ical2)  + icalPropertyNames(icalE2);
        const QSet<QString> lost   = before - after;
        const QSet<QString> gained = after - before;

        const auto& expectation = expectedLossTable().value(kindName);

        // --- named, per-defect QEXPECT_FAIL breakdown -----------------------
        // IP.6 (2026-09-02): VEVENT's GEO/RELATED-TO/COMMENT/CONTACT/
        // RESOURCES/REQUEST-STATUS breakdown and VTODO's O83/O91 breakdown
        // are REMOVED from here — every property either round-trips now
        // (RELATED-TO, COMMENT, CONTACT, RESOURCES, and O83's seven) or is
        // a permanent, ratified drop with no future item to attribute a
        // QEXPECT_FAIL to (GEO — dropped by design, O86; REQUEST-STATUS —
        // upstream, no KCalendarCore accessor exists at all). Both are
        // still covered, honestly, by the non-QEXPECT_FAIL "real gate"
        // QCOMPARE below, via expectedLossTable()'s permanent entries —
        // exactly the same treatment onlineMeeting/eventType/etc already
        // get in canonToIcalLoss() with no dedicated named assertion.
        //
        // IP.10 (2026-09-02): VJOURNAL's QEXPECT_FAIL block REMOVED — O87's
        // seven properties (ATTACH, ATTENDEE, EXDATE, ORGANIZER,
        // RECURRENCE-ID, RELATED-TO, RRULE, +RDATE per O91) all round-trip
        // now (organizer/attendees/attachments/relatedTo via IP.6's
        // incidencecommonfields, recurrence/recurrenceId via new
        // journal-specific code modeled on VTODO's W1 shape). What remains
        // for VJOURNAL — REQUEST-STATUS (upstream, permanent) — is covered
        // by the real gate below, same treatment as VEVENT/VTODO's.

        // --- the real gate: lost must equal EXACTLY the declared allow-list -
        // Not wrapped in QEXPECT_FAIL: this is the non-vacuity check. If a
        // closing item lands and removes a drop, this starts failing for
        // real until that item's QEXPECT_FAIL above is removed too — the
        // forcing function PLAN.md asks for ("if any comes up green, the
        // fixture is not maximal enough" — the mirror image is "if the
        // declared list stops matching reality, the list is stale").
        QCOMPARE(sortedList(lost), expectation.expectedLost);

        // Sanity net: no phantom property should ever appear that was not
        // in the source. Not one of PLAN.md's named acceptance items, but
        // cheap and it would catch a real regression class (see
        // journalcanonfields.cpp's former unconditional `classification`
        // insert — a phantom-key bug fixed by IP.10, switching to
        // incidencecommonfields.cpp's guarded promoteClassification()/
        // demoteClassification()).
        QVERIFY2(gained.isEmpty(),
                 qPrintable(QStringLiteral("%1: unexpected GAINED propert(y/ies) not in source: %2")
                                .arg(kindName, sortedList(gained).join(QStringLiteral(", ")))));

        // --- fixpoint: promote(demote(promote(source))) canon-stable? ------
        if (!expectation.expectFixpoint) {
            QEXPECT_FAIL("", "IP.6 / O86 — VTODO promote->demote->promote is NOT a "
                              "fixpoint: GEO survives the first promote and is "
                              "corrupted (not merely dropped) by kcalendarcore's "
                              "GEO serializer, so the second promote cannot parse it "
                              "back", Continue);
        }
        QCOMPARE(c1m, c2m);
    }

    void runAlarmCase(bool isTodo, int alarmIndex, TriggerForm form)
    {
        using Kalburator::Calendar::ICalToCanonStage;
        using Kalburator::Calendar::CanonToICalStage;
        ICalToCanonStage promote;
        CanonToICalStage demote;

        const QByteArray& src = isTodo ? kVtodoAlarms : kVeventAlarms;
        const auto srcAlarm = nthAlarm(src, alarmIndex, isTodo);
        QVERIFY2(srcAlarm, "fixture alarm must parse");
        QVERIFY2(srcAlarm->enabled(), "fixture alarm must be enabled=true in the source "
                                       "(no source ever sets the KDE DISABLED marker)");

        const QByteArray canon = promote.transform(src);
        QVERIFY2(!canon.isEmpty(), "must promote to non-empty canon");
        const QByteArray ical2 = demote.transform(canon);
        const auto rtAlarm = nthAlarm(ical2, alarmIndex, isTodo);
        QVERIFY2(rtAlarm, "demoted alarm must reparse");

        const QString label = QStringLiteral("%1 %2 trigger")
                                   .arg(isTodo ? QStringLiteral("VTODO") : QStringLiteral("VEVENT"),
                                        triggerFormLabel(form));

        // IP.4 / O79: trigger form (and its exact value) must survive
        // promote->demote for every form, on both kinds — the shared
        // alarmshape module branches on the alarm's actual trigger form
        // instead of reading startOffset() unconditionally.
        QVERIFY2(triggerFormMatches(rtAlarm, form),
                 qPrintable(label + QStringLiteral(" must survive promote->demote with its exact form and value")));

        // IP.4 / O85: alarmFromJson() always calls setEnabled(true) — RFC
        // 5545 has no "disabled alarm" wire representation, so every
        // demoted alarm must come back enabled.
        QVERIFY2(rtAlarm->enabled(), qPrintable(label + QStringLiteral(": alarm must still be enabled after round trip")));
    }
};

QTEST_GUILESS_MAIN(TestIncidenceRfc5545Fidelity)
#include "tst_incidence_rfc5545_fidelity.moc"
