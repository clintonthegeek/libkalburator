#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QObject>
#include <QFutureWatcher>
#include <QFutureInterface>

#include <KConfig>
#include <KConfigGroup>

#include "providermanager.h"
#include "iprovider.h"
#include "backendregistry.h"
#include "backendcontribution.h"
#include "syncbackend.h"
#include "backendconfiguration.h"
#include "collectioninfo.h"
#include "shape.h"
#include "caldavbackendcontribution.h"
#include "carddavbackendcontribution.h"

using namespace Kalburator::Sync;

namespace {

class FakeBackend : public SyncBackend {
    Q_OBJECT
public:
    FakeBackend() = default;

    QString backendType() const override { return QStringLiteral("fake"); }
    QList<Kalburator::Shape::Shape> nativeShapes() const override { return {}; }
    // resourceId() intentionally NOT overridden: SyncBackendBase's default
    // returns m_resourceId once ProviderManager::registerProviderBackends
    // calls setResourceId() on registration — testRegistersOneBackendPerSpec
    // asserts on that.

    void loadCalendars(const QString &) override {}
    void storeCalendars(const QString &,
                        const QList<KCalendarCore::MemoryCalendar*> &) override {}
    void startSync(const QString &,
                   KCalendarCore::MemoryCalendar*,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QMap<QString, QString> &) override {}
    void removeItem(const QString &, const QString &) override {}
};

class FakeProvider : public IProvider {
    Q_OBJECT
public:
    explicit FakeProvider(const QString &id, QObject *parent = nullptr)
        : IProvider(parent), m_id(id) {}

    QString id() const override { return m_id; }
    QString kind() const override { return QStringLiteral("fake"); }
    QString displayName() const override { return m_displayName; }

    void load(const BackendConfiguration &cfg) override {
        m_displayName = cfg.displayName;
        m_loadedParams = cfg.connectionParams;
        m_loaded = true;
    }

    BackendConfiguration save() const override {
        BackendConfiguration cfg;
        cfg.id = m_id;
        cfg.displayName = m_displayName;
        cfg.connectionParams = m_loadedParams;
        return cfg;
    }

    QWidget *createConfigWidget(QWidget *) override { return nullptr; }

    QFuture<bool> connect() override {
        if (m_failConnect) {
            QFutureInterface<bool> fi;
            fi.reportStarted();
            fi.reportResult(false);
            fi.reportFinished();
            emit error(QStringLiteral("fake: failure injected"));
            return fi.future();
        }
        // Mirror the real providers' v0.61 idempotency fast-path: an
        // already-connected provider returns a finished future WITHOUT
        // re-emitting connectionStateChanged(true).
        if (m_connected) {
            QFutureInterface<bool> fi;
            fi.reportStarted();
            fi.reportResult(true);
            fi.reportFinished();
            return fi.future();
        }
        m_connected = true;
        m_collections = m_collectionsSeed;
        emit collectionsChanged();
        emit connectionStateChanged(true);
        QFutureInterface<bool> fi;
        fi.reportStarted();
        fi.reportResult(true);
        fi.reportFinished();
        return fi.future();
    }

    void disconnect() override {
        if (!m_connected) return;
        m_connected = false;
        m_collections.clear();
        emit connectionStateChanged(false);
    }

    bool isConnected() const override { return m_connected; }
    QList<CollectionInfo> collections() const override { return m_collections; }

    // Default (Phase 1 real-provider shape): one spec per collection,
    // domainId == collection id — mirrors CalDavProvider::createBackends()
    // etc. Tests that want to exercise domain GROUPING (multiple
    // collections hosted by a single spec, the shape Task 2 introduces for
    // real DAV providers) call seedDomainGroups() instead, which takes
    // priority when non-empty.
    std::vector<ProviderBackendSpec> createBackends() override {
        std::vector<ProviderBackendSpec> out;
        if (!m_domainGroups.isEmpty()) {
            for (const auto &group : m_domainGroups) {
                ProviderBackendSpec spec;
                spec.domainId = group.domainId;
                spec.backend = std::make_unique<FakeBackend>();
                spec.collections = group.collections;
                out.push_back(std::move(spec));
            }
            return out;
        }
        for (const auto &col : std::as_const(m_collections)) {
            ProviderBackendSpec spec;
            spec.domainId = col.id;
            spec.backend = std::make_unique<FakeBackend>();
            spec.collections = { col };
            out.push_back(std::move(spec));
        }
        return out;
    }

