// EEE Phase 3 — google-person ⇄ contacts-canon edge suite. Pins the
// declared loss profile (docs/2026-08-23-google-person-edge-loss-profile.md):
// promote from a rich wire object modeled on reference §2.1 + the committed
// fixture's shapes, declared-vs-actual demote walk, round-trip identity,
// registry inspection, and committed-fixture promotion (all 72 sanitized
// connections promote cleanly).
//
// NOTE: no terminated raw string literals in this TU (O59 moc tooling rule).

#include <QTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "canonenvelope.h"
#include "googlepersoncanonstages.h"
#include "contactsdomaindefinition.h"
#include "contactsstockshapes.h"
#include "shaperegistries.h"
#include "lossprofile.h"

using Kalburator::Shape::CanonEnvelope::parse;
using Kalburator::Shape::CanonEnvelope::serialize;
using Kalburator::Shape::CanonEnvelope::stampEnvelope;
using Kalburator::Shape::CanonEnvelope::providerExtrasKey;
using Kalburator::Contacts::GooglePersonToCanonStage;
using Kalburator::Contacts::CanonToGooglePersonStage;
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

const QByteArray kRichPerson =
    "{"
    "\"resourceName\": \"people/c111122223333\","
    "\"etag\": \"%EgcBAgkuNz0+GgQBAgUHIgwzb1plTGl3b2Judz0=\","
    "\"metadata\": {\"objectType\": \"PERSON\", \"sources\": [{\"id\": \"42\", \"type\": \"CONTACT\"}]},"
    "\"names\": [{"
    "  \"displayName\": \"Alice Example\", \"givenName\": \"Alice\", "
    "  \"familyName\": \"Example\", \"unstructuredName\": \"Alice Example\","
    "  \"metadata\": {\"primary\": true, \"sourcePrimary\": true}"
    "}],"
    "\"emailAddresses\": [{"
    "  \"value\": \"alice@example.com\", \"type\": \"home\", \"formattedType\": \"Home\","
    "  \"metadata\": {\"primary\": true}"
    "}],"
    "\"phoneNumbers\": [{"
    "  \"value\": \"+1555-0100\", \"canonicalForm\": \"+15550100\", \"type\": \"mobile\""
    "}],"
    "\"addresses\": [{"
    "  \"streetAddress\": \"1 Main St\", \"city\": \"Springfield\", "
    "  \"region\": \"IL\", \"postalCode\": \"62701\", \"country\": \"USA\", "
    "  \"countryCode\": \"US\", \"formattedValue\": \"1 Main St, Springfield\", \"type\": \"work\""
    "}],"
    "\"organizations\": [{"
    "  \"name\": \"Acme\", \"title\": \"Engineer\", \"department\": \"R&D\", \"current\": true,"
    "  \"metadata\": {\"primary\": true}"
    "}],"
    "\"birthdays\": [{\"date\": {\"year\": 1990, \"month\": 8, \"day\": 23}}],"
    "\"genders\": [{\"value\": \"female\", \"addressMeAs\": \"she\"}],"
    "\"biographies\": [{\"value\": \"Old friend\", \"metadata\": {\"primary\": true}}],"
    "\"urls\": [{\"value\": \"https://alice.example\", \"type\": \"blog\"}],"
    "\"interests\": [{\"value\": \"hiking\"}],"
    "\"skills\": [{\"value\": \"carpentry\"}],"
    "\"occupations\": [{\"value\": \"Engineer\"}],"
    "\"locales\": [{\"value\": \"en-US\"}],"
    "\"sipAddresses\": [{\"value\": \"sip:alice@example.com\"}],"
    "\"clientData\": []"
    "}";

} // namespace

class TestGooglePersonCanonEdge : public QObject {
    Q_OBJECT
private slots:

