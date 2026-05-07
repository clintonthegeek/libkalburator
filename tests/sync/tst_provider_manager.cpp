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
#include "iblobbackend.h"
#include "backendregistry.h"
#include "syncbackend.h"
#include "backendconfiguration.h"
#include "collectioninfo.h"
#include "shape.h"

using namespace Kalburator::Sync;

namespace {

class FakeBackend : public SyncBackend {
    Q_OBJECT
public:
    FakeBackend() = default;

    QString backendType() const override { return QStringLiteral("fake"); }
    QList<Kalburator::Shape::Shape> nativeShapes() const override { return {}; }
    QString resourceId() const override { return QStringLiteral("fake"); }

    void loadCalendars(const QString &) override {}
    void storeCalendars(const QString &,
                        const QList<KCalendarCore::MemoryCalendar*> &) override {}
    void startSync(const QString &,
                   KCalendarCore::MemoryCalendar*,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QMap<QString, QString> &,
                   const Kalburator::Sync::TranscodingPlan &) override {}
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

    std::unique_ptr<IBlobBackend>
    createBackend(const QString & /*collectionId*/) override {
        return std::make_unique<FakeBackend>();
    }

    void setFailConnect(bool fail) { m_failConnect = fail; }
    void seedCollections(QList<CollectionInfo> cols) { m_collectionsSeed = std::move(cols); }
    bool wasLoaded() const { return m_loaded; }
    QVariantMap loadedParams() const { return m_loadedParams; }
    void setDisplayName(const QString &n) { m_displayName = n; }
    void setConnectionParams(const QVariantMap &p) { m_loadedParams = p; }

private:
    QString m_id;
    QString m_displayName;
    bool m_connected = false;
    bool m_loaded = false;
    bool m_failConnect = false;
    QVariantMap m_loadedParams;
    QList<CollectionInfo> m_collections;
    QList<CollectionInfo> m_collectionsSeed;
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
    ProviderManager mgr(&reg);
    mgr.setFactoryForTest([](const QString &kind) -> std::unique_ptr<IProvider> {
        if (kind == QLatin1String("fake")) {
            return std::make_unique<FakeProvider>(QString());
        }
        return nullptr;
    });

    // Inject id by constructing in factory by reading sub-group name? No —
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
    ProviderManager mgr(&reg);
    mgr.setFactoryForTest([](const QString &kind) -> std::unique_ptr<IProvider> {
        if (kind == QLatin1String("fake")) {
            return std::make_unique<FakeProvider>(QStringLiteral("ignored"));
        }
        return nullptr;
    });

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
        ProviderManager mgr(&reg);
        mgr.setFactoryForTest([](const QString &kind) -> std::unique_ptr<IProvider> {
            if (kind == QLatin1String("fake")) {
                return std::make_unique<FakeProvider>(QStringLiteral("p-rt"));
            }
            return nullptr;
        });
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

QTEST_GUILESS_MAIN(TstProviderManager)
#include "tst_provider_manager.moc"
