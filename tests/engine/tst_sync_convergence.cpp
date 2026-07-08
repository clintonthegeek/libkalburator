// Phase B4 (N2 fix) — the sync-convergence campaign's core regression gate.
//
// Real end-to-end wiring: FakeCalDavServer -> CalDavProvider ->
// RemoteCalendarBackend (source) <-> SyncEngine::runSync <-> LocalBackend
// (target, writing real .ics files to a tmp dir), through a real
// SyncMapping/SyncRequest — no mocks on the diff/merge/baseline path.
//
// The bug this pins (finding N2, docs/campaign/2026-07-03-sync-convergence-
// roadmap.md §Phase B4): change detection used to gate on a SINGLE stored
// content hash per baseline record, compared against each side's *native*
// serialized bytes. LocalBackend (KCalendarCore-serialized) and
// RemoteCalendarBackend (server-serialized) never produce byte-identical
// output for the same logical record — so after the very first sync wrote
// source's content into target's own native format, every subsequent sync
// read target's real bytes as "modified" (hash mismatch against the single
// shared baseline) forever. A real user's mirror collection soft-froze the
// GUI every 120s because sync could never converge.
//
// secondSyncIsNoOp is the named acceptance test the roadmap calls for: seed
// the (real, verbatim-storing — no server-side byte normalization, so this
// test cannot flake on that known-acceptable residual) fake server with a
// VEVENT and a VTODO, run one sync (mirror populates LocalBackend), run a
// second sync with nothing changed anywhere, and prove NOTHING happened:
// the SyncEngine reports no unresolved conflicts, the fake server sees zero
// additional PUT/DELETE requests, and every .ics file LocalBackend wrote is
// byte-for-byte and mtime-for-mtime untouched.
//
// (SyncResult::sourceStats/targetStats are NOT used for this assertion —
// grep confirms nothing in src/engine/ ever populates those SyncStats
// fields for the unified dispatch path; they are a pre-existing dead field
// on this path, not something this phase touches. Convergence must be
// witnessed externally: fake-server request counters + on-disk file state.)

#include <QtTest/QtTest>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QFutureWatcher>
#include <QSignalSpy>
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
using Kalburator::Sync::IBlobBackend;
using Kalburator::Sync::ISyncConfigStore;
using Kalburator::Sync::ISyncHost;
using Kalburator::Sync::LocalBackend;
using Kalburator::Sync::RemoteCalendarBackend;
using Kalburator::Sync::SyncBackend;
using Kalburator::Engine::SyncEngine;
using Kalburator::Engine::SyncRequest;
using Kalburator::Sync::SyncMapping;
using Kalburator::Sync::SyncMode;
using Kalburator::Sync::SyncResult;
using Kalburator::Shape::LossProfile;

namespace {

constexpr int kSyncTimeoutMs = 10000;

// ──────────────────────────────────────────────────────────────────────────
// CapturingSyncHost — same minimal ISyncHost used by
// tst_carddav_engine_integration.cpp for real-backend engine wiring.
// ──────────────────────────────────────────────────────────────────────────
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

// Spin the event loop until a QFuture<bool> finishes (Qt6's blocking
// waitForFinished() does not pump the QNAM async I/O event loop).
bool waitForFutureBool(QFuture<bool> f, int timeoutMs = 5000)
{
    if (f.isFinished()) return true;
    QFutureWatcher<bool> w;
    QSignalSpy doneSpy(&w, &QFutureWatcher<bool>::finished);
    w.setFuture(f);
    if (f.isFinished()) return true;
    return doneSpy.wait(timeoutMs);
}

QByteArray makeVEvent(const QString &uid)
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
    v += "SUMMARY:Convergence Event\r\n";
    v += "END:VEVENT\r\n";
    v += "END:VCALENDAR\r\n";
    return v;
}

QByteArray makeVTodo(const QString &uid)
{
    QByteArray v;
    v += "BEGIN:VCALENDAR\r\n";
    v += "VERSION:2.0\r\n";
    v += "PRODID:-//FakeCalDavServer//EN\r\n";
    v += "BEGIN:VTODO\r\n";
    v += "UID:" + uid.toUtf8() + "\r\n";
    v += "DTSTAMP:20260701T120000Z\r\n";
    v += "SUMMARY:Convergence Todo\r\n";
    v += "STATUS:NEEDS-ACTION\r\n";
    v += "END:VTODO\r\n";
    v += "END:VCALENDAR\r\n";
    return v;
}

// Same shape as makeVEvent but with a distinguishable SUMMARY, so the
// Phase B5 acceptance-matrix tests (which need two independent events —
// one to edit/delete, one to leave alone as a no-op control) don't collide
// with makeVEvent's fixed "evt-1" content.
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

} // namespace

