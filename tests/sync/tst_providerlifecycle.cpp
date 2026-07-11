// Phase 5 step 1 — ProviderLifecycle canary tests.
// Verifies construction, provisionProvider(), updateProvider(), and
// the backendsReady() + providerProvisioned() signal emissions.

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QObject>
#include <QFutureInterface>

#include "providerlifecycle.h"
#include "providermanager.h"
#include "backendregistry.h"
#include "iprovider.h"
#include "iblobbackend.h"
#include "backendconfiguration.h"
#include "collectioninfo.h"
#include "syncbackend.h"
#include "shape.h"

using namespace Kalburator::Sync;

namespace {

// Minimal fake backend required by FakeProvider::createBackend.
class FakeBackend : public SyncBackend {
    Q_OBJECT
public:
    FakeBackend() = default;
    QString backendType() const override { return QStringLiteral("fake"); }
    QList<Kalburator::Shape::Shape> nativeShapes() const override { return {}; }
    QString resourceId() const override { return QStringLiteral("fake"); }
    void loadCalendars(const QString &) override {}
    void storeCalendars(const QString &,
                        const QList<KCalendarCore::MemoryCalendar *> &) override {}
    void startSync(const QString &,
                   KCalendarCore::MemoryCalendar *,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QMap<QString, QString> &) override {}
    void removeItem(const QString &, const QString &) override {}
};

// Minimal fake provider that succeeds connect() synchronously.
class FakeProvider : public IProvider {
    Q_OBJECT
public:
    explicit FakeProvider(const QString &id, QObject *parent = nullptr)
        : IProvider(parent), m_id(id) {}

    QString id()          const override { return m_id; }
    QString kind()        const override { return QStringLiteral("fake"); }
    QString displayName() const override { return m_displayName; }

    void load(const BackendConfiguration &cfg) override {
        m_displayName   = cfg.displayName;
        m_loadedParams  = cfg.connectionParams;
    }

    BackendConfiguration save() const override {
        BackendConfiguration cfg;
        cfg.id              = m_id;
        cfg.displayName     = m_displayName;
        cfg.connectionParams = m_loadedParams;
        return cfg;
    }

    QWidget *createConfigWidget(QWidget *) override { return nullptr; }

    QFuture<bool> connect() override {
        m_connected = true;
        emit connectionStateChanged(true);
        QFutureInterface<bool> fi;
        fi.reportStarted();
        fi.reportResult(true);
        fi.reportFinished();
        return fi.future();
    }

    void disconnect() override {
        m_connected = false;
        emit connectionStateChanged(false);
    }

    bool isConnected() const override { return m_connected; }
    QList<CollectionInfo> collections() const override { return {}; }

    std::unique_ptr<IBlobBackend> createBackend(const QString &) override {
        return std::make_unique<FakeBackend>();
    }

    // PHASE1-TASK1.1 — v2 contract stub. Empty by design — Phase 2
    // will fill this in.
    QList<ProviderBackendSpec> createBackends(const QString &) const override {
        return {};
    }

    void setDisplayName(const QString &n) { m_displayName = n; }

private:
    QString      m_id;
    QString      m_displayName;
    bool         m_connected = false;
    QVariantMap  m_loadedParams;
};

} // anonymous namespace

