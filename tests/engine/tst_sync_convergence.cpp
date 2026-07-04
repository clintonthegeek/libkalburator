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
#include <QDir>
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

} // namespace

class TstSyncConvergence : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // The campaign's core regression gate.
    void secondSyncIsNoOp();

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

QTEST_MAIN(TstSyncConvergence)
#include "tst_sync_convergence.moc"