    void setFailConnect(bool fail) { m_failConnect = fail; }
    void seedCollections(QList<CollectionInfo> cols) { m_collectionsSeed = std::move(cols); }
    bool wasLoaded() const { return m_loaded; }
    QVariantMap loadedParams() const { return m_loadedParams; }
    void setDisplayName(const QString &n) { m_displayName = n; }
    void setConnectionParams(const QVariantMap &p) { m_loadedParams = p; }
    void setActiveCollections(QList<CollectionInfo> cols) {
        m_collections = std::move(cols);
        emit collectionsChanged();
    }

    // testRegistersOneBackendPerSpec / testUnregisterRemovesAllProviderSpecs:
    // seed explicit domain->collections groupings so createBackends() emits
    // multiple collections under a single spec, exercising the "provider
    // chooses granularity" contract iprovider.h documents.
    struct DomainGroup {
        QString domainId;
        QList<CollectionInfo> collections;
    };
    void seedDomainGroups(QList<DomainGroup> groups) { m_domainGroups = std::move(groups); }

private:
    QString m_id;
    QString m_displayName;
    bool m_connected = false;
    bool m_loaded = false;
    bool m_failConnect = false;
    QVariantMap m_loadedParams;
    QList<CollectionInfo> m_collections;
    QList<CollectionInfo> m_collectionsSeed;
    QList<DomainGroup> m_domainGroups;
};

// Minimal BackendContribution that produces FakeProvider instances.
// Used in tests that previously called setFactoryForTest().
class FakeBC : public BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("fake"); }
    QString displayName() const override { return QStringLiteral("Fake"); }
    QList<Kalburator::Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<IProvider> createProvider(QObject * = nullptr) const override {
        return std::make_unique<FakeProvider>(QString());
    }
};

// Wait for QFuture<void> to finish, spinning the event loop.
bool waitForFuture(QFuture<void> f, int timeoutMs = 5000)
{
    QFutureWatcher<void> w;
    QSignalSpy doneSpy(&w, &QFutureWatcher<void>::finished);
    w.setFuture(f);
    if (f.isFinished()) return true;
    return doneSpy.wait(timeoutMs);
}

} // anon namespace

class TstProviderManager : public QObject {
    Q_OBJECT
private slots:
    void load_constructs_provider_via_factory();
    void load_skips_unknown_kind();
    void connectAll_invokes_each_providers_connect();
    void connectAll_registers_backends_with_registry();
    void disconnectAll_unregisters_backends();
    void removeProvider_disconnects_first();
    void saveToProfile_round_trips_with_loadFromProfile();
    void provider_failure_does_not_block_others();
    void connectAll_skips_already_connected_providers();
    void onProviderCollectionsChanged_reregisters_backends();
    void removeProvider_with_unknown_id_is_noop();
    void providersChanged_emitted_on_add_and_remove();
    void connectAll_with_zero_providers_returns_finished_future();
    void default_factory_creates_caldav_provider();
    void default_factory_creates_carddav_provider();
    void providerState_transitionsThroughLifecycle();
    void addProvider_registersBackendsForPreConnectedProvider();
    void testRegistersOneBackendPerSpec();
    void testUnregisterRemovesAllProviderSpecs();
};

void TstProviderManager::load_constructs_provider_via_factory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    KConfig cfg(dir.path() + QStringLiteral("/test.conf"), KConfig::SimpleConfig);
    KConfigGroup providers(&cfg, QStringLiteral("Providers"));
    {
        KConfigGroup a = providers.group(QStringLiteral("p-a"));
        a.writeEntry("kind", "fake");
        a.writeEntry("displayName", "Account A");
        a.writeEntry("url", "https://a.example/");
    }
    {
        KConfigGroup b = providers.group(QStringLiteral("p-b"));
        b.writeEntry("kind", "fake");
        b.writeEntry("displayName", "Account B");
        b.writeEntry("url", "https://b.example/");
    }
    providers.sync();

    BackendRegistry reg;
    reg.registerContribution(std::make_shared<FakeBC>());
    ProviderManager mgr(&reg);

    // ProviderManager assigns id via cfg.id during load(). Verify by
    // checking displayName instead, since FakeProvider has no id setter.
    mgr.loadFromProfile(providers);

    const auto list = mgr.providers();
    QCOMPARE(list.size(), 2);
    for (auto *p : list) {
        auto *fp = static_cast<FakeProvider*>(p);
        QVERIFY(fp->wasLoaded());
        QVERIFY(fp->displayName() == QLatin1String("Account A")
                || fp->displayName() == QLatin1String("Account B"));
        QVERIFY(fp->loadedParams().contains(QStringLiteral("url")));
    }
}