    // Promote: every mapped group lands in canon; unmapped top-level fields
    // land verbatim under providerExtras["google"].
    void promoteRichPersonIsLossless()
    {
        GooglePersonToCanonStage stage;
        const QJsonObject canon =
            parse(stage.transform(kRichPerson));
        QVERIFY2(!canon.isEmpty(), "promote returned empty canon");

        QCOMPARE(canon.value(QStringLiteral("uid")).toString(),
                 QStringLiteral("people/c111122223333"));

        const QJsonArray names = canon.value(QStringLiteral("names")).toArray();
        QCOMPARE(names.size(), 1);
        const QJsonObject n = names.at(0).toObject();
        QCOMPARE(n.value(QStringLiteral("formatted")).toString(),
                 QStringLiteral("Alice Example"));
        QCOMPARE(n.value(QStringLiteral("given")).toString(),
                 QStringLiteral("Alice"));
        QCOMPARE(n.value(QStringLiteral("family")).toString(),
                 QStringLiteral("Example"));
        QCOMPARE(n.value(QStringLiteral("primary")).toBool(), true);

        const QJsonArray emails = canon.value(QStringLiteral("emails")).toArray();
        QCOMPARE(emails.size(), 1);
        QCOMPARE(emails.at(0).toObject().value(QStringLiteral("value")).toString(),
                 QStringLiteral("alice@example.com"));
        QCOMPARE(emails.at(0).toObject().value(QStringLiteral("primary")).toBool(), true);
        QCOMPARE(emails.at(0).toObject().value(QStringLiteral("type")).toString(),
                 QStringLiteral("home"));

        QCOMPARE(canon.value(QStringLiteral("phones")).toArray()
                     .at(0).toObject()
                     .value(QStringLiteral("canonicalForm")).toString(),
                 QStringLiteral("+15550100"));

        QCOMPARE(canon.value(QStringLiteral("addresses")).toArray()
                     .at(0).toObject()
                     .value(QStringLiteral("street")).toString(),
                 QStringLiteral("1 Main St"));

        QCOMPARE(canon.value(QStringLiteral("organizations")).toArray()
                     .at(0).toObject()
                     .value(QStringLiteral("title")).toString(),
                 QStringLiteral("Engineer"));

        QCOMPARE(canon.value(QStringLiteral("birthday")).toObject()
                     .value(QStringLiteral("date")).toObject()
                     .value(QStringLiteral("year")).toInt(), 1990);
        QCOMPARE(canon.value(QStringLiteral("notes")).toString(),
                 QStringLiteral("Old friend"));
        QCOMPARE(canon.value(QStringLiteral("gender")).toObject()
                     .value(QStringLiteral("value")).toString(),
                 QStringLiteral("female"));
        QCOMPARE(canon.value(QStringLiteral("interests")).toArray()
                     .at(0).toString(),
                 QStringLiteral("hiking"));
        QCOMPARE(canon.value(QStringLiteral("languages")).toArray()
                     .at(0).toString(),
                 QStringLiteral("en-US"));

        // etag + metadata stash under providerExtras
        const QJsonObject extras = canon.value(providerExtrasKey())
                                       .toObject()
                                       .value(QStringLiteral("google"))
                                       .toObject();
        QVERIFY(!extras.value(QStringLiteral("etag")).toString().isEmpty());
        QVERIFY(!extras.value(QStringLiteral("metadata")).toObject().isEmpty());
    }