// ──────────────────────────────────────────────────────────────────────────
// Phase B5 — acceptance-matrix + fast-path fixture.
//
// A reusable rig (real FakeCalDavServer + real CalDavProvider ->
// RemoteCalendarBackend as source, real LocalBackend writing to a tmp dir
// as target, a real SyncEngine + SyncMapping) shared by every Phase B5 test
// below. Isolated per-test: fresh tmp dirs for the mirror, the CTag store,
// the local fingerprint store, and the baseline store; a per-test CalDAV
// collection href/CTag so no state leaks between tests via the FakeCalDavServer
// (a fresh instance per fixture) or any on-disk cache.
//
// Both backends are given a real setDbPath()/setCacheDir() (unlike
// secondSyncIsNoOp above, which relies on RemoteCalendarBackend's default
// content-cache location) specifically so:
//   (a) RemoteCalendarBackend's CTag persists across runOnce() calls in a way
//       this test controls end-to-end (no reliance on ~/.cache surviving
///      between unrelated test runs), and
//   (b) LocalBackend's fingerprint queries stay stable across runOnce() calls,
//       which is a precondition for SyncEngine::prepareSyncFastPath's
//       skip-eligibility check (H3: fresh revision vs the engine-owned
//       per-mapping token in BaselineStore) to ever report "unchanged"
//       instead of "no token on file yet".
struct ConvergenceFixture
{
    // Declaration order fixed to match the destruction order the original
    // secondSyncIsNoOp test relied on (reverse-declaration order): backends
    // and the server must outlive the engine/registry that reference them.
    std::unique_ptr<FakeCalDavServer> server;
    std::unique_ptr<CalDavProvider> provider;
    std::unique_ptr<IBlobBackend> rawRemote;
    RemoteCalendarBackend *remote = nullptr;
    std::unique_ptr<QTemporaryDir> localDir;
    std::unique_ptr<QTemporaryDir> remoteStateDir; // CTag store + content cache
    std::unique_ptr<QTemporaryDir> fingerprintDir; // LocalBackend fingerprint store
    std::unique_ptr<LocalBackend> local;
    std::unique_ptr<BackendRegistry> registry;
    std::unique_ptr<CapturingSyncHost> host;
    std::unique_ptr<SyncEngine> engine;
    std::unique_ptr<QTemporaryDir> baselineDir;
    std::unique_ptr<BaselineStore> baselines;

    QString href;
    QString collId;
    QString mirrorDir;
    QString localCollectionId;
    SyncMapping mapping;

    SyncResult runOnce()
    {
        SyncRequest req;
        // Deliberately empty (== "all enabled"), NOT { mapping.id }: a
        // single-mapping request (mappingIds.size()==1) short-circuits
        // SyncEngine::runSync straight to processSingleMapping(), which
        // explicitly does NOT call prepareSyncFastPath() (see its comment
        // in syncengine.cpp) — the fast-path skip machinery only runs on
        // the multi-mapping driveQueue() path (isAllEnabled() or subset).
        // The B5 fast-path tests need that path even with only one mapping
        // registered; the acceptance-matrix tests get it for free and it
        // doesn't change their semantics (still exactly one mapping).
        req.behavior = SyncEngine::SyncBehavior::Unmonitored;
        auto future = engine->runSync(req);
        QDeadlineTimer deadline(kSyncTimeoutMs);
        while (!future.isFinished() && !deadline.hasExpired())
            QTest::qWait(10);
        if (!future.isFinished()) {
            SyncResult timedOut;
            timedOut.success = false;
            timedOut.errorMessage = QStringLiteral("sync did not finish within timeout");
            return timedOut;
        }
        return future.resultAt(0).first();
    }

    QString pathFor(const QString &uid) const
    {
        return QDir(mirrorDir).filePath(uid + QStringLiteral(".ics"));
    }
};

// Builds the fixture, seeds the fake server with @p seedEvents under a
// fresh collection href, and — if @p initialCtag is non-empty — configures
// the collection's CS:getctag to that fixed value (a precondition for the
// fast-path CTag short-circuit to ever engage; without a configured CTag,
// RemoteCalendarBackend always falls back to the full list+multiget path,
// per FakeCalDavServer's documented default).
std::unique_ptr<ConvergenceFixture> makeConvergenceFixture(
    Kalburator::Shape::ShapeRegistries &shape,
    const QList<QByteArray> &seedEvents,
    const QString &initialCtag,
    bool skipUnchanged)
{
    auto fx = std::make_unique<ConvergenceFixture>();

    fx->server = std::make_unique<FakeCalDavServer>();
    fx->href = QStringLiteral("/calendars/testuser/personal/");
    fx->server->setCalendarComponents(fx->href, { QStringLiteral("VEVENT") });
    fx->server->setSeedEvents(fx->href, seedEvents);
    if (!initialCtag.isEmpty())
        fx->server->setCollectionCtag(fx->href, initialCtag);

    if (!fx->server->startListening())
        return nullptr;

    BackendConfiguration cfg;
    cfg.id          = QStringLiteral("convergence-account");
    cfg.type        = QStringLiteral("caldav");
    cfg.displayName = QStringLiteral("Fake CalDAV (B5 fixture)");
    cfg.connectionParams.insert(QStringLiteral("url"),      fx->server->baseUrl().toString());
    cfg.connectionParams.insert(QStringLiteral("username"), QStringLiteral("testuser"));
    cfg.connectionParams.insert(QStringLiteral("password"), QStringLiteral("testpass"));

    fx->provider = std::make_unique<CalDavProvider>();
    fx->provider->load(cfg);
    if (!waitForFutureBool(fx->provider->connect(), 5000)) return nullptr;
    if (!fx->provider->isConnected()) return nullptr;

    const auto cols = fx->provider->collections();
    if (cols.isEmpty()) return nullptr;
    fx->collId = cols.first().id;

    fx->rawRemote = fx->provider->createBackend(fx->collId);
    if (!fx->rawRemote) return nullptr;
    fx->remote = dynamic_cast<RemoteCalendarBackend *>(fx->rawRemote.get());
    if (!fx->remote) return nullptr;

    fx->remoteStateDir = std::make_unique<QTemporaryDir>();
    if (!fx->remoteStateDir->isValid()) return nullptr;
    fx->remote->setDbPath(fx->remoteStateDir->filePath(QStringLiteral("ctags.db")));
    fx->remote->setCacheDir(fx->remoteStateDir->path());

    fx->localDir = std::make_unique<QTemporaryDir>();
    if (!fx->localDir->isValid()) return nullptr;
    fx->local = std::make_unique<LocalBackend>(fx->localDir->path());
    fx->fingerprintDir = std::make_unique<QTemporaryDir>();
    if (!fx->fingerprintDir->isValid()) return nullptr;
    fx->local->setDbPath(fx->fingerprintDir->filePath(QStringLiteral("fingerprints.db")));

    fx->localCollectionId = QStringLiteral("mirror");
    {
        CollectionInfo info;
        info.id   = fx->localCollectionId;
        info.name = fx->localCollectionId;
        info.type = QStringLiteral("calendar");
        if (fx->local->createCollection(info).isEmpty()) return nullptr;
    }
    fx->mirrorDir = QDir(fx->localDir->path()).filePath(fx->localCollectionId);

    fx->registry = std::make_unique<BackendRegistry>();
    fx->registry->registerBackendInstance(fx->remote->backendId(), fx->remote);
    fx->registry->registerBackendInstance(fx->local->backendId(), fx->local.get());

    fx->host = std::make_unique<CapturingSyncHost>(fx->registry.get());
    fx->engine = std::make_unique<SyncEngine>(fx->registry.get(), fx->host.get(), shape);
    fx->engine->setSkipUnchangedMappings(skipUnchanged);

    fx->baselineDir = std::make_unique<QTemporaryDir>();
    if (!fx->baselineDir->isValid()) return nullptr;
    fx->baselines = std::make_unique<BaselineStore>(
        fx->baselineDir->filePath(QStringLiteral("baselines.db")));
    fx->engine->setBaselineStore(fx->baselines.get());

    fx->mapping.id             = QStringLiteral("b5-mapping");
    fx->mapping.sourceBackend  = fx->remote->backendId();
    fx->mapping.sourceCalendar = fx->collId;
    fx->mapping.targetBackend  = fx->local->backendId();
    fx->mapping.targetCalendar = fx->localCollectionId;
    fx->mapping.mode           = SyncMode::TwoWay;
    fx->mapping.conflictPolicy = ConflictResolution::LastWriteWins;
    fx->mapping.enabled        = true;
    fx->engine->setSyncMappings({ fx->mapping });

    return fx;
}