void TstProviderManager::load_skips_unknown_kind()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    KConfig cfg(dir.path() + QStringLiteral("/test.conf"), KConfig::SimpleConfig);
    KConfigGroup providers(&cfg, QStringLiteral("Providers"));
    KConfigGroup p = providers.group(QStringLiteral("p-unknown"));
    p.writeEntry("kind", "xyz");
    providers.sync();

    BackendRegistry reg;
    reg.registerContribution(std::make_shared<FakeBC>());
    ProviderManager mgr(&reg);

    mgr.loadFromProfile(providers);
    QVERIFY(mgr.providers().isEmpty());
}

void TstProviderManager::connectAll_invokes_each_providers_connect()
{
    BackendRegistry reg;
    ProviderManager mgr(&reg);

    auto a = std::make_unique<FakeProvider>(QStringLiteral("a"));
    auto b = std::make_unique<FakeProvider>(QStringLiteral("b"));
    auto *aPtr = a.get();
    auto *bPtr = b.get();
    mgr.addProvider(std::move(a));
    mgr.addProvider(std::move(b));

    QFuture<void> f = mgr.connectAll();
    QVERIFY(waitForFuture(f));
    QVERIFY(aPtr->isConnected());
    QVERIFY(bPtr->isConnected());
}

void TstProviderManager::connectAll_registers_backends_with_registry()
{
    BackendRegistry reg;
    ProviderManager mgr(&reg);

    auto p = std::make_unique<FakeProvider>(QStringLiteral("p1"));
    CollectionInfo a; a.id = QStringLiteral("cal-a"); a.name = QStringLiteral("A");
    CollectionInfo b; b.id = QStringLiteral("cal-b"); b.name = QStringLiteral("B");
    p->seedCollections({a, b});
    mgr.addProvider(std::move(p));

    QFuture<void> f = mgr.connectAll();
    QVERIFY(waitForFuture(f));

    const QStringList ids = reg.registeredInstanceIds();
    QVERIFY(ids.contains(QStringLiteral("p1:cal-a")));
    QVERIFY(ids.contains(QStringLiteral("p1:cal-b")));
}

void TstProviderManager::disconnectAll_unregisters_backends()
{
    BackendRegistry reg;
    ProviderManager mgr(&reg);

    auto p = std::make_unique<FakeProvider>(QStringLiteral("p1"));
    CollectionInfo c; c.id = QStringLiteral("cal-x");
    p->seedCollections({c});
    mgr.addProvider(std::move(p));

    QVERIFY(waitForFuture(mgr.connectAll()));
    QVERIFY(reg.registeredInstanceIds().contains(QStringLiteral("p1:cal-x")));

    mgr.disconnectAll();
    QVERIFY(!reg.registeredInstanceIds().contains(QStringLiteral("p1:cal-x")));
}

void TstProviderManager::removeProvider_disconnects_first()
{
    BackendRegistry reg;
    ProviderManager mgr(&reg);

    auto p = std::make_unique<FakeProvider>(QStringLiteral("p-rm"));
    CollectionInfo c; c.id = QStringLiteral("cal-1");
    p->seedCollections({c});
    mgr.addProvider(std::move(p));

    QVERIFY(waitForFuture(mgr.connectAll()));
    QVERIFY(reg.registeredInstanceIds().contains(QStringLiteral("p-rm:cal-1")));

    mgr.removeProvider(QStringLiteral("p-rm"));
    QVERIFY(!reg.registeredInstanceIds().contains(QStringLiteral("p-rm:cal-1")));
    QCOMPARE(mgr.providerById(QStringLiteral("p-rm")), nullptr);
}

