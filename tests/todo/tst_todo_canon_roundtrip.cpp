#include <QTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include "canonenvelope.h"
#include "vtodocanonstages.h"
#include "tododomaindefinition.h"
#include "todostockshapes.h"
#include "shaperegistries.h"
#include "lossprofile.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Todo>

using Kalburator::Shape::CanonEnvelope::parse;
using Kalburator::Todo::VTodoToCanonStage;
using Kalburator::Todo::CanonToVTodoStage;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::Shape;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::LossKind;

namespace {

// Build a ShapeRegistries with the todo domain fully registered
// (DomainDefinition canonical spine + StockShapes peers + edges).
Kalburator::Shape::ShapeRegistries makeTodoRegistries()
{
    Kalburator::Shape::ShapeRegistries regs;
    auto& reg = regs.transformation;

    Kalburator::Todo::TodoDomainDefinition def;
    // Build the versioned spine (ical-vtodo → canon) as PluginManager would.
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

    Kalburator::Todo::TodoStockShapes shapes;
    for (const auto& [shape, cat] : shapes.peerShapes())
        reg.registerShape(shape, cat);
    for (const auto& edge : shapes.edges())
        reg.registerEdge(edge);

    return regs;
}

const QByteArray kTestVTodo =
    "BEGIN:VCALENDAR\r\n"
    "VERSION:2.0\r\n"
    "PRODID:-//Test//Test//EN\r\n"
    "BEGIN:VTODO\r\n"
    "UID:test-todo-uid-12345\r\n"
    "SUMMARY:Buy groceries\r\n"
    "DESCRIPTION:Get milk and bread\r\n"
    "STATUS:NEEDS-ACTION\r\n"
    "PRIORITY:3\r\n"
    "PERCENT-COMPLETE:25\r\n"
    "DUE:20260601T120000Z\r\n"
    "CATEGORIES:Shopping,Errands\r\n"
    "END:VTODO\r\n"
    "END:VCALENDAR\r\n";

const QByteArray kTestVTodoWithRecurrence =
    "BEGIN:VCALENDAR\r\n"
    "VERSION:2.0\r\n"
    "PRODID:-//Test//Test//EN\r\n"
    "BEGIN:VTODO\r\n"
    "UID:test-recur-uid-001\r\n"
    "SUMMARY:Weekly review\r\n"
    "RRULE:FREQ=WEEKLY;BYDAY=MO\r\n"
    "EXDATE:20260601T000000Z\r\n"
    "END:VTODO\r\n"
    "END:VCALENDAR\r\n";

KCalendarCore::Todo::Ptr parseTodoFromICal(const QByteArray &bytes)
{
    KCalendarCore::ICalFormat fmt;
    auto inc = fmt.fromString(QString::fromUtf8(bytes));
    return inc.dynamicCast<KCalendarCore::Todo>();
}

// Isolate just the VTODO component's own lines, excluding any VTIMEZONE.
// KCalendarCore's own serializer regenerates a full VTIMEZONE (with
// legitimate STANDARD/DAYLIGHT RRULEs) for any TZID-based todo — that is
// correct iCal and must not be mistaken for recurrence contamination.
QByteArray todoComponentOf(const QByteArray &ical)
{
    const int b = ical.indexOf("BEGIN:VTODO");
    if (b < 0) return {};
    const int e = ical.indexOf("END:VTODO", b);
    if (e < 0) return {};
    return ical.mid(b, e - b);
}

// N1 regression fixture — a non-recurring VTODO with a TZID DUE carries a
// full VTIMEZONE block whose STANDARD/DAYLIGHT sub-components have their own
// RRULE. A whole-blob line scan would harvest those as the todo's recurrence.
const QByteArray kTestVTodoWithVtimezoneNoOwnRecurrence =
    "BEGIN:VCALENDAR\r\n"
    "VERSION:2.0\r\n"
    "PRODID:-//Test//Test//EN\r\n"
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
    "END:VTIMEZONE\r\n"
    "BEGIN:VTODO\r\n"
    "UID:tz-todo-one-off@example.com\r\n"
    "SUMMARY:One-off task\r\n"
    "DUE;TZID=America/New_York:20260615T170000\r\n"
    "END:VTODO\r\n"
    "END:VCALENDAR\r\n";

} // namespace

class TestTodoCanonRoundtrip : public QObject {
    Q_OBJECT
private slots:

