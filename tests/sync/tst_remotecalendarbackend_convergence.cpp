// v0.63 — RemoteCalendarBackend convergence tests.
//
// Two coordinated fixes land together here (see
// docs/2026-06-03-caldav-discovery-redundancy-handoff.md and PlanStan's
// docs/handoffs/2026-05-27-libkalburator-content-cache.md):
//
//   1. Discovery primer. CalDavProvider / MultiProtocolDavProvider seed each
//      RemoteCalendarBackend from connect-time discovery via primeCalendars(),
//      so the per-backend loadCalendars() short-circuits its server-wide
//      PROPFIND. We assert ZERO additional PROPFINDs past the connect-time walk
//      and that a primed backend emits calendarDiscovered for exactly its bound
//      calendar.
//
//   2. Content-cache filename determinism. The cache file name must be stable
//      across launches (was qHash(host+path), which mixes a per-process random
//      seed) and must honor setCacheDir().

#include <QtTest/QtTest>
#include <QFutureWatcher>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QUrl>

#include "fakecaldavserver.h"

#include "backendconfiguration.h"
#include "caldavprovider.h"
#include "collectioninfo.h"
#include "iblobbackend.h"
#include "multiprotocoldavprovider.h"
#include "remotecalendarbackend.h"
#include "syncbackend.h"
#include "syncoperation.h"

using namespace Kalburator::Sync;

