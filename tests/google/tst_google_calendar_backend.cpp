// B2C P1 — GoogleCalendarBackend against the mock Google server.
// Pins: discovery via calendarList (accessRole → writable), initial
// syncToken walk reporting the FULL set, incremental walk merging changes
// + tombstones, 410 Gone self-heal (O42), persistence resume across
// backend instances, O67 write rules (created/updated stripped on POST,
// id aliasing on minted transport ids, PATCH-in-place updates, 410-delete
// as success).

#include <QtTest/QtTest>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "googlecalendarbackend.h"
#include "mockgoogleserver.h"

using Kalburator::Google::MockGoogleServer;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::GoogleCalendarBackend;
using Kalburator::Sync::WriterBatch;

namespace {

QJsonObject makeEvent(const QString &id, const QString &summary)
{
    return QJsonObject{
        { QStringLiteral("id"), id },
        { QStringLiteral("summary"), summary },
        { QStringLiteral("status"), QStringLiteral("confirmed") },
        { QStringLiteral("iCalUID"), id + QStringLiteral("@example.com") },
        { QStringLiteral("start"),
          QJsonObject{ { QStringLiteral("date"),
                         QStringLiteral("2026-09-01") } } },
        { QStringLiteral("end"),
          QJsonObject{ { QStringLiteral("date"),
                         QStringLiteral("2026-09-02") } } },
    };
}

WriterBatch batchOf(const QList<BackendRecord> &creates = {},
                    const QList<BackendRecord> &updates = {},
                    const QStringList &deletes = {})
{
    WriterBatch b;
    b.creates = creates;
    b.updates = updates;
    b.deletes = deletes;
    return b;
}

BackendRecord recordWithWire(const QJsonObject &wire)
{
    BackendRecord r;
    r.id = wire.value(QStringLiteral("id")).toString();
    r.type = QStringLiteral("event");
    r.displayName = wire.value(QStringLiteral("summary")).toString();
    r.data = QJsonDocument(wire).toJson(QJsonDocument::Compact);
    return r;
}

} // namespace