    void vtodoToCanonExtractsCoreFields()
    {
        VTodoToCanonStage stage;
        const QByteArray out = stage.transform(kTestVTodo);
        QVERIFY2(!out.isEmpty(), "VTodoToCanonStage returned empty bytes");

        const QJsonObject obj = parse(out);
        QVERIFY2(!obj.isEmpty(), "Canon JSON output is empty object");

        // uid must be present and match the VTODO UID
        const QString uid = obj.value(QStringLiteral("uid")).toString();
        QVERIFY2(!uid.isEmpty(), "uid must be present in canon output");
        QCOMPARE(uid, QStringLiteral("test-todo-uid-12345"));

        // _canon envelope must be stamped with domain = "todo"
        const QJsonObject canon = obj.value(QStringLiteral("_canon")).toObject();
        QCOMPARE(canon.value(QStringLiteral("domain")).toString(),
                 QStringLiteral("todo"));

        // summary and description
        QCOMPARE(obj.value(QStringLiteral("summary")).toString(),
                 QStringLiteral("Buy groceries"));
        QCOMPARE(obj.value(QStringLiteral("description")).toString(),
                 QStringLiteral("Get milk and bread"));

        // status
        QCOMPARE(obj.value(QStringLiteral("status")).toString(),
                 QStringLiteral("needsAction"));

        // priority
        QCOMPARE(obj.value(QStringLiteral("priority")).toInt(), 3);

        // percentComplete
        QCOMPARE(obj.value(QStringLiteral("percentComplete")).toInt(), 25);

        // due (present as Json object)
        QVERIFY(obj.contains(QStringLiteral("due")));

        // categories
        const QJsonArray cats = obj.value(QStringLiteral("categories")).toArray();
        QStringList catList;
        for (const auto& c : cats)
            catList << c.toString();
        QVERIFY2(catList.contains(QStringLiteral("Shopping")),
                 "categories must contain 'Shopping'");
        QVERIFY2(catList.contains(QStringLiteral("Errands")),
                 "categories must contain 'Errands'");
    }

    void vtodoToCanonEmptyInputReturnsEmpty()
    {
        VTodoToCanonStage stage;
        QVERIFY(stage.transform(QByteArray{}).isEmpty());
    }

    void vtodoRoundTripPreservesCoreFields()
    {
        VTodoToCanonStage  fwd;
        CanonToVTodoStage  rev;

        const QByteArray canon  = fwd.transform(kTestVTodo);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");

        const QByteArray output = rev.transform(canon);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty");

        // Parse both and compare core fields via KCalendarCore getters.
        const auto origTodo = parseTodoFromICal(kTestVTodo);
        const auto outTodo  = parseTodoFromICal(output);
        QVERIFY2(origTodo, "could not parse original VTODO");
        QVERIFY2(outTodo,  "could not parse output VTODO");

        QCOMPARE(outTodo->summary(),         origTodo->summary());
        QCOMPARE(outTodo->description(),     origTodo->description());
        QCOMPARE(outTodo->priority(),        origTodo->priority());
        QCOMPARE(outTodo->percentComplete(), origTodo->percentComplete());
        QCOMPARE(outTodo->categories(),      origTodo->categories());

        // due date must survive (compare date component — time may round to UTC)
        if (origTodo->hasDueDate()) {
            QVERIFY2(outTodo->hasDueDate(), "due date must survive round-trip");
            QCOMPARE(outTodo->dtDue().date(), origTodo->dtDue().date());
        }
    }

    void vtodoRoundTripPreservesRecurrenceLines()
    {
        VTodoToCanonStage  fwd;
        CanonToVTodoStage  rev;

        const QByteArray canon  = fwd.transform(kTestVTodoWithRecurrence);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");

        // Canon must have recurrence array
        const QJsonObject obj = parse(canon);
        const QJsonArray recArr = obj.value(QStringLiteral("recurrence")).toArray();
        QVERIFY2(!recArr.isEmpty(), "recurrence must be captured in canon");

        // Collect the verbatim recurrence lines the forward stage captured.
        // Each entry in recArr is already stripped of CRLF (trimmed by extractRecurrenceLines).
        QStringList capturedLines;
        for (const auto& rv : recArr)
            capturedLines << rv.toString();

        // We expect both RRULE and EXDATE to be captured.
        const QString expectedRRule  = QStringLiteral("RRULE:FREQ=WEEKLY;BYDAY=MO");
        const QString expectedExdate = QStringLiteral("EXDATE:20260601T000000Z");
        QVERIFY2(capturedLines.contains(expectedRRule),
                 "RRULE:FREQ=WEEKLY;BYDAY=MO must be captured verbatim in recurrence array");
        QVERIFY2(capturedLines.contains(expectedExdate),
                 "EXDATE:20260601T000000Z must be captured verbatim in recurrence array");

        const QByteArray output = rev.transform(canon);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty");

        // Extract recurrence lines from the round-tripped output (strip CRLF → trimmed).
        const auto outputText = QString::fromUtf8(output);
        const auto outputLines = outputText.split(QLatin1Char('\n'));
        QStringList outputRecLines;
        for (const QString &raw : outputLines) {
            const QString line = raw.trimmed();
            if (line.startsWith(QStringLiteral("RRULE:"))  ||
                line.startsWith(QStringLiteral("RDATE:"))  ||
                line.startsWith(QStringLiteral("EXDATE:")))
                outputRecLines << line;
        }

        // Byte-identical check: every verbatim line captured in canon must appear
        // unchanged in the output (invariants 3 and 5).
        for (const QString &line : capturedLines) {
            QVERIFY2(outputRecLines.contains(line),
                     qPrintable(QStringLiteral(
                         "Recurrence line not found byte-identically in output: ") + line));
        }
    }

