// Sync-hardening H3 — engine-owned sync-progress tokens (O17, O18, O19).
//
// Pins docs/campaign/2026-07-05-sync-hardening-phases.md §6: the engine's
// skip-eligibility check now compares each mapping+side's fresh revision
// against a per-mapping token owned by BaselineStore (sync_tokens, schema
// v7), written only on a successful mapping run using the pre-fetch
// snapshot prepareSyncFastPath captured before that run's fetch. This
// replaces the old design where the skip check consulted each backend's
// OWN persisted cache-validity token (ChangeDetection::cachedCollectionRevision),
// which a backend could commit internally on a successful FETCH regardless
// of whether the overall sync (including the apply phase) succeeded — the
// O17 masking bug this phase closes.
//
// - applyFailure_doesNotStrandChange: O17. RemoteCalendarBackend (source,
//   FakeCalDavServer) commits its own CTag internally once fetchItems fully
//   completes — independent of whether the apply-to-target phase that
//   follows succeeds. A LocalBackend target pointed at a read-only
//   directory lets the fetch succeed while the apply fails. Pre-H3, cycle 2
//   could see source's internally-committed CTag as "unchanged" and skip,
//   stranding the never-applied item forever. Post-H3, cycle 1's failure
//   means no token is persisted at all, so cycle 2 is never skip-eligible.
// - foreignEditBetweenCycles_defeatsSkip: O18 (accepted weaker fallback
//   pin per the phase doc). Two LocalBackends. A foreign edit landing
//   after a settled mapping's last successful cycle must defeat the next
//   cycle's skip, because the stored token is the pre-fetch snapshot of
//   that LAST cycle, not a post-write re-hash that could have absorbed
//   (and thereby masked) the foreign edit.
// - settledMapping_keepsSkipping: pins that H3 didn't just disable
//   skipping outright.
// - clobberRun_clearsTokens: pins that a clobber (which discards baselines)
//   also discards this mapping's sync-progress tokens, so a clobbered
//   mapping can't wrongly skip against a token from before the wipe.

#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QFutureWatcher>
#include <QMutex>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>

#include <memory>

#include "backendconfiguration.h"
#include "backendrecord.h"
#include "backendregistry.h"
#include "baselinestore.h"
#include "caldavprovider.h"
#include "collectioninfo.h"
#include "fakecaldavserver.h"
#include "iblobbackend.h"
#include "isynchost.h"
#include "localbackend.h"
#include "lossprofile.h"
#include "mockbackend.h"
#include "pluginmanager.h"
#include "remotecalendarbackend.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "syncbackend.h"
#include "syncengine.h"
#include "syncrequest.h"
#include "synctypes.h"

using Kalburator::Sync::BackendConfiguration;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::BackendRegistry;
using Kalburator::Storage::BaselineStore;
using Kalburator::Sync::CalDavProvider;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::ConflictResolution;
using Kalburator::Sync::ExecutionOverride;
using Kalburator::Sync::IBlobBackend;
using Kalburator::Sync::IProvider;
using Kalburator::Sync::ISyncConfigStore;
using Kalburator::Sync::ISyncHost;
using Kalburator::Sync::LocalBackend;
using Kalburator::Sync::MockBackend;
using Kalburator::Sync::RemoteCalendarBackend;
using Kalburator::Sync::SyncBackend;
using Kalburator::Engine::SyncEngine;
using Kalburator::Engine::SyncRequest;
using Kalburator::Sync::SyncMapping;
using Kalburator::Sync::SyncMode;
using Kalburator::Sync::SyncResult;
using Kalburator::Shape::LossProfile;

namespace {

constexpr int kSyncTimeoutMs = 15000;

// ---- qInfo capture for the skip log line (same technique as
// tst_engine_skip_unchanged.cpp) ----
QMutex           g_logMutex;
QStringList      g_logMessages;
QtMessageHandler g_prevHandler = nullptr;

void captureHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    { QMutexLocker lk(&g_logMutex); g_logMessages.append(msg); }
    if (g_prevHandler) g_prevHandler(type, ctx, msg);
}

void clearLog() { QMutexLocker lk(&g_logMutex); g_logMessages.clear(); }

bool sawSkipLog(const QString &mappingId)
{
    QMutexLocker lk(&g_logMutex);
    for (const auto &m : g_logMessages)
        if (m.contains(QLatin1String("skipping unchanged mapping")) && m.contains(mappingId))
            return true;
    return false;
}

