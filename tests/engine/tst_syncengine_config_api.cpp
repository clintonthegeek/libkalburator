// tst_syncengine_config_api.cpp
//
// WP-D5 (architectural-redress campaign) — SyncEngine config API unit tests.
//
// Covers the six config-management methods that have been open since the
// 2026-05-29 AUDIT finding (supplement S5, "dark since extraction"):
//
//   - loadSyncMappings: reads from StubSyncConfigStore via ISyncHost::configStore()
//   - setMappingEnabled / hasSyncMappings: per-mapping enable gate
//   - hasSyncWork: true iff mappings OR active controllers present
//   - setSkipUnchangedMappings / skipUnchangedMappings: flag round-trip
//   - registerActiveController / unregisterActiveController
//
// None of these tests run sync; they only exercise the configuration layer.
// The StubSyncHost + StubSyncConfigStore stubs replace the heavyweight
// integration fixture used by tst_syncengine_unification.

#include <QtTest/QtTest>
#include <memory>

#include "backendregistry.h"
#include "pluginmanager.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "syncengine.h"
#include "synctypes.h"

#include "../calendar/stubs/stubsynchost.h"

using namespace Kalburator;
using namespace Kalburator::Sync;
using namespace Kalburator::Engine;
using namespace Kalburator::Sync::Test;

namespace {

SyncMapping makeMapping(const QString &id,
                        const QString &sourceBackend = QStringLiteral("src"),
                        const QString &targetBackend = QStringLiteral("tgt"))
{
    SyncMapping m;
    m.id            = id;
    m.sourceBackend = sourceBackend;
    m.sourceCalendar= id + QStringLiteral("-cal");
    m.targetBackend = targetBackend;
    m.targetCalendar= id + QStringLiteral("-tcal");
    m.enabled       = true;
    return m;
}

} // namespace

class TstSyncEngineConfigApi : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    // loadSyncMappings
    void loadSyncMappings_populatesMappings();
    void loadSyncMappings_emptyStoreLeavesMappingsEmpty();
    void loadSyncMappings_clearsExistingMappings();

    // setMappingEnabled / hasSyncMappings
    void setMappingEnabled_disablesMapping();
    void setMappingEnabled_reenablesMapping();
    void setMappingEnabled_unknownIdIsNoop();

    // hasSyncWork
    void hasSyncWork_falseWithNoMappingsOrControllers();
    void hasSyncWork_trueWithMappings();

    // setSkipUnchangedMappings
    void setSkipUnchangedMappings_roundTrip();

    // registerActiveController / unregisterActiveController
    void registerActiveController_makesHasSyncWorkTrue();
    void unregisterActiveController_removesController();

    // setMaxConcurrentMappings / maxConcurrentMappings (parallel-sync Task 7)
    void testMaxConcurrentMappingsDefaultsToOne();
    void testMaxConcurrentMappingsClampsToAtLeastOne();
    void testMaxConcurrentMappingsRoundTrips();

private:
    std::unique_ptr<BackendRegistry>           m_registry;
    std::unique_ptr<Kalburator::Shape::ShapeRegistries> m_shape;
    std::unique_ptr<PluginManager>             m_pm;
    std::unique_ptr<StubSyncHost>              m_host;
    std::unique_ptr<SyncEngine>                m_engine;
};

void TstSyncEngineConfigApi::initTestCase()
{
    m_registry = std::make_unique<BackendRegistry>();
    m_shape    = std::make_unique<Kalburator::Shape::ShapeRegistries>();
    m_pm       = std::make_unique<PluginManager>(m_registry.get(), *m_shape);
    registerStockPlugins(*m_pm);
}

void TstSyncEngineConfigApi::init()
{
    m_host   = std::make_unique<StubSyncHost>(m_registry.get());
    m_engine = std::make_unique<SyncEngine>(m_registry.get(), m_host.get(), *m_shape);
}

void TstSyncEngineConfigApi::cleanup()
{
    m_engine.reset();
    m_host.reset();
}

// ---------------------------------------------------------------------------
// loadSyncMappings
// ---------------------------------------------------------------------------

void TstSyncEngineConfigApi::loadSyncMappings_populatesMappings()
{
    m_host->stubConfig()->setMappings({
        makeMapping(QStringLiteral("m1")),
        makeMapping(QStringLiteral("m2")),
    });

    // loadSyncMappings reads via m_controller->configStore()
    m_engine->loadSyncMappings(m_host->stubCollection());

    QCOMPARE(m_engine->syncMappings().size(), 2);
    QVERIFY(m_engine->hasSyncMappings());
}

void TstSyncEngineConfigApi::loadSyncMappings_emptyStoreLeavesMappingsEmpty()
{
    // No mappings in store
    m_engine->loadSyncMappings(m_host->stubCollection());

    QVERIFY(m_engine->syncMappings().isEmpty());
    QVERIFY(!m_engine->hasSyncMappings());
}

