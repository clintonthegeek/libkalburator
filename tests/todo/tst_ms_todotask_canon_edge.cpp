// EEE Phase 3 — ms-todotask ⇄ todo-canon edge suite. Pins the declared
// loss profile (docs/2026-08-23-ms-todotask-edge-loss-profile.md): promote
// from a rich wire object modeled on reference §3.2, declared-vs-actual
// demote walk (importance thresholds, body split, recurrence carry via the
// kalburator.canon open extension), round-trip identity, registry
// inspection.
//
// NOTE: no terminated raw string literals in this TU (O59 moc tooling rule).
// Fixture-promotion slot pins the sanitized 2026-08-24 /me/todo corpus
// captures under tests/fixtures/vendor/microsoft/.

#include <QTest>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>

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

        // single reminder → alarms[0] (W5 — unified {type, at} shape, NOT
        // the old vendor-specific {"reminder": {...}} sub-shape). "Pacific
        // Standard Time" (Windows vocabulary) resolves via the vendored
        // CLDR map to America/Los_Angeles; expected value computed the same
        // way the implementation does, rather than hand-computing DST math.
        {
            const QJsonObject alarm0 = canon.value(QStringLiteral("alarms")).toArray()
                                            .at(0).toObject();
            QCOMPARE(alarm0.value(QStringLiteral("type")).toInt(), 1); // Display
            QVERIFY2(!alarm0.contains(QStringLiteral("reminder")),
                     "alarms[0] must not carry the legacy 'reminder' sub-shape");
            const QTimeZone pacific("America/Los_Angeles");
            QVERIFY(pacific.isValid());
            const QDateTime wall = QDateTime::fromString(
                QStringLiteral("2026-09-01T16:45:00"), QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
            const QDateTime expectedUtc = QDateTime(wall.date(), wall.time(), pacific).toUTC();
            QCOMPARE(QDateTime::fromString(alarm0.value(QStringLiteral("at")).toString(), Qt::ISODate),
                     expectedUtc);
        }

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

        // O74: providerExtrasDigest is present (kRichTask has extras).
        QVERIFY2(!canon.value(QStringLiteral("providerExtrasDigest")).toString().isEmpty(),
                 "providerExtrasDigest must be computed when extras are non-empty");
    }

    // O74 — the digest must be STABLE across a change confined purely to a
    // volatile bookkeeping field (@odata.etag / lastModifiedDateTime /
    // @odata.context), and must CHANGE when real (non-volatile) extras
    // content changes. This is the load-bearing property the filtering
    // exists for: an unfiltered digest would flip on every server write.
    void providerExtrasDigestIgnoresVolatileMsBookkeeping()
    {
        MsTodoTaskToCanonStage stage;

        auto makeTask = [](const QString& etag, const QString& lastMod,
                            const QString& context, const QString& hasAttachments) {
            return QStringLiteral(
                "{\"id\": \"AAMkDIGEST1\", \"title\": \"x\", "
                "\"@odata.etag\": \"%1\", \"lastModifiedDateTime\": \"%2\", "
                "\"@odata.context\": \"%3\", \"hasAttachments\": %4}")
                .arg(etag, lastMod, context, hasAttachments).toUtf8();
        };

        const QJsonObject base = parse(stage.transform(
            makeTask(QStringLiteral("W/\\\"one\\\""), QStringLiteral("2026-08-23T06:06:07Z"),
                     QStringLiteral("https://graph.microsoft.com/v1.0/$metadata#x"),
                     QStringLiteral("true"))));
        const QJsonObject onlyBookkeepingChanged = parse(stage.transform(
            makeTask(QStringLiteral("W/\\\"two\\\""), QStringLiteral("2026-08-24T09:00:00Z"),
                     QStringLiteral("https://graph.microsoft.com/beta/$metadata#x(extensions())"),
                     QStringLiteral("true"))));
        const QJsonObject realContentChanged = parse(stage.transform(
            makeTask(QStringLiteral("W/\\\"one\\\""), QStringLiteral("2026-08-23T06:06:07Z"),
                     QStringLiteral("https://graph.microsoft.com/v1.0/$metadata#x"),
                     QStringLiteral("false"))));

        const QString baseDigest = base.value(QStringLiteral("providerExtrasDigest")).toString();
        QVERIFY2(!baseDigest.isEmpty(), "digest must be present");
        QCOMPARE(onlyBookkeepingChanged.value(QStringLiteral("providerExtrasDigest")).toString(),
                 baseDigest);
        QVERIFY2(realContentChanged.value(QStringLiteral("providerExtrasDigest")).toString()
                     != baseDigest,
                 "a real (non-volatile) extras content change must change the digest");
    }

    // W5 MS-leg shape unification round trip: C→MS→C is identity on the
    // unified {type, at} alarm shape (the fix folded into W5 — see the
    // promote block's comment).
    void msTodoTaskAlarmRoundTripUnifiesShape()
    {
        MsTodoTaskToCanonStage promote;
        CanonToMsTodoTaskStage demote;

        const QByteArray wire =
            "{\"id\": \"AAMkALARM1\", \"title\": \"Water plants\", "
            "\"reminderDateTime\": {\"dateTime\": \"2026-09-01T16:45:00.0000000\", "
            "\"timeZone\": \"Pacific Standard Time\"}}";

        const QByteArray canon1 = promote.transform(wire);
        QVERIFY2(!canon1.isEmpty(), "promote returned empty canon");
        const QJsonObject obj1 = parse(canon1);
        const QString at1 = obj1.value(QStringLiteral("alarms")).toArray()
                                 .at(0).toObject().value(QStringLiteral("at")).toString();
        QVERIFY2(!at1.isEmpty(), "alarms[0].at must be populated");

        const QByteArray wireBack = demote.transform(canon1);
        const QJsonObject demoted = parse(wireBack);
        const QJsonObject reminder = demoted.value(QStringLiteral("reminderDateTime")).toObject();
        QCOMPARE(reminder.value(QStringLiteral("timeZone")).toString(), QStringLiteral("UTC"));

        const QByteArray canon2 = promote.transform(wireBack);
        const QJsonObject obj2 = parse(canon2);
        const QString at2 = obj2.value(QStringLiteral("alarms")).toArray()
                                 .at(0).toObject().value(QStringLiteral("at")).toString();
        QCOMPARE(QDateTime::fromString(at2, Qt::ISODate),
                 QDateTime::fromString(at1, Qt::ISODate));
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

        // W4: completionAnchor has no todoTask home → auto-carries as an
        // open-extension carrier (x-canon-completion-anchor), same
        // mechanism as percentComplete above (decision 2: auto-carry, not
        // added to the demote's `handled` set).
        {
            QJsonObject c = makeCanon();
            c.insert(QStringLiteral("completionAnchor"),
                     QJsonObject{ { QStringLiteral("type"), QStringLiteral("restart") },
                                  { QStringLiteral("interval"), 1 },
                                  { QStringLiteral("unit"), QStringLiteral("w") } });
            const QJsonObject out = parse(stage.transform(serialize(c)));
            bool found = false;
            for (const auto& ev :
                 out.value(QStringLiteral("extensions")).toArray()) {
                const QJsonObject ext = ev.toObject();
                if (ext.value(QStringLiteral("extensionName")).toString()
                    == QLatin1String("kalburator.canon")
                    && ext.contains(QLatin1String("x-canon-completion-anchor")))
                    found = true;
            }
            QVERIFY2(found,
                     "completionAnchor must ride a kalburator.canon carrier");
        }

        // W3: seriesSplitOf has no todoTask home → auto-carries as an
        // open-extension carrier, literally x-canon-series-split-of (same
        // mechanism as completionAnchor above: decision 2, auto-carry, not
        // added to the demote's `handled` set).
        {
            QJsonObject c = makeCanon();
            c.insert(QStringLiteral("seriesSplitOf"),
                     QStringLiteral("weekly-series-1"));
            const QJsonObject out = parse(stage.transform(serialize(c)));
            QString carriedValue;
            for (const auto& ev :
                 out.value(QStringLiteral("extensions")).toArray()) {
                const QJsonObject ext = ev.toObject();
                if (ext.value(QStringLiteral("extensionName")).toString()
                    == QLatin1String("kalburator.canon")
                    && ext.contains(QLatin1String("x-canon-series-split-of")))
                    carriedValue =
                        ext.value(QLatin1String("x-canon-series-split-of")).toString();
            }
            QCOMPARE(carriedValue, QStringLiteral("weekly-series-1"));
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
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("completionAnchor")}),
                 LossKind::Reversible);
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("seriesSplitOf")}),
                 LossKind::Reversible);
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("providerExtrasDigest")}),
                 LossKind::Dropped);

        const auto promoteLoss = regs.transformation.inspect(mt, canon);
        QVERIFY2(promoteLoss.isLossless(), "promote must be lossless");
    }

    // Slot — the committed live-capture fixtures (sanitized 2026-08-24
    // corpus extracts under tests/fixtures/vendor/microsoft/) promote
    // cleanly: every listed task maps through the canon edge with wire
    // identity, title, and importance→priority intact, and the todo-lists
    // collection keeps its well-known-list vocabulary.
    void promoteCommittedLiveFixture()
    {
        QFile tasksFile(QLatin1String(KALBURATOR_VENDOR_FIXTURE_DIR)
                        + QStringLiteral("/microsoft/todo-tasks-listing.json"));
        QVERIFY2(tasksFile.open(QIODevice::ReadOnly),
                 qPrintable(tasksFile.errorString()));
        const QJsonDocument tasksDoc = QJsonDocument::fromJson(tasksFile.readAll());
        QVERIFY2(!tasksDoc.isNull(), "todo-tasks-listing.json is not valid JSON");
        const QJsonArray tasks = tasksDoc.object()
                                     .value(QStringLiteral("value")).toArray();
        QVERIFY2(!tasks.isEmpty(), "todo-tasks-listing.json has no value[]");

        MsTodoTaskToCanonStage stage;
        for (const auto& item : tasks) {
            const QJsonObject wire = item.toObject();
            const QJsonObject canon =
                parse(stage.transform(
                    QJsonDocument(wire).toJson(QJsonDocument::Compact)));

            QVERIFY2(!canon.isEmpty(), "promote returned empty canon");
            QCOMPARE(canon.value(QStringLiteral("uid")).toString(),
                     wire.value(QStringLiteral("id")).toString());
            QCOMPARE(canon.value(QStringLiteral("summary")).toString(),
                     wire.value(QStringLiteral("title")).toString());

            // importance ∈ {low, normal, high} → priority {9, 5, 1}
            const QString importance =
                wire.value(QStringLiteral("importance")).toString();
            int expectedPriority = 5;
            if (importance == QLatin1String("low"))
                expectedPriority = 9;
            else if (importance == QLatin1String("high"))
                expectedPriority = 1;
            QCOMPARE(canon.value(QStringLiteral("priority")).toInt(),
                     expectedPriority);

            // due / completed handling must survive the real wire shapes.
            if (wire.contains(QLatin1String("dueDateTime"))) {
                QCOMPARE(canon.value(QStringLiteral("due")).toObject()
                             .value(QStringLiteral("dateTime")).toString(),
                         wire.value(QStringLiteral("dueDateTime")).toObject()
                             .value(QStringLiteral("dateTime")).toString());
            }
            if (wire.contains(QLatin1String("completedDateTime")))
                QVERIFY(canon.contains(QLatin1String("completed")));

            const QJsonObject extras = canon.value(providerExtrasKey())
                                           .toObject()
                                           .value(QStringLiteral("msgraph"))
                                           .toObject();
            QVERIFY(!extras.value(QStringLiteral("@odata.etag")).toString()
                         .isEmpty());
        }

        // todo-lists: the default list keeps its well-known name.
        QFile listsFile(QLatin1String(KALBURATOR_VENDOR_FIXTURE_DIR)
                        + QStringLiteral("/microsoft/todo-lists.json"));
        QVERIFY2(listsFile.open(QIODevice::ReadOnly),
                 qPrintable(listsFile.errorString()));
        const QJsonDocument listsDoc = QJsonDocument::fromJson(listsFile.readAll());
        QVERIFY2(!listsDoc.isNull(), "todo-lists.json is not valid JSON");
        const QJsonArray lists = listsDoc.object()
                                     .value(QStringLiteral("value")).toArray();
        QVERIFY2(!lists.isEmpty(), "todo-lists.json has no value[]");
        QCOMPARE(lists.at(0).toObject()
                     .value(QStringLiteral("wellknownListName")).toString(),
                 QStringLiteral("defaultList"));
    }
};

QTEST_MAIN(TestMsTodoTaskCanonEdge)
#include "tst_ms_todotask_canon_edge.moc"