void TstProviderManager::saveToProfile_round_trips_with_loadFromProfile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/round.conf");

    // ── Manager A: populate + save ──────────────────────────────────
    {
        KConfig cfg(path, KConfig::SimpleConfig);
        KConfigGroup providers(&cfg, QStringLiteral("Providers"));

        BackendRegistry reg;
        ProviderManager mgr(&reg);

        auto p = std::make_unique<FakeProvider>(QStringLiteral("p-rt"));
        p->setDisplayName(QStringLiteral("Round-Trip Account"));
        QVariantMap params;
        params.insert(QStringLiteral("url"), QStringLiteral("https://rt.example/"));
        params.insert(QStringLiteral("username"), QStringLiteral("alice"));
        p->setConnectionParams(params);
        mgr.addProvider(std::move(p));

        mgr.saveToProfile(providers);
        cfg.sync();
    }

    // ── Manager B: load + verify ───────────────────────────────────
    {
        KConfig cfg(path, KConfig::SimpleConfig);
        KConfigGroup providers(&cfg, QStringLiteral("Providers"));

        BackendRegistry reg;
        reg.registerContribution(std::make_shared<FakeBC>());
        ProviderManager mgr(&reg);
        mgr.loadFromProfile(providers);

        QCOMPARE(mgr.providers().size(), 1);
        auto *fp = static_cast<FakeProvider*>(mgr.providers().first());
        QCOMPARE(fp->displayName(), QStringLiteral("Round-Trip Account"));
        QCOMPARE(fp->loadedParams().value(QStringLiteral("url")).toString(),
                 QStringLiteral("https://rt.example/"));
        QCOMPARE(fp->loadedParams().value(QStringLiteral("username")).toString(),
                 QStringLiteral("alice"));
    }
}

void TstProviderManager::provider_failure_does_not_block_others()
{
    BackendRegistry reg;
    ProviderManager mgr(&reg);

    auto bad = std::make_unique<FakeProvider>(QStringLiteral("bad"));
    bad->setFailConnect(true);
    auto *badPtr = bad.get();
    QSignalSpy errSpy(badPtr, &IProvider::error);

    auto good = std::make_unique<FakeProvider>(QStringLiteral("good"));
    auto *goodPtr = good.get();

    mgr.addProvider(std::move(bad));
    mgr.addProvider(std::move(good));

    QFuture<void> f = mgr.connectAll();
    QVERIFY(waitForFuture(f));

    QVERIFY(!badPtr->isConnected());
    QVERIFY(goodPtr->isConnected());
    QCOMPARE(errSpy.count(), 1);
}

void TstProviderManager::connectAll_skips_already_connected_providers()
{
    BackendRegistry reg;
    ProviderManager mgr(&reg);

    auto p = std::make_unique<FakeProvider>(QStringLiteral("p-skip"));
    auto *pPtr = p.get();
    mgr.addProvider(std::move(p));

    QVERIFY(waitForFuture(mgr.connectAll()));
    QVERIFY(pPtr->isConnected());

    QSignalSpy stateSpy(pPtr, &IProvider::connectionStateChanged);

    QVERIFY(waitForFuture(mgr.connectAll()));
    QVERIFY(pPtr->isConnected());
    QCOMPARE(stateSpy.count(), 0);
}

void TstProviderManager::onProviderCollectionsChanged_reregisters_backends()
{
    BackendRegistry reg;
    ProviderManager mgr(&reg);

    auto p = std::make_unique<FakeProvider>(QStringLiteral("p-reregister"));
    CollectionInfo c1;
    c1.id = QStringLiteral("cal-1");
    p->seedCollections({c1});
    auto *pPtr = p.get();
    mgr.addProvider(std::move(p));

    QVERIFY(waitForFuture(mgr.connectAll()));
    QVERIFY(reg.registeredInstanceIds().contains(
        QStringLiteral("p-reregister:cal-1")));

    CollectionInfo c2;
    c2.id = QStringLiteral("cal-2");
    pPtr->setActiveCollections({c2});

    const QStringList ids = reg.registeredInstanceIds();
    QVERIFY(!ids.contains(QStringLiteral("p-reregister:cal-1")));
    QVERIFY(ids.contains(QStringLiteral("p-reregister:cal-2")));
}

void TstProviderManager::removeProvider_with_unknown_id_is_noop()
{
    BackendRegistry reg;
    ProviderManager mgr(&reg);

    auto p = std::make_unique<FakeProvider>(QStringLiteral("real-p"));
    mgr.addProvider(std::move(p));

    mgr.removeProvider(QStringLiteral("does-not-exist"));

    QCOMPARE(mgr.providers().size(), 1);
    QVERIFY(mgr.providerById(QStringLiteral("real-p")) != nullptr);
}

void TstProviderManager::providersChanged_emitted_on_add_and_remove()
{
    BackendRegistry reg;
    ProviderManager mgr(&reg);

    QSignalSpy spy(&mgr, &ProviderManager::providersChanged);

    mgr.addProvider(std::make_unique<FakeProvider>(QStringLiteral("ev-p")));
    QCOMPARE(spy.count(), 1);

    mgr.removeProvider(QStringLiteral("ev-p"));
    QCOMPARE(spy.count(), 2);
}