class TstSyncConvergence : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // The campaign's core regression gate.
    void secondSyncIsNoOp();

    // Phase B5 — acceptance matrix (all engine-level, FakeCalDavServer +
    // LocalBackend, per docs/campaign/2026-07-03-sync-convergence-roadmap.md
    // Phase B5 item 1).
    void localEditPropagatesExactlyOncePut();
    void remoteEditFetchesExactlyOneChangedItem();
    void remoteDeleteRemovesExactlyOneLocally();

    // Phase B5 item 2 — fast-path wiring: prove prepareSyncFastPath now
    // correctly identifies a genuinely-unchanged mapping as skippable, and
    // that engaging the skip means the mapping never dispatches to the
    // worker at all (so the "fetchItems runs twice" structural residual the
    // roadmap flags is moot for a fully-idle cycle).
    void fastPathSkipsGenuinelyUnchangedMapping();

    // E4 item 3 (roadmap D2's last item) — property-phase PROPPATCH
    // suppression: once a collection-property change (color) has been
    // applied and its baseline snapshot persisted (T9), a subsequent quiet
    // cycle with nothing further changed must issue ZERO PROPPATCHes.
    void colorChangeThenQuietCycle_secondCycleIssuesZeroProppatches();

private:
    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;
};

void TstSyncConvergence::initTestCase()
{
    Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
    Kalburator::registerStockPlugins(pm);
}

