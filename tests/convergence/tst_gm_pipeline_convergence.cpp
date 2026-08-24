// EEE Phase 6 — the payoff: G⇄M convergence through the canon.
//
// Pipeline-level gate: for every vendor pair and direction, take a REAL
// canon record (promoted losslessly from a vendor-A wire payload), cross
// vendor B (demote → re-promote), and require every differing top-level
// canon property to be DECLARED in vendor B's demote LossProfile. An
// undeclared divergence is a RED failure (campaign invariant 2 applied at
// pipeline level).
//
// Comparison scope: top-level canon properties. `_canon` envelope and
// `providerExtras` transport bags are excluded (extras are vendor-local
// transport everywhere, per every edge's declared profile). uid IS
// compared — anchors survive crossing by design (O61(f) notes they are
// per-COPY, not per-vendor).
//
// NOTE: no terminated raw string literals in this TU (O59 moc tooling rule).

#include <QTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>

#include "canonenvelope.h"
#include "convergencematrix.h"
#include "calendarstockshapes.h"
#include "contactsstockshapes.h"
#include "todostockshapes.h"
#include "googlecanonstages.h"
#include "mseventcanonstages.h"
#include "googlepersoncanonstages.h"
#include "mscontactcanonstages.h"
#include "googletaskcanonstages.h"
#include "mstodotaskcanonstages.h"
#include "lossprofile.h"

using Kalburator::Shape::CanonEnvelope::parse;
using Kalburator::Shape::CanonEnvelope::serialize;
using Kalburator::Shape::CanonEnvelope::stampEnvelope;
using Kalburator::Shape::CanonEnvelope::providerExtrasKey;
using Kalburator::Shape::ConvergenceMatrix;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::PropertyId;

