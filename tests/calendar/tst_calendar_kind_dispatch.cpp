#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>

#include "calendarcanonproperties.h"
#include "canonenvelope.h"
#include "icalcanonstages.h"
#include "calendardomaindefinition.h"
#include "calendarstockshapes.h"
#include "shaperegistries.h"

#include "journalcanonfields.h"

// IP.1 (incidence-parity campaign) — catalogue/emitter coverage gate.
#include "../shape/canonkeycoverage.h"
#include "todocanonproperties.h"
#include "vtodocanonstages.h"
#include "contactscanonproperties.h"
#include "vcardcanonstages.h"
#include "googlepersoncanonstages.h"
#include "mscontactcanonstages.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Journal>

using Kalburator::Calendar::journalFieldsToCanon;
using Kalburator::Calendar::canonObjectToJournalBytes;
using Kalburator::Shape::CanonEnvelope::parse;

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

KCalendarCore::Journal::Ptr parseJournal(const QByteArray &bytes)
{
    KCalendarCore::ICalFormat fmt;
    return fmt.fromString(QString::fromUtf8(bytes))
        .dynamicCast<KCalendarCore::Journal>();
}

const QByteArray kJournal =
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\n"
    "BEGIN:VJOURNAL\r\nUID:journal-1\r\nSUMMARY:Trip notes\r\n"
    "DESCRIPTION:Saw the sea\r\nDTSTART:20260601T090000Z\r\n"
    "CATEGORIES:Travel\r\nEND:VJOURNAL\r\nEND:VCALENDAR\r\n";

const QByteArray kVtodo =
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\n"
    "BEGIN:VTODO\r\nUID:todo-handoff-1\r\nSUMMARY:Buy milk\r\n"
    "STATUS:NEEDS-ACTION\r\nPERCENT-COMPLETE:0\r\nEND:VTODO\r\nEND:VCALENDAR\r\n";

// ---------------------------------------------------------------------------
// IP.1 maximal fixtures — one per (domain, kind) pair, each exercising every
// optional top-level field its promote emitter can produce. A minimal
// fixture would make the coverage gate below vacuous (PLAN.md IP.1).
// ---------------------------------------------------------------------------

// Maximal VEVENT: every top-level key eventFieldsToCanon() can emit except
// recurrenceId/recurrenceRange, which are exercised by kMaximalVeventException
// below (a real exception occurrence does not also carry its own RRULE, and
// the gate only needs each key to appear in *some* fixture, not all in one).
const QByteArray kMaximalVevent =
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\n"
    "BEGIN:VEVENT\r\n"
    "UID:event-maximal-1\r\n"
    "SEQUENCE:3\r\n"
    "CREATED:20260101T000000Z\r\n"
    "LAST-MODIFIED:20260102T000000Z\r\n"
    "SUMMARY:Maximal Event\r\n"
    "DESCRIPTION:Full description\r\n"
    "X-ALT-DESC;FMTTYPE=text/html:<p>Full description</p>\r\n"
    "LOCATION:Room 42\r\n"
    "STATUS:CONFIRMED\r\n"
    "CLASS:PRIVATE\r\n"
    "TRANSP:TRANSPARENT\r\n"
    "X-MICROSOFT-CDO-BUSYSTATUS:BUSY\r\n"
    "DTSTART:20260601T090000Z\r\n"
    "DTEND:20260601T100000Z\r\n"
    "RRULE:FREQ=WEEKLY;COUNT=5\r\n"
    "EXDATE:20260608T090000Z\r\n"
    "COLOR:blue\r\n"
    "CATEGORIES:Work,Important\r\n"
    "URL:https://example.com/event\r\n"
    "ORGANIZER;CN=Boss Person:mailto:boss@example.com\r\n"
    "ATTENDEE;CN=Alice;ROLE=REQ-PARTICIPANT;PARTSTAT=ACCEPTED;RSVP=TRUE:mailto:alice@example.com\r\n"
    "PRIORITY:1\r\n"
    "BEGIN:VALARM\r\n"
    "ACTION:DISPLAY\r\n"
    "TRIGGER:-PT15M\r\n"
    "DESCRIPTION:Reminder\r\n"
    "END:VALARM\r\n"
    "ATTACH;FMTTYPE=text/plain:https://example.com/file.txt\r\n"
    "X-CANON-CUSTOM:extra-value\r\n"
    "END:VEVENT\r\nEND:VCALENDAR\r\n";

// Detached exception occurrence — exercises recurrenceId/recurrenceRange.
const QByteArray kMaximalVeventException =
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\n"
    "BEGIN:VEVENT\r\n"
    "UID:event-maximal-1\r\n"
    "RECURRENCE-ID;RANGE=THISANDFUTURE:20260608T090000Z\r\n"
    "SUMMARY:Maximal Event (moved)\r\n"
    "DTSTART:20260608T110000Z\r\n"
    "DTEND:20260608T120000Z\r\n"
    "END:VEVENT\r\nEND:VCALENDAR\r\n";

