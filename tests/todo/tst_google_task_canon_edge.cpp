// EEE Phase 3 — google-task ⇄ todo-canon edge suite. Pins the declared
// loss profile (docs/2026-08-23-google-task-edge-loss-profile.md): promote
// from a rich wire object modeled on reference §3.1, declared-vs-actual
// demote walk (Dropped rulings — NO carrier channel exists), round-trip
// identity for the representable set, registry inspection.
//
// NOTE: no terminated raw string literals in this TU (O59 moc tooling rule).

#include <QFile>
#include <QTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "canonenvelope.h"
#include "googletaskcanonstages.h"
#include "tododomaindefinition.h"
#include "todostockshapes.h"
#include "shaperegistries.h"
#include "lossprofile.h"

using Kalburator::Shape::CanonEnvelope::parse;
using Kalburator::Shape::CanonEnvelope::serialize;
using Kalburator::Shape::CanonEnvelope::stampEnvelope;
using Kalburator::Shape::CanonEnvelope::providerExtrasKey;
using Kalburator::Todo::GoogleTaskToCanonStage;
using Kalburator::Todo::CanonToGoogleTaskStage;
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
    "\"kind\": \"tasks#task\","
    "\"id\": \"dGVzdCB0YXNrIDE\","
    "\"etag\": \"\\\"MTQ3NTMyNjA5MQ\\\"\","
    "\"title\": \"Ship the release\","
    "\"notes\": \"Cut the branch and tag\","
    "\"status\": \"needsAction\","
    "\"due\": \"2026-09-01T00:00:00.000Z\","
    "\"updated\": \"2026-08-23T10:01:00.000Z\","
    "\"parent\": \"cGFyZW50dGFzaw\","
    "\"position\": \"00000000000000123456\","
    "\"links\": [{\"type\": \"related\", \"link\": \"https://example.com/pr/1\"}],"
    "\"webViewLink\": \"https://tasks.google.com/task/1\","
    "\"deleted\": false,"
    "\"hidden\": false"
    "}";

} // namespace

class TestGoogleTaskCanonEdge : public QObject {
    Q_OBJECT
private slots:

    // Promote: mapped fields land in canon; Google-only fields ride extras.
    void promoteRichTaskIsLossless()
    {
        GoogleTaskToCanonStage stage;
        const QJsonObject canon = parse(stage.transform(kRichTask));
        QVERIFY2(!canon.isEmpty(), "promote returned empty canon");

        QCOMPARE(canon.value(QStringLiteral("uid")).toString(),
                 QStringLiteral("dGVzdCB0YXNrIDE"));
        QCOMPARE(canon.value(QStringLiteral("summary")).toString(),
                 QStringLiteral("Ship the release"));
        QCOMPARE(canon.value(QStringLiteral("description")).toString(),
                 QStringLiteral("Cut the branch and tag"));
        QCOMPARE(canon.value(QStringLiteral("status")).toString(),
                 QStringLiteral("needsAction"));

        // midnight-UTC due → {date, allDay}
        QCOMPARE(canon.value(QStringLiteral("due")).toObject()
                     .value(QStringLiteral("date")).toString(),
                 QStringLiteral("2026-09-01"));
        QCOMPARE(canon.value(QStringLiteral("due")).toObject()
                     .value(QStringLiteral("allDay")).toBool(),
                 true);

        QCOMPARE(canon.value(QStringLiteral("lastModified")).toString(),
                 QStringLiteral("2026-08-23T10:01:00.000Z"));
        QCOMPARE(canon.value(QStringLiteral("parentUid")).toString(),
                 QStringLiteral("cGFyZW50dGFzaw"));
        QCOMPARE(canon.value(QStringLiteral("sortOrder")).toString(),
                 QStringLiteral("00000000000000123456"));

        // kind/etag/links/webViewLink stash verbatim
        const QJsonObject extras = canon.value(providerExtrasKey())
                                       .toObject()
                                       .value(QStringLiteral("google"))
                                       .toObject();
        QCOMPARE(extras.value(QStringLiteral("kind")).toString(),
                 QStringLiteral("tasks#task"));
        QVERIFY(!extras.value(QStringLiteral("etag")).toString().isEmpty());
        QCOMPARE(extras.value(QStringLiteral("webViewLink")).toString(),
                 QStringLiteral("https://tasks.google.com/task/1"));
    }