void TstSyncConvergence::secondSyncIsNoOp()
{
    // ── Stand up the fake server with one calendar carrying a VEVENT and a
    //    VTODO (both must survive the A1/A2 per-kind canon dispatch fixes).

    FakeCalDavServer server;
    const QString href = QStringLiteral("/calendars/testuser/personal/");
    server.setCalendarComponents(href, { QStringLiteral("VEVENT"), QStringLiteral("VTODO") });

    const QByteArray evt  = makeVEvent(QStringLiteral("evt-1"));
    const QByteArray todo = makeVTodo(QStringLiteral("todo-1"));
    server.setSeedEvents(href, { evt, todo });

    QVERIFY(server.startListening());

    // ── Connect a real CalDavProvider and get the RemoteCalendarBackend.

    BackendConfiguration cfg;
    cfg.id          = QStringLiteral("convergence-account");
    cfg.type        = QStringLiteral("caldav");
    cfg.displayName = QStringLiteral("Fake CalDAV (convergence)");
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

    std::unique_ptr<IBlobBackend> rawBackend = provider.createBackend(collId);
    QVERIFY(rawBackend != nullptr);
    auto *remoteBackend = dynamic_cast<RemoteCalendarBackend *>(rawBackend.get());
    QVERIFY(remoteBackend != nullptr);
    const QString remoteBackendId = remoteBackend->backendId();

    // ── Real LocalBackend, writing actual .ics files to a tmp dir.

    QTemporaryDir localDir;
    QVERIFY(localDir.isValid());
    LocalBackend local(localDir.path());
    const QString localCollectionId = QStringLiteral("mirror");
    const QString localBackendId = local.backendId();
    // LocalBackend requires the calendar's subdirectory to pre-exist (it
    // does not auto-create one on first fetch, only createCollection()/
    // first write does) — create it via the same CollectionInfo path a
    // real wizard-assembled local-mirror calendar would use.
    {
        CollectionInfo info;
        info.id   = localCollectionId;
        info.name = localCollectionId;
        info.type = QStringLiteral("calendar");
        QVERIFY(!local.createCollection(info).isEmpty());
    }

    // ── Wire the engine.

    BackendRegistry registry;
    registry.registerBackendInstance(remoteBackendId, remoteBackend);
    registry.registerBackendInstance(localBackendId,  &local);

    CapturingSyncHost host(&registry);
    SyncEngine engine(&registry, &host, m_shape);

    QTemporaryDir baselineDir;
    QVERIFY(baselineDir.isValid());
    BaselineStore baselines(baselineDir.filePath(QStringLiteral("baselines.db")));
    engine.setBaselineStore(&baselines);

    SyncMapping mapping;
    mapping.id             = QStringLiteral("convergence-mapping");
    mapping.sourceBackend  = remoteBackendId;
    mapping.sourceCalendar = collId;
    mapping.targetBackend  = localBackendId;
    mapping.targetCalendar = localCollectionId;
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::LastWriteWins;
    mapping.enabled        = true;
    engine.setSyncMappings({ mapping });

    auto runOnce = [&]() -> SyncResult {
        SyncRequest req;
        req.mappingIds = { mapping.id };
        req.behavior = SyncEngine::SyncBehavior::Unmonitored;
        auto future = engine.runSync(req);
        QDeadlineTimer deadline(kSyncTimeoutMs);
        while (!future.isFinished() && !deadline.hasExpired())
            QTest::qWait(10);
        if (!future.isFinished()) {
            SyncResult timedOut;
            timedOut.success = false;
            timedOut.errorMessage = QStringLiteral("sync did not finish within timeout");
            return timedOut;
        }
        return future.resultAt(0).first();
    };

    // ── Sync 1: mirror populates. Target starts empty.

    const SyncResult r1 = runOnce();
    QVERIFY2(r1.success, qUtf8Printable(r1.errorMessage));
    QVERIFY(!r1.hasUnresolvedConflicts());

    const QString mirrorDir = QDir(localDir.path()).filePath(localCollectionId);
    const QString evtPath  = QDir(mirrorDir).filePath(QStringLiteral("evt-1.ics"));
    const QString todoPath = QDir(mirrorDir).filePath(QStringLiteral("todo-1.ics"));

    QVERIFY2(QFileInfo::exists(evtPath),
             qUtf8Printable(QStringLiteral("expected %1 to exist after first sync").arg(evtPath)));
    QVERIFY2(QFileInfo::exists(todoPath),
             qUtf8Printable(QStringLiteral("expected %1 to exist after first sync").arg(todoPath)));

    // Both mirrored files must be non-empty and parse — the A1/A2 canon
    // fixes this phase depends on (VTODO no longer transcodes to empty
    // bytes; VEVENT recurrence is never corrupted from VTIMEZONE scraping).
    {
        QFile evtFile(evtPath);
        QVERIFY(evtFile.open(QIODevice::ReadOnly));
        const QByteArray evtBytes = evtFile.readAll();
        QVERIFY(!evtBytes.isEmpty());
        QVERIFY(evtBytes.contains(QByteArrayLiteral("BEGIN:VEVENT")));

        QFile todoFile(todoPath);
        QVERIFY(todoFile.open(QIODevice::ReadOnly));
        const QByteArray todoBytes = todoFile.readAll();
        QVERIFY(!todoBytes.isEmpty());
        QVERIFY(todoBytes.contains(QByteArrayLiteral("BEGIN:VTODO")));
    }

    const QDateTime evtMtimeAfterSync1  = QFileInfo(evtPath).lastModified();
    const QDateTime todoMtimeAfterSync1 = QFileInfo(todoPath).lastModified();
    const QByteArray evtBytesAfterSync1  = [&] {
        QFile f(evtPath); Q_UNUSED(f.open(QIODevice::ReadOnly)); return f.readAll();
    }();
    const QByteArray todoBytesAfterSync1 = [&] {
        QFile f(todoPath); Q_UNUSED(f.open(QIODevice::ReadOnly)); return f.readAll();
    }();

    const int putsAfterSync1    = server.requestCount(QByteArrayLiteral("PUT"));
    const int deletesAfterSync1 = server.requestCount(QByteArrayLiteral("DELETE"));

    // ── Sync 2: nothing changed anywhere. THIS is the assertion the whole
    //    campaign is about: per-side baseline hashes (Phase B4) must let
    //    the diff recognize both records as unchanged on both sides, even
    //    though LocalBackend's and RemoteCalendarBackend's native bytes for
    //    the same logical record are never byte-identical to each other.
    //    Pre-fix, this always produced a spurious update/conflict on every
    //    record, every cycle, forever.

    const SyncResult r2 = runOnce();
    QVERIFY2(r2.success, qUtf8Printable(r2.errorMessage));
    QVERIFY(!r2.hasUnresolvedConflicts());

    // No new writes reached the fake server (nothing should ever have flowed
    // toward source in this scenario — target never independently changed).
    QCOMPARE(server.requestCount(QByteArrayLiteral("PUT")),    putsAfterSync1);
    QCOMPARE(server.requestCount(QByteArrayLiteral("DELETE")), deletesAfterSync1);

    // No new writes reached the target either: same files, same bytes, same
    // mtimes. This is the literal "zero creates/updates/deletes" proof —
    // if the diff had (wrongly) decided either record was "modified" on
    // sync 2, LocalBackend would have rewritten the file and both the bytes
    // and the mtime would differ here.
    QVERIFY(QFileInfo::exists(evtPath));
    QVERIFY(QFileInfo::exists(todoPath));
    QCOMPARE(QFileInfo(evtPath).lastModified(),  evtMtimeAfterSync1);
    QCOMPARE(QFileInfo(todoPath).lastModified(), todoMtimeAfterSync1);
    {
        QFile f(evtPath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), evtBytesAfterSync1);
    }
    {
        QFile f(todoPath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), todoBytesAfterSync1);
    }

    // Exactly the two mirrored files, nothing extra (no duplicate/orphan
    // writes from a misfired diff branch).
    QDir dir(mirrorDir);
    const QStringList icsFiles = dir.entryList({ QStringLiteral("*.ics") }, QDir::Files);
    QCOMPARE(icsFiles.size(), 2);
}