class TestGoogleCalendarBackend : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        m_server = new MockGoogleServer(this);
        QVERIFY(m_server->start());
        m_backend = new GoogleCalendarBackend(this);
        m_backend->setBaseUrl(m_server->baseUrl());
        m_backend->setAccessToken(QStringLiteral("test-token"));
    }

    void cleanup()
    {
        delete m_backend;
        delete m_server;
        m_backend = nullptr;
        m_server = nullptr;
    }

    // Discovery: accessRole drives writability; primary flags default.
    void loadCalendarsMapsAccessRoles()
    {
        QJsonArray list{
            QJsonObject{ { QStringLiteral("id"), QStringLiteral("primary") },
                         { QStringLiteral("summary"), QStringLiteral("Main") },
                         { QStringLiteral("accessRole"),
                           QStringLiteral("owner") },
                         { QStringLiteral("primary"), true } },
            QJsonObject{ { QStringLiteral("id"), QStringLiteral("hol") },
                         { QStringLiteral("summary"), QStringLiteral("Holidays") },
                         { QStringLiteral("accessRole"),
                           QStringLiteral("reader") } }
        };
        m_server->setCalendarList(list);

        QSignalSpy finishedSpy(m_backend,
                               &Kalburator::Sync::SyncBackend::loadCalendarsFinished);
        m_backend->loadCalendars(QStringLiteral("coll"));
        QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() == 1
                                     && finishedSpy.at(0).at(1).toBool(),
                                 5000);

        const auto collections = m_backend->availableCollections();
        QCOMPARE(collections.size(), 2);
        bool sawPrimary = false;
        bool sawReader = false;
        for (const auto &c : collections) {
            if (c.id == QLatin1String("primary")) {
                sawPrimary = true;
                QVERIFY(!c.readOnly);
                QVERIFY(c.isDefault);
            } else if (c.id == QLatin1String("hol")) {
                sawReader = true;
                QVERIFY(c.readOnly);
            }
        }
        QVERIFY(sawPrimary && sawReader);
    }

    // Initial fetch: full listing, sync token committed, memo served once.
    void initialFetchReportsFullSetAndMemoIsSingleShot()
    {
        QJsonArray events{ makeEvent(QStringLiteral("e1"), QStringLiteral("One")),
                           makeEvent(QStringLiteral("e2"), QStringLiteral("Two")) };
        m_server->setEvents(QStringLiteral("primary"), events);

        auto *op = m_backend->fetchItems(QStringLiteral("primary"));
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QCOMPARE(op->fetchedItems().size(), 2);

        QList<BackendRecord> records;
        QString err;
        QVERIFY(m_backend->recordsFromLastFetch(QStringLiteral("primary"),
                                                records, err));
        QCOMPARE(records.size(), 2);
        // Second serve must fail — single-shot H5/O23 contract.
        QVERIFY(!m_backend->recordsFromLastFetch(QStringLiteral("primary"),
                                                 records, err));

        // Wire truth: the first listing carried no syncToken param; the
        // mock minted one for us to resume from later (checked by the
        // persistence test below).
    }

    // Incremental fetch: presenting the persisted token delivers ONLY
    // changes; status:cancelled tombstones remove from the merged view;
    // every fetch still reports the FULL merged set.
    void incrementalWalkMergesChangesAndTombstones()
    {
        QTemporaryDir cacheDir;
        m_backend->setCacheDir(cacheDir.path());

        QJsonArray seed{ makeEvent(QStringLiteral("e1"), QStringLiteral("One")),
                         makeEvent(QStringLiteral("e2"), QStringLiteral("Two")) };
        m_server->setEvents(QStringLiteral("primary"), seed);
        auto *op1 = m_backend->fetchItems(QStringLiteral("primary"));
        QTRY_VERIFY_WITH_TIMEOUT(op1->isFinished(), 5000);
        QCOMPARE(op1->fetchedItems().size(), 2);

        // Queue an update to e1 and a cancellation of e2. The queued items
        // are delivered when the CURRENT live token is presented.
        const QString liveToken = m_server->requests().isEmpty()
            ? QString() : QStringLiteral("sync_1");
        QJsonObject updatedE1 = makeEvent(QStringLiteral("e1"), QStringLiteral("One!"));
        QJsonObject cancelledE2 = makeEvent(QStringLiteral("e2"), QStringLiteral("Two"));
        cancelledE2.insert(QStringLiteral("status"),
                           QStringLiteral("cancelled"));
        m_server->queueSyncChanges(
            QStringLiteral("primary"), liveToken,
            { updatedE1, cancelledE2 });

        auto *op2 = m_backend->fetchItems(QStringLiteral("primary"));
        QTRY_VERIFY_WITH_TIMEOUT(op2->isFinished(), 5000);
        QCOMPARE(op2->state(), Kalburator::Sync::SyncOperation::Succeeded);
        // FULL merged view: e1 updated, e2 tombstoned ⇒ exactly 1 record,
        // with e1's NEW wire content (hash reflects the change).
        QList<BackendRecord> records;
        QString err;
        QVERIFY(m_backend->recordsFromLastFetch(QStringLiteral("primary"),
                                                records, err));
        QCOMPARE(records.size(), 1);
        QCOMPARE(records.first().id, QStringLiteral("e1"));
        QVERIFY(records.first().displayName == QLatin1String("One!")
                || records.first().data.contains("One!"));
    }

    // 410 Gone self-heals: expire tokens server-side; the next fetch fails
    // over to ONE fresh initial listing and still succeeds.
    void goneTokenSelfHealsToFreshListing()
    {
        QJsonArray seed{ makeEvent(QStringLiteral("e1"), QStringLiteral("One")) };
        m_server->setEvents(QStringLiteral("primary"), seed);
        auto *op1 = m_backend->fetchItems(QStringLiteral("primary"));
        QTRY_VERIFY_WITH_TIMEOUT(op1->isFinished(), 5000);

        m_server->expireSyncTokens(QStringLiteral("primary"));

        auto *op2 = m_backend->fetchItems(QStringLiteral("primary"));
        QTRY_VERIFY_WITH_TIMEOUT(op2->isFinished(), 5000);
        QCOMPARE(op2->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QCOMPARE(op2->fetchedItems().size(), 1);
    }

    // Persistence: a second backend instance over the same cache dir
    // resumes with the stored sync token (its FIRST listing request
    // carries syncToken=).
    void persistenceResumesWithStoredToken()
    {
        QTemporaryDir cacheDir;
        m_backend->setCacheDir(cacheDir.path());
        QJsonArray seed{ makeEvent(QStringLiteral("e1"), QStringLiteral("One")) };
        m_server->setEvents(QStringLiteral("primary"), seed);
        auto *op1 = m_backend->fetchItems(QStringLiteral("primary"));
        QTRY_VERIFY_WITH_TIMEOUT(op1->isFinished(), 5000);

        delete m_backend;
        m_backend = new GoogleCalendarBackend(this);
        m_backend->setBaseUrl(m_server->baseUrl());
        m_backend->setAccessToken(QStringLiteral("test-token"));
        m_backend->setCacheDir(cacheDir.path());

        auto *op2 = m_backend->fetchItems(QStringLiteral("primary"));
        QTRY_VERIFY_WITH_TIMEOUT(op2->isFinished(), 5000);
        QCOMPARE(op2->state(), Kalburator::Sync::SyncOperation::Succeeded);

        // Find this instance's first GET of the events collection and
        // verify it presented the resumed token.
        bool resumedRequestSeen = false;
        const auto reqs = m_server->requests();
        for (int i = reqs.size() - 1; i >= 0; --i) {
            if (reqs.at(i).method == "GET"
                && reqs.at(i).path.contains(QLatin1String("/events"))) {
                QVERIFY2(reqs.at(i).path.contains(QLatin1String("syncToken=")),
                         qPrintable(reqs.at(i).path));
                resumedRequestSeen = true;
                break;
            }
        }
        QVERIFY(resumedRequestSeen);
    }

    // O67(b)(1): creates strip read-only created/updated before POST —
    // the mock answers 400 otherwise, so success proves the strip.
    // The minted transport id bridges via addIdAlias.
    void createStripsReadOnlyFieldsAndBridgesAlias()
    {
        m_server->setEvents(QStringLiteral("primary"), {});
        QJsonObject authored = makeEvent(QStringLiteral("requested-id"),
                                         QStringLiteral("New event"));
        authored.insert(QStringLiteral("iCalUID"),
                        QStringLiteral("anchor-uid@example.com"));
        // Demote emits these from canon created/lastModified:
        authored.insert(QStringLiteral("created"),
                        QStringLiteral("2026-08-25T00:00:00Z"));
        authored.insert(QStringLiteral("updated"),
                        QStringLiteral("2026-08-25T00:00:00Z"));

        auto *op = m_backend->applyRecords(
            QStringLiteral("primary"),
            batchOf({ recordWithWire(authored) }));
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QCOMPARE(op->failedUids().size(), 0);
        // Alias: requested id → minted transport id (mockevt…).
        QCOMPARE(op->succeededUids().size(), 1);
        QVERIFY(op->succeededUids().first().startsWith(
            QLatin1String("mockevt")));
        const auto aliases = op->idAliases();
        bool aliasFound = false;
        for (auto it = aliases.constBegin(); it != aliases.constEnd(); ++it) {
            if (it.key() == QLatin1String("requested-id"))
                aliasFound = true;
        }
        QVERIFY(aliasFound);

        // Wire truth: the POST body must NOT contain created/updated/id
        // (O67(b)(1) + O68) but MUST keep iCalUID.
        const QByteArray postBody =
            m_server->requests().last().method == "POST"
                ? m_server->requests().last().body : QByteArray();
        QVERIFY(!postBody.isEmpty());
        const QJsonObject sent =
            QJsonDocument::fromJson(postBody).object();
        QVERIFY(!sent.contains(QLatin1String("created")));
        QVERIFY(!sent.contains(QLatin1String("updated")));
        QVERIFY(!sent.contains(QLatin1String("id")));
        QCOMPARE(sent.value(QStringLiteral("iCalUID")).toString(),
                 QStringLiteral("anchor-uid@example.com"));
    }

    // Updates go out as PATCH in place; deletes accept 204 and treat 410
    // as success (idempotent semantics).
    void updatePatchesAndDeleteAcceptsGone()
    {
        m_server->setEvents(QStringLiteral("primary"),
                            { makeEvent(QStringLiteral("live-1"),
                                        QStringLiteral("Original")) });
        auto *seed = m_backend->fetchItems(QStringLiteral("primary"));
        QTRY_VERIFY_WITH_TIMEOUT(seed->isFinished(), 5000);

        QJsonObject patch = makeEvent(QStringLiteral("live-1"),
                                      QStringLiteral("Edited"));
        auto *upd = m_backend->applyRecords(
            QStringLiteral("primary"),
            batchOf({}, { recordWithWire(patch) }));
        QTRY_VERIFY_WITH_TIMEOUT(upd->isFinished(), 5000);
        QCOMPARE(upd->state(), Kalburator::Sync::SyncOperation::Succeeded);
        const auto reqsAfterUpdate = m_server->requests();
        QCOMPARE(reqsAfterUpdate.last().method, QByteArray("PATCH"));
        QVERIFY(reqsAfterUpdate.last().path.contains(QLatin1String("live-1")));

        // Delete an id the server no longer knows ⇒ 410 ⇒ success.
        auto *del = m_backend->applyRecords(
            QStringLiteral("primary"), batchOf({}, {}, { QStringLiteral("ghost") }));
        QTRY_VERIFY_WITH_TIMEOUT(del->isFinished(), 5000);
        QCOMPARE(del->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QCOMPARE(del->succeededUids(), QStringList{ QStringLiteral("ghost") });
    }

private:
    MockGoogleServer *m_server = nullptr;
    GoogleCalendarBackend *m_backend = nullptr;
};

QTEST_MAIN(TestGoogleCalendarBackend)
#include "tst_google_calendar_backend.moc"
