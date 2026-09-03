#include <QTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

#include "canonenvelope.h"
#include "icalcanonstages.h"
#include "calendardomaindefinition.h"
#include "calendarstockshapes.h"
#include "shaperegistries.h"
#include "lossprofile.h"

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>

using Kalburator::Shape::CanonEnvelope::parse;
using Kalburator::Calendar::ICalToCanonStage;
using Kalburator::Calendar::CanonToICalStage;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::Shape;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::LossKind;

namespace {

// Build a ShapeRegistries with the calendar domain fully registered.
Kalburator::Shape::ShapeRegistries makeCalendarRegistries()
{
    Kalburator::Shape::ShapeRegistries regs;
    auto& reg = regs.transformation;

    Kalburator::Calendar::CalendarDomainDefinition def;
    const auto spine = def.canonicalSpine();
    if (!spine.isEmpty()) {
        const auto& [rootShape, rootCat] = spine.first();
        reg.registerShape(rootShape, rootCat);
        reg.declareCanonical(def.domain(), rootShape);
        for (int i = 1; i < spine.size(); ++i) {
            const auto& [s, cat] = spine.at(i);
            reg.registerShape(s, cat);
            reg.appendCanonicalVersion(def.domain(), s);
        }
    }

    Kalburator::Calendar::CalendarStockShapes shapes;
    for (const auto& [shape, cat] : shapes.peerShapes())
        reg.registerShape(shape, cat);
    for (const auto& edge : shapes.edges())
        reg.registerEdge(edge);

    return regs;
}

KCalendarCore::Event::Ptr parseEvent(const QByteArray &bytes)
{
    KCalendarCore::ICalFormat fmt;
    auto inc = fmt.fromString(QString::fromUtf8(bytes));
    return inc.dynamicCast<KCalendarCore::Event>();
}

// Isolate just the VEVENT component's own lines, excluding any VTIMEZONE.
// KCalendarCore's own serializer regenerates a full VTIMEZONE (with
// legitimate STANDARD/DAYLIGHT RRULEs expressing DST transitions) for any
// TZID-based event — that is correct iCal and must not be mistaken for
// event recurrence contamination (the N1 bug is scoped to the event's own
// component only).
QByteArray eventComponentOf(const QByteArray &ical)
{
    const int b = ical.indexOf("BEGIN:VEVENT");
    if (b < 0) return {};
    const int e = ical.indexOf("END:VEVENT", b);
    if (e < 0) return {};
    return ical.mid(b, e - b);
}

// A representative VEVENT with core fields, RRULE, EXDATE, and ATTENDEE.
const QByteArray kTestIcal =
    "BEGIN:VCALENDAR\r\n"
    "VERSION:2.0\r\n"
    "PRODID:-//Test//Test//EN\r\n"
    "BEGIN:VEVENT\r\n"
    "UID:test-event-uid-12345\r\n"
    "SUMMARY:Team Meeting\r\n"
    "DESCRIPTION:Weekly sync\r\n"
    "DTSTART:20260601T090000Z\r\n"
    "DTEND:20260601T100000Z\r\n"
    "STATUS:CONFIRMED\r\n"
    "CATEGORIES:Work,Meetings\r\n"
    "RRULE:FREQ=WEEKLY;BYDAY=MO\r\n"
    "EXDATE:20260615T090000Z\r\n"
    "ORGANIZER;CN=Alice:mailto:alice@example.com\r\n"
    "ATTENDEE;CN=Bob;PARTSTAT=ACCEPTED;RSVP=TRUE:mailto:bob@example.com\r\n"
    "END:VEVENT\r\n"
    "END:VCALENDAR\r\n";

// Expected recurrence lines verbatim (no CR — extractRecurrenceLines trims them).
const QByteArray kExpectedRRule  = "RRULE:FREQ=WEEKLY;BYDAY=MO";
const QByteArray kExpectedExdate = "EXDATE:20260615T090000Z";

// N1 regression fixtures — a one-off VEVENT with a TZID DTSTART carries a
// full VTIMEZONE block whose STANDARD/DAYLIGHT sub-components have their own
// RRULE (the DST transition rule). A whole-blob line scan harvests those as
// if they were the event's own recurrence — the corruption documented in
// PlanStan's sync-nonconvergence-vtimezone-corruption-and-dav-transport.md.
const QByteArray kVtimezoneBlock =
    "BEGIN:VTIMEZONE\r\n"
    "TZID:America/New_York\r\n"
    "BEGIN:STANDARD\r\n"
    "DTSTART:20071104T020000\r\n"
    "RRULE:FREQ=YEARLY;BYMONTH=11;BYDAY=1SU\r\n"
    "TZOFFSETFROM:-0400\r\n"
    "TZOFFSETTO:-0500\r\n"
    "TZNAME:EST\r\n"
    "END:STANDARD\r\n"
    "BEGIN:DAYLIGHT\r\n"
    "DTSTART:20070311T020000\r\n"
    "RRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=2SU\r\n"
    "TZOFFSETFROM:-0500\r\n"
    "TZOFFSETTO:-0400\r\n"
    "TZNAME:EDT\r\n"
    "END:DAYLIGHT\r\n"
    "END:VTIMEZONE\r\n";

const QByteArray kEventWithVtimezoneNoOwnRecurrence =
    "BEGIN:VCALENDAR\r\n"
    "VERSION:2.0\r\n"
    "PRODID:-//Test//Test//EN\r\n" + kVtimezoneBlock +
    "BEGIN:VEVENT\r\n"
    "UID:tz-event-one-off@example.com\r\n"
    "DTSTAMP:20260601T120000Z\r\n"
    "DTSTART;TZID=America/New_York:20260615T100000\r\n"
    "DTEND;TZID=America/New_York:20260615T110000\r\n"
    "SUMMARY:One-off Meeting\r\n"
    "END:VEVENT\r\n"
    "END:VCALENDAR\r\n";

const QByteArray kEventWithVtimezoneAndOwnRecurrence =
    "BEGIN:VCALENDAR\r\n"
    "VERSION:2.0\r\n"
    "PRODID:-//Test//Test//EN\r\n" + kVtimezoneBlock +
    "BEGIN:VEVENT\r\n"
    "UID:tz-event-recurring@example.com\r\n"
    "DTSTAMP:20260601T120000Z\r\n"
    "DTSTART;TZID=America/New_York:20260615T100000\r\n"
    "DTEND;TZID=America/New_York:20260615T110000\r\n"
    "SUMMARY:Recurring Meeting\r\n"
    "RRULE:FREQ=WEEKLY\r\n"
    "END:VEVENT\r\n"
    "END:VCALENDAR\r\n";

// A folded RRULE (RFC 5545 §3.1: a continuation line starts with a single
// space). Must be unfolded to one logical line before matching.
const QByteArray kEventWithFoldedRrule =
    "BEGIN:VCALENDAR\r\n"
    "VERSION:2.0\r\n"
    "PRODID:-//Test//Test//EN\r\n"
    "BEGIN:VEVENT\r\n"
    "UID:folded-rrule-event@example.com\r\n"
    "DTSTAMP:20260601T120000Z\r\n"
    "DTSTART:20260601T090000Z\r\n"
    "DTEND:20260601T100000Z\r\n"
    "SUMMARY:Folded RRULE\r\n"
    "RRULE:FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR;WKST=MO;UNTIL=2026123\r\n"
    " 1T000000Z\r\n"
    "END:VEVENT\r\n"
    "END:VCALENDAR\r\n";

const QByteArray kExpectedUnfoldedRrule =
    "RRULE:FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR;WKST=MO;UNTIL=20261231T000000Z";

} // namespace