class CapturingSyncHost final : public ISyncHost
{
public:
    explicit CapturingSyncHost(BackendRegistry *registry) : m_registry(registry) {}
    SyncBackend *backendById(const QString &id) override
    {
        return m_registry ? static_cast<SyncBackend*>(m_registry->backendInstance(id)) : nullptr;
    }
    QHash<QString, SyncBackend *> backends() override
    {
        QHash<QString, SyncBackend *> out;
        if (!m_registry) return out;
        for (const auto &id : m_registry->registeredInstanceIds())
            out.insert(id, static_cast<SyncBackend*>(m_registry->backendInstance(id)));
        return out;
    }
    ISyncConfigStore *configStore() override { return nullptr; }
    void syncStarted(const QString &, const LossProfile &) override {}
    void recordChanged(const QString &, const QString &, ChangeKind) override {}
private:
    BackendRegistry *m_registry = nullptr;
};

bool waitForFutureBool(QFuture<bool> f, int timeoutMs = 5000)
{
    if (f.isFinished()) return true;
    QFutureWatcher<bool> w;
    QSignalSpy doneSpy(&w, &QFutureWatcher<bool>::finished);
    w.setFuture(f);
    if (f.isFinished()) return true;
    return doneSpy.wait(timeoutMs);
}

QByteArray makeVEventWithSummary(const QString &uid, const QString &summary)
{
    QByteArray v;
    v += "BEGIN:VCALENDAR\r\n";
    v += "VERSION:2.0\r\n";
    v += "PRODID:-//FakeCalDavServer//EN\r\n";
    v += "BEGIN:VEVENT\r\n";
    v += "UID:" + uid.toUtf8() + "\r\n";
    v += "DTSTAMP:20260701T120000Z\r\n";
    v += "DTSTART:20260705T090000Z\r\n";
    v += "DTEND:20260705T100000Z\r\n";
    v += "SUMMARY:" + summary.toUtf8() + "\r\n";
    v += "END:VEVENT\r\n";
    v += "END:VCALENDAR\r\n";
    return v;
}

QByteArray makeIcsBytes(const QString &uid)
{
    return QStringLiteral(
        "BEGIN:VCALENDAR\r\n"
        "VERSION:2.0\r\n"
        "PRODID:-//test//test//EN\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:%1\r\n"
        "SUMMARY:Token soundness event %1\r\n"
        "DTSTART:20250101T120000Z\r\n"
        "DTEND:20250101T130000Z\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n").arg(uid).toUtf8();
}

SyncResult runOnce(SyncEngine &engine, SyncEngine::SyncBehavior behavior = SyncEngine::SyncBehavior::Unmonitored)
{
    clearLog();
    SyncRequest req;
    req.behavior = behavior;
    auto f = engine.runSync(req);
    QDeadlineTimer deadline(kSyncTimeoutMs);
    while (!f.isFinished() && !deadline.hasExpired())
        QTest::qWait(10);
    if (!f.isFinished()) {
        SyncResult timedOut;
        timedOut.success = false;
        timedOut.errorMessage = QStringLiteral("sync did not finish within timeout");
        return timedOut;
    }
    return f.resultAt(0).first();
}

// Task 2.1: CalDavProvider now emits exactly one spec for the whole
// connected account (domainId == "cal"), whose single RemoteCalendarBackend
// hosts every calendar. A lookup by domainId ("cal") is the new equivalent
// of the old per-collection createBackend(collectionId).
std::unique_ptr<IBlobBackend>
backendForCollection(IProvider &provider, const QString &domainId)
{
    auto specs = provider.createBackends();
    for (auto &spec : specs) {
        if (spec.domainId == domainId) return std::move(spec.backend);
    }
    return nullptr;
}

} // namespace

class TstSyncTokenSoundness : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void applyFailure_doesNotStrandChange();
    void foreignEditBetweenCycles_defeatsSkip();
    void settledMapping_keepsSkipping();
    void clobberRun_clearsTokens();
    void writingCycleImmediatelyFollowedByQuietCycle_skips();
    void foreignEditDuringWritingCycle_defeatsIncrementalSkip();
    void testAppliedRevisionsRideOnTheResult();

private:
    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;
};

void TstSyncTokenSoundness::initTestCase()
{
    Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
    Kalburator::registerStockPlugins(pm);
    g_prevHandler = qInstallMessageHandler(captureHandler);
}

void TstSyncTokenSoundness::cleanupTestCase()
{
    qInstallMessageHandler(g_prevHandler);
    g_prevHandler = nullptr;
}

