/// G.6 Task 46 — CancellationReason integration tests.
///
/// Verifies that SyncEngineFuture::cancelWithReason propagates correctly:
/// - UserRequested cancels the entire queue via the underlying QFuture.
/// - ResourceLost("res-x") cancels only mappings whose backends use "res-x";
///   mappings using other resources continue and complete normally.

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QTimeZone>

#include "backendregistry.h"
#include "baselinestore.h"
#include "conflictmanager.h"
#include "mockbackend.h"
#include "syncengine.h"
#include "syncenginefuture.h"
#include "syncconflictstore.h"
#include "synctypes.h"

#include "stubsynchost.h"

using namespace Kalburator::Sync;
using namespace Kalburator::Sync::Test;

namespace {

constexpr int kSyncTimeoutMs = 5000;

// MockBackend subclass with a controllable resourceId.
class ResourcedMockBackend : public MockBackend {
public:
    explicit ResourcedMockBackend(const QString &backendId,
                                   const QString &resourceId)
        : MockBackend(backendId), m_resourceId(resourceId) {}

    QString resourceId() const override { return m_resourceId; }

private:
    QString m_resourceId;
};

SyncMapping makeMapping(const QString &id,
                        const QString &src, const QString &tgt,
                        const QString &col)
{
    SyncMapping m;
    m.id             = id;
    m.sourceBackend  = src;
    m.sourceCalendar = col;
    m.targetBackend  = tgt;
    m.targetCalendar = col;
    m.mode           = SyncMode::TwoWay;
    m.conflictPolicy = ConflictResolution::SourceWins;
    m.enabled        = true;
    return m;
}

} // namespace

class TstCancellationReason : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    /// cancel() (UserRequested) stops the entire queue; the future is
    /// marked isCanceled() and the reason is UserRequested.
    void userRequestedCancel_stopsFuture();

    /// cancelWithReason(ResourceLost, "res-x") before the queue starts:
    /// mappings whose backends use "res-x" are skipped (cancelled results);
    /// mappings using other resources run and succeed.
    void resourceLostBeforeStart_skipsAffectedMappings();

    /// CancellationReason is preserved and readable from SyncEngineFuture.
    void cancellationReasonIsReadable();

private:
    std::unique_ptr<QTemporaryDir>                  m_tmpDir;
    std::unique_ptr<BackendRegistry>                m_registry;
    std::vector<std::unique_ptr<ResourcedMockBackend>> m_backends;
    std::unique_ptr<StubSyncHost>                   m_host;
    std::unique_ptr<Kalburator::Storage::BaselineStore> m_calBaselines;
    std::unique_ptr<SyncConflictStore>              m_conflictStore;
    std::unique_ptr<ConflictManager>               m_conflictManager;
    std::unique_ptr<SyncEngine>                    m_engine;

    // Helpers
    ResourcedMockBackend *makeAndRegister(const QString &backendId,
                                          const QString &resource);
    void setupStubCalendar(const QString &calId);
};

ResourcedMockBackend *TstCancellationReason::makeAndRegister(const QString &backendId,
                                                               const QString &resource)
{
    auto b = std::make_unique<ResourcedMockBackend>(backendId, resource);
    b->createCalendar(QStringLiteral("col"), QStringLiteral("col"), backendId);
    m_registry->registerBackendInstance(backendId, b.get());
    auto *ptr = b.get();
    m_backends.push_back(std::move(b));
    return ptr;
}

void TstCancellationReason::setupStubCalendar(const QString &calId)
{
    auto *hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal->setId(calId);
    m_host->stubCollection()->addCalendarWithId(calId, hostCal);
}

void TstCancellationReason::init()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());

    m_registry = std::make_unique<BackendRegistry>();
    m_host     = std::make_unique<StubSyncHost>(m_registry.get());

    setupStubCalendar(QStringLiteral("col"));

    const QString dbPath = m_tmpDir->filePath(QStringLiteral(".kalburator-sync.db"));
    m_calBaselines    = std::make_unique<Kalburator::Storage::BaselineStore>(dbPath);
    m_conflictStore   = std::make_unique<SyncConflictStore>(dbPath);
    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());

    m_engine = std::make_unique<SyncEngine>(m_registry.get(), m_host.get());
    m_engine->setBaselineStore(m_calBaselines.get());
    m_engine->setSyncConflictStore(m_conflictStore.get());
    m_engine->setConflictManager(m_conflictManager.get());
    m_engine->setCollection(m_host->stubCollection());
}

void TstCancellationReason::cleanup()
{
    m_engine.reset();
    m_conflictManager.reset();
    m_conflictStore.reset();
    m_calBaselines.reset();
    m_host.reset();
    m_backends.clear();
    m_registry.reset();
    m_tmpDir.reset();
}

// ---------------------------------------------------------------------------

