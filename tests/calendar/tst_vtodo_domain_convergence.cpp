// IP.11 (incidence-parity campaign) — VTODO domain-crossing convergence
// proof.
//
// O89: a VTODO reaches one of two canonical shapes depending on transport
// metadata: `{calendar,canon}` (via `ICalToCanonStage`'s kind dispatch) or
// `{todo,canon}` (via the todo domain's own `{todo,ical-vtodo}` →
// `{todo,canon}` edge, `VTodoToCanonStage`). PLAN.md Amendment 2 §B.4
// (PlanStan Q1 → (a) converge, ratified 2026-09-02) rescoped this item from
// "choose a route" to "prove the two representations are equivalent" —
// IP.3/IP.6/IP.9 already did the convergence work; this file is the
// crossing gate that proves it landed.
//
// Both promote paths dispatch to the SAME underlying emitter —
// icalcanonstages.cpp:56 calls `Kalburator::Todo::todoFieldsToCanon()`
// directly for the calendar-domain leg, and `vtodocanonstages.cpp`'s
// `VTodoToCanonStage` calls the identical function for the todo domain's
// own leg. So this is fundamentally a proof that wrapping ONE
// implementation in two different envelope/domain contexts produces
// equivalent canon — not a comparison of two independent implementations.
//
// New file rather than a new slot in `tst_gm_pipeline_convergence.cpp`
// (house rule O64's usual home for crossing gates): argued in the IP.11
// return receipt. Short version — that file's `reportAndAssertWithin()`
// pattern compares a demote→re-promote round trip through ONE vendor
// against THAT vendor's OWN declared loss profile; this gate compares TWO
// INDEPENDENT PROMOTES of the SAME source against EACH OTHER, with no
// single "declared loss profile" governing the comparison (the two
// legitimate divergences — `_canon.domain`/`_canon.kind`, and the four
// vendor-only keys — are asserted explicitly instead). Forcing this into
// `reportAndAssertWithin()`'s shape would not fit; placed alongside
// `tst_incidence_rfc5545_fidelity.cpp` (IP.8) instead, since it reuses that
// item's maximal-RFC-5545-VTODO fixture discipline and its file-level
// "why a new file" precedent.

#include <QTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include "icalcanonstages.h"
#include "vtodocanonstages.h"
#include "googletaskcanonstages.h"
#include "canonenvelope.h"
#include "calendarstockshapes.h"

