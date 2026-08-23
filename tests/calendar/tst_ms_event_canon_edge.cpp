// EEE Phase 7.B step 2 — ms-event ⇄ canon edge suite. Pins the declared loss
// profile (docs/2026-08-23-ms-event-edge-loss-profile.md): promote from a
// captured-shaped payload (O57 wire truths), declared-vs-actual demote walk,
// round-trip identity for the lossless+carrier set, registry inspection.
//
// NOTE: plain concatenated string literals, NOT raw string literals — moc
// silently produces no output for Q_OBJECT classes in translation units
// containing a terminated R"(...)" literal (FINDINGS O59 tooling note).

#include <QTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include "canonenvelope.h"
#include "mseventcanonstages.h"
#include "calendardomaindefinition.h"
#include "calendarstockshapes.h"
#include "shaperegistries.h"
#include "lossprofile.h"

using Kalburator::Shape::CanonEnvelope::parse;
using Kalburator::Shape::CanonEnvelope::serialize;
using Kalburator::Shape::CanonEnvelope::stampEnvelope;
using Kalburator::Shape::CanonEnvelope::providerExtrasKey;
using Kalburator::Calendar::MsEventToCanonStage;
using Kalburator::Calendar::CanonToMsEventStage;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::Shape;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::LossKind;

