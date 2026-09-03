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
using Kalburator::Shape::CanonEnvelope::serialize;
using Kalburator::Shape::CanonEnvelope::providerExtrasKey;
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

        // providerExtrasDigest (O74): Dropped — derived/meta, no wire form.
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("providerExtrasDigest")}),
                 LossKind::Dropped);
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

    // W3 (VP.e): RANGE=THISANDFUTURE is write-hostile on real CalDAV
    // servers — the library NEVER re-emits it, unconditionally, even
    // though canon still losslessly CAPTURES an incoming
    // RANGE=THISANDFUTURE on promote (a foreign producer's existing write,
    // read-side only — see vtodocanonfields.cpp's recurrenceId/
    // recurrenceRange demote block). This replaces
    // vtodoRoundTripPreservesThisAndFutureRange, which pinned the OPPOSITE
    // (pre-W3) behavior — see the W3 recon handoff "headline finding" and
    // the series-split contract doc.
    void vtodoDemoteNeverEmitsThisAndFutureRange()
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

        // Promote is unchanged: canon still captures the incoming RANGE.
        const QJsonObject obj = parse(canon);
        QCOMPARE(obj.value(QStringLiteral("recurrenceRange")).toString(),
                 QStringLiteral("thisAndFuture"));

        CanonToVTodoStage rev;
        const QByteArray output = rev.transform(canon);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty");
        QVERIFY2(!output.contains("RANGE=THISANDFUTURE"),
                 qPrintable(QStringLiteral(
                     "demoted bytes must NEVER carry RANGE=THISANDFUTURE "
                     "(write-hostile on real servers); got:\n")
                     + QString::fromUtf8(output)));

        const auto outTodo = parseTodoFromICal(output);
        QVERIFY2(outTodo, "could not parse output VTODO");
        // The bare exception identity survives — only the RANGE modifier
        // is stripped (the new Degraded loss-profile entry for
        // recurrenceRange; recurrenceId itself stays Reversible).
        QVERIFY2(outTodo->hasRecurrenceId(),
                 "the bare RECURRENCE-ID exception identity must survive");
        QVERIFY2(!outTodo->thisAndFuture(),
                 "thisAndFuture() must be false on the demoted todo");
    }

    // W3: seriesSplitOf rides X-CANON-SERIES-SPLIT-OF (Reversible carrier)
    // — mirrors the completionAnchor/X-ORG-REPEATER pattern (W4).
    void vtodoRoundTripPreservesSeriesSplitOf()
    {
        const QByteArray vtodo =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VTODO\r\n"
            "UID:weekly-series-1-split-20260601T090000Z\r\n"
            "SUMMARY:Weekly review\r\n"
            "X-CANON-SERIES-SPLIT-OF:weekly-series-1\r\n"
            "END:VTODO\r\n"
            "END:VCALENDAR\r\n";

        VTodoToCanonStage fwd;
        const QByteArray canon = fwd.transform(vtodo);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");

        const QJsonObject obj = parse(canon);
        QCOMPARE(obj.value(QStringLiteral("seriesSplitOf")).toString(),
                 QStringLiteral("weekly-series-1"));

        CanonToVTodoStage rev;
        const QByteArray output = rev.transform(canon);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty");

        const auto outTodo = parseTodoFromICal(output);
        QVERIFY2(outTodo, "could not parse output VTODO");
        QCOMPARE(outTodo->nonKDECustomProperty("X-CANON-SERIES-SPLIT-OF"),
                 QStringLiteral("weekly-series-1"));
    }

    // Loss-profile pin: seriesSplitOf is Reversible on canon → vtodo.
    void canonToVtodoLossProfileChargesSeriesSplitOfReversible()
    {
        const auto regs = makeTodoRegistries();
        const Shape canon{ DomainId{QStringLiteral("todo")}, EncodingId{QStringLiteral("canon")} };
        const Shape vtodo{ DomainId{QStringLiteral("todo")}, EncodingId{QStringLiteral("ical-vtodo")} };

        const auto loss = regs.transformation.inspect(canon, vtodo);
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("seriesSplitOf")}),
                 LossKind::Reversible);
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("recurrenceRange")}),
                 LossKind::Degraded);
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

    // ---- W4: completion-anchored recurrence --------------------------------

    // Promote: a generic X-ORG-REPEATER custom prop carrying a Restart
    // marker (".+1w") derives the catalogued completionAnchor key.
    void vtodoPromotesRestartCompletionAnchorFromOrgRepeaterMarker()
    {
        const QByteArray vtodo =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VTODO\r\n"
            "UID:org-repeater-restart-1\r\n"
            "SUMMARY:Water plants\r\n"
            "COMPLETED:20260608T090000Z\r\n"
            "X-ORG-REPEATER:.+1w\r\n"
            "END:VTODO\r\n"
            "END:VCALENDAR\r\n";

        VTodoToCanonStage fwd;
        const QByteArray canon = fwd.transform(vtodo);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");

        const QJsonObject obj = parse(canon);
        const QJsonObject anchor = obj.value(QStringLiteral("completionAnchor")).toObject();
        QVERIFY2(!anchor.isEmpty(), "completionAnchor must be derived from X-ORG-REPEATER");
        QCOMPARE(anchor.value(QStringLiteral("type")).toString(), QStringLiteral("restart"));
        QCOMPARE(anchor.value(QStringLiteral("interval")).toInt(), 1);
        QCOMPARE(anchor.value(QStringLiteral("unit")).toString(), QStringLiteral("w"));

        // The verbatim marker itself rides providerExtras["x-vtodo"] via the
        // generic custom-prop channel (no extra machinery needed for it).
        const QJsonObject extras = obj.value(QStringLiteral("providerExtras")).toObject();
        const QJsonObject xvtodo = extras.value(QStringLiteral("x-vtodo")).toObject();
        QCOMPARE(xvtodo.value(QStringLiteral("X-ORG-REPEATER")).toString(),
                 QStringLiteral(".+1w"));
    }

    // Promote: a CatchUp marker ("++2d") maps to type "catchUp".
    void vtodoPromotesCatchUpCompletionAnchorFromOrgRepeaterMarker()
    {
        const QByteArray vtodo =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VTODO\r\n"
            "UID:org-repeater-catchup-1\r\n"
            "SUMMARY:Check backups\r\n"
            "COMPLETED:20260610T080000Z\r\n"
            "X-ORG-REPEATER:++2d\r\n"
            "END:VTODO\r\n"
            "END:VCALENDAR\r\n";

        VTodoToCanonStage fwd;
        const QByteArray canon = fwd.transform(vtodo);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");

        const QJsonObject obj = parse(canon);
        const QJsonObject anchor = obj.value(QStringLiteral("completionAnchor")).toObject();
        QVERIFY2(!anchor.isEmpty(), "completionAnchor must be derived from X-ORG-REPEATER");
        QCOMPARE(anchor.value(QStringLiteral("type")).toString(), QStringLiteral("catchUp"));
        QCOMPARE(anchor.value(QStringLiteral("interval")).toInt(), 2);
        QCOMPARE(anchor.value(QStringLiteral("unit")).toString(), QStringLiteral("d"));
    }

    // A bare '+' marker is Cumulative (out of W4 scope) — must NOT derive a
    // completionAnchor.
    void vtodoBareCumulativeRepeaterDoesNotDeriveCompletionAnchor()
    {
        const QByteArray vtodo =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VTODO\r\n"
            "UID:org-repeater-cumulative-1\r\n"
            "SUMMARY:Cumulative task\r\n"
            "X-ORG-REPEATER:+1w\r\n"
            "END:VTODO\r\n"
            "END:VCALENDAR\r\n";

        VTodoToCanonStage fwd;
        const QByteArray canon = fwd.transform(vtodo);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QJsonObject obj = parse(canon);
        QVERIFY2(!obj.contains(QStringLiteral("completionAnchor")),
                 "a bare '+' (Cumulative) marker must not derive completionAnchor (W4 scope)");
    }

    // Promote: no X-ORG-REPEATER at all → no completionAnchor key.
    void vtodoPromoteWithoutRepeaterMarkerHasNoCompletionAnchor()
    {
        VTodoToCanonStage fwd;
        const QByteArray canon = fwd.transform(kTestVTodo);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QJsonObject obj = parse(canon);
        QVERIFY2(!obj.contains(QStringLiteral("completionAnchor")),
                 "a VTODO without X-ORG-REPEATER must not carry completionAnchor");
    }

    // Demote: completionAnchor (no explicit `start`) derives a standard
    // RRULE anchored at `completed` — an explicit DTSTART matching
    // `completed` is emitted alongside FREQ=WEEKLY (interval 1 omitted).
    void vtodoDemoteDerivesRruleAnchoredAtCompletedForRestartAnchor()
    {
        const QByteArray canon = R"({
            "uid": "derive-restart-1",
            "summary": "Water plants",
            "completed": "2026-06-08T09:00:00Z",
            "completionAnchor": {"type":"restart","interval":1,"unit":"w"}
        })";

        CanonToVTodoStage rev;
        const QByteArray output = rev.transform(canon);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty");
        QVERIFY2(output.contains("RRULE:FREQ=WEEKLY"),
                 qPrintable(QStringLiteral("expected derived RRULE:FREQ=WEEKLY; got:\n")
                     + QString::fromUtf8(output)));
        QVERIFY2(!output.contains("INTERVAL="),
                 "interval 1 must be omitted from the derived RRULE");
        QVERIFY2(output.contains("DTSTART:20260608T090000Z"),
                 qPrintable(QStringLiteral(
                     "derived RRULE must be anchored via an explicit DTSTART "
                     "matching `completed`; got:\n") + QString::fromUtf8(output)));

        const auto outTodo = parseTodoFromICal(output);
        QVERIFY2(outTodo, "could not parse output VTODO");
        QVERIFY2(outTodo->recurs(), "output todo must recur");
        // dtStart() (no-arg) returns the RECURRENCE-RELATIVE "current
        // occurrence" for a recurring todo (KCalendarCore::Todo::dtStart(),
        // reimp of Incidence::dtStart(), defaults to first=false) — use
        // dtStart(true) to read back the literal DTSTART property we wrote.
        QCOMPARE(outTodo->dtStart(true).toUTC(),
                 QDateTime::fromString(QStringLiteral("2026-06-08T09:00:00Z"), Qt::ISODate));
    }

    // Demote: interval != 1 and a non-week unit emit INTERVAL= and the
    // correct FREQ mapping (unit alphabet h/d/w/m/y → HOURLY/DAILY/WEEKLY/
    // MONTHLY/YEARLY).
    void vtodoDemoteCatchUpAnchorEmitsIntervalAndUnitMapping()
    {
        const QByteArray canon = R"({
            "uid": "derive-catchup-1",
            "completed": "2026-06-10T08:00:00Z",
            "completionAnchor": {"type":"catchUp","interval":2,"unit":"d"}
        })";

        CanonToVTodoStage rev;
        const QByteArray output = rev.transform(canon);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty");
        QVERIFY2(output.contains("RRULE:FREQ=DAILY;INTERVAL=2"),
                 qPrintable(QStringLiteral("expected RRULE:FREQ=DAILY;INTERVAL=2; got:\n")
                     + QString::fromUtf8(output)));
        QVERIFY2(output.contains("DTSTART:20260610T080000Z"),
                 qPrintable(QStringLiteral("expected DTSTART:20260610T080000Z; got:\n")
                     + QString::fromUtf8(output)));
    }

    // Demote: when canon already carries an explicit `start`, the derived
    // RRULE must NOT clobber it with a second DTSTART line — the real
    // start is left untouched (declared corner case, see the W4 return
    // receipt: the RRULE's RFC5545 anchor is then the explicit start, not
    // `completed`).
    void vtodoCompletionAnchorDoesNotOverrideExplicitStart()
    {
        const QByteArray canon = R"({
            "uid": "derive-explicit-start-1",
            "completed": "2026-06-08T09:00:00Z",
            "start": {"dateTime": "2026-05-01T12:00:00Z", "floating": false},
            "completionAnchor": {"type":"restart","interval":1,"unit":"m"}
        })";

        CanonToVTodoStage rev;
        const QByteArray output = rev.transform(canon);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty");
        QVERIFY2(output.contains("RRULE:FREQ=MONTHLY"),
                 qPrintable(QStringLiteral("expected derived RRULE:FREQ=MONTHLY; got:\n")
                     + QString::fromUtf8(output)));

        // Exactly one DTSTART line, matching the explicit start (not completed).
        const auto outputLines = QString::fromUtf8(output).split(QLatin1Char('\n'));
        int dtstartCount = 0;
        for (const QString& raw : outputLines) {
            const QString line = raw.trimmed();
            if (line.startsWith(QStringLiteral("DTSTART")))
                ++dtstartCount;
        }
        QCOMPARE(dtstartCount, 1);
        QVERIFY2(!output.contains("DTSTART:20260608T090000Z"),
                 "must not gain a second DTSTART matching `completed`");

        const auto outTodo = parseTodoFromICal(output);
        QVERIFY2(outTodo, "could not parse output VTODO");
        // dtStart(true) reads the literal DTSTART property (see the sibling
        // restart-anchor test for why the no-arg getter is unsuitable for a
        // recurring todo).
        QCOMPARE(outTodo->dtStart(true).toUTC(),
                 QDateTime::fromString(QStringLiteral("2026-05-01T12:00:00Z"), Qt::ISODate));
    }

    // Demote: when canon already carries verbatim `recurrence` lines, the
    // derived RRULE must NOT be injected too (invariant 3: verbatim always
    // wins) — defensive guard, since this combination should not arise from
    // a real promote (completionAnchor only comes from X-ORG-REPEATER,
    // which does not co-occur with a native RRULE in practice).
    void vtodoVerbatimRecurrenceTakesPrecedenceOverCompletionAnchor()
    {
        const QByteArray canon = R"({
            "uid": "derive-precedence-1",
            "completed": "2026-06-08T09:00:00Z",
            "recurrence": ["RRULE:FREQ=DAILY"],
            "completionAnchor": {"type":"restart","interval":1,"unit":"w"}
        })";

        CanonToVTodoStage rev;
        const QByteArray output = rev.transform(canon);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty");
        QVERIFY2(output.contains("RRULE:FREQ=DAILY"),
                 "verbatim recurrence must be emitted");
        QVERIFY2(!output.contains("RRULE:FREQ=WEEKLY"),
                 "the derived completionAnchor RRULE must not also be injected");
    }

    // Round trip: promote → demote → promote again reproduces the same
    // completionAnchor (derived from the X-ORG-REPEATER marker, which
    // itself survives the round trip via the generic extras channel — NOT
    // from the derived RRULE, which promote does not read back).
    void vtodoCompletionAnchorRoundTripStable()
    {
        const QByteArray vtodo =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VTODO\r\n"
            "UID:org-repeater-roundtrip-1\r\n"
            "SUMMARY:Water plants\r\n"
            "COMPLETED:20260608T090000Z\r\n"
            "X-ORG-REPEATER:.+1w\r\n"
            "END:VTODO\r\n"
            "END:VCALENDAR\r\n";

        VTodoToCanonStage fwd;
        CanonToVTodoStage rev;

        const QByteArray canon1 = fwd.transform(vtodo);
        QVERIFY2(!canon1.isEmpty(), "forward stage returned empty");
        const QJsonObject obj1 = parse(canon1);
        const QJsonObject anchor1 = obj1.value(QStringLiteral("completionAnchor")).toObject();
        QVERIFY2(!anchor1.isEmpty(), "completionAnchor must be derived");

        const QByteArray demoted = rev.transform(canon1);
        QVERIFY2(!demoted.isEmpty(), "reverse stage returned empty");
        {
            // KCalendarCore re-serializes a nonKDECustomProperty with an
            // explicit ";VALUE=TEXT" parameter, so check via the parsed
            // property rather than a literal byte match.
            const auto demotedTodo = parseTodoFromICal(demoted);
            QVERIFY2(demotedTodo, "could not parse demoted VTODO");
            QCOMPARE(demotedTodo->nonKDECustomProperty("X-ORG-REPEATER"),
                     QStringLiteral(".+1w"));
        }

        const QByteArray canon2 = fwd.transform(demoted);
        QVERIFY2(!canon2.isEmpty(), "second forward pass returned empty");
        const QJsonObject obj2 = parse(canon2);
        const QJsonObject anchor2 = obj2.value(QStringLiteral("completionAnchor")).toObject();
        QVERIFY2(!anchor2.isEmpty(), "completionAnchor must be re-derived on the second pass");

        QCOMPARE(anchor2.value(QStringLiteral("type")).toString(),
                 anchor1.value(QStringLiteral("type")).toString());
        QCOMPARE(anchor2.value(QStringLiteral("interval")).toInt(),
                 anchor1.value(QStringLiteral("interval")).toInt());
        QCOMPARE(anchor2.value(QStringLiteral("unit")).toString(),
                 anchor1.value(QStringLiteral("unit")).toString());
    }

    // Loss-profile pin: completionAnchor is Reversible on canon → vtodo
    // (rides providerExtras/x-vtodo + the derived RRULE).
    void canonToVtodoLossProfileChargesCompletionAnchorReversible()
    {
        const auto regs = makeTodoRegistries();
        const Shape canon{ DomainId{QStringLiteral("todo")}, EncodingId{QStringLiteral("canon")} };
        const Shape vtodo{ DomainId{QStringLiteral("todo")}, EncodingId{QStringLiteral("ical-vtodo")} };

        const auto loss = regs.transformation.inspect(canon, vtodo);
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("completionAnchor")}),
                 LossKind::Reversible);
    }

    // -----------------------------------------------------------------
    // W6.2 — malformed DTSTART/DUE coercion
    // -----------------------------------------------------------------

    // Rule (a): DTSTART DATE-only + DUE DATE-TIME ⇒ START coerced UP to a
    // DATE-TIME value matching DUE's zone (binding response-doc wording).
    void vtodoCoercesDateOnlyStartToDueDateTimeType()
    {
        const QByteArray vtodo =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VTODO\r\n"
            "UID:coerce-a-1\r\n"
            "SUMMARY:Mismatched a\r\n"
            "DTSTART;VALUE=DATE:20260601\r\n"
            "DUE:20260601T170000Z\r\n"
            "END:VTODO\r\n"
            "END:VCALENDAR\r\n";

        VTodoToCanonStage fwd;
        const QByteArray canon = fwd.transform(vtodo);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QJsonObject obj = parse(canon);

        const QJsonObject startObj = obj.value(QStringLiteral("start")).toObject();
        QVERIFY2(!startObj.contains(QStringLiteral("date")),
                 "start must be coerced away from DATE-only");
        QCOMPARE(startObj.value(QStringLiteral("dateTime")).toString(),
                 QStringLiteral("2026-06-01T00:00:00Z"));

        const QJsonObject dueObj = obj.value(QStringLiteral("due")).toObject();
        QCOMPARE(dueObj.value(QStringLiteral("dateTime")).toString(),
                 QStringLiteral("2026-06-01T17:00:00Z"));
    }

    // Rule (a), reverse direction: DTSTART DATE-TIME + DUE DATE-only ⇒
    // START is truncated DOWN to DATE-only to match DUE. This is the
    // response doc's literal "DUE's type always wins" wording, NOT
    // tasks.org's actual (symmetric) behavior, which would instead promote
    // DUE up to DATE-TIME in this direction — see Open Decision 4 in the
    // VP.f return receipt for the explicit, deliberate divergence note.
    void vtodoCoercesDateTimeStartToDueDateOnlyType()
    {
        const QByteArray vtodo =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VTODO\r\n"
            "UID:coerce-a-2\r\n"
            "SUMMARY:Mismatched a reverse\r\n"
            "DTSTART:20260601T090000Z\r\n"
            "DUE;VALUE=DATE:20260602\r\n"
            "END:VTODO\r\n"
            "END:VCALENDAR\r\n";

        VTodoToCanonStage fwd;
        const QByteArray canon = fwd.transform(vtodo);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QJsonObject obj = parse(canon);

        const QJsonObject startObj = obj.value(QStringLiteral("start")).toObject();
        QVERIFY2(startObj.contains(QStringLiteral("date")),
                 "start must be coerced to DATE-only to match DUE");
        QCOMPARE(startObj.value(QStringLiteral("date")).toString(),
                 QStringLiteral("2026-06-01"));

        const QJsonObject dueObj = obj.value(QStringLiteral("due")).toObject();
        QVERIFY(dueObj.contains(QStringLiteral("date")));
        QCOMPARE(dueObj.value(QStringLiteral("date")).toString(),
                 QStringLiteral("2026-06-02"));
    }

    // Rule (b): DUE <= DTSTART ⇒ DTSTART is dropped from canon entirely.
    void vtodoDropsStartWhenDueNotAfterDtstart()
    {
        const QByteArray vtodo =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VTODO\r\n"
            "UID:coerce-b-1\r\n"
            "SUMMARY:Backwards dates\r\n"
            "DTSTART:20260605T090000Z\r\n"
            "DUE:20260601T090000Z\r\n"
            "END:VTODO\r\n"
            "END:VCALENDAR\r\n";

        VTodoToCanonStage fwd;
        const QByteArray canon = fwd.transform(vtodo);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QJsonObject obj = parse(canon);

        QVERIFY2(!obj.contains(QStringLiteral("start")),
                 "DUE <= DTSTART must drop start from canon entirely");
        QVERIFY(obj.contains(QStringLiteral("due")));
    }

    // Rule (c): DURATION without DTSTART/DUE. Probe-confirmed (2026-08-28):
    // KCalendarCore's parser resolves DURATION into dtDue() at parse time;
    // with no DTSTART to add the duration to, dtDue() comes back invalid —
    // the "drop" already happens by construction, zero promote-side code
    // needed. This pins that already-correct behavior.
    void vtodoPromoteDropsDurationWithoutDtstart()
    {
        const QByteArray vtodo =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VTODO\r\n"
            "UID:coerce-c-1\r\n"
            "SUMMARY:Duration only\r\n"
            "DURATION:PT1H\r\n"
            "END:VTODO\r\n"
            "END:VCALENDAR\r\n";

        VTodoToCanonStage fwd;
        const QByteArray canon = fwd.transform(vtodo);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QJsonObject obj = parse(canon);

        QVERIFY2(!obj.contains(QStringLiteral("due")),
                 "DURATION without DTSTART must not surface as a due value");
        QVERIFY2(!obj.contains(QStringLiteral("start")),
                 "no DTSTART was present in the source");
    }

    // Bonus fix: a plain all-day DTSTART/DUE round-trips through BOTH
    // promote AND demote as a real VALUE=DATE, not a UTC-midnight
    // DATE-TIME (…T000000Z) — the W6.2 bonus fix pin.
    void vtodoAllDayRoundTripPreservesDateValueForm()
    {
        const QByteArray vtodo =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VTODO\r\n"
            "UID:allday-roundtrip-1\r\n"
            "SUMMARY:All day task\r\n"
            "DTSTART;VALUE=DATE:20260601\r\n"
            "DUE;VALUE=DATE:20260602\r\n"
            "END:VTODO\r\n"
            "END:VCALENDAR\r\n";

        VTodoToCanonStage fwd;
        CanonToVTodoStage rev;

        const QByteArray canon = fwd.transform(vtodo);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QJsonObject obj = parse(canon);
        QVERIFY(obj.value(QStringLiteral("start")).toObject().contains(QStringLiteral("date")));
        QVERIFY(obj.value(QStringLiteral("due")).toObject().contains(QStringLiteral("date")));

        const QByteArray output = rev.transform(canon);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty");

        QVERIFY2(output.contains("DTSTART;VALUE=DATE:20260601"),
                 qPrintable(QStringLiteral("expected DTSTART;VALUE=DATE:20260601 in output:\n")
                     + QString::fromUtf8(output)));
        QVERIFY2(output.contains("DUE;VALUE=DATE:20260602"),
                 qPrintable(QStringLiteral("expected DUE;VALUE=DATE:20260602 in output:\n")
                     + QString::fromUtf8(output)));
        QVERIFY2(!output.contains("T000000Z"),
                 "must not regress to a UTC-midnight DATE-TIME form");
    }

    // -----------------------------------------------------------------
    // W5 — VALARM shape extension
    // -----------------------------------------------------------------

    // Regression pin: the pre-existing offset-form shape (default
    // start-relative) is unchanged by the W5 additive keys.
    void vtodoAlarmOffsetFormRoundTrips()
    {
        const QByteArray vtodo =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VTODO\r\n"
            "UID:alarm-offset-1\r\n"
            "SUMMARY:Offset alarm\r\n"
            "DUE:20260601T170000Z\r\n"
            "BEGIN:VALARM\r\n"
            "ACTION:DISPLAY\r\n"
            "TRIGGER:-PT15M\r\n"
            "END:VALARM\r\n"
            "END:VTODO\r\n"
            "END:VCALENDAR\r\n";

        VTodoToCanonStage fwd;
        CanonToVTodoStage rev;

        const QByteArray canon = fwd.transform(vtodo);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QJsonObject obj = parse(canon);
        const QJsonArray alarms = obj.value(QStringLiteral("alarms")).toArray();
        QCOMPARE(alarms.size(), 1);
        const QJsonObject a = alarms.at(0).toObject();
        QCOMPARE(a.value(QStringLiteral("offset")).toInt(), -900);
        QVERIFY2(!a.contains(QStringLiteral("related")),
                 "default start-relative offset omits related (back-compat)");
        QVERIFY2(!a.contains(QStringLiteral("at")), "offset-form alarm must not carry 'at'");

        const QByteArray output = rev.transform(canon);
        const auto outTodo = parseTodoFromICal(output);
        QVERIFY(outTodo);
        const auto outAlarms = outTodo->alarms();
        QCOMPARE(outAlarms.size(), 1);
        QVERIFY(outAlarms.first()->hasStartOffset());
        QCOMPARE(outAlarms.first()->startOffset().asSeconds(), -900);
    }

    // New "at" absolute-trigger form. Also pins the W5 bug fix: pre-W5 code
    // unconditionally read startOffset() (zero for an absolute-trigger
    // alarm), so a bogus "offset: 0" must NOT appear alongside "at".
    void vtodoAlarmAbsoluteAtFormRoundTrips()
    {
        const QByteArray vtodo =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VTODO\r\n"
            "UID:alarm-at-1\r\n"
            "SUMMARY:Absolute alarm\r\n"
            "DUE:20260601T170000Z\r\n"
            "BEGIN:VALARM\r\n"
            "ACTION:DISPLAY\r\n"
            "TRIGGER;VALUE=DATE-TIME:20260601T163000Z\r\n"
            "END:VALARM\r\n"
            "END:VTODO\r\n"
            "END:VCALENDAR\r\n";

        VTodoToCanonStage fwd;
        CanonToVTodoStage rev;

        const QByteArray canon = fwd.transform(vtodo);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QJsonObject obj = parse(canon);
        const QJsonArray alarms = obj.value(QStringLiteral("alarms")).toArray();
        QCOMPARE(alarms.size(), 1);
        const QJsonObject a = alarms.at(0).toObject();
        QCOMPARE(a.value(QStringLiteral("at")).toString(), QStringLiteral("2026-06-01T16:30:00Z"));
        QVERIFY2(!a.contains(QStringLiteral("offset")),
                 "absolute-trigger alarm must not also carry a bogus offset (W5 bug fix)");

        const QByteArray output = rev.transform(canon);
        const auto outTodo = parseTodoFromICal(output);
        QVERIFY(outTodo);
        const auto outAlarms = outTodo->alarms();
        QCOMPARE(outAlarms.size(), 1);
        QVERIFY(outAlarms.first()->hasTime());
        QCOMPARE(outAlarms.first()->time().toUTC(),
                 QDateTime::fromString(QStringLiteral("2026-06-01T16:30:00Z"), Qt::ISODate));
    }

    // New "related":"end" offset form. Also pins the W5 bug fix: pre-W5
    // code unconditionally read startOffset() (zero for an END-related
    // alarm), silently corrupting it to a start-relative offset:0.
    void vtodoAlarmEndRelatedOffsetRoundTrips()
    {
        const QByteArray vtodo =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VTODO\r\n"
            "UID:alarm-end-1\r\n"
            "SUMMARY:End-related alarm\r\n"
            "DUE:20260601T170000Z\r\n"
            "BEGIN:VALARM\r\n"
            "ACTION:DISPLAY\r\n"
            "TRIGGER;RELATED=END:-PT15M\r\n"
            "END:VALARM\r\n"
            "END:VTODO\r\n"
            "END:VCALENDAR\r\n";

        VTodoToCanonStage fwd;
        CanonToVTodoStage rev;

        const QByteArray canon = fwd.transform(vtodo);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QJsonObject obj = parse(canon);
        const QJsonArray alarms = obj.value(QStringLiteral("alarms")).toArray();
        QCOMPARE(alarms.size(), 1);
        const QJsonObject a = alarms.at(0).toObject();
        QCOMPARE(a.value(QStringLiteral("offset")).toInt(), -900);
        QCOMPARE(a.value(QStringLiteral("related")).toString(), QStringLiteral("end"));

        const QByteArray output = rev.transform(canon);
        const auto outTodo = parseTodoFromICal(output);
        QVERIFY(outTodo);
        const auto outAlarms = outTodo->alarms();
        QCOMPARE(outAlarms.size(), 1);
        QVERIFY2(outAlarms.first()->hasEndOffset(),
                 "related:end must demote to an END-related offset, not START");
        QCOMPARE(outAlarms.first()->endOffset().asSeconds(), -900);
    }

    // New REPEAT/DURATION pair.
    void vtodoAlarmRepeatDurationPairRoundTrips()
    {
        const QByteArray vtodo =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VTODO\r\n"
            "UID:alarm-repeat-1\r\n"
            "SUMMARY:Repeating alarm\r\n"
            "DUE:20260601T170000Z\r\n"
            "BEGIN:VALARM\r\n"
            "ACTION:DISPLAY\r\n"
            "TRIGGER:-PT15M\r\n"
            "REPEAT:3\r\n"
            "DURATION:PT5M\r\n"
            "END:VALARM\r\n"
            "END:VTODO\r\n"
            "END:VCALENDAR\r\n";

        VTodoToCanonStage fwd;
        CanonToVTodoStage rev;

        const QByteArray canon = fwd.transform(vtodo);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QJsonObject obj = parse(canon);
        const QJsonArray alarms = obj.value(QStringLiteral("alarms")).toArray();
        QCOMPARE(alarms.size(), 1);
        const QJsonObject a = alarms.at(0).toObject();
        QCOMPARE(a.value(QStringLiteral("repeatCount")).toInt(), 3);
        QCOMPARE(a.value(QStringLiteral("repeatIntervalSecs")).toInt(), 300);

        const QByteArray output = rev.transform(canon);
        const auto outTodo = parseTodoFromICal(output);
        QVERIFY(outTodo);
        const auto outAlarms = outTodo->alarms();
        QCOMPARE(outAlarms.size(), 1);
        QCOMPARE(outAlarms.first()->repeatCount(), 3);
        QCOMPARE(outAlarms.first()->snoozeTime().asSeconds(), 300);
    }

    // Demote must never synthesize an unpaired REPEAT/DURATION: only one of
    // the two canon keys present ⇒ neither setter is called (RFC5545:
    // REPEAT/DURATION MUST occur as a pair).
    void vtodoAlarmUnpairedRepeatIsNotSynthesized()
    {
        QJsonObject canon;
        canon.insert(QStringLiteral("uid"), QStringLiteral("alarm-unpaired-1"));
        QJsonObject a;
        a.insert(QStringLiteral("type"), 1);
        a.insert(QStringLiteral("offset"), -900);
        a.insert(QStringLiteral("repeatCount"), 3);
        // repeatIntervalSecs deliberately absent — must not be synthesized.
        canon.insert(QStringLiteral("alarms"), QJsonArray{ a });

        CanonToVTodoStage rev;
        const QByteArray canonBytes = QJsonDocument(canon).toJson(QJsonDocument::Compact);
        const QByteArray output = rev.transform(canonBytes);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty");
        const auto outTodo = parseTodoFromICal(output);
        QVERIFY(outTodo);
        const auto outAlarms = outTodo->alarms();
        QCOMPARE(outAlarms.size(), 1);
        QCOMPARE(outAlarms.first()->repeatCount(), 0);
    }

    // IP.4 / O85: an enabled source alarm must survive promote->demote
    // still enabled (see the twin VEVENT slot in
    // tst_calendar_canon_roundtrip.cpp — the shared alarmshape module
    // honours this on both incidence kinds).
    void vtodoAlarmEnabledSurvivesRoundTrip()
    {
        const QByteArray vtodo =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VTODO\r\n"
            "UID:alarm-enabled-1\r\n"
            "SUMMARY:Enabled alarm\r\n"
            "DUE:20260601T170000Z\r\n"
            "BEGIN:VALARM\r\n"
            "ACTION:DISPLAY\r\n"
            "TRIGGER:-PT15M\r\n"
            "END:VALARM\r\n"
            "END:VTODO\r\n"
            "END:VCALENDAR\r\n";

        VTodoToCanonStage fwd;
        CanonToVTodoStage rev;

        const auto srcTodo = parseTodoFromICal(vtodo);
        QVERIFY(srcTodo);
        QVERIFY2(srcTodo->alarms().first()->enabled(),
                 "fixture alarm must be enabled=true in the source");

        const QByteArray canon = fwd.transform(vtodo);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QByteArray output = rev.transform(canon);
        const auto outTodo = parseTodoFromICal(output);
        QVERIFY(outTodo);
        const auto outAlarms = outTodo->alarms();
        QCOMPARE(outAlarms.size(), 1);
        QVERIFY2(outAlarms.first()->enabled(),
                 "alarm must still be enabled after round trip (O85)");
    }

    // -----------------------------------------------------------------
    // W7 — generic X-prop passthrough + O74 providerExtrasDigest
    // -----------------------------------------------------------------

    // A genuinely arbitrary/unknown custom property (NOT one of the
    // recognized/consumed ones like X-ORG-REPEATER/X-ALT-DESC/
    // X-CANON-SERIES-SPLIT-OF) must survive promote → demote via
    // providerExtras["x-vtodo"] alone — proving the *generic* passthrough
    // mechanism, not just its special-cased consumers.
    void vtodoGenericUnknownXPropSurvivesRoundTrip()
    {
        const QByteArray vtodo =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VTODO\r\n"
            "UID:xprop-generic-1\r\n"
            "SUMMARY:Has an unknown x-prop\r\n"
            "X-SOME-RANDOM-CLIENT-FIELD:hello world\r\n"
            "END:VTODO\r\n"
            "END:VCALENDAR\r\n";

        VTodoToCanonStage fwd;
        CanonToVTodoStage rev;

        const QByteArray canon = fwd.transform(vtodo);
        QVERIFY2(!canon.isEmpty(), "forward stage returned empty");
        const QJsonObject obj = parse(canon);
        const QJsonObject xvtodo = obj.value(providerExtrasKey()).toObject()
                                       .value(QStringLiteral("x-vtodo")).toObject();
        QCOMPARE(xvtodo.value(QStringLiteral("X-SOME-RANDOM-CLIENT-FIELD")).toString(),
                 QStringLiteral("hello world"));

        const QByteArray output = rev.transform(canon);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty");
        const auto outTodo = parseTodoFromICal(output);
        QVERIFY2(outTodo, "could not parse output VTODO");
        QCOMPARE(outTodo->nonKDECustomProperty("X-SOME-RANDOM-CLIENT-FIELD"),
                 QStringLiteral("hello world"));
    }

    // O74: providerExtrasDigest is present whenever extras are non-empty
    // and absent when there are none, and it changes when the extras
    // content actually changes (proving the fingerprint tracks its input,
    // not just "present or not").
    void vtodoProviderExtrasDigestTracksExtrasContent()
    {
        VTodoToCanonStage fwd;

        auto vtodoWithXProp = [](const QString& value) {
            return QStringLiteral(
                "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//Test//EN\r\n"
                "BEGIN:VTODO\r\nUID:digest-1\r\nSUMMARY:x\r\n"
                "X-SOME-FIELD:%1\r\nEND:VTODO\r\nEND:VCALENDAR\r\n").arg(value).toUtf8();
        };

        const QJsonObject a = parse(fwd.transform(vtodoWithXProp(QStringLiteral("one"))));
        const QJsonObject b = parse(fwd.transform(vtodoWithXProp(QStringLiteral("two"))));

        const QString digestA = a.value(QStringLiteral("providerExtrasDigest")).toString();
        const QString digestB = b.value(QStringLiteral("providerExtrasDigest")).toString();
        QVERIFY2(!digestA.isEmpty(), "digest must be present when extras are non-empty");
        QVERIFY2(digestA != digestB, "digest must change when extras content changes");

        const QByteArray noXPropVtodo =
            "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//Test//EN\r\n"
            "BEGIN:VTODO\r\nUID:digest-2\r\nSUMMARY:x\r\nEND:VTODO\r\nEND:VCALENDAR\r\n";
        const QJsonObject noXProp = parse(fwd.transform(noXPropVtodo));
        QVERIFY2(!noXProp.contains(QStringLiteral("providerExtrasDigest")),
                 "digest must be absent when there are no extras");
    }

    // -------------------------------------------------------------------
    // IP.6 commit 2 (O83) — VTODO gains SEQUENCE/CLASS/COLOR/URL/ORGANIZER/
    // ATTENDEE/ATTACH on the {todo,canon}<->{todo,ical-vtodo} edge, the
    // exact same shared emitter O83 named. Round-trip via VTodoToCanonStage
    // / CanonToVTodoStage, mirroring vtodoRelatedToPreserved()'s pattern.
    // -------------------------------------------------------------------
    void vtodoRoundTripPreservesO83Fields()
    {
        const QByteArray vtodo =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VTODO\r\n"
            "UID:o83-uid\r\n"
            "SUMMARY:O83 task\r\n"
            "SEQUENCE:7\r\n"
            "CLASS:CONFIDENTIAL\r\n"
            "COLOR:teal\r\n"
            "URL:http://example.com/t\r\n"
            "ORGANIZER:mailto:o@example.com\r\n"
            "ATTENDEE;CN=A:mailto:a@example.com\r\n"
            "ATTACH:http://example.com/f.pdf\r\n"
            "END:VTODO\r\n"
            "END:VCALENDAR\r\n";

        VTodoToCanonStage fwd;
        const QByteArray canon = fwd.transform(vtodo);
        QVERIFY(!canon.isEmpty());

        const QJsonObject obj = parse(canon);
        QCOMPARE(obj.value(QStringLiteral("sequence")).toInt(), 7);
        QCOMPARE(obj.value(QStringLiteral("classification")).toString(), QStringLiteral("confidential"));
        QCOMPARE(obj.value(QStringLiteral("color")).toString(), QStringLiteral("teal"));
        QCOMPARE(obj.value(QStringLiteral("url")).toString(), QStringLiteral("http://example.com/t"));
        QVERIFY2(!obj.value(QStringLiteral("organizer")).toObject().isEmpty(), "organizer must be promoted");
        QVERIFY2(!obj.value(QStringLiteral("attendees")).toArray().isEmpty(), "attendees must be promoted");
        QVERIFY2(!obj.value(QStringLiteral("attachments")).toArray().isEmpty(), "attachments must be promoted");

        CanonToVTodoStage rev;
        const QByteArray output = rev.transform(canon);
        const auto outTodo = parseTodoFromICal(output);
        QVERIFY2(outTodo, "could not parse output VTODO");
        QCOMPARE(outTodo->revision(), 7);
        QCOMPARE(outTodo->secrecy(), KCalendarCore::Incidence::SecrecyConfidential);
        QCOMPARE(outTodo->color(), QStringLiteral("teal"));
        QCOMPARE(outTodo->url().toString(), QStringLiteral("http://example.com/t"));
        QCOMPARE(outTodo->organizer().email(), QStringLiteral("o@example.com"));
        QCOMPARE(outTodo->attendees().size(), 1);
        QCOMPARE(outTodo->attachments().size(), 1);
    }

    // IP.6 commit 2 (O91) — COMMENT/CONTACT round-trip on this edge too
    // (same shared emitter). RESOURCES does NOT — O94, upstream:
    // KCalendarCore::ICalFormat never reads or writes a RESOURCES line at
    // all, on either ical wire edge (see canonToVtodoLoss()'s own comment
    // and the IP.6 return receipt for the probe that found this).
    void vtodoCommentsContactsRoundTripResourcesDoesNot()
    {
        const QByteArray vtodo =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VTODO\r\n"
            "UID:o91-uid\r\n"
            "SUMMARY:O91 task\r\n"
            "COMMENT:a comment\r\n"
            "CONTACT:Jane Doe\\, +1-555-0100\r\n"
            "RESOURCES:Projector,VCR\r\n"
            "END:VTODO\r\n"
            "END:VCALENDAR\r\n";

        VTodoToCanonStage fwd;
        const QByteArray canon = fwd.transform(vtodo);
        const QJsonObject obj = parse(canon);
        QVERIFY2(!obj.value(QStringLiteral("comments")).toArray().isEmpty(), "comments must be promoted");
        QVERIFY2(!obj.value(QStringLiteral("contacts")).toArray().isEmpty(), "contacts must be promoted");

        CanonToVTodoStage rev;
        const QByteArray output = rev.transform(canon);
        QVERIFY2(output.contains("COMMENT:"), "COMMENT must survive the round trip");
        QVERIFY2(output.contains("CONTACT:"), "CONTACT must survive the round trip");
        QVERIFY2(!output.contains("RESOURCES"), "RESOURCES must NOT survive — O94, upstream kcalendarcore gap");
    }

    // IP.6 commit 2 (O86, ratified Amendment 2 §B.5) — geo is dropped
    // entirely: never promoted, never demoted, regardless of what a
    // pre-existing canon record (from before this change) might still
    // carry.
    void vtodoNeverPromotesGeoAnyMore()
    {
        const QByteArray vtodo =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VTODO\r\n"
            "UID:geo-uid\r\n"
            "SUMMARY:Geo task\r\n"
            "GEO:1.5;2.5\r\n"
            "END:VTODO\r\n"
            "END:VCALENDAR\r\n";

        VTodoToCanonStage fwd;
        const QByteArray canon = fwd.transform(vtodo);
        const QJsonObject obj = parse(canon);
        QVERIFY2(!obj.contains(QStringLiteral("geo")), "geo must never be promoted any more (O86)");

        // A pre-existing canon record with a stale "geo" key must demote
        // without emitting a GEO line (Dropped means never re-emitted).
        QJsonObject staleCanon = obj;
        QJsonObject geoObj;
        geoObj.insert(QStringLiteral("lat"), 1.5);
        geoObj.insert(QStringLiteral("lon"), 2.5);
        staleCanon.insert(QStringLiteral("geo"), geoObj);
        staleCanon.insert(QStringLiteral("uid"), QStringLiteral("geo-uid"));

        CanonToVTodoStage rev;
        const QByteArray output = rev.transform(serialize(staleCanon));
        QVERIFY2(!output.contains("GEO:"), "a stale canon geo key must not be re-emitted");
    }

    // IP.6 commit 2 (O93 RESOLVED) — canonToVtodoLoss() now declares only
    // the two-then-three genuinely permanent drops (geo/requestStatus/
    // resources), matching canonToVtodoIcalLoss() exactly; O83's seven and
    // O91's comments/contacts are no longer declared Dropped because they
    // are no longer dropped.
    void canonToVtodoLossProfileMatchesFixedEmitter()
    {
        const auto loss = Kalburator::Todo::canonToVtodoLoss();
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("geo")}), LossKind::Dropped);
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("requestStatus")}), LossKind::Dropped);
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("resources")}), LossKind::Dropped);
        static const char* kFixed[] = {
            "attachments", "attendees", "classification", "color", "organizer",
            "sequence", "url", "comments", "contacts",
        };
        for (const char* id : kFixed) {
            QVERIFY2(!loss.affected.contains(PropertyId{QString::fromLatin1(id)}),
                     qPrintable(QStringLiteral("canonToVtodoLoss() still wrongly drops fixed '%1'")
                                    .arg(QString::fromLatin1(id))));
        }
    }
};

QTEST_GUILESS_MAIN(TestTodoCanonRoundtrip)
#include "tst_todo_canon_roundtrip.moc"
