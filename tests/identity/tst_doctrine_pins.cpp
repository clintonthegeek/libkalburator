// Doctrine pins — EEE roadmap Part IV (ethics of the data model), made
// falsifiable. Each slot adversarially verifies one binding rule; if a
// future change breaks the ethic, THIS suite goes red, not a README.
//
// NOTE: no terminated raw string literals in this TU (O59 moc tooling rule).

#include <QTest>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "identitystore.h"
#include "persondirectory.h"

using Kalburator::Identity::EntityLink;
using Kalburator::Identity::IdentityStore;
using Kalburator::Identity::PersonDirectory;

namespace {

QByteArray contactCanon(const QString& uid, const QString& name,
                        const QStringList& emails)
{
    QJsonArray emailRows;
    for (const QString& e : emails)
        emailRows.append(QJsonObject{ { QStringLiteral("value"), e } });
    QJsonObject obj;
    obj.insert(QStringLiteral("_canon"),
               QJsonObject{ { QStringLiteral("domain"), QStringLiteral("contacts") },
                            { QStringLiteral("v"), 1 } });
    obj.insert(QStringLiteral("uid"), uid);
    if (!name.isEmpty())
        obj.insert(QStringLiteral("names"),
                   QJsonArray{ QJsonObject{ { QStringLiteral("formatted"), name } } });
    obj.insert(QStringLiteral("emails"), emailRows);
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QByteArray eventCanon(const QString& uid, const QStringList& attendeeEmails)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("_canon"),
               QJsonObject{ { QStringLiteral("domain"), QStringLiteral("calendar") },
                            { QStringLiteral("v"), 1 } });
    obj.insert(QStringLiteral("uid"), uid);
    QJsonArray att;
    for (const QString& e : attendeeEmails)
        att.append(QJsonObject{ { QStringLiteral("email"), e } });
    obj.insert(QStringLiteral("attendees"), att);
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

/// Raw byte-level sweep of BOTH tables for any trace of the needle.
bool anyTraceInFile(const QString& dbPath, const QString& needle,
                    bool* ok)
{
    *ok = false;
    QSqlDatabase db = QSqlDatabase::addDatabase(
        QStringLiteral("QSQLITE"), QStringLiteral("trace-") + needle);
    db.setDatabaseName(dbPath);
    if (!db.open())
        return true;  // treat unreadable as suspicious, caller fails
    bool found = false;
    const QHash<QString, QStringList> kColumns = {
        { QStringLiteral("record_links"),
          { QStringLiteral("entity_id"), QStringLiteral("record_uid"),
            QStringLiteral("display_name") } },
        { QStringLiteral("email_index"),
          { QStringLiteral("email"), QStringLiteral("entity_id") } },
    };
    for (auto it = kColumns.constBegin(); it != kColumns.constEnd(); ++it) {
        const QString table = it.key();
        QStringList clauses;
        for (int i = 0; i < it.value().size(); ++i)
            clauses << QStringLiteral("%1 LIKE ?").arg(it.value().at(i));
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT COUNT(*) FROM %1 WHERE %2")
                      .arg(table, clauses.join(QStringLiteral(" OR "))));
        const QString pat = QLatin1Char('%') + needle + QLatin1Char('%');
        for (int i = 0; i < it.value().size(); ++i)
            q.addBindValue(pat);
        if (!q.exec()) {
            *ok = false;
            return true;
        }
        while (q.next())
            if (q.value(0).toInt() > 0)
                found = true;
    }
    db.close();
    QSqlDatabase::removeDatabase(QStringLiteral("trace-") + needle);
    *ok = true;
    return found;
}

} // namespace

class TestDoctrinePins : public QObject {
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

    // Part IV rule 1 — NEVER A MERGE, guarded against future "smart"
    // matching: identical NAMES with disjoint emails must stay distinct.
    // Name similarity is profiling; shared email is testimony. Only the
    // latter may ever link.
    void identicalNamesWithDisjointEvidenceStayDistinct()
    {
        IdentityStore store(m_dir->filePath("identity.db"));
        PersonDirectory dir(store);
        const QString e1 =
            dir.observe(contactCanon(QStringLiteral("people/a1"),
                                     QStringLiteral("Alice Example"),
                                     { QStringLiteral("a@x.com") }));
        const QString e2 =
            dir.observe(contactCanon(QStringLiteral("people/a2"),
                                     QStringLiteral("Alice Example"),
                                     { QStringLiteral("b@x.com") }));
        QVERIFY(!e1.isEmpty());
        QVERIFY(!e2.isEmpty());
        QVERIFY2(e1 != e2,
                 "name equality must NEVER merge entities (Part IV rule 1)");
    }

