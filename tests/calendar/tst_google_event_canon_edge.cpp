#include <QTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include "canonenvelope.h"
#include "googlecanonstages.h"
#include "calendardomaindefinition.h"
#include "calendarstockshapes.h"
#include "shaperegistries.h"
#include "lossprofile.h"

using Kalburator::Shape::CanonEnvelope::parse;
using Kalburator::Shape::CanonEnvelope::serialize;
using Kalburator::Shape::CanonEnvelope::stampEnvelope;
using Kalburator::Calendar::GoogleEventToCanonStage;
using Kalburator::Calendar::CanonToGoogleEventStage;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::Shape;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::LossKind;

namespace {

// Build a ShapeRegistries with the calendar domain fully registered
// (including the google-event peer shape and its two EEE Phase 2 edges).
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

// A captured-shaped Google Calendar v3 event (modeled on the wire format —
// reference doc §1.1 plus live Google exports). NOTE: plain concatenated
// string literals, NOT a raw string literal — moc silently produces no
// output for Q_OBJECT classes in translation units containing a terminated
// R"(...)" literal (FINDINGS O59 tooling note).
const QByteArray kGoogleEvent =
    "{"
    "\"kind\": \"calendar#event\","
    "\"etag\": \"\\\"3396688454742000\\\"\","
    "\"id\": \"0n4s31vko6o3a9n6sj6kj9d24p\","
    "\"status\": \"confirmed\","
    "\"htmlLink\": \"https://www.google.com/calendar/event?eid=MG40czMxdmtvNm8zYTluNnNqNmtqOWQyNHA\","
    "\"created\": \"2026-05-30T14:32:11.000Z\","
    "\"updated\": \"2026-06-01T09:15:00.000Z\","
    "\"summary\": \"Team Meeting\","
    "\"description\": \"Weekly sync\","
    "\"location\": \"Room 4\","
    "\"colorId\": \"5\","
    "\"creator\": {\"email\": \"alice@example.com\", \"self\": true},"
    "\"organizer\": {\"email\": \"alice@example.com\", \"self\": true},"
    "\"start\": {\"dateTime\": \"2026-06-01T09:00:00-04:00\", \"timeZone\": \"America/New_York\"},"
    "\"end\": {\"dateTime\": \"2026-06-01T10:00:00-04:00\", \"timeZone\": \"America/New_York\"},"
    "\"recurrence\": [\"RRULE:FREQ=WEEKLY;BYDAY=MO,WE\"],"
    "\"iCalUID\": \"team-meeting-uid@google.com\","
    "\"sequence\": 2,"
    "\"visibility\": \"private\","
    "\"transparency\": \"opaque\","
    "\"eventType\": \"default\","
    "\"guestsCanModify\": true,"
    "\"attendees\": ["
    "  {\"email\": \"bob@example.com\", \"displayName\": \"Bob\", \"responseStatus\": \"accepted\", \"optional\": false},"
    "  {\"email\": \"carol@example.com\", \"responseStatus\": \"tentative\", \"optional\": true}"
    "],"
    "\"reminders\": {\"useDefault\": false, \"overrides\": ["
    "  {\"method\": \"popup\", \"minutes\": 15},"
    "  {\"method\": \"email\", \"minutes\": 60}"
    "]},"
    "\"conferenceData\": {\"entryPoints\": ["
    "  {\"entryPointType\": \"video\", \"uri\": \"https://meet.google.com/abc-defg-hij\"}"
    "]},"
    "\"extendedProperties\": {\"private\": {\"x-canon-priority\": \"3\"}},"
    "\"source\": {\"url\": \"https://example.com/meeting-notes\", \"title\": \"Notes\"}"
    "}";

/// Minimal canon envelope with the given properties.
QJsonObject makeCanonObject()
{
    QJsonObject obj;
    obj.insert(QStringLiteral("uid"), QStringLiteral("roundtrip-uid@google.com"));
    QJsonObject start;
    start.insert(QStringLiteral("dateTime"), QStringLiteral("2026-06-01T13:00:00Z"));
    start.insert(QStringLiteral("tz"), QStringLiteral("America/New_York"));
    start.insert(QStringLiteral("floating"), false);
    obj.insert(QStringLiteral("start"), start);
    stampEnvelope(obj, QStringLiteral("calendar"), obj.value(QStringLiteral("uid")).toString());
    return obj;
}

} // namespace

class TestGoogleEventCanonEdge : public QObject {
    Q_OBJECT
private slots:

    // Slot 1 — promote: every mapped field lands in canon; every unmapped
    // Google field lands verbatim under providerExtras["google"].
    void promoteCapturedShapedPayloadIsLossless()
    {
        GoogleEventToCanonStage stage;
        const QByteArray canonBytes = stage.transform(kGoogleEvent);
        QVERIFY2(!canonBytes.isEmpty(), "promote returned empty bytes");

        const QJsonObject canon = parse(canonBytes);
        QCOMPARE(canon.value(QStringLiteral("uid")).toString(),
                 QStringLiteral("team-meeting-uid@google.com"));
        QCOMPARE(canon.value(QStringLiteral("sequence")).toInt(), 2);
        QCOMPARE(canon.value(QStringLiteral("created")).toString(),
                 QStringLiteral("2026-05-30T14:32:11Z"));
        QCOMPARE(canon.value(QStringLiteral("lastModified")).toString(),
                 QStringLiteral("2026-06-01T09:15:00Z"));
        QCOMPARE(canon.value(QStringLiteral("summary")).toString(), QStringLiteral("Team Meeting"));
        QCOMPARE(canon.value(QStringLiteral("description")).toString(), QStringLiteral("Weekly sync"));
        QCOMPARE(canon.value(QStringLiteral("location")).toString(), QStringLiteral("Room 4"));
        QCOMPARE(canon.value(QStringLiteral("status")).toString(), QStringLiteral("confirmed"));
        QCOMPARE(canon.value(QStringLiteral("classification")).toString(), QStringLiteral("private"));
        QCOMPARE(canon.value(QStringLiteral("timeTransparency")).toString(), QStringLiteral("opaque"));
        QCOMPARE(canon.value(QStringLiteral("eventType")).toString(), QStringLiteral("default"));
        QCOMPARE(canon.value(QStringLiteral("guestsCanModify")).toBool(), true);
        QCOMPARE(canon.value(QStringLiteral("color")).toString(), QStringLiteral("5"));
        QCOMPARE(canon.value(QStringLiteral("url")).toString(),
                 QStringLiteral("https://example.com/meeting-notes"));

        // start/end: tz-aware Google form → canon UTC ISO + tz + floating:false
        const QJsonObject start = canon.value(QStringLiteral("start")).toObject();
        QCOMPARE(start.value(QStringLiteral("dateTime")).toString(),
                 QStringLiteral("2026-06-01T13:00:00Z"));
        QCOMPARE(start.value(QStringLiteral("tz")).toString(), QStringLiteral("America/New_York"));
        QCOMPARE(start.value(QStringLiteral("floating")).toBool(), false);
        QCOMPARE(canon.value(QStringLiteral("end")).toObject()
                     .value(QStringLiteral("dateTime")).toString(),
                 QStringLiteral("2026-06-01T14:00:00Z"));
        QCOMPARE(canon.value(QStringLiteral("allDay")).toBool(), false);

        // recurrence: verbatim RFC5545 lines
        const QJsonArray recurrence = canon.value(QStringLiteral("recurrence")).toArray();
        QCOMPARE(recurrence.size(), 1);
        QCOMPARE(recurrence.at(0).toString(), QStringLiteral("RRULE:FREQ=WEEKLY;BYDAY=MO,WE"));

        // attendees: canon encoding matches eventcanonfields.cpp keys
        const QJsonArray attendees = canon.value(QStringLiteral("attendees")).toArray();
        QCOMPARE(attendees.size(), 2);
        {
            const QJsonObject bob = attendees.at(0).toObject();
            QCOMPARE(bob.value(QStringLiteral("email")).toString(), QStringLiteral("bob@example.com"));
            QCOMPARE(bob.value(QStringLiteral("name")).toString(), QStringLiteral("Bob"));
            QCOMPARE(bob.value(QStringLiteral("role")).toString(), QStringLiteral("required"));
            QCOMPARE(bob.value(QStringLiteral("partstat")).toString(), QStringLiteral("accepted"));
            QCOMPARE(bob.value(QStringLiteral("rsvp")).toBool(), false);
        }
        {
            const QJsonObject carol = attendees.at(1).toObject();
            QCOMPARE(carol.value(QStringLiteral("role")).toString(), QStringLiteral("optional"));
            QCOMPARE(carol.value(QStringLiteral("partstat")).toString(), QStringLiteral("tentative"));
        }

        // reminders → alarms (popup→Display=1, email→Email=3, minutes→negative secs)
        const QJsonArray alarms = canon.value(QStringLiteral("alarms")).toArray();
        QCOMPARE(alarms.size(), 2);
        QCOMPARE(alarms.at(0).toObject().value(QStringLiteral("type")).toInt(), 1);
        QCOMPARE(alarms.at(0).toObject().value(QStringLiteral("offset")).toInt(), -900);
        QCOMPARE(alarms.at(1).toObject().value(QStringLiteral("type")).toInt(), 3);
        QCOMPARE(alarms.at(1).toObject().value(QStringLiteral("offset")).toInt(), -3600);

        // conferenceData → onlineMeeting
        QCOMPARE(canon.value(QStringLiteral("onlineMeeting")).toObject()
                     .value(QStringLiteral("joinUrl")).toString(),
                 QStringLiteral("https://meet.google.com/abc-defg-hij"));

        // x-canon-priority carrier re-promoted
        QCOMPARE(canon.value(QStringLiteral("priority")).toInt(), 3);

        // unmapped fields → providerExtras["google"]
        const QJsonObject extrasGoogle = canon.value(QStringLiteral("providerExtras"))
                                             .toObject().value(QStringLiteral("google")).toObject();
        QCOMPARE(extrasGoogle.value(QStringLiteral("id")).toString(),
                 QStringLiteral("0n4s31vko6o3a9n6sj6kj9d24p"));
        QCOMPARE(extrasGoogle.value(QStringLiteral("etag")).toString(),
                 QStringLiteral("\"3396688454742000\""));
        QVERIFY(!extrasGoogle.value(QStringLiteral("htmlLink")).toString().isEmpty());
        QCOMPARE(extrasGoogle.value(QStringLiteral("kind")).toString(),
                 QStringLiteral("calendar#event"));
        QCOMPARE(extrasGoogle.value(QStringLiteral("creator")).toObject()
                     .value(QStringLiteral("email")).toString(),
                 QStringLiteral("alice@example.com"));
        // organizer leftovers ({self:true}) and source leftovers ({title})
        QCOMPARE(extrasGoogle.value(QStringLiteral("organizer")).toObject()
                     .value(QStringLiteral("self")).toBool(), true);
        QCOMPARE(extrasGoogle.value(QStringLiteral("source")).toObject()
                     .value(QStringLiteral("title")).toString(), QStringLiteral("Notes"));
        // verbatim stashes for reminders/conferenceData
        QVERIFY(!extrasGoogle.value(QStringLiteral("reminders")).toObject().isEmpty());
        QVERIFY(!extrasGoogle.value(QStringLiteral("conferenceData")).toObject().isEmpty());
    }

