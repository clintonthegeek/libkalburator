// Phase H Task 8 — End-to-end integration test for the provider stack.
//
// This test wires the full Phase H additive provider design:
//
//   KConfig -> ProviderManager::loadFromProfile (default factory)
//           -> CalDavProvider constructed
//           -> connectAll() against FakeCalDavServer
//           -> backends registered with BackendRegistry under
//              "<provider-id>:<collection-id>" ids
//           -> dynamic_cast to RemoteCalendarBackend verifies the type
//              contract relied on by SyncRouter / SyncEngine
//           -> disconnectAll() unregisters cleanly
//
// Out of scope (intentionally): a SyncEngine round-trip against the
// registered backend. The engine is independently tested by the
// tests/calendar/ stub-host integration suite (Phase D.0). Phase H's
// specific contract is the provider/registry path, which is what this
// test pins. A SyncEngine round-trip would require the fake server to
// handle item-level CalDAV verbs (GET/PUT/REPORT/DELETE) on top of the
// three discovery PROPFINDs the fake currently implements — a 5-10x
// expansion of fake-server complexity for marginal coverage gain.

#include <QtTest/QtTest>
#include <QFutureWatcher>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QUrl>

#include <KConfig>
#include <KConfigGroup>

#include "fakecaldavserver.h"

#include "backendconfiguration.h"
#include "backendregistry.h"
#include "caldavbackendcontribution.h"
#include "caldavprovider.h"
#include "collectioninfo.h"
#include "iprovider.h"
#include "providermanager.h"
#include "remotecalendarbackend.h"
#include "syncbackend.h"

using namespace Kalburator::Sync;

namespace {

// Spin the event loop until a QFuture<void> finishes. Per project
// FINDINGS, do NOT use future.waitForFinished() — Qt6's blocking wait
// does not pump the event loop the QNAM-driven discovery lives on.
bool waitForFutureVoid(QFuture<void> f, int timeoutMs = 5000)
{
    if (f.isFinished()) return true;
    QFutureWatcher<void> w;
    QSignalSpy doneSpy(&w, &QFutureWatcher<void>::finished);
    w.setFuture(f);
    if (f.isFinished()) return true;
    return doneSpy.wait(timeoutMs);
}

// Write a Providers/<id> KConfig subgroup describing one CalDav
// provider. The keys here must match what ProviderManager::loadFromProfile
// reads (kind, displayName, plus connectionParams) and what
// CalDavProvider::load consumes from connectionParams (url, username,
// password).
void writeCalDavConfig(KConfigGroup &providersGroup,
                       const QString &id,
                       const QString &displayName,
                       const QUrl &serverUrl,
                       const QString &username,
                       const QString &password)
{
    KConfigGroup sub = providersGroup.group(id);
    sub.writeEntry("kind",        QStringLiteral("caldav"));
    sub.writeEntry("displayName", displayName);
    sub.writeEntry("url",         serverUrl.toString());
    sub.writeEntry("username",    username);
    sub.writeEntry("password",    password);
    sub.sync();
}

} // namespace

class TstCalDavIntegration : public QObject
{
    Q_OBJECT
private slots:
    void load_from_kconfig_constructs_caldav_provider();
    void connectAll_registers_provider_backends();
    void registered_backend_is_a_remote_backend();
    void disconnectAll_unregisters_backends();
    void save_then_load_round_trip();
};

// ─────────────────────────────────────────────────────────────────────
// 1. KConfig -> default factory -> CalDavProvider
// ─────────────────────────────────────────────────────────────────────
void TstCalDavIntegration::load_from_kconfig_constructs_caldav_provider()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    KConfig cfg(tmp.path() + QStringLiteral("/test.conf"), KConfig::SimpleConfig);
    KConfigGroup providers(&cfg, QStringLiteral("Providers"));
    writeCalDavConfig(providers,
                      QStringLiteral("acct1"),
                      QStringLiteral("My Server"),
                      QUrl(QStringLiteral("http://example.invalid/")),
                      QStringLiteral("alice"),
                      QStringLiteral("secret"));
    cfg.sync();

    BackendRegistry registry;
    registry.registerContribution(std::make_shared<CalDavBackendContribution>());
    ProviderManager manager(&registry);
    manager.loadFromProfile(providers);

    const auto list = manager.providers();
    QCOMPARE(list.size(), 1);

    IProvider *p = list.first();
    QCOMPARE(p->kind(),        QStringLiteral("caldav"));
    QCOMPARE(p->id(),          QStringLiteral("acct1"));
    QCOMPARE(p->displayName(), QStringLiteral("My Server"));
    QVERIFY(qobject_cast<CalDavProvider*>(p) != nullptr);
    QVERIFY(!p->isConnected());
}

