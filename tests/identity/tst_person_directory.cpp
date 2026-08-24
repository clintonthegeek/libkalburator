// PersonDirectory — the Nepomuk moment (EEE §5 payoff). Proves the
// composition: REAL Google and Microsoft fixture payloads promote through
// their edges, land in one identity store, and a meeting roster resolves
// to named persons across vendors. Unresolved emails stay strangers.
//
// NOTE: no terminated raw string literals in this TU (O59 moc tooling rule).

#include <QTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "canonenvelope.h"
#include "persondirectory.h"
#include "googlepersoncanonstages.h"
#include "mscontactcanonstages.h"
#include "googlecanonstages.h"

using Kalburator::Shape::CanonEnvelope::parse;
using Kalburator::Shape::CanonEnvelope::serialize;
using Kalburator::Shape::CanonEnvelope::stampEnvelope;
using Kalburator::Identity::PersonDirectory;
using Kalburator::Identity::IdentityStore;
using Kalburator::Identity::RosterEntry;
using Kalburator::Contacts::GooglePersonToCanonStage;
using Kalburator::Contacts::MsContactToCanonStage;
using Kalburator::Calendar::GoogleEventToCanonStage;

namespace {

QByteArray readFixture(const QString& relPath)
{
    QFile f(QLatin1String(KALBURATOR_VENDOR_FIXTURE_DIR) + relPath);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}

} // namespace

class TestPersonDirectory : public QObject {
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

    // THE cross-vendor single-human proof: a Google People contact and a
    // Graph contact sharing an email become ONE entity with a name, even
    // though their uids come from different vendor namespaces.
    void crossVendorRecordsResolveToOnePerson()
    {
        IdentityStore store(m_dir->filePath("identity.db"));
        PersonDirectory dir(store);

        GooglePersonToCanonStage gPromote;
        MsContactToCanonStage msPromote;

        const QByteArray googleCanon = gPromote.transform(QByteArray(
            "{\"resourceName\": \"people/alice-g\","
            "\"names\": [{\"displayName\": \"Alice Example\", "
            "\"givenName\": \"Alice\", \"familyName\": \"Example\"}],"
            "\"emailAddresses\": [{\"value\": \"alice@example.com\"}]}"));
        const QByteArray msCanon = msPromote.transform(QByteArray(
            "{\"id\": \"AAMk-ms-alice\","
            "\"displayName\": \"Alice Example\","
            "\"givenName\": \"Alice\", \"surname\": \"Example\","
            "\"emailAddresses\": [{\"address\": \"alice@example.com\", "
            "\"name\": \"Alice Example\"}]}"));

        const QString entG = dir.observe(googleCanon);
        const QString entM = dir.observe(msCanon);
        QVERIFY(!entG.isEmpty());
        QCOMPARE(entM, entG);  // same human

        // Roster of an event she attends resolves to her NAME — joined
        // from either vendor's record.
        QByteArray eventCanon =
            "{\"_canon\":{\"domain\":\"calendar\",\"v\":1},"
            "\"uid\":\"evt-x\","
            "\"organizer\":{\"email\":\"boss@example.com\"},"
            "\"attendees\":[{\"email\":\"alice@example.com\"}]}";
        const auto roster = dir.eventRoster(eventCanon);
        QCOMPARE(roster.size(), 2);
        QCOMPARE(roster[0].email, QStringLiteral("boss@example.com"));
        QVERIFY2(roster[0].entityId.isEmpty(),
                 "unlinked organizer stays a stranger");
        QCOMPARE(roster[1].email, QStringLiteral("alice@example.com"));
        QCOMPARE(roster[1].entityId, entG);
        QCOMPARE(roster[1].displayName, QStringLiteral("Alice Example"));
    }