// Maximal VTODO: every top-level key todoFieldsToCanon() (the shared VTODO
// emitter — see PLAN.md fact 1) can emit, INCLUDING the three O78 drift keys
// (seriesSplitOf via X-CANON-SERIES-SPLIT-OF, completionAnchor via
// X-ORG-REPEATER, providerExtrasDigest via any unmapped X-prop). This exact
// fixture feeds BOTH the {calendar,canon} and {todo,canon} pairs below,
// since both ride the same emitter (icalcanonstages.cpp:56 / vtodocanonstages.cpp:45).
const QByteArray kMaximalVtodo =
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\n"
    "BEGIN:VTODO\r\n"
    "UID:vtodo-maximal-1\r\n"
    "CREATED:20260101T000000Z\r\n"
    "LAST-MODIFIED:20260102T000000Z\r\n"
    "SUMMARY:Maximal Todo\r\n"
    "DESCRIPTION:Full todo description\r\n"
    "X-ALT-DESC;FMTTYPE=text/html:<p>Full todo description</p>\r\n"
    "STATUS:IN-PROCESS\r\n"
    "PERCENT-COMPLETE:40\r\n"
    "PRIORITY:2\r\n"
    "CATEGORIES:Errands,Home\r\n"
    "DTSTART:20260601T090000Z\r\n"
    "DUE:20260610T170000Z\r\n"
    "COMPLETED:20260605T120000Z\r\n"
    "RRULE:FREQ=WEEKLY;COUNT=3\r\n"
    "X-CANON-SERIES-SPLIT-OF:old-master-uid\r\n"
    "X-ORG-REPEATER:.+1w\r\n"
    "BEGIN:VALARM\r\n"
    "ACTION:DISPLAY\r\n"
    "TRIGGER:-PT30M\r\n"
    "DESCRIPTION:Todo reminder\r\n"
    "END:VALARM\r\n"
    "LOCATION:Home Office\r\n"
    "GEO:43.6532;-79.3832\r\n"
    "RELATED-TO;RELTYPE=PARENT:parent-uid-1\r\n"
    "X-CANON-CUSTOM:extra-value\r\n"
    "END:VTODO\r\nEND:VCALENDAR\r\n";

// Detached VTODO exception occurrence — exercises recurrenceId/recurrenceRange
// (same reasoning as kMaximalVeventException: a real exception instance does
// not also carry its own RRULE).
const QByteArray kMaximalVtodoException =
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\n"
    "BEGIN:VTODO\r\n"
    "UID:vtodo-maximal-1\r\n"
    "RECURRENCE-ID;RANGE=THISANDFUTURE:20260608T090000Z\r\n"
    "SUMMARY:Maximal Todo (moved)\r\n"
    "DUE:20260615T170000Z\r\n"
    "END:VTODO\r\nEND:VCALENDAR\r\n";

// Maximal vCard4: every top-level key VCard4ToCanonStage can emit.
const QByteArray kMaximalVcard =
    "BEGIN:VCARD\r\nVERSION:4.0\r\n"
    "UID:vcard-maximal-1\r\n"
    "FN:Ada Lovelace\r\n"
    "N:Lovelace;Ada;Augusta;Countess;\r\n"
    "NICKNAME:Ada\r\n"
    "EMAIL;TYPE=WORK;PREF=1:ada@example.com\r\n"
    "EMAIL;TYPE=HOME:ada.home@example.com\r\n"
    "TEL;TYPE=CELL;PREF=1:+1-555-123-4567\r\n"
    "ADR;TYPE=HOME:;;1 Analytical Engine Way;London;;E1 1AA;UK\r\n"
    "ORG:Royal Society;Mathematics\r\n"
    "TITLE:Mathematician\r\n"
    "ROLE:Analyst\r\n"
    "URL:https://example.com/ada\r\n"
    "IMPP:xmpp:ada@example.com\r\n"
    "BDAY:18151210\r\n"
    "ANNIVERSARY:18351208\r\n"
    "GENDER:F\r\n"
    "NOTE:Wrote the first published algorithm.\r\n"
    "PHOTO;VALUE=URI:https://example.com/ada.jpg\r\n"
    "CATEGORIES:Historical,Mathematics\r\n"
    "LANG:en\r\n"
    "TZ:-05:00\r\n"
    "RELATED;TYPE=colleague:urn:uuid:babbage-uid\r\n"
    "X-TEST-CUSTOM:hello\r\n"
    "END:VCARD\r\n";