// ──────────────────────────────────────────────────────────────────────────
// Phase B5 acceptance matrix (docs/campaign/2026-07-03-sync-convergence-
// roadmap.md, Phase B5 item 1). Each test seeds two independent VEVENTs —
// one that gets mutated/deleted, one left alone as a same-cycle control —
// so a test can distinguish "the engine touched what changed" from "the
// engine touched everything every cycle" (the exact class of bug B4 fixed).
// ──────────────────────────────────────────────────────────────────────────

void TstSyncConvergence::localEditPropagatesExactlyOncePut()
{
    auto fx = makeConvergenceFixture(
        m_shape,
        { makeVEventWithSummary(QStringLiteral("evt-a"), QStringLiteral("Original A")),
          makeVEventWithSummary(QStringLiteral("evt-b"), QStringLiteral("Original B")) },
        QStringLiteral("ctag-fixed"), // never changes in this test
        /*skipUnchanged=*/true);
    QVERIFY(fx != nullptr);

    const SyncResult r1 = fx->runOnce();
    QVERIFY2(r1.success, qUtf8Printable(r1.errorMessage));
    QVERIFY(!r1.hasUnresolvedConflicts());

    const QString pathA = fx->pathFor(QStringLiteral("evt-a"));
    const QString pathB = fx->pathFor(QStringLiteral("evt-b"));
    QVERIFY(QFileInfo::exists(pathA));
    QVERIFY(QFileInfo::exists(pathB));

    const int putsAfterSync1    = fx->server->requestCount(QByteArrayLiteral("PUT"));
    const int deletesAfterSync1 = fx->server->requestCount(QByteArrayLiteral("DELETE"));
    const QByteArray bBytesAfterSync1 = [&] {
        QFile f(pathB); Q_UNUSED(f.open(QIODevice::ReadOnly)); return f.readAll();
    }();
    const QDateTime bMtimeAfterSync1 = QFileInfo(pathB).lastModified();

    // Edit the LOCAL (target) mirror file directly — simulates the user
    // editing the event in PlanStan, which writes straight to the local
    // calendar file, bypassing the engine entirely until the next sync.
    {
        QFile f(pathA);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QByteArray data = f.readAll();
        f.close();
        QVERIFY(data.contains("SUMMARY:Original A"));
        data.replace("SUMMARY:Original A", "SUMMARY:Edited A Locally");
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(f.write(data), qint64(data.size()));
    }

    // Sync 2: the local edit must propagate to the remote as exactly one
    // update PUT — not a delete+recreate, not a PUT-per-mapping-record.
    const SyncResult r2 = fx->runOnce();
    QVERIFY2(r2.success, qUtf8Printable(r2.errorMessage));
    QVERIFY(!r2.hasUnresolvedConflicts());

    QCOMPARE(fx->server->requestCount(QByteArrayLiteral("PUT")),    putsAfterSync1 + 1);
    QCOMPARE(fx->server->requestCount(QByteArrayLiteral("DELETE")), deletesAfterSync1);

    // The untouched control record must be completely unaffected.
    QCOMPARE(QFileInfo(pathB).lastModified(), bMtimeAfterSync1);
    {
        QFile f(pathB);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), bBytesAfterSync1);
    }

    const int putsAfterSync2    = fx->server->requestCount(QByteArrayLiteral("PUT"));
    const int deletesAfterSync2 = fx->server->requestCount(QByteArrayLiteral("DELETE"));
    const QByteArray aBytesAfterSync2 = [&] {
        QFile f(pathA); Q_UNUSED(f.open(QIODevice::ReadOnly)); return f.readAll();
    }();
    const QDateTime aMtimeAfterSync2 = QFileInfo(pathA).lastModified();

    // Sync 3: nothing changed anywhere since sync 2 — must be a hard no-op,
    // exactly like secondSyncIsNoOp.
    const SyncResult r3 = fx->runOnce();
    QVERIFY2(r3.success, qUtf8Printable(r3.errorMessage));
    QVERIFY(!r3.hasUnresolvedConflicts());

    QCOMPARE(fx->server->requestCount(QByteArrayLiteral("PUT")),    putsAfterSync2);
    QCOMPARE(fx->server->requestCount(QByteArrayLiteral("DELETE")), deletesAfterSync2);
    QCOMPARE(QFileInfo(pathA).lastModified(), aMtimeAfterSync2);
    {
        QFile f(pathA);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), aBytesAfterSync2);
    }
}

