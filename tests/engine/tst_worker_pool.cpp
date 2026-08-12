// Parallel-sync Task 2 — worker pool lifecycle.
//
// The pool replaces the single m_worker/m_workerThread pair. At size 1 it
// must be the pre-pool code path exactly; the rest of the suite proves
// that. This binary pins the pool's own invariants: it starts, it is
// idempotent, it grows, every worker lands on its own thread, and
// teardown leaves nothing running.
//
// No real sync ever runs in this binary, so the ShapeRegistries bundle is
// left with no domain plugins registered — SyncEngineWorker's constructor
// and SyncEngine::startWorkerPool() never touch domain state, only
// SyncEngineWorker::processSync (never invoked here) would need it.

#include <QtTest/QtTest>
#include <QObject>
#include <QThread>

#include "backendregistry.h"
#include "isynchost.h"
#include "lossprofile.h"
#include "mockbackend.h"
#include "shaperegistries.h"
#include "syncbackend.h"
#include "syncengine.h"

using Kalburator::Sync::BackendRegistry;
using Kalburator::Sync::ISyncConfigStore;
using Kalburator::Sync::ISyncHost;
using Kalburator::Sync::MockBackend;
using Kalburator::Sync::SyncBackend;
using Kalburator::Engine::SyncEngine;

namespace {

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

    void syncStarted(const QString &, const Kalburator::Shape::LossProfile &) override {}
    void recordChanged(const QString &, const QString &, ChangeKind) override {}

private:
    BackendRegistry *m_registry = nullptr;
};

} // namespace

class TestWorkerPool : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_registry = std::make_unique<BackendRegistry>();
        m_host     = std::make_unique<RegistrySyncHost>(m_registry.get());
        m_shape    = std::make_unique<Kalburator::Shape::ShapeRegistries>();
        m_engine   = std::make_unique<SyncEngine>(m_registry.get(), m_host.get(), *m_shape);
    }

    void cleanup()
    {
        m_engine.reset();
        m_shape.reset();
        m_host.reset();
        m_registry.reset();
    }

    void testPoolStartsAtRequestedSize()
    {
        m_engine->startWorkerPool(1);
        QCOMPARE(m_engine->poolSizeForTest(), 1);
        QVERIFY(m_engine->poolThreadsRunningForTest());
    }

    void testStartIsIdempotent()
    {
        m_engine->startWorkerPool(1);
        m_engine->startWorkerPool(1);
        QCOMPARE(m_engine->poolSizeForTest(), 1);
    }

    void testPoolGrows()
    {
        m_engine->startWorkerPool(1);
        m_engine->startWorkerPool(4);
        QCOMPARE(m_engine->poolSizeForTest(), 4);
        QVERIFY(m_engine->poolThreadsRunningForTest());
    }

    void testPoolNeverShrinks()
    {
        m_engine->startWorkerPool(4);
        m_engine->startWorkerPool(1);
        QCOMPARE(m_engine->poolSizeForTest(), 4);
    }

    void testEveryWorkerHasItsOwnThread()
    {
        m_engine->startWorkerPool(4);
        QCOMPARE(m_engine->distinctPoolThreadCountForTest(), 4);
    }

    void testStopLeavesNothingRunning()
    {
        m_engine->startWorkerPool(4);
        m_engine->stopWorkerPool();
        QCOMPARE(m_engine->poolSizeForTest(), 0);
        QVERIFY(!m_engine->poolThreadsRunningForTest());
    }

private:
    std::unique_ptr<BackendRegistry>                  m_registry;
    std::unique_ptr<RegistrySyncHost>                 m_host;
    std::unique_ptr<Kalburator::Shape::ShapeRegistries> m_shape;
    std::unique_ptr<SyncEngine>                       m_engine;
};

QTEST_MAIN(TestWorkerPool)
#include "tst_worker_pool.moc"