// Maximal Google People `Person` JSON: every top-level key
// GooglePersonToCanonStage can emit — including the categories/timeZone/
// anniversary/significantDates carried generically via clientData
// x-canon-* rows (there is no native Google People home for them; see
// googlepersoncanonstages.cpp's "unhandled canon props" comment).
//
// NOTE: built from concatenated quoted literals, NOT a raw string literal
// (O59 house rule — a terminated raw string literal `R"(...)"` in a
// translation unit containing a Q_OBJECT class makes moc silently emit no
// output at all, symptom "undefined reference to vtable").
const QByteArray kMaximalGooglePerson = QByteArrayLiteral(
    "{"
    "\"resourceName\":\"people/c123\","
    "\"names\":[{\"givenName\":\"Grace\",\"familyName\":\"Hopper\",\"displayName\":\"Grace Hopper\"}],"
    "\"nicknames\":[{\"value\":\"Amazing Grace\"}],"
    "\"emailAddresses\":[{\"value\":\"grace@example.com\",\"type\":\"work\",\"metadata\":{\"primary\":true}}],"
    "\"phoneNumbers\":[{\"value\":\"+1-555-0100\",\"type\":\"mobile\"}],"
    "\"addresses\":[{\"streetAddress\":\"1 Main St\",\"city\":\"Arlington\",\"region\":\"VA\","
    "\"postalCode\":\"22201\",\"country\":\"USA\",\"countryCode\":\"US\","
    "\"formattedValue\":\"1 Main St, Arlington, VA\",\"type\":\"home\"}],"
    "\"organizations\":[{\"name\":\"US Navy\",\"title\":\"Rear Admiral\"}],"
    "\"urls\":[{\"value\":\"https://example.com/grace\"}],"
    "\"relations\":[{\"person\":\"Vincent Foster\",\"type\":\"spouse\"}],"
    "\"externalIds\":[{\"value\":\"ext-1\",\"type\":\"custom\"}],"
    "\"memberships\":[{\"contactGroupMembership\":{\"contactGroupId\":\"myContacts\"}}],"
    "\"imClients\":[{\"value\":\"sip:grace@example.com\",\"protocol\":\"sip\"}],"
    "\"calendarUrls\":[{\"url\":\"https://cal.example.com/grace\"}],"
    "\"interests\":[{\"value\":\"Compilers\"}],"
    "\"skills\":[{\"value\":\"COBOL\"}],"
    "\"occupations\":[{\"value\":\"Computer Scientist\"}],"
    "\"locales\":[{\"value\":\"en\"}],"
    "\"sipAddresses\":[{\"value\":\"sip:grace@example.com\"}],"
    "\"birthdays\":[{\"date\":{\"year\":1906,\"month\":12,\"day\":9}}],"
    "\"genders\":[{\"value\":\"female\"}],"
    "\"biographies\":[{\"value\":\"COBOL pioneer.\"}],"
    "\"photos\":[{\"url\":\"https://example.com/grace.jpg\"}],"
    "\"clientData\":["
    "{\"key\":\"x-canon-categories\",\"value\":\"[\\\"Historical\\\"]\"},"
    "{\"key\":\"x-canon-time-zone\",\"value\":\"America/New_York\"},"
    "{\"key\":\"x-canon-anniversary\",\"value\":\"{\\\"date\\\":\\\"1934-06-01\\\",\\\"hasYear\\\":true}\"},"
    "{\"key\":\"x-canon-significant-dates\",\"value\":\"[{\\\"date\\\":\\\"1944-01-01\\\",\\\"label\\\":\\\"Enlisted\\\"}]\"}"
    "]"
    "}");