void TstSyncConvergence::remoteEditFetchesExactlyOneChangedItem()
{
    auto fx = makeConvergenceFixture(
        m_shape,
        { makeVEventWithSummary(QStringLiteral("evt-a"), QStringLiteral("Original A")),
          makeVEventWithSummary(QStringLiteral("evt-b"), QStringLiteral("Original B")) },
        QStringLiteral("ctag-1"),
        /*skipUnchanged=*/true);
    QVERIFY(fx != nullptr);

    const SyncResult r1 = fx->runOnce();
    QVERIFY2(r1.success, qUtf8Printable(r1.errorMessage));
    QVERIFY(!r1.hasUnresolvedConflicts());

    const QString pathA = fx->pathFor(QStringLiteral("evt-a"));
    const QString pathB = fx->pathFor(QStringLiteral("evt-b"));
    QVERIFY(QFileInfo::exists(pathA));
    QVERIFY(QFileInfo::exists(pathB));

    const int multigetsAfterSync1 = fx->server->multigetReportCount();
    const int putsAfterSync1      = fx->server->requestCount(QByteArrayLiteral("PUT"));
    const int deletesAfterSync1   = fx->server->requestCount(QByteArrayLiteral("DELETE"));
    const QByteArray bBytesAfterSync1 = [&] {
        QFile f(pathB); Q_UNUSED(f.open(QIODevice::ReadOnly)); return f.readAll();
    }();
    const QDateTime bMtimeAfterSync1 = QFileInfo(pathB).lastModified();

    // Edit the item directly on the "server" (out-of-band — simulates
    // another client) and bump the collection CTag, exactly as a real
    // CalDAV server would after any write.
    fx->server->setSeedEvents(fx->href,
        { makeVEventWithSummary(QStringLiteral("evt-a"), QStringLiteral("Edited On Server")) });
    fx->server->setCollectionCtag(fx->href, QStringLiteral("ctag-2"));

    const SyncResult r2 = fx->runOnce();
    QVERIFY2(r2.success, qUtf8Printable(r2.errorMessage));
    QVERIFY(!r2.hasUnresolvedConflicts());

    // Exactly one multiget REPORT (of the one changed href) — not a
    // per-item multiget storm, not zero (the change must be observed).
    QCOMPARE(fx->server->multigetReportCount(), multigetsAfterSync1 + 1);

    // A remote-only change must never write back to the remote.
    QCOMPARE(fx->server->requestCount(QByteArrayLiteral("PUT")),    putsAfterSync1);
    QCOMPARE(fx->server->requestCount(QByteArrayLiteral("DELETE")), deletesAfterSync1);

    // Exactly one local write: evt-a picks up the new content...
    {
        QFile f(pathA);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray bytes = f.readAll();
        QVERIFY(bytes.contains("SUMMARY:Edited On Server"));
    }
    // ...evt-b (the control) is untouched.
    QCOMPARE(QFileInfo(pathB).lastModified(), bMtimeAfterSync1);
    {
        QFile f(pathB);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), bBytesAfterSync1);
    }

    const int multigetsAfterSync2 = fx->server->multigetReportCount();
    const int putsAfterSync2      = fx->server->requestCount(QByteArrayLiteral("PUT"));
    const int deletesAfterSync2   = fx->server->requestCount(QByteArrayLiteral("DELETE"));
    const QDateTime aMtimeAfterSync2 = QFileInfo(pathA).lastModified();

    // Sync 3: CTag unchanged since sync 2, content unchanged — no-op.
    const SyncResult r3 = fx->runOnce();
    QVERIFY2(r3.success, qUtf8Printable(r3.errorMessage));
    QVERIFY(!r3.hasUnresolvedConflicts());

    QCOMPARE(fx->server->multigetReportCount(),                     multigetsAfterSync2);
    QCOMPARE(fx->server->requestCount(QByteArrayLiteral("PUT")),    putsAfterSync2);
    QCOMPARE(fx->server->requestCount(QByteArrayLiteral("DELETE")), deletesAfterSync2);
    QCOMPARE(QFileInfo(pathA).lastModified(), aMtimeAfterSync2);
}

void TstSyncConvergence::remoteDeleteRemovesExactlyOneLocally()
{
    auto fx = makeConvergenceFixture(
        m_shape,
        { makeVEventWithSummary(QStringLiteral("evt-a"), QStringLiteral("Original A")),
          makeVEventWithSummary(QStringLiteral("evt-b"), QStringLiteral("Original B")) },
        QStringLiteral("ctag-1"),
        /*skipUnchanged=*/true);
    QVERIFY(fx != nullptr);

    const SyncResult r1 = fx->runOnce();
    QVERIFY2(r1.success, qUtf8Printable(r1.errorMessage));
    QVERIFY(!r1.hasUnresolvedConflicts());

    const QString pathA = fx->pathFor(QStringLiteral("evt-a"));
    const QString pathB = fx->pathFor(QStringLiteral("evt-b"));
    QVERIFY(QFileInfo::exists(pathA));
    QVERIFY(QFileInfo::exists(pathB));
    const QByteArray bBytesAfterSync1 = [&] {
        QFile f(pathB); Q_UNUSED(f.open(QIODevice::ReadOnly)); return f.readAll();
    }();
    const QDateTime bMtimeAfterSync1 = QFileInfo(pathB).lastModified();

    // Delete evt-a on the "server" out-of-band and bump the CTag. Only 2
    // baseline records exist for this mapping, so a 1-of-2 (50%) delete
    // trips the engine's built-in relative mass-delete threshold (>25% of
    // baseline, syncengine.cpp) — but no IMassDeleteGuard is registered on
    // this test engine (that's PlanStan's Track C1 integration, out of this
    // phase's scope), so per the engine's documented backward-compatible
    // default, the delete proceeds unconditionally. This test exercises
    // exactly that default path, not the guard itself.
    fx->server->removeEvent(fx->href, QStringLiteral("evt-a"));
    fx->server->setCollectionCtag(fx->href, QStringLiteral("ctag-2"));

    const SyncResult r2 = fx->runOnce();
    QVERIFY2(r2.success, qUtf8Printable(r2.errorMessage));
    QVERIFY(!r2.hasUnresolvedConflicts());

    QVERIFY2(!QFileInfo::exists(pathA), "expected the locally-mirrored evt-a to be deleted");
    QVERIFY(QFileInfo::exists(pathB));
    QCOMPARE(QFileInfo(pathB).lastModified(), bMtimeAfterSync1);
    {
        QFile f(pathB);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), bBytesAfterSync1);
    }

    QDir dir(fx->mirrorDir);
    QCOMPARE(dir.entryList({ QStringLiteral("*.ics") }, QDir::Files).size(), 1);

    // Sync 3: nothing changed since sync 2 (evt-a already gone, evt-b
    // untouched) — no further deletes, no-op.
    const int deletesAfterSync2 = fx->server->requestCount(QByteArrayLiteral("DELETE"));
    const int putsAfterSync2    = fx->server->requestCount(QByteArrayLiteral("PUT"));
    const SyncResult r3 = fx->runOnce();
    QVERIFY2(r3.success, qUtf8Printable(r3.errorMessage));
    QVERIFY(!r3.hasUnresolvedConflicts());
    QCOMPARE(fx->server->requestCount(QByteArrayLiteral("DELETE")), deletesAfterSync2);
    QCOMPARE(fx->server->requestCount(QByteArrayLiteral("PUT")),    putsAfterSync2);
    QCOMPARE(dir.entryList({ QStringLiteral("*.ics") }, QDir::Files).size(), 1);
}