    // Slot 2 — each Degraded/Simplified/Dropped declaration matches the
    // actual demote output.
    void demoteDeclaredLossMatchesReality()
    {
        CanonToGoogleEventStage stage;
        const Shape googleEvent{ DomainId{QStringLiteral("calendar")},
                                 EncodingId{QStringLiteral("google-event")} };
        Q_UNUSED(googleEvent);

        // classification Degraded: MS "personal" → visibility private + carrier
        {
            QJsonObject canon = makeCanonObject();
            canon.insert(QStringLiteral("classification"), QStringLiteral("personal"));
            const QJsonObject out = parse(stage.transform(serialize(canon)));
            QCOMPARE(out.value(QStringLiteral("visibility")).toString(),
                     QStringLiteral("private"));
            QCOMPARE(out.value(QStringLiteral("extendedProperties")).toObject()
                         .value(QStringLiteral("private")).toObject()
                         .value(QStringLiteral("x-canon-classification")).toString(),
                     QStringLiteral("personal"));
        }

        // locations Simplified: multi → primary location string + carrier
        {
            QJsonObject canon = makeCanonObject();
            QJsonArray locs;
            QJsonObject l1; l1.insert(QStringLiteral("displayName"), QStringLiteral("Room A"));
            QJsonObject l2; l2.insert(QStringLiteral("displayName"), QStringLiteral("Room B"));
            locs.append(l1); locs.append(l2);
            canon.insert(QStringLiteral("locations"), locs);
            const QJsonObject out = parse(stage.transform(serialize(canon)));
            QCOMPARE(out.value(QStringLiteral("location")).toString(), QStringLiteral("Room A"));
            QVERIFY(out.value(QStringLiteral("extendedProperties")).toObject()
                        .value(QStringLiteral("private")).toObject()
                        .contains(QStringLiteral("x-canon-locations")));
        }

        // priority Reversible: carrier only, no Google field
        {
            QJsonObject canon = makeCanonObject();
            canon.insert(QStringLiteral("priority"), 3);
            const QJsonObject out = parse(stage.transform(serialize(canon)));
            QVERIFY(!out.contains(QStringLiteral("priority")));
            QCOMPARE(out.value(QStringLiteral("extendedProperties")).toObject()
                         .value(QStringLiteral("private")).toObject()
                         .value(QStringLiteral("x-canon-priority")).toString(),
                     QStringLiteral("3"));
        }

        // categories Simplified → carrier (no Google CATEGORIES field)
        {
            QJsonObject canon = makeCanonObject();
            canon.insert(QStringLiteral("categories"),
                         QJsonArray{ QStringLiteral("Work"), QStringLiteral("Focus") });
            const QJsonObject out = parse(stage.transform(serialize(canon)));
            QVERIFY(!out.contains(QStringLiteral("categories")));
            QVERIFY(out.value(QStringLiteral("extendedProperties")).toObject()
                        .value(QStringLiteral("private")).toObject()
                        .value(QStringLiteral("x-canon-categories")).toString()
                        .contains(QStringLiteral("Work")));
        }

        // geo Dropped: nowhere in the output — not even a carrier
        {
            QJsonObject canon = makeCanonObject();
            QJsonObject geo; geo.insert(QStringLiteral("lat"), 52.0);
            canon.insert(QStringLiteral("geo"), geo);
            const QJsonObject out = parse(stage.transform(serialize(canon)));
            QVERIFY(!out.contains(QStringLiteral("geo")));
            QVERIFY(!out.value(QStringLiteral("extendedProperties")).toObject()
                        .value(QStringLiteral("private")).toObject()
                        .contains(QStringLiteral("x-canon-geo")));
        }

        // color Degraded: non-palette string → no colorId, carried
        {
            QJsonObject canon = makeCanonObject();
            canon.insert(QStringLiteral("color"), QStringLiteral("#ff0000"));
            const QJsonObject out = parse(stage.transform(serialize(canon)));
            QVERIFY(!out.contains(QStringLiteral("colorId")));
            QCOMPARE(out.value(QStringLiteral("extendedProperties")).toObject()
                         .value(QStringLiteral("private")).toObject()
                         .value(QStringLiteral("x-canon-color")).toString(),
                     QStringLiteral("#ff0000"));
        }

        // eventType Degraded: MS vocab → "default" + carried original
        {
            QJsonObject canon = makeCanonObject();
            canon.insert(QStringLiteral("eventType"), QStringLiteral("singleInstance"));
            const QJsonObject out = parse(stage.transform(serialize(canon)));
            QCOMPARE(out.value(QStringLiteral("eventType")).toString(),
                     QStringLiteral("default"));
            QCOMPARE(out.value(QStringLiteral("extendedProperties")).toObject()
                         .value(QStringLiteral("private")).toObject()
                         .value(QStringLiteral("x-canon-event-type")).toString(),
                     QStringLiteral("singleInstance"));
        }
    }