class TestProviderLifecycle : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ── Construction ──────────────────────────────────────────────────────────

    void constructionWorks()
    {
        BackendRegistry  registry;
        ProviderManager  manager(&registry);
        ProviderLifecycle lc(&registry, &manager);

        QCOMPARE(lc.providerManager(), &manager);
        QCOMPARE(lc.backendRegistry(), &registry);
        QVERIFY(lc.profilePath().isEmpty());
    }

    // ── setProfilePath ────────────────────────────────────────────────────────

    void setProfilePath_setsPathOnFirstCall()
    {
        BackendRegistry  registry;
        ProviderManager  manager(&registry);
        ProviderLifecycle lc(&registry, &manager);

        lc.setProfilePath(QStringLiteral("/tmp/foo.providers"));
        QCOMPARE(lc.profilePath(), QStringLiteral("/tmp/foo.providers"));
    }

    void setProfilePath_isIdempotent()
    {
        BackendRegistry  registry;
        ProviderManager  manager(&registry);
        ProviderLifecycle lc(&registry, &manager);

        lc.setProfilePath(QStringLiteral("/tmp/first.providers"));
        lc.setProfilePath(QStringLiteral("/tmp/second.providers")); // must be ignored
        QCOMPARE(lc.profilePath(), QStringLiteral("/tmp/first.providers"));
    }

    // ── provisionProvider ─────────────────────────────────────────────────────

    void provisionProvider_nullProviderReturnsEmpty()
    {
        BackendRegistry  registry;
        ProviderManager  manager(&registry);
        ProviderLifecycle lc(&registry, &manager);

        const QString uuid = lc.provisionProvider(nullptr);
        QVERIFY(uuid.isEmpty());
    }

    void provisionProvider_returnsProviderUuid()
    {
        BackendRegistry  registry;
        ProviderManager  manager(&registry);
        ProviderLifecycle lc(&registry, &manager);

        const QString uuid = lc.provisionProvider(
            std::make_unique<FakeProvider>(QStringLiteral("p-1")));
        QCOMPARE(uuid, QStringLiteral("p-1"));
    }

    void provisionProvider_emitsProviderProvisionedSignal()
    {
        BackendRegistry   registry;
        ProviderManager   manager(&registry);
        ProviderLifecycle lc(&registry, &manager);

        QSignalSpy spy(&lc, &ProviderLifecycle::providerProvisioned);
        lc.provisionProvider(
            std::make_unique<FakeProvider>(QStringLiteral("p-sig")));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("p-sig"));
    }

    void provisionProvider_backendsReadyEmittedAfterConnectAll()
    {
        BackendRegistry   registry;
        ProviderManager   manager(&registry);
        ProviderLifecycle lc(&registry, &manager);

        QSignalSpy ready(&lc, &ProviderLifecycle::backendsReady);

        lc.provisionProvider(
            std::make_unique<FakeProvider>(QStringLiteral("p-ready")));

        // backendsReady is emitted from a QFuture::then(context, ...) continuation,
        // which is delivered via this thread's event loop on a later iteration —
        // NOT inline, even when connectAll() is synchronous. A single
        // processEvents() races the queued delivery (flaky); spin the loop until
        // the signal arrives.
        QTRY_VERIFY(ready.count() >= 1);
    }

    // ── updateProvider ────────────────────────────────────────────────────────

    void updateProvider_unknownIdReturnsFalse()
    {
        BackendRegistry  registry;
        ProviderManager  manager(&registry);
        ProviderLifecycle lc(&registry, &manager);

        BackendConfiguration cfg;
        QVERIFY(!lc.updateProvider(QStringLiteral("nope"), cfg));
    }

    void updateProvider_knownIdReturnsTrue()
    {
        BackendRegistry  registry;
        ProviderManager  manager(&registry);
        ProviderLifecycle lc(&registry, &manager);

        lc.provisionProvider(
            std::make_unique<FakeProvider>(QStringLiteral("p-upd")));

        BackendConfiguration cfg;
        cfg.id = QStringLiteral("p-upd");
        cfg.displayName = QStringLiteral("Updated");
        QVERIFY(lc.updateProvider(QStringLiteral("p-upd"), cfg));
    }

    void updateProvider_emitsProviderUpdatedSignal()
    {
        BackendRegistry   registry;
        ProviderManager   manager(&registry);
        ProviderLifecycle lc(&registry, &manager);

        lc.provisionProvider(
            std::make_unique<FakeProvider>(QStringLiteral("p-upd-sig")));

        QSignalSpy spy(&lc, &ProviderLifecycle::providerUpdated);

        BackendConfiguration cfg;
        cfg.id = QStringLiteral("p-upd-sig");
        lc.updateProvider(QStringLiteral("p-upd-sig"), cfg);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("p-upd-sig"));
    }

    // ── loadFromProfile / saveToProfile ───────────────────────────────────────

    void loadFromProfile_noopWhenPathEmpty()
    {
        BackendRegistry  registry;
        ProviderManager  manager(&registry);
        ProviderLifecycle lc(&registry, &manager);

        // Should not crash with empty path.
        lc.loadFromProfile();
        QVERIFY(manager.providers().isEmpty());
    }

    void saveToProfile_noopWhenPathEmpty()
    {
        BackendRegistry  registry;
        ProviderManager  manager(&registry);
        ProviderLifecycle lc(&registry, &manager);

        // Should not crash with empty path.
        lc.saveToProfile();
    }
};

QTEST_GUILESS_MAIN(TestProviderLifecycle)
#include "tst_providerlifecycle.moc"
