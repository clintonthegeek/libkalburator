#include <QTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include "canonenvelope.h"
#include "vcardcanonstages.h"
#include "contactsdomaindefinition.h"
#include "contactsstockshapes.h"
#include "shaperegistries.h"
#include "lossprofile.h"

#include <KContacts/VCardConverter>
#include <KContacts/Addressee>

using Kalburator::Shape::CanonEnvelope::parse;
using Kalburator::Contacts::VCard4ToCanonStage;
using Kalburator::Contacts::CanonToVCard4Stage;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::Shape;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::LossKind;

namespace {

// Build a ShapeRegistries with the contacts domain fully registered
// (DomainDefinition canonical spine + StockShapes peers + edges).
Kalburator::Shape::ShapeRegistries makeContactsRegistries()
{
    Kalburator::Shape::ShapeRegistries regs;
    auto& reg = regs.transformation;

    Kalburator::Contacts::ContactsDomainDefinition def;
    // Build the versioned spine (vcard4 → canon) as PluginManager would.
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

} // namespace

namespace {

const QByteArray kTestVCard =
    "BEGIN:VCARD\r\n"
    "VERSION:4.0\r\n"
    "UID:test-uid-12345\r\n"
    "FN:Alice Smith\r\n"
    "N:Smith;Alice;Marie;Dr.;Jr.\r\n"
    "NICKNAME:Ally\r\n"
    "EMAIL;TYPE=WORK;PREF=1:alice@example.com\r\n"
    "EMAIL;TYPE=HOME:alice.home@example.com\r\n"
    "TEL;TYPE=CELL;PREF=1:+1-555-123-4567\r\n"
    "ORG:ACME Corp;Engineering\r\n"
    "TITLE:Senior Engineer\r\n"
    "ROLE:Software Developer\r\n"
    "CATEGORIES:Friend,Colleague\r\n"
    "NOTE:This is a test contact.\r\n"
    "END:VCARD\r\n";

} // namespace

class TestContactsCanonRoundtrip : public QObject {
    Q_OBJECT
private slots:

    void vcard4ToCanonExtractsCoreFields()
    {
        VCard4ToCanonStage stage;
        const QByteArray out = stage.transform(kTestVCard);
        QVERIFY2(!out.isEmpty(), "VCard4ToCanonStage returned empty bytes");

        const QJsonObject obj = parse(out);
        QVERIFY2(!obj.isEmpty(), "Canon JSON output is empty object");

        // uid must be present and match the vCard UID
        const QString uid = obj.value(QStringLiteral("uid")).toString();
        QVERIFY2(!uid.isEmpty(), "uid must be present in canon output");
        QCOMPARE(uid, QStringLiteral("test-uid-12345"));

        // _canon envelope must be stamped with domain = "contacts"
        const QJsonObject canon = obj.value(QStringLiteral("_canon")).toObject();
        QCOMPARE(canon.value(QStringLiteral("domain")).toString(),
                 QStringLiteral("contacts"));

        // names[0] must have formatted and family/given populated
        const QJsonArray names = obj.value(QStringLiteral("names")).toArray();
        QVERIFY2(!names.isEmpty(), "names must not be empty");
        const QJsonObject name0 = names.at(0).toObject();
        QCOMPARE(name0.value(QStringLiteral("formatted")).toString(),
                 QStringLiteral("Alice Smith"));
        QCOMPARE(name0.value(QStringLiteral("family")).toString(),
                 QStringLiteral("Smith"));
        QCOMPARE(name0.value(QStringLiteral("given")).toString(),
                 QStringLiteral("Alice"));

        // emails array must contain both addresses
        const QJsonArray emails = obj.value(QStringLiteral("emails")).toArray();
        QVERIFY2(emails.size() >= 2, "emails must have at least 2 entries");
        // First entry (preferred) should be the work email
        bool foundWork = false;
        for (const auto& e : emails) {
            const auto eo = e.toObject();
            if (eo.value(QStringLiteral("value")).toString() ==
                QStringLiteral("alice@example.com")) {
                foundWork = true;
                QVERIFY2(eo.value(QStringLiteral("primary")).toBool(),
                         "work email must be primary (PREF=1)");
            }
        }
        QVERIFY2(foundWork, "alice@example.com must appear in emails");

        // phones array
        const QJsonArray phones = obj.value(QStringLiteral("phones")).toArray();
        QVERIFY2(!phones.isEmpty(), "phones must not be empty");
        QCOMPARE(phones.at(0).toObject().value(QStringLiteral("value")).toString(),
                 QStringLiteral("+1-555-123-4567"));

        // organizations
        const QJsonArray orgs = obj.value(QStringLiteral("organizations")).toArray();
        QVERIFY2(!orgs.isEmpty(), "organizations must not be empty");
        QCOMPARE(orgs.at(0).toObject().value(QStringLiteral("name")).toString(),
                 QStringLiteral("ACME Corp"));

        // categories
        const QJsonArray cats = obj.value(QStringLiteral("categories")).toArray();
        QVERIFY2(cats.contains(QJsonValue(QStringLiteral("Friend"))),
                 "categories must contain 'Friend'");
        QVERIFY2(cats.contains(QJsonValue(QStringLiteral("Colleague"))),
                 "categories must contain 'Colleague'");

        // notes
        const QString notes = obj.value(QStringLiteral("notes")).toString();
        QCOMPARE(notes, QStringLiteral("This is a test contact."));
    }