    // Slot 3 — canon → google → canon byte-equal on the lossless +
    // Reversible-carrier property set.
    void losslessRoundTripIsIdentity()
    {
        QJsonObject canon = makeCanonObject();
        canon.insert(QStringLiteral("sequence"), 2);
        canon.insert(QStringLiteral("created"), QStringLiteral("2026-05-30T14:32:11Z"));
        canon.insert(QStringLiteral("lastModified"), QStringLiteral("2026-06-01T09:15:00Z"));
        canon.insert(QStringLiteral("summary"), QStringLiteral("Sync"));
        canon.insert(QStringLiteral("description"), QStringLiteral("Body"));
        canon.insert(QStringLiteral("descriptionHtml"), QStringLiteral("<b>Body</b>"));
        canon.insert(QStringLiteral("location"), QStringLiteral("Room 4"));
        canon.insert(QStringLiteral("status"), QStringLiteral("confirmed"));
        canon.insert(QStringLiteral("classification"), QStringLiteral("confidential"));
        canon.insert(QStringLiteral("timeTransparency"), QStringLiteral("transparent"));
        canon.insert(QStringLiteral("color"), QStringLiteral("7"));
        {
            QJsonObject end;
            end.insert(QStringLiteral("dateTime"), QStringLiteral("2026-06-01T14:00:00Z"));
            end.insert(QStringLiteral("tz"), QStringLiteral("America/New_York"));
            end.insert(QStringLiteral("floating"), false);
            canon.insert(QStringLiteral("end"), end);
        }
        canon.insert(QStringLiteral("recurrence"),
                     QJsonArray{ QStringLiteral("RRULE:FREQ=DAILY;COUNT=10") });
        {
            QJsonObject org;
            org.insert(QStringLiteral("email"), QStringLiteral("alice@example.com"));
            org.insert(QStringLiteral("name"), QStringLiteral("Alice"));
            canon.insert(QStringLiteral("organizer"), org);
        }
        {
            QJsonArray atts;
            QJsonObject a;
            a.insert(QStringLiteral("email"), QStringLiteral("bob@example.com"));
            a.insert(QStringLiteral("name"), QStringLiteral("Bob"));
            a.insert(QStringLiteral("role"), QStringLiteral("required"));
            a.insert(QStringLiteral("partstat"), QStringLiteral("accepted"));
            a.insert(QStringLiteral("rsvp"), false);
            atts.append(a);
            canon.insert(QStringLiteral("attendees"), atts);
        }
        canon.insert(QStringLiteral("responseRequested"), true);
        canon.insert(QStringLiteral("priority"), 3);
        canon.insert(QStringLiteral("url"), QStringLiteral("https://example.com/notes"));
        canon.insert(QStringLiteral("eventType"), QStringLiteral("default"));
        canon.insert(QStringLiteral("guestsCanModify"), true);
        {
            QJsonObject typed;
            typed.insert(QStringLiteral("x-app-key"), QStringLiteral("app-value"));
            canon.insert(QStringLiteral("typedProperties"), typed);
        }

        CanonToGoogleEventStage demote;
        GoogleEventToCanonStage promote;
        const QByteArray googleBytes = demote.transform(serialize(canon));
        QVERIFY2(!googleBytes.isEmpty(), "demote returned empty bytes");
        const QByteArray roundTripped = promote.transform(googleBytes);
        QVERIFY2(!roundTripped.isEmpty(), "re-promote returned empty bytes");

        // QJsonDocument sorts object keys, so byte-compare of re-serialized
        // compact JSON is order-stable.
        QCOMPARE(roundTripped, serialize(canon));
    }

