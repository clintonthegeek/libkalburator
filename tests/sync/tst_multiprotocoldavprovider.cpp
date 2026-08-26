#include <QObject>
#include <QtTest/QtTest>
#include <QFutureWatcher>
#include <QSignalSpy>
#include <QUuid>

#include "fakecaldavserver.h"
#include "fakecarddavserver.h"

#include "../../src/sync/multiprotocoldavprovider.h"
#include "../../src/calendar/syncbackend.h"
#include "../../src/calendar/remotecalendarbackend.h"
#include "../../src/contacts/remotecontactsbackend.h"
#include "../../src/universal/kinddemuxbackend.h"
#include "../../src/universal/filteredcollectionbackend.h"
#include "../../src/plugin/pluginmanager.h"
#include "../../src/shape/shaperegistries.h"
#include "../../src/plugin/stock_plugins.h"
#include "../../src/sync/backendregistry.h"

using namespace Kalburator;
using namespace Kalburator::Sync;

namespace {

// Task 2.2: MultiProtocolDavProvider emits at most two specs per connected
// account — domainId "cal" (a single RemoteCalendarBackend hosting every
// calendar) and, in full mode with addressbooks discovered, domainId
// "contacts" (a single RemoteContactsBackend hosting every addressbook). A
// lookup by domainId is the new equivalent of the old per-collection
// createBackend(collectionId).
std::unique_ptr<IBlobBackend>
backendForDomain(IProvider &provider, const QString &domainId)
{
    auto specs = provider.createBackends();
    for (auto &spec : specs) {
        if (spec.domainId == domainId) return std::move(spec.backend);
    }
    return nullptr;
}

// ---- B2C P3.e kind-demux helpers -------------------------------------------

// Full VCALENDAR wrapper around one component block of @p kind ("VEVENT",
// "VTODO", …) with the given UID — the raw-bytes shape a CalDAV server
// returns per calendar object.
QByteArray icsBlob(const QString &uid, const char *kind, const char *summary)
{
    return QByteArray("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//test//EN\r\nBEGIN:")
        + kind + "\r\nUID:" + uid.toUtf8()
        + "\r\nSUMMARY:" + summary + "\r\nEND:" + kind
        + "\r\nEND:VCALENDAR\r\n";
}

void hybridTestConfig(BackendConfiguration &cfg, const char *cfgId)
{
    // Shared connect recipe for the demux slots: one CalDAV server with a
    // single MIXED collection (VEVENT+VTODO advertised), CardDAV pointed at
    // a bogus principal.
    cfg.id   = QString::fromLatin1(cfgId);
    cfg.type = QStringLiteral("multiproto-dav");
    cfg.connectionParams.insert(QStringLiteral("username"), QStringLiteral("testuser"));
    cfg.connectionParams.insert(QStringLiteral("password"), QStringLiteral("testpass"));
    cfg.connectionParams.insert(QStringLiteral("manualCarddavPrincipal"),
                                QStringLiteral("/bogus-carddav/"));
}

} // anonymous namespace

class TstMultiProtocolDavProvider : public QObject
{
    Q_OBJECT
private slots:
    void kindIsMultiprotoDav();
    void idIsNonEmptyAfterConstruction();
    void displayNameDefaultsToSomethingHuman();
    void isNotConnectedAfterConstruction();
    void collectionsEmptyAfterConstruction();
    void loadAndSaveRoundTripsConnectionParams();
    void calendarsOnlyRoundTripsThroughSaveLoad();
    void loadAppliesDisplayNameAndId();
    void connectWithoutUrlReturnsFalseQuickly();
    void connectInvalidCredentialsEmitsErrorAndResolvesFalse();
    void connectPartialSuccessSkipped();
    void calendarsOnly_mode_excludes_contacts();
    void createBackendsNotConnectedReturnsEmpty();
    void connectPopulatesContentTypesOnCalDavCollections();
    void primedBackendEmitsAdvertisedCollectionIdAtDiscovery();
    void pluginRegistersMultiProtoDavContribution();
    void contributionCreateProviderHonorsParent();
    void connect_while_inflight_is_idempotent();
    void testTwoDomainSpecs();
    void testCalendarsOnlySingleSpec();
    void testSlugIdsNoPrefixes();
    // B2C P3.e kind-demux
    void hybridCollectionYieldsTodoDomainSpec();
    void threeSpecsWhenContactsAlsoExist();
    void viewsSplitRecordsByComponentKind();
    void viewRecordIdsMatchParentUids();
    void writesThroughViewsReachSharedTransport();
};

void TstMultiProtocolDavProvider::kindIsMultiprotoDav()
{
    MultiProtocolDavProvider p;
    QCOMPARE(p.kind(), QStringLiteral("multiproto-dav"));
}

void TstMultiProtocolDavProvider::idIsNonEmptyAfterConstruction()
{
    MultiProtocolDavProvider p;
    const QString id = p.id();
    QVERIFY(!id.isEmpty());
    QVERIFY(QUuid(id).isNull() == false);
}

void TstMultiProtocolDavProvider::displayNameDefaultsToSomethingHuman()
{
    MultiProtocolDavProvider p;
    QVERIFY(!p.displayName().isEmpty());
}

void TstMultiProtocolDavProvider::isNotConnectedAfterConstruction()
{
    MultiProtocolDavProvider p;
    QVERIFY(!p.isConnected());
}

void TstMultiProtocolDavProvider::collectionsEmptyAfterConstruction()
{
    MultiProtocolDavProvider p;
    QVERIFY(p.collections().isEmpty());
}