// Maximal Microsoft Graph `contact` JSON: every top-level key
// MsContactToCanonStage can emit — including gender/anniversary/
// significantDates/timeZone/languages/interests/skills/calendarUrls/
// sipAddresses/memberships/externalIds carried generically via the
// kalburator.canon open-extension x-canon-* rows (no native Graph contact
// home for them; see mscontactcanonstages.cpp's "unhandled canon props"
// comment).
// NOTE: concatenated quoted literals, not a raw string literal — see the
// O59 house-rule note on kMaximalGooglePerson above.
const QByteArray kMaximalMsContact = QByteArrayLiteral(
    "{"
    "\"id\":\"AAMk-maximal-1\","
    "\"displayName\":\"Grace Hopper\","
    "\"givenName\":\"Grace\","
    "\"surname\":\"Hopper\","
    "\"middleName\":\"Brewster\","
    "\"title\":\"Rear Admiral\","
    "\"generation\":\"Jr.\","
    "\"yomiGivenName\":\"Gureisu\","
    "\"yomiSurname\":\"Hopa\","
    "\"fileAs\":\"Hopper, Grace\","
    "\"nickName\":\"Amazing Grace\","
    "\"emailAddresses\":[{\"address\":\"grace@example.com\",\"name\":\"Grace Hopper\"}],"
    "\"primaryEmailAddress\":{\"address\":\"grace@example.com\",\"name\":\"Grace Hopper\"},"
    "\"homePhones\":[\"555-0100\"],"
    "\"businessPhones\":[\"555-0200\"],"
    "\"mobilePhone\":\"555-0300\","
    "\"imAddresses\":[\"sip:grace@example.com\"],"
    "\"homeAddress\":{\"street\":\"1 Home St\",\"city\":\"Arlington\",\"state\":\"VA\","
    "\"countryOrRegion\":\"US\",\"postalCode\":\"22201\"},"
    "\"businessAddress\":{\"street\":\"1 Work St\",\"city\":\"Arlington\",\"state\":\"VA\","
    "\"countryOrRegion\":\"US\",\"postalCode\":\"22202\"},"
    "\"otherAddress\":{\"street\":\"1 Other St\",\"city\":\"Arlington\",\"state\":\"VA\","
    "\"countryOrRegion\":\"US\",\"postalCode\":\"22203\"},"
    "\"companyName\":\"US Navy\","
    "\"jobTitle\":\"Rear Admiral\","
    "\"department\":\"Computing\","
    "\"officeLocation\":\"Pentagon\","
    "\"profession\":\"Computer Scientist\","
    "\"businessHomePage\":\"https://example.com/grace\","
    "\"assistantName\":\"Assistant Bob\","
    "\"manager\":\"Manager Sam\","
    "\"spouseName\":\"Vincent\","
    "\"children\":[\"Margaret\"],"
    "\"personalNotes\":\"COBOL pioneer.\","
    "\"birthday\":\"1906-12-09T00:00:00Z\","
    "\"categories\":[\"Historical\"],"
    "\"extensions\":[{"
    "\"@odata.type\":\"microsoft.graph.openTypeExtension\","
    "\"extensionName\":\"kalburator.canon\","
    "\"x-canon-gender\":\"{\\\"sex\\\":\\\"female\\\"}\","
    "\"x-canon-anniversary\":\"{\\\"date\\\":\\\"1934-06-01\\\"}\","
    "\"x-canon-significant-dates\":\"[]\","
    "\"x-canon-time-zone\":\"America/New_York\","
    "\"x-canon-languages\":\"[\\\"en\\\"]\","
    "\"x-canon-interests\":\"[\\\"Computing\\\"]\","
    "\"x-canon-skills\":\"[\\\"COBOL\\\"]\","
    "\"x-canon-calendar-urls\":\"[]\","
    "\"x-canon-sip-addresses\":\"[]\","
    "\"x-canon-memberships\":\"[]\","
    "\"x-canon-external-ids\":\"[]\""
    "}]"
    "}");

} // namespace

class TestCalendarKindDispatch : public QObject {
    Q_OBJECT
private slots:
    void vjournalFieldsRoundTrip()
    {
        const auto journal = parseJournal(kJournal);
        QVERIFY(journal);
        QJsonObject obj = journalFieldsToCanon(journal, kJournal);
        obj.insert(QStringLiteral("uid"), journal->uid());

        QCOMPARE(obj.value(QStringLiteral("summary")).toString(),
                 QStringLiteral("Trip notes"));
        QCOMPARE(obj.value(QStringLiteral("description")).toString(),
                 QStringLiteral("Saw the sea"));
        QVERIFY(obj.contains(QStringLiteral("start")));

        const QByteArray out = canonObjectToJournalBytes(obj);
        QVERIFY2(out.contains("VJOURNAL"), "must serialize back to a VJOURNAL");
        const auto outJournal = parseJournal(out);
        QVERIFY(outJournal);
        QCOMPARE(outJournal->summary(),     journal->summary());
        QCOMPARE(outJournal->description(),  journal->description());
        QCOMPARE(outJournal->dtStart().date(), journal->dtStart().date());
    }