    // Declared-loss walk: demote honors each Simplified/Degraded/Dropped
    // ruling (no carrier channel exists).
    void demoteDeclaredLossMatchesReality()
    {
        CanonToGoogleTaskStage stage;

        auto makeCanon = []() {
            QJsonObject obj;
            obj.insert(QStringLiteral("uid"), QStringLiteral("dGVzdDk5"));
            stampEnvelope(obj, QStringLiteral("todo"),
                          QStringLiteral("dGVzdDk5"));
            return obj;
        };

        // status vocabulary collapses to needsAction/completed
        {
            QJsonObject c = makeCanon();
            c.insert(QStringLiteral("status"), QStringLiteral("inProcess"));
            const QJsonObject out = parse(stage.transform(serialize(c)));
            QCOMPARE(out.value(QStringLiteral("status")).toString(),
                     QStringLiteral("needsAction"));
        }
        {
            QJsonObject c = makeCanon();
            c.insert(QStringLiteral("completed"),
                     QStringLiteral("2026-08-23T10:00:00Z"));
            const QJsonObject out = parse(stage.transform(serialize(c)));
            QCOMPARE(out.value(QStringLiteral("status")).toString(),
                     QStringLiteral("completed"));
            QCOMPARE(out.value(QStringLiteral("completed")).toString(),
                     QStringLiteral("2026-08-23T10:00:00Z"));
        }

        // due {dateTime,tz} degrades to its date part
        {
            QJsonObject c = makeCanon();
            c.insert(QStringLiteral("due"),
                     QJsonObject{ { QStringLiteral("dateTime"),
                                    QStringLiteral("2026-09-01T14:30:00") },
                                  { QStringLiteral("tz"), QStringLiteral("UTC") } });
            const QJsonObject out = parse(stage.transform(serialize(c)));
            QCOMPARE(out.value(QStringLiteral("due")).toString(),
                     QStringLiteral("2026-09-01T00:00:00Z"));
        }

        // priority/recurrence have no home and NO carrier channel → absent
        {
            QJsonObject c = makeCanon();
            c.insert(QStringLiteral("priority"), 9);
            c.insert(QStringLiteral("recurrence"),
                     QJsonArray{ QStringLiteral("RRULE:FREQ=DAILY") });
            const QJsonObject out = parse(stage.transform(serialize(c)));
            QVERIFY(!out.contains(QStringLiteral("priority")));
            QVERIFY(!out.contains(QStringLiteral("recurrence")));
        }

        // W4: completionAnchor has no home and NO carrier channel on Google
        // Tasks (no recurrence field at all) → silently dropped, same as
        // recurrence.
        {
            QJsonObject c = makeCanon();
            c.insert(QStringLiteral("completionAnchor"),
                     QJsonObject{ { QStringLiteral("type"), QStringLiteral("restart") },
                                  { QStringLiteral("interval"), 1 },
                                  { QStringLiteral("unit"), QStringLiteral("w") } });
            const QJsonObject out = parse(stage.transform(serialize(c)));
            QVERIFY(!out.contains(QStringLiteral("completionAnchor")));
        }
    }

    // C→G→C byte-equal identity for the representable set.
    void losslessRoundTripIsIdentity()
    {
        QJsonObject canon;
        canon.insert(QStringLiteral("uid"), QStringLiteral("dGVzdDc3Nw"));
        stampEnvelope(canon, QStringLiteral("todo"),
                      QStringLiteral("dGVzdDc3Nw"));
        canon.insert(QStringLiteral("summary"),
                     QStringLiteral("Water the plants"));
        canon.insert(QStringLiteral("description"),
                     QStringLiteral("Monstera especially"));
        canon.insert(QStringLiteral("status"), QStringLiteral("needsAction"));
        canon.insert(
            QStringLiteral("due"),
            QJsonObject{ { QStringLiteral("date"), QStringLiteral("2026-09-05") },
                         { QStringLiteral("allDay"), true } });
        canon.insert(QStringLiteral("lastModified"),
                     QStringLiteral("2026-08-23T10:01:00.000Z"));
        canon.insert(QStringLiteral("sortOrder"),
                     QStringLiteral("00000000000000098765"));

        CanonToGoogleTaskStage demote;
        GoogleTaskToCanonStage promote;
        const QByteArray wireBytes = demote.transform(serialize(canon));
        QVERIFY2(!wireBytes.isEmpty(), "demote returned empty bytes");
        const QByteArray roundTripped = promote.transform(wireBytes);
        QVERIFY2(!roundTripped.isEmpty(), "re-promote returned empty bytes");
        QCOMPARE(roundTripped, serialize(canon));
    }

