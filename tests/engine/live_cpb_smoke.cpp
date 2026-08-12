// CP-B live smoke (scratch — NOT a CI test, delete after the checkpoint).
// Drives SyncEngine end-to-end against a REAL Radicale server:
//   Phase 1: local create -> sync -> event lands on server
//   Phase 2: server-side modify (independent backend) -> sync -> local converges
//   Phase 3: settle cycle, then quiet cycle (token soundness live)
//   Phase 4: pulled cable — SIGSTOP the server; a dirty sync must FAIL within
//            the transfer timeout (not hang), and after SIGCONT the engine
//            must accept and complete a fresh runSync that repairs (O22/O17).
//
// Env: RADICALE_URL (default http://127.0.0.1:5233/testuser1/),
//      RADICALE_USER/PASS (testuser1/password1),
//      RADICALE_PID (pid to SIGSTOP/SIGCONT for phase 4; phase skipped if unset)
//
// Both backends are relocated onto ONE shared I/O thread — the exact
// topology PlanStan adopts in H7. All direct backend calls are marshaled.

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFutureWatcher>
#include <QTemporaryDir>
#include <QThread>

#include <csignal>
#include <cstdio>

#include "backendconfiguration.h"
#include "backendregistry.h"
#include "baselinestore.h"
#include "caldavprovider.h"
#include "collectioninfo.h"
#include "iblobbackend.h"
#include "isynchost.h"
#include "localbackend.h"
#include "pluginmanager.h"
#include "remotecalendarbackend.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "syncbackend.h"
#include "syncengine.h"
#include "syncrequest.h"
#include "synctypes.h"

using namespace Kalburator::Sync;
using Kalburator::Engine::SyncEngine;
using Kalburator::Engine::SyncRequest;
using Kalburator::Storage::BaselineStore;

static int g_failures = 0;

#define CHECK(cond, label)                                                     \
    do {                                                                       \
        if (cond) {                                                            \
            std::printf("PASS  %s\n", label);                                  \
        } else {                                                               \
            std::printf("FAIL  %s\n", label);                                  \
            ++g_failures;                                                      \
        }                                                                      \
        std::fflush(stdout);                                                   \
    } while (0)