namespace {

const QStringList kExcludedKeys = {
    QStringLiteral("_canon"), providerExtrasKey()
};

// Rich Google Calendar event (modeled on the Phase-2 suite's captured-shape
// payload).
const QByteArray kGoogleEvent =
    "{"
    "\"id\": \"0n4s31vko6o3a9n6sj6kj9d24p\","
    "\"status\": \"confirmed\","
    "\"summary\": \"Team Meeting\","
    "\"description\": \"Weekly sync\","
    "\"location\": \"Room 4\","
    "\"creator\": {\"email\": \"alice@example.com\", \"self\": true},"
    "\"organizer\": {\"email\": \"alice@example.com\", \"self\": true},"
    "\"start\": {\"dateTime\": \"2026-06-01T09:00:00-04:00\", \"timeZone\": \"America/New_York\"},"
    "\"end\": {\"dateTime\": \"2026-06-01T10:00:00-04:00\", \"timeZone\": \"America/New_York\"},"
    "\"recurrence\": [\"RRULE:FREQ=WEEKLY;BYDAY=MO,WE\"],"
    "\"iCalUID\": \"team-meeting-uid@google.com\","
    "\"visibility\": \"private\","
    "\"transparency\": \"opaque\","
    "\"attendees\": ["
    "  {\"email\": \"bob@example.com\", \"displayName\": \"Bob\", \"responseStatus\": \"accepted\", \"optional\": false},"
    "  {\"email\": \"carol@example.com\", \"responseStatus\": \"tentative\", \"optional\": true}"
    "]"
    "}";

// Rich Graph v1.0 event (captured-shape).
const QByteArray kMsEvent =
    "{"
    "\"id\": \"AAMkAGZlMjNkNGU0AAA=\","
    "\"iCalUId\": \"040000008200E00074C5B7101A82E00800000000\","
    "\"subject\": \"Team Meeting\","
    "\"bodyPreview\": \"Weekly sync\","
    "\"body\": {\"contentType\": \"text\", \"content\": \"Weekly sync\"},"
    "\"start\": {\"dateTime\": \"2026-06-01T13:00:00.0000000\", \"timeZone\": \"UTC\"},"
    "\"end\": {\"dateTime\": \"2026-06-01T14:00:00.0000000\", \"timeZone\": \"UTC\"},"
    "\"location\": {\"displayName\": \"Room 4\"},"
    "\"isAllDay\": false,"
    "\"showAs\": \"busy\","
    "\"sensitivity\": \"private\","
    "\"responseRequested\": true,"
    "\"organizer\": {\"messageType\": null, \"eventMessageType\": \"meetingRequest\", "
    "\"from\": {\"emailAddress\": {\"name\": \"Alice\", \"address\": \"alice@example.com\"}}},"
    "\"attendees\": ["
    "  {\"type\": \"required\", \"status\": {\"response\": \"accepted\", \"time\": \"2026-05-30T14:32:11Z\"}, "
    "\"emailAddress\": {\"name\": \"Bob\", \"address\": \"bob@example.com\"}},"
    "  {\"type\": \"optional\", \"status\": {\"response\": \"tentative\", \"time\": \"2026-05-30T14:32:11Z\"}, "
    "\"emailAddress\": {\"name\": \"Carol\", \"address\": \"carol@example.com\"}}"
    "]"
    "}";

// Rich Google People contact (Phase-3 shapes).
const QByteArray kGooglePerson =
    "{"
    "\"resourceName\": \"people/c111122223333\","
    "\"etag\": \"%EgcBAgkuNz0+GgQBAgUHIgwzb1plTGl3b2Judz0=\","
    "\"names\": [{"
    "  \"displayName\": \"Alice Example\", \"givenName\": \"Alice\", "
    "  \"familyName\": \"Example\","
    "  \"metadata\": {\"primary\": true, \"sourcePrimary\": true}"
    "}],"
    "\"emailAddresses\": [{"
    "  \"value\": \"alice@example.com\", \"type\": \"home\","
    "  \"metadata\": {\"primary\": true}"
    "}],"
    "\"phoneNumbers\": [{"
    "  \"value\": \"+1555-0100\", \"canonicalForm\": \"+15550100\", \"type\": \"mobile\""
    "}],"
    "\"addresses\": [{"
    "  \"streetAddress\": \"1 Main St\", \"city\": \"Springfield\", "
    "  \"region\": \"IL\", \"postalCode\": \"62701\", \"formattedValue\": \"1 Main St\", \"type\": \"work\""
    "}],"
    "\"organizations\": [{\"name\": \"Acme\", \"title\": \"Engineer\", \"current\": true}],"
    "\"urls\": [{\"value\": \"https://alice.example\", \"type\": \"blog\"}],"
    "\"clientData\": []"
    "}";

// Rich Graph contact (ms-contact suite shapes).
const QByteArray kMsContact =
    "{"
    "\"id\": \"AAMkAGZlMjNkNGU0\","
    "\"displayName\": \"Bob Sample\","
    "\"givenName\": \"Bob\","
    "\"surname\": \"Sample\","
    "\"fileAs\": \"Sample, Bob\","
    "\"emailAddresses\": [{\"address\": \"bob@example.com\", \"name\": \"Bob Sample\"}],"
    "\"primaryEmailAddress\": {\"address\": \"bob@example.com\", \"name\": \"Bob Sample\"},"
    "\"businessPhones\": [\"+1 555 0100\"],"
    "\"mobilePhone\": \"+1 555 0102\","
    "\"homeAddress\": {\"street\": \"1 Main St\", \"city\": \"Springfield\", \"state\": \"IL\"},"
    "\"companyName\": \"Acme\","
    "\"jobTitle\": \"Engineer\","
    "\"personalNotes\": \"Met at conf\","
    "\"birthday\": \"1990-08-23T00:00:00Z\","
    "\"categories\": [\"CORPUS\"]"
    "}";

QJsonObject deepObj(const QJsonObject& o)
{
    return o;
}

/// Top-level keys whose JSON values differ between two canon objects
/// (excluding the envelope + provider-extras transport bags).
QStringList diffKeys(const QJsonObject& a, const QJsonObject& b)
{
    QSet<QString> keys;
    for (auto it = a.constBegin(); it != a.constEnd(); ++it)
        keys.insert(it.key());
    for (auto it = b.constBegin(); it != b.constEnd(); ++it)
        keys.insert(it.key());
    for (const QString& e : kExcludedKeys)
        keys.remove(e);

    QStringList out;
    for (const QString& k : keys) {
        const bool inA = a.contains(k);
        const bool inB = b.contains(k);
        if (inA != inB || deepObj(a).value(k) != deepObj(b).value(k))
            out << k;
    }
    std::sort(out.begin(), out.end());
    return out;
}

void reportAndAssertWithin(const QString& context,
                           const QJsonObject& before,
                           const QJsonObject& after,
                           const LossProfile& declared)
{
    const QStringList diffs = diffKeys(before, after);
    for (const QString& key : diffs) {
        const QString kind = declared.affected.contains(PropertyId{key})
                                 ? QLatin1String(
                                       ConvergenceMatrix::lossKindName(
                                           declared.affected.value(
                                               PropertyId{key})))
                                 : QStringLiteral("<UNDECLARED>");
        qInfo("%s: diff %s (%s)", qPrintable(context), qPrintable(key),
              qPrintable(kind));
    }
    for (const QString& key : diffs) {
        QVERIFY2(declared.affected.contains(PropertyId{key}),
                 qPrintable(QStringLiteral("%1: property '%2' diverged across "
                                          "the crossing but is UNDECLARED")
                                .arg(context, key)));
    }
}

} // namespace

