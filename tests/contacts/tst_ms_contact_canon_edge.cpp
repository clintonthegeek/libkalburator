// EEE Phase 3 — ms-contact ⇄ contacts-canon edge suite. Pins the declared
// loss profile (docs/2026-08-23-ms-contact-edge-loss-profile.md): promote
// from a rich wire object modeled on reference §2.2 + the committed
// fixture's shapes, declared-vs-actual demote walk, round-trip identity,
// registry inspection, and committed-fixture promotion (every sanitized
// connection in contacts-listing.json promotes cleanly).
//
// NOTE: no terminated raw string literals in this TU (O59 moc tooling rule).

#include <QTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "canonenvelope.h"
#include "mscontactcanonstages.h"
#include "contactsdomaindefinition.h"
#include "contactsstockshapes.h"
#include "shaperegistries.h"
#include "lossprofile.h"

using Kalburator::Shape::CanonEnvelope::parse;
using Kalburator::Shape::CanonEnvelope::serialize;
using Kalburator::Shape::CanonEnvelope::stampEnvelope;
using Kalburator::Shape::CanonEnvelope::providerExtrasKey;
using Kalburator::Contacts::MsContactToCanonStage;
using Kalburator::Contacts::CanonToMsContactStage;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::LossKind;
using Kalburator::Shape::Shape;

namespace {

Kalburator::Shape::ShapeRegistries makeContactsRegistries()
{
    Kalburator::Shape::ShapeRegistries regs;
    auto& reg = regs.transformation;

    Kalburator::Contacts::ContactsDomainDefinition def;
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

    Kalburator::Contacts::ContactsStockShapes shapes;
    for (const auto& [shape, cat] : shapes.peerShapes())
        reg.registerShape(shape, cat);
    for (const auto& edge : shapes.edges())
        reg.registerEdge(edge);

    return regs;
}

const QByteArray kRichContact =
    "{"
    "\"@odata.etag\": \"W/\\\"CQAAABYAAAImBx1m\\\"\","
    "\"id\": \"AAMkAGZlMjNkNGU0\","
    "\"changeKey\": \"EQAAABYAAAIm\","
    "\"displayName\": \"Bob Sample\","
    "\"givenName\": \"Bob\","
    "\"surname\": \"Sample\","
    "\"middleName\": \"Q\","
    "\"nickName\": \"Bobby\","
    "\"title\": \"Mr.\","
    "\"fileAs\": \"Sample, Bob\","
    "\"generation\": \"Jr.\","
    "\"yomiGivenName\": \"bob-yomi\","
    "\"yomiSurname\": \"sample-yomi\","
    "\"emailAddresses\": ["
    "  {\"address\": \"bob@example.com\", \"name\": \"Bob Sample\"},"
    "  {\"address\": \"bob2@example.com\", \"name\": \"Bob Two\"}"
    "],"
    "\"primaryEmailAddress\": {\"address\": \"bob@example.com\", \"name\": \"Bob Sample\"},"
    "\"businessPhones\": [\"+1 555 0100\"],"
    "\"homePhones\": [\"+1 555 0101\"],"
    "\"mobilePhone\": \"+1 555 0102\","
    "\"imAddresses\": [\"bob@im\"],"
    "\"homeAddress\": {\"street\": \"1 Main St\", \"city\": \"Springfield\", "
    "\"state\": \"IL\", \"countryOrRegion\": \"USA\", \"postalCode\": \"62701\"},"
    "\"businessAddress\": {},"
    "\"otherAddress\": {},"
    "\"companyName\": \"Acme\","
    "\"department\": \"R&D\","
    "\"jobTitle\": \"Engineer\","
    "\"officeLocation\": \"HQ/4\","
    "\"profession\": \"Engineering\","
    "\"businessHomePage\": \"https://acme.example/~bob\","
    "\"assistantName\": \"Carol Assist\","
    "\"manager\": \"Dave Manager\","
    "\"spouseName\": \"Erin Spouse\","
    "\"children\": [\"Finn\"],"
    "\"personalNotes\": \"Met at conf\","
    "\"birthday\": \"1990-08-23T00:00:00Z\","
    "\"categories\": [\"CORPUS\"],"
    "\"createdDateTime\": \"2026-08-23T06:06:07Z\","
    "\"lastModifiedDateTime\": \"2026-08-23T06:06:07Z\","
    "\"parentFolderId\": \"FOLDERID\""
    "}";

} // namespace