// ──────────────────────────────────────────────────────────────────────────
// O17 pin — a failed apply must not let a later cycle skip and strand the
// change. Remote source (FakeCalDavServer -> RemoteCalendarBackend), whose
// fetchItems commits its own CTag store on a fully-completed fetch,
// independent of whether the apply-to-target phase succeeds.
// ──────────────────────────────────────────────────────────────────────────
void TstSyncTokenSoundness::applyFailure_doesNotStrandChange()
{
    QTemporaryDir remoteStateDir;
    QTemporaryDir baselineDir;
    QVERIFY(remoteStateDir.isValid());
    QVERIFY(baselineDir.isValid());

    FakeCalDavServer server;
    const QString href = QStringLiteral("/calendars/testuser/personal/");
    server.setCalendarComponents(href, { QStringLiteral("VEVENT") });
    server.setSeedEvents(href, { makeVEventWithSummary(QStringLiteral("evt-o17"), QStringLiteral("O17 event")) });
    QVERIFY(server.startListening());

    BackendConfiguration cfg;
    cfg.id          = QStringLiteral("o17-account");
    cfg.type        = QStringLiteral("caldav");
    cfg.displayName = QStringLiteral("Fake CalDAV (H3 O17 fixture)");
    cfg.connectionParams.insert(QStringLiteral("url"),      server.baseUrl().toString());
    cfg.connectionParams.insert(QStringLiteral("username"), QStringLiteral("testuser"));
    cfg.connectionParams.insert(QStringLiteral("password"), QStringLiteral("testpass"));

    CalDavProvider provider;
    provider.load(cfg);
    QVERIFY(waitForFutureBool(provider.connect(), 5000));
    QVERIFY(provider.isConnected());
    const auto cols = provider.collections();
    QVERIFY(!cols.isEmpty());
    const QString collId = cols.first().id;

    auto rawRemote = backendForCollection(provider, QStringLiteral("cal"));
    QVERIFY(rawRemote);
    auto *remote = dynamic_cast<RemoteCalendarBackend *>(rawRemote.get());
    QVERIFY(remote);
    remote->setDbPath(remoteStateDir.filePath(QStringLiteral("ctags.db")));
    remote->setCacheDir(remoteStateDir.path());

    // MockBackend as target: its createRecord() honours failure injection
    // (setFailurePoint) while reporting discoveredWritable()==true (the
    // base-class default) — unlike a permission-denied directory, which
    // LocalBackend detects up front and gracefully SKIPS writes for
    // (still reporting success). We need a genuine apply-phase FAILURE
    // (mirrorErrors > 0 -> result.success == false), not a pre-flight skip.
    const QString targetCollectionId = QStringLiteral("mirror");
    MockBackend target(QStringLiteral("mock-o17-target"));
    {
        CollectionInfo info;
        info.id   = targetCollectionId;
        info.name = targetCollectionId;
        info.type = QStringLiteral("calendar");
        QVERIFY(!target.createCollection(info).isEmpty());
    }

    BackendRegistry registry;
    registry.registerBackendInstance(remote->backendId(), remote);
    registry.registerBackendInstance(target.backendId(), &target);

    CapturingSyncHost host(&registry);
    SyncEngine engine(&registry, &host, m_shape);
    engine.setSkipUnchangedMappings(true);

    BaselineStore baselines(baselineDir.filePath(QStringLiteral("baselines.db")));
    engine.setBaselineStore(&baselines);

    const QString mappingId = QStringLiteral("o17-mapping");
    SyncMapping mapping;
    mapping.id             = mappingId;
    mapping.sourceBackend  = remote->backendId();
    mapping.sourceCalendar = collId;
    mapping.targetBackend  = target.backendId();
    mapping.targetCalendar = targetCollectionId;
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::LastWriteWins;
    mapping.enabled        = true;
    engine.setSyncMappings({ mapping });

    // Cycle 1: the source fetch completes fully (RemoteCalendarBackend
    // commits its own CTag internally), but the apply phase fails outright.
    target.setFailurePoint(MockBackend::FailurePoint::OnStoreItems);
    const SyncResult r1 = runOnce(engine);
    QVERIFY2(!r1.success, "cycle 1 must fail: target createRecord is failure-injected");
    QVERIFY(target.createRecordCalls().size() >= 1);

    // A failed run must not persist ANY sync-progress token — that is the
    // O17 fix (pre-H3, the source's own internally-committed CTag could
    // make cycle 2 look "unchanged" regardless of this failure).
    QVERIFY2(baselines.syncToken(mappingId, QStringLiteral("source")).isEmpty(),
             "a failed apply must not persist the source token");
    QVERIFY2(baselines.syncToken(mappingId, QStringLiteral("target")).isEmpty(),
             "a failed apply must not persist the target token");

    // Clear the injected failure; nothing on the source changed in the
    // meantime.
    target.clearFailurePoint();

    const SyncResult r2 = runOnce(engine);
    QVERIFY2(r2.success, qUtf8Printable(QStringLiteral("cycle 2 failed: ") + r2.errorMessage));
    QVERIFY2(!sawSkipLog(mappingId),
             "a mapping with no persisted token must never be judged skip-eligible");
    QVERIFY2(target.loadRecords(targetCollectionId).size() == 1,
             "the previously-stranded item must land once the target accepts writes again");
}

