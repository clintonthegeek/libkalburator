#include <QTest>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include "canonenvelope.h"
#include "vcardcanonstages.h"

using Kalburator::Shape::CanonEnvelope::parse;
using Kalburator::Contacts::VCard4ToCanonStage;

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
};

QTEST_GUILESS_MAIN(TestContactsCanonRoundtrip)
#include "tst_contacts_canon_roundtrip.moc"
