// Sync-excellence campaign — FINDINGS O43 RED test (found 2026-07-09 at E10).
//
// SyncEngineWorker::prepareFastPath's A6 revision-query marshal posts a
// QueuedConnection lambda onto the backend's thread capturing `&revs` and
// `&loop` — both on the WORKER's stack — then blocks in loop.exec(). If the
// engine is torn down while that lambda is still pending on the backend
// thread, QThread::quit() (stopWorkerThread) exits the nested loop, the
// worker frame unwinds, and the pending lambda is left holding dangling
// stack pointers. When the backend thread later runs it, the continuation
// calls QMetaObject::invokeMethod on the dead QEventLoop* → SIGSEGV.
//
// PlanStan hit this deterministically (5/5) at the E10 pin bump:
// tst_collectioncontroller::testAutoSyncOnLoadDeferredUntilSyncInfraReady
// destroys CollectionController right after auto-sync-on-load kicks off,
// with backends relocated to the shared I/O thread. The same window exists
// live: app close mid-sync. Green at v0.84 (pre-A6).
//
// This test reproduces the window exactly: a ChangeDetection-capable
// backend on its own (deliberately blocked) I/O thread, an engine torn
// down while its fast-path revision query is still queued behind the
// blocker, then the blocker released so the stale lambda actually runs.
// RED: SIGSEGV. GREEN (O43 fix): the heap-owned rendezvous outlives the
// worker frame and drops the late result harmlessly.

#include <QtTest/QtTest>
#include <QObject>
#include <QSemaphore>
#include <QString>
#include <QThread>

#include "backendregistry.h"
#include "changedetection.h"
#include "isynchost.h"
#include "lossprofile.h"
#include "mockbackend.h"
#include "pluginmanager.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "syncbackend.h"
#include "syncengine.h"
#include "syncrequest.h"
#include "synctypes.h"

using Kalburator::Sync::BackendRegistry;
using Kalburator::Sync::ConflictResolution;
using Kalburator::Sync::ISyncConfigStore;
using Kalburator::Sync::ISyncHost;
using Kalburator::Sync::MockBackend;
using Kalburator::Sync::SyncBackend;
using Kalburator::Engine::SyncEngine;
using Kalburator::Engine::SyncRequest;
using Kalburator::Sync::SyncMapping;
using Kalburator::Sync::SyncMode;
using Kalburator::Shape::LossProfile;

namespace {

// Minimal ISyncHost over a BackendRegistry (same shape as the other engine
// tests; duplicated because the engine-test target links no shared stub
// carrying it).
class RegistrySyncHost final : public ISyncHost
{
public:
    explicit RegistrySyncHost(BackendRegistry *registry) : m_registry(registry) {}

    SyncBackend *backendById(const QString &id) override
    {
        return m_registry ? static_cast<SyncBackend*>(m_registry->backendInstance(id))
                          : nullptr;
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

// MockBackend + ChangeDetection: enough for prepareFastPath's
// dynamic_cast to include it in the revision query. The default
// collectionRevisionsAsync (synchronous adapter over collectionRevisions)
// is exactly the path PlanStan's crash stacks show.
class RevisionQueryBackend : public MockBackend, public Kalburator::Sync::ChangeDetection
{
    Q_OBJECT
public:
    using MockBackend::MockBackend;

    QString collectionRevision(const QString &) override
    {
        return QStringLiteral("rev-current");
    }
    QString cachedCollectionRevision(const QString &) const override
    {
        return QString();
    }
};

} // namespace

class TstFastPathTeardownRace : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void teardownMidRevisionQuery_pendingBackendLambdaIsDropped();

private:
    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;
};

void TstFastPathTeardownRace::initTestCase()
{
    Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
    Kalburator::registerStockPlugins(pm);
}

void TstFastPathTeardownRace::teardownMidRevisionQuery_pendingBackendLambdaIsDropped()
{
    RevisionQueryBackend source(QStringLiteral("src"));
    RevisionQueryBackend target(QStringLiteral("tgt"));

    QThread ioThread;
    ioThread.setObjectName(QStringLiteral("o43-test-backend-io"));
    ioThread.start();
    source.moveToThread(&ioThread);
    target.moveToThread(&ioThread);

    BackendRegistry registry;
    registry.registerBackendInstance(QStringLiteral("src"), &source);
    registry.registerBackendInstance(QStringLiteral("tgt"), &target);
    RegistrySyncHost host(&registry);

    // Block the backend I/O thread so the worker's revision-query lambda
    // stays PENDING in the thread's queue across the whole engine teardown.
    QSemaphore ioThreadBlocker;
    QMetaObject::invokeMethod(&source, [&ioThreadBlocker]() {
        ioThreadBlocker.acquire();
    }, Qt::QueuedConnection);

    {
        SyncEngine engine(&registry, &host, m_shape);
        engine.setSkipUnchangedMappings(true);

        SyncMapping mapping;
        mapping.id             = QStringLiteral("o43-mapping");
        mapping.sourceBackend  = QStringLiteral("src");
        mapping.sourceCalendar = QStringLiteral("col-src");
        mapping.targetBackend  = QStringLiteral("tgt");
        mapping.targetCalendar = QStringLiteral("col-tgt");
        mapping.mode           = SyncMode::TwoWay;
        mapping.conflictPolicy = ConflictResolution::SourceWins;
        mapping.enabled        = true;
        engine.setSyncMappings({ mapping });

        // EMPTY mappingIds => all-enabled => multi-mapping driver => the
        // fast-path pre-pass (single-mapping requests bypass it).
        SyncRequest req;
        req.behavior = SyncEngine::SyncBehavior::Unmonitored;
        auto f = engine.runSync(req);
        Q_UNUSED(f);

        // Let the worker reach prepareFastPath and park in its rendezvous:
        // it posts the revision query onto the (blocked) backend thread and
        // waits for the continuation. 300 ms is orders of magnitude beyond
        // the two queued hops involved.
        QTest::qWait(300);
    }
    // ~SyncEngine → stopWorkerThread() → QThread::quit(): quitNow exits the
    // worker's nested rendezvous loop, prepareFastPath's frame unwinds, and
    // the engine is gone — with the revision query still queued on the
    // blocked backend thread.

    // Release the backend thread: the stale lambda now actually runs.
    // Pre-fix it invokes a dangling worker-stack QEventLoop* (SEGV); with
    // the O43 fix the heap-owned rendezvous drops the late result.
    ioThreadBlocker.release();

    // Prove the stale lambda ran to completion: a marker queued BEHIND it
    // on the same thread must be reached.
    QSemaphore drained;
    QMetaObject::invokeMethod(&source, [&drained]() {
        drained.release();
    }, Qt::QueuedConnection);
    QVERIFY2(drained.tryAcquire(1, 5000),
             "backend I/O thread never drained the pending revision query");

    ioThread.quit();
    QVERIFY(ioThread.wait(5000));
}

QTEST_GUILESS_MAIN(TstFastPathTeardownRace)
#include "tst_fastpath_teardown_race.moc"