namespace {

bool waitForFutureBool(QFuture<bool> f, int timeoutMs = 5000)
{
    if (f.isFinished()) return true;
    QFutureWatcher<bool> w;
    QSignalSpy doneSpy(&w, &QFutureWatcher<bool>::finished);
    w.setFuture(f);
    if (f.isFinished()) return true;
    return doneSpy.wait(timeoutMs);
}

BackendConfiguration makeConfig(const QUrl &serverUrl)
{
    BackendConfiguration cfg;
    cfg.id = QStringLiteral("test-account");
    cfg.type = QStringLiteral("caldav");
    cfg.displayName = QStringLiteral("Fake Account");
    cfg.connectionParams.insert(QStringLiteral("url"), serverUrl.toString());
    cfg.connectionParams.insert(QStringLiteral("username"), QStringLiteral("testuser"));
    cfg.connectionParams.insert(QStringLiteral("password"), QStringLiteral("testpass"));
    return cfg;
}

QList<QPair<QString, QString>> fiveCalendars()
{
    return {
        { QStringLiteral("Personal"), QStringLiteral("/calendars/testuser/personal/") },
        { QStringLiteral("Work"),     QStringLiteral("/calendars/testuser/work/") },
        { QStringLiteral("Family"),   QStringLiteral("/calendars/testuser/family/") },
        { QStringLiteral("Trips"),    QStringLiteral("/calendars/testuser/trips/") },
        { QStringLiteral("Shared"),   QStringLiteral("/calendars/testuser/shared/") },
    };
}

// Same FNV-1a the backend uses for the content-cache filename. Re-derived here
// so the test pins the *stable* hash and fails if anyone reverts to qHash().
quint64 fnv1a(const QString &s)
{
    const QByteArray bytes = s.toUtf8();
    quint64 hash = 1469598103934665603ULL;
    for (const char ch : bytes) {
        hash ^= static_cast<quint64>(static_cast<unsigned char>(ch));
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace

class TstRemoteCalendarBackendConvergence : public QObject
{
    Q_OBJECT
private slots:
    void primed_loadCalendars_issues_zero_additional_propfinds();
    void primed_backend_emits_exactly_its_bound_calendar();
    void multiproto_primed_loadCalendars_issues_zero_additional_propfinds();
    void unprimed_loadCalendars_falls_back_to_network_propfind();
    void content_cache_filename_is_deterministic_and_honors_setCacheDir();
};

void TstRemoteCalendarBackendConvergence::primed_loadCalendars_issues_zero_additional_propfinds()
{
    FakeCalDavServer server;
    server.setCalendars(fiveCalendars());
    QVERIFY(server.startListening());

    CalDavProvider provider;
    provider.load(makeConfig(server.baseUrl()));
    QFuture<bool> fut = provider.connect();
    QVERIFY(waitForFutureBool(fut));
    QCOMPARE(fut.result(), true);

    // Connect performed exactly one discovery walk. We don't pin the walk's
    // length, only that it happened and is the *only* PROPFIND source.
    const int propfindsAfterConnect = server.requestCount("PROPFIND");
    QVERIFY2(propfindsAfterConnect > 0, "discovery walk should issue PROPFINDs");

    const auto cols = provider.collections();
    QCOMPARE(cols.size(), 5);

    // Open backends for two of the five and load their calendars.
    for (int i : { 0, 3 }) {
        const QString collId = cols.at(i).id;
        auto backend = provider.createBackend(collId);
        QVERIFY(backend != nullptr);
        auto *remote = dynamic_cast<RemoteCalendarBackend *>(backend.get());
        QVERIFY(remote != nullptr);

        QSignalSpy finishedSpy(remote, SIGNAL(loadCalendarsFinished(QString, bool, QString)));
        remote->loadCalendars(collId);
        // Primed path is synchronous — finished should already have fired.
        QCOMPARE(finishedSpy.count(), 1);
        QCOMPARE(finishedSpy.first().at(1).toBool(), true);
    }

    // The whole point: opening + loading backends added NO PROPFINDs.
    QCOMPARE(server.requestCount("PROPFIND"), propfindsAfterConnect);
}

void TstRemoteCalendarBackendConvergence::primed_backend_emits_exactly_its_bound_calendar()
{
    FakeCalDavServer server;
    server.setCalendars(fiveCalendars());
    QVERIFY(server.startListening());

    CalDavProvider provider;
    provider.load(makeConfig(server.baseUrl()));
    QVERIFY(waitForFutureBool(provider.connect()));

    const auto cols = provider.collections();
    QCOMPARE(cols.size(), 5);
    const QString boundId = cols.first().id;  // for plain CalDav, id == discovery key

    auto backend = provider.createBackend(boundId);
    QVERIFY(backend != nullptr);
    auto *remote = dynamic_cast<RemoteCalendarBackend *>(backend.get());
    QVERIFY(remote != nullptr);

    QSignalSpy discoveredSpy(remote, SIGNAL(calendarDiscovered(QString, QString)));
    remote->loadCalendars(boundId);

    // Exactly one calendar surfaces — the bound one — not all five on the server.
    QCOMPARE(discoveredSpy.count(), 1);
    QCOMPARE(discoveredSpy.first().at(0).toString(), boundId);
    QCOMPARE(discoveredSpy.first().at(1).toString(), boundId);
}

void TstRemoteCalendarBackendConvergence::multiproto_primed_loadCalendars_issues_zero_additional_propfinds()
{
    // Same guarantee for MultiProtocolDavProvider, whose collectionId is prefixed
    // (multiproto-dav:<id>:cal:<innerKey>) and must be unprefixed to reach the
    // discovery caps. One base URL speaks both protocols; we point a CalDav-only
    // fake at it and force the CardDav half to fail fast (bogus principal), so the
    // provider still connects via CalDav (anyOk = calOk || cardOk).
    FakeCalDavServer server;
    server.setCalendars(fiveCalendars());
    QVERIFY(server.startListening());

    BackendConfiguration cfg;
    cfg.id = QStringLiteral("mpdav-test");
    cfg.type = QStringLiteral("multiproto-dav");
    cfg.connectionParams.insert(QStringLiteral("url"), server.baseUrl().toString());
    cfg.connectionParams.insert(QStringLiteral("username"), QStringLiteral("testuser"));
    cfg.connectionParams.insert(QStringLiteral("password"), QStringLiteral("testpass"));
    // Force the CardDAV half straight to a 404 so it fails fast instead of probing.
    cfg.connectionParams.insert(QStringLiteral("manualCarddavPrincipal"),
                                QStringLiteral("/bogus-carddav/"));

    MultiProtocolDavProvider provider;
    provider.load(cfg);

    QFuture<bool> fut = provider.connect();
    QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 20000);
    QCOMPARE(fut.resultAt(0), true);          // connected via the CalDAV half
    QVERIFY(provider.isConnected());

    // Only the calendar collections matter here; CardDAV contributed none.
    QList<CollectionInfo> calCols;
    for (const auto &c : provider.collections()) {
        if (c.id.contains(QStringLiteral(":cal:"))) calCols.append(c);
    }
    QCOMPARE(calCols.size(), 5);

    // Baseline AFTER connect: includes both the CalDAV walk and whatever PROPFINDs
    // the failing CardDAV probe issued. createBackend + loadCalendars must add none.
    const int propfindsAfterConnect = server.requestCount("PROPFIND");
    QVERIFY2(propfindsAfterConnect > 0, "discovery should have issued PROPFINDs");

    const QString calPrefix = QStringLiteral("multiproto-dav:mpdav-test:cal:");
    for (int i : { 0, 4 }) {
        const QString prefixedId = calCols.at(i).id;
        QVERIFY(prefixedId.startsWith(calPrefix));
        const QString innerKey = prefixedId.mid(calPrefix.length());

        auto backend = provider.createBackend(prefixedId);
        QVERIFY(backend != nullptr);
        auto *remote = dynamic_cast<RemoteCalendarBackend *>(backend.get());
        QVERIFY(remote != nullptr);

        QSignalSpy discoveredSpy(remote, SIGNAL(calendarDiscovered(QString, QString)));
        QSignalSpy finishedSpy(remote, SIGNAL(loadCalendarsFinished(QString, bool, QString)));
        remote->loadCalendars(prefixedId);

        // Primed path is synchronous and surfaces exactly the bound calendar,
        // emitted under the prefixed collectionId but with the inner (unprefixed)
        // calendar id — matching the network path's (collectionId, displayName).
        QCOMPARE(finishedSpy.count(), 1);
        QCOMPARE(finishedSpy.first().at(1).toBool(), true);
        QCOMPARE(discoveredSpy.count(), 1);
        QCOMPARE(discoveredSpy.first().at(0).toString(), prefixedId);
        QCOMPARE(discoveredSpy.first().at(1).toString(), innerKey);
    }

    // The whole point: priming made loadCalendars add zero PROPFINDs.
    QCOMPARE(server.requestCount("PROPFIND"), propfindsAfterConnect);
}

void TstRemoteCalendarBackendConvergence::unprimed_loadCalendars_falls_back_to_network_propfind()
{
    // A backend that was never primed (standalone use, tests) must retain the
    // original behavior: walk the server. We prove it does NOT short-circuit by
    // observing a real PROPFIND on the wire.
    FakeCalDavServer server;
    server.setCalendars(fiveCalendars());
    QVERIFY(server.startListening());

    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.registerCalendarUrl(QStringLiteral("Personal"),
                                server.baseUrl().toString() + QStringLiteral("calendars/testuser/personal/"));

    QSignalSpy discoveredSpy(&backend, SIGNAL(calendarDiscovered(QString, QString)));
    backend.loadCalendars(QStringLiteral("Personal"));

    // Network path is asynchronous: nothing is emitted synchronously (unlike the
    // primed fast-path), and a PROPFIND eventually reaches the server.
    QCOMPARE(discoveredSpy.count(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(server.requestCount("PROPFIND") > 0, 5000);
}

void TstRemoteCalendarBackendConvergence::content_cache_filename_is_deterministic_and_honors_setCacheDir()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QUrl url(QStringLiteral("http://127.0.0.1:1/dav/"));  // unreachable; we never spin the loop
    const QString expectedName =
        QStringLiteral("caldav-cache-%1.db").arg(fnv1a(url.host() + url.path()));

    // Two independent instances for the same account must resolve to the SAME
    // cache file under the chosen directory.
    for (int i = 0; i < 2; ++i) {
        RemoteCalendarBackend backend(url, QStringLiteral("u"), QStringLiteral("p"));
        backend.setCacheDir(dir.path());
        backend.registerCalendarUrl(QStringLiteral("cal"), url.toString() + QStringLiteral("cal/"));
        // fetchItems() runs initContentCache() synchronously before deferring the
        // network op, so the DB file exists by the time it returns.
        backend.fetchItems(QStringLiteral("cal"));
        QVERIFY2(QFile::exists(dir.filePath(expectedName)),
                 "cache DB must be created under setCacheDir() with the FNV-1a-derived name");
    }

    // Determinism: exactly one cache file, reused across both instances.
    const auto entries = QDir(dir.path()).entryList({ QStringLiteral("caldav-cache-*.db") }, QDir::Files);
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first(), expectedName);
}

QTEST_GUILESS_MAIN(TstRemoteCalendarBackendConvergence)
#include "tst_remotecalendarbackend_convergence.moc"
