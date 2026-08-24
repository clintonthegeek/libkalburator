// EEE identity layer (campaign proposal §5) — the first resolver, one rule:
// contacts' emails[].value ↔ calendar attendees[].email / organizer.email
// share one entity. Entities LINK records; they never collapse them.
//
// NOTE: no terminated raw string literals in this TU (O59 moc tooling rule).

#include <QTest>
#include <QTemporaryDir>

#include "identityresolver.h"
#include "identitystore.h"
#include "googlepersoncanonstages.h"
#include "mseventcanonstages.h"
#include "googlecanonstages.h"

using Kalburator::Identity::EntityLink;
using Kalburator::Identity::IdentityStore;
using Kalburator::Identity::extractCanonKeys;
using Kalburator::Identity::linkCanonRecord;

namespace {

const QByteArray kContactCanon =
    "{"
    "\"_canon\": {\"domain\": \"contacts\", \"v\": 1},"
    "\"uid\": \"people/c111\","
    "\"names\": [{\"formatted\": \"Alice Example\"}],"
    "\"emails\": [{\"value\": \"alice@example.com\", \"primary\": true}]"
    "}";

const QByteArray kEventCanon =
    "{"
    "\"_canon\": {\"domain\": \"calendar\", \"v\": 1},"
    "\"uid\": \"evt-42\","
    "\"summary\": \"Sync with Alice\","
    "\"organizer\": {\"email\": \"bob@example.com\"},"
    "\"attendees\": ["
    "  {\"email\": \"alice@example.com\", \"partstat\": \"needsAction\"},"
    "  {\"email\": \"carol@example.com\"}"
    "]"
    "}";

const QByteArray kSecondContactCanon =
    "{"
    "\"_canon\": {\"domain\": \"contacts\", \"v\": 1},"
    "\"uid\": \"people/c222\","
    "\"names\": [{\"formatted\": \"Carol Contact\"}],"
    "\"emails\": [{\"value\": \"carol@example.com\"}]"
    "}";

} // namespace