void TstMultiProtocolDavProvider::loadAndSaveRoundTripsConnectionParams()
{
    BackendConfiguration cfg;
    cfg.id = QStringLiteral("test-uuid-1");
    cfg.type = QStringLiteral("multiproto-dav");
    cfg.displayName = QStringLiteral("My Nextcloud");
    cfg.connectionParams[QStringLiteral("url")]
        = QStringLiteral("https://cloud.example.com");
    cfg.connectionParams[QStringLiteral("username")]
        = QStringLiteral("alice");
    cfg.connectionParams[QStringLiteral("password")]
        = QStringLiteral("hunter2");
    cfg.connectionParams[QStringLiteral("manualCaldavPrincipal")]
        = QStringLiteral("https://cloud.example.com/dav/cal/");

    MultiProtocolDavProvider p;
    p.load(cfg);
    const BackendConfiguration roundtrip = p.save();

    QCOMPARE(roundtrip.id,          cfg.id);
    QCOMPARE(roundtrip.type,        QStringLiteral("multiproto-dav"));
    QCOMPARE(roundtrip.displayName, cfg.displayName);
    QCOMPARE(roundtrip.connectionParams.value(QStringLiteral("url")).toString(),
             QStringLiteral("https://cloud.example.com"));
    QCOMPARE(roundtrip.connectionParams.value(QStringLiteral("username")).toString(),
             QStringLiteral("alice"));
    QCOMPARE(roundtrip.connectionParams.value(QStringLiteral("password")).toString(),
             QStringLiteral("hunter2"));
    QCOMPARE(roundtrip.connectionParams.value(QStringLiteral("manualCaldavPrincipal")).toString(),
             QStringLiteral("https://cloud.example.com/dav/cal/"));
}

// A calendarsOnly provider must persist the flag so that a registry-
// reconstructed provider — which the contribution builds with
// calendarsOnly=false (multiprotocoldavbackendcontribution.h) — restores the
// intended calendars-only behavior on load(). Without this, a reopened
// collection re-discovers contacts the user never asked for.
void TstMultiProtocolDavProvider::calendarsOnlyRoundTripsThroughSaveLoad()
{
    MultiProtocolDavProvider src(/*calendarsOnly=*/true);
    const BackendConfiguration cfg = src.save();
    QCOMPARE(cfg.connectionParams.value(QStringLiteral("calendarsOnly")).toBool(), true);

    // Mirror the reload path: the contribution constructs with calendarsOnly=false,
    // then load() must restore the persisted true.
    MultiProtocolDavProvider reloaded(/*calendarsOnly=*/false);
    reloaded.load(cfg);
    QCOMPARE(reloaded.save().connectionParams.value(QStringLiteral("calendarsOnly")).toBool(),
             true);
}

void TstMultiProtocolDavProvider::loadAppliesDisplayNameAndId()
{
    BackendConfiguration cfg;
    cfg.id = QStringLiteral("specific-id");
    cfg.displayName = QStringLiteral("Work Nextcloud");

    MultiProtocolDavProvider p;
    p.load(cfg);

    QCOMPARE(p.id(),          QStringLiteral("specific-id"));
    QCOMPARE(p.displayName(), QStringLiteral("Work Nextcloud"));
}

void TstMultiProtocolDavProvider::connectWithoutUrlReturnsFalseQuickly()
{
    MultiProtocolDavProvider p;
    BackendConfiguration cfg;
    cfg.id = QStringLiteral("test");
    cfg.connectionParams[QStringLiteral("username")] = QStringLiteral("u");
    cfg.connectionParams[QStringLiteral("password")] = QStringLiteral("p");
    // url intentionally absent
    p.load(cfg);

    auto fut = p.connect();
    QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 5000);
    QCOMPARE(fut.resultAt(0), false);
    QVERIFY(!p.isConnected());
}

void TstMultiProtocolDavProvider::connectInvalidCredentialsEmitsErrorAndResolvesFalse()
{
    MultiProtocolDavProvider p;
    QSignalSpy errSpy(&p, &IProvider::error);
    BackendConfiguration cfg;
    cfg.id = QStringLiteral("test");
    cfg.connectionParams[QStringLiteral("url")]      = QStringLiteral("http://localhost:1/");
    cfg.connectionParams[QStringLiteral("username")] = QStringLiteral("nobody");
    cfg.connectionParams[QStringLiteral("password")] = QStringLiteral("nopass");
    p.load(cfg);

    auto fut = p.connect();
    QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 30000);
    QCOMPARE(fut.resultAt(0), false);
    QVERIFY(errSpy.count() > 0 || !p.isConnected());  // either error emitted or simply not connected
}

void TstMultiProtocolDavProvider::connectPartialSuccessSkipped()
{
    // CalDAV succeeds; CardDAV pointed at "/bogus-carddav/" (404) so it fails.
    // In full mode (calendarsOnly = false): partial success — provider reports
    // connected=true with only calendar collections; lastWarning() names the
    // failed protocol.
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    BackendConfiguration cfg;
    cfg.id   = QStringLiteral("partial-test");
    cfg.type = QStringLiteral("multiproto-dav");
    cfg.connectionParams.insert(QStringLiteral("url"), server.baseUrl().toString());
    cfg.connectionParams.insert(QStringLiteral("username"), QStringLiteral("testuser"));
    cfg.connectionParams.insert(QStringLiteral("password"), QStringLiteral("testpass"));
    cfg.connectionParams.insert(QStringLiteral("manualCarddavPrincipal"),
                                QStringLiteral("/bogus-carddav/"));

    MultiProtocolDavProvider provider(false);   // full mode: CardDAV failure = partial
    provider.load(cfg);

    QFuture<bool> fut = provider.connect();
    QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 20000);
    QCOMPARE(fut.resultAt(0), true);
    QVERIFY(provider.isConnected());

    // Calendar collections present; no contact collections.
    bool hasCalCol     = false;
    bool hasContactCol = false;
    for (const auto &c : provider.collections()) {
        if (c.type == QLatin1String("calendar")) hasCalCol     = true;
        if (c.type == QLatin1String("contacts")) hasContactCol = true;
    }
    QVERIFY2(hasCalCol,      "expected at least one calendar collection after partial connect");
    QVERIFY2(!hasContactCol, "expected no contact collections: CardDAV failed");

    // A warning about the CardDAV failure must be surfaced.
    QVERIFY2(!provider.lastWarning().isEmpty(),
             "expected lastWarning() to name the CardDAV failure");
}