// ──────────────────────────────────────────────────────────────────────────
// O18 pin (accepted weaker fallback per the phase doc) — a foreign edit
// landing after a settled mapping's last successful cycle must defeat the
// next cycle's skip.
// ──────────────────────────────────────────────────────────────────────────
void TstSyncTokenSoundness::foreignEditBetweenCycles_defeatsSkip()
{
    QTemporaryDir sourceDir, targetDir, fpDir, baselineDir;
    QVERIFY(sourceDir.isValid() && targetDir.isValid() && fpDir.isValid() && baselineDir.isValid());

    LocalBackend source(sourceDir.path());
    LocalBackend target(targetDir.path());
    source.setDbPath(fpDir.filePath(QStringLiteral("source-fp.db")));
    target.setDbPath(fpDir.filePath(QStringLiteral("target-fp.db")));

    const QString sourceCollection = QStringLiteral("src");
    const QString targetCollection = QStringLiteral("tgt");
    {
        CollectionInfo info;
        info.id = sourceCollection; info.name = sourceCollection; info.type = QStringLiteral("calendar");
        QVERIFY(!source.createCollection(info).isEmpty());
    }
    {
        CollectionInfo info;
        info.id = targetCollection; info.name = targetCollection; info.type = QStringLiteral("calendar");
        QVERIFY(!target.createCollection(info).isEmpty());
    }

    auto *blobSource = static_cast<IBlobBackend *>(&source);
    BackendRecord rec;
    rec.id = QStringLiteral("evt-1");
    rec.data = makeIcsBytes(QStringLiteral("evt-1"));
    QVERIFY(!blobSource->createRecord(sourceCollection, rec).isEmpty());

    BackendRegistry registry;
    registry.registerBackendInstance(source.backendId(), &source);
    registry.registerBackendInstance(target.backendId(), &target);

    CapturingSyncHost host(&registry);
    SyncEngine engine(&registry, &host, m_shape);
    engine.setSkipUnchangedMappings(true);

    BaselineStore baselines(baselineDir.filePath(QStringLiteral("baselines.db")));
    engine.setBaselineStore(&baselines);

    const QString mappingId = QStringLiteral("o18-mapping");
    SyncMapping mapping;
    mapping.id             = mappingId;
    mapping.sourceBackend  = source.backendId();
    mapping.sourceCalendar = sourceCollection;
    mapping.targetBackend  = target.backendId();
    mapping.targetCalendar = targetCollection;
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::LastWriteWins;
    mapping.enabled        = true;
    engine.setSyncMappings({ mapping });

    // Sync 1: populate. Sync 2: real work but idle content (H3's accepted
    // one-cycle lag — see tst_sync_convergence.cpp's fastPathSkipsGenuinely-
    // UnchangedMapping comment for the mechanism). Sync 3 settles/skips.
    const SyncResult r1 = runOnce(engine);
    QVERIFY2(r1.success, qUtf8Printable(r1.errorMessage));
    const SyncResult r2 = runOnce(engine);
    QVERIFY2(r2.success, qUtf8Printable(r2.errorMessage));
    const SyncResult r3 = runOnce(engine);
    QVERIFY2(r3.success, qUtf8Printable(r3.errorMessage));
    QVERIFY2(sawSkipLog(mappingId), "mapping must be settled/skipping before the foreign edit");

    // Foreign edit: a new record lands directly on the source backend,
    // bypassing the engine entirely (as an external process editing the
    // collection would).
    BackendRecord foreign;
    foreign.id = QStringLiteral("evt-foreign");
    foreign.data = makeIcsBytes(QStringLiteral("evt-foreign"));
    QVERIFY(!blobSource->createRecord(sourceCollection, foreign).isEmpty());

    const SyncResult r4 = runOnce(engine);
    QVERIFY2(r4.success, qUtf8Printable(r4.errorMessage));
    QVERIFY2(!sawSkipLog(mappingId),
             "a foreign edit since the last stored token must defeat the skip");
    QVERIFY(QFileInfo::exists(QDir(QDir(targetDir.path()).filePath(targetCollection))
                                   .filePath(QStringLiteral("evt-foreign.ics"))));
}