class TestIdentityLinks : public QObject {
    Q_OBJECT
private slots:

    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
    }
    void cleanup()
    {
        m_dir.reset();
    }

    // THE rule: a contact and an event participant sharing an email link
    // to one entity — "this meeting's people are these contacts".
    void sharedEmailLinksContactAndAttendee()
    {
        IdentityStore store(m_dir->filePath("identity.db"));
        QVERIFY(store.isOpen());

        const QString contactEnt =
            linkCanonRecord(store, kContactCanon);
        QVERIFY2(!contactEnt.isEmpty(), "contact must mint an entity");

        const QString eventEnt = linkCanonRecord(store, kEventCanon);
        QCOMPARE(eventEnt, contactEnt);

        // The event's organizer (bob) has no contact yet — the EVENT record
        // links via alice's email and joins the same entity.
        const auto records =
            store.recordsForEntity(contactEnt);
        QCOMPARE(records.size(), 2);
        bool sawContact = false;
        bool sawEvent = false;
        for (const auto& r : records) {
            if (r.domain == QLatin1String("contacts")
                && r.recordUid == QLatin1String("people/c111"))
                sawContact = true;
            if (r.domain == QLatin1String("calendar")
                && r.recordUid == QLatin1String("evt-42"))
                sawEvent = true;
        }
        QVERIFY(sawContact);
        QVERIFY(sawEvent);
    }

    // Second contact joins the entity through her OWN shared email
    // (attendee carol ↔ contact carol).
    void secondContactJoinsViaOwnEmail()
    {
        IdentityStore store(m_dir->filePath("identity.db"));
        QVERIFY(store.isOpen());

        const QString ent1 = linkCanonRecord(store, kContactCanon);
        linkCanonRecord(store, kEventCanon);
        const QString ent3 = linkCanonRecord(store, kSecondContactCanon);
        QCOMPARE(ent3, ent1);

        QCOMPARE(store.recordsForEntity(ent1).size(), 3);
        QVERIFY(store.emailsForEntity(ent1)
                    .contains(QStringLiteral("carol@example.com")));
    }

    // No shared evidence ⇒ distinct entities, never a merge.
    void distinctPeopleMintDistinctEntities()
    {
        IdentityStore store(m_dir->filePath("identity.db"));
        QVERIFY(store.isOpen());

        const QByteArray dave =
            "{\"_canon\":{\"domain\":\"contacts\",\"v\":1},"
            "\"uid\":\"people/d1\","
            "\"emails\":[{\"value\":\"dave@example.com\"}]}";
        const QByteArray erin =
            "{\"_canon\":{\"domain\":\"contacts\",\"v\":1},"
            "\"uid\":\"people/e1\","
            "\"emails\":[{\"value\":\"erin@example.com\"}]}";
        const QString e1 = linkCanonRecord(store, dave);
        const QString e2 = linkCanonRecord(store, erin);
        QVERIFY(!e1.isEmpty());
        QVERIFY(!e2.isEmpty());
        QVERIFY2(e1 != e2, "no shared email ⇒ separate entities");
    }

    // Re-linking a record is idempotent (stable entity).
    void relinkKeepsEntity()
    {
        IdentityStore store(m_dir->filePath("identity.db"));
        QVERIFY(store.isOpen());
        const QString first = linkCanonRecord(store, kContactCanon);
        const QString again = linkCanonRecord(store, kContactCanon);
        QCOMPARE(again, first);
        QCOMPARE(store.entityIdFor(QStringLiteral("contacts"),
                                   QStringLiteral("people/c111")),
                 first);
    }

    // Deleting a contact dissolves only its own link.
    void unlinkDissolvesOnlyOwnLink()
    {
        IdentityStore store(m_dir->filePath("identity.db"));
        QVERIFY(store.isOpen());
        const QString ent = linkCanonRecord(store, kContactCanon);
        linkCanonRecord(store, kEventCanon);

        store.unlinkRecord(QStringLiteral("contacts"),
                           QStringLiteral("people/c111"));
        QVERIFY(store.entityIdFor(QStringLiteral("contacts"),
                                  QStringLiteral("people/c111"))
                    .isEmpty());
        // event untouched
        QCOMPARE(store.entityIdFor(QStringLiteral("calendar"),
                                   QStringLiteral("evt-42")),
                 ent);

        // A NEW contact with alice's email re-joins the surviving entity
        // (the event attendee still holds the evidence alive).
        const QByteArray aliceAgain =
            "{\"_canon\":{\"domain\":\"contacts\",\"v\":1},"
            "\"uid\":\"people/c333\","
            "\"emails\":[{\"value\":\"alice@example.com\"}]}";
        QCOMPARE(linkCanonRecord(store, aliceAgain), ent);
    }

    // Full dissolution: when the LAST record of an entity unlinks, the
    // email evidence is pruned — a same-email stranger mints fresh rather
    // than resurrecting the dead entity.
    void lastUnlinkPrunesEvidence()
    {
        IdentityStore store(m_dir->filePath("identity.db"));
        QVERIFY(store.isOpen());
        const QByteArray solo =
            "{\"_canon\":{\"domain\":\"contacts\",\"v\":1},"
            "\"uid\":\"people/solo\","
            "\"emails\":[{\"value\":\"solo@example.com\"}]}";
        const QString ent = linkCanonRecord(store, solo);
        QVERIFY(!ent.isEmpty());

        store.unlinkRecord(QStringLiteral("contacts"),
                           QStringLiteral("people/solo"));

        const QByteArray stranger =
            "{\"_canon\":{\"domain\":\"contacts\",\"v\":1},"
            "\"uid\":\"people/stranger\","
            "\"emails\":[{\"value\":\"solo@example.com\"}]}";
        const QString fresh = linkCanonRecord(store, stranger);
        QVERIFY2(fresh != ent, "dead entity must not resurrect");
    }

    // Persistence: reopen the store, links survive.
    void reopenPersists()
    {
        const QString path = m_dir->filePath("identity.db");
        QString ent;
        {
            IdentityStore store(path);
            QVERIFY(store.isOpen());
            ent = linkCanonRecord(store, kContactCanon);
        }
        IdentityStore reopened(path);
        QVERIFY(reopened.isOpen());
        QCOMPARE(reopened.entityIdFor(QStringLiteral("contacts"),
                                      QStringLiteral("people/c111")),
                 ent);
    }

    // Key extraction works against REAL vendor-edge promote output for all
    // three relevant encodings (google-person, ms-event, google-event).
    void extractKeysFromVendorEdgeOutput()
    {
        using Kalburator::Contacts::GooglePersonToCanonStage;
        using Kalburator::Calendar::MsEventToCanonStage;
        using Kalburator::Calendar::GoogleEventToCanonStage;

        GooglePersonToCanonStage personPromote;
        const QJsonObject contactCanon = Kalburator::Shape::CanonEnvelope::parse(
            personPromote.transform(QByteArray(
                "{\"resourceName\":\"people/c9\","
                "\"emailAddresses\":[{\"value\":\"kim@example.com\"}]}")));
        const auto personKeys = extractCanonKeys(contactCanon);
        QCOMPARE(personKeys.domain, QLatin1String("contacts"));
        QCOMPARE(personKeys.uid, QLatin1String("people/c9"));
        QVERIFY(personKeys.emails.contains(QStringLiteral("kim@example.com")));

        MsEventToCanonStage msPromote;
        const QJsonObject msCanon = Kalburator::Shape::CanonEnvelope::parse(
            msPromote.transform(QByteArray(
                "{\"id\":\"e1\",\"subject\":\"Standup\","
                "\"attendees\":[{\"emailAddress\":"
                "{\"address\":\"lee@example.com\",\"name\":\"Lee\"}}]}")));
        const auto msKeys = extractCanonKeys(msCanon);
        QCOMPARE(msKeys.domain, QLatin1String("calendar"));
        QCOMPARE(msKeys.uid, QLatin1String("e1"));
        QVERIFY(msKeys.emails.contains(QStringLiteral("lee@example.com")));

        GoogleEventToCanonStage gPromote;
        const QJsonObject gCanon = Kalburator::Shape::CanonEnvelope::parse(
            gPromote.transform(QByteArray(
                "{\"id\":\"g1\",\"summary\":\"Lunch\","
                "\"organizer\":{\"email\":\"pat@example.com\"},"
                "\"start\":{\"date\":\"2026-09-01\"},"
                "\"end\":{\"date\":\"2026-09-01\"}}")));
        const auto gKeys = extractCanonKeys(gCanon);
        QCOMPARE(gKeys.uid, QLatin1String("g1"));
        QVERIFY(gKeys.emails.contains(QStringLiteral("pat@example.com")));
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
};

QTEST_MAIN(TestIdentityLinks)
#include "tst_identity_links.moc"