    // Slot 4 — the registry inspect(canon → google-event) returns the
    // declared profile.
    void inspectDeclaresGoogleEdgeLoss()
    {
        const auto regs = makeCalendarRegistries();
        const Shape canon{ DomainId{QStringLiteral("calendar")},
                           EncodingId{QStringLiteral("canon")} };
        const Shape googleEvent{ DomainId{QStringLiteral("calendar")},
                                 EncodingId{QStringLiteral("google-event")} };

        const auto loss = regs.transformation.inspect(canon, googleEvent);
        QVERIFY2(!loss.isLossless(), "canon->google-event must be declared lossy");

        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("classification")}),
                 LossKind::Degraded);
        QVERIFY(!loss.affected.contains(PropertyId{QStringLiteral("recurrence")}));
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("priority")}),
                 LossKind::Reversible);
    }

    // Slot 5 — floating:true start has no Google form: pinned to UTC,
    // original carried in x-canon-floating.
    void floatingStartDegradesToUtcWithCarrier()
    {
        QJsonObject canon = makeCanonObject();
        QJsonObject start;
        start.insert(QStringLiteral("dateTime"), QStringLiteral("2026-06-01T09:00:00Z"));
        start.insert(QStringLiteral("floating"), true);
        canon.insert(QStringLiteral("start"), start);

        CanonToGoogleEventStage stage;
        const QJsonObject out = parse(stage.transform(serialize(canon)));

        const QJsonObject gStart = out.value(QStringLiteral("start")).toObject();
        QCOMPARE(gStart.value(QStringLiteral("dateTime")).toString(),
                 QStringLiteral("2026-06-01T09:00:00Z"));
        QCOMPARE(gStart.value(QStringLiteral("timeZone")).toString(), QStringLiteral("UTC"));
        QCOMPARE(out.value(QStringLiteral("extendedProperties")).toObject()
                     .value(QStringLiteral("private")).toObject()
                     .value(QStringLiteral("x-canon-floating")).toString(),
                 QStringLiteral("1"));

        // …and the carrier re-promotes on the way back.
        GoogleEventToCanonStage promote;
        const QJsonObject repromoted = parse(promote.transform(
            QJsonDocument(out).toJson(QJsonDocument::Compact)));
        const QJsonObject cStart = repromoted.value(QStringLiteral("start")).toObject();
        QCOMPARE(cStart.value(QStringLiteral("floating")).toBool(), true);
        QVERIFY(!cStart.contains(QStringLiteral("tz")));
    }
};

QTEST_MAIN(TestGoogleEventCanonEdge)
#include "tst_google_event_canon_edge.moc"