    // Declared-loss walk: demote honors each Simplified/Reversible ruling.
    void demoteDeclaredLossMatchesReality()
    {
        CanonToGooglePersonStage stage;

        auto makeCanon = []() {
            QJsonObject obj;
            obj.insert(QStringLiteral("uid"),
                       QStringLiteral("people/c999"));
            stampEnvelope(obj, QStringLiteral("contacts"), QStringLiteral("people/c999"));
            return obj;
        };

        // birthday singular → one row
        {
            QJsonObject c = makeCanon();
            c.insert(QStringLiteral("birthday"),
                     QJsonObject{ { QStringLiteral("date"),
                                    QJsonObject{ { QStringLiteral("year"), 1980 },
                                                 { QStringLiteral("month"), 1 },
                                                 { QStringLiteral("day"), 2 } } } });
            const QJsonObject out = parse(stage.transform(serialize(c)));
            QCOMPARE(out.value(QStringLiteral("birthdays")).toArray().size(), 1);
            QCOMPARE(out.value(QStringLiteral("birthdays")).toArray()
                         .at(0).toObject()
                         .value(QStringLiteral("date")).toObject()
                         .value(QStringLiteral("year")).toInt(), 1980);
        }

        // notes → biographies row
        {
            QJsonObject c = makeCanon();
            c.insert(QStringLiteral("notes"), QStringLiteral("Met at conf"));
            const QJsonObject out = parse(stage.transform(serialize(c)));
            QCOMPARE(out.value(QStringLiteral("biographies")).toArray()
                         .at(0).toObject()
                         .value(QStringLiteral("value")).toString(),
                     QStringLiteral("Met at conf"));
        }

        // categories: no Google home → clientData carrier
        {
            QJsonObject c = makeCanon();
            c.insert(QStringLiteral("categories"),
                     QJsonArray{ QStringLiteral("Friends") });
            const QJsonObject out = parse(stage.transform(serialize(c)));
            bool foundCarrier = false;
            for (const auto& cv :
                 out.value(QStringLiteral("clientData")).toArray()) {
                if (cv.toObject().value(QStringLiteral("key")).toString()
                    == QLatin1String("x-canon-categories")) {
                    QVERIFY(cv.toObject().value(QStringLiteral("value"))
                                .toString()
                                .contains(QLatin1String("Friends")));
                    foundCarrier = true;
                }
            }
            QVERIFY2(foundCarrier, "categories must ride a clientData carrier");
        }
    }

    // C→G→C byte-equal identity for the lossless + carrier set.
    void losslessRoundTripIsIdentity()
    {
        QJsonObject canon;
        canon.insert(QStringLiteral("uid"), QStringLiteral("people/c777"));
        stampEnvelope(canon, QStringLiteral("contacts"), QStringLiteral("people/c777"));
        {
            QJsonArray names;
            names.append(QJsonObject{
                { QStringLiteral("formatted"), QStringLiteral("Bob Sample") },
                { QStringLiteral("given"), QStringLiteral("Bob") },
                { QStringLiteral("family"), QStringLiteral("Sample") },
                { QStringLiteral("primary"), true } });
            canon.insert(QStringLiteral("names"), names);
        }
        {
            QJsonArray emails;
            emails.append(QJsonObject{
                { QStringLiteral("value"), QStringLiteral("bob@example.com") },
                { QStringLiteral("primary"), true },
                { QStringLiteral("type"), QStringLiteral("work") } });
            canon.insert(QStringLiteral("emails"), emails);
        }
        {
            QJsonArray phones;
            phones.append(QJsonObject{
                { QStringLiteral("value"), QStringLiteral("+1555-0111") },
                { QStringLiteral("canonicalForm"), QStringLiteral("+15550111") },
                { QStringLiteral("type"), QStringLiteral("mobile") } });
            canon.insert(QStringLiteral("phones"), phones);
        }
        {
            QJsonArray urls;
            urls.append(QJsonObject{
                { QStringLiteral("value"), QStringLiteral("https://bob.example") },
                { QStringLiteral("type"), QStringLiteral("blog") } });
            canon.insert(QStringLiteral("urls"), urls);
        }

        CanonToGooglePersonStage demote;
        GooglePersonToCanonStage promote;
        const QByteArray personBytes = demote.transform(serialize(canon));
        QVERIFY2(!personBytes.isEmpty(), "demote returned empty bytes");
        const QByteArray roundTripped = promote.transform(personBytes);
        QVERIFY2(!roundTripped.isEmpty(), "re-promote returned empty bytes");
        QCOMPARE(roundTripped, serialize(canon));
    }