class TestCalendarCanonRoundtrip : public QObject {
    Q_OBJECT
private slots:

    void icalToCanonExtractsCoreFields()
    {
        ICalToCanonStage stage;
        const QByteArray out = stage.transform(kTestIcal);
        QVERIFY2(!out.isEmpty(), "ICalToCanonStage returned empty bytes");

        const QJsonObject obj = parse(out);
        QVERIFY2(!obj.isEmpty(), "Canon JSON output is empty object");

        // uid must be present
        const QString uid = obj.value(QStringLiteral("uid")).toString();
        QVERIFY2(!uid.isEmpty(), "uid must be present");
        QCOMPARE(uid, QStringLiteral("test-event-uid-12345"));

        // _canon envelope
        const QJsonObject canon = obj.value(QStringLiteral("_canon")).toObject();
        QCOMPARE(canon.value(QStringLiteral("domain")).toString(),
                 QStringLiteral("calendar"));

        // summary and description
        QCOMPARE(obj.value(QStringLiteral("summary")).toString(),
                 QStringLiteral("Team Meeting"));
        QCOMPARE(obj.value(QStringLiteral("description")).toString(),
                 QStringLiteral("Weekly sync"));

        // status
        QCOMPARE(obj.value(QStringLiteral("status")).toString(),
                 QStringLiteral("confirmed"));

        // start and end must be present
        QVERIFY(obj.contains(QStringLiteral("start")));
        QVERIFY(obj.contains(QStringLiteral("end")));

        // categories
        const QJsonArray cats = obj.value(QStringLiteral("categories")).toArray();
        QStringList catList;
        for (const auto& c : cats)
            catList << c.toString();
        QVERIFY2(catList.contains(QStringLiteral("Work")),
                 "categories must contain 'Work'");
        QVERIFY2(catList.contains(QStringLiteral("Meetings")),
                 "categories must contain 'Meetings'");

        // recurrence must be captured
        const QJsonArray recArr = obj.value(QStringLiteral("recurrence")).toArray();
        QVERIFY2(!recArr.isEmpty(), "recurrence must be captured");
        bool foundRRule = false;
        for (const auto& rv : recArr) {
            if (rv.toString().contains(QStringLiteral("FREQ=WEEKLY")))
                foundRRule = true;
        }
        QVERIFY2(foundRRule, "RRULE:FREQ=WEEKLY must be captured in recurrence");

        // attendees
        const QJsonArray attendees = obj.value(QStringLiteral("attendees")).toArray();
        QVERIFY2(!attendees.isEmpty(), "attendees must be captured");
        bool foundBob = false;
        for (const auto& av : attendees) {
            const auto a = av.toObject();
            if (a.value(QStringLiteral("email")).toString().contains(QStringLiteral("bob")))
                foundBob = true;
        }
        QVERIFY2(foundBob, "bob@example.com must appear in attendees");
    }

    void icalToCanonEmptyInputReturnsEmpty()
    {
        ICalToCanonStage stage;
        QVERIFY(stage.transform(QByteArray{}).isEmpty());
    }

    void icalRoundTripPreservesCoreFieldsAndRecurrence()
    {
        ICalToCanonStage fwd;
        CanonToICalStage rev;

        const QByteArray canon  = fwd.transform(kTestIcal);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");

        const QByteArray output = rev.transform(canon);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty");

        // Parse both via KCalendarCore and compare
        const auto origEvent = parseEvent(kTestIcal);
        const auto outEvent  = parseEvent(output);
        QVERIFY2(origEvent, "could not parse original VEVENT");
        QVERIFY2(outEvent,  "could not parse output VEVENT");

        QCOMPARE(outEvent->summary(),     origEvent->summary());
        QCOMPARE(outEvent->description(), origEvent->description());
        QCOMPARE(outEvent->categories(),  origEvent->categories());

        // Start/end date must survive
        QCOMPARE(outEvent->dtStart().date(), origEvent->dtStart().date());
        if (origEvent->hasEndDate())
            QCOMPARE(outEvent->dtEnd().date(), origEvent->dtEnd().date());

        // Recurrence lines must survive byte-identical (invariants 3/5).
        // extractRecurrenceLines() trims CR; we do the same here before comparing.
        auto normalizeLine = [](const QByteArray &line) -> QByteArray {
            QByteArray n = line;
            n.replace("\r\n", "\n");
            if (n.endsWith('\r'))
                n.chop(1);
            return n.trimmed();
        };

        // Collect all RRULE:/RDATE:/EXDATE: lines from the output (one per LF-split line).
        const auto outputLines = output.split('\n');
        QByteArrayList recurrenceLines;
        for (const QByteArray &raw : outputLines) {
            const QByteArray normed = normalizeLine(raw);
            if (normed.startsWith("RRULE:")  ||
                normed.startsWith("RDATE:")  ||
                normed.startsWith("EXDATE:"))
                recurrenceLines.append(normed);
        }

        QVERIFY2(recurrenceLines.contains(kExpectedRRule),
                 qPrintable(QStringLiteral("RRULE line must survive byte-identical; got: %1")
                     .arg(QString::fromUtf8(output))));
        QVERIFY2(recurrenceLines.contains(kExpectedExdate),
                 qPrintable(QStringLiteral("EXDATE line must survive byte-identical; got: %1")
                     .arg(QString::fromUtf8(output))));
    }