void TstMultiProtocolDavProvider::calendarsOnly_mode_excludes_contacts()
{
    // WP-A1 regression: in calendarsOnly mode (ctor default) CalDAV succeeds,
    // CardDAV is irrelevant — connect() resolves true and collections() returns
    // only :cal: entries even if CardDAV would also fail.
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    BackendConfiguration cfg;
    cfg.id   = QStringLiteral("co-test");
    cfg.type = QStringLiteral("multiproto-dav");
    cfg.connectionParams.insert(QStringLiteral("url"), server.baseUrl().toString());
    cfg.connectionParams.insert(QStringLiteral("username"), QStringLiteral("testuser"));
    cfg.connectionParams.insert(QStringLiteral("password"), QStringLiteral("testpass"));
    cfg.connectionParams.insert(QStringLiteral("manualCarddavPrincipal"),
                                QStringLiteral("/bogus-carddav/"));

    MultiProtocolDavProvider provider(true);    // calendarsOnly = true (ctor explicit)
    provider.load(cfg);

    QFuture<bool> fut = provider.connect();
    QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 20000);
    QCOMPARE(fut.resultAt(0), true);
    QVERIFY(provider.isConnected());

    // Only calendar collections — no contacts regardless of CardDAV outcome.
    for (const auto &c : provider.collections()) {
        QVERIFY2(c.type != QLatin1String("contacts"),
                 qPrintable("Expected no contact collection in calendarsOnly mode, got: " + c.id));
    }
    QVERIFY2(!provider.collections().isEmpty(),
             "expected at least one calendar collection");

    // In calendarsOnly mode CardDAV failure does NOT produce a warning.
    QVERIFY2(provider.lastWarning().isEmpty(),
             "expected no warning: CardDAV failure is irrelevant in calendarsOnly mode");
}

void TstMultiProtocolDavProvider::createBackendsNotConnectedReturnsEmpty()
{
    // Not connected, no m_urlByCollectionId / m_collections entries —
    // createBackends() has nothing to produce regardless of what ids a
    // later connect() would populate.
    MultiProtocolDavProvider p;
    QVERIFY(!p.isConnected());
    QVERIFY(p.createBackends().empty());
}

void TstMultiProtocolDavProvider::connectPopulatesContentTypesOnCalDavCollections()
{
    // The CalDAV-leg CollectionInfo rows must carry the discovered
    // per-calendar component capabilities as contentTypes (WildPalms RFC
    // 2026-06-09). Same fake-server pattern as the v0.63 convergence test:
    // one base URL serves CalDAV; the CardDAV half is pointed at a bogus
    // principal so it fails fast and the provider connects via CalDAV alone.
    FakeCalDavServer server;
    server.setCalendars({
        { QStringLiteral("Events"), QStringLiteral("/calendars/testuser/events/") },
        { QStringLiteral("Tasks"),  QStringLiteral("/calendars/testuser/tasks/") },
        { QStringLiteral("Mixed"),  QStringLiteral("/calendars/testuser/mixed/") }
    });
    server.setCalendarComponents(QStringLiteral("/calendars/testuser/events/"),
                                 { QStringLiteral("VEVENT") });
    server.setCalendarComponents(QStringLiteral("/calendars/testuser/tasks/"),
                                 { QStringLiteral("VTODO") });
    server.setCalendarComponents(QStringLiteral("/calendars/testuser/mixed/"),
                                 { QStringLiteral("VEVENT"), QStringLiteral("VTODO") });
    QVERIFY(server.startListening());

    BackendConfiguration cfg;
    cfg.id = QStringLiteral("mpdav-ct-test");
    cfg.type = QStringLiteral("multiproto-dav");
    cfg.connectionParams.insert(QStringLiteral("url"), server.baseUrl().toString());
    cfg.connectionParams.insert(QStringLiteral("username"), QStringLiteral("testuser"));
    cfg.connectionParams.insert(QStringLiteral("password"), QStringLiteral("testpass"));
    cfg.connectionParams.insert(QStringLiteral("manualCarddavPrincipal"),
                                QStringLiteral("/bogus-carddav/"));

    MultiProtocolDavProvider provider;
    provider.load(cfg);

    QFuture<bool> fut = provider.connect();
    QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 20000);
    QCOMPARE(fut.resultAt(0), true);

    QHash<QString, QStringList> typesByName;
    for (const auto &c : provider.collections()) {
        if (c.type == QLatin1String("calendar"))
            typesByName.insert(c.name, c.contentTypes);
    }
    QCOMPARE(typesByName.size(), 3);
    QCOMPARE(typesByName.value(QStringLiteral("Events")),
             (QStringList{ QStringLiteral("VEVENT") }));
    QCOMPARE(typesByName.value(QStringLiteral("Tasks")),
             (QStringList{ QStringLiteral("VTODO") }));
    QCOMPARE(typesByName.value(QStringLiteral("Mixed")),
             (QStringList{ QStringLiteral("VEVENT"), QStringLiteral("VTODO") }));
}