namespace {

using Kalburator::Shape::CanonEnvelope::parse;

// Maximal RFC 5545 §3.6.2 VTODO — every property the grammar permits on
// this component, built directly from the RFC (not from what our own
// emitters happen to read), matching IP.8's discipline
// (tst_incidence_rfc5545_fidelity.cpp's kVtodoMaster, reproduced here
// rather than shared across TUs — each QTest executable is self-contained
// by this codebase's convention). GEO and RESOURCES and REQUEST-STATUS are
// included even though none of the three round-trips today (O86/O94/O91) —
// their absence from BOTH canon outputs below is itself part of the
// equivalence proof: both domains lose them identically, because both ride
// the same emitter on the same input bytes.
const QByteArray kMaximalVtodo = QByteArrayLiteral(
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//IP11//EN\r\nBEGIN:VTODO\r\n"
    "UID:ip11-td-1\r\nDTSTAMP:20260101T000000Z\r\nCREATED:20260101T000000Z\r\n"
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

/// Top-level keys whose JSON values differ between two canon objects,
/// excluding `_canon` — the envelope difference is SUPPOSED to differ
/// (domain, and vevent-default vs. explicit "vtodo" kind tagging) and is
/// asserted explicitly, separately, in the slot below rather than folded
/// into this generic diff.
QStringList diffKeysExcludingEnvelope(const QJsonObject& a, const QJsonObject& b)
{
    QSet<QString> keys;
    for (auto it = a.constBegin(); it != a.constEnd(); ++it)
        keys.insert(it.key());
    for (auto it = b.constBegin(); it != b.constEnd(); ++it)
        keys.insert(it.key());
    keys.remove(QStringLiteral("_canon"));

    QStringList out;
    for (const QString& k : keys) {
        const bool inA = a.contains(k);
        const bool inB = b.contains(k);
        if (inA != inB || a.value(k) != b.value(k))
            out << k;
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

class TestVtodoDomainConvergence : public QObject {
    Q_OBJECT
private slots:

    // The item's deliverable (Amendment 2 §B.4 point 1): the SAME VTODO
    // promoted through {calendar,canon} and through {todo,canon} yields
    // equivalent canon.
    void maximalVtodoConvergesAcrossDomains()
    {
        Kalburator::Calendar::ICalToCanonStage calPromote;
        Kalburator::Todo::VTodoToCanonStage todoPromote;

        const QJsonObject canonCal = parse(calPromote.transform(kMaximalVtodo));
        QVERIFY2(!canonCal.isEmpty(), "calendar-domain promote failed");
        const QJsonObject canonTodo = parse(todoPromote.transform(kMaximalVtodo));
        QVERIFY2(!canonTodo.isEmpty(), "todo-domain promote failed");

        // Envelope difference — SUPPOSED to differ, asserted explicitly so
        // a future regression that makes them accidentally equal (e.g. a
        // kind leaking across domains) is just as visible as one that
        // makes them diverge.
        const QJsonObject calEnvelope = canonCal.value(QStringLiteral("_canon")).toObject();
        QCOMPARE(calEnvelope.value(QStringLiteral("domain")).toString(),
                 QStringLiteral("calendar"));
        QCOMPARE(calEnvelope.value(QStringLiteral("kind")).toString(),
                 QStringLiteral("vtodo"));

        const QJsonObject todoEnvelope = canonTodo.value(QStringLiteral("_canon")).toObject();
        QCOMPARE(todoEnvelope.value(QStringLiteral("domain")).toString(),
                 QStringLiteral("todo"));
        QVERIFY2(!todoEnvelope.contains(QStringLiteral("kind")),
                 "todo-domain canon unexpectedly carries a _canon.kind key "
                 "(stampEnvelope's kind parameter is calendar-domain-only)");

        // uid must agree — both stages read it from the SAME parsed
        // KCalendarCore::Todo, off the SAME iCal bytes.
        QCOMPARE(canonCal.value(QStringLiteral("uid")).toString(),
                 canonTodo.value(QStringLiteral("uid")).toString());

        // Everything else — every RFC 5545 property this fixture carries,
        // AND providerExtras (the generic X- passthrough, including
        // X-CUSTOM-THING above) — must be IDENTICAL: both promote paths
        // call the exact same Kalburator::Todo::todoFieldsToCanon() with
        // the same iCal bytes (icalcanonstages.cpp:56 /
        // vtodocanonstages.cpp's VTodoToCanonStage), so there is genuinely
        // nothing left to diverge for an iCal-sourced VTODO.
        const QStringList diffs = diffKeysExcludingEnvelope(canonCal, canonTodo);
        for (const QString& k : diffs)
            qInfo("maximalVtodoConvergesAcrossDomains: diff %s", qPrintable(k));
        QVERIFY2(diffs.isEmpty(),
                 "calendar-domain and todo-domain canon diverge outside the "
                 "envelope for a maximal RFC 5545 VTODO — see qInfo above "
                 "for the offending key(s)");
    }

    // Amendment 2 §B.4 point 2: checklistItems/linkedResources/parentUid/
    // sortOrder are catalogued in todocanonproperties.cpp and absent from
    // calendarcanonproperties.cpp. Investigated, NOT an IP.3 gap: neither
    // the calendar-domain iCal leg (icalcanonstages.cpp →
    // todoFieldsToCanon) NOR the todo-domain's own iCal leg
    // (VTodoToCanonStage → the SAME function) ever populates these four
    // keys — confirmed by the previous slot (the two paths are
    // byte-identical outside the envelope for an iCal source, so if either
    // path produced one of these keys the other would too, and the diff
    // would have failed). The four keys arrive EXCLUSIVELY from the Google
    // Tasks / MS To-Do vendor JSON promote stages
    // (googletaskcanonstages.cpp, mstodotaskcanonstages.cpp), which speak
    // {todo,google-task} / {todo,ms-todotask} — shapes that exist ONLY
    // under the todo domain. Grep confirms zero references to any of the
    // four keys anywhere under src/calendar/ (eventcanonfields.cpp,
    // journalcanonfields.cpp, googlecanonstages.cpp,
    // mseventcanonstages.cpp) — the calendar-domain vendor legs (Google
    // Calendar events, MS Graph events) never touch task hierarchy/
    // checklist/sort-order concepts at all. So there is no "cross both
    // domains" scenario for these fields to diverge IN: their origin never
    // touches iCal, and has no calendar-domain counterpart to compare
    // against. calendarVendorOnlyIds() (calendarcanonproperties.cpp)
    // correctly does not list them, exactly as its own comment says.
    void vendorOnlyKeysHaveNoCalendarDomainCounterpart()
    {
        // (a) The maximal iCal-sourced VTODO produces neither key on
        // either domain leg — the "modulo" exclusion in Amendment 2 §B.4
        // point 1 hides nothing for the iCal case.
        Kalburator::Calendar::ICalToCanonStage calPromote;
        Kalburator::Todo::VTodoToCanonStage todoPromote;
        const QJsonObject canonCal = parse(calPromote.transform(kMaximalVtodo));
        const QJsonObject canonTodo = parse(todoPromote.transform(kMaximalVtodo));
        const QStringList vendorOnlyKeys = {
            QStringLiteral("checklistItems"), QStringLiteral("linkedResources"),
            QStringLiteral("parentUid"), QStringLiteral("sortOrder")
        };
        for (const QString& key : vendorOnlyKeys) {
            QVERIFY2(!canonCal.contains(key),
                     qPrintable(QStringLiteral(
                         "calendar-domain canon unexpectedly carries "
                         "vendor-only key '%1'").arg(key)));
            QVERIFY2(!canonTodo.contains(key),
                     qPrintable(QStringLiteral(
                         "todo-domain iCal-leg canon unexpectedly carries "
                         "vendor-only key '%1'").arg(key)));
        }

        // (b) Where they DO legitimately come from: a real Google Tasks
        // vendor JSON payload populates parentUid/sortOrder in
        // {todo,canon} via the "parent"/"position" wire fields
        // (googletaskcanonstages.cpp:119-127).
        const QByteArray googleTaskJson =
            "{\"id\":\"tid-1\",\"title\":\"Ship EEE\","
            "\"parent\":\"parent-tid\",\"position\":\"00000000012345678901\"}";
        Kalburator::Todo::GoogleTaskToCanonStage gPromote;
        const QJsonObject canonFromVendor = parse(gPromote.transform(googleTaskJson));
        QVERIFY2(!canonFromVendor.isEmpty(), "google-task promote failed");
        QCOMPARE(canonFromVendor.value(QStringLiteral("parentUid")).toString(),
                 QStringLiteral("parent-tid"));
        QVERIFY2(canonFromVendor.contains(QStringLiteral("sortOrder")),
                 "google-task promote did not produce sortOrder from 'position'");

        // (c) Structural confirmation that this vendor leg has no
        // calendar-domain counterpart to converge against: the calendar
        // domain's own peer-shape registry names only
        // ical/org-ical/google-event/ms-event — no google-task, no
        // ms-todotask peer exists under the calendar domain at all.
        const Kalburator::Calendar::CalendarStockShapes calShapes;
        for (const auto& peer : calShapes.peerShapes()) {
            const QString enc = peer.first.encoding.toString();
            QVERIFY2(enc != QStringLiteral("google-task"),
                     "calendar domain unexpectedly carries a google-task peer shape");
            QVERIFY2(enc != QStringLiteral("ms-todotask"),
                     "calendar domain unexpectedly carries an ms-todotask peer shape");
        }
    }
};

QTEST_MAIN(TestVtodoDomainConvergence)
#include "tst_vtodo_domain_convergence.moc"