    void icalRoundTripPreservesAttendees()
    {
        ICalToCanonStage fwd;
        CanonToICalStage rev;

        const QByteArray canon  = fwd.transform(kTestIcal);
        const QByteArray output = rev.transform(canon);
        QVERIFY(!output.isEmpty());

        const auto outEvent = parseEvent(output);
        QVERIFY2(outEvent, "could not parse output VEVENT");

        // Attendee bob must survive
        const auto attendees = outEvent->attendees();
        bool foundBob = false;
        for (const auto& a : attendees) {
            if (a.email().contains(QStringLiteral("bob")))
                foundBob = true;
        }
        QVERIFY2(foundBob, "bob@example.com must survive ical->canon->ical");
    }

    // Edge + spine routing tests (Task C5)

    void icalRoutesToCanonDirectly()
    {
        // With spine=[ical, canon] and ical→canon direct edge.
        const auto regs = makeCalendarRegistries();
        const Shape ical{ DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("ical")} };
        const Shape canon{ DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("canon")} };

        const auto pipeline = regs.transformation.compile(ical, canon);
        QVERIFY2(pipeline.has_value(),
                 "compile(ical, canon) must succeed");
    }

    void canonToIcalLossProfileChargesDroppedAndReversible()
    {
        const auto regs = makeCalendarRegistries();
        const Shape canon{ DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("canon")} };
        const Shape ical{ DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("ical")} };

        const auto loss = regs.transformation.inspect(canon, ical);
        QVERIFY2(!loss.isLossless(), "canon->ical must be lossy");

        // onlineMeeting: Dropped
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("onlineMeeting")}),
                 LossKind::Dropped);

        // descriptionHtml: Reversible (→ X-ALT-DESC)
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("descriptionHtml")}),
                 LossKind::Reversible);

        // locations: Simplified
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("locations")}),
                 LossKind::Simplified);

        // IP.6 commit 2: the three permanent, ratified VEVENT drops —
        // geo (O86), requestStatus (O91, upstream), resources (O94,
        // upstream — see incidencecommonfields.h's promoteResources()
        // doc comment).
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("geo")}),
                 LossKind::Dropped);
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("requestStatus")}),
                 LossKind::Dropped);
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("resources")}),
                 LossKind::Dropped);

        // IP.7a / O82: recurrenceRange is now a Degraded row — demote
        // unconditionally refuses to re-emit RANGE=THISANDFUTURE. The bare
        // recurrenceId identity is unaffected and needs no row of its own.
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("recurrenceRange")}),
                 LossKind::Degraded);
    }

    // IP.6 commit 2 (Amendment 1 §A.3.2 + O91) — VEVENT gains RELATED-TO,
    // COMMENT, CONTACT on this edge (the same shared incidencecommonfields
    // code VTODO now uses too — see tst_todo_canon_roundtrip.cpp's
    // vtodoRoundTripPreservesO83Fields()/vtodoCommentsContactsRoundTripResourcesDoesNot()
    // for the VTODO-side twin of this slot).
    void icalRoundTripPreservesRelatedToCommentsContacts()
    {
        const QByteArray ical =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:related-uid\r\n"
            "SUMMARY:Related event\r\n"
            "DTSTART:20260601T100000Z\r\n"
            "DTEND:20260601T110000Z\r\n"
            "RELATED-TO:parent-uid\r\n"
            "COMMENT:a comment\r\n"
            "CONTACT:Jane Doe\\, +1-555-0100\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";

        ICalToCanonStage fwd;
        CanonToICalStage rev;

        const QByteArray canon = fwd.transform(ical);
        QVERIFY(!canon.isEmpty());
        const QJsonObject obj = parse(canon);
        QVERIFY2(!obj.value(QStringLiteral("relatedTo")).toArray().isEmpty(),
                 "relatedTo must be promoted for VEVENT (Amendment 1 §A.3.2)");
        QVERIFY2(!obj.value(QStringLiteral("comments")).toArray().isEmpty(),
                 "comments must be promoted (O91)");
        QVERIFY2(!obj.value(QStringLiteral("contacts")).toArray().isEmpty(),
                 "contacts must be promoted (O91)");

        const QByteArray output = rev.transform(canon);
        QVERIFY2(output.contains("RELATED-TO:"), "RELATED-TO must survive the round trip");
        QVERIFY2(output.contains("COMMENT:"), "COMMENT must survive the round trip");
        QVERIFY2(output.contains("CONTACT:"), "CONTACT must survive the round trip");
    }

    // Task 1: canon envelope kind discriminator
    void envelopeStampsAndReadsKind()
    {
        using namespace Kalburator::Shape::CanonEnvelope;
        QJsonObject obj;
        stampEnvelope(obj, QStringLiteral("calendar"), QStringLiteral("u-1"),
                      QStringLiteral("vtodo"));
        QCOMPARE(kind(obj), QStringLiteral("vtodo"));
        const QJsonObject canon = obj.value(QStringLiteral("_canon")).toObject();
        QCOMPARE(canon.value(QStringLiteral("kind")).toString(),
                 QStringLiteral("vtodo"));

        // Default (no kind) writes no kind key and reads back empty.
        QJsonObject ev;
        stampEnvelope(ev, QStringLiteral("calendar"), QStringLiteral("u-2"));
        QVERIFY(kind(ev).isEmpty());
        QVERIFY(!ev.value(QStringLiteral("_canon")).toObject()
                    .contains(QStringLiteral("kind")));
    }

    // Fix 1 verification: classification=personal → CLASS:PRIVATE + verbatim stash
    void canonPersonalClassificationProducesPrivateAndStash()
    {
        // Build a minimal canon JSON with classification="personal"
        QJsonObject obj;
        obj.insert(QStringLiteral("uid"),            QStringLiteral("personal-class-test-uid"));
        obj.insert(QStringLiteral("summary"),        QStringLiteral("Personal Event"));
        obj.insert(QStringLiteral("classification"), QStringLiteral("personal"));
        QJsonObject startObj;
        startObj.insert(QStringLiteral("dateTime"), QStringLiteral("2026-06-01T09:00:00Z"));
        startObj.insert(QStringLiteral("floating"),  false);
        obj.insert(QStringLiteral("start"), startObj);

        QJsonObject canonMeta;
        canonMeta.insert(QStringLiteral("domain"), QStringLiteral("calendar"));
        obj.insert(QStringLiteral("_canon"), canonMeta);

        const QByteArray canonBytes =
            QJsonDocument(obj).toJson(QJsonDocument::Compact);

        CanonToICalStage rev;
        const QByteArray output = rev.transform(canonBytes);
        QVERIFY2(!output.isEmpty(), "CanonToICalStage must produce output");

        // Must emit CLASS:PRIVATE (invariant 4: best available iCal encoding)
        QVERIFY2(output.contains("CLASS:PRIVATE"),
                 "classification=personal must produce CLASS:PRIVATE in iCal output");

        // Must stash the verbatim original (invariant 4: recoverable).
        // KCalendarCore serializes non-KDE custom properties with an explicit
        // VALUE=TEXT parameter, so match parameter-tolerantly.
        static const QRegularExpression stashRe(
            QStringLiteral("^X-CANON-CLASSIFICATION(?:;[^:\\r\\n]*)?:personal\\r?$"),
            QRegularExpression::MultilineOption);
        QVERIFY2(stashRe.match(QString::fromUtf8(output)).hasMatch(),
                 "classification=personal must stash verbatim value in X-CANON-CLASSIFICATION");
    }

    // N1: a one-off event with a TZID DTSTART must not gain the VTIMEZONE's
    // DST-transition rules as its own recurrence.
    void vtimezoneRecurrenceDoesNotContaminateEventWithoutOwnRRule()
    {
        ICalToCanonStage fwd;
        CanonToICalStage rev;

        const QByteArray canon = fwd.transform(kEventWithVtimezoneNoOwnRecurrence);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");

        const QJsonObject obj = parse(canon);
        const QJsonArray recArr = obj.value(QStringLiteral("recurrence")).toArray();
        QVERIFY2(recArr.isEmpty(),
                 qPrintable(QStringLiteral("recurrence must be empty for a one-off event; got: %1")
                     .arg(QString::fromUtf8(QJsonDocument(recArr).toJson(QJsonDocument::Compact)))));

        const QByteArray output = rev.transform(canon);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty");

        // Scope to the VEVENT's own component — KCalendarCore legitimately
        // regenerates a VTIMEZONE (with its own RRULEs) for a TZID event;
        // only the event's own lines matter for this assertion.
        const QByteArray eventBlock = eventComponentOf(output);
        QVERIFY2(!eventBlock.isEmpty(), "output must contain a VEVENT component");
        QVERIFY2(!eventBlock.contains("RRULE:"),  "the event's own component must contain zero RRULE lines");
        QVERIFY2(!eventBlock.contains("RDATE:"),  "the event's own component must contain zero RDATE lines");
        QVERIFY2(!eventBlock.contains("EXDATE:"), "the event's own component must contain zero EXDATE lines");

        const auto outEvent = parseEvent(output);
        QVERIFY2(outEvent, "output must parse as a valid VEVENT via KCalendarCore");
        QVERIFY2(!outEvent->recurs(), "output event must not recur");
    }

    // N1: when the event DOES have its own RRULE, exactly that line survives
    // byte-exact — none of the VTIMEZONE's transition rules leak in.
    void vtimezoneRecurrenceDoesNotContaminateEventWithOwnRRule()
    {
        ICalToCanonStage fwd;
        CanonToICalStage rev;

        const QByteArray canon = fwd.transform(kEventWithVtimezoneAndOwnRecurrence);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");

        const QJsonObject obj = parse(canon);
        const QJsonArray recArr = obj.value(QStringLiteral("recurrence")).toArray();
        QCOMPARE(recArr.size(), 1);
        QCOMPARE(recArr.first().toString(), QStringLiteral("RRULE:FREQ=WEEKLY"));

        const QByteArray output = rev.transform(canon);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty");

        // Scope to the VEVENT's own component (see eventComponentOf) — a
        // freshly regenerated VTIMEZONE legitimately carries its own RRULEs.
        const QByteArray eventBlock = eventComponentOf(output);
        QVERIFY2(!eventBlock.isEmpty(), "output must contain a VEVENT component");

        int rruleCount = 0;
        for (const QByteArray &raw : eventBlock.split('\n')) {
            QByteArray line = raw;
            if (line.endsWith('\r')) line.chop(1);
            if (line.startsWith("RRULE:"))
                ++rruleCount;
        }
        QCOMPARE(rruleCount, 1);
        QVERIFY2(eventBlock.contains("RRULE:FREQ=WEEKLY"),
                 "the event's own RRULE must survive byte-exact");
    }

    // N1: a folded RRULE (continuation line) must be unfolded before
    // matching, not mangled into two truncated recurrence entries.
    void foldedRruleRoundTripsIntact()
    {
        ICalToCanonStage fwd;
        CanonToICalStage rev;

        const QByteArray canon = fwd.transform(kEventWithFoldedRrule);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");

        const QJsonObject obj = parse(canon);
        const QJsonArray recArr = obj.value(QStringLiteral("recurrence")).toArray();
        QCOMPARE(recArr.size(), 1);
        QCOMPARE(recArr.first().toString(), QString::fromUtf8(kExpectedUnfoldedRrule));

        const QByteArray output = rev.transform(canon);
        QVERIFY2(output.contains(kExpectedUnfoldedRrule),
                 qPrintable(QStringLiteral("unfolded RRULE must survive intact; got: %1")
                     .arg(QString::fromUtf8(output))));
    }

    // O41 (E12) RED (b): a source with NO CREATED/LAST-MODIFIED at all (both
    // properties absent — plausible for content dropped in by some external
    // tool, RFC 5545 makes both optional) must round-trip through canon and
    // back to iCal WITHOUT KCalendarCore stamping wall-clock "now" defaults
    // into the outbound bytes. Before the E12 fix, canonObjectToEventBytes
    // left created()/lastModified() unset on the KCalendarCore::Event, but
    // KCalendarCore::ICalFormat::toICalString() stamped real values anyway —
    // a second forward-pass over those OUTPUT bytes would then find literal
    // CREATED/LAST-MODIFIED lines the original canon never had, permanently
    // disagreeing with a source that keeps omitting them (the live O41
    // phantom-conflict mechanism).
    void timestampLessSourceRoundTripsWithoutManufacturedStamps()
    {
        const QByteArray noTimestamps =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:no-timestamps-event@example.com\r\n"
            "DTSTAMP:20260601T120000Z\r\n"
            "DTSTART:20260601T090000Z\r\n"
            "DTEND:20260601T100000Z\r\n"
            "SUMMARY:No Created Or LastModified\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";

        ICalToCanonStage fwd;
        CanonToICalStage rev;

        const QByteArray canon1 = fwd.transform(noTimestamps);
        QVERIFY2(!canon1.isEmpty(), "forward stage returned empty");

        const QJsonObject obj1 = parse(canon1);
        QVERIFY2(!obj1.contains(QStringLiteral("created")),
                 "canon must not have a created key when the source lacks CREATED");
        QVERIFY2(!obj1.contains(QStringLiteral("lastModified")),
                 "canon must not have a lastModified key when the source lacks LAST-MODIFIED");

        const QByteArray icalOut = rev.transform(canon1);
        QVERIFY2(!icalOut.isEmpty(), "reverse stage returned empty");
        QVERIFY2(!icalOut.contains("CREATED:"),
                 qPrintable(QStringLiteral("write side must not manufacture CREATED; got: %1")
                     .arg(QString::fromUtf8(icalOut))));
        QVERIFY2(!icalOut.contains("LAST-MODIFIED:"),
                 qPrintable(QStringLiteral("write side must not manufacture LAST-MODIFIED; got: %1")
                     .arg(QString::fromUtf8(icalOut))));

        // Re-fetch: a second forward pass over the materialized bytes must
        // land on the SAME canon (no created/lastModified key either side) —
        // the round-trip pin that fails forever before this fix.
        const QByteArray canon2 = fwd.transform(icalOut);
        const QJsonObject obj2 = parse(canon2);
        QVERIFY2(!obj2.contains(QStringLiteral("created")),
                 "re-fetched canon must still lack created");
        QVERIFY2(!obj2.contains(QStringLiteral("lastModified")),
                 "re-fetched canon must still lack lastModified");
    }

    // -----------------------------------------------------------------
    // IP.4 — shared VALARM shape module (O79 + O85). Modeled on the W5
    // VTODO slots in tests/todo/tst_todo_canon_roundtrip.cpp — these are
    // VEVENT's first-ever coverage for the non-start-relative trigger
    // forms, since eventcanonfields.cpp never received W5's shape
    // extension until IP.4 pointed it at the shared alarmshape module.
    // -----------------------------------------------------------------

    // Absolute (VALUE=DATE-TIME) trigger. Before IP.4, eventcanonfields.cpp
    // read alarm->startOffset() unconditionally (O79), silently corrupting
    // this to a bogus "offset: 0" start-relative row.
    void veventAlarmAbsoluteFormRoundTrips()
    {
        const QByteArray vevent =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:vevent-alarm-at-1\r\n"
            "SUMMARY:Absolute alarm\r\n"
            "DTSTART:20260601T090000Z\r\n"
            "DTEND:20260601T100000Z\r\n"
            "BEGIN:VALARM\r\n"
            "ACTION:DISPLAY\r\n"
            "TRIGGER;VALUE=DATE-TIME:20260601T083000Z\r\n"
            "END:VALARM\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";

        ICalToCanonStage fwd;
        CanonToICalStage rev;

        const QByteArray canon = fwd.transform(vevent);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QJsonObject obj = parse(canon);
        const QJsonArray alarms = obj.value(QStringLiteral("alarms")).toArray();
        QCOMPARE(alarms.size(), 1);
        const QJsonObject a = alarms.at(0).toObject();
        QCOMPARE(a.value(QStringLiteral("at")).toString(), QStringLiteral("2026-06-01T08:30:00Z"));
        QVERIFY2(!a.contains(QStringLiteral("offset")),
                 "absolute-trigger alarm must not also carry a bogus offset (O79)");

        const QByteArray output = rev.transform(canon);
        const auto outEvent = parseEvent(output);
        QVERIFY(outEvent);
        const auto outAlarms = outEvent->alarms();
        QCOMPARE(outAlarms.size(), 1);
        QVERIFY(outAlarms.first()->hasTime());
        QCOMPARE(outAlarms.first()->time().toUTC(),
                 QDateTime::fromString(QStringLiteral("2026-06-01T08:30:00Z"), Qt::ISODate));
    }

    // END-related trigger. Before IP.4, this silently corrupted to a
    // start-relative offset:0 (O79) the same way the absolute case did.
    void veventAlarmEndRelatedFormRoundTrips()
    {
        const QByteArray vevent =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:vevent-alarm-end-1\r\n"
            "SUMMARY:End-related alarm\r\n"
            "DTSTART:20260601T090000Z\r\n"
            "DTEND:20260601T100000Z\r\n"
            "BEGIN:VALARM\r\n"
            "ACTION:DISPLAY\r\n"
            "TRIGGER;RELATED=END:-PT15M\r\n"
            "END:VALARM\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";

        ICalToCanonStage fwd;
        CanonToICalStage rev;

        const QByteArray canon = fwd.transform(vevent);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QJsonObject obj = parse(canon);
        const QJsonArray alarms = obj.value(QStringLiteral("alarms")).toArray();
        QCOMPARE(alarms.size(), 1);
        const QJsonObject a = alarms.at(0).toObject();
        QCOMPARE(a.value(QStringLiteral("offset")).toInt(), -900);
        QCOMPARE(a.value(QStringLiteral("related")).toString(), QStringLiteral("end"));

        const QByteArray output = rev.transform(canon);
        const auto outEvent = parseEvent(output);
        QVERIFY(outEvent);
        const auto outAlarms = outEvent->alarms();
        QCOMPARE(outAlarms.size(), 1);
        QVERIFY2(outAlarms.first()->hasEndOffset(),
                 "related:end must demote to an END-related offset, not START (O79)");
        QCOMPARE(outAlarms.first()->endOffset().asSeconds(), -900);
    }

    // REPEAT/DURATION pair. VEVENT's canon alarms row never carried
    // repeatCount/repeatIntervalSecs before IP.4 — it was still the
    // pre-W5 {type, offset, text} shape.
    void veventAlarmRepeatDurationPairRoundTrips()
    {
        const QByteArray vevent =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:vevent-alarm-repeat-1\r\n"
            "SUMMARY:Repeating alarm\r\n"
            "DTSTART:20260601T090000Z\r\n"
            "DTEND:20260601T100000Z\r\n"
            "BEGIN:VALARM\r\n"
            "ACTION:DISPLAY\r\n"
            "TRIGGER:-PT15M\r\n"
            "REPEAT:3\r\n"
            "DURATION:PT5M\r\n"
            "END:VALARM\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";

        ICalToCanonStage fwd;
        CanonToICalStage rev;

        const QByteArray canon = fwd.transform(vevent);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QJsonObject obj = parse(canon);
        const QJsonArray alarms = obj.value(QStringLiteral("alarms")).toArray();
        QCOMPARE(alarms.size(), 1);
        const QJsonObject a = alarms.at(0).toObject();
        QCOMPARE(a.value(QStringLiteral("repeatCount")).toInt(), 3);
        QCOMPARE(a.value(QStringLiteral("repeatIntervalSecs")).toInt(), 300);

        const QByteArray output = rev.transform(canon);
        const auto outEvent = parseEvent(output);
        QVERIFY(outEvent);
        const auto outAlarms = outEvent->alarms();
        QCOMPARE(outAlarms.size(), 1);
        QCOMPARE(outAlarms.first()->repeatCount(), 3);
        QCOMPARE(outAlarms.first()->snoozeTime().asSeconds(), 300);
    }

    // O85: an enabled source alarm must survive promote->demote still
    // enabled, on the VEVENT leg (see the twin VTODO slot in
    // tst_todo_canon_roundtrip.cpp — this proves the shared module honours
    // it on both incidence kinds, not just the one W5 originally fixed).
    void veventAlarmEnabledSurvivesRoundTrip()
    {
        const QByteArray vevent =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:vevent-alarm-enabled-1\r\n"
            "SUMMARY:Enabled alarm\r\n"
            "DTSTART:20260601T090000Z\r\n"
            "DTEND:20260601T100000Z\r\n"
            "BEGIN:VALARM\r\n"
            "ACTION:DISPLAY\r\n"
            "TRIGGER:-PT15M\r\n"
            "END:VALARM\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";

        ICalToCanonStage fwd;
        CanonToICalStage rev;

        const auto srcEvent = parseEvent(vevent);
        QVERIFY(srcEvent);
        QVERIFY2(srcEvent->alarms().first()->enabled(),
                 "fixture alarm must be enabled=true in the source");

        const QByteArray canon = fwd.transform(vevent);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QByteArray output = rev.transform(canon);
        const auto outEvent = parseEvent(output);
        QVERIFY(outEvent);
        const auto outAlarms = outEvent->alarms();
        QCOMPARE(outAlarms.size(), 1);
        QVERIFY2(outAlarms.first()->enabled(),
                 "alarm must still be enabled after round trip (O85)");
    }

    // IP.5/O80 — VEVENT's x-ical providerExtras stash now gets a digest so
    // an extras-only edit dirties the differ (calendar's catalogue-scoped
    // CanonJsonDiffer never sees providerExtras itself).
    void veventPromoteStampsProviderExtrasDigest()
    {
        static const QByteArray kEventWithXProp =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:test-event-uid-xprop\r\n"
            "SUMMARY:Team Meeting\r\n"
            "DTSTART:20260601T090000Z\r\n"
            "DTEND:20260601T100000Z\r\n"
            "X-CANON-TEST-PROP:hello\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";

        ICalToCanonStage stage;
        const QJsonObject obj = parse(stage.transform(kEventWithXProp));
        QVERIFY2(!obj.isEmpty(), "promote must not be empty");

        using Kalburator::Shape::CanonEnvelope::providerExtrasKey;
        const QJsonObject xical = obj.value(providerExtrasKey()).toObject()
                                      .value(QStringLiteral("x-ical")).toObject();
        QVERIFY2(xical.contains(QStringLiteral("X-CANON-TEST-PROP")),
                 "fixture must exercise the x-ical passthrough");
        QVERIFY2(!obj.value(QStringLiteral("providerExtrasDigest")).toString().isEmpty(),
                 "VEVENT promote must stamp providerExtrasDigest when extras are present");
    }

    // IP.5/O80 — VJOURNAL's providerExtras["x-ical"] stash
    // (journalcanonfields.cpp) gets the SAME digest treatment as VEVENT —
    // same mechanism, same volatile-key list (empty: genuine X-props only,
    // no vendor bookkeeping on this CalDAV leg).
    void journalPromoteStampsProviderExtrasDigest()
    {
        static const QByteArray kJournalWithXProp =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VJOURNAL\r\n"
            "UID:test-journal-uid-xprop\r\n"
            "DTSTAMP:20260901T120000Z\r\n"
            "SUMMARY:Journal Entry\r\n"
            "X-CANON-TEST-PROP:hello\r\n"
            "END:VJOURNAL\r\n"
            "END:VCALENDAR\r\n";

        ICalToCanonStage stage;
        const QJsonObject obj = parse(stage.transform(kJournalWithXProp));
        QVERIFY2(!obj.isEmpty(), "promote must not be empty");

        using Kalburator::Shape::CanonEnvelope::providerExtrasKey;
        const QJsonObject xical = obj.value(providerExtrasKey()).toObject()
                                      .value(QStringLiteral("x-ical")).toObject();
        QVERIFY2(xical.contains(QStringLiteral("X-CANON-TEST-PROP")),
                 "fixture must exercise the x-ical passthrough");
        QVERIFY2(!obj.value(QStringLiteral("providerExtrasDigest")).toString().isEmpty(),
                 "VJOURNAL promote must stamp providerExtrasDigest when extras are present");
    }

    // -----------------------------------------------------------------
    // IP.7a — RANGE=THISANDFUTURE non-re-emission (O82, the VEVENT twin of
    // VTODO's W3 safety rule / VP.e's vtodoDemoteNeverEmitsThisAndFutureRange,
    // VJOURNAL's own twin at IP.10 —
    // tst_calendar_kind_dispatch.cpp::vjournalDemoteNeverEmitsThisAndFutureRange).
    // -----------------------------------------------------------------
    void veventDemoteNeverEmitsThisAndFutureRange()
    {
        const QByteArray ical =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:range-uid\r\n"
            "RECURRENCE-ID;RANGE=THISANDFUTURE:20260602T090000Z\r\n"
            "SUMMARY:Moved and all future\r\n"
            "DTSTART:20260608T090000Z\r\n"
            "DTEND:20260608T100000Z\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";

        ICalToCanonStage fwd;
        CanonToICalStage rev;

        const QByteArray canon = fwd.transform(ical);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QJsonObject obj = parse(canon);

        // Promote losslessly captures the incoming RANGE=THISANDFUTURE.
        QCOMPARE(obj.value(QStringLiteral("recurrenceRange")).toString(),
                 QStringLiteral("thisAndFuture"));

        const QByteArray output = rev.transform(canon);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty");
        QVERIFY2(!output.contains("RANGE=THISANDFUTURE"),
                 qPrintable(QStringLiteral(
                     "demoted bytes must NEVER carry RANGE=THISANDFUTURE "
                     "(O82, write-hostile on real servers):\n")
                     + QString::fromUtf8(output)));

        const auto outEvent = parseEvent(output);
        QVERIFY(outEvent);
        QVERIFY2(!outEvent->thisAndFuture(),
                 "thisAndFuture() must be false on the demoted event");
        // Bare exception identity is unaffected.
        QVERIFY2(output.contains("RECURRENCE-ID"),
                 "the exception identity itself must still round-trip");
    }

    // -----------------------------------------------------------------
    // IP.7b — malformed DTSTART/DTEND coercion (O81). DTSTART-wins per
    // Amendment 2 §B.2 (ratified by PlanStan 2026-09-02): "the mandatory
    // temporal anchor wins; the optional derived bound is coerced to match
    // it" — opposite polarity from VTODO's W6.2 DUE-wins rule, same
    // underlying principle. Mirrors
    // tests/todo/tst_todo_canon_roundtrip.cpp's W6.2 slots
    // (vtodoCoercesDateOnlyStartToDueDateTimeType /
    // vtodoCoercesDateTimeStartToDueDateOnlyType /
    // vtodoDropsStartWhenDueNotAfterDtstart /
    // vtodoPromoteDropsDurationWithoutDtstart).
    // -----------------------------------------------------------------

    // Rule 1, bullet 1: DTSTART DATE + DTEND DATE-TIME ⇒ DTEND coerced DOWN
    // to DATE-only, taking DTEND's own date part (NOT clamped to DTSTART's
    // date). Canon's stored "end" date is one day EARLY relative to the
    // true wire date (getter/canon-space — see the eventcanonfields.cpp
    // comment and the IP.7 return receipt §3): demote's automatic +1-day
    // all-day re-serialization is what reconstructs the true date, so the
    // round-trip assertion below (not just the intermediate canon value)
    // is the one that actually pins the contract.
    void veventCoercesDateTimeEndToDateOnlyType()
    {
        const QByteArray ical =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:coerce-vevent-a-1\r\n"
            "SUMMARY:Mismatched a\r\n"
            "DTSTART;VALUE=DATE:20260601\r\n"
            "DTEND:20260602T170000Z\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";

        ICalToCanonStage fwd;
        CanonToICalStage rev;
        const QByteArray canon = fwd.transform(ical);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QJsonObject obj = parse(canon);

        const QJsonObject startObj = obj.value(QStringLiteral("start")).toObject();
        QVERIFY(startObj.contains(QStringLiteral("date")));
        QCOMPARE(startObj.value(QStringLiteral("date")).toString(),
                 QStringLiteral("2026-06-01"));
        QCOMPARE(obj.value(QStringLiteral("allDay")).toBool(), true);

        const QJsonObject endObj = obj.value(QStringLiteral("end")).toObject();
        QVERIFY2(endObj.contains(QStringLiteral("date")),
                 "end must be coerced away from DATE-TIME to match DTSTART's DATE-only type");
        // Getter/canon-space value (true wire date 2026-06-02, minus one
        // day to compensate for demote's automatic all-day +1).
        QCOMPARE(endObj.value(QStringLiteral("date")).toString(),
                 QStringLiteral("2026-06-01"));

        const QByteArray output = rev.transform(canon);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty");
        QVERIFY2(output.contains("DTEND;VALUE=DATE:20260602"),
                 qPrintable(QStringLiteral(
                     "coerced DTEND must demote to DTEND's own true date part "
                     "(2026-06-02), not DTSTART's date:\n")
                     + QString::fromUtf8(output)));
    }

    // Rule 1, bullet 2: DTSTART DATE-TIME + DTEND DATE ⇒ DTEND coerced UP to
    // DATE-TIME at 00:00 IN DTSTART's timezone (house rule O60). The raw
    // dtEnd() KCalendarCore hands promote here is ALREADY one day short of
    // the literal wire date (the same all-day getter quirk as above) — the
    // implementation adds the day back before constructing the 00:00
    // moment, so 2026-06-02 (the literal wire DTEND date) is what should
    // come out, not 2026-06-01.
    void veventCoercesDateOnlyEndToDateTimeType()
    {
        const QByteArray ical =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:coerce-vevent-a-2\r\n"
            "SUMMARY:Mismatched a reverse\r\n"
            "DTSTART:20260601T090000Z\r\n"
            "DTEND;VALUE=DATE:20260602\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";

        ICalToCanonStage fwd;
        CanonToICalStage rev;
        const QByteArray canon = fwd.transform(ical);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QJsonObject obj = parse(canon);

        const QJsonObject startObj = obj.value(QStringLiteral("start")).toObject();
        QVERIFY2(!startObj.contains(QStringLiteral("date")),
                 "start must stay DATE-TIME — DTSTART is the anchor, never coerced");
        QCOMPARE(startObj.value(QStringLiteral("dateTime")).toString(),
                 QStringLiteral("2026-06-01T09:00:00Z"));
        QCOMPARE(obj.value(QStringLiteral("allDay")).toBool(), false);

        const QJsonObject endObj = obj.value(QStringLiteral("end")).toObject();
        QVERIFY2(!endObj.contains(QStringLiteral("date")),
                 "end must be coerced UP to DATE-TIME to match DTSTART's type");
        QCOMPARE(endObj.value(QStringLiteral("dateTime")).toString(),
                 QStringLiteral("2026-06-02T00:00:00Z"));

        const QByteArray output = rev.transform(canon);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty");
        QVERIFY2(output.contains("DTEND:20260602T000000Z"),
                 qPrintable(QStringLiteral("expected DTEND:20260602T000000Z in output:\n")
                     + QString::fromUtf8(output)));
    }

    // Rule 2: a coerced (or already-matching-type) DTEND <= DTSTART ⇒ DTEND
    // is dropped from canon entirely — RFC 5545 §3.6.1's default stands,
    // rather than clamping to an invented bound. DATE-TIME pair (no
    // inclusive/exclusive all-day concept involved).
    void veventDropsEndWhenCoercedEndNotAfterDtstart()
    {
        const QByteArray ical =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:coerce-vevent-b-1\r\n"
            "SUMMARY:Backwards dates\r\n"
            "DTSTART:20260605T090000Z\r\n"
            "DTEND:20260601T090000Z\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";

        ICalToCanonStage fwd;
        const QByteArray canon = fwd.transform(ical);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QJsonObject obj = parse(canon);

        QVERIFY(obj.contains(QStringLiteral("start")));
        QVERIFY2(!obj.contains(QStringLiteral("end")),
                 "DTEND <= DTSTART must drop end from canon entirely");
    }

    // Rule 2, coerced-to-date-only variant: bullet 1's coercion (DTSTART
    // DATE + DTEND DATE-TIME) can itself produce a degenerate date-only
    // pair when DTEND's date part falls on or before DTSTART's date —
    // pins that the inclusive/exclusive getter-space comparison (see the
    // eventcanonfields.cpp comment) correctly identifies THIS case as
    // degenerate, in contrast to veventAllDayRoundTripPreservesDateValueForm
    // below (a native, uncoerced one-day event, where end.date()==
    // start.date() in getter space and must NOT be dropped).
    //
    // A genuinely backwards NATIVE all-day pair (both sides already
    // DATE-only on the wire, DTEND's date <= DTSTART's) is not
    // separately pinned here: probed directly (IP.7 return receipt §3)
    // and found that KCalendarCore::Event::dtEnd() itself silently clamps
    // such a malformed pair so the getter reports end.date()==start.date()
    // — i.e. the library never even hands promote a value from which a
    // "before start" date-only end could be constructed natively. The
    // coercion path below is the only route through which this promote
    // code can ever see one.
    void veventDropsCoercedDateOnlyEndWhenWireDateNotAfterDtstart()
    {
        const QByteArray ical =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:coerce-vevent-b-2\r\n"
            "SUMMARY:Backwards after coercion\r\n"
            "DTSTART;VALUE=DATE:20260605\r\n"
            "DTEND:20260601T170000Z\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";

        ICalToCanonStage fwd;
        const QByteArray canon = fwd.transform(ical);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QJsonObject obj = parse(canon);

        QVERIFY(obj.contains(QStringLiteral("start")));
        QVERIFY2(!obj.contains(QStringLiteral("end")),
                 "DTEND's date part on/before DTSTART's date must drop end "
                 "from canon entirely, even after bullet-1's coercion");
    }

    // Rule 3: DURATION present instead of DTEND ⇒ nothing to coerce, leave
    // it. KCalendarCore::Event resolves DURATION into a computed dtEnd()
    // that is already type-consistent with dtStart() (probe-confirmed,
    // IP.7 return receipt) — this pins that already-correct behaviour, the
    // zero-code-no-op shape mirroring VTODO's rule (c) pin.
    void veventPromoteLeavesDurationDerivedEndAlone()
    {
        const QByteArray ical =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:coerce-vevent-c-1\r\n"
            "SUMMARY:Duration only\r\n"
            "DTSTART:20260601T090000Z\r\n"
            "DURATION:PT1H\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";

        ICalToCanonStage fwd;
        const QByteArray canon = fwd.transform(ical);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QJsonObject obj = parse(canon);

        const QJsonObject startObj = obj.value(QStringLiteral("start")).toObject();
        QCOMPARE(startObj.value(QStringLiteral("dateTime")).toString(),
                 QStringLiteral("2026-06-01T09:00:00Z"));
        const QJsonObject endObj = obj.value(QStringLiteral("end")).toObject();
        QVERIFY2(!endObj.isEmpty(),
                 "DURATION-derived dtEnd() is valid and already type-consistent — no drop");
        QCOMPARE(endObj.value(QStringLiteral("dateTime")).toString(),
                 QStringLiteral("2026-06-01T10:00:00Z"));
    }

    // Bonus regression pin (mirrors VTODO's vtodoAllDayRoundTripPreservesDateValueForm):
    // a well-formed all-day DTSTART/DTEND (both DATE, no mismatch to coerce)
    // round-trips through BOTH promote AND demote as a real VALUE=DATE, not
    // a UTC-midnight DATE-TIME — confirming IP.7b's per-property isDateOnly
    // detection didn't regress the already-correct well-formed case.
    void veventAllDayRoundTripPreservesDateValueForm()
    {
        const QByteArray ical =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:allday-vevent-roundtrip-1\r\n"
            "SUMMARY:All day event\r\n"
            "DTSTART;VALUE=DATE:20260601\r\n"
            "DTEND;VALUE=DATE:20260602\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";

        ICalToCanonStage fwd;
        CanonToICalStage rev;

        const QByteArray canon = fwd.transform(ical);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QJsonObject obj = parse(canon);
        QVERIFY(obj.value(QStringLiteral("start")).toObject().contains(QStringLiteral("date")));
        QVERIFY(obj.value(QStringLiteral("end")).toObject().contains(QStringLiteral("date")));

        const QByteArray output = rev.transform(canon);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty");
        QVERIFY2(output.contains("DTSTART;VALUE=DATE:20260601"),
                 qPrintable(QStringLiteral("expected DTSTART;VALUE=DATE:20260601 in output:\n")
                     + QString::fromUtf8(output)));
        QVERIFY2(output.contains("DTEND;VALUE=DATE:20260602"),
                 qPrintable(QStringLiteral("expected DTEND;VALUE=DATE:20260602 in output:\n")
                     + QString::fromUtf8(output)));
    }
};

QTEST_GUILESS_MAIN(TestCalendarCanonRoundtrip)
#include "tst_calendar_canon_roundtrip.moc"