    void vtodoRelatedToPreserved()
    {
        const QByteArray vtodo =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VTODO\r\n"
            "UID:child-uid\r\n"
            "SUMMARY:Child task\r\n"
            "RELATED-TO:parent-uid\r\n"
            "END:VTODO\r\n"
            "END:VCALENDAR\r\n";

        VTodoToCanonStage fwd;
        const QByteArray canon = fwd.transform(vtodo);
        QVERIFY(!canon.isEmpty());

        const QJsonObject obj = parse(canon);
        const QJsonArray rels = obj.value(QStringLiteral("relatedTo")).toArray();
        QVERIFY2(!rels.isEmpty(), "relatedTo must be captured from RELATED-TO");
        QCOMPARE(rels.at(0).toObject().value(QStringLiteral("uid")).toString(),
                 QStringLiteral("parent-uid"));

        // Round-trip: RELATED-TO must survive
        CanonToVTodoStage rev;
        const QByteArray output = rev.transform(canon);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty");
        const auto outTodo = parseTodoFromICal(output);
        QVERIFY2(outTodo, "could not parse output VTODO");
        QCOMPARE(outTodo->relatedTo(KCalendarCore::Incidence::RelTypeParent),
                 QStringLiteral("parent-uid"));
    }

    // Edge + spine routing tests (Task B5)

    void vtodoRoutesToCanonDirectly()
    {
        // With spine=[ical-vtodo, canon] and edge ical-vtodo→canon, compile must succeed.
        // (Since ical-vtodo is the root spine node, this is a 1-hop: peer→spine[0]→...)
        // Actually ical-vtodo IS spine[0]; compile(ical-vtodo, canon) = direct lead edge.
        const auto regs = makeTodoRegistries();
        const Shape vtodo{ DomainId{QStringLiteral("todo")}, EncodingId{QStringLiteral("ical-vtodo")} };
        const Shape canon{ DomainId{QStringLiteral("todo")}, EncodingId{QStringLiteral("canon")} };

        const auto pipeline = regs.transformation.compile(vtodo, canon);
        QVERIFY2(pipeline.has_value(),
                 "compile(ical-vtodo, canon) must succeed");
    }