    void vtodoSurvivesIcalCanonRoundTrip()   // handoff §6
    {
        using Kalburator::Shape::DomainId;
        using Kalburator::Shape::EncodingId;
        using Kalburator::Shape::Shape;
        const auto regs = makeCalendarRegistries();
        const Shape ical { DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("ical")} };
        const Shape canon{ DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("canon")} };

        const auto toCanon = regs.transformation.compile(ical, canon);
        const auto toIcal  = regs.transformation.compile(canon, ical);
        QVERIFY(toCanon.has_value() && toIcal.has_value());

        const QByteArray canonBytes = toCanon->apply(kVtodo);
        QVERIFY2(!canonBytes.isEmpty(),
                 "VTODO must promote to non-empty canon");
        QCOMPARE(Kalburator::Shape::CanonEnvelope::kind(
                     Kalburator::Shape::CanonEnvelope::parse(canonBytes)),
                 QStringLiteral("vtodo"));

        const QByteArray rt = toIcal->apply(canonBytes);
        QVERIFY2(rt.contains("VTODO"), "VTODO must survive ical->canon->ical");
        QVERIFY2(rt.contains("UID:todo-handoff-1"), "VTODO uid must survive");
        QVERIFY2(rt.contains("Buy milk"), "VTODO summary must survive");
    }

    void vjournalSurvivesIcalCanonRoundTrip()
    {
        using Kalburator::Shape::DomainId;
        using Kalburator::Shape::EncodingId;
        using Kalburator::Shape::Shape;
        const auto regs = makeCalendarRegistries();
        const Shape ical { DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("ical")} };
        const Shape canon{ DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("canon")} };
        const auto toCanon = regs.transformation.compile(ical, canon);
        const auto toIcal  = regs.transformation.compile(canon, ical);
        QVERIFY(toCanon.has_value() && toIcal.has_value());

        const QByteArray canonBytes = toCanon->apply(kJournal);
        QVERIFY2(!canonBytes.isEmpty(), "VJOURNAL must promote to non-empty canon");
        QCOMPARE(Kalburator::Shape::CanonEnvelope::kind(
                     Kalburator::Shape::CanonEnvelope::parse(canonBytes)),
                 QStringLiteral("vjournal"));
        const QByteArray rt = toIcal->apply(canonBytes);
        QVERIFY2(rt.contains("VJOURNAL"), "VJOURNAL must survive round trip");
        QVERIFY2(rt.contains("UID:journal-1"), "VJOURNAL uid must survive");
    }

    // -------------------------------------------------------------------
    // IP.9 / O88 — kind-scoped loss profiles. Before this item, EVERY
    // record demoted through {calendar,canon}->{calendar,ical} (VEVENT,
    // VTODO or VJOURNAL alike) was warned with the single event-shaped
    // canonToIcalLoss() profile, because CalendarStockShapes::edges()
    // registered exactly one LossProfile for this shape pair even though
    // CanonToICalStage::transform() dispatches to three different
    // emitters by kind. Pins that the record's own kind now selects the
    // right profile (TransformationEdge::lossByKind, Pipeline::
    // composedLoss(kind)) — and, just as important, that neither VTODO
    // nor VJOURNAL is warned about a field only an event could carry.
    // -------------------------------------------------------------------

    void vtodoDemoteLossProfileIsVtodoShapedNotEventShaped()
    {
        using Kalburator::Shape::DomainId;
        using Kalburator::Shape::EncodingId;
        using Kalburator::Shape::Shape;
        using Kalburator::Shape::PropertyId;
        using Kalburator::Shape::LossKind;
        const auto regs = makeCalendarRegistries();
        const Shape ical { DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("ical")} };
        const Shape canon{ DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("canon")} };
        const auto toIcal = regs.transformation.compile(canon, ical);
        QVERIFY(toIcal.has_value());

        const auto loss = toIcal->composedLoss(QStringLiteral("vtodo"));
        QVERIFY2(!loss.isLossless(), "vtodo profile must not be empty");

        // O83/O91: the eleven VTODO-shaped drops must be present, Dropped.
        static const char* kVtodoDropped[] = {
            "attachments", "attendees", "classification", "color", "organizer",
            "sequence", "url", "comments", "contacts", "resources", "requestStatus",
        };
        for (const char* id : kVtodoDropped) {
            QVERIFY2(loss.affected.contains(PropertyId{QString::fromLatin1(id)}),
                     qPrintable(QStringLiteral("vtodo profile missing '%1'").arg(QString::fromLatin1(id))));
            QCOMPARE(loss.affected.value(PropertyId{QString::fromLatin1(id)}), LossKind::Dropped);
        }
        // O86: geo's NAME survives but its VALUE is corrupted — Degraded,
        // not Dropped (see canonToVtodoIcalLoss()'s own comment on the fit).
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("geo")}), LossKind::Degraded);

        // The O88 bug, pinned directly: none of canonToIcalLoss()'s
        // event-only vendor keys may leak into the vtodo profile.
        static const char* kEventOnly[] = {
            "onlineMeeting", "eventType", "typedProperties", "locations",
            "guestsCanModify", "guestsCanInviteOthers", "guestsCanSeeOtherGuests",
            "allowNewTimeProposals", "hideAttendees", "locked", "privateCopy",
            "responseRequested", "descriptionHtml", "freeBusyStatus",
        };
        for (const char* id : kEventOnly) {
            QVERIFY2(!loss.affected.contains(PropertyId{QString::fromLatin1(id)}),
                     qPrintable(QStringLiteral("vtodo profile wrongly carries event-only '%1'")
                                    .arg(QString::fromLatin1(id))));
        }
    }

    void vjournalDemoteLossProfileIsVjournalShapedNotEventShaped()
    {
        using Kalburator::Shape::DomainId;
        using Kalburator::Shape::EncodingId;
        using Kalburator::Shape::Shape;
        using Kalburator::Shape::PropertyId;
        using Kalburator::Shape::LossKind;
        const auto regs = makeCalendarRegistries();
        const Shape ical { DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("ical")} };
        const Shape canon{ DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("canon")} };
        const auto toIcal = regs.transformation.compile(canon, ical);
        QVERIFY(toIcal.has_value());

        const auto loss = toIcal->composedLoss(QStringLiteral("vjournal"));
        QVERIFY2(!loss.isLossless(), "vjournal profile must not be empty");

        // O87/O91: the nine VJOURNAL-shaped drops must be present, Dropped
        // ("recurrence" alone covers RRULE/RDATE/EXDATE — invariant 3).
        static const char* kVjournalDropped[] = {
            "attachments", "attendees", "organizer", "relatedTo", "recurrenceId",
            "recurrence", "comments", "contacts", "requestStatus",
        };
        for (const char* id : kVjournalDropped) {
            QVERIFY2(loss.affected.contains(PropertyId{QString::fromLatin1(id)}),
                     qPrintable(QStringLiteral("vjournal profile missing '%1'").arg(QString::fromLatin1(id))));
            QCOMPARE(loss.affected.value(PropertyId{QString::fromLatin1(id)}), LossKind::Dropped);
        }

        // The O88 bug, pinned directly: none of canonToIcalLoss()'s
        // event-only vendor keys may leak into the vjournal profile.
        static const char* kEventOnly[] = {
            "onlineMeeting", "eventType", "typedProperties", "locations",
            "guestsCanModify", "guestsCanInviteOthers", "guestsCanSeeOtherGuests",
            "allowNewTimeProposals", "hideAttendees", "locked", "privateCopy",
            "responseRequested", "descriptionHtml", "freeBusyStatus", "classification",
        };
        for (const char* id : kEventOnly) {
            QVERIFY2(!loss.affected.contains(PropertyId{QString::fromLatin1(id)}),
                     qPrintable(QStringLiteral("vjournal profile wrongly carries event-only '%1'")
                                    .arg(QString::fromLatin1(id))));
        }
    }

    void veventDemoteLossProfileUnchangedByIp9()
    {
        // Regression guard: the default/untagged kind (vevent) must still
        // resolve to canonToIcalLoss() exactly as before IP.9 — lossByKind
        // is an override map, never merged with `loss`.
        using Kalburator::Shape::DomainId;
        using Kalburator::Shape::EncodingId;
        using Kalburator::Shape::Shape;
        const auto regs = makeCalendarRegistries();
        const Shape ical { DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("ical")} };
        const Shape canon{ DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("canon")} };
        const auto toIcal = regs.transformation.compile(canon, ical);
        QVERIFY(toIcal.has_value());

        const auto defaultLoss = toIcal->composedLoss();
        const auto veventLoss  = toIcal->composedLoss(QStringLiteral("vevent"));
        QCOMPARE(defaultLoss.affected, Kalburator::Calendar::canonToIcalLoss().affected);
        QCOMPARE(veventLoss.affected, Kalburator::Calendar::canonToIcalLoss().affected);
    }

    void veventStillRoundTrips()   // regression guard inside the dispatch test
    {
        using Kalburator::Calendar::ICalToCanonStage;
        using Kalburator::Calendar::CanonToICalStage;
        const QByteArray vevent =
            "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//T//EN\r\n"
            "BEGIN:VEVENT\r\nUID:e-1\r\nSUMMARY:Sync\r\n"
            "DTSTART:20260601T090000Z\r\nDTEND:20260601T100000Z\r\n"
            "END:VEVENT\r\nEND:VCALENDAR\r\n";
        ICalToCanonStage fwd; CanonToICalStage rev;
        const QByteArray canon = fwd.transform(vevent);
        QVERIFY(!canon.isEmpty());
        // Absent or "vevent" kind both mean event.
        const QByteArray out = rev.transform(canon);
        QVERIFY2(out.contains("VEVENT"), "VEVENT must still round-trip");
        QVERIFY2(out.contains("UID:e-1"), "VEVENT uid must survive");
    }

    void unknownKindDemotesToEmpty()   // defensive branch (feeds Task 7 guard)
    {
        using Kalburator::Calendar::CanonToICalStage;
        QJsonObject obj;
        obj.insert(QStringLiteral("uid"), QStringLiteral("x-1"));
        QJsonObject canonMeta;
        canonMeta.insert(QStringLiteral("domain"), QStringLiteral("calendar"));
        canonMeta.insert(QStringLiteral("kind"),   QStringLiteral("vfreebusy"));
        obj.insert(QStringLiteral("_canon"), canonMeta);
        const QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Compact);
        CanonToICalStage rev;
        QVERIFY2(rev.transform(bytes).isEmpty(),
                 "unknown kind must demote to empty (guarded loudly by the engine)");
    }

    // -----------------------------------------------------------------------
    // IP.1 — catalogue/emitter coverage gate (O78).
    //
    // Replaces the old catalogueIncludesTodoAndJournalFields(), which
    // hand-listed exactly four union keys (due/completed/percentComplete/
    // relatedTo), was never updated when W3/W4/O74 added three more
    // (seriesSplitOf/completionAnchor/providerExtrasDigest) to the shared
    // VTODO emitter, and stayed green through the entire drift — the drift's
    // own tombstone (PLAN.md IP.1). These slots instead PROMOTE a maximal
    // fixture and compute the emitted top-level key set against the real
    // catalogue at runtime, for every (domain, kind) pair the library can
    // promote — no hand-maintained list on either side.
    //
    // Merges the key sets of a fixture's "master" and "detached exception"
    // forms before the subset check where a kind has mutually-exclusive
    // per-instance shapes (recurrenceId/recurrenceRange vs. a bare RRULE) —
    // the gate only requires each key to appear in SOME promoted instance
    // of the pair, not all of them at once in one physically-valid instance.
    // -----------------------------------------------------------------------

    void calendarCatalogueDeclaresVeventKeys()
    {
        using Kalburator::Calendar::ICalToCanonStage;
        ICalToCanonStage fwd;
        QJsonObject obj = Kalburator::Shape::CanonEnvelope::parse(fwd.transform(kMaximalVevent));
        const QJsonObject exception =
            Kalburator::Shape::CanonEnvelope::parse(fwd.transform(kMaximalVeventException));
        for (auto it = exception.constBegin(); it != exception.constEnd(); ++it)
            obj.insert(it.key(), it.value());
        QVERIFY2(!obj.isEmpty(), "VEVENT must promote to non-empty canon");

        const auto ids = Kalburator::Calendar::calendarCanonPropertyIds();
        Kalburator::TestSupport::verifyCanonKeysDeclared(obj, ids, QStringLiteral("(calendar, vevent)"));
    }

    void calendarCatalogueDeclaresVtodoKeys()
    {
        using Kalburator::Calendar::ICalToCanonStage;
        ICalToCanonStage fwd;
        QJsonObject obj = Kalburator::Shape::CanonEnvelope::parse(fwd.transform(kMaximalVtodo));
        const QJsonObject exception =
            Kalburator::Shape::CanonEnvelope::parse(fwd.transform(kMaximalVtodoException));
        for (auto it = exception.constBegin(); it != exception.constEnd(); ++it)
            obj.insert(it.key(), it.value());
        QVERIFY2(!obj.isEmpty(), "VTODO must promote to non-empty canon");
        // The whole point of this fixture (PLAN.md IP.1 expected result):
        // the shared VTODO emitter must actually have produced the three
        // drifted keys, or this slot would pass for the wrong reason.
        QVERIFY2(obj.contains(QStringLiteral("providerExtrasDigest")),
                 "fixture must exercise providerExtrasDigest (unmapped X-prop present)");
        QVERIFY2(obj.contains(QStringLiteral("seriesSplitOf")),
                 "fixture must exercise seriesSplitOf (X-CANON-SERIES-SPLIT-OF present)");
        QVERIFY2(obj.contains(QStringLiteral("completionAnchor")),
                 "fixture must exercise completionAnchor (X-ORG-REPEATER present)");

        const auto ids = Kalburator::Calendar::calendarCanonPropertyIds();
        // Was RED under IP.1 (O78): the calendar catalogue had never declared
        // providerExtrasDigest/seriesSplitOf/completionAnchor even though
        // {calendar,canon} carries a VTODO through the SAME shared emitter as
        // {todo,canon} (icalcanonstages.cpp:56). IP.2 catalogued the three
        // keys in calendarcanonproperties.cpp and removed the QEXPECT_FAIL.
        Kalburator::TestSupport::verifyCanonKeysDeclared(obj, ids, QStringLiteral("(calendar, vtodo)"));
    }

    void calendarCatalogueDeclaresVjournalKeys()
    {
        const auto journal = parseJournal(kJournal);
        QVERIFY(journal);
        // kJournal (module-level fixture) plus every remaining optional
        // journal field journalFieldsToCanon() can emit.
        const QByteArray maximalJournal =
            "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\n"
            "BEGIN:VJOURNAL\r\n"
            "UID:journal-maximal-1\r\n"
            "SEQUENCE:2\r\n"
            "CREATED:20260101T000000Z\r\n"
            "LAST-MODIFIED:20260102T000000Z\r\n"
            "SUMMARY:Trip notes\r\n"
            "DESCRIPTION:Saw the sea\r\n"
            "DTSTART:20260601T090000Z\r\n"
            "STATUS:FINAL\r\n"
            "CLASS:CONFIDENTIAL\r\n"
            "COLOR:teal\r\n"
            "URL:https://example.com/journal\r\n"
            "CATEGORIES:Travel,Personal\r\n"
            "X-CANON-CUSTOM:extra-value\r\n"
            "END:VJOURNAL\r\nEND:VCALENDAR\r\n";
        const auto jr = parseJournal(maximalJournal);
        QVERIFY(jr);
        const QJsonObject obj = journalFieldsToCanon(jr, maximalJournal);
        QVERIFY2(!obj.isEmpty(), "VJOURNAL must promote to non-empty canon");

        const auto ids = Kalburator::Calendar::calendarCanonPropertyIds();
        Kalburator::TestSupport::verifyCanonKeysDeclared(obj, ids, QStringLiteral("(calendar, vjournal)"));
    }

    void todoCatalogueDeclaresVtodoKeys()
    {
        // Same maximal VTODO fixture as calendarCatalogueDeclaresVtodoKeys(),
        // through the (todo,vtodo)->(todo,canon) edge instead of
        // (calendar,ical)->(calendar,canon) — same shared emitter
        // (vtodocanonstages.cpp:45 also calls todoFieldsToCanon()), but the
        // TODO catalogue already declares all three drifted keys, so this
        // pair is expected GREEN.
        using Kalburator::Todo::VTodoToCanonStage;
        VTodoToCanonStage fwd;
        QJsonObject obj = Kalburator::Shape::CanonEnvelope::parse(fwd.transform(kMaximalVtodo));
        const QJsonObject exception =
            Kalburator::Shape::CanonEnvelope::parse(fwd.transform(kMaximalVtodoException));
        for (auto it = exception.constBegin(); it != exception.constEnd(); ++it)
            obj.insert(it.key(), it.value());
        QVERIFY2(!obj.isEmpty(), "VTODO must promote to non-empty canon");

        const auto ids = Kalburator::Todo::todoCanonPropertyIds();
        Kalburator::TestSupport::verifyCanonKeysDeclared(obj, ids, QStringLiteral("(todo, vtodo)"));
    }

    void contactsCatalogueDeclaresVcardKeys()
    {
        using Kalburator::Contacts::VCard4ToCanonStage;
        VCard4ToCanonStage fwd;
        const QJsonObject obj = Kalburator::Shape::CanonEnvelope::parse(fwd.transform(kMaximalVcard));
        QVERIFY2(!obj.isEmpty(), "vCard4 must promote to non-empty canon");

        const auto ids = Kalburator::Contacts::contactsCanonPropertyIds();
        Kalburator::TestSupport::verifyCanonKeysDeclared(obj, ids, QStringLiteral("(contacts, vcard4)"));
    }

    void contactsCatalogueDeclaresGooglePersonKeys()
    {
        using Kalburator::Contacts::GooglePersonToCanonStage;
        GooglePersonToCanonStage fwd;
        const QJsonObject obj = Kalburator::Shape::CanonEnvelope::parse(fwd.transform(kMaximalGooglePerson));
        QVERIFY2(!obj.isEmpty(), "google-person must promote to non-empty canon");

        const auto ids = Kalburator::Contacts::contactsCanonPropertyIds();
        Kalburator::TestSupport::verifyCanonKeysDeclared(obj, ids, QStringLiteral("(contacts, google-person)"));
    }

    void contactsCatalogueDeclaresMsContactKeys()
    {
        using Kalburator::Contacts::MsContactToCanonStage;
        MsContactToCanonStage fwd;
        const QJsonObject obj = Kalburator::Shape::CanonEnvelope::parse(fwd.transform(kMaximalMsContact));
        QVERIFY2(!obj.isEmpty(), "ms-contact must promote to non-empty canon");

        const auto ids = Kalburator::Contacts::contactsCanonPropertyIds();
        Kalburator::TestSupport::verifyCanonKeysDeclared(obj, ids, QStringLiteral("(contacts, ms-contact)"));
    }
};

QTEST_GUILESS_MAIN(TestCalendarKindDispatch)
#include "tst_calendar_kind_dispatch.moc"