namespace {

class SmokeHost final : public ISyncHost
{
public:
    explicit SmokeHost(BackendRegistry *registry) : m_registry(registry) {}
    SyncBackend *backendById(const QString &id) override
    {
        return m_registry ? static_cast<SyncBackend *>(m_registry->backendInstance(id)) : nullptr;
    }
    QHash<QString, SyncBackend *> backends() override
    {
        QHash<QString, SyncBackend *> out;
        for (const auto &id : m_registry->registeredInstanceIds())
            out.insert(id, static_cast<SyncBackend *>(m_registry->backendInstance(id)));
        return out;
    }
    ISyncConfigStore *configStore() override { return nullptr; }
    void syncStarted(const QString &, const Kalburator::Shape::LossProfile &) override {}
    void recordChanged(const QString &, const QString &, ChangeKind) override {}
private:
    BackendRegistry *m_registry = nullptr;
};

bool waitFutureBool(QFuture<bool> f, int timeoutMs)
{
    QDeadlineTimer dl(timeoutMs);
    while (!f.isFinished() && !dl.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return f.isFinished();
}

// Phase 1: CalDavProvider still emits one spec per collection (domainId ==
// collection id), so a known-collection lookup against createBackends() is
// equivalent to the old createBackend(collectionId).
std::unique_ptr<IBlobBackend>
backendForCollection(IProvider &provider, const QString &collectionId)
{
    auto specs = provider.createBackends();
    for (auto &spec : specs) {
        if (spec.domainId == collectionId) return std::move(spec.backend);
    }
    return nullptr;
}

SyncResult runOnce(SyncEngine &engine, int timeoutMs = 30000)
{
    SyncRequest req;
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    auto f = engine.runSync(req);
    QDeadlineTimer dl(timeoutMs);
    while (!f.isFinished() && !dl.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    SyncResult r;
    if (!f.isFinished()) {
        r.success = false;
        r.errorMessage = QStringLiteral("TIMEOUT: runSync future never finished");
        return r;
    }
    const auto results = f.resultAt(0);
    return results.isEmpty() ? SyncResult{} : results.first();
}

QByteArray makeVEvent(const char *uid, const char *summary)
{
    QByteArray v;
    v += "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//cpb//smoke//EN\r\n";
    v += "BEGIN:VEVENT\r\nUID:"; v += uid;
    v += "\r\nDTSTAMP:20260705T120000Z\r\nDTSTART:20260706T090000Z\r\n";
    v += "DTEND:20260706T100000Z\r\nSUMMARY:"; v += summary;
    v += "\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    return v;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QString url  = qEnvironmentVariable("RADICALE_URL",  "http://127.0.0.1:5233/testuser1/");
    const QString user = qEnvironmentVariable("RADICALE_USER", "testuser1");
    const QString pass = qEnvironmentVariable("RADICALE_PASS", "password1");
    const qint64  srvPid = qEnvironmentVariable("RADICALE_PID", "0").toLongLong();

    Kalburator::Shape::ShapeRegistries shape;
    BackendRegistry pmRegistry;
    Kalburator::PluginManager pm(&pmRegistry, shape);
    Kalburator::registerStockPlugins(pm);

    // ---- connect provider, pick the "smoke" calendar ----
    BackendConfiguration cfg;
    cfg.id   = QStringLiteral("cpb-live");
    cfg.type = QStringLiteral("caldav");
    cfg.connectionParams.insert(QStringLiteral("url"), url);
    cfg.connectionParams.insert(QStringLiteral("username"), user);
    cfg.connectionParams.insert(QStringLiteral("password"), pass);

    CalDavProvider provider;
    provider.load(cfg);
    if (!waitFutureBool(provider.connect(), 10000) || !provider.isConnected()) {
        std::printf("FATAL: provider.connect() failed against %s\n", qUtf8Printable(url));
        return 2;
    }
    QString collId;
    for (const auto &c : provider.collections())
        if (c.id.contains(QLatin1String("smoke"))) collId = c.id;
    if (collId.isEmpty()) {
        std::printf("FATAL: no 'smoke' calendar found on server\n");
        return 2;
    }
    std::printf("INFO  connected; smoke collection id = %s\n", qUtf8Printable(collId));

    auto rawRemote = backendForCollection(provider, collId);
    auto *remote = dynamic_cast<RemoteCalendarBackend *>(rawRemote.get());
    if (!remote) { std::printf("FATAL: createBackends() produced no RemoteCalendarBackend\n"); return 2; }
    rawRemote.release(); // deleted via invokeMethod on its own thread below

    QTemporaryDir remoteState, localDir, fpDir, baselineDir;
    remote->setDbPath(remoteState.filePath(QStringLiteral("ctags.db")));
    remote->setCacheDir(remoteState.path());
    remote->setTransferTimeoutMs(5000); // keep the pulled-cable phase quick

    auto *local = new LocalBackend(localDir.path());
    local->setDbPath(fpDir.filePath(QStringLiteral("local-fp.db")));
    const QString localCollection = QStringLiteral("src");
    {
        CollectionInfo info;
        info.id = localCollection; info.name = localCollection;
        info.type = QStringLiteral("calendar");
        if (local->createCollection(info).isEmpty()) {
            std::printf("FATAL: LocalBackend createCollection failed\n");
            return 2;
        }
    }
    // seed the phase-1 event BEFORE relocation (simplest)
    auto *blobLocal = static_cast<IBlobBackend *>(local);
    {
        BackendRecord rec;
        rec.id = QStringLiteral("cpb-evt-1");
        rec.data = makeVEvent("cpb-evt-1", "CP-B live smoke");
        if (blobLocal->createRecord(localCollection, rec).isEmpty()) {
            std::printf("FATAL: local createRecord failed\n");
            return 2;
        }
    }

    // ---- H7 topology: both backends on ONE shared I/O thread ----
    QThread ioThread;
    ioThread.setObjectName(QStringLiteral("cpb-io"));
    ioThread.start();
    auto ioThreadGuard = qScopeGuard([&]() {
        QMetaObject::invokeMethod(remote, [remote]() { delete remote; },
                                  Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(local, [local]() { delete local; },
                                  Qt::BlockingQueuedConnection);
        ioThread.quit();
        ioThread.wait();
    });
    remote->setParent(nullptr);
    remote->moveToThread(&ioThread);
    local->moveToThread(&ioThread);

    BackendRegistry registry;
    registry.registerBackendInstance(remote->backendId(), remote);
    registry.registerBackendInstance(local->backendId(), local);

    SmokeHost host(&registry);
    SyncEngine engine(&registry, &host, shape);
    engine.setSkipUnchangedMappings(true);

    BaselineStore baselines(baselineDir.filePath(QStringLiteral("baselines.db")));
    engine.setBaselineStore(&baselines);

    SyncMapping mapping;
    mapping.id             = QStringLiteral("cpb-live-mapping");
    mapping.sourceBackend  = local->backendId();
    mapping.sourceCalendar = localCollection;
    mapping.targetBackend  = remote->backendId();
    mapping.targetCalendar = collId;
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::LastWriteWins;
    mapping.enabled        = true;
    engine.setSyncMappings({ mapping });

    auto remoteRecords = [&]() {
        QString err;
        QList<BackendRecord> recs;
        QMetaObject::invokeMethod(remote, [&]() {
            remote->loadRecordsOrError(collId, recs, err);
        }, Qt::BlockingQueuedConnection);
        return recs;
    };
    auto remoteHas = [&](const char *needle) {
        const auto recs = remoteRecords();
        for (const auto &rec : recs)
            if (rec.data.contains(needle)) return true;
        return false;
    };

    // ---- Phase 1: local create -> sync -> lands on server ----
    {
        const SyncResult r = runOnce(engine);
        if (!r.success)
            std::printf("INFO  phase1 error: %s\n", qUtf8Printable(r.errorMessage));
        CHECK(r.success, "phase1: initial sync succeeds");
        CHECK(remoteHas("cpb-evt-1"), "phase1: event visible on server after sync");
    }

    // ---- Phase 2: server-side modify -> sync -> local converges ----
    {
        // Modify via a second, independent backend instance (main thread)
        // so the change is genuinely foreign to the engine's backend.
        QTemporaryDir foreignState;
        auto rawForeign = backendForCollection(provider, collId);
        auto *foreign = dynamic_cast<RemoteCalendarBackend *>(rawForeign.get());
        foreign->setDbPath(foreignState.filePath(QStringLiteral("ctags.db")));
        foreign->setCacheDir(foreignState.path());
        QString err;
        QList<BackendRecord> recs;
        foreign->loadRecordsOrError(collId, recs, err);
        bool updated = false;
        for (auto rec : recs) {
            const bool hit = rec.data.contains("cpb-evt-1");
            if (!hit) continue;
            rec.data.replace("CP-B live smoke", "CP-B live smoke EDITED");
            updated = foreign->updateRecord(rec);
        }
        CHECK(updated, "phase2: foreign server-side edit applied");

        const SyncResult r = runOnce(engine);
        if (!r.success)
            std::printf("INFO  phase2 error: %s\n", qUtf8Printable(r.errorMessage));
        CHECK(r.success, "phase2: sync after foreign edit succeeds");

        bool localHasEdit = false;
        QDirIterator it(localDir.path(), { QStringLiteral("*.ics") },
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QFile f(it.next());
            f.open(QIODevice::ReadOnly);
            if (f.readAll().contains("EDITED")) localHasEdit = true;
        }
        CHECK(localHasEdit, "phase2: foreign edit converged to local side");
    }

    // ---- Phase 3: settle + quiet cycles ----
    {
        const SyncResult r1 = runOnce(engine); // settle (post-write re-diff)
        CHECK(r1.success, "phase3: settle cycle succeeds");
        QElapsedTimer t; t.start();
        const SyncResult r2 = runOnce(engine);
        CHECK(r2.success, "phase3: quiet cycle succeeds");
        std::printf("INFO  quiet cycle wall time: %lld ms\n", (long long)t.elapsed());
    }

    // ---- Phase 4: pulled cable (O22) ----
    if (srvPid > 0) {
        // dirty the source so the mapping is NOT skip-eligible and the
        // engine must actually talk to the frozen server
        {
            BackendRecord rec;
            rec.id = QStringLiteral("cpb-evt-2");
            rec.data = makeVEvent("cpb-evt-2", "pulled cable probe");
            QString newId;
            QMetaObject::invokeMethod(local, [&]() {
                newId = blobLocal->createRecord(localCollection, rec);
            }, Qt::BlockingQueuedConnection);
            CHECK(!newId.isEmpty(), "phase4: local dirty record created");
        }
        ::kill((pid_t)srvPid, SIGSTOP);

        QElapsedTimer t; t.start();
        const SyncResult r = runOnce(engine, 90000);
        const qint64 ms = t.elapsed();
        std::printf("INFO  pulled-cable sync finished in %lld ms: success=%d err=%s\n",
                    (long long)ms, r.success ? 1 : 0, qUtf8Printable(r.errorMessage));
        CHECK(!r.success, "phase4: sync against frozen server FAILS (does not hang)");
        CHECK(ms < 60000, "phase4: failure arrives within the transfer-timeout window");

        ::kill((pid_t)srvPid, SIGCONT);
        QDeadlineTimer settle(1500);
        while (!settle.hasExpired())
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

        const SyncResult r2 = runOnce(engine, 60000);
        if (!r2.success)
            std::printf("INFO  phase4 recovery error: %s\n", qUtf8Printable(r2.errorMessage));
        CHECK(r2.success, "phase4: engine accepts and completes a fresh runSync after recovery");
        CHECK(remoteHas("cpb-evt-2"), "phase4: change stranded by the outage lands after recovery");
    } else {
        std::printf("SKIP  phase4 (RADICALE_PID not set)\n");
    }

    // ---- teardown: engine worker pool first, then backends/I/O thread (H7 order) ----
    engine.stopWorkerPool();

    std::printf(g_failures == 0 ? "\nCP-B LIVE SMOKE: ALL PASS\n"
                                : "\nCP-B LIVE SMOKE: %d FAILURE(S)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