// ──────────────────────────────────────────────────────────────────────────
// Skip still works — pins that H3 didn't just disable skipping outright.
// ──────────────────────────────────────────────────────────────────────────
void TstSyncTokenSoundness::settledMapping_keepsSkipping()
{
    QTemporaryDir sourceDir, targetDir, fpDir, baselineDir;
    QVERIFY(sourceDir.isValid() && targetDir.isValid() && fpDir.isValid() && baselineDir.isValid());

    LocalBackend source(sourceDir.path());
    LocalBackend target(targetDir.path());
    source.setDbPath(fpDir.filePath(QStringLiteral("source-fp.db")));
    target.setDbPath(fpDir.filePath(QStringLiteral("target-fp.db")));

    const QString sourceCollection = QStringLiteral("src");
    const QString targetCollection = QStringLiteral("tgt");
    {
        CollectionInfo info;
        info.id = sourceCollection; info.name = sourceCollection; info.type = QStringLiteral("calendar");
        QVERIFY(!source.createCollection(info).isEmpty());
    }
    {
        CollectionInfo info;
        info.id = targetCollection; info.name = targetCollection; info.type = QStringLiteral("calendar");
        QVERIFY(!target.createCollection(info).isEmpty());
    }

    auto *blobSource = static_cast<IBlobBackend *>(&source);
    BackendRecord rec;
    rec.id = QStringLiteral("evt-1");
    rec.data = makeIcsBytes(QStringLiteral("evt-1"));
    QVERIFY(!blobSource->createRecord(sourceCollection, rec).isEmpty());

    BackendRegistry registry;
    registry.registerBackendInstance(source.backendId(), &source);
    registry.registerBackendInstance(target.backendId(), &target);

    CapturingSyncHost host(&registry);
    SyncEngine engine(&registry, &host, m_shape);
    engine.setSkipUnchangedMappings(true);

    BaselineStore baselines(baselineDir.filePath(QStringLiteral("baselines.db")));
    engine.setBaselineStore(&baselines);

    const QString mappingId = QStringLiteral("skip-still-works-mapping");
    SyncMapping mapping;
    mapping.id             = mappingId;
    mapping.sourceBackend  = source.backendId();
    mapping.sourceCalendar = sourceCollection;
    mapping.targetBackend  = target.backendId();
    mapping.targetCalendar = targetCollection;
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::LastWriteWins;
    mapping.enabled        = true;
    engine.setSyncMappings({ mapping });

    QVERIFY(runOnce(engine).success);
    QVERIFY(runOnce(engine).success);
    QVERIFY(runOnce(engine).success);
    QVERIFY2(sawSkipLog(mappingId), "third cycle over idle content must skip");

    // Two MORE quiet cycles after the first skip: both must also skip.
    QVERIFY(runOnce(engine).success);
    QVERIFY2(sawSkipLog(mappingId), "a settled mapping keeps skipping (cycle 4)");
    QVERIFY(runOnce(engine).success);
    QVERIFY2(sawSkipLog(mappingId), "a settled mapping keeps skipping (cycle 5)");
}

