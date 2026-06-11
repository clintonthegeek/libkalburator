// SPDX-License-Identifier: GPL-2.0-or-later
//
// Architectural-redress Plan 8 step 3 (2026-06-10) — pins the CANONICAL
// single-mapping cancel contract after the dual future-interface collapse.
//
// Before the collapse, runSync(SyncRequest) with a single mapping wrapped
// the native single-iface future via QFuture::then(). Qt6 drops a .then()
// continuation when its source is canceled, so a canonical single-mapping
// consumer saw resultCount()==0 after cancel — NO result at all, not even a
// cancelled one (the WildPalms finding; FINDINGS "From Plan 1" / "By Plan 8").
// WildPalms worked around it with a resultCount()>0 guard + isCanceled()
// synthesis in their watcher.
//
// After the step-3 collapse the single-mapping branch reports NATIVELY into
// the sole QFutureInterface<QList<SyncResult>> (no .then() wrap), so the
// F2 Task 23 cancellation contract — resultCount()==1, a one-element list
// whose SyncResult.cancelled==true — is preserved on the canonical path and
// the guard becomes unnecessary for the lib's own callers.
//
// Falsifiability (INVARIANTS §6): shown RED against the pre-collapse engine
// (the T1 tree, commit 0595044). There runSync()'s single-mapping branch
// .then()-wraps dispatchSingleNative, and the engine's cancel watcher is
// bound to the NATIVE future — not the .then() continuation the caller holds.
// Canceling the canonical single-mapping future therefore never reaches the
// worker: the run completes and writes items (observed: 2 written, so the
// "no items reached the destination" QCOMPARE below fails first). Only the
// deprecated runSyncFuture(mappingId) shims — which returned the native future
// verbatim — had a working single-mapping cancel pre-collapse. The step-3
// collapse makes the canonical entry native, so cancel works here too. This
// test passes only on the post-collapse (T2, 26c90ff) engine.

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QTimeZone>

#include "backendregistry.h"
#include "baselinestore.h"
#include "mockbackend.h"
#include "pluginmanager.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "syncengine.h"
#include "syncrequest.h"
#include "synctypes.h"

#include "stubsynchost.h"

#include <KCalendarCore/Event>
#include <KCalendarCore/MemoryCalendar>

#include <memory>

using namespace Kalburator::Sync;
using namespace Kalburator::Sync::Test;

namespace {

// Generous bound: QTRY_VERIFY returns as soon as the future finishes, so a
// large timeout costs nothing on success and only guards against worker-thread
// scheduling jitter under parallel ctest load (cf. tst_engine_cancellation).
constexpr int  kSyncTimeoutMs   = 30000;
constexpr auto kSourceBackendId = "source-mock";
constexpr auto kTargetBackendId = "target-mock";
constexpr auto kCollectionId    = "stub-collection";
constexpr auto kCalendarId      = "cal1";
constexpr auto kMappingId       = "m1";

KCalendarCore::Event::Ptr makeEvent(const QString &uid, const QString &summary)
{
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    event->setUid(uid);
    event->setSummary(summary);
    event->setDtStart(QDateTime::currentDateTimeUtc());
    return event;
}

SyncMapping makeCalendarMapping()
{
    SyncMapping m;
    m.id             = QString::fromLatin1(kMappingId);
    m.sourceBackend  = QString::fromLatin1(kSourceBackendId);
    m.sourceCalendar = QString::fromLatin1(kCalendarId);
    m.targetBackend  = QString::fromLatin1(kTargetBackendId);
    m.targetCalendar = QString::fromLatin1(kCalendarId);
    m.mode           = SyncMode::TwoWay;
    m.conflictPolicy = ConflictResolution::LastWriteWins;
    m.enabled        = true;
    return m;
}

} // namespace

class TstEngineSingleMappingCancel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    /// Canonical single-mapping runSync(SyncRequest) cancel preserves the
    /// F2 Task 23 contract NATIVELY (no .then() wrap): resultCount()==1, and
    /// resultAt(0) is a one-element list whose SyncResult.cancelled==true.
    void canonicalSingleMappingCancel_preservesNativeResult();

private:
    std::unique_ptr<QTemporaryDir>                      m_tmpDir;
    std::unique_ptr<BackendRegistry>                    m_registry;
    std::unique_ptr<MockBackend>                        m_src;
    std::unique_ptr<MockBackend>                        m_dst;
    std::unique_ptr<StubSyncHost>                       m_host;
    std::unique_ptr<Kalburator::Storage::BaselineStore> m_baselines;
    std::unique_ptr<SyncEngine>                         m_engine;

    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;
};