void TstSyncEngineConfigApi::loadSyncMappings_clearsExistingMappings()
{
    // Seed with two mappings
    m_host->stubConfig()->setMappings({
        makeMapping(QStringLiteral("m1")),
        makeMapping(QStringLiteral("m2")),
    });
    m_engine->loadSyncMappings(m_host->stubCollection());
    QCOMPARE(m_engine->syncMappings().size(), 2);

    // Replace store with one mapping and reload
    m_host->stubConfig()->setMappings({makeMapping(QStringLiteral("m3"))});
    m_engine->loadSyncMappings(m_host->stubCollection());

    QCOMPARE(m_engine->syncMappings().size(), 1);
    QCOMPARE(m_engine->syncMappings().first().id, QStringLiteral("m3"));
}

// ---------------------------------------------------------------------------
// setMappingEnabled
// ---------------------------------------------------------------------------

void TstSyncEngineConfigApi::setMappingEnabled_disablesMapping()
{
    m_host->stubConfig()->setMappings({makeMapping(QStringLiteral("m1"))});
    m_engine->loadSyncMappings(m_host->stubCollection());

    QVERIFY(m_engine->syncMappings().first().enabled);

    m_engine->setMappingEnabled(QStringLiteral("m1"), false);

    QVERIFY(!m_engine->syncMappings().first().enabled);
}

void TstSyncEngineConfigApi::setMappingEnabled_reenablesMapping()
{
    SyncMapping m = makeMapping(QStringLiteral("m1"));
    m.enabled = false;
    m_host->stubConfig()->setMappings({m});
    m_engine->loadSyncMappings(m_host->stubCollection());

    QVERIFY(!m_engine->syncMappings().first().enabled);

    m_engine->setMappingEnabled(QStringLiteral("m1"), true);

    QVERIFY(m_engine->syncMappings().first().enabled);
}

void TstSyncEngineConfigApi::setMappingEnabled_unknownIdIsNoop()
{
    m_host->stubConfig()->setMappings({makeMapping(QStringLiteral("m1"))});
    m_engine->loadSyncMappings(m_host->stubCollection());

    // Should not crash or modify existing mapping
    m_engine->setMappingEnabled(QStringLiteral("does-not-exist"), false);
    QVERIFY(m_engine->syncMappings().first().enabled);
}

// ---------------------------------------------------------------------------
// hasSyncWork
// ---------------------------------------------------------------------------

void TstSyncEngineConfigApi::hasSyncWork_falseWithNoMappingsOrControllers()
{
    QVERIFY(!m_engine->hasSyncWork());
}

void TstSyncEngineConfigApi::hasSyncWork_trueWithMappings()
{
    m_host->stubConfig()->setMappings({makeMapping(QStringLiteral("m1"))});
    m_engine->loadSyncMappings(m_host->stubCollection());

    QVERIFY(m_engine->hasSyncWork());
}

// ---------------------------------------------------------------------------
// setSkipUnchangedMappings
// ---------------------------------------------------------------------------

void TstSyncEngineConfigApi::setSkipUnchangedMappings_roundTrip()
{
    QVERIFY(!m_engine->skipUnchangedMappings());

    m_engine->setSkipUnchangedMappings(true);
    QVERIFY(m_engine->skipUnchangedMappings());

    m_engine->setSkipUnchangedMappings(false);
    QVERIFY(!m_engine->skipUnchangedMappings());
}

// ---------------------------------------------------------------------------
// registerActiveController / unregisterActiveController
// ---------------------------------------------------------------------------

void TstSyncEngineConfigApi::registerActiveController_makesHasSyncWorkTrue()
{
    QVERIFY(!m_engine->hasSyncWork());

    // DecSyncActiveController is opaque from this layer; pass nullptr — the
    // engine stores the pointer in a QHash without dereferencing it at
    // registration time.
    m_engine->registerActiveController(QStringLiteral("cal-1"), nullptr);

    QVERIFY(m_engine->hasSyncWork());
}

void TstSyncEngineConfigApi::unregisterActiveController_removesController()
{
    m_engine->registerActiveController(QStringLiteral("cal-1"), nullptr);
    QVERIFY(m_engine->hasSyncWork());

    m_engine->unregisterActiveController(QStringLiteral("cal-1"));
    QVERIFY(!m_engine->hasSyncWork());
}

// ---------------------------------------------------------------------------
// setMaxConcurrentMappings / maxConcurrentMappings (parallel-sync Task 7)
// ---------------------------------------------------------------------------

void TstSyncEngineConfigApi::testMaxConcurrentMappingsDefaultsToOne()
{
    // The whole consumer-safety story rests on this. WildPalms and every
    // existing call site are unchanged precisely because they never touch
    // the setter.
    QCOMPARE(m_engine->maxConcurrentMappings(), 1);
}

void TstSyncEngineConfigApi::testMaxConcurrentMappingsClampsToAtLeastOne()
{
    m_engine->setMaxConcurrentMappings(0);
    QCOMPARE(m_engine->maxConcurrentMappings(), 1);
    m_engine->setMaxConcurrentMappings(-5);
    QCOMPARE(m_engine->maxConcurrentMappings(), 1);
}

void TstSyncEngineConfigApi::testMaxConcurrentMappingsRoundTrips()
{
    m_engine->setMaxConcurrentMappings(4);
    QCOMPARE(m_engine->maxConcurrentMappings(), 4);
}

QTEST_GUILESS_MAIN(TstSyncEngineConfigApi)
#include "tst_syncengine_config_api.moc"