    // Registry inspection: both directions registered; demote declared lossy.
    void inspectDeclaresGoogleTaskEdge()
    {
        const auto regs = makeTodoRegistries();
        const Shape canon{ DomainId{QStringLiteral("todo")},
                           EncodingId{QStringLiteral("canon")} };
        const Shape gt{ DomainId{QStringLiteral("todo")},
                        EncodingId{QStringLiteral("google-task")} };

        const auto loss = regs.transformation.inspect(canon, gt);
        QVERIFY2(!loss.isLossless(), "canon->google-task must be declared lossy");
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("due")}),
                 LossKind::Degraded);
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("status")}),
                 LossKind::Simplified);
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("recurrence")}),
                 LossKind::Dropped);
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("priority")}),
                 LossKind::Dropped);
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("completionAnchor")}),
                 LossKind::Dropped);

        const auto promoteLoss = regs.transformation.inspect(gt, canon);
        QVERIFY2(promoteLoss.isLossless(), "promote must be lossless");
    }

    // Slot — the committed live-capture fixtures (tests/fixtures/vendor/
    // google/task-listing-default.json and task-lists.json, sanitized
    // extracts of the real account's 2026-08-24 Tasks captures) promote
    // cleanly: every wire item maps to non-empty canon with uid/summary/
    // status pinned to the (sanitized) wire values, transport identity
    // stashed in providerExtras; the taskLists collection parses as a
    // shape smoke check.
    void promoteCommittedLiveFixture()
    {
        QFile listing(QLatin1String(KALBURATOR_VENDOR_FIXTURE_DIR)
                      + QStringLiteral("/google/task-listing-default.json"));
        QVERIFY2(listing.open(QIODevice::ReadOnly),
                 qPrintable(listing.errorString()));
        const QJsonObject wire =
            QJsonDocument::fromJson(listing.readAll()).object();
        QVERIFY(!wire.isEmpty());
        QCOMPARE(wire.value(QStringLiteral("kind")).toString(),
                 QStringLiteral("tasks#tasks"));

        const QJsonArray items = wire.value(QStringLiteral("items")).toArray();
        QVERIFY2(!items.isEmpty(), "fixture has no items");

        GoogleTaskToCanonStage stage;
        for (const auto& it : items) {
            const QJsonObject task = it.toObject();
            QVERIFY(!task.isEmpty());
            const QByteArray bytes = stage.transform(
                QJsonDocument(task).toJson(QJsonDocument::Compact));
            const QJsonObject canon = parse(bytes);
            QVERIFY2(!canon.isEmpty(), "promote returned empty canon");

            // uid ← wire id; summary ← wire title (post-sanitization);
            // status vocabulary collapses to needsAction/completed.
            QCOMPARE(canon.value(QStringLiteral("uid")).toString(),
                     task.value(QStringLiteral("id")).toString());
            QCOMPARE(canon.value(QStringLiteral("summary")).toString(),
                     task.value(QStringLiteral("title")).toString());
            const QString status =
                canon.value(QStringLiteral("status")).toString();
            QVERIFY2(status == QLatin1String("needsAction")
                         || status == QLatin1String("completed"),
                     qPrintable(QStringLiteral("unexpected status: %1")
                                    .arg(status)));

            // transport identity stashed: id itself is consumed into uid
            // (NOT duplicated in extras), but kind/etag ride the stash.
            const QJsonObject extras =
                canon.value(providerExtrasKey()).toObject()
                    .value(QStringLiteral("google")).toObject();
            QCOMPARE(extras.value(QStringLiteral("kind")).toString(),
                     QStringLiteral("tasks#task"));
            QCOMPARE(extras.value(QStringLiteral("etag")).toString(),
                     task.value(QStringLiteral("etag")).toString());
            QVERIFY(!extras.isEmpty());
        }

        // Collection-level shape smoke check.
        QFile lists(QLatin1String(KALBURATOR_VENDOR_FIXTURE_DIR)
                    + QStringLiteral("/google/task-lists.json"));
        QVERIFY2(lists.open(QIODevice::ReadOnly), qPrintable(lists.errorString()));
        const QJsonDocument doc = QJsonDocument::fromJson(lists.readAll());
        QVERIFY2(doc.isObject(), "task-lists.json is not a JSON object");
        QCOMPARE(doc.object().value(QStringLiteral("kind")).toString(),
                 QStringLiteral("tasks#taskLists"));
    }
};

QTEST_MAIN(TestGoogleTaskCanonEdge)
#include "tst_google_task_canon_edge.moc"