// ──────────────────────────────────────────────────────────────────────────
// Clobber clears tokens — a clobber run must drop this mapping's stored
// sync-progress tokens so a subsequent cycle can't wrongly skip against a
// token from before the wipe.
// ──────────────────────────────────────────────────────────────────────────
void TstSyncTokenSoundness::clobberRun_clearsTokens()
{
    QTemporaryDir sourceDir, targetDir, fpDir, baselineDir;
    QVERIFY(sourceDir.isValid() && targetDir.isValid() && fpDir.isValid() && baselineDir.isValid());

    LocalBackend source(sourceDir.path());
    LocalBackend target(targetDir.path());
    source.setDbPath(fpDir.filePath(QStringLiteral("source-fp.db")));
    target.setDbPath(fpDir.filePath(QStringLiteral("target-fp.db")));

    const QString sourceCollection = QStringLiteral("src");
    const QString targetCollection = QStringLiteral("tgt");
    {
        CollectionInfo info;
        info.id = sourceCollection; info.name = sourceCollection; info.type = QStringLiteral("calendar");
        QVERIFY(!source.createCollection(info).isEmpty());
    }
    {
        CollectionInfo info;
        info.id = targetCollection; info.name = targetCollection; info.type = QStringLiteral("calendar");
        QVERIFY(!target.createCollection(info).isEmpty());
    }

    auto *blobSource = static_cast<IBlobBackend *>(&source);
    BackendRecord rec;
    rec.id = QStringLiteral("evt-1");
    rec.data = makeIcsBytes(QStringLiteral("evt-1"));
    QVERIFY(!blobSource->createRecord(sourceCollection, rec).isEmpty());

    BackendRegistry registry;
    registry.registerBackendInstance(source.backendId(), &source);
    registry.registerBackendInstance(target.backendId(), &target);

    CapturingSyncHost host(&registry);
    SyncEngine engine(&registry, &host, m_shape);
    engine.setSkipUnchangedMappings(true);

    BaselineStore baselines(baselineDir.filePath(QStringLiteral("baselines.db")));
    engine.setBaselineStore(&baselines);

    const QString mappingId = QStringLiteral("clobber-clears-mapping");
    SyncMapping mapping;
    mapping.id             = mappingId;
    mapping.sourceBackend  = source.backendId();
    mapping.sourceCalendar = sourceCollection;
    mapping.targetBackend  = target.backendId();
    mapping.targetCalendar = targetCollection;
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::LastWriteWins;
    mapping.enabled        = true;
    engine.setSyncMappings({ mapping });

    QVERIFY(runOnce(engine).success);
    QVERIFY(runOnce(engine).success);
    QVERIFY(runOnce(engine).success);
    QVERIFY2(sawSkipLog(mappingId), "mapping must be settled/skipping before the clobber");
    QVERIFY(!baselines.syncToken(mappingId, QStringLiteral("source")).isEmpty());
    QVERIFY(!baselines.syncToken(mappingId, QStringLiteral("target")).isEmpty());

    // Clobber run: all-enabled dispatch (empty mappingIds) with
    // executionOverride.clobber = true.
    {
        clearLog();
        SyncRequest req;
        req.behavior = SyncEngine::SyncBehavior::Unmonitored;
        ExecutionOverride ov;
        ov.clobber = true;
        req.executionOverride = ov;
        auto f = engine.runSync(req);
        QDeadlineTimer deadline(kSyncTimeoutMs);
        while (!f.isFinished() && !deadline.hasExpired())
            QTest::qWait(10);
        QVERIFY(f.isFinished());
        QVERIFY2(f.resultAt(0).first().success,
                 qUtf8Printable(f.resultAt(0).first().errorMessage));
    }

    QVERIFY2(baselines.syncToken(mappingId, QStringLiteral("source")).isEmpty(),
             "a clobber run must clear the source token");
    QVERIFY2(baselines.syncToken(mappingId, QStringLiteral("target")).isEmpty(),
             "a clobber run must clear the target token");

    // A subsequent quiet cycle must NOT skip — the cleared tokens force a
    // re-diff even though nothing has actually changed.
    QVERIFY(runOnce(engine).success);
    QVERIFY2(!sawSkipLog(mappingId),
             "the cycle right after a clobber must not skip (tokens were cleared)");
}

// ──────────────────────────────────────────────────────────────────────────
// E9.2 (sync-excellence campaign, O34) — LocalBackend's incremental
// expected-fingerprint removes the accepted H3 one-cycle re-diff lag: the
// cycle IMMEDIATELY after a writing cycle should already be skip-eligible,
// not just the cycle after that (foreignEditBetweenCycles_defeatsSkip's own
// comment documents the pre-E9.2 lag: "Sync 2: real work but idle content").
// Pre-E9.2 this is RED (cycle 2 does not skip; only cycle 3 does).
// ──────────────────────────────────────────────────────────────────────────
void TstSyncTokenSoundness::writingCycleImmediatelyFollowedByQuietCycle_skips()
{
    QTemporaryDir sourceDir, targetDir, fpDir, baselineDir;
    QVERIFY(sourceDir.isValid() && targetDir.isValid() && fpDir.isValid() && baselineDir.isValid());

    LocalBackend source(sourceDir.path());
    LocalBackend target(targetDir.path());
    source.setDbPath(fpDir.filePath(QStringLiteral("source-fp.db")));
    target.setDbPath(fpDir.filePath(QStringLiteral("target-fp.db")));

    const QString sourceCollection = QStringLiteral("src");
    const QString targetCollection = QStringLiteral("tgt");
    {
        CollectionInfo info;
        info.id = sourceCollection; info.name = sourceCollection; info.type = QStringLiteral("calendar");
        QVERIFY(!source.createCollection(info).isEmpty());
    }
    {
        CollectionInfo info;
        info.id = targetCollection; info.name = targetCollection; info.type = QStringLiteral("calendar");
        QVERIFY(!target.createCollection(info).isEmpty());
    }

    auto *blobSource = static_cast<IBlobBackend *>(&source);
    BackendRecord rec;
    rec.id = QStringLiteral("evt-1");
    rec.data = makeIcsBytes(QStringLiteral("evt-1"));
    QVERIFY(!blobSource->createRecord(sourceCollection, rec).isEmpty());

    BackendRegistry registry;
    registry.registerBackendInstance(source.backendId(), &source);
    registry.registerBackendInstance(target.backendId(), &target);

    CapturingSyncHost host(&registry);
    SyncEngine engine(&registry, &host, m_shape);
    engine.setSkipUnchangedMappings(true);

    BaselineStore baselines(baselineDir.filePath(QStringLiteral("baselines.db")));
    engine.setBaselineStore(&baselines);

    const QString mappingId = QStringLiteral("e9-immediate-skip-mapping");
    SyncMapping mapping;
    mapping.id             = mappingId;
    mapping.sourceBackend  = source.backendId();
    mapping.sourceCalendar = sourceCollection;
    mapping.targetBackend  = target.backendId();
    mapping.targetCalendar = targetCollection;
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::LastWriteWins;
    mapping.enabled        = true;
    engine.setSyncMappings({ mapping });

    // Cycle 1: writing cycle (creates evt-1 on target).
    const SyncResult r1 = runOnce(engine);
    QVERIFY2(r1.success, qUtf8Printable(r1.errorMessage));

    // Cycle 2: immediately following quiet cycle — nothing changed on
    // either side since cycle 1's write. With E9.2's incremental
    // expected-fingerprint, this must already be skip-eligible.
    const SyncResult r2 = runOnce(engine);
    QVERIFY2(r2.success, qUtf8Printable(r2.errorMessage));
    QVERIFY2(sawSkipLog(mappingId),
             "E9.2: the cycle immediately after a writing cycle must skip "
             "(incremental expected-fingerprint removes the one-cycle lag)");
}