namespace {

constexpr auto kSvepGuid = "{66f5926c-9c3e-4c14-9e4b-7a2f0d1c9eee}";

// Build a ShapeRegistries with the calendar domain fully registered
// (including the ms-event peer shape and its two EEE Phase 7.B edges).
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

// A captured-shaped Microsoft Graph v1.0 event (modeled on reference §1.2
// PLUS the O57 live-corpus realities: top-level uid (= iCalUId), split-brain
// zone vocabulary, year-1 sentinel responseStatus, zero-sentinel recurrence
// numerics with stray index, Bing-resolved location extras).
const QByteArray kMsEvent =
    "{"
    "\"id\": \"AQMkADAwATNiZmYAjsk3ZgBhZS1lNzQ0LTQw\","
    "\"changeKey\": \"CQAAABYAAAAmH0m\","
    "\"uid\": \"040000008200E00074C5B7101A82E00800000000\","
    "\"subject\": \"Planning Sync\","
    "\"body\": {\"contentType\": \"html\", \"content\": \"<html><body>Agenda: <b>roadmap</b></body></html>\"},"
    "\"bodyPreview\": \"Agenda: roadmap\","
    "\"start\": {\"dateTime\": \"2026-11-26T09:00:00.0000000\", \"timeZone\": \"UTC\"},"
    "\"end\": {\"dateTime\": \"2026-11-26T10:00:00.0000000\", \"timeZone\": \"UTC\"},"
    "\"originalStartTimeZone\": \"Eastern Standard Time\","
    "\"originalEndTimeZone\": \"Eastern Standard Time\","
    "\"location\": {\"displayName\": \"Room 4\", \"address\": {}, \"coordinates\": {}},"
    "\"locations\": ["
    "  {\"displayName\": \"Room 4\", \"address\": {}, \"coordinates\": {}, "
    "   \"locationUri\": \"https://www.bing.com/search?q=Room+4\", "
    "   \"uniqueId\": \"ROOM4GUID\", \"uniqueIdType\": \"bing\", \"addedBy\": null}"
    "],"
    "\"recurrence\": {\"pattern\": {\"type\": \"weekly\", \"interval\": 1,"
    "                                \"daysOfWeek\": [\"thursday\"],"
    "                                \"firstDayOfWeek\": \"sunday\","
    "                                \"index\": \"first\"},"
    "                 \"range\": {\"type\": \"noEnd\", \"startDate\": \"2026-11-26\","
    "                            \"recurrenceTimeZone\": \"Eastern Standard Time\"}},"
    "\"cancelledOccurrences\": [],"
    "\"type\": \"seriesMaster\","
    "\"seriesMasterId\": null,"
    "\"responseStatus\": {\"response\": \"organizer\", \"time\": \"0001-01-01T00:00:00Z\"},"
    "\"isAllDay\": false,"
    "\"isCancelled\": false,"
    "\"isOrganizer\": true,"
    "\"responseRequested\": true,"
    "\"importance\": \"high\","
    "\"sensitivity\": \"personal\","
    "\"showAs\": \"busy\","
    "\"categories\": [\"Work\"],"
    "\"organizer\": {\"emailAddress\": {\"name\": \"Clinton\", \"address\": \"clintoneist1@outlook.com\"}},"
    "\"attendees\": ["
    "  {\"type\": \"required\", \"status\": {\"response\": \"none\", \"time\": \"0001-01-01T00:00:00Z\"},"
    "   \"emailAddress\": {\"name\": \"Bob\", \"address\": \"bob@example.com\"}}"
    "],"
    "\"allowNewTimeProposals\": true,"
    "\"hideAttendees\": false,"
    "\"isReminderOn\": true,"
    "\"reminderMinutesBeforeStart\": 15,"
    "\"webLink\": \"https://outlook.live.com/calendar/...\","
    "\"createdDateTime\": \"2026-08-23T12:00:00.0000000Z\","
    "\"lastModifiedDateTime\": \"2026-08-23T13:30:00.0000000Z\""
    "}";

/// Minimal canon envelope with the given properties (mirrors the Google-edge
/// helper; uid matches the fixture's).
QJsonObject makeCanonObject()
{
    QJsonObject obj;
    obj.insert(QStringLiteral("uid"),
               QStringLiteral("040000008200E00074C5B7101A82E00800000000"));
    QJsonObject start;
    start.insert(QStringLiteral("dateTime"), QStringLiteral("2026-11-26T14:00:00Z"));
    start.insert(QStringLiteral("tz"), QStringLiteral("America/New_York"));
    start.insert(QStringLiteral("floating"), false);
    obj.insert(QStringLiteral("start"), start);
    stampEnvelope(obj, QStringLiteral("calendar"), obj.value(QStringLiteral("uid")).toString());
    return obj;
}

/// Extract a carrier value from a demoted ms-event JSON object.
/// NOTE: absence is signalled via hasCarrier(); a bare `return {}` from a
/// QJsonValue-returning helper is Null-typed on this Qt (isUndefined()==false),
/// which bit the first version of this suite.
QJsonValue carrierValue(const QJsonObject& wire, const QString& kebabKey)
{
    for (const auto& sv :
         wire.value(QStringLiteral("singleValueExtendedProperties")).toArray()) {
        const QString id = sv.toObject().value(QStringLiteral("id")).toString();
        if (id.contains(QLatin1String(kSvepGuid)) && id.endsWith(kebabKey))
            return sv.toObject().value(QStringLiteral("value"));
    }
    return {};
}

bool hasCarrier(const QJsonObject& wire, const QString& kebabKey)
{
    for (const auto& sv :
         wire.value(QStringLiteral("singleValueExtendedProperties")).toArray()) {
        const QString id = sv.toObject().value(QStringLiteral("id")).toString();
        if (id.contains(QLatin1String(kSvepGuid)) && id.endsWith(kebabKey))
            return true;
    }
    return false;
}

} // namespace

class TestMsEventCanonEdge : public QObject {
    Q_OBJECT
private slots:

    // Slot 1 — promote: every mapped field lands in canon; every unmapped
    // Graph field lands verbatim under providerExtras["msgraph"]; O57 wire
    // realities are handled (sentinels normalize ABSENT, split-brain zones
    // preserve originals, Bing extras stash).
    void promoteCapturedShapedPayloadIsLossless()
    {
        MsEventToCanonStage stage;
        const QByteArray canonBytes = stage.transform(kMsEvent);
        QVERIFY2(!canonBytes.isEmpty(), "promote returned empty bytes");

        const QJsonObject canon = parse(canonBytes);
        // uid ← top-level uid (O57(a))
        QCOMPARE(canon.value(QStringLiteral("uid")).toString(),
                 QStringLiteral("040000008200E00074C5B7101A82E00800000000"));
        QCOMPARE(canon.value(QStringLiteral("summary")).toString(),
                 QStringLiteral("Planning Sync"));
        // body HTML → descriptionHtml (Graph holds both natively)
        QVERIFY(canon.contains(QStringLiteral("descriptionHtml")));
        QVERIFY(canon.value(QStringLiteral("descriptionHtml")).toString()
                    .contains(QLatin1String("<b>roadmap</b>")));
        // timestamps normalized to second-granular UTC ISO
        QCOMPARE(canon.value(QStringLiteral("created")).toString(),
                 QStringLiteral("2026-08-23T12:00:00Z"));
        QCOMPARE(canon.value(QStringLiteral("lastModified")).toString(),
                 QStringLiteral("2026-08-23T13:30:00Z"));

        // start/end: UTC wire → canon UTC ISO + tz + floating:false
        const QJsonObject start = canon.value(QStringLiteral("start")).toObject();
        QCOMPARE(start.value(QStringLiteral("dateTime")).toString(),
                 QStringLiteral("2026-11-26T09:00:00Z"));
        QCOMPARE(start.value(QStringLiteral("tz")).toString(), QStringLiteral("UTC"));
        QCOMPARE(start.value(QStringLiteral("floating")).toBool(), false);

        // recurrence: weekly pattern (+ stray index ignored per O57(f)) → RRULE
        const QJsonArray recurrence = canon.value(QStringLiteral("recurrence")).toArray();
        QCOMPARE(recurrence.size(), 1);
        QCOMPARE(recurrence.at(0).toString(),
                 QStringLiteral("RRULE:FREQ=WEEKLY;INTERVAL=1;BYDAY=TH;WKST=SU"));

        // classification: sensitivity "personal" promoted verbatim (the demote
        // owns personal→private+carrier)
        QCOMPARE(canon.value(QStringLiteral("classification")).toString(),
                 QStringLiteral("personal"));
        QCOMPARE(canon.value(QStringLiteral("freeBusyStatus")).toString(),
                 QStringLiteral("busy"));
        QCOMPARE(canon.value(QStringLiteral("timeTransparency")).toString(),
                 QStringLiteral("opaque"));
        QCOMPARE(canon.value(QStringLiteral("priority")).toInt(), 1);  // high→1

        // organizer
        QCOMPARE(canon.value(QStringLiteral("organizer")).toObject()
                     .value(QStringLiteral("email")).toString(),
                 QStringLiteral("clintoneist1@outlook.com"));

        // attendees: responseStatus.status → partstat (sentinel time dropped)
        const QJsonArray attendees = canon.value(QStringLiteral("attendees")).toArray();
        QCOMPARE(attendees.size(), 1);
        QCOMPARE(attendees.at(0).toObject().value(QStringLiteral("email")).toString(),
                 QStringLiteral("bob@example.com"));
        QCOMPARE(attendees.at(0).toObject().value(QStringLiteral("partstat")).toString(),
                 QStringLiteral("needsAction"));  // "none" → needsAction? see below
        QCOMPARE(attendees.at(0).toObject().value(QStringLiteral("role")).toString(),
                 QStringLiteral("required"));

        // alarms: reminder pair → display alarm
        const QJsonArray alarms = canon.value(QStringLiteral("alarms")).toArray();
        QCOMPARE(alarms.size(), 1);
        QCOMPARE(alarms.at(0).toObject().value(QStringLiteral("type")).toInt(), 1);
        QCOMPARE(alarms.at(0).toObject().value(QStringLiteral("offset")).toInt(), -900);

        // categories direct
        QCOMPARE(canon.value(QStringLiteral("categories")).toArray().size(), 1);

        // url ← webLink
        QVERIFY(canon.value(QStringLiteral("url")).toString()
                    .startsWith(QLatin1String("https://outlook.live.com")));

        // O57(d): year-1 sentinel responseStatus must NOT manufacture stamps —
        // no created-class fields beyond the real ones, and the sentinel never
        // appears anywhere in the promoted record.
        QVERIFY(!serialize(canon).contains("0001-01-01"));

        // O57(b): split-brain zones — originalStartTimeZone is master-level
        // creation metadata; it flows to extras verbatim.
        const QJsonObject extrasMs = canon.value(providerExtrasKey())
                                         .toObject()
                                         .value(QStringLiteral("msgraph"))
                                         .toObject();
        QCOMPARE(extrasMs.value(QStringLiteral("originalStartTimeZone")).toString(),
                 QStringLiteral("Eastern Standard Time"));
        // transport identity stashed
        QCOMPARE(extrasMs.value(QStringLiteral("id")).toString(),
                 QStringLiteral("AQMkADAwATNiZmYAjsk3ZgBhZS1lNzQ0LTQw"));
        QCOMPARE(extrasMs.value(QStringLiteral("changeKey")).toString(),
                 QStringLiteral("CQAAABYAAAAmH0m"));
        // structural topology: derivable types are NOT stashed (the demote
        // side reconstructs them structurally — keeps C→G→C byte-equal)
        QVERIFY(!extrasMs.contains(QStringLiteral("type")));
        // Bing location extras stashed (O57(c))
        QVERIFY(!extrasMs.value(QStringLiteral("locations")).toArray().isEmpty());
    }

