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
    "END:VTODO\r\n"
    "END:VCALENDAR\r\n";

KCalendarCore::Todo::Ptr parseTodoFromICal(const QByteArray &bytes)
{
    KCalendarCore::ICalFormat fmt;
    auto inc = fmt.fromString(QString::fromUtf8(bytes));
    return inc.dynamicCast<KCalendarCore::Todo>();
}

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
        // Must contain the RRULE line verbatim
        bool foundRRule = false;
        for (const auto& rv : recArr) {
            if (rv.toString().contains(QStringLiteral("FREQ=WEEKLY")))
                foundRRule = true;
        }
        QVERIFY2(foundRRule, "RRULE:FREQ=WEEKLY must be captured in recurrence array");

        const QByteArray output = rev.transform(canon);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty");
        // RRULE line must appear in the output iCal
        QVERIFY2(output.contains("FREQ=WEEKLY"),
                 "RRULE must survive vtodo->canon->vtodo round-trip");
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
};

QTEST_GUILESS_MAIN(TestTodoCanonRoundtrip)
#include "tst_todo_canon_roundtrip.moc"