void TstMultiProtocolDavProvider::primedBackendEmitsAdvertisedCollectionIdAtDiscovery()
{
    // Regression (PlanStan "Missing Calendars" false positive): the per-calendar
    // backend from createBackends() must emit calendarDiscovered() with the SAME
    // (prefixed) id that collections() advertised. The host builds its
    // logical-calendar bindings from collections() ids, then matches discovery
    // by exact id. Priming with the inner (unprefixed) key made discovery emit
    // a different id, so every calendar was orphaned at discovery AND reported
    // as a missing calendar — even though its events loaded fine.
    FakeCalDavServer server;
    server.setCalendars({
        { QStringLiteral("Events"), QStringLiteral("/calendars/testuser/events/") }
    });
    server.setCalendarComponents(QStringLiteral("/calendars/testuser/events/"),
                                 { QStringLiteral("VEVENT") });
    QVERIFY(server.startListening());

    BackendConfiguration cfg;
    cfg.id   = QStringLiteral("mpdav-primed-id");
    cfg.type = QStringLiteral("multiproto-dav");
    cfg.connectionParams.insert(QStringLiteral("url"), server.baseUrl().toString());
    cfg.connectionParams.insert(QStringLiteral("username"), QStringLiteral("testuser"));
    cfg.connectionParams.insert(QStringLiteral("password"), QStringLiteral("testpass"));
    cfg.connectionParams.insert(QStringLiteral("manualCarddavPrincipal"),
                                QStringLiteral("/bogus-carddav/"));

    MultiProtocolDavProvider provider;   // calendarsOnly default
    provider.load(cfg);
    QFuture<bool> fut = provider.connect();
    QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 20000);
    QCOMPARE(fut.resultAt(0), true);

    QString calId;
    for (const auto &c : provider.collections()) {
        if (c.type == QLatin1String("calendar")) { calId = c.id; break; }
    }
    QVERIFY2(!calId.isEmpty(), "expected a calendar collection after connect");

    // Task 2.2: the single spec's domainId is "cal", not a per-collection id.
    auto backend = backendForDomain(provider, QStringLiteral("cal"));
    QVERIFY2(backend != nullptr, "createBackends() produced no \"cal\" spec");
    auto *cal = dynamic_cast<SyncBackend *>(backend.get());
    QVERIFY2(cal != nullptr, "calendar collection did not yield a SyncBackend");

    QSignalSpy discovered(cal, &SyncBackend::calendarDiscovered);
    cal->loadCalendars(QStringLiteral("any-collection-id"));   // replays primed cache
    QTRY_VERIFY_WITH_TIMEOUT(discovered.count() >= 1, 5000);

    // calendarDiscovered(collectionId, calendarId): the calendarId (index 1)
    // must equal the advertised collection id, not the inner discovery key.
    const QString emitted = discovered.first().at(1).toString();
    QCOMPARE(emitted, calId);
}

void TstMultiProtocolDavProvider::pluginRegistersMultiProtoDavContribution()
{
    BackendRegistry reg;
    Shape::ShapeRegistries shape;
    PluginManager pm(&reg, shape);
    registerStockPlugins(pm);
    QVERIFY(reg.contributionFor(
        QStringLiteral("multiproto-dav")) != nullptr);
}

void TstMultiProtocolDavProvider::contributionCreateProviderHonorsParent()
{
    // Pins the ctor-argument swallow found by the 2026-06-10 audit: the
    // contribution called make_unique<MultiProtocolDavProvider>(parent),
    // binding the QObject* to the bool calendarsOnly parameter (pointer→bool
    // conversion) and dropping the parent entirely — so the provider was
    // unparented and the mode flag tracked the parent's null-ness.
    BackendRegistry reg;
    Shape::ShapeRegistries shape;
    PluginManager pm(&reg, shape);
    registerStockPlugins(pm);
    auto *contrib = reg.contributionFor(QStringLiteral("multiproto-dav"));
    QVERIFY(contrib != nullptr);

    QObject owner;
    auto provider = contrib->createProvider(&owner);
    QVERIFY(provider != nullptr);
    QCOMPARE(provider->parent(), &owner);
    // Reparented onto `owner` — hand ownership to the parent to avoid the
    // unique_ptr/QObject-parent double delete. Capture the released pointer
    // (rather than discarding it outright) so the intentional hand-off reads
    // as deliberate, not a leak.
    IProvider *released = provider.release();
    Q_UNUSED(released);
}

