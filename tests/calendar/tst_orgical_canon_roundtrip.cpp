#include <QTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "canonenvelope.h"
#include "orgicalcanonstages.h"
#include "calendardomaindefinition.h"
#include "calendarstockshapes.h"
#include "shaperegistries.h"
#include "lossprofile.h"

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/RecurrenceRule>

using Kalburator::Shape::CanonEnvelope::parse;
using Kalburator::Calendar::CanonToOrgICalStage;
using Kalburator::Calendar::OrgICalToCanonStage;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::Shape;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::LossKind;

namespace {

KCalendarCore::Event::Ptr parseEvent(const QByteArray &bytes)
{
    KCalendarCore::ICalFormat fmt;
    auto inc = fmt.fromString(QString::fromUtf8(bytes));
    return inc.dynamicCast<KCalendarCore::Event>();
}

/// Build a minimal canon JSON with the given recurrence array.
QByteArray makeCanon(const QStringList &recurrenceLines,
                     const QString &uid = QStringLiteral("test-uid-orgical"))
{
    QJsonObject obj;
    obj.insert(QStringLiteral("uid"),     uid);
    obj.insert(QStringLiteral("summary"), QStringLiteral("Test Event"));

    QJsonObject startObj;
    startObj.insert(QStringLiteral("dateTime"), QStringLiteral("2026-06-01T09:00:00Z"));
    startObj.insert(QStringLiteral("floating"),  false);
    obj.insert(QStringLiteral("start"), startObj);

    if (!recurrenceLines.isEmpty()) {
        QJsonArray arr;
        for (const QString &line : recurrenceLines)
            arr.append(line);
        obj.insert(QStringLiteral("recurrence"), arr);
    }

    QJsonObject canonMeta;
    canonMeta.insert(QStringLiteral("domain"), QStringLiteral("calendar"));
    obj.insert(QStringLiteral("_canon"), canonMeta);

    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

// Build a ShapeRegistries with the calendar domain fully registered.
Kalburator::Shape::ShapeRegistries makeOrgIcalRegistries()
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

} // namespace

class TestOrgICalCanonRoundtrip : public QObject {
    Q_OBJECT
private slots:

    /// canon → org-ical: a complex weekly RRULE (BYDAY=MO,WE,FR) must be
    /// reduced to a basic weekly rule (no byDays), and the original must
    /// be stashed in X-ORIGINAL-RRULE.
    void canonToOrgIcalSimplifiesComplexRecurrence()
    {
        const QByteArray canon = makeCanon(
            {QStringLiteral("RRULE:FREQ=WEEKLY;BYDAY=MO,WE,FR")});

        CanonToOrgICalStage stage;
        const QByteArray orgIcal = stage.transform(canon);
        QVERIFY2(!orgIcal.isEmpty(), "CanonToOrgICalStage returned empty bytes");

        // Parse the output iCal via KCalendarCore
        const auto event = parseEvent(orgIcal);
        QVERIFY2(event, "Failed to parse org-ical output as VEVENT");

        // The simplified recurrence must be a basic weekly rule
        const auto *rec = event->recurrence();
        QVERIFY2(rec, "Event has no recurrence object");

        const auto rules = rec->rRules();
        QVERIFY2(!rules.isEmpty(), "Simplified org-ical must still have an RRULE");

        // The simplified rule must have EMPTY byDays (no BYDAY)
        const auto *primaryRule = rules.first();
        QVERIFY2(primaryRule->byDays().isEmpty(),
                 "Simplified RRULE must have empty byDays (BYDAY removed)");

        // The simplified rule must be weekly
        QCOMPARE(primaryRule->recurrenceType(),
                 KCalendarCore::RecurrenceRule::rWeekly);

        // The output iCal must contain X-ORIGINAL-RRULE stashing the original
        QVERIFY2(orgIcal.contains("X-ORIGINAL-RRULE"),
                 "org-ical output must contain X-ORIGINAL-RRULE with the original rule");

        // The stashed value must contain the BYDAY token
        QVERIFY2(orgIcal.contains("BYDAY=MO"),
                 "X-ORIGINAL-RRULE must carry the original BYDAY=MO,WE,FR tokens");
    }