// ──────────────────────────────────────────────────────────────────────────
// Phase B5 item 2/3 — fast-path wiring + idle-cycle budget.
//
// Proves SyncEngine::prepareSyncFastPath correctly identifies a genuinely
// unchanged mapping as skip-eligible post-B3/B4 (the roadmap's real-world
// symptom was "of 7 mappings, 0 are unchanged" — this asserts the opposite:
// the ONE mapping here IS recognized unchanged), and that engaging the skip
// (setSkipUnchangedMappings(true)) means the mapping is never dispatched to
// the worker at all for an idle cycle: zero calendar-query/multiget REPORTs,
// zero PUT/DELETE, zero local file touches — only the one CTag PROPFIND
// prepareSyncFastPath itself issues. Because the skip short-circuits before
// SyncEngineWorker ever calls fetchItems, the "fetchItems runs at least
// twice per mapping" structural residual the roadmap flags (gating fetch +
// loadRecords' internal reuse) is moot for this path — it never runs even
// once on an idle cycle.
//
// Investigation finding: a BRAND NEW collection needs TWO real (non-skipped)
// sync cycles before the fast path can engage on the third, not one — this
// is a genuine characteristic of the engine-owned sync-progress token (H3,
// BaselineStore::syncToken/setSyncToken), not a bug:
//   - Sync 1 (populate): prepareSyncFastPath's pre-fetch snapshot captures
//     the source's live ctag and the target's (still-empty-mirror)
//     fingerprint BEFORE this run's fetch+apply. Since no token is stored
//     yet, the mapping is correctly judged NOT skip-eligible and dispatches
//     normally, populating the local mirror. On success, onWorkerSyncCompleted
//     writes THIS pre-fetch snapshot as the stored token for both sides —
//     per H3's design, the token is always the pre-fetch value, never a
//     post-write re-query (a post-write re-query is exactly the O18 masking
//     bug H3 closes). So after sync 1, the stored target token reflects the
//     EMPTY mirror, not the just-populated one.
//   - Sync 2: prepareSyncFastPath's fresh query now sees the source ctag
//     unchanged (matches the stored token — source-side unchanged) but the
//     target's fresh fingerprint reflects the POPULATED mirror, which does
//     NOT match the stored (pre-population) token — so the mapping is still
//     judged not skip-eligible and dispatches again. This second dispatch is
//     a genuine no-op (content already converged), but its pre-fetch
//     snapshot now captures the STEADY-STATE fingerprint on both sides;
//     onWorkerSyncCompleted persists that as the new stored token.
//   - Sync 3: the fresh query matches the sync-2-stored token on BOTH sides
//     (nothing changed between sync 2's snapshot and its completion) — this
//     is the first cycle that can actually skip.
// This matches the roadmap's framing that a mirror only reaches its cheap
// steady state after the initial population settles — the one-cycle lag is
// an accepted cost of the pre-fetch-snapshot design (CP-A), never a masked
// change: a stale token can only cost an extra redundant re-diff.
void TstSyncConvergence::fastPathSkipsGenuinelyUnchangedMapping()
{
    auto fx = makeConvergenceFixture(
        m_shape,
        { makeVEventWithSummary(QStringLiteral("evt-a"), QStringLiteral("Original A")) },
        QStringLiteral("ctag-fixed"),
        /*skipUnchanged=*/true);
    QVERIFY(fx != nullptr);

    // Sync 1: real work — populates the mirror. Stores this cycle's
    // pre-fetch snapshot as both sides' sync-progress token (H3), which for
    // the target reflects the PRE-population (empty-mirror) state — see the
    // comment above.
    const SyncResult r1 = fx->runOnce();
    QVERIFY2(r1.success, qUtf8Printable(r1.errorMessage));
    QVERIFY(!r1.hasUnresolvedConflicts());

    const QString pathA = fx->pathFor(QStringLiteral("evt-a"));
    QVERIFY(QFileInfo::exists(pathA));
    const QByteArray aBytesAfterSync1 = [&] {
        QFile f(pathA); Q_UNUSED(f.open(QIODevice::ReadOnly)); return f.readAll();
    }();
    const QDateTime aMtimeAfterSync1 = QFileInfo(pathA).lastModified();
    const int putsAfterSync1    = fx->server->requestCount(QByteArrayLiteral("PUT"));
    const int deletesAfterSync1 = fx->server->requestCount(QByteArrayLiteral("DELETE"));

    // Sync 2: still real work (the target's fresh fingerprint doesn't match
    // sync 1's pre-population token, so the mapping is correctly judged
    // not-yet-skippable) — but this cycle's own pre-fetch snapshot now
    // reflects the steady-state mirror, so it's the token that lets sync 3
    // skip. Content is unchanged throughout, so this must still be a
    // write-free no-op on both sides even though it is NOT skipped outright
    // — falsifiable via the per-B4 baseline machinery, same proof shape as
    // secondSyncIsNoOp.
    const SyncResult r2 = fx->runOnce();
    QVERIFY2(r2.success, qUtf8Printable(r2.errorMessage));
    QVERIFY(!r2.hasUnresolvedConflicts());
    QCOMPARE(fx->server->requestCount(QByteArrayLiteral("PUT")),    putsAfterSync1);
    QCOMPARE(fx->server->requestCount(QByteArrayLiteral("DELETE")), deletesAfterSync1);
    QCOMPARE(QFileInfo(pathA).lastModified(), aMtimeAfterSync1);
    {
        QFile f(pathA);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), aBytesAfterSync1);
    }

    const int propfindsAfterSync2 = fx->server->requestCount(QByteArrayLiteral("PROPFIND"));
    const int reportsAfterSync2   = fx->server->requestCount(QByteArrayLiteral("REPORT"));
    const int multigetsAfterSync2 = fx->server->multigetReportCount();
    const int putsAfterSync2      = fx->server->requestCount(QByteArrayLiteral("PUT"));
    const int deletesAfterSync2   = fx->server->requestCount(QByteArrayLiteral("DELETE"));
    const QByteArray aBytesAfterSync2 = [&] {
        QFile f(pathA); Q_UNUSED(f.open(QIODevice::ReadOnly)); return f.readAll();
    }();
    const QDateTime aMtimeAfterSync2 = QFileInfo(pathA).lastModified();

    // Sanity: sync 2 really did touch the network (it was NOT skipped) —
    // otherwise sync 3's "no NEW requests" assertions below would be
    // vacuously true for the wrong reason.
    QVERIFY(reportsAfterSync2 > 0);

    // Sync 3: NOW both sides' ChangeDetection caches are warm and correct.
    // With per-side baselines (B4) and CTag/content-cache coherence (B3)
    // both in place, prepareSyncFastPath must compute matching fresh-vs-
    // cached revisions on BOTH sides and mark the mapping skip-eligible;
    // with the flag on, the mapping is skipped outright
    // (SyncEngine::advanceQueue's m_skippedMappingIds branch,
    // syncengine.cpp) rather than dispatched to the worker.
    const SyncResult r3 = fx->runOnce();
    QVERIFY2(r3.success, qUtf8Printable(r3.errorMessage));
    QVERIFY(!r3.hasUnresolvedConflicts());

    // Idle-cycle budget: exactly one CTag PROPFIND (prepareSyncFastPath's
    // own collectionRevisions() call for the remote side; the local side's
    // ChangeDetection is a directory-fingerprint scan, no network) —
    // and ZERO calendar-query REPORTs, ZERO multiget REPORTs, ZERO
    // PUT/DELETE. If the mapping were (wrongly) dispatched to the worker,
    // at least one calendar-query REPORT would appear here even in the
    // fully-unchanged case (fetchItems's list-then-serve-from-cache path
    // still issues one calendar-query REPORT) — so this REPORT-count
    // assertion is the falsifiable proof that the skip actually fired,
    // not just that no writes happened to.
    QCOMPARE(fx->server->requestCount(QByteArrayLiteral("PROPFIND")), propfindsAfterSync2 + 1);
    QCOMPARE(fx->server->requestCount(QByteArrayLiteral("REPORT")),   reportsAfterSync2);
    QCOMPARE(fx->server->multigetReportCount(),                       multigetsAfterSync2);
    QCOMPARE(fx->server->requestCount(QByteArrayLiteral("PUT")),      putsAfterSync2);
    QCOMPARE(fx->server->requestCount(QByteArrayLiteral("DELETE")),   deletesAfterSync2);

    // Zero local file parses/writes: byte- and mtime-identical, across BOTH
    // the sync-2-to-3 gap (this assertion) and the whole sync-1-to-3 span
    // (bytes/mtime already proven stable sync1->sync2 implicitly by sync2
    // being a no-write cycle above).
    QCOMPARE(QFileInfo(pathA).lastModified(), aMtimeAfterSync2);
    {
        QFile f(pathA);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), aBytesAfterSync2);
    }
}