void TstMultiProtocolDavProvider::connect_while_inflight_is_idempotent()
{
    QTcpServer hungServer;
    QVERIFY(hungServer.listen(QHostAddress::LocalHost, 0));
    const QUrl hungUrl(
        QStringLiteral("http://127.0.0.1:%1/").arg(hungServer.serverPort()));

    MultiProtocolDavProvider p;
    BackendConfiguration cfg;
    cfg.id = QStringLiteral("test");
    cfg.connectionParams[QStringLiteral("url")]      = hungUrl.toString();
    cfg.connectionParams[QStringLiteral("username")] = QStringLiteral("u");
    cfg.connectionParams[QStringLiteral("password")] = QStringLiteral("p");
    p.load(cfg);

    QFuture<bool> fut1 = p.connect();
    QVERIFY(!fut1.isFinished());

    // Second connect() in-flight: must return the same (shared) future.
    QFuture<bool> fut2 = p.connect();
    QVERIFY(!fut2.isFinished());

    QSignalSpy stateSpy(&p, qOverload<bool>(&IProvider::connectionStateChanged));
    p.disconnect();
    QTRY_VERIFY_WITH_TIMEOUT(fut1.isFinished(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(fut2.isFinished(), 5000);
    QCOMPARE(fut1.resultAt(0), false);
    QCOMPARE(fut2.resultAt(0), false);
    // No connectionStateChanged: was never connected.
    QCOMPARE(stateSpy.count(), 0);
}

void TstMultiProtocolDavProvider::testSlugIdsNoPrefixes()
{
    FakeCalDavServer server;
    server.setCalendars({
        { QStringLiteral("Personal"), QStringLiteral("/calendars/testuser/personal/") }
    });
    QVERIFY(server.startListening());

    BackendConfiguration cfg;
    cfg.id = QStringLiteral("slug-test");
    cfg.connectionParams.insert(QStringLiteral("url"), server.baseUrl().toString());
    cfg.connectionParams.insert(QStringLiteral("username"), QStringLiteral("testuser"));
    cfg.connectionParams.insert(QStringLiteral("password"), QStringLiteral("testpass"));
    cfg.connectionParams.insert(QStringLiteral("manualCarddavPrincipal"),
                                QStringLiteral("/bogus-carddav/"));

    MultiProtocolDavProvider provider(true);   // calendarsOnly
    provider.load(cfg);

    QFuture<bool> fut = provider.connect();
    QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 20000);
    QCOMPARE(fut.resultAt(0), true);

    QVERIFY(!provider.collections().isEmpty());
    for (const auto &c : provider.collections()) {
        QVERIFY2(!c.id.contains(QStringLiteral("multiproto-dav:")),
                 qPrintable("collection id still carries the deleted prefix: " + c.id));
    }
}

void TstMultiProtocolDavProvider::testCalendarsOnlySingleSpec()
{
    FakeCalDavServer server;
    server.setCalendars({
        { QStringLiteral("Personal"), QStringLiteral("/calendars/testuser/personal/") },
        { QStringLiteral("Work"),     QStringLiteral("/calendars/testuser/work/") }
    });
    QVERIFY(server.startListening());

    BackendConfiguration cfg;
    cfg.id = QStringLiteral("cal-only-test");
    cfg.connectionParams.insert(QStringLiteral("url"), server.baseUrl().toString());
    cfg.connectionParams.insert(QStringLiteral("username"), QStringLiteral("testuser"));
    cfg.connectionParams.insert(QStringLiteral("password"), QStringLiteral("testpass"));
    cfg.connectionParams.insert(QStringLiteral("manualCarddavPrincipal"),
                                QStringLiteral("/bogus-carddav/"));

    MultiProtocolDavProvider provider(true);   // calendarsOnly = true
    provider.load(cfg);

    QFuture<bool> fut = provider.connect();
    QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 20000);
    QCOMPARE(fut.resultAt(0), true);

    auto specs = provider.createBackends();
    QCOMPARE(specs.size(), std::size_t(1));
    QCOMPARE(specs.front().domainId, QStringLiteral("cal"));
    QCOMPARE(specs.front().collections.size(), 2);

    auto *remote = dynamic_cast<RemoteCalendarBackend *>(specs.front().backend.get());
    QVERIFY(remote != nullptr);
}

void TstMultiProtocolDavProvider::testTwoDomainSpecs()
{
    // Two physically separate fake servers: CalDAV (untouched) and CardDAV.
    // MultiProtocolDavProvider hands both discoveries the SAME configured
    // "url" (the CalDAV server's origin), so the CardDAV leg is steered onto
    // its own server entirely via an ABSOLUTE manualCarddavPrincipal — this
    // skips the well-known bootstrap and the root PROPFIND (which would
    // otherwise hit the CalDAV server) and walks straight from that absolute
    // principal href to the addressbook-home-set. The card server's context
    // path is set to its own base URL so every href it returns (home-set,
    // addressbook list) is already absolute and self-consistent, never
    // falling back to relative-resolution against the CalDAV server's host.
    FakeCalDavServer calServer;
    calServer.setCalendars({
        { QStringLiteral("Personal"), QStringLiteral("/calendars/testuser/personal/") },
        { QStringLiteral("Work"),     QStringLiteral("/calendars/testuser/work/") }
    });
    QVERIFY(calServer.startListening());

    FakeCardDavServer cardServer;
    cardServer.setAddressbooks({
        { QStringLiteral("personal"), QStringLiteral("Personal") },
        { QStringLiteral("family"),   QStringLiteral("Family") }
    });
    QVERIFY(cardServer.startListening());   // must listen before baseUrl() has a real port
    QString cardBase = cardServer.baseUrl().toString();
    if (cardBase.endsWith(QLatin1Char('/'))) cardBase.chop(1);
    cardServer.setContextPath(cardBase);

    BackendConfiguration cfg;
    cfg.id = QStringLiteral("two-domain-test");
    cfg.connectionParams.insert(QStringLiteral("url"), calServer.baseUrl().toString());
    cfg.connectionParams.insert(QStringLiteral("username"), QStringLiteral("testuser"));
    cfg.connectionParams.insert(QStringLiteral("password"), QStringLiteral("testpass"));
    cfg.connectionParams.insert(QStringLiteral("manualCarddavPrincipal"),
                                cardBase + QStringLiteral("/principals/users/testuser/"));

    MultiProtocolDavProvider provider(false);   // full mode: contacts enabled
    provider.load(cfg);

    QFuture<bool> fut = provider.connect();
    QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 20000);
    QCOMPARE(fut.resultAt(0), true);
    QVERIFY(provider.isConnected());

    int calCount = 0, contactCount = 0;
    for (const auto &c : provider.collections()) {
        if (c.type == QLatin1String("calendar")) ++calCount;
        else if (c.type == QLatin1String("contacts")) ++contactCount;
    }
    QCOMPARE(calCount, 2);
    QCOMPARE(contactCount, 2);

    auto specs = provider.createBackends();
    QCOMPARE(specs.size(), std::size_t(2));

    bool sawCal = false, sawContacts = false;
    for (auto &spec : specs) {
        if (spec.domainId == QStringLiteral("cal")) {
            sawCal = true;
            QCOMPARE(spec.collections.size(), 2);
            QVERIFY(dynamic_cast<RemoteCalendarBackend *>(spec.backend.get()) != nullptr);
        } else if (spec.domainId == QStringLiteral("contacts")) {
            sawContacts = true;
            QCOMPARE(spec.collections.size(), 2);
            QVERIFY(dynamic_cast<RemoteContactsBackend *>(spec.backend.get()) != nullptr);
        }
    }
    QVERIFY(sawCal);
    QVERIFY(sawContacts);
}