class TestGmPipelineConvergence : public QObject {
    Q_OBJECT
private slots:

    // calendar: google-event → canon → ms-event → canon
    void eventCrossingGoogleToMsStaysDeclared()
    {
        Kalburator::Calendar::GoogleEventToCanonStage gPromote;
        Kalburator::Calendar::CanonToMsEventStage msDemote;
        Kalburator::Calendar::MsEventToCanonStage msPromote;

        const QJsonObject canon0 = parse(gPromote.transform(kGoogleEvent));
        QVERIFY2(!canon0.isEmpty(), "google promote failed");
        const QByteArray wire = msDemote.transform(serialize(canon0));
        QVERIFY2(!wire.isEmpty(), "ms demote returned empty");
        const QJsonObject canon1 = parse(msPromote.transform(wire));
        QVERIFY2(!canon1.isEmpty(), "ms re-promote returned empty");

        reportAndAssertWithin(QStringLiteral("calendar G→MS"),
                              canon0, canon1,
                              Kalburator::Calendar::canonToMsEventLoss());
    }

    // calendar: ms-event → canon → google-event → canon
    void eventCrossingMsToGoogleStaysDeclared()
    {
        Kalburator::Calendar::MsEventToCanonStage msPromote;
        Kalburator::Calendar::CanonToGoogleEventStage gDemote;
        Kalburator::Calendar::GoogleEventToCanonStage gPromote;

        const QJsonObject canon0 = parse(msPromote.transform(kMsEvent));
        QVERIFY2(!canon0.isEmpty(), "ms promote failed");
        const QByteArray wire = gDemote.transform(serialize(canon0));
        QVERIFY2(!wire.isEmpty(), "google demote returned empty");
        const QJsonObject canon1 = parse(gPromote.transform(wire));
        QVERIFY2(!canon1.isEmpty(), "google re-promote returned empty");

        reportAndAssertWithin(QStringLiteral("calendar MS→G"),
                              canon0, canon1,
                              Kalburator::Calendar::canonToGoogleEventLoss());
    }

    // contacts: google-person → canon → ms-contact → canon
    void contactCrossingGoogleToMsStaysDeclared()
    {
        Kalburator::Contacts::GooglePersonToCanonStage gPromote;
        Kalburator::Contacts::CanonToMsContactStage msDemote;
        Kalburator::Contacts::MsContactToCanonStage msPromote;

        const QJsonObject canon0 = parse(gPromote.transform(kGooglePerson));
        QVERIFY2(!canon0.isEmpty(), "person promote failed");
        const QByteArray wire = msDemote.transform(serialize(canon0));
        QVERIFY2(!wire.isEmpty(), "contact demote returned empty");
        const QJsonObject canon1 = parse(msPromote.transform(wire));
        QVERIFY2(!canon1.isEmpty(), "contact re-promote returned empty");

        reportAndAssertWithin(QStringLiteral("contacts G→MS"),
                              canon0, canon1,
                              Kalburator::Contacts::canonToMsContactLoss());
    }

    // contacts: ms-contact → canon → google-person → canon
    void contactCrossingMsToGoogleStaysDeclared()
    {
        Kalburator::Contacts::MsContactToCanonStage msPromote;
        Kalburator::Contacts::CanonToGooglePersonStage gDemote;
        Kalburator::Contacts::GooglePersonToCanonStage gPromote;

        const QJsonObject canon0 = parse(msPromote.transform(kMsContact));
        QVERIFY2(!canon0.isEmpty(), "contact promote failed");
        const QByteArray wire = gDemote.transform(serialize(canon0));
        QVERIFY2(!wire.isEmpty(), "person demote returned empty");
        const QJsonObject canon1 = parse(gPromote.transform(wire));
        QVERIFY2(!canon1.isEmpty(), "person re-promote returned empty");

        reportAndAssertWithin(QStringLiteral("contacts MS→G"),
                              canon0, canon1,
                              Kalburator::Contacts::canonToGooglePersonLoss());
    }