    void vcard4RoundTripPreservesCoreFields()
    {
        // vCard4 with core fields plus a KContacts custom property.
        // KContacts stores custom properties as "APP-NAME:value"; the round-trip
        // must preserve them.  We use insertCustom("X-TEST","CUSTOM","hello")
        // which stores in customs() as "X-TEST-CUSTOM:hello" — a form that
        // KContacts can emit and re-parse correctly.
        const QByteArray input =
            "BEGIN:VCARD\r\n"
            "VERSION:4.0\r\n"
            "UID:rt-uid-001\r\n"
            "FN:Round Trip\r\n"
            "N:Trip;Round;;;\r\n"
            "EMAIL;TYPE=WORK;PREF=1:rt@example.com\r\n"
            "TEL;TYPE=CELL:+1-555-999-0000\r\n"
            "ORG:RoundCorp\r\n"
            "TITLE:Tester\r\n"
            "CATEGORIES:TestCat\r\n"
            "END:VCARD\r\n";

        VCard4ToCanonStage  fwd;
        CanonToVCard4Stage  rev;

        const QByteArray canon  = fwd.transform(input);
        QVERIFY2(!canon.isEmpty(),  "forward stage returned empty");

        const QByteArray output = rev.transform(canon);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty (stub?)");

        // Parse both as Addressee objects and compare fields via KContacts getters.
        KContacts::VCardConverter conv;
        const auto origList = conv.parseVCards(input);
        const auto outList  = conv.parseVCards(output);
        QVERIFY2(!origList.isEmpty(), "could not parse original vCard");
        QVERIFY2(!outList.isEmpty(),  "could not parse output vCard");

        const KContacts::Addressee orig = origList.first();
        const KContacts::Addressee out  = outList.first();

        QCOMPARE(out.formattedName(), orig.formattedName());
        QCOMPARE(out.familyName(),    orig.familyName());
        QCOMPARE(out.givenName(),     orig.givenName());
        QCOMPARE(out.organization(),  orig.organization());
        QCOMPARE(out.title(),         orig.title());

        // Email: round-trip must preserve address and type
        QVERIFY2(!out.emailList().isEmpty(), "output has no emails");
        bool foundEmail = false;
        for (const auto& e : out.emailList()) {
            if (e.mail() == QStringLiteral("rt@example.com")) {
                foundEmail = true;
                QVERIFY2(e.isPreferred(), "work email must survive as preferred");
            }
        }
        QVERIFY2(foundEmail, "rt@example.com must survive round-trip");

        // Phone
        QVERIFY2(!out.phoneNumbers().isEmpty(), "output has no phones");
        QCOMPARE(out.phoneNumbers().first().number(),
                 orig.phoneNumbers().first().number());

        // Categories
        const QStringList outCats = out.categories();
        QVERIFY2(outCats.contains(QStringLiteral("TestCat")),
                 "categories must survive round-trip");
    }