// ─────────────────────────────────────────────────────────────────────
// 2. connectAll against fake server -> registry has <id>:<collection>
// ─────────────────────────────────────────────────────────────────────
void TstCalDavIntegration::connectAll_registers_provider_backends()
{
    FakeCalDavServer server;
    server.setCalendars({
        { QStringLiteral("Personal"), QStringLiteral("/calendars/testuser/personal/") },
        { QStringLiteral("Work"),     QStringLiteral("/calendars/testuser/work/")     }
    });
    QVERIFY(server.startListening());

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    KConfig cfg(tmp.path() + QStringLiteral("/test.conf"), KConfig::SimpleConfig);
    KConfigGroup providers(&cfg, QStringLiteral("Providers"));
    // Write the KConfig already pointing at the fake server's baseUrl,
    // so the production load() path runs unchanged.
    writeCalDavConfig(providers,
                      QStringLiteral("acct-conn"),
                      QStringLiteral("Fake Account"),
                      server.baseUrl(),
                      QStringLiteral("testuser"),
                      QStringLiteral("testpass"));
    cfg.sync();

    BackendRegistry registry;
    registry.registerContribution(std::make_shared<CalDavBackendContribution>());
    ProviderManager manager(&registry);
    manager.loadFromProfile(providers);
    QCOMPARE(manager.providers().size(), 1);

    QFuture<void> fut = manager.connectAll();
    QVERIFY(waitForFutureVoid(fut));

    IProvider *p = manager.providers().first();
    QVERIFY(p->isConnected());
    const auto cols = p->collections();
    QCOMPARE(cols.size(), 2);
    QVERIFY(!cols.first().id.isEmpty());

    const QStringList ids = registry.registeredInstanceIds();
    for (const auto &c : cols) {
        const QString expected =
            QStringLiteral("acct-conn:%1").arg(c.id);
        QVERIFY2(ids.contains(expected),
                 qPrintable(QStringLiteral("registry missing id ") + expected
                            + QStringLiteral(" — actual: ")
                            + ids.join(QLatin1Char(','))));
    }
}

// ─────────────────────────────────────────────────────────────────────
// 3. registry-stored backend is a RemoteCalendarBackend (the type contract
//    SyncRouter / SyncEngine rely on)
// ─────────────────────────────────────────────────────────────────────
void TstCalDavIntegration::registered_backend_is_a_remote_backend()
{
    FakeCalDavServer server;
    QVERIFY(server.startListening());
    // Default fake serves one calendar at /calendars/testuser/personal/.

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    KConfig cfg(tmp.path() + QStringLiteral("/test.conf"), KConfig::SimpleConfig);
    KConfigGroup providers(&cfg, QStringLiteral("Providers"));
    writeCalDavConfig(providers,
                      QStringLiteral("acct-type"),
                      QStringLiteral("Fake"),
                      server.baseUrl(),
                      QStringLiteral("testuser"),
                      QStringLiteral("testpass"));
    cfg.sync();

    BackendRegistry registry;
    registry.registerContribution(std::make_shared<CalDavBackendContribution>());
    ProviderManager manager(&registry);
    manager.loadFromProfile(providers);
    QVERIFY(waitForFutureVoid(manager.connectAll()));

    IProvider *p = manager.providers().first();
    QVERIFY(p->isConnected());
    const auto cols = p->collections();
    QVERIFY(!cols.isEmpty());

    const QString backendId =
        QStringLiteral("acct-type:%1").arg(cols.first().id);

    SyncBackend *sb = static_cast<SyncBackend*>(registry.backendInstance(backendId));
    QVERIFY2(sb != nullptr,
             qPrintable(QStringLiteral("registry has no backend for ") + backendId));

    auto *remote = dynamic_cast<RemoteCalendarBackend*>(sb);
    QVERIFY2(remote != nullptr,
             "registry-stored backend must be a RemoteCalendarBackend");
}