    // todo: superset canon crossed BOTH directions. Source canon here is
    // hand-built (no task fixtures yet — corpus capture deferred), which is
    // exactly the interesting case: the superset exercises every ruling.
    void todoCrossingsStayDeclared()
    {
        auto makeSupersetTodo = []() {
            QJsonObject obj;
            obj.insert(QStringLiteral("uid"), QStringLiteral("task-superset"));
            obj.insert(QStringLiteral("summary"), QStringLiteral("Ship EEE"));
            obj.insert(QStringLiteral("description"), QStringLiteral("Cut release"));
            obj.insert(QStringLiteral("status"), QStringLiteral("needsAction"));
            obj.insert(QStringLiteral("priority"), 9);
            obj.insert(QStringLiteral("percentComplete"), 40);
            obj.insert(QStringLiteral("categories"),
                       QJsonArray{ QStringLiteral("work") });
            obj.insert(QStringLiteral("due"),
                       QJsonObject{ { QStringLiteral("date"),
                                      QStringLiteral("2026-09-01") },
                                    { QStringLiteral("allDay"), true } });
            obj.insert(QStringLiteral("completed"),
                       QStringLiteral("2026-08-23T11:00:00Z"));
            obj.insert(QStringLiteral("recurrence"),
                       QJsonArray{ QStringLiteral("RRULE:FREQ=DAILY;INTERVAL=1") });
            stampEnvelope(obj, QStringLiteral("todo"),
                          QStringLiteral("task-superset"));
            return obj;
        };

        // → google-task →
        {
            Kalburator::Todo::CanonToGoogleTaskStage gDemote;
            Kalburator::Todo::GoogleTaskToCanonStage gPromote;
            const QJsonObject canon0 = makeSupersetTodo();
            const QByteArray wire = gDemote.transform(serialize(canon0));
            QVERIFY2(!wire.isEmpty(), "google-task demote returned empty");
            const QJsonObject canon1 = parse(gPromote.transform(wire));
            QVERIFY2(!canon1.isEmpty(), "google-task re-promote returned empty");
            reportAndAssertWithin(
                QStringLiteral("todo →G"),
                canon0, canon1,
                Kalburator::Todo::canonToGoogleTaskLoss());
        }
        // → ms-todotask →
        {
            Kalburator::Todo::CanonToMsTodoTaskStage msDemote;
            Kalburator::Todo::MsTodoTaskToCanonStage msPromote;
            const QJsonObject canon0 = makeSupersetTodo();
            const QByteArray wire = msDemote.transform(serialize(canon0));
            QVERIFY2(!wire.isEmpty(), "ms-todotask demote returned empty");
            const QJsonObject canon1 = parse(msPromote.transform(wire));
            QVERIFY2(!canon1.isEmpty(), "ms-todotask re-promote returned empty");
            reportAndAssertWithin(
                QStringLiteral("todo →MS"),
                canon0, canon1,
                Kalburator::Todo::canonToMsTodoTaskLoss());
        }
    }

    // The committed ledger matches regeneration byte-for-byte (O63 rule).
    void committedMatrixMatchesGenerated()
    {
        QFile f(QLatin1String(KALBURATOR_EEE_DOC_DIR)
                + QStringLiteral("/CONVERGENCE-MATRIX.md"));
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(f.errorString()));
        const QString committed = QString::fromUtf8(f.readAll());

        const Kalburator::Calendar::CalendarStockShapes calendar;
        const Kalburator::Contacts::ContactsStockShapes contacts;
        const Kalburator::Todo::TodoStockShapes todo;
        const QString generated = ConvergenceMatrix::generate(
            { { QStringLiteral("calendar"), &calendar },
              { QStringLiteral("contacts"), &contacts },
              { QStringLiteral("todo"), &todo } });

        if (committed != generated) {
            qInfo("Committed matrix differs from regeneration — run:\n"
                  "  ./build/tools/matrixgen/matrixgen %s",
                  qPrintable(f.fileName()));
        }
        QCOMPARE(committed, generated);
    }
};

QTEST_MAIN(TestGmPipelineConvergence)
#include "tst_gm_pipeline_convergence.moc"