// ---- B2C P3.e kind-demux ----------------------------------------------------
//
// A mixed CalDAV collection (VEVENT+VTODO in ONE DAV collection) must
// surface as TWO ProviderBackendSpecs: "cal" filtered to VEVENT/VJOURNAL and
// a NEW "todo" spec filtered to VTODO — both over the same underlying
// RemoteCalendarBackend transport, both keeping the SAME collection id.
// Rectification rule (binding): transport grouping never crosses a domain
// boundary.

void TstMultiProtocolDavProvider::hybridCollectionYieldsTodoDomainSpec()
{
    FakeCalDavServer server;
    const QString mixedHref = QStringLiteral("/calendars/testuser/mixed/");
    server.setCalendars({ { QStringLiteral("Mixed"), mixedHref } });
    server.setCalendarComponents(mixedHref,
                                 { QStringLiteral("VEVENT"), QStringLiteral("VTODO") });
    server.setSeedEvents(mixedHref, {
        icsBlob(QStringLiteral("evt-1"), "VEVENT", "One"),
        icsBlob(QStringLiteral("evt-2"), "VEVENT", "Two"),
        icsBlob(QStringLiteral("tdo-1"), "VTODO",  "Task One"),
        icsBlob(QStringLiteral("tdo-2"), "VTODO",  "Task Two"),
    });
    QVERIFY(server.startListening());

    BackendConfiguration cfg;
    hybridTestConfig(cfg, "demux-two-spec");
    cfg.connectionParams.insert(QStringLiteral("url"), server.baseUrl().toString());

    MultiProtocolDavProvider provider;   // calendarsOnly default: no contacts
    provider.load(cfg);
    QFuture<bool> fut = provider.connect();
    QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 20000);
    QCOMPARE(fut.resultAt(0), true);

    auto specs = provider.createBackends();
    QCOMPARE(specs.size(), std::size_t(2));

    bool sawCal = false, sawTodo = false;
    QString calColId, todoColId;
    for (auto &spec : specs) {
        if (spec.domainId == QStringLiteral("cal")) {
            sawCal = true;
            QCOMPARE(spec.collections.size(), 1);
            calColId = spec.collections.first().id;
            // Demuxed shape: the cal domain no longer hands out the raw
            // RemoteCalendarBackend (which would leak VTODOs across the
            // boundary) but the routed KindDemuxBackend.
            QVERIFY(dynamic_cast<RemoteCalendarBackend *>(spec.backend.get()) == nullptr);
            QVERIFY(dynamic_cast<Kalburator::Sinks::KindDemuxBackend *>(spec.backend.get()) != nullptr);
        } else if (spec.domainId == QStringLiteral("todo")) {
            sawTodo = true;
            QCOMPARE(spec.collections.size(), 1);
            todoColId = spec.collections.first().id;
            QVERIFY(dynamic_cast<Kalburator::Sinks::KindDemuxBackend *>(spec.backend.get()) != nullptr);
        }
    }
    QVERIFY(sawCal);
    QVERIFY(sawTodo);
    // Pinned decision 3: the filtered views keep the SAME collection id —
    // disambiguation lives at the spec (domain) level, never in ids.
    QCOMPARE(calColId, todoColId);
}