// ─────────────────────────────────────────────────────────────────────
// 4. disconnectAll unregisters the backends
// ─────────────────────────────────────────────────────────────────────
void TstCalDavIntegration::disconnectAll_unregisters_backends()
{
    FakeCalDavServer server;
    server.setCalendars({
        { QStringLiteral("A"), QStringLiteral("/calendars/testuser/a/") },
        { QStringLiteral("B"), QStringLiteral("/calendars/testuser/b/") }
    });
    QVERIFY(server.startListening());

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    KConfig cfg(tmp.path() + QStringLiteral("/test.conf"), KConfig::SimpleConfig);
    KConfigGroup providers(&cfg, QStringLiteral("Providers"));
    writeCalDavConfig(providers,
                      QStringLiteral("acct-disc"),
                      QStringLiteral("Fake"),
                      server.baseUrl(),
                      QStringLiteral("testuser"),
                      QStringLiteral("testpass"));
    cfg.sync();

    BackendRegistry registry;
    registry.registerContribution(std::make_shared<CalDavBackendContribution>());
    ProviderManager manager(&registry);
    manager.loadFromProfile(providers);
    QVERIFY(waitForFutureVoid(manager.connectAll()));

    // Sanity: registry has at least one entry under our prefix.
    const QString prefix = QStringLiteral("acct-disc:");
    bool sawAny = false;
    for (const QString &id : registry.registeredInstanceIds()) {
        if (id.startsWith(prefix)) { sawAny = true; break; }
    }
    QVERIFY(sawAny);

    manager.disconnectAll();

    for (const QString &id : registry.registeredInstanceIds()) {
        QVERIFY2(!id.startsWith(prefix),
                 qPrintable(QStringLiteral("registry still has ") + id
                            + QStringLiteral(" after disconnectAll")));
    }
    QVERIFY(!manager.providers().first()->isConnected());
}

// ─────────────────────────────────────────────────────────────────────
// 5. save -> load round-trip via real CalDavProvider (not FakeProvider)
// ─────────────────────────────────────────────────────────────────────
void TstCalDavIntegration::save_then_load_round_trip()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/round.conf");

    const QString providerId    = QStringLiteral("acct-rt");
    const QString displayName   = QStringLiteral("Round-Trip Account");
    const QUrl    serverUrl(QStringLiteral("https://rt.example.invalid/dav/"));
    const QString username      = QStringLiteral("alice");
    const QString password      = QStringLiteral("p4ssword");

    // ── Phase 1: addProvider with externally-configured CalDavProvider,
    //            then saveToProfile.
    {
        KConfig cfg(path, KConfig::SimpleConfig);
        KConfigGroup providers(&cfg, QStringLiteral("Providers"));

        BackendRegistry registry;
        ProviderManager manager(&registry);

        auto provider = std::make_unique<CalDavProvider>();
        BackendConfiguration bc;
        bc.id = providerId;
        bc.type = QStringLiteral("caldav");
        bc.displayName = displayName;
        bc.connectionParams.insert(QStringLiteral("url"),      serverUrl.toString());
        bc.connectionParams.insert(QStringLiteral("username"), username);
        bc.connectionParams.insert(QStringLiteral("password"), password);
        provider->load(bc);
        manager.addProvider(std::move(provider));

        manager.saveToProfile(providers);
        cfg.sync();
    }

    // ── Phase 2: fresh KConfig -> fresh manager -> loadFromProfile ──
    {
        KConfig cfg(path, KConfig::SimpleConfig);
        KConfigGroup providers(&cfg, QStringLiteral("Providers"));

        BackendRegistry registry;
        registry.registerContribution(std::make_shared<CalDavBackendContribution>());
        ProviderManager manager(&registry);
        manager.loadFromProfile(providers);

        const auto list = manager.providers();
        QCOMPARE(list.size(), 1);

        auto *p = qobject_cast<CalDavProvider*>(list.first());
        QVERIFY(p != nullptr);
        QCOMPARE(p->id(),          providerId);
        QCOMPARE(p->kind(),        QStringLiteral("caldav"));
        QCOMPARE(p->displayName(), displayName);

        const BackendConfiguration restored = p->save();
        QCOMPARE(restored.id, providerId);
        QCOMPARE(restored.type, QStringLiteral("caldav"));
        QCOMPARE(restored.displayName, displayName);
        QCOMPARE(restored.connectionParams.value(QStringLiteral("url")).toString(),
                 serverUrl.toString());
        QCOMPARE(restored.connectionParams.value(QStringLiteral("username")).toString(),
                 username);
        QCOMPARE(restored.connectionParams.value(QStringLiteral("password")).toString(),
                 password);
    }
}

QTEST_GUILESS_MAIN(TstCalDavIntegration)
#include "tst_caldav_integration.moc"