class TestMsContactCanonEdge : public QObject {
    Q_OBJECT
private slots:

    // Promote: the flat Graph scalars land in canon groups; unmapped
    // top-level fields land verbatim under providerExtras["msgraph"].
    void promoteRichContactIsLossless()
    {
        MsContactToCanonStage stage;
        const QJsonObject canon = parse(stage.transform(kRichContact));
        QVERIFY2(!canon.isEmpty(), "promote returned empty canon");

        QCOMPARE(canon.value(QStringLiteral("uid")).toString(),
                 QStringLiteral("AAMkAGZlMjNkNGU0"));

        const QJsonArray names = canon.value(QStringLiteral("names")).toArray();
        QCOMPARE(names.size(), 1);
        const QJsonObject n = names.at(0).toObject();
        QCOMPARE(n.value(QStringLiteral("formatted")).toString(),
                 QStringLiteral("Bob Sample"));
        QCOMPARE(n.value(QStringLiteral("given")).toString(),
                 QStringLiteral("Bob"));
        QCOMPARE(n.value(QStringLiteral("family")).toString(),
                 QStringLiteral("Sample"));
        QCOMPARE(n.value(QStringLiteral("fileAs")).toString(),
                 QStringLiteral("Sample, Bob"));
        QCOMPARE(n.value(QStringLiteral("phoneticGiven")).toString(),
                 QStringLiteral("bob-yomi"));

        const QJsonArray emails = canon.value(QStringLiteral("emails")).toArray();
        QCOMPARE(emails.size(), 2);
        QCOMPARE(emails.at(0).toObject().value(QStringLiteral("value")).toString(),
                 QStringLiteral("bob@example.com"));
        QCOMPARE(emails.at(0).toObject().value(QStringLiteral("primary")).toBool(),
                 true);

        const QJsonArray phones = canon.value(QStringLiteral("phones")).toArray();
        QCOMPARE(phones.size(), 3);
        QCOMPARE(phones.at(0).toObject().value(QStringLiteral("type")).toString(),
                 QStringLiteral("home"));

        QCOMPARE(canon.value(QStringLiteral("addresses")).toArray()
                     .at(0).toObject()
                     .value(QStringLiteral("region")).toString(),
                 QStringLiteral("IL"));

        QCOMPARE(canon.value(QStringLiteral("organizations")).toArray()
                     .at(0).toObject()
                     .value(QStringLiteral("name")).toString(),
                 QStringLiteral("Acme"));
        QCOMPARE(canon.value(QStringLiteral("occupations")).toArray().at(0).toString(),
                 QStringLiteral("Engineering"));
        QCOMPARE(canon.value(QStringLiteral("urls")).toArray()
                     .at(0).toObject()
                     .value(QStringLiteral("value")).toString(),
                 QStringLiteral("https://acme.example/~bob"));

        const QJsonArray relations =
            canon.value(QStringLiteral("relations")).toArray();
        QCOMPARE(relations.size(), 4);
        QCOMPARE(relations.at(3).toObject().value(QStringLiteral("type")).toString(),
                 QStringLiteral("child"));

        QCOMPARE(canon.value(QStringLiteral("notes")).toString(),
                 QStringLiteral("Met at conf"));
        QCOMPARE(canon.value(QStringLiteral("birthday")).toObject()
                     .value(QStringLiteral("dateTime")).toString(),
                 QStringLiteral("1990-08-23T00:00:00Z"));
        QCOMPARE(canon.value(QStringLiteral("categories")).toArray().at(0).toString(),
                 QStringLiteral("CORPUS"));

        // @odata.etag + changeKey + timestamps stash under providerExtras
        const QJsonObject extras = canon.value(providerExtrasKey())
                                       .toObject()
                                       .value(QStringLiteral("msgraph"))
                                       .toObject();
        QVERIFY(!extras.value(QStringLiteral("changeKey")).toString().isEmpty());
        QVERIFY(!extras.value(QStringLiteral("@odata.etag")).toString().isEmpty());
        QVERIFY(!extras.value(QStringLiteral("createdDateTime")).toString().isEmpty());
    }