void TstEngineSingleMappingCancel::initTestCase()
{
    Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
    Kalburator::registerStockPlugins(pm);
}

void TstEngineSingleMappingCancel::init()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());

    m_registry = std::make_unique<BackendRegistry>();
    m_src = std::make_unique<MockBackend>();
    m_dst = std::make_unique<MockBackend>();
    m_registry->registerBackendInstance(QString::fromLatin1(kSourceBackendId), m_src.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kTargetBackendId), m_dst.get());

    m_host = std::make_unique<StubSyncHost>(m_registry.get());

    // Seed both backends with the calendar the mapping references.
    m_src->createCalendar(QString::fromLatin1(kCollectionId),
                          QString::fromLatin1(kCalendarId),
                          QStringLiteral("Calendar 1"));
    m_dst->createCalendar(QString::fromLatin1(kCollectionId),
                          QString::fromLatin1(kCalendarId),
                          QStringLiteral("Calendar 1"));

    auto *hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal->setId(QString::fromLatin1(kCalendarId));
    m_host->stubCollection()->addCalendarWithId(QString::fromLatin1(kCalendarId), hostCal);

    const QString dbPath = m_tmpDir->filePath(QStringLiteral(".kalburator-sync.db"));
    m_baselines = std::make_unique<Kalburator::Storage::BaselineStore>(dbPath);

    m_engine = std::make_unique<SyncEngine>(m_registry.get(), m_host.get(), m_shape);
    m_engine->setBaselineStore(m_baselines.get());
    m_engine->setCollection(m_host->stubCollection());
    m_engine->setSyncMappings({ makeCalendarMapping() });
}

void TstEngineSingleMappingCancel::cleanup()
{
    m_engine.reset();
    m_baselines.reset();
    m_host.reset();
    m_dst.reset();
    m_src.reset();
    m_registry.reset();
    m_tmpDir.reset();
}

void TstEngineSingleMappingCancel::canonicalSingleMappingCancel_preservesNativeResult()
{
    // Seed the source so a non-cancelled run would write to the destination;
    // after cancel the destination MUST stay empty.
    m_src->addIncidence(QString::fromLatin1(kCalendarId),
                        makeEvent(QStringLiteral("evt-1"), QStringLiteral("Event One")));
    m_src->addIncidence(QString::fromLatin1(kCalendarId),
                        makeEvent(QStringLiteral("evt-2"), QStringLiteral("Event Two")));

    // Block the source fetch so the worker cannot race past the
    // cancellation pre-check before our cancel propagates — makes the
    // cancel-wins outcome deterministic (cf. tst_engine_cancellation C1).
    m_src->setFetchBlocking(true);

    // CANONICAL single-mapping entry: runSync(SyncRequest) with exactly one
    // mapping id (NOT the deleted runSyncFuture(mappingId) shim). After the
    // step-3 collapse this branch dispatches natively — no .then() wrap.
    SyncRequest req;
    req.mappingIds = { QString::fromLatin1(kMappingId) };
    QFuture<QList<SyncResult>> future = m_engine->runSync(req);

    future.cancel();

    // Spin the engine-thread event loop so QFutureWatcher::canceled →
    // onCancelObserved → queued observeCancel reaches the worker.
    // (waitForFinished does NOT spin the loop — CLAUDE.md.)
    QTest::qWait(50);
    m_src->releaseFetchBlocker();

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    QVERIFY(future.isCanceled());

    // No items reached the destination.
    QCOMPARE(m_dst->allUids(QString::fromLatin1(kCalendarId)).size(), 0);

    // The canonical single-mapping cancel contract, preserved NATIVELY by the
    // Plan 8 step 3 collapse: exactly one result, a one-element list whose
    // SyncResult.cancelled==true. NO resultCount()>0 guard is needed — that
    // was the pre-collapse .then()-path workaround this plan removed.
    // (Pre-collapse the run reached neither this nor a clean cancel — the
    //  cancel never propagated and items were written; see the top note.)
    QCOMPARE(future.resultCount(), 1);
    const QList<SyncResult> list = future.resultAt(0);
    QCOMPARE(list.size(), 1);
    QVERIFY(list.first().cancelled);
}

QTEST_MAIN(TstEngineSingleMappingCancel)
#include "tst_engine_single_mapping_cancel.moc"