    // Registry inspection: both directions registered; demote declared lossy.
    void inspectDeclaresGooglePersonEdge()
    {
        const auto regs = makeContactsRegistries();
        const Shape canon{ DomainId{QStringLiteral("contacts")},
                           EncodingId{QStringLiteral("canon")} };
        const Shape gp{ DomainId{QStringLiteral("contacts")},
                        EncodingId{QStringLiteral("google-person")} };

        const auto loss = regs.transformation.inspect(canon, gp);
        QVERIFY2(!loss.isLossless(), "canon->google-person must be declared lossy");
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("birthday")}),
                 LossKind::Simplified);
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("categories")}),
                 LossKind::Reversible);

        const auto promoteLoss = regs.transformation.inspect(
            gp, Shape{ DomainId{QStringLiteral("contacts")},
                       EncodingId{QStringLiteral("canon")} });
        QVERIFY2(promoteLoss.isLossless(), "promote must be lossless");
    }

    // Committed live-capture fixture: every sanitized connection promotes.
    // IP.5/O80 — the digest must be STABLE across a change confined purely
    // to `etag` (Google's universal per-write concurrency token — see the
    // promote-side comment: real captured People API listings showed this
    // top-level etag change on EVERY person even across two fetches 34s
    // apart with NO edit made, while `metadata` stayed byte-identical), and
    // must CHANGE when real (non-volatile) extras content changes (e.g. an
    // unmapped field like `miscKeywords`, which is not consumed by name and
    // so lands in the same stash). `metadata` itself is deliberately NOT
    // varied here — the promote-side comment documents it as genuinely
    // edit-correlated, i.e. content this digest is right to hash.
    void providerExtrasDigestIgnoresVolatileGoogleBookkeeping()
    {
        GooglePersonToCanonStage stage;

        auto makePerson = [](const QString& etag, const QString& keyword) {
            return QByteArray(
                "{\"resourceName\": \"people/c123\", \"etag\": \"").append(etag.toUtf8())
                .append("\", \"names\": [{\"displayName\": \"x\"}], \"miscKeywords\": [{\"value\": \"")
                .append(keyword.toUtf8()).append("\"}]}");
        };

        const QJsonObject base = parse(stage.transform(
            makePerson(QStringLiteral("one"), QStringLiteral("hello"))));
        const QJsonObject onlyEtagChanged = parse(stage.transform(
            makePerson(QStringLiteral("two"), QStringLiteral("hello"))));
        const QJsonObject realContentChanged = parse(stage.transform(
            makePerson(QStringLiteral("one"), QStringLiteral("goodbye"))));

        const QString baseDigest = base.value(QStringLiteral("providerExtrasDigest")).toString();
        QVERIFY2(!baseDigest.isEmpty(), "digest must be present");
        QCOMPARE(onlyEtagChanged.value(QStringLiteral("providerExtrasDigest")).toString(),
                 baseDigest);
        QVERIFY2(realContentChanged.value(QStringLiteral("providerExtrasDigest")).toString()
                     != baseDigest,
                 "a real (non-volatile) extras content change must change the digest");
    }

    void promoteCommittedLiveFixture()
    {
        QFile f(QLatin1String(KALBURATOR_VENDOR_FIXTURE_DIR)
                + QStringLiteral("/google/contacts-connections.json"));
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(f.errorString()));
        const QJsonArray connections =
            QJsonDocument::fromJson(f.readAll())
                .object()
                .value(QStringLiteral("connections"))
                .toArray();
        QVERIFY2(connections.size() >= 70, "fixture should hold ~72 connections");

        GooglePersonToCanonStage stage;
        for (const auto& cv : connections) {
            const QJsonObject canon = parse(stage.transform(
                QJsonDocument(cv.toObject()).toJson(QJsonDocument::Compact)));
            QVERIFY2(!canon.isEmpty(), "promote returned empty canon");
            QVERIFY(!canon.value(QStringLiteral("uid")).toString().isEmpty());
            QCOMPARE(canon.value(providerExtrasKey())
                         .toObject()
                         .value(QStringLiteral("google"))
                         .toObject()
                         .value(QStringLiteral("etag"))
                         .toString()
                         .isEmpty(),
                     false);
        }
    }
};

QTEST_MAIN(TestGooglePersonCanonEdge)
#include "tst_google_person_canon_edge.moc"