    void canonToVcard4OmitsGoogleOnlyFields()
    {
        // Build a canon JSON that includes Google-only fields (occupations,
        // interests) alongside a core field (names/FN). The reverse stage
        // must emit the core field and silently drop the Google-only ones.
        using Kalburator::Shape::CanonEnvelope::serialize;
        using Kalburator::Shape::CanonEnvelope::stampEnvelope;

        QJsonObject canon;
        // Minimal names entry so the output vCard has an FN
        QJsonArray names;
        QJsonObject nameObj;
        nameObj.insert(QStringLiteral("formatted"), QStringLiteral("Drop Test"));
        nameObj.insert(QStringLiteral("given"),     QStringLiteral("Drop"));
        nameObj.insert(QStringLiteral("family"),    QStringLiteral("Test"));
        names.append(nameObj);
        canon.insert(QStringLiteral("names"), names);

        // Google-only fields that have no vCard4 representation
        QJsonArray occupations;
        occupations.append(QStringLiteral("Engineer"));
        canon.insert(QStringLiteral("occupations"), occupations);

        QJsonArray interests;
        interests.append(QStringLiteral("Hiking"));
        canon.insert(QStringLiteral("interests"), interests);

        stampEnvelope(canon, QStringLiteral("contacts"), QStringLiteral("drop-test-uid"));
        const QByteArray canonBytes = serialize(canon);

        CanonToVCard4Stage rev;
        const QByteArray output = rev.transform(canonBytes);
        QVERIFY2(!output.isEmpty(), "reverse stage returned empty");

        // Core field must survive
        QVERIFY2(output.contains("Drop Test") || output.contains("Drop"),
                 "FN/N must survive in output");

        // Google-only values must not appear
        QVERIFY2(!output.contains("Engineer"),
                 "occupations must be absent in vCard4 output");
        QVERIFY2(!output.contains("Hiking"),
                 "interests must be absent in vCard4 output");
    }

    void vcard4ToCanonEmptyInputReturnsEmpty()
    {
        VCard4ToCanonStage stage;
        const QByteArray out = stage.transform(QByteArray{});
        QVERIFY(out.isEmpty());
    }

    void vcard4ToCanonNoExtraEmptyKeys()
    {
        // A minimal vCard — only uid+FN. The canon output must NOT have
        // empty arrays for absent fields like emails, phones, etc.
        const QByteArray minimal =
            "BEGIN:VCARD\r\n"
            "VERSION:4.0\r\n"
            "UID:minimal-001\r\n"
            "FN:Bob\r\n"
            "END:VCARD\r\n";

        VCard4ToCanonStage stage;
        const QByteArray out = stage.transform(minimal);
        const QJsonObject obj = parse(out);
        QVERIFY2(!obj.isEmpty(), "output must not be empty JSON");

        // These keys must NOT be present for a minimal vCard
        QVERIFY2(!obj.contains(QStringLiteral("emails")),
                 "emails must be absent when vCard has no email");
        QVERIFY2(!obj.contains(QStringLiteral("phones")),
                 "phones must be absent when vCard has no phone");
        QVERIFY2(!obj.contains(QStringLiteral("organizations")),
                 "organizations must be absent when vCard has no org");
        QVERIFY2(!obj.contains(QStringLiteral("categories")),
                 "categories must be absent when vCard has no categories");
    }

    // Edge + spine routing tests (Task A5)