    // Declared-loss walk: demote honors each Simplified/Reversible ruling.
    void demoteDeclaredLossMatchesReality()
    {
        CanonToMsContactStage stage;

        auto makeCanon = []() {
            QJsonObject obj;
            obj.insert(QStringLiteral("uid"),
                       QStringLiteral("AAMkCANON999"));
            stampEnvelope(obj, QStringLiteral("contacts"),
                          QStringLiteral("AAMkCANON999"));
            return obj;
        };

        // names[0] collapses onto the flat scalars
        {
            QJsonObject c = makeCanon();
            c.insert(QStringLiteral("names"),
                     QJsonArray{ QJsonObject{
                         { QStringLiteral("formatted"), QStringLiteral("Ada L") },
                         { QStringLiteral("given"), QStringLiteral("Ada") },
                         { QStringLiteral("family"), QStringLiteral("Lovelace") } } });
            const QJsonObject out = parse(stage.transform(serialize(c)));
            QCOMPARE(out.value(QStringLiteral("displayName")).toString(),
                     QStringLiteral("Ada L"));
            QCOMPARE(out.value(QStringLiteral("givenName")).toString(),
                     QStringLiteral("Ada"));
            QCOMPARE(out.value(QStringLiteral("surname")).toString(),
                     QStringLiteral("Lovelace"));
            QVERIFY(!out.contains(QStringLiteral("displayName2")));
        }

        // typed phone rows collapse into the fixed buckets
        {
            QJsonObject c = makeCanon();
            c.insert(QStringLiteral("phones"),
                     QJsonArray{
                         QJsonObject{ { QStringLiteral("value"), QStringLiteral("+15550101") },
                                      { QStringLiteral("type"), QStringLiteral("home") } },
                         QJsonObject{ { QStringLiteral("value"), QStringLiteral("+15550102") },
                                      { QStringLiteral("type"), QStringLiteral("mobile") } } });
            const QJsonObject out = parse(stage.transform(serialize(c)));
            QCOMPARE(out.value(QStringLiteral("homePhones")).toArray().size(), 1);
            QCOMPARE(out.value(QStringLiteral("mobilePhone")).toString(),
                     QStringLiteral("+15550102"));
            QVERIFY(!out.contains(QStringLiteral("businessPhones")));
        }

        // gender: no GA contact home → open-extension carrier
        {
            QJsonObject c = makeCanon();
            c.insert(QStringLiteral("gender"),
                     QJsonObject{ { QStringLiteral("value"), QStringLiteral("female") } });
            const QJsonObject out = parse(stage.transform(serialize(c)));
            bool foundCarrier = false;
            for (const auto& ev : out.value(QStringLiteral("extensions")).toArray()) {
                const QJsonObject ext = ev.toObject();
                if (ext.value(QStringLiteral("extensionName")).toString()
                    == QLatin1String("kalburator.canon")) {
                    QVERIFY(ext.contains(QLatin1String("x-canon-gender")));
                    foundCarrier = true;
                }
            }
            QVERIFY2(foundCarrier, "gender must ride a kalburator.canon carrier");
        }
    }

