// EEE Phase 3 — ms-todotask ⇄ todo-canon edge suite. Pins the declared
// loss profile (docs/2026-08-23-ms-todotask-edge-loss-profile.md): promote
// from a rich wire object modeled on reference §3.2, declared-vs-actual
// demote walk (importance thresholds, body split, recurrence carry via the
// kalburator.canon open extension), round-trip identity, registry
// inspection.
//
// NOTE: no terminated raw string literals in this TU (O59 moc tooling rule).
// Fixture-promotion slot deferred until a /me/todo corpus capture lands.

#include <QTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "canonenvelope.h"
#include "mstodotaskcanonstages.h"
#include "tododomaindefinition.h"
#include "todostockshapes.h"
#include "shaperegistries.h"
#include "lossprofile.h"

using Kalburator::Shape::CanonEnvelope::parse;
using Kalburator::Shape::CanonEnvelope::serialize;
using Kalburator::Shape::CanonEnvelope::stampEnvelope;
using Kalburator::Shape::CanonEnvelope::providerExtrasKey;
using Kalburator::Todo::MsTodoTaskToCanonStage;
using Kalburator::Todo::CanonToMsTodoTaskStage;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::LossKind;
using Kalburator::Shape::Shape;

namespace {

Kalburator::Shape::ShapeRegistries makeTodoRegistries()
{
    Kalburator::Shape::ShapeRegistries regs;
    auto& reg = regs.transformation;

    Kalburator::Todo::TodoDomainDefinition def;
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

const QByteArray kRichTask =
    "{"
    "\"@odata.etag\": \"W/\\\"CQAAABYAAAIm4\\\"\","
    "\"id\": \"AAMkAGZmNmJhNjE0\","
    "\"title\": \"File expense report\","
    "\"body\": {\"content\": \"<b>Receipts</b> attached\", \"contentType\": \"html\"},"
    "\"status\": \"inProgress\","
    "\"importance\": \"high\","
    "\"categories\": [\"work\"],"
    "\"dueDateTime\": {\"dateTime\": \"2026-09-01T17:00:00.0000000\", \"timeZone\": \"Pacific Standard Time\"},"
    "\"startDateTime\": {\"dateTime\": \"2026-08-30T09:00:00.0000000\", \"timeZone\": \"Pacific Standard Time\"},"
    "\"isReminderOn\": true,"
    "\"reminderDateTime\": {\"dateTime\": \"2026-09-01T16:45:00.0000000\", \"timeZone\": \"Pacific Standard Time\"},"
    "\"recurrence\": {\"pattern\": {\"type\": \"weekly\", \"interval\": 1, \"firstDayOfWeek\": \"monday\", \"daysOfWeek\": [\"monday\"]}, \"range\": {\"type\": \"noEnd\", \"startDate\": \"2026-08-24\"}},"
    "\"hasAttachments\": true,"
    "\"createdDateTime\": \"2026-08-23T06:06:07Z\","
    "\"lastModifiedDateTime\": \"2026-08-23T06:06:07Z\""
    "}";

} // namespace

class TestMsTodoTaskCanonEdge : public QObject {
    Q_OBJECT
private slots:

    // Promote: mapped fields land in canon; MS-only fields ride extras.
    void promoteRichTaskIsLossless()
    {
        MsTodoTaskToCanonStage stage;
        const QJsonObject canon = parse(stage.transform(kRichTask));
        QVERIFY2(!canon.isEmpty(), "promote returned empty canon");

        QCOMPARE(canon.value(QStringLiteral("uid")).toString(),
                 QStringLiteral("AAMkAGZmNmJhNjE0"));
        QCOMPARE(canon.value(QStringLiteral("summary")).toString(),
                 QStringLiteral("File expense report"));
        QCOMPARE(canon.value(QStringLiteral("descriptionHtml")).toString(),
                 QStringLiteral("<b>Receipts</b> attached"));
        QCOMPARE(canon.value(QStringLiteral("status")).toString(),
                 QStringLiteral("inProgress"));
        QCOMPARE(canon.value(QStringLiteral("priority")).toInt(), 1);
        QCOMPARE(canon.value(QStringLiteral("categories")).toArray().at(0).toString(),
                 QStringLiteral("work"));

        // dateTimeTimeZone → {dateTime, tz} verbatim
        QCOMPARE(canon.value(QStringLiteral("due")).toObject()
                     .value(QStringLiteral("tz")).toString(),
                 QStringLiteral("Pacific Standard Time"));
        QCOMPARE(canon.value(QStringLiteral("due")).toObject()
                     .value(QStringLiteral("dateTime")).toString(),
                 QStringLiteral("2026-09-01T17:00:00.0000000"));

        // single reminder → alarms[0]
        QCOMPARE(canon.value(QStringLiteral("alarms")).toArray()
                     .at(0).toObject()
                     .value(QStringLiteral("reminder")).toObject()
                     .value(QStringLiteral("dateTime")).toString(),
                 QStringLiteral("2026-09-01T16:45:00.0000000"));

        // patternedRecurrence → RFC5545 line
        QVERIFY(canon.value(QStringLiteral("recurrence")).toArray()
                    .at(0).toString()
                    .startsWith(QLatin1String("RRULE:FREQ=WEEKLY")));

        // @odata.etag + timestamps stash under providerExtras
        const QJsonObject extras = canon.value(providerExtrasKey())
                                       .toObject()
                                       .value(QStringLiteral("msgraph"))
                                       .toObject();
        QVERIFY(!extras.value(QStringLiteral("@odata.etag")).toString().isEmpty());
        QVERIFY(!extras.value(QStringLiteral("createdDateTime")).toString().isEmpty());
    }

    // Declared-loss walk: demote honors each Simplified/Degraded ruling.
    void demoteDeclaredLossMatchesReality()
    {
        CanonToMsTodoTaskStage stage;

        auto makeCanon = []() {
            QJsonObject obj;
            obj.insert(QStringLiteral("uid"), QStringLiteral("AAMkCANON9"));
            stampEnvelope(obj, QStringLiteral("todo"),
                          QStringLiteral("AAMkCANON9"));
            return obj;
        };

        // priority thresholds → importance enum
        {
            QJsonObject c = makeCanon();
            c.insert(QStringLiteral("priority"), 5);
            const QJsonObject out = parse(stage.transform(serialize(c)));
            QCOMPARE(out.value(QStringLiteral("importance")).toString(),
                     QStringLiteral("normal"));
        }

        // description (text) → body contentType text
        {
            QJsonObject c = makeCanon();
            c.insert(QStringLiteral("description"), QStringLiteral("plain"));
            const QJsonObject out = parse(stage.transform(serialize(c)));
            QCOMPARE(out.value(QStringLiteral("body")).toObject()
                         .value(QStringLiteral("contentType")).toString(),
                     QStringLiteral("text"));
        }

        // due all-day form degrades to midnight UTC
        {
            QJsonObject c = makeCanon();
            c.insert(QStringLiteral("due"),
                     QJsonObject{ { QStringLiteral("date"),
                                    QStringLiteral("2026-09-02") },
                                  { QStringLiteral("allDay"), true } });
            const QJsonObject out = parse(stage.transform(serialize(c)));
            QCOMPARE(out.value(QStringLiteral("dueDateTime")).toObject()
                         .value(QStringLiteral("dateTime")).toString(),
                     QStringLiteral("2026-09-02T00:00:00"));
            QCOMPARE(out.value(QStringLiteral("dueDateTime")).toObject()
                         .value(QStringLiteral("timeZone")).toString(),
                     QStringLiteral("UTC"));
        }

        // percentComplete: no todoTask home → carrier row present
        {
            QJsonObject c = makeCanon();
            c.insert(QStringLiteral("percentComplete"), 50);
            const QJsonObject out = parse(stage.transform(serialize(c)));
            bool found = false;
            for (const auto& ev :
                 out.value(QStringLiteral("extensions")).toArray()) {
                const QJsonObject ext = ev.toObject();
                if (ext.value(QStringLiteral("extensionName")).toString()
                    == QLatin1String("kalburator.canon")
                    && ext.contains(QLatin1String("x-canon-percent-complete")))
                    found = true;
            }
            QVERIFY2(found,
                     "percentComplete must ride a kalburator.canon carrier");
        }
    }

