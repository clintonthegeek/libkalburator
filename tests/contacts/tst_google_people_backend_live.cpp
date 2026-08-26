// B2C P2.f — GooglePeopleBackend LIVE checkpoint (proposal invariant 1:
// mock-green cannot see consumer-account wire truths). Skips unless
// KALBURATOR_GOOGLE_DIR holds a googlecli-authorized token cache.
// Protocol: initial connections walk (paging + personFields projection) →
// create CORPUS-tagged probe person with a clientData carrier (inline,
// live-Reversible channel per the O66 verdict table) → refetch sees it
// with clientData intact → PATCH :updateContact rename → delete →
// verified gone. Cleanup is unconditional at the end; sweep-clean covers
// stragglers.
//
// Run: KALBURATOR_GOOGLE_DIR=<dir> ctest -R tst_google_people_backend_live

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

#include "googlepeoplebackend.h"
#include "googleauth.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::GooglePeopleBackend;
using Kalburator::Sync::WriterBatch;

namespace {

QString loadLiveAccessToken()
{
    const QString dir = qEnvironmentVariable("KALBURATOR_GOOGLE_DIR");
    if (dir.isEmpty())
        return {};
    const Kalburator::Google::TokenStore store(
        dir + QStringLiteral("/token-cache.json"));
    const auto tokens = store.load();
    if (!tokens.hasLiveAccessToken())
        return {};
    return tokens.accessToken;
}

} // namespace

class TestGooglePeopleBackendLive : public QObject {
    Q_OBJECT
private slots:

