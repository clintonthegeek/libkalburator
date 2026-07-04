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

    void catalogueIncludesTodoAndJournalFields()
    {
        const auto ids = Kalburator::Calendar::calendarCanonPropertyIds();
        const auto has = [&](const char* k){
            return ids.contains(Kalburator::Shape::PropertyId{QString::fromLatin1(k)});
        };
        QVERIFY2(has("due"),             "catalogue must include todo 'due'");
        QVERIFY2(has("completed"),       "catalogue must include todo 'completed'");
        QVERIFY2(has("percentComplete"), "catalogue must include todo 'percentComplete'");
        QVERIFY2(has("relatedTo"),       "catalogue must include todo 'relatedTo'");
    }
};

QTEST_GUILESS_MAIN(TestCalendarKindDispatch)
#include "tst_calendar_kind_dispatch.moc"