    // Bulk observation of BOTH committed live-capture fixtures: every
    // Google connection and every Graph contact lands in one directory.
    void bulkObserveBothVendorFixtures()
    {
        IdentityStore store(m_dir->filePath("identity.db"));
        PersonDirectory dir(store);
        GooglePersonToCanonStage gPromote;
        MsContactToCanonStage msPromote;

        int observed = 0;

        QFile gf(QLatin1String(KALBURATOR_VENDOR_FIXTURE_DIR)
                 + QStringLiteral("/google/contacts-connections.json"));
        QVERIFY2(gf.open(QIODevice::ReadOnly), qPrintable(gf.errorString()));
        const QJsonArray connections =
            QJsonDocument::fromJson(gf.readAll())
                .object()
                .value(QStringLiteral("connections"))
                .toArray();
        for (const auto& cv : connections) {
            const QString ent = dir.observe(gPromote.transform(
                QJsonDocument(cv.toObject()).toJson(QJsonDocument::Compact)));
            if (!ent.isEmpty())
                ++observed;
        }

        QFile mf(QLatin1String(KALBURATOR_VENDOR_FIXTURE_DIR)
                 + QStringLiteral("/microsoft/contacts-listing.json"));
        QVERIFY2(mf.open(QIODevice::ReadOnly), qPrintable(mf.errorString()));
        const QJsonArray contacts =
            QJsonDocument::fromJson(mf.readAll())
                .object()
                .value(QStringLiteral("value"))
                .toArray();
        for (const auto& cv : contacts) {
            const QString ent = dir.observe(msPromote.transform(
                QJsonDocument(cv.toObject()).toJson(QJsonDocument::Compact)));
            if (!ent.isEmpty())
                ++observed;
        }

        QVERIFY2(observed >= 70,
                 qPrintable(QStringLiteral("expected most records to observe; got %1")
                                .arg(observed)));
    }

    // Roster order is stable: organizer first, attendees in wire order,
    // duplicates collapsed case-insensitively.
    void rosterOrderAndDedup()
    {
        IdentityStore store(m_dir->filePath("identity.db"));
        PersonDirectory dir(store);
        QByteArray eventCanon =
            "{\"_canon\":{\"domain\":\"calendar\",\"v\":1},"
            "\"uid\":\"evt-dup\","
            "\"organizer\":{\"email\":\"Pat@Example.com\"},"
            "\"attendees\":["
            "{\"email\":\"pat@example.com\"},"
            "{\"email\":\"sam@example.com\"}]}";
        const auto roster = dir.eventRoster(eventCanon);
        QCOMPARE(roster.size(), 2);
        QCOMPARE(roster[0].email, QStringLiteral("Pat@Example.com"));
        QCOMPARE(roster[1].email, QStringLiteral("sam@example.com"));
    }

    // An empty-roster event yields nothing; a non-event canon is safe.
    void degenerateInputsAreSafe()
    {
        IdentityStore store(m_dir->filePath("identity.db"));
        PersonDirectory dir(store);
        QVERIFY(dir.eventRoster(QByteArray("{}")).isEmpty());
        QVERIFY(dir.eventRoster(QByteArray()).isEmpty());

        QByteArray contactNoEmails =
            "{\"_canon\":{\"domain\":\"contacts\",\"v\":1},"
            "\"uid\":\"people/empty\"}";
        // A record with NO emails still earns its own entity (people
        // exist without addresses) — but nothing converges onto it.
        QVERIFY(!dir.observe(contactNoEmails).isEmpty());
        QByteArray strangerEvent =
            "{\"_canon\":{\"domain\":\"calendar\",\"v\":1},"
            "\"uid\":\"evt-s\","
            "\"attendees\":[{\"email\":\"nobody@example.com\"}]}";
        QVERIFY(dir.eventRoster(strangerEvent).at(0).entityId.isEmpty());
    }

    // Display-name projection survives store reopen (schema v2 column).
    void projectionPersistsAcrossReopen()
    {
        const QString path = m_dir->filePath("identity.db");
        {
            IdentityStore store(path);
            PersonDirectory dir(store);
            const QByteArray canon = GooglePersonToCanonStage{}.transform(
                QByteArray(
                    "{\"resourceName\": \"people/persist\","
                    "\"names\": [{\"displayName\": \"Persisted Pat\"}],"
                    "\"emailAddresses\": [{\"value\": \"pat@example.com\"}]}"));
            QVERIFY(!dir.observe(canon).isEmpty());
        }
        IdentityStore reopened(path);
        QCOMPARE(reopened.displayNameFor(QStringLiteral("contacts"),
                                         QStringLiteral("people/persist")),
                 QStringLiteral("Persisted Pat"));
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
};

QTEST_MAIN(TestPersonDirectory)
#include "tst_person_directory.moc"