    /// canon → org-ical → canon: the restored canon must semantically
    /// reproduce the original complex RRULE (FREQ=WEEKLY with MO,WE,FR).
    /// We assert on distinguishing tokens rather than byte-identity because
    /// KCalendarCore may reorder day abbreviations or normalize the rule
    /// when it re-serializes after restoring the X-ORIGINAL-RRULE.
    void orgIcalRoundTripRestoresComplexRecurrence()
    {
        const QByteArray canon = makeCanon(
            {QStringLiteral("RRULE:FREQ=WEEKLY;BYDAY=MO,WE,FR")});

        CanonToOrgICalStage fwd;
        OrgICalToCanonStage rev;

        const QByteArray orgIcal     = fwd.transform(canon);
        QVERIFY2(!orgIcal.isEmpty(), "CanonToOrgICalStage returned empty");

        const QByteArray restoredCanon = rev.transform(orgIcal);
        QVERIFY2(!restoredCanon.isEmpty(), "OrgICalToCanonStage returned empty");

        const QJsonObject obj = parse(restoredCanon);
        QVERIFY2(!obj.isEmpty(), "Restored canon is not valid JSON envelope");

        // The restored recurrence array must contain an RRULE with FREQ=WEEKLY
        // and BYDAY including MO, WE, FR.
        const QJsonArray recArr = obj.value(QStringLiteral("recurrence")).toArray();
        QVERIFY2(!recArr.isEmpty(), "Restored canon must have a recurrence array");

        bool foundFreqWeekly = false;
        bool foundByDayMO    = false;
        bool foundByDayWE    = false;
        bool foundByDayFR    = false;

        for (const auto &rv : recArr) {
            const QString line = rv.toString();
            if (line.contains(QStringLiteral("FREQ=WEEKLY")))
                foundFreqWeekly = true;
            if (line.contains(QStringLiteral("MO")))
                foundByDayMO = true;
            if (line.contains(QStringLiteral("WE")))
                foundByDayWE = true;
            if (line.contains(QStringLiteral("FR")))
                foundByDayFR = true;
        }

        QVERIFY2(foundFreqWeekly, "Restored RRULE must contain FREQ=WEEKLY");
        QVERIFY2(foundByDayMO,    "Restored RRULE must contain MO in BYDAY");
        QVERIFY2(foundByDayWE,    "Restored RRULE must contain WE in BYDAY");
        QVERIFY2(foundByDayFR,    "Restored RRULE must contain FR in BYDAY");
    }

    /// canon → org-ical: a simple daily recurrence (INTERVAL=2) must pass
    /// through unchanged — no X-ORIGINAL-RRULE stashed, rule intact.
    void canonToOrgIcalLeavesSimpleRecurrenceUnchanged()
    {
        const QByteArray canon = makeCanon(
            {QStringLiteral("RRULE:FREQ=DAILY;INTERVAL=2")});

        CanonToOrgICalStage stage;
        const QByteArray orgIcal = stage.transform(canon);
        QVERIFY2(!orgIcal.isEmpty(), "CanonToOrgICalStage returned empty bytes");

        // Simple recurrence must NOT trigger X-ORIGINAL-RRULE
        QVERIFY2(!orgIcal.contains("X-ORIGINAL-RRULE"),
                 "Simple recurrence must not produce X-ORIGINAL-RRULE");

        // Parse and verify the rule is still daily with interval 2
        const auto event = parseEvent(orgIcal);
        QVERIFY2(event, "Failed to parse org-ical output as VEVENT");

        const auto *rec = event->recurrence();
        QVERIFY2(rec, "Event has no recurrence object");

        const auto rules = rec->rRules();
        QVERIFY2(!rules.isEmpty(), "Simple org-ical must still have an RRULE");

        const auto *rule = rules.first();
        QCOMPARE(rule->recurrenceType(), KCalendarCore::RecurrenceRule::rDaily);
        QCOMPARE(rule->frequency(), 2);
    }

    // Edge routing tests (Task 3)

    /// The shape router must be able to compile a canon → org-ical pipeline
    /// directly (single registered edge, not via a two-hop path).
    void canonRoutesToOrgIcalDirectly()
    {
        const auto registries = makeOrgIcalRegistries();
        const Shape canon  { DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("canon")} };
        const Shape orgIcal{ DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("org-ical")} };

        const auto pipeline = registries.transformation.compile(canon, orgIcal);
        QVERIFY2(pipeline.has_value(),
                 "compile(canon, org-ical) must succeed — direct edge registered");
    }

    /// The loss profile for canon → org-ical must charge recurrence as Simplified
    /// (invariant 4: complex RRULE is reduced, not dropped).
    void canonToOrgIcalLossChargesRecurrenceSimplified()
    {
        const auto registries = makeOrgIcalRegistries();
        const Shape canon  { DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("canon")} };
        const Shape orgIcal{ DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("org-ical")} };

        const auto loss = registries.transformation.inspect(canon, orgIcal);
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("recurrence")}),
                 LossKind::Simplified);
    }
};

QTEST_GUILESS_MAIN(TestOrgICalCanonRoundtrip)
#include "tst_orgical_canon_roundtrip.moc"
