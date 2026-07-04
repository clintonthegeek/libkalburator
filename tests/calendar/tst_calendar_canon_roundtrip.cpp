#include <QTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

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

        // Must stash the verbatim original (invariant 4: recoverable)
        QVERIFY2(output.contains("X-CANON-CLASSIFICATION:personal"),
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
};

QTEST_GUILESS_MAIN(TestCalendarCanonRoundtrip)
#include "tst_calendar_canon_roundtrip.moc"