    void canonToVtodoLossProfileChargesDroppedAndReversible()
    {
        const auto regs = makeTodoRegistries();
        const Shape canon{ DomainId{QStringLiteral("todo")}, EncodingId{QStringLiteral("canon")} };
        const Shape vtodo{ DomainId{QStringLiteral("todo")}, EncodingId{QStringLiteral("ical-vtodo")} };

        const auto loss = regs.transformation.inspect(canon, vtodo);
        QVERIFY2(!loss.isLossless(),
                 "canon->ical-vtodo must be lossy");

        // linkedResources: Dropped
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("linkedResources")}),
                 LossKind::Dropped);

        // descriptionHtml: Reversible (→ X-ALT-DESC)
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("descriptionHtml")}),
                 LossKind::Reversible);

        // checklistItems: Reversible
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("checklistItems")}),
                 LossKind::Reversible);
    }

    // N1: a non-recurring VTODO with a TZID DUE must not gain the
    // VTIMEZONE's DST-transition rules as its own recurrence.
    void vtimezoneRecurrenceDoesNotContaminateNonRecurringTodo()
    {
        VTodoToCanonStage fwd;
        CanonToVTodoStage rev;

        const QByteArray canon = fwd.transform(kTestVTodoWithVtimezoneNoOwnRecurrence);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");

        const QJsonObject obj = parse(canon);
        const QJsonArray recArr = obj.value(QStringLiteral("recurrence")).toArray();
        QVERIFY2(recArr.isEmpty(),
                 qPrintable(QStringLiteral("recurrence must be empty for a non-recurring todo; got: %1")
                     .arg(QString::fromUtf8(QJsonDocument(recArr).toJson(QJsonDocument::Compact)))));

        const QByteArray output = rev.transform(canon);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty");

        // Scope to the VTODO's own component — KCalendarCore legitimately
        // regenerates a VTIMEZONE (with its own RRULEs) for a TZID todo;
        // only the todo's own lines matter for this assertion.
        const QByteArray todoBlock = todoComponentOf(output);
        QVERIFY2(!todoBlock.isEmpty(), "output must contain a VTODO component");
        QVERIFY2(!todoBlock.contains("RRULE:"),  "the todo's own component must contain zero RRULE lines");
        QVERIFY2(!todoBlock.contains("RDATE:"),  "the todo's own component must contain zero RDATE lines");
        QVERIFY2(!todoBlock.contains("EXDATE:"), "the todo's own component must contain zero EXDATE lines");

        const auto outTodo = parseTodoFromICal(output);
        QVERIFY2(outTodo, "output must parse as a valid VTODO via KCalendarCore");
        QVERIFY2(!outTodo->recurs(), "output todo must not recur");
    }
    // VP.c-step-1a — detached-exception identity: RECURRENCE-ID must survive
    // promote → demote byte-equivalently (UTC DATE-TIME form, mirroring the
    // event path's recurrenceId canon object).
    void vtodoRoundTripPreservesRecurrenceId()
    {
        const QByteArray vtodo =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VTODO\r\n"
            "UID:series-todo-1\r\n"
            "SUMMARY:Moved instance\r\n"
            "RECURRENCE-ID:20260602T090000Z\r\n"
            "DUE:20260602T170000Z\r\n"
            "END:VTODO\r\n"
            "END:VCALENDAR\r\n";

        VTodoToCanonStage fwd;
        const QByteArray canon = fwd.transform(vtodo);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");

        const QJsonObject obj = parse(canon);
        const QJsonObject recIdObj = obj.value(QStringLiteral("recurrenceId")).toObject();
        QCOMPARE(recIdObj.value(QStringLiteral("dateTime")).toString(),
                 QStringLiteral("2026-06-02T09:00:00Z"));

        CanonToVTodoStage rev;
        const QByteArray output = rev.transform(canon);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty");

        // Byte-equivalent RECURRENCE-ID line in the output.
        QVERIFY2(output.contains("RECURRENCE-ID:20260602T090000Z"),
                 qPrintable(QStringLiteral(
                     "demoted bytes must carry RECURRENCE-ID:20260602T090000Z; got:\n")
                     + QString::fromUtf8(output)));

        // And it re-parses as a real exception identity.
        const auto outTodo = parseTodoFromICal(output);
        QVERIFY2(outTodo, "could not parse output VTODO");
        QVERIFY(outTodo->hasRecurrenceId());
        QCOMPARE(outTodo->recurrenceId().toUTC(),
                 QDateTime::fromString(QStringLiteral("2026-06-02T09:00:00Z"), Qt::ISODate));
        QVERIFY(!outTodo->thisAndFuture());
    }

    // RANGE=THISANDFUTURE rides along as the recurrenceRange canon string
    // (mirrors the event path) and re-emits as RANGE=THISANDFUTURE.
    void vtodoRoundTripPreservesThisAndFutureRange()
    {
        const QByteArray vtodo =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VTODO\r\n"
            "UID:series-todo-2\r\n"
            "SUMMARY:this-and-future instance\r\n"
            "RECURRENCE-ID;RANGE=THISANDFUTURE:20260602T090000Z\r\n"
            "DUE:20260602T170000Z\r\n"
            "END:VTODO\r\n"
            "END:VCALENDAR\r\n";

        VTodoToCanonStage fwd;
        const QByteArray canon = fwd.transform(vtodo);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");

        const QJsonObject obj = parse(canon);
        QCOMPARE(obj.value(QStringLiteral("recurrenceRange")).toString(),
                 QStringLiteral("thisAndFuture"));

        CanonToVTodoStage rev;
        const QByteArray output = rev.transform(canon);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty");
        QVERIFY2(output.contains("RANGE=THISANDFUTURE"),
                 qPrintable(QStringLiteral(
                     "demoted bytes must carry RANGE=THISANDFUTURE; got:\n")
                     + QString::fromUtf8(output)));

        const auto outTodo = parseTodoFromICal(output);
        QVERIFY2(outTodo, "could not parse output VTODO");
        QVERIFY(outTodo->hasRecurrenceId());
        QVERIFY(outTodo->thisAndFuture());
    }

    // A master VTODO (no RECURRENCE-ID) must NOT gain a recurrenceId key.
    void vtodoMasterHasNoRecurrenceId()
    {
        VTodoToCanonStage fwd;
        const QByteArray canon = fwd.transform(kTestVTodoWithRecurrence);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QJsonObject obj = parse(canon);
        QVERIFY2(!obj.contains(QStringLiteral("recurrenceId")),
                 "master todo must not carry a recurrenceId key");
    }
};

QTEST_GUILESS_MAIN(TestTodoCanonRoundtrip)
#include "tst_todo_canon_roundtrip.moc"