    // C→MS→C byte-equal identity for the lossless + carrier set.
    void losslessRoundTripIsIdentity()
    {
        QJsonObject canon;
        canon.insert(QStringLiteral("uid"), QStringLiteral("AAMkRT777"));
        stampEnvelope(canon, QStringLiteral("contacts"), QStringLiteral("AAMkRT777"));
        {
            QJsonArray names;
            names.append(QJsonObject{
                { QStringLiteral("formatted"), QStringLiteral("Bob Sample") },
                { QStringLiteral("given"), QStringLiteral("Bob") },
                { QStringLiteral("family"), QStringLiteral("Sample") },
                { QStringLiteral("fileAs"), QStringLiteral("Sample, Bob") } });
            canon.insert(QStringLiteral("names"), names);
        }
        {
            QJsonArray emails;
            emails.append(QJsonObject{
                { QStringLiteral("value"), QStringLiteral("bob@example.com") },
                { QStringLiteral("primary"), true } });
            canon.insert(QStringLiteral("emails"), emails);
        }
        {
            QJsonArray phones;
            phones.append(QJsonObject{
                { QStringLiteral("value"), QStringLiteral("+15550102") },
                { QStringLiteral("type"), QStringLiteral("mobile") } });
            canon.insert(QStringLiteral("phones"), phones);
        }
        {
            QJsonArray addresses;
            addresses.append(QJsonObject{
                { QStringLiteral("street"), QStringLiteral("1 Main St") },
                { QStringLiteral("city"), QStringLiteral("Springfield") },
                { QStringLiteral("type"), QStringLiteral("home") } });
            canon.insert(QStringLiteral("addresses"), addresses);
        }
        {
            QJsonArray imClients;
            imClients.append(QJsonObject{
                { QStringLiteral("username"), QStringLiteral("bob@im") } });
            canon.insert(QStringLiteral("imClients"), imClients);
        }
        canon.insert(QStringLiteral("notes"), QStringLiteral("Met at conf"));
        canon.insert(
            QStringLiteral("birthday"),
            QJsonObject{ { QStringLiteral("dateTime"),
                           QStringLiteral("1990-08-23T00:00:00Z") } });
        canon.insert(QStringLiteral("categories"),
                     QJsonArray{ QStringLiteral("CORPUS") });
        // carrier-routed prop (Reversible)
        canon.insert(QStringLiteral("gender"),
                     QJsonObject{ { QStringLiteral("value"), QStringLiteral("female") } });

        CanonToMsContactStage demote;
        MsContactToCanonStage promote;
        const QByteArray wireBytes = demote.transform(serialize(canon));
        QVERIFY2(!wireBytes.isEmpty(), "demote returned empty bytes");
        const QByteArray roundTripped = promote.transform(wireBytes);
        QVERIFY2(!roundTripped.isEmpty(), "re-promote returned empty bytes");
        QCOMPARE(roundTripped, serialize(canon));
    }

    // Registry inspection: both directions registered; demote declared lossy.
    void inspectDeclaresMsContactEdge()
    {
        const auto regs = makeContactsRegistries();
        const Shape canon{ DomainId{QStringLiteral("contacts")},
                           EncodingId{QStringLiteral("canon")} };
        const Shape ms{ DomainId{QStringLiteral("contacts")},
                        EncodingId{QStringLiteral("ms-contact")} };

        const auto loss = regs.transformation.inspect(canon, ms);
        QVERIFY2(!loss.isLossless(), "canon->ms-contact must be declared lossy");
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("names")}),
                 LossKind::Simplified);
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("gender")}),
                 LossKind::Reversible);
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("photos")}),
                 LossKind::Dropped);

        const auto promoteLoss = regs.transformation.inspect(ms, canon);
        QVERIFY2(promoteLoss.isLossless(), "promote must be lossless");
    }

    // Committed live-capture fixture: every sanitized connection promotes.
    void promoteCommittedLiveFixture()
    {
        QFile f(QLatin1String(KALBURATOR_VENDOR_FIXTURE_DIR)
                + QStringLiteral("/microsoft/contacts-listing.json"));
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(f.errorString()));
        const QJsonArray value =
            QJsonDocument::fromJson(f.readAll())
                .object()
                .value(QStringLiteral("value"))
                .toArray();
        QVERIFY2(value.size() >= 5, "fixture should hold its captured page");

        MsContactToCanonStage stage;
        for (const auto& cv : value) {
            const QJsonObject canon = parse(stage.transform(
                QJsonDocument(cv.toObject()).toJson(QJsonDocument::Compact)));
            QVERIFY2(!canon.isEmpty(), "promote returned empty canon");
            QVERIFY(!canon.value(QStringLiteral("uid")).toString().isEmpty());
        }
    }
};

QTEST_MAIN(TestMsContactCanonEdge)
#include "tst_ms_contact_canon_edge.moc"