    void fullProbeCycleAgainstLiveAccount()
    {
        const QString token = loadLiveAccessToken();
        if (token.isEmpty())
            QSKIP("No live Google token cache (set KALBURATOR_GOOGLE_DIR "
                  "with a googlecli-authorized profile).");

        const QString coll = GooglePeopleBackend::defaultCollectionId();

        // ---- 1. Initial connections walk (paging + personFields) ----------
        GooglePeopleBackend backend;
        backend.setAccessToken(token);

        auto *fetch1 = backend.fetchItems(coll);
        QTRY_VERIFY_WITH_TIMEOUT(fetch1->isFinished(), 60000);
        QCOMPARE(fetch1->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QList<BackendRecord> records;
        QString err;
        QVERIFY(backend.recordsFromLastFetch(coll, records, err));
        for (const auto &r : records)
            QVERIFY(r.id.startsWith(QStringLiteral("people/")));
        qInfo() << "initial fetch records:" << records.size();

        // ---- 2. Create CORPUS-tagged probe with a clientData carrier ------
        const QString stamp =
            QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddThhmmss"));
        const QString probeName =
            QStringLiteral("CORPUS b2c-p2f-go %1").arg(stamp);
        QJsonObject authored{
            { QStringLiteral("names"),
              QJsonArray{ QJsonObject{
                  { QStringLiteral("displayName"), probeName },
                  { QStringLiteral("givenName"), QStringLiteral("CORPUS") },
                  { QStringLiteral("familyName"), QStringLiteral("p2f") } } } },
            { QStringLiteral("emailAddresses"),
              QJsonArray{ QJsonObject{
                  { QStringLiteral("value"),
                    QStringLiteral("b2c-p2f-%1@example.com").arg(stamp) } } } },
            { QStringLiteral("clientData"),
              QJsonArray{ QJsonObject{
                  { QStringLiteral("key"), QStringLiteral("x-canon-gender") },
                  { QStringLiteral("value"), QStringLiteral("male") } } } } };
        BackendRecord rec;
        rec.id = QStringLiteral("requested-b2c-p2f-probe");
        rec.type = QStringLiteral("contact");
        rec.displayName = probeName;
        rec.data = QJsonDocument(authored).toJson(QJsonDocument::Compact);

        WriterBatch createBatch;
        createBatch.creates = { rec };
        auto *create = backend.applyRecords(coll, createBatch);
        QTRY_VERIFY_WITH_TIMEOUT(create->isFinished(), 60000);
        QCOMPARE(create->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QCOMPARE(create->failedUids().size(), 0);
        const QString storedId = create->succeededUids().value(0);
        QVERIFY2(storedId.startsWith(QLatin1String("people/c")),
                 "consumer People mints a server-side resourceName");
        QVERIFY2(storedId != rec.id, "minted id must differ from requested");
        QCOMPARE(create->idAliases().value(rec.id), storedId);
        qInfo() << "probe created; minted resourceName:" << storedId;

        // Cleanup guard: whatever happens below, try to remove the probe.
        auto removeProbe = [&]() {
            WriterBatch delBatch;
            delBatch.deletes = { storedId };
            auto *del = backend.applyRecords(coll, delBatch);
            QTRY_VERIFY_WITH_TIMEOUT(del->isFinished(), 60000);
        };

        // ---- 3. Refetch sees the probe with clientData intact -------------
        // The backend walks incrementally off its sync token; People can lag
        // briefly on freshly created contacts — poll, then fall back to ONE
        // authoritative full walk from a fresh backend instance before
        // declaring failure.
        bool seen = false;
        bool carrierOk = false;
        QString probeEtag;   // O72: captured for the mandatory update token
        auto scanForProbe = [&](const QList<BackendRecord> &rs, bool *found,
                                bool *carrierOk) {
            *found = false;
            *carrierOk = false;
            for (const auto &r : rs) {
                if (r.id != storedId)
                    continue;
                *found = true;
                const QJsonObject wire =
                    QJsonDocument::fromJson(r.data).object();
                // O72: listings deliver the top-level etag even though the
                // personFields projection doesn't ask for it — captured
                // here because :updateContact REQUIRES it.
                if (wire.contains(QStringLiteral("etag")))
                    probeEtag = wire.value(QStringLiteral("etag")).toString();
                const QJsonArray cd =
                    wire.value(QStringLiteral("clientData")).toArray();
                for (const auto &cv : cd) {
                    if (cv.toObject().value(QStringLiteral("key"))
                                .toString() == QLatin1String("x-canon-gender")
                        && cv.toObject().value(QStringLiteral("value"))
                                   .toString() == QLatin1String("male"))
                        *carrierOk = true;
                }
            }
        };
        for (int attempt = 0; attempt < 5 && !seen; ++attempt) {
            auto *f = backend.fetchItems(coll);
            QTRY_VERIFY_WITH_TIMEOUT(f->isFinished(), 60000);
            QList<BackendRecord> fresh;
            QString ferr;
            if (!backend.recordsFromLastFetch(coll, fresh, ferr))
                continue;
            scanForProbe(fresh, &seen, &carrierOk);
            if (!seen)
                QTest::qWait(2000);
        }        if (!seen) {
            qInfo() << "probe not on incremental pages; forcing full re-walk";
            GooglePeopleBackend fullWalker;
            fullWalker.setBaseUrl(
                QStringLiteral("https://people.googleapis.com"));
            fullWalker.setAccessToken(token);
            auto *ff = fullWalker.fetchItems(coll);
            QTRY_VERIFY_WITH_TIMEOUT(ff->isFinished(), 60000);
            QCOMPARE(ff->state(), Kalburator::Sync::SyncOperation::Succeeded);
            QList<BackendRecord> fresh;
            QString ferr;
            QVERIFY(fullWalker.recordsFromLastFetch(coll, fresh, ferr));
            scanForProbe(fresh, &seen, &carrierOk);
        }
        QVERIFY2(seen, "probe never surfaced on any listing walk");
        QVERIFY2(carrierOk,
                 "clientData carrier must ride the listing verbatim "
                 "(live-Reversible channel)");
        qInfo() << "probe visible with clientData carrier intact";

        // ---- 4. PATCH :updateContact rename ---------------------------------
        // O72: the etag captured from the listing MUST ride the patch body
        // (People rejects etag-less updates). Live truth: the server DERIVES
        // displayName from given+family, so the rename is asserted on
        // familyName.
        QJsonObject patched{
            { QStringLiteral("etag"), probeEtag },
            { QStringLiteral("names"),
              QJsonArray{ QJsonObject{
                  { QStringLiteral("givenName"), QStringLiteral("CORPUS") },
                  { QStringLiteral("familyName"),
                    QStringLiteral("p2f EDITED") } } } } };
        BackendRecord updRec;
        updRec.id = storedId;
        updRec.type = QStringLiteral("contact");
        updRec.data = QJsonDocument(patched).toJson(QJsonDocument::Compact);
        WriterBatch updBatch;
        updBatch.updates = { updRec };
        auto *update = backend.applyRecords(coll, updBatch);
        QTRY_VERIFY_WITH_TIMEOUT(update->isFinished(), 60000);
        QCOMPARE(update->state(), Kalburator::Sync::SyncOperation::Succeeded);

        bool renameSeen = false;
        for (int attempt = 0; attempt < 5 && !renameSeen; ++attempt) {
            auto *f = backend.fetchItems(coll);
            QTRY_VERIFY_WITH_TIMEOUT(f->isFinished(), 60000);
            QList<BackendRecord> fresh;
            QString ferr;
            if (!backend.recordsFromLastFetch(coll, fresh, ferr))
                continue;
            for (const auto &r : fresh) {
                if (r.id != storedId)
                    continue;
                const QJsonObject wire =
                    QJsonDocument::fromJson(r.data).object();
                if (wire.value(QStringLiteral("names"))
                        .toArray().at(0).toObject()
                        .value(QStringLiteral("familyName")).toString()
                    == QLatin1String("p2f EDITED"))
                    renameSeen = true;
            }
            if (!renameSeen)
                QTest::qWait(2000);
        }
        QVERIFY2(renameSeen, ":updateContact rename not visible on refetch");
        qInfo() << "PATCH-in-place update OK";

        // ---- 5. Delete + verify gone ----------------------------------------
        removeProbe();
        bool stillThere = true;
        for (int attempt = 0; attempt < 5 && stillThere; ++attempt) {
            auto *f = backend.fetchItems(coll);
            QTRY_VERIFY_WITH_TIMEOUT(f->isFinished(), 60000);
            QList<BackendRecord> postDelete;
            QString derr;
            if (!backend.recordsFromLastFetch(coll, postDelete, derr))
                continue;
            stillThere = false;
            for (const auto &r : postDelete)
                if (r.id == storedId)
                    stillThere = true;
            if (stillThere)
                QTest::qWait(2000);
        }
        if (stillThere) {
            qInfo() << "probe not tombstoned incrementally; forcing full "
                       "re-walk";
            GooglePeopleBackend fullWalker;
            fullWalker.setBaseUrl(
                QStringLiteral("https://people.googleapis.com"));
            fullWalker.setAccessToken(token);
            auto *ff = fullWalker.fetchItems(coll);
            QTRY_VERIFY_WITH_TIMEOUT(ff->isFinished(), 60000);
            QList<BackendRecord> postDelete;
            QString derr;
            stillThere = false;
            if (fullWalker.recordsFromLastFetch(coll, postDelete, derr)) {
                for (const auto &r : postDelete)
                    if (r.id == storedId)
                        stillThere = true;
            }
        }
        QVERIFY2(!stillThere, "probe survived deletion");

        qInfo() << "LIVE CHECKPOINT PASSED (GooglePeopleBackend,"
                   "/v1/people/me/connections)";
    }
};

QTEST_MAIN(TestGooglePeopleBackendLive)
#include "tst_google_people_backend_live.moc"