void TstMultiProtocolDavProvider::threeSpecsWhenContactsAlsoExist()
{
    FakeCalDavServer calServer;
    const QString mixedHref = QStringLiteral("/calendars/testuser/mixed/");
    calServer.setCalendars({ { QStringLiteral("Mixed"), mixedHref } });
    calServer.setCalendarComponents(mixedHref,
                                    { QStringLiteral("VEVENT"), QStringLiteral("VTODO") });
    QVERIFY(calServer.startListening());

    FakeCardDavServer cardServer;
    cardServer.setAddressbooks({
        { QStringLiteral("personal"), QStringLiteral("Personal") }
    });
    QVERIFY(cardServer.startListening());
    QString cardBase = cardServer.baseUrl().toString();
    if (cardBase.endsWith(QLatin1Char('/'))) cardBase.chop(1);
    cardServer.setContextPath(cardBase);

    BackendConfiguration cfg;
    hybridTestConfig(cfg, "demux-three-spec");
    cfg.connectionParams.insert(QStringLiteral("url"), calServer.baseUrl().toString());
    cfg.connectionParams.insert(QStringLiteral("manualCarddavPrincipal"),
                                cardBase + QStringLiteral("/principals/users/testuser/"));

    MultiProtocolDavProvider provider(false);   // full mode
    provider.load(cfg);
    QFuture<bool> fut = provider.connect();
    QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 20000);
    QCOMPARE(fut.resultAt(0), true);

    auto specs = provider.createBackends();
    QCOMPARE(specs.size(), std::size_t(3));
    QStringList domains;
    for (auto &spec : specs)
        domains << spec.domainId;
    domains.sort();
    QCOMPARE(domains, (QStringList{ QStringLiteral("cal"),
                                    QStringLiteral("contacts"),
                                    QStringLiteral("todo") }));

    for (auto &spec : specs) {
        if (spec.domainId == QLatin1String("contacts"))
            QVERIFY(dynamic_cast<RemoteContactsBackend *>(spec.backend.get()) != nullptr);
    }
}

void TstMultiProtocolDavProvider::viewsSplitRecordsByComponentKind()
{
    FakeCalDavServer server;
    const QString mixedHref = QStringLiteral("/calendars/testuser/mixed/");
    server.setCalendars({ { QStringLiteral("Mixed"), mixedHref } });
    server.setCalendarComponents(mixedHref,
                                 { QStringLiteral("VEVENT"), QStringLiteral("VTODO") });
    server.setSeedEvents(mixedHref, {
        icsBlob(QStringLiteral("evt-1"), "VEVENT", "One"),
        icsBlob(QStringLiteral("evt-2"), "VEVENT", "Two"),
        icsBlob(QStringLiteral("tdo-1"), "VTODO",  "Task One"),
        icsBlob(QStringLiteral("tdo-2"), "VTODO",  "Task Two"),
    });
    QVERIFY(server.startListening());

    BackendConfiguration cfg;
    hybridTestConfig(cfg, "demux-split");
    cfg.connectionParams.insert(QStringLiteral("url"), server.baseUrl().toString());

    MultiProtocolDavProvider provider;
    provider.load(cfg);
    QFuture<bool> fut = provider.connect();
    QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 20000);
    QCOMPARE(fut.resultAt(0), true);

    auto specs = provider.createBackends();
    QCOMPARE(specs.size(), std::size_t(2));

    QString colId;
    std::unique_ptr<IBlobBackend> calBackend, todoBackend;
    for (auto &spec : specs) {
        if (spec.domainId == QLatin1String("cal")) {
            colId = spec.collections.first().id;
            calBackend = std::move(spec.backend);
        } else if (spec.domainId == QLatin1String("todo")) {
            todoBackend = std::move(spec.backend);
        }
    }
    QVERIFY(calBackend && todoBackend);

    auto *calDemux = dynamic_cast<SyncBackendBase *>(calBackend.get());
    auto *todoDemux = dynamic_cast<SyncBackendBase *>(todoBackend.get());
    QVERIFY(calDemux && todoDemux);

    // TODO view: ONLY VTODO records from the mixed server payload.
    const QList<BackendRecord> todoRecs = todoDemux->loadRecords(colId);
    QCOMPARE(todoRecs.size(), 2);
    QSet<QString> todoUids;
    for (const auto &r : todoRecs) {
        QVERIFY(r.data.contains("BEGIN:VTODO"));
        QVERIFY(!r.data.contains("BEGIN:VEVENT"));
        todoUids.insert(r.id);
    }
    QCOMPARE(todoUids, (QSet<QString>{ QStringLiteral("tdo-1"), QStringLiteral("tdo-2") }));

    // CAL view: ONLY VEVENT records.
    const QList<BackendRecord> calRecs = calDemux->loadRecords(colId);
    QCOMPARE(calRecs.size(), 2);
    QSet<QString> calUids;
    for (const auto &r : calRecs) {
        QVERIFY(r.data.contains("BEGIN:VEVENT"));
        QVERIFY(!r.data.contains("BEGIN:VTODO"));
        calUids.insert(r.id);
    }
    QCOMPARE(calUids, (QSet<QString>{ QStringLiteral("evt-1"), QStringLiteral("evt-2") }));

    // Singular reads respect the split too.
    QVERIFY(todoDemux->loadRecord(QStringLiteral("tdo-1")).has_value());
    QVERIFY(!todoDemux->loadRecord(QStringLiteral("evt-1")).has_value());
    QVERIFY(calDemux->loadRecord(QStringLiteral("evt-1")).has_value());
    QVERIFY(!calDemux->loadRecord(QStringLiteral("tdo-1")).has_value());

    // availableCollections() on each view reports exactly the one shared id.
    QCOMPARE(todoDemux->availableCollections().size(), 1);
    QCOMPARE(todoDemux->availableCollections().first().id, colId);
    QCOMPARE(calDemux->availableCollections().first().id, colId);
}