// ──────────────────────────────────────────────────────────────────────────
// E9.2 safety pin — a foreign edit landing on the target's own on-disk
// state right after (i.e. present by the time of) the writing cycle's
// post-write incremental fingerprint computation must still defeat the
// very next cycle's skip. The incremental fingerprint only patches the
// files LocalBackend itself wrote — it must never accidentally absorb a
// change to a file it did NOT write, or a foreign edit landing in that
// narrow window would be silently masked forever (the same class of bug
// H3/O18 already closed for the pre-fetch-snapshot mechanism).
// ──────────────────────────────────────────────────────────────────────────
void TstSyncTokenSoundness::foreignEditDuringWritingCycle_defeatsIncrementalSkip()
{
    QTemporaryDir sourceDir, targetDir, fpDir, baselineDir;
    QVERIFY(sourceDir.isValid() && targetDir.isValid() && fpDir.isValid() && baselineDir.isValid());

    LocalBackend source(sourceDir.path());
    LocalBackend target(targetDir.path());
    source.setDbPath(fpDir.filePath(QStringLiteral("source-fp.db")));
    target.setDbPath(fpDir.filePath(QStringLiteral("target-fp.db")));

    const QString sourceCollection = QStringLiteral("src");
    const QString targetCollection = QStringLiteral("tgt");
    {
        CollectionInfo info;
        info.id = sourceCollection; info.name = sourceCollection; info.type = QStringLiteral("calendar");
        QVERIFY(!source.createCollection(info).isEmpty());
    }
    {
        CollectionInfo info;
        info.id = targetCollection; info.name = targetCollection; info.type = QStringLiteral("calendar");
        QVERIFY(!target.createCollection(info).isEmpty());
    }

    auto *blobSource = static_cast<IBlobBackend *>(&source);
    BackendRecord rec;
    rec.id = QStringLiteral("evt-1");
    rec.data = makeIcsBytes(QStringLiteral("evt-1"));
    QVERIFY(!blobSource->createRecord(sourceCollection, rec).isEmpty());

    BackendRegistry registry;
    registry.registerBackendInstance(source.backendId(), &source);
    registry.registerBackendInstance(target.backendId(), &target);

    CapturingSyncHost host(&registry);
    SyncEngine engine(&registry, &host, m_shape);
    engine.setSkipUnchangedMappings(true);

    BaselineStore baselines(baselineDir.filePath(QStringLiteral("baselines.db")));
    engine.setBaselineStore(&baselines);

    const QString mappingId = QStringLiteral("e9-foreign-during-write-mapping");
    SyncMapping mapping;
    mapping.id             = mappingId;
    mapping.sourceBackend  = source.backendId();
    mapping.sourceCalendar = sourceCollection;
    mapping.targetBackend  = target.backendId();
    mapping.targetCalendar = targetCollection;
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::LastWriteWins;
    mapping.enabled        = true;
    engine.setSyncMappings({ mapping });

    // Cycle 1: writing cycle (creates evt-1 on target). LocalBackend's
    // incremental fingerprint, computed at the end of applyRecords(),
    // patches ONLY evt-1.ics into its fetch-time snapshot.
    const SyncResult r1 = runOnce(engine);
    QVERIFY2(r1.success, qUtf8Printable(r1.errorMessage));

    // Foreign edit: a file lands directly in the TARGET's directory,
    // bypassing the engine (and this backend's own write set) entirely —
    // exactly the file the incremental patch must NOT have accounted for.
    auto *blobTarget = static_cast<IBlobBackend *>(&target);
    BackendRecord foreign;
    foreign.id = QStringLiteral("evt-foreign");
    foreign.data = makeIcsBytes(QStringLiteral("evt-foreign"));
    QVERIFY(!blobTarget->createRecord(targetCollection, foreign).isEmpty());

    // Cycle 2: must NOT skip — the incremental fingerprint LocalBackend
    // reported after cycle 1 cannot possibly reflect a file it never wrote,
    // so the next cycle's real (full-rescan) revision necessarily differs
    // from the stored token, forcing a re-diff that picks up the foreign file.
    const SyncResult r2 = runOnce(engine);
    QVERIFY2(r2.success, qUtf8Printable(r2.errorMessage));
    QVERIFY2(!sawSkipLog(mappingId),
             "a foreign edit outside the incremental write set must defeat the skip");
    QVERIFY(QFileInfo::exists(QDir(QDir(sourceDir.path()).filePath(sourceCollection))
                                   .filePath(QStringLiteral("evt-foreign.ics"))));
}