void TstProviderManager::connectAll_with_zero_providers_returns_finished_future()
{
    BackendRegistry reg;
    ProviderManager mgr(&reg);

    QFuture<void> f = mgr.connectAll();
    QVERIFY(waitForFuture(f, 1000));
}

void TstProviderManager::default_factory_creates_caldav_provider()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    KConfig cfg(dir.path() + QStringLiteral("/test_caldav.conf"), KConfig::SimpleConfig);
    KConfigGroup providers(&cfg, QStringLiteral("Providers"));
    {
        KConfigGroup p = providers.group(QStringLiteral("test-caldav"));
        p.writeEntry("kind", "caldav");
        p.writeEntry("displayName", "Test CalDAV");
    }
    providers.sync();

    BackendRegistry reg;
    reg.registerContribution(std::make_shared<CalDavBackendContribution>());
    ProviderManager mgr(&reg);

    // Default factory should handle caldav (plugin registers to local registry)
    mgr.loadFromProfile(providers);
    QCOMPARE(mgr.providers().size(), 1);
    QCOMPARE(mgr.providers().first()->kind(), QStringLiteral("caldav"));
}

void TstProviderManager::default_factory_creates_carddav_provider()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    KConfig cfg(dir.path() + QStringLiteral("/test_carddav.conf"), KConfig::SimpleConfig);
    KConfigGroup providers(&cfg, QStringLiteral("Providers"));
    {
        KConfigGroup p = providers.group(QStringLiteral("test-carddav"));
        p.writeEntry("kind", "carddav");
        p.writeEntry("displayName", "Test CardDAV");
    }
    providers.sync();

    BackendRegistry reg;
    reg.registerContribution(std::make_shared<CardDavBackendContribution>());
    ProviderManager mgr(&reg);

    // Phase Ib: Default factory should now handle carddav (plugin registers to local registry)
    mgr.loadFromProfile(providers);
    QCOMPARE(mgr.providers().size(), 1);
    QCOMPARE(mgr.providers().first()->kind(), QStringLiteral("carddav"));
}

void TstProviderManager::providerState_transitionsThroughLifecycle()
{
    BackendRegistry reg;
    ProviderManager mgr(&reg);

    auto p = std::make_unique<FakeProvider>(QStringLiteral("p1"));
    FakeProvider *raw = p.get();
    mgr.addProvider(std::move(p));

    QSignalSpy spy(&mgr, &ProviderManager::providerStateChanged);

    // Initial: Disconnected (querying state must not emit).
    QCOMPARE(mgr.providerState(QStringLiteral("p1")),
             ProviderConnectionState::Disconnected);
    QCOMPARE(spy.count(), 0);

    // Simulate connection success.
    QVERIFY(waitForFuture(raw->connect()));
    QCOMPARE(mgr.providerState(QStringLiteral("p1")),
             ProviderConnectionState::Connected);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("p1"));
    QCOMPARE(spy.first().at(1).value<ProviderConnectionState>(),
             ProviderConnectionState::Connected);

    // Simulate disconnect.
    raw->disconnect();
    QCOMPARE(mgr.providerState(QStringLiteral("p1")),
             ProviderConnectionState::Disconnected);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(1).value<ProviderConnectionState>(),
             ProviderConnectionState::Disconnected);

    // Unknown provider: queries return Disconnected (safe default).
    QCOMPARE(mgr.providerState(QStringLiteral("does-not-exist")),
             ProviderConnectionState::Disconnected);
}

void TstProviderManager::addProvider_registersBackendsForPreConnectedProvider()
{
    // The Add Account flow (ProviderConfigDialog) connects the provider to
    // discover its calendars BEFORE the manager ever sees it — the
    // connectionStateChanged(true) emission happens with no subscribers.
    // A subsequent connectAll() hits the provider's idempotency fast-path
    // (finished future, no re-emission), so addProvider() itself must
    // register the backends and seed the Connected state.
    BackendRegistry reg;
    ProviderManager mgr(&reg);

    auto p = std::make_unique<FakeProvider>(QStringLiteral("p1"));
    CollectionInfo c; c.id = QStringLiteral("cal-pre"); c.name = QStringLiteral("Pre");
    p->seedCollections({c});

    // Dialog-style pre-connect, unmanaged.
    QVERIFY(waitForFuture(p->connect()
        .then([](bool) {}))); // adapt QFuture<bool> -> QFuture<void> for the helper
    QVERIFY(p->isConnected());

    mgr.addProvider(std::move(p));

    // Backends must be registered immediately — connectAll() won't do it.
    QVERIFY(reg.registeredInstanceIds().contains(QStringLiteral("p1:cal-pre")));
    QCOMPARE(mgr.providerState(QStringLiteral("p1")),
             ProviderConnectionState::Connected);

    // And connectAll() afterwards must remain harmless.
    QVERIFY(waitForFuture(mgr.connectAll()));
    QVERIFY(reg.registeredInstanceIds().contains(QStringLiteral("p1:cal-pre")));
}

