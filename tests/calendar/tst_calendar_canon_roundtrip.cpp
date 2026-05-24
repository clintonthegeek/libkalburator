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

// A representative VEVENT with core fields, RRULE, and ATTENDEE.
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
    "ORGANIZER;CN=Alice:mailto:alice@example.com\r\n"
    "ATTENDEE;CN=Bob;PARTSTAT=ACCEPTED;RSVP=TRUE:mailto:bob@example.com\r\n"
    "END:VEVENT\r\n"
    "END:VCALENDAR\r\n";

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

        // RRULE must survive in the output iCal bytes
        QVERIFY2(output.contains("FREQ=WEEKLY"),
                 "RRULE must survive ical->canon->ical round-trip");
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
};

QTEST_GUILESS_MAIN(TestCalendarCanonRoundtrip)
#include "tst_calendar_canon_roundtrip.moc"