    // Slot 2 — each Degraded/Simplified/Dropped declaration matches the
    // actual demote output.
    void demoteDeclaredLossMatchesReality()
    {
        CanonToMsEventStage stage;

        // classification Degraded: canon "personal" → sensitivity private +
        // carrier restores it on re-promote.
        {
            QJsonObject canon = makeCanonObject();
            canon.insert(QStringLiteral("classification"), QStringLiteral("personal"));
            const QJsonObject out = parse(stage.transform(serialize(canon)));
            QCOMPARE(out.value(QStringLiteral("sensitivity")).toString(),
                     QStringLiteral("private"));
            QCOMPARE(carrierValue(out, QStringLiteral("x-canon-classification")),
                     QJsonValue(QStringLiteral("personal")));
        }

        // status Degraded: tentative has no master-level form → carrier.
        {
            QJsonObject canon = makeCanonObject();
            canon.insert(QStringLiteral("status"), QStringLiteral("tentative"));
            const QJsonObject out = parse(stage.transform(serialize(canon)));
            QVERIFY(!out.contains(QStringLiteral("isCancelled")));
            QCOMPARE(carrierValue(out, QStringLiteral("x-canon-status")),
                     QJsonValue(QStringLiteral("tentative")));
            // cancelled DOES have a form.
            canon.insert(QStringLiteral("status"), QStringLiteral("cancelled"));
            const QJsonObject out2 = parse(stage.transform(serialize(canon)));
            QCOMPARE(out2.value(QStringLiteral("isCancelled")).toBool(), true);
        }

        // color Dropped: nowhere in the output — not even a carrier.
        {
            QJsonObject canon = makeCanonObject();
            canon.insert(QStringLiteral("color"), QStringLiteral("#ff0000"));
            const QByteArray dbg = stage.transform(serialize(canon));
            qWarning("COLOR DEMOTE: %s", dbg.constData());
            const QJsonObject out = parse(dbg);
            QVERIFY(!out.contains(QStringLiteral("color")));
            QVERIFY(!hasCarrier(out, QStringLiteral("x-canon-color")));
        }

        // sequence Simplified: carried via carrier (Graph has no SEQUENCE).
        {
            QJsonObject canon = makeCanonObject();
            canon.insert(QStringLiteral("sequence"), 4);
            const QJsonObject out = parse(stage.transform(serialize(canon)));
            QVERIFY(!out.contains(QStringLiteral("sequence")));
            QCOMPARE(carrierValue(out, QStringLiteral("x-canon-sequence")),
                     QJsonValue(QStringLiteral("4")));
        }

        // guestsCanModify Reversible: carrier only, no Graph field.
        {
            QJsonObject canon = makeCanonObject();
            canon.insert(QStringLiteral("guestsCanModify"), true);
            const QJsonObject out = parse(stage.transform(serialize(canon)));
            QVERIFY(!out.contains(QStringLiteral("guestsCanModify")));
            QCOMPARE(carrierValue(out, QStringLiteral("x-canon-guests-can-modify")),
                     QJsonValue(QStringLiteral("true")));
        }

        // priority Simplified: bucket mapping; lossy buckets carry the int.
        {
            QJsonObject canon = makeCanonObject();
            canon.insert(QStringLiteral("priority"), 3);   // high bucket, lossy
            const QJsonObject out = parse(stage.transform(serialize(canon)));
            QCOMPARE(out.value(QStringLiteral("importance")).toString(),
                     QStringLiteral("high"));
            QCOMPARE(carrierValue(out, QStringLiteral("x-canon-priority")),
                     QJsonValue(QStringLiteral("3")));

            canon.insert(QStringLiteral("priority"), 5);   // exact bucket
            const QJsonObject out5 = parse(stage.transform(serialize(canon)));
            QCOMPARE(out5.value(QStringLiteral("importance")).toString(),
                     QStringLiteral("normal"));
            QVERIFY(!hasCarrier(out5, QStringLiteral("x-canon-priority")));
        }

        // allDay Degraded: date form → midnight-to-midnight pair + isAllDay.
        {
            QJsonObject canon = makeCanonObject();
            canon.insert(QStringLiteral("allDay"), true);
            QJsonObject s; s.insert(QStringLiteral("date"), QStringLiteral("2026-12-24"));
            s.insert(QStringLiteral("allDay"), true);
            canon.insert(QStringLiteral("start"), s);
            QJsonObject e; e.insert(QStringLiteral("date"), QStringLiteral("2026-12-25"));
            e.insert(QStringLiteral("allDay"), true);
            canon.insert(QStringLiteral("end"), e);
            const QJsonObject out = parse(stage.transform(serialize(canon)));
            QCOMPARE(out.value(QStringLiteral("isAllDay")).toBool(), true);
            QCOMPARE(out.value(QStringLiteral("start")).toObject()
                         .value(QStringLiteral("dateTime")).toString(),
                     QStringLiteral("2026-12-24T00:00:00.0000000"));
            QCOMPARE(out.value(QStringLiteral("end")).toObject()
                         .value(QStringLiteral("dateTime")).toString(),
                     QStringLiteral("2026-12-25T00:00:00.0000000"));
        }

        // unrepresentable recurrence Simplified: reduced pattern + full
        // original carried.
        {
            QJsonObject canon = makeCanonObject();
            canon.insert(QStringLiteral("recurrence"),
                         QJsonArray{QStringLiteral("RRULE:FREQ=HOURLY;INTERVAL=6")});
            const QJsonObject out = parse(stage.transform(serialize(canon)));
            QCOMPARE(out.value(QStringLiteral("recurrence")).toObject()
                         .value(QStringLiteral("pattern")).toObject()
                         .value(QStringLiteral("type")).toString(),
                     QStringLiteral("daily"));
            const QJsonValue carried =
                carrierValue(out, QStringLiteral("x-canon-recurrence"));
            QVERIFY2(carried.toString().contains(QLatin1String("HOURLY")),
                     "full original RRULE must ride the carrier");
        }

        // geo Dropped (cross-kind ruling).
        {
            QJsonObject canon = makeCanonObject();
            QJsonObject geo; geo.insert(QStringLiteral("lat"), 52.0);
            canon.insert(QStringLiteral("geo"), geo);
            const QJsonObject out = parse(stage.transform(serialize(canon)));
            QVERIFY(!out.contains(QStringLiteral("geo")));
            QVERIFY(!hasCarrier(out, QStringLiteral("x-canon-geo")));
        }
    }