void TstProviderManager::testRegistersOneBackendPerSpec()
{
    // Mock provider returns TWO specs: {"cal", backendA, 3 collections},
    // {"contacts", backendB, 2 collections}.
    BackendRegistry reg;
    ProviderManager mgr(&reg);

    auto p = std::make_unique<FakeProvider>(QStringLiteral("p-spec"));
    CollectionInfo c1; c1.id = QStringLiteral("cal-1");
    CollectionInfo c2; c2.id = QStringLiteral("cal-2");
    CollectionInfo c3; c3.id = QStringLiteral("cal-3");
    CollectionInfo b1; b1.id = QStringLiteral("book-1");
    CollectionInfo b2; b2.id = QStringLiteral("book-2");
    p->seedDomainGroups(QList<FakeProvider::DomainGroup>{
        { QStringLiteral("cal"), { c1, c2, c3 } },
        { QStringLiteral("contacts"), { b1, b2 } },
    });
    mgr.addProvider(std::move(p));

    QVERIFY(waitForFuture(mgr.connectAll()));

    // Assert: registry has exactly "<id>:cal" and "<id>:contacts".
    const QStringList ids = reg.registeredInstanceIds();
    QCOMPARE(ids.size(), 2);
    QVERIFY(ids.contains(QStringLiteral("p-spec:cal")));
    QVERIFY(ids.contains(QStringLiteral("p-spec:contacts")));

    // backendIdsForProvider(id) returns both.
    const QStringList mine = mgr.backendIdsForProvider(QStringLiteral("p-spec"));
    QCOMPARE(mine.size(), 2);
    QVERIFY(mine.contains(QStringLiteral("p-spec:cal")));
    QVERIFY(mine.contains(QStringLiteral("p-spec:contacts")));

    // Each backend's resourceId() == its registry id.
    auto *calBackend = reg.backendInstance(QStringLiteral("p-spec:cal"));
    QVERIFY(calBackend);
    QCOMPARE(calBackend->resourceId(), QStringLiteral("p-spec:cal"));

    auto *contactsBackend = reg.backendInstance(QStringLiteral("p-spec:contacts"));
    QVERIFY(contactsBackend);
    QCOMPARE(contactsBackend->resourceId(), QStringLiteral("p-spec:contacts"));
}

void TstProviderManager::testUnregisterRemovesAllProviderSpecs()
{
    BackendRegistry reg;
    ProviderManager mgr(&reg);

    auto p = std::make_unique<FakeProvider>(QStringLiteral("p-spec2"));
    CollectionInfo c1; c1.id = QStringLiteral("cal-1");
    CollectionInfo b1; b1.id = QStringLiteral("book-1");
    p->seedDomainGroups(QList<FakeProvider::DomainGroup>{
        { QStringLiteral("cal"), { c1 } },
        { QStringLiteral("contacts"), { b1 } },
    });
    auto *pPtr = p.get();
    mgr.addProvider(std::move(p));

    QVERIFY(waitForFuture(mgr.connectAll()));
    QCOMPARE(mgr.backendIdsForProvider(QStringLiteral("p-spec2")).size(), 2);

    // After unregisterProviderBackends (triggered here via disconnect()):
    // both ids gone from registry and from backendIdsForProvider().
    pPtr->disconnect();

    const QStringList idsAfter = reg.registeredInstanceIds();
    QVERIFY(!idsAfter.contains(QStringLiteral("p-spec2:cal")));
    QVERIFY(!idsAfter.contains(QStringLiteral("p-spec2:contacts")));
    QVERIFY(mgr.backendIdsForProvider(QStringLiteral("p-spec2")).isEmpty());
}

QTEST_GUILESS_MAIN(TstProviderManager)
#include "tst_provider_manager.moc"