// ──────────────────────────────────────────────────────────────────────────
// Parallel-sync Task 1 — the settled WriteOperation's post-apply revision
// must ride on the mapping's own SyncResult, not on the engine-side
// per-worker state that carried it before this task (now retired). This
// is what makes the value safe to read once N mappings are in flight
// under a worker pool: it is attached to the result, not to a single
// worker.
// ──────────────────────────────────────────────────────────────────────────
void TstSyncTokenSoundness::testAppliedRevisionsRideOnTheResult()
{
    QTemporaryDir sourceDir, targetDir, fpDir, baselineDir;
    QVERIFY(sourceDir.isValid() && targetDir.isValid() && fpDir.isValid() && baselineDir.isValid());

    LocalBackend source(sourceDir.path());
    LocalBackend target(targetDir.path());
    source.setDbPath(fpDir.filePath(QStringLiteral("source-fp.db")));
    target.setDbPath(fpDir.filePath(QStringLiteral("target-fp.db")));

    const QString sourceCollection = QStringLiteral("src");
    const QString targetCollection = QStringLiteral("tgt");
    {
        CollectionInfo info;
        info.id = sourceCollection; info.name = sourceCollection; info.type = QStringLiteral("calendar");
        QVERIFY(!source.createCollection(info).isEmpty());
    }
    {
        CollectionInfo info;
        info.id = targetCollection; info.name = targetCollection; info.type = QStringLiteral("calendar");
        QVERIFY(!target.createCollection(info).isEmpty());
    }

    // A mapping that actually writes must report the target's post-apply
    // revision on its own SyncResult, not via engine-side worker state.
    auto *blobSource = static_cast<IBlobBackend *>(&source);
    BackendRecord rec;
    rec.id = QStringLiteral("evt-1");
    rec.data = makeIcsBytes(QStringLiteral("evt-1"));
    QVERIFY(!blobSource->createRecord(sourceCollection, rec).isEmpty());

    BackendRegistry registry;
    registry.registerBackendInstance(source.backendId(), &source);
    registry.registerBackendInstance(target.backendId(), &target);

    CapturingSyncHost host(&registry);
    SyncEngine engine(&registry, &host, m_shape);
    engine.setSkipUnchangedMappings(true);

    BaselineStore baselines(baselineDir.filePath(QStringLiteral("baselines.db")));
    engine.setBaselineStore(&baselines);

    const QString mappingId = QStringLiteral("applied-revision-on-result-mapping");
    SyncMapping mapping;
    mapping.id             = mappingId;
    mapping.sourceBackend  = source.backendId();
    mapping.sourceCalendar = sourceCollection;
    mapping.targetBackend  = target.backendId();
    mapping.targetCalendar = targetCollection;
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::LastWriteWins;
    mapping.enabled        = true;
    engine.setSyncMappings({ mapping });

    SyncRequest req;
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    auto future = engine.runSync(req);
    QDeadlineTimer deadline(kSyncTimeoutMs);
    while (!future.isFinished() && !deadline.hasExpired())
        QTest::qWait(10);
    QVERIFY(future.isFinished());

    QCOMPARE(future.resultCount(), 1);
    const QList<SyncResult> results = future.resultAt(0);
    QCOMPARE(results.size(), 1);
    QVERIFY(results.first().success);
    QVERIFY2(!results.first().appliedTargetRevision.isEmpty(),
             "a mapping that wrote to target must carry its post-apply "
             "target revision on the SyncResult");
}

QTEST_MAIN(TstSyncTokenSoundness)
#include "tst_sync_token_soundness.moc"