    // Unrepresentable RRULE rulings ride the carrier and re-promote
    // byte-equal (7.B carried-set discipline on a new channel).
    void unrepresentableRruleCarriesThroughExtension()
    {
        // Ordinal-prefixed BYDAY on MONTHLY (no BYSETPOS) is not
        // representable by Graph patterns — the converter reduces + carries.
        QJsonObject canon;
        canon.insert(QStringLiteral("uid"), QStringLiteral("AAMkREC1"));
        stampEnvelope(canon, QStringLiteral("todo"), QStringLiteral("AAMkREC1"));
        canon.insert(QStringLiteral("summary"), QStringLiteral("Recur odd"));
        canon.insert(
            QStringLiteral("recurrence"),
            QJsonArray{ QStringLiteral("RRULE:FREQ=MONTHLY;BYDAY=2MO") });

        CanonToMsTodoTaskStage demote;
        MsTodoTaskToCanonStage promote;
        const QByteArray wireBytes = demote.transform(serialize(canon));
        const QJsonObject wire = parse(wireBytes);

        // carrier row must exist with the full original line set
        bool foundCarrier = false;
        for (const auto& ev : wire.value(QStringLiteral("extensions")).toArray()) {
            const QJsonObject ext = ev.toObject();
            if (ext.value(QStringLiteral("extensionName")).toString()
                == QLatin1String("kalburator.canon")
                && ext.contains(QLatin1String("x-canon-recurrence"))) {
                foundCarrier = true;
            }
        }
        QVERIFY2(foundCarrier, "cannot-represent RRULE must be carried");

        const QByteArray roundTripped = promote.transform(wireBytes);
        QCOMPARE(roundTripped, serialize(canon));
    }

    // C→MS→C byte-equal identity for the lossless + carrier set.
    void losslessRoundTripIsIdentity()
    {
        QJsonObject canon;
        canon.insert(QStringLiteral("uid"), QStringLiteral("AAMkRT777"));
        stampEnvelope(canon, QStringLiteral("todo"), QStringLiteral("AAMkRT777"));
        canon.insert(QStringLiteral("summary"), QStringLiteral("Water the plants"));
        canon.insert(QStringLiteral("description"), QStringLiteral("Monstera especially"));
        canon.insert(QStringLiteral("status"), QStringLiteral("inProgress"));
        canon.insert(QStringLiteral("priority"), 9);
        canon.insert(QStringLiteral("categories"),
                     QJsonArray{ QStringLiteral("home") });
        canon.insert(
            QStringLiteral("due"),
            QJsonObject{ { QStringLiteral("dateTime"),
                           QStringLiteral("2026-09-05T18:00:00.0000000") },
                         { QStringLiteral("tz"),
                           QStringLiteral("Pacific Standard Time") } });
        canon.insert(
            QStringLiteral("completed"),
            QStringLiteral("2026-08-23T11:00:00.0000000"));

        CanonToMsTodoTaskStage demote;
        MsTodoTaskToCanonStage promote;
        const QByteArray wireBytes = demote.transform(serialize(canon));
        QVERIFY2(!wireBytes.isEmpty(), "demote returned empty bytes");
        const QByteArray roundTripped = promote.transform(wireBytes);
        QVERIFY2(!roundTripped.isEmpty(), "re-promote returned empty bytes");
        QCOMPARE(roundTripped, serialize(canon));
    }

    // Registry inspection: both directions registered; demote declared lossy.
    void inspectDeclaresMsTodoTaskEdge()
    {
        const auto regs = makeTodoRegistries();
        const Shape canon{ DomainId{QStringLiteral("todo")},
                           EncodingId{QStringLiteral("canon")} };
        const Shape mt{ DomainId{QStringLiteral("todo")},
                        EncodingId{QStringLiteral("ms-todotask")} };

        const auto loss = regs.transformation.inspect(canon, mt);
        QVERIFY2(!loss.isLossless(), "canon->ms-todotask must be declared lossy");
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("priority")}),
                 LossKind::Degraded);
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("recurrence")}),
                 LossKind::Reversible);
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("checklistItems")}),
                 LossKind::Dropped);

        const auto promoteLoss = regs.transformation.inspect(mt, canon);
        QVERIFY2(promoteLoss.isLossless(), "promote must be lossless");
    }
};

QTEST_MAIN(TestMsTodoTaskCanonEdge)
#include "tst_ms_todotask_canon_edge.moc"
