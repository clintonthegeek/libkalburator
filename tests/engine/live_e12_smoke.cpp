// E12 live smoke (scratch — NOT a CI test, delete after the phase closes).
// Live re-run of the CP-B kill-mid-push protocol (O28 shape) but with
// TIMESTAMP-LESS source events — the O41 phantom-conflict producer this
// phase fixes. Drives SyncEngine end-to-end against a REAL Radicale server;
// the actual process kill/restart is orchestrated by the calling shell
// script (a real crash needs the listening socket to actually close, which
// SIGSTOP does not give us) — this binary just runs ONE sync cycle per
// invocation against state fixed by env vars so it persists across the
// kill/restart boundary between two process invocations.
//
// Env: RADICALE_URL/USER/PASS (as live_cpb_smoke.cpp), E12_COLLECTION
// (calendar name substring to select, default "phantomlive"), E12_LOCAL_DIR
// (fixed local mirror root — must persist across invocations),
// E12_FP_DB / E12_BASELINE_DB / E12_REMOTE_STATE_DIR (fixed state paths),
// E12_PHASE=seed|sync|verify, E12_N (item count, phase=seed only).

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFuture>

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

QByteArray makeVEventNoTimestamps(const QString &uid, const QString &summary)
{
    // No CREATED, no LAST-MODIFIED — only DTSTAMP (RFC 5545 mandatory), the
    // exact O41 shape: content some external tool might drop in.
    QByteArray v;
    v += "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//e12//smoke//EN\r\n";
    v += "BEGIN:VEVENT\r\nUID:"; v += uid.toUtf8();
    v += "\r\nDTSTAMP:20260701T120000Z\r\nDTSTART:20260706T090000Z\r\n";
    v += "DTEND:20260706T100000Z\r\nSUMMARY:"; v += summary.toUtf8();
    v += "\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    return v;
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

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QString url  = qEnvironmentVariable("RADICALE_URL",  "http://127.0.0.1:5233/testuser1/");
    const QString user = qEnvironmentVariable("RADICALE_USER", "testuser1");
    const QString pass = qEnvironmentVariable("RADICALE_PASS", "password1");
    const QString collNeedle = qEnvironmentVariable("E12_COLLECTION", "phantomlive");
    const QString localDirPath = qEnvironmentVariable("E12_LOCAL_DIR");
    const QString fpDbPath = qEnvironmentVariable("E12_FP_DB");
    const QString baselineDbPath = qEnvironmentVariable("E12_BASELINE_DB");
    const QString remoteStateDirPath = qEnvironmentVariable("E12_REMOTE_STATE_DIR");
    const QString phase = qEnvironmentVariable("E12_PHASE", "sync");
    const int n = qEnvironmentVariable("E12_N", "10").toInt();

    if (localDirPath.isEmpty() || fpDbPath.isEmpty() || baselineDbPath.isEmpty()
            || remoteStateDirPath.isEmpty()) {
        std::printf("FATAL: E12_LOCAL_DIR/E12_FP_DB/E12_BASELINE_DB/E12_REMOTE_STATE_DIR must all be set\n");
        return 2;
    }
    QDir().mkpath(localDirPath);
    QDir().mkpath(remoteStateDirPath);

    Kalburator::Shape::ShapeRegistries shape;
    BackendRegistry pmRegistry;
    Kalburator::PluginManager pm(&pmRegistry, shape);
    Kalburator::registerStockPlugins(pm);

    BackendConfiguration cfg;
    cfg.id   = QStringLiteral("e12-live");
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
        if (c.id.contains(collNeedle)) collId = c.id;
    if (collId.isEmpty()) {
        std::printf("FATAL: no '%s' calendar found on server\n", qUtf8Printable(collNeedle));
        return 2;
    }
    std::printf("INFO  connected; collection id = %s\n", qUtf8Printable(collId));

    auto rawRemote = backendForCollection(provider, collId);
    auto *remote = dynamic_cast<RemoteCalendarBackend *>(rawRemote.get());
    if (!remote) { std::printf("FATAL: createBackends() produced no RemoteCalendarBackend\n"); return 2; }
    rawRemote.release();
    remote->setDbPath(QDir(remoteStateDirPath).filePath(QStringLiteral("ctags.db")));
    remote->setCacheDir(remoteStateDirPath);

    LocalBackend local(localDirPath);
    const QString localCollection = QStringLiteral("mirror");
    {
        CollectionInfo info;
        info.id = localCollection; info.name = localCollection;
        info.type = QStringLiteral("calendar");
        // idempotent: OK if it already exists from a prior invocation
        local.createCollection(info);
    }
    local.setDbPath(fpDbPath);

    const QString mirrorDir = QDir(localDirPath).filePath(localCollection);
    if (phase == QStringLiteral("seed")) {
        QDir().mkpath(mirrorDir);
        for (int i = 0; i < n; ++i) {
            const QString uid = QStringLiteral("e12-live-%1").arg(i);
            const QString path = QDir(mirrorDir).filePath(uid + QStringLiteral(".ics"));
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                std::printf("FATAL: cannot write %s\n", qUtf8Printable(path));
                return 2;
            }
            const QByteArray bytes = makeVEventNoTimestamps(uid, QStringLiteral("E12 Live Item %1").arg(i));
            f.write(bytes);
        }
        std::printf("INFO  seeded %d timestamp-less local events in %s\n", n, qUtf8Printable(mirrorDir));
        return 0;
    }

    BackendRegistry registry;
    registry.registerBackendInstance(remote->backendId(), remote);
    registry.registerBackendInstance(local.backendId(), &local);

    SmokeHost host(&registry);
    SyncEngine engine(&registry, &host, shape);
    engine.setSkipUnchangedMappings(false);

    BaselineStore baselines(baselineDbPath);
    engine.setBaselineStore(&baselines);

    SyncMapping mapping;
    mapping.id             = QStringLiteral("e12-live-mapping");
    mapping.sourceBackend  = local.backendId();
    mapping.sourceCalendar = localCollection;
    mapping.targetBackend  = remote->backendId();
    mapping.targetCalendar = collId;
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::AskUser;
    mapping.enabled        = true;
    engine.setSyncMappings({ mapping });

    const SyncResult r = runOnce(engine);
    std::printf("RESULT success=%d unresolvedConflicts=%d error=%s\n",
                r.success ? 1 : 0, (int)r.unresolvedConflicts.size(),
                qUtf8Printable(r.errorMessage));

    if (phase == QStringLiteral("verify")) {
        int fail = 0;
        if (!r.unresolvedConflicts.isEmpty()) {
            std::printf("FAIL  expected zero phantom conflicts, got %d\n",
                        (int)r.unresolvedConflicts.size());
            ++fail;
        } else {
            std::printf("PASS  zero phantom conflicts\n");
        }
        if (!r.success) {
            std::printf("FAIL  repair cycle did not report success: %s\n", qUtf8Printable(r.errorMessage));
            ++fail;
        } else {
            std::printf("PASS  repair cycle succeeded\n");
        }
        engine.stopWorkerPool();
        std::printf(fail == 0 ? "\nE12 LIVE SMOKE: ALL PASS\n" : "\nE12 LIVE SMOKE: %d FAILURE(S)\n", fail);
        return fail == 0 ? 0 : 1;
    }

    engine.stopWorkerPool();
    return 0;
}