void TstCancellationReason::userRequestedCancel_stopsFuture()
{
    makeAndRegister(QStringLiteral("src1"), QStringLiteral("res-a"));
    makeAndRegister(QStringLiteral("tgt1"), QStringLiteral("res-b"));

    m_engine->setSyncMappings({
        makeMapping(QStringLiteral("m1"),
                    QStringLiteral("src1"), QStringLiteral("tgt1"),
                    QStringLiteral("col")),
    });

    SyncEngineFuture f(m_engine->runSyncFuture());

    QVERIFY(f.isValid());
    QCOMPARE(f.cancellationReason(), CancellationReason::UserRequested);

    f.cancel();

    // Wait for the future to fully finish (not just isCanceled) so that the
    // worker thread has completed its work before cleanup() calls
    // stopWorkerThread(). Without this, cleanup() deadlocks: the engine
    // thread blocks in QThread::wait() while the worker is blocked in a
    // BlockingQueuedConnection call to the engine thread.
    QTRY_VERIFY_WITH_TIMEOUT(f.isFinished(), kSyncTimeoutMs);

    QCOMPARE(f.cancellationReason(), CancellationReason::UserRequested);
}

void TstCancellationReason::resourceLostBeforeStart_skipsAffectedMappings()
{
    // Three mappings (sequential queue, v1):
    //   m1: src-a(res-b) → tgt-a(res-c)  — no res-x    → RUN and SUCCEED
    //   m2: src-b(res-b) → tgt-b(res-c)  — no res-x    → RUN and SUCCEED
    //   m3: src-c(res-x) → tgt-c(res-x)  — uses res-x  → SKIPPED (cancelled result)
    //
    // cancelWithReason(ResourceLost, "res-x") is called after runSyncFuture().
    // Since v1 is sequential, m1 and m2 may already be done or in-flight by
    // the time the engine processes m3; m3's backends use res-x so it is
    // skipped with a cancelled SyncResult. All 3 results appear in the future.
    makeAndRegister(QStringLiteral("src-a"), QStringLiteral("res-b"));
    makeAndRegister(QStringLiteral("tgt-a"), QStringLiteral("res-c"));
    makeAndRegister(QStringLiteral("src-b"), QStringLiteral("res-b"));
    makeAndRegister(QStringLiteral("tgt-b"), QStringLiteral("res-c"));
    makeAndRegister(QStringLiteral("src-c"), QStringLiteral("res-x"));
    makeAndRegister(QStringLiteral("tgt-c"), QStringLiteral("res-x"));

    m_engine->setSyncMappings({
        makeMapping(QStringLiteral("m1"),
                    QStringLiteral("src-a"), QStringLiteral("tgt-a"),
                    QStringLiteral("col")),
        makeMapping(QStringLiteral("m2"),
                    QStringLiteral("src-b"), QStringLiteral("tgt-b"),
                    QStringLiteral("col")),
        makeMapping(QStringLiteral("m3"),
                    QStringLiteral("src-c"), QStringLiteral("tgt-c"),
                    QStringLiteral("col")),
    });

    SyncEngineFuture f(m_engine->runSyncFuture());

    // Inject ResourceLost. m1 and m2 are already queued/in-flight but don't use
    // res-x, so they complete normally. m3 uses res-x and is skipped with a
    // cancelled result when advanceQueue reaches it.
    m_engine->cancelWithReason(CancellationReason::ResourceLost,
                               QStringLiteral("res-x"));

    QTRY_VERIFY_WITH_TIMEOUT(f.isFinished(), kSyncTimeoutMs);

    // The future is NOT marked isCanceled() — the queue completed normally
    // (ResourceLost only skips specific mappings, not the whole run).
    QVERIFY(!f.isCanceled());

    const auto results = f.results();
    // All 3 mapping slots are represented.
    QCOMPARE(results.size(), 3);

    // m1 and m2 succeeded (don't touch res-x).
    int successCount = 0;
    int cancelledCount = 0;
    for (const auto &r : results) {
        if (r.success)
            ++successCount;
        if (r.cancelled)
            ++cancelledCount;
    }
    QVERIFY(successCount >= 2); // m1 and m2 at minimum
    QVERIFY(cancelledCount >= 1); // m3 at minimum
}

void TstCancellationReason::cancellationReasonIsReadable()
{
    // Verify that the reason stored on a SyncEngineFuture survives
    // across copies (shared state).
    makeAndRegister(QStringLiteral("src1"), QStringLiteral("res-a"));
    makeAndRegister(QStringLiteral("tgt1"), QStringLiteral("res-b"));

    m_engine->setSyncMappings({
        makeMapping(QStringLiteral("m1"),
                    QStringLiteral("src1"), QStringLiteral("tgt1"),
                    QStringLiteral("col")),
    });

    SyncEngineFuture f1(m_engine->runSyncFuture());
    SyncEngineFuture f2 = f1; // copy — shares state

    QCOMPARE(f1.cancellationReason(), CancellationReason::UserRequested);
    QCOMPARE(f2.cancellationReason(), CancellationReason::UserRequested);

    f1.cancelWithReason(CancellationReason::Timeout);

    // Both copies see the updated reason.
    QCOMPARE(f1.cancellationReason(), CancellationReason::Timeout);
    QCOMPARE(f2.cancellationReason(), CancellationReason::Timeout);

    QTRY_VERIFY_WITH_TIMEOUT(f1.isFinished(), kSyncTimeoutMs);
}

QTEST_MAIN(TstCancellationReason)
#include "tst_cancellation_reason.moc"