void TstSyncConvergence::colorChangeThenQuietCycle_secondCycleIssuesZeroProppatches()
{
    // skipUnchanged=false: every cycle dispatches fully (property phase
    // included) rather than being skipped outright by the fast path — this
    // test is about the property-diff suppression itself, not fast-path
    // skip-eligibility (that's fastPathSkipsGenuinelyUnchangedMapping's job).
    auto fx = makeConvergenceFixture(
        m_shape,
        { makeVEventWithSummary(QStringLiteral("evt-color"), QStringLiteral("Original")) },
        QStringLiteral("ctag-fixed"),
        /*skipUnchanged=*/false);
    QVERIFY(fx != nullptr);

    // Give the TARGET (LocalBackend) a color the source doesn't have yet —
    // cycle 1's property phase must propagate it to source via PROPPATCH.
    QVERIFY(fx->local->setCalendarColor(fx->localCollectionId, QColor(Qt::red)));

    const SyncResult r1 = fx->runOnce();
    QVERIFY2(r1.success, qUtf8Printable(r1.errorMessage));
    QVERIFY(!r1.hasUnresolvedConflicts());

    const int proppatchesAfterSync1 = fx->server->requestCount(QByteArrayLiteral("PROPPATCH"));
    QVERIFY2(proppatchesAfterSync1 > 0,
             "cycle 1 must have propagated the color change via PROPPATCH");

    // Cycle 2: nothing changed anywhere since. The color-baseline snapshot
    // T9 persisted after cycle 1 must suppress a re-apply — zero NEW
    // PROPPATCHes.
    const SyncResult r2 = fx->runOnce();
    QVERIFY2(r2.success, qUtf8Printable(r2.errorMessage));
    QVERIFY(!r2.hasUnresolvedConflicts());

    QCOMPARE(fx->server->requestCount(QByteArrayLiteral("PROPPATCH")), proppatchesAfterSync1);
}

QTEST_MAIN(TstSyncConvergence)
#include "tst_sync_convergence.moc"