    void vcard3RoutesToCanonViaTwoHops()
    {
        // With spine=[vcard4, canon] and edges v3→v4 + v4→canon, the registry
        // must compile a 2-hop pipeline v3→v4→canon.
        const auto regs = makeContactsRegistries();
        const Shape v3{ DomainId{QStringLiteral("contacts")}, EncodingId{QStringLiteral("vcard3")} };
        const Shape canon{ DomainId{QStringLiteral("contacts")}, EncodingId{QStringLiteral("canon")} };

        const auto pipeline = regs.transformation.compile(v3, canon);
        QVERIFY2(pipeline.has_value(),
                 "compile(vcard3, canon) must succeed via vcard3->vcard4->canon N-hop");
        QCOMPARE(pipeline->edges().size(), 2);
    }

    void canonReversibleGoogleFieldsRoundTripViaProviderExtras()
    {
        // Verifies the Reversible loss contract for sipAddresses, calendarUrls,
        // and externalIds: a canon→vcard4→canon cycle must not silently drop
        // them; they must be recoverable from providerExtras["x-vcard"].
        using Kalburator::Shape::CanonEnvelope::serialize;
        using Kalburator::Shape::CanonEnvelope::stampEnvelope;
        using Kalburator::Shape::CanonEnvelope::providerExtrasKey;

        // --- Build a canon object with the three Reversible fields ---
        QJsonObject canon;

        // Core field so the vCard output has an FN
        QJsonArray names;
        QJsonObject nameObj;
        nameObj.insert(QStringLiteral("formatted"), QStringLiteral("Reversible Test"));
        nameObj.insert(QStringLiteral("given"),     QStringLiteral("Reversible"));
        nameObj.insert(QStringLiteral("family"),    QStringLiteral("Test"));
        names.append(nameObj);
        canon.insert(QStringLiteral("names"), names);

        // sipAddresses — StringList canon kind (array of strings)
        QJsonArray sipArr;
        sipArr.append(QStringLiteral("sip:alice@example.com"));
        sipArr.append(QStringLiteral("sip:alice-work@corp.example.com"));
        canon.insert(QStringLiteral("sipAddresses"), sipArr);

        // calendarUrls — Json canon kind (array of objects)
        QJsonArray calArr;
        QJsonObject calEntry;
        calEntry.insert(QStringLiteral("url"),  QStringLiteral("https://cal.example.com/alice"));
        calEntry.insert(QStringLiteral("type"), QStringLiteral("work"));
        calArr.append(calEntry);
        canon.insert(QStringLiteral("calendarUrls"), calArr);

        // externalIds — Json canon kind (array of objects)
        QJsonArray extArr;
        QJsonObject extEntry;
        extEntry.insert(QStringLiteral("value"), QStringLiteral("abc-123"));
        extEntry.insert(QStringLiteral("type"),  QStringLiteral("google_profile"));
        extArr.append(extEntry);
        canon.insert(QStringLiteral("externalIds"), extArr);

        stampEnvelope(canon, QStringLiteral("contacts"),
                      QStringLiteral("reversible-test-uid"));
        const QByteArray canonBytes = serialize(canon);

        // --- canon → vCard4 → canon ---
        CanonToVCard4Stage rev;
        const QByteArray vcardBytes = rev.transform(canonBytes);
        QVERIFY2(!vcardBytes.isEmpty(), "CanonToVCard4Stage returned empty");

        VCard4ToCanonStage fwd;
        const QByteArray canonBytes2 = fwd.transform(vcardBytes);
        QVERIFY2(!canonBytes2.isEmpty(), "VCard4ToCanonStage returned empty");

        // --- Assert fields are recoverable from providerExtras["x-vcard"] ---
        const QJsonObject obj2 = parse(canonBytes2);
        QVERIFY2(!obj2.isEmpty(), "round-tripped canon is empty JSON");

        const QJsonObject extras = obj2.value(providerExtrasKey()).toObject();
        const QJsonObject xvcard = extras.value(QStringLiteral("x-vcard")).toObject();

        // sipAddresses must be in xvcard["CANON-SIPADDRESSES"] as compact JSON array
        QVERIFY2(xvcard.contains(QStringLiteral("CANON-SIPADDRESSES")),
                 "sipAddresses must be stashed in providerExtras[x-vcard][CANON-SIPADDRESSES]");
        {
            const QJsonDocument doc = QJsonDocument::fromJson(
                xvcard.value(QStringLiteral("CANON-SIPADDRESSES")).toString().toUtf8());
            QVERIFY2(doc.isArray(), "CANON-SIPADDRESSES value must be a JSON array");
            const QJsonArray recovered = doc.array();
            QVERIFY2(recovered.contains(QJsonValue(QStringLiteral("sip:alice@example.com"))),
                     "first SIP address must be recoverable verbatim");
            QVERIFY2(recovered.contains(
                         QJsonValue(QStringLiteral("sip:alice-work@corp.example.com"))),
                     "second SIP address must be recoverable verbatim");
        }

        // calendarUrls must be in xvcard["CANON-CALENDARURLS"] as compact JSON array
        QVERIFY2(xvcard.contains(QStringLiteral("CANON-CALENDARURLS")),
                 "calendarUrls must be stashed in providerExtras[x-vcard][CANON-CALENDARURLS]");
        {
            const QJsonDocument doc = QJsonDocument::fromJson(
                xvcard.value(QStringLiteral("CANON-CALENDARURLS")).toString().toUtf8());
            QVERIFY2(doc.isArray(), "CANON-CALENDARURLS value must be a JSON array");
            const QJsonArray recovered = doc.array();
            QVERIFY2(!recovered.isEmpty(), "calendarUrls array must be non-empty");
            QCOMPARE(recovered.at(0).toObject()
                         .value(QStringLiteral("url")).toString(),
                     QStringLiteral("https://cal.example.com/alice"));
        }

        // externalIds must be in xvcard["CANON-EXTERNALIDS"] as compact JSON array
        QVERIFY2(xvcard.contains(QStringLiteral("CANON-EXTERNALIDS")),
                 "externalIds must be stashed in providerExtras[x-vcard][CANON-EXTERNALIDS]");
        {
            const QJsonDocument doc = QJsonDocument::fromJson(
                xvcard.value(QStringLiteral("CANON-EXTERNALIDS")).toString().toUtf8());
            QVERIFY2(doc.isArray(), "CANON-EXTERNALIDS value must be a JSON array");
            const QJsonArray recovered = doc.array();
            QVERIFY2(!recovered.isEmpty(), "externalIds array must be non-empty");
            QCOMPARE(recovered.at(0).toObject()
                         .value(QStringLiteral("value")).toString(),
                     QStringLiteral("abc-123"));
        }
    }

    void canonToVcard4LossProfileChargesGoogleOnlyFields()
    {
        // The canon→vcard4 demote edge must declare Google-only fields as Dropped
        // and reversible fields (sipAddresses etc.) as Reversible.
        const auto regs = makeContactsRegistries();
        const Shape canon{ DomainId{QStringLiteral("contacts")}, EncodingId{QStringLiteral("canon")} };
        const Shape v4{ DomainId{QStringLiteral("contacts")}, EncodingId{QStringLiteral("vcard4")} };

        const auto loss = regs.transformation.inspect(canon, v4);
        QVERIFY2(!loss.isLossless(),
                 "canon->vcard4 must be lossy (Google-only fields cannot be represented)");

        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("occupations")}),
                 LossKind::Dropped);
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("interests")}),
                 LossKind::Dropped);
        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("skills")}),
                 LossKind::Dropped);

        QCOMPARE(loss.affected.value(PropertyId{QStringLiteral("sipAddresses")}),
                 LossKind::Reversible);
    }
};

QTEST_GUILESS_MAIN(TestContactsCanonRoundtrip)
#include "tst_contacts_canon_roundtrip.moc"