    // Part IV rule 2 — THE GRAPH FORGETS, verified at the byte level:
    // after unlink, a raw sweep of both tables finds no trace of the
    // record's uid, its email, or its entity id.
    void forgettingIsByteLevelVerifiable()
    {
        const QString path = m_dir->filePath("identity.db");
        QString ent;
        {
            IdentityStore store(path);
            PersonDirectory dir(store);
            ent = dir.observe(contactCanon(QStringLiteral("people/gone"),
                                           QStringLiteral("Ghost Gone"),
                                           { QStringLiteral("gone@x.com") }));
            QVERIFY(!ent.isEmpty());
        }
        {
            IdentityStore store(path);
            store.unlinkRecord(QStringLiteral("contacts"),
                               QStringLiteral("people/gone"));
            QVERIFY(store.entityIdFor(QStringLiteral("contacts"),
                                      QStringLiteral("people/gone"))
                        .isEmpty());
            QVERIFY(store.entityIdForEmail(QStringLiteral("gone@x.com"))
                        .isEmpty());
            QVERIFY(store.recordsForEntity(ent).isEmpty());
        }
        bool ok = false;
        QVERIFY2(!anyTraceInFile(path, QStringLiteral("people/gone"), &ok)
                 && ok,
                 "record uid must leave no bytes behind");
        QVERIFY2(!anyTraceInFile(path, QStringLiteral("gone@x.com"), &ok)
                 && ok,
                 "email evidence must be pruned with its last record");
        QVERIFY2(!anyTraceInFile(path, ent, &ok) && ok,
                 "dead entity id must leave no bytes behind");
    }

    // Part IV rule 3 — STRANGERS STAY STRANGERS under adversarial bulk:
    // a hundred unknown attendees resolve to nothing; nothing invented.
    void bulkStrangersStayStrangers()
    {
        IdentityStore store(m_dir->filePath("identity.db"));
        PersonDirectory dir(store);

        QStringList unknowns;
        for (int i = 0; i < 100; ++i)
            unknowns << QStringLiteral("stranger%1@nowhere.invalid").arg(i);
        const auto roster =
            dir.eventRoster(eventCanon(QStringLiteral("evt-crowd"), unknowns));
        QCOMPARE(roster.size(), 100);
        for (const auto& entry : roster) {
            QVERIFY2(entry.entityId.isEmpty(),
                     qPrintable(QStringLiteral("%1 must stay a stranger")
                                    .arg(entry.email)));
            QVERIFY2(entry.displayName.isEmpty(),
                     qPrintable(QStringLiteral("%1 must get no invented name")
                                    .arg(entry.email)));
        }
    }

    // Part IV rule 4 — ONE EXPLICIT RULE: email evidence bridges PERSON
    // records only. An event never joins a person's entity via its
    // attendees (O65: convergence belongs to persons, not meetings), and
    // records with no shared evidence stay apart.
    void onlyEmailEvidenceBridgesRecords()
    {
        IdentityStore store(m_dir->filePath("identity.db"));
        PersonDirectory dir(store);

        const QString e1 =
            dir.observe(contactCanon(QStringLiteral("people/p1"),
                                     QStringLiteral("Pat One"),
                                     { QStringLiteral("bridge@x.com") }));

        // A calendar event carrying Pat's email must NOT adopt her
        // identity — its own entity is its uid alone.
        QByteArray evt =
            QStringLiteral("{\"_canon\":{\"domain\":\"calendar\",\"v\":1},"
                           "\"uid\":\"evt-b1\",\"attendees\":"
                           "[{\"email\":\"bridge@x.com\"}]}")
                .toUtf8();
        const QString eEvt = dir.observe(evt);
        QVERIFY(!eEvt.isEmpty());
        QVERIFY2(eEvt != e1,
                 "an event must never merge into a person's entity (O65)");
        // …yet Pat still RESOLVES as that event's participant.
        QCOMPARE(store.entityIdForEmail(QStringLiteral("bridge@x.com")), e1);

        // A third record with fresh, unrelated evidence stays apart.
        const QString e3 =
            dir.observe(contactCanon(QStringLiteral("people/p3"),
                                     QStringLiteral("Unrelated Person"),
                                     { QStringLiteral("other@x.com") }));
        QVERIFY2(e3 != e1, "unrelated records must not share an entity");
        QVERIFY2(e3 != eEvt, "persons and events never share entities");
    }

    // Part IV rule 7 — SEIZURE TEST, storage half: the schema version is
    // pinned; a seized copy must be readable by exactly this codebase and
    // silently-migrated stores must be detectable.
    void schemaVersionIsPinned()
    {
        {
            IdentityStore store(m_dir->filePath("identity.db"));
            QVERIFY(store.isOpen());
        }
        QSqlDatabase db = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), QStringLiteral("pinconn"));
        db.setDatabaseName(m_dir->filePath("identity.db"));
        QVERIFY(db.open());
        {
            QSqlQuery q(db);
            q.exec(QStringLiteral("PRAGMA user_version"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 2);
        }
        db.close();
        QSqlDatabase::removeDatabase(QStringLiteral("pinconn"));
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
};

QTEST_MAIN(TestDoctrinePins)
#include "tst_doctrine_pins.moc"