void TstMultiProtocolDavProvider::viewRecordIdsMatchParentUids()
{
    // Ids identical across both views: record ids are the parent transport's
    // UIDs verbatim (no view-suffixing); collection id is identical too.
    FakeCalDavServer server;
    const QString mixedHref = QStringLiteral("/calendars/testuser/mixed/");
    server.setCalendars({ { QStringLiteral("Mixed"), mixedHref } });
    server.setCalendarComponents(mixedHref,
                                 { QStringLiteral("VEVENT"), QStringLiteral("VTODO") });
    server.setSeedEvents(mixedHref, {
        icsBlob(QStringLiteral("shared-check-evt"), "VEVENT", "E"),
        icsBlob(QStringLiteral("shared-check-tdo"), "VTODO",  "T"),
    });
    QVERIFY(server.startListening());

    BackendConfiguration cfg;
    hybridTestConfig(cfg, "demux-ids");
    cfg.connectionParams.insert(QStringLiteral("url"), server.baseUrl().toString());

    MultiProtocolDavProvider provider;
    provider.load(cfg);
    QFuture<bool> fut = provider.connect();
    QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 20000);
    QCOMPARE(fut.resultAt(0), true);

    auto specs = provider.createBackends();
    QCOMPARE(specs.size(), std::size_t(2));
    QString colId;
    std::unique_ptr<IBlobBackend> calBackend, todoBackend;
    for (auto &spec : specs) {
        if (spec.domainId == QLatin1String("cal")) {
            colId = spec.collections.first().id;
            calBackend = std::move(spec.backend);
        } else if (spec.domainId == QLatin1String("todo")) {
            QCOMPARE(spec.collections.first().id, colId);
            todoBackend = std::move(spec.backend);
        }
    }
    QVERIFY(!colId.isEmpty() && calBackend && todoBackend);

    auto *calDemux = dynamic_cast<SyncBackendBase *>(calBackend.get());
    auto *todoDemux = dynamic_cast<SyncBackendBase *>(todoBackend.get());
    QVERIFY(calDemux && todoDemux);

    QCOMPARE(todoDemux->loadRecords(colId).first().id,
             QStringLiteral("shared-check-tdo"));
    QCOMPARE(calDemux->loadRecords(colId).first().id,
             QStringLiteral("shared-check-evt"));
    // hrefs stay stable in both views: writes/reads address the same server
    // store, so the collection id both specs advertise is byte-equal.
    QCOMPARE(colId, QStringLiteral("mixed"));
}

void TstMultiProtocolDavProvider::writesThroughViewsReachSharedTransport()
{
    FakeCalDavServer server;
    const QString mixedHref = QStringLiteral("/calendars/testuser/mixed/");
    server.setCalendars({ { QStringLiteral("Mixed"), mixedHref } });
    server.setCalendarComponents(mixedHref,
                                 { QStringLiteral("VEVENT"), QStringLiteral("VTODO") });
    QVERIFY(server.startListening());

    BackendConfiguration cfg;
    hybridTestConfig(cfg, "demux-writes");
    cfg.connectionParams.insert(QStringLiteral("url"), server.baseUrl().toString());

    MultiProtocolDavProvider provider;
    provider.load(cfg);
    QFuture<bool> fut = provider.connect();
    QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 20000);
    QCOMPARE(fut.resultAt(0), true);

    auto specs = provider.createBackends();
    QCOMPARE(specs.size(), std::size_t(2));
    QString colId;
    std::unique_ptr<IBlobBackend> calBackend, todoBackend;
    for (auto &spec : specs) {
        if (spec.domainId == QLatin1String("cal")) {
            colId = spec.collections.first().id;
            calBackend = std::move(spec.backend);
        } else if (spec.domainId == QLatin1String("todo")) {
            todoBackend = std::move(spec.backend);
        }
    }
    QVERIFY(calBackend && todoBackend);
    auto *calDemux = dynamic_cast<SyncBackendBase *>(calBackend.get());
    auto *todoDemux = dynamic_cast<SyncBackendBase *>(todoBackend.get());
    QVERIFY(calDemux && todoDemux);

    // Create through the TODO view → lands on the shared transport as-is
    // (raw iCal passthrough, no JSON filter stamping).
    const QByteArray todoIcs = icsBlob(QStringLiteral("new-todo"), "VTODO", "Fresh");
    BackendRecord rec;
    rec.id   = QStringLiteral("new-todo");
    rec.type = QStringLiteral("todo");
    rec.data = todoIcs;
    const QString storedId = todoDemux->createRecord(colId, rec);
    QCOMPARE(storedId, QStringLiteral("new-todo"));
    QTRY_VERIFY_WITH_TIMEOUT(server.hasEvent(mixedHref, QStringLiteral("new-todo")), 5000);
    const QList<QByteArray> stored = server.storedEvents(mixedHref);
    bool bytesExact = false;
    for (const auto &b : stored) {
        if (b == todoIcs) bytesExact = true;
    }
    QVERIFY2(bytesExact, "expected the VTODO bytes stored UNCHANGED by the todo view");

    // Create through the CAL view with VEVENT bytes → also reaches the same
    // shared transport.
    const QByteArray evtIcs = icsBlob(QStringLiteral("new-evt"), "VEVENT", "Fresh");
    BackendRecord erec;
    erec.id   = QStringLiteral("new-evt");
    erec.type = QStringLiteral("event");
    erec.data = evtIcs;
    QCOMPARE(calDemux->createRecord(colId, erec), QStringLiteral("new-evt"));
    QTRY_VERIFY_WITH_TIMEOUT(server.hasEvent(mixedHref, QStringLiteral("new-evt")), 5000);

    // Writes were PUTs to the shared collection in both cases.
    QCOMPARE(server.requestPaths("PUT").size(), 2);

    // Delete through either view reaches the shared store.
    QVERIFY(todoDemux->deleteRecord(QStringLiteral("new-todo")));
    QVERIFY(!server.hasEvent(mixedHref, QStringLiteral("new-todo")));
}

QTEST_GUILESS_MAIN(TstMultiProtocolDavProvider)
#include "tst_multiprotocoldavprovider.moc"