    // Slot 3 — canon → ms-event → canon byte-equal on the lossless +
    // carrier-restored property set.
    void losslessRoundTripIsIdentity()
    {
        QJsonObject canon = makeCanonObject();
        canon.insert(QStringLiteral("sequence"), 2);
        canon.insert(QStringLiteral("created"), QStringLiteral("2026-08-23T12:00:00Z"));
        canon.insert(QStringLiteral("lastModified"), QStringLiteral("2026-08-23T13:30:00Z"));
        canon.insert(QStringLiteral("summary"), QStringLiteral("Sync"));
        canon.insert(QStringLiteral("descriptionHtml"), QStringLiteral("<b>Body</b>"));
        canon.insert(QStringLiteral("location"), QStringLiteral("Room 4"));
        canon.insert(QStringLiteral("classification"), QStringLiteral("confidential"));
        canon.insert(QStringLiteral("freeBusyStatus"), QStringLiteral("busy"));
        canon.insert(QStringLiteral("timeTransparency"), QStringLiteral("opaque"));
        {
            QJsonObject end;
            end.insert(QStringLiteral("dateTime"), QStringLiteral("2026-11-26T15:00:00Z"));
            end.insert(QStringLiteral("tz"), QStringLiteral("America/New_York"));
            end.insert(QStringLiteral("floating"), false);
            canon.insert(QStringLiteral("end"), end);
        }
        canon.insert(QStringLiteral("recurrence"),
                     QJsonArray{QStringLiteral("RRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=TU,TH;WKST=SU")});
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
        canon.insert(QStringLiteral("priority"), 5);   // exact bucket: no carrier noise
        canon.insert(QStringLiteral("url"), QStringLiteral("https://example.com/notes"));
        canon.insert(QStringLiteral("allowNewTimeProposals"), true);
        canon.insert(QStringLiteral("hideAttendees"), false);
        canon.insert(QStringLiteral("categories"),
                     QJsonArray{QStringLiteral("Work"), QStringLiteral("Focus")});
        {
            QJsonArray alarms;
            QJsonObject alarm;
            alarm.insert(QStringLiteral("type"), 1);
            alarm.insert(QStringLiteral("offset"), -900);
            alarms.append(alarm);
            canon.insert(QStringLiteral("alarms"), alarms);
        }
        {
            QJsonObject typed;
            typed.insert(QStringLiteral("x-app-key"), QStringLiteral("app-value"));
            canon.insert(QStringLiteral("typedProperties"), typed);
        }
        {
            QJsonObject om;
            om.insert(QStringLiteral("joinUrl"), QStringLiteral("https://teams.example.com/join"));
            canon.insert(QStringLiteral("onlineMeeting"), om);
        }

        CanonToMsEventStage demote;
        MsEventToCanonStage promote;
        const QByteArray msBytes = demote.transform(serialize(canon));
        QVERIFY2(!msBytes.isEmpty(), "demote returned empty bytes");
        const QByteArray roundTripped = promote.transform(msBytes);
        QVERIFY2(!roundTripped.isEmpty(), "re-promote returned empty bytes");
        // QJsonDocument sorts object keys, so byte-compare of re-serialized
        // compact JSON is order-stable.
        QCOMPARE(roundTripped, serialize(canon));
    }

    // Slot 3b — an UNREPRESENTABLE rule also round-trips byte-equal: the
    // reduced pattern rides the wire, the full original rides the carrier.
    void unrepresentableRruleRoundTripsByteIdentically()
    {
        QJsonObject canon = makeCanonObject();
        canon.insert(QStringLiteral("recurrence"),
                     QJsonArray{QStringLiteral("RRULE:FREQ=HOURLY;INTERVAL=6")});

        CanonToMsEventStage demote;
        MsEventToCanonStage promote;
        const QByteArray msBytes = demote.transform(serialize(canon));
        const QByteArray roundTripped = promote.transform(msBytes);
        QCOMPARE(roundTripped, serialize(canon));
    }

    // Slot 4 — the registry inspect(canon → ms-event) returns the declared
    // profile.
    void inspectDeclaresMsEdgeLoss()
    {
        const auto regs = makeCalendarRegistries();
        const Shape canon{ DomainId{QStringLiteral("calendar")},
                           EncodingId{QStringLiteral("canon")} };
        const Shape msEvent{ DomainId{QStringLiteral("calendar")},
                             EncodingId{QStringLiteral("ms-event")} };

        const auto loss = regs.transformation.inspect(canon, msEvent);
        QVERIFY2(!loss.isLossless(), "canon->ms-event must be declared lossy");

        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("classification")}),
                 LossKind::Degraded);
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("recurrence")}),
                 LossKind::Simplified);
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("color")}),
                 LossKind::Dropped);
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("sequence")}),
                 LossKind::Simplified);
        // The promote direction is declared lossless.
        const auto promoteLoss = regs.transformation.inspect(
            msEvent,
            Shape{ DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("canon")} });
        QVERIFY2(promoteLoss.isLossless(), "ms-event->canon must be lossless");
    }

    // Slot 5 — O57(b) split-brain zones: Windows vocabulary normalizes to
    // IANA with the ORIGINAL preserved in tzOriginal; demote re-emits the
    // author's vocabulary verbatim.
    void windowsZoneNormalizesWithOriginalPreserved()
    {
        MsEventToCanonStage promote;
        CanonToMsEventStage demote;

        // Promote: authored-in-ET event whose endpoints say UTC but whose
        // original*TimeZone says "Eastern Standard Time" — the endpoint zone
        // itself in Windows vocabulary drives resolution here.
        QJsonObject wire;
        wire.insert(QStringLiteral("id"), QStringLiteral("ztest"));
        wire.insert(QStringLiteral("start"),
                    QJsonObject{{QStringLiteral("dateTime"),
                                 QStringLiteral("2026-11-26T09:00:00.0000000")},
                                {QStringLiteral("timeZone"),
                                 QStringLiteral("Eastern Standard Time")}});
        wire.insert(QStringLiteral("end"),
                    QJsonObject{{QStringLiteral("dateTime"),
                                 QStringLiteral("2026-11-26T10:00:00.0000000")},
                                {QStringLiteral("timeZone"),
                                 QStringLiteral("Eastern Standard Time")}});
        const QJsonObject canon = parse(promote.transform(
            QJsonDocument(wire).toJson(QJsonDocument::Compact)));

        const QJsonObject cStart = canon.value(QStringLiteral("start")).toObject();
        QCOMPARE(cStart.value(QStringLiteral("tz")).toString(),
                 QStringLiteral("America/New_York"));
        QCOMPARE(cStart.value(QStringLiteral("tzOriginal")).toString(),
                 QStringLiteral("Eastern Standard Time"));
        QCOMPARE(cStart.value(QStringLiteral("dateTime")).toString(),
                 QStringLiteral("2026-11-26T14:00:00Z"));   // EST = UTC-5 in Nov

        // Demote: the author's vocabulary comes back verbatim.
        const QJsonObject out = parse(demote.transform(serialize(canon)));
        QCOMPARE(out.value(QStringLiteral("start")).toObject()
                     .value(QStringLiteral("timeZone")).toString(),
                 QStringLiteral("Eastern Standard Time"));
        QCOMPARE(out.value(QStringLiteral("start")).toObject()
                     .value(QStringLiteral("dateTime")).toString(),
                 QStringLiteral("2026-11-26T09:00:00.0000000"));
    }

    // Slot 6 — floating:true has no Graph form: pinned to UTC, restored by
    // the x-canon-floating carrier on re-promote.
    void floatingStartDegradesToUtcWithCarrier()
    {
        QJsonObject canon = makeCanonObject();
        QJsonObject start;
        start.insert(QStringLiteral("dateTime"), QStringLiteral("2026-11-26T09:00:00Z"));
        start.insert(QStringLiteral("floating"), true);
        canon.insert(QStringLiteral("start"), start);

        CanonToMsEventStage stage;
        const QJsonObject out = parse(stage.transform(serialize(canon)));

        const QJsonObject gStart = out.value(QStringLiteral("start")).toObject();
        QCOMPARE(gStart.value(QStringLiteral("dateTime")).toString(),
                 QStringLiteral("2026-11-26T09:00:00.0000000"));
        QCOMPARE(gStart.value(QStringLiteral("timeZone")).toString(), QStringLiteral("UTC"));
        QCOMPARE(carrierValue(out, QStringLiteral("x-canon-floating")),
                 QJsonValue(QStringLiteral("true")));

        // …and the carrier re-promotes on the way back.
        MsEventToCanonStage promote;
        const QJsonObject repromoted = parse(promote.transform(
            QJsonDocument(out).toJson(QJsonDocument::Compact)));
        const QJsonObject cStart = repromoted.value(QStringLiteral("start")).toObject();
        QCOMPARE(cStart.value(QStringLiteral("floating")).toBool(), true);
        QVERIFY(!cStart.contains(QStringLiteral("tz")));
    }

    // Slot 7 — exception records: type:"exception" + originalStart promotes
    // to recurrenceId keyed by original start; demote reconstructs the
    // topology structurally (never stored).
    void exceptionRecordKeysOnOriginalStart()
    {
        QJsonObject wire;
        wire.insert(QStringLiteral("id"), QStringLiteral("exc1"));
        wire.insert(QStringLiteral("type"), QStringLiteral("exception"));
        wire.insert(QStringLiteral("originalStart"),
                    QStringLiteral("2026-12-03T14:00:00.0000000Z"));
        wire.insert(QStringLiteral("start"),
                    QJsonObject{{QStringLiteral("dateTime"),
                                 QStringLiteral("2026-12-03T16:00:00.0000000")},
                                {QStringLiteral("timeZone"), QStringLiteral("UTC")}});
        wire.insert(QStringLiteral("end"),
                    QJsonObject{{QStringLiteral("dateTime"),
                                 QStringLiteral("2026-12-03T17:00:00.0000000")},
                                {QStringLiteral("timeZone"), QStringLiteral("UTC")}});
        wire.insert(QStringLiteral("seriesMasterId"), QStringLiteral("master1"));

        MsEventToCanonStage promote;
        const QJsonObject canon = parse(promote.transform(
            QJsonDocument(wire).toJson(QJsonDocument::Compact)));
        QCOMPARE(canon.value(QStringLiteral("recurrenceId")).toObject()
                     .value(QStringLiteral("dateTime")).toString(),
                 QStringLiteral("2026-12-03T14:00:00Z"));

        CanonToMsEventStage demote;
        const QJsonObject out = parse(demote.transform(serialize(canon)));
        QCOMPARE(out.value(QStringLiteral("type")).toString(),
                 QStringLiteral("exception"));
        QCOMPARE(out.value(QStringLiteral("originalStart")).toString(),
                 QStringLiteral("2026-12-03T14:00:00.0000000Z"));
    }
};

QTEST_MAIN(TestMsEventCanonEdge)
#include "tst_ms_event_canon_edge.moc"
