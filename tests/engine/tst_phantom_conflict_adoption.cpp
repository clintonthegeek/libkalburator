// Sync-excellence campaign E8 (FINDINGS O28) — post-crash phantom-conflict
// adoption.
//
// Real end-to-end wiring: LocalBackend (source, real .ics files written
// directly — simulating a user who just added N new local items) ->
// SyncEngine::runSync -> RemoteCalendarBackend (target) -> FakeCalDavServer,
// through a real SyncMapping/SyncRequest — no mocks on the diff/merge/
// baseline path. Mirrors tst_sync_convergence.cpp's fixture shape but with
// source/target swapped so a LOCAL create batch is what gets pushed and can
// be interrupted mid-push.
//
// The bug this pins (FINDINGS O28, found live by the CP-C/H8 kill-Radicale-
// mid-push check): a partial push + server crash leaves N same-UID pairs
// with NO baseline (the failed mapping correctly persisted nothing per O17)
// and byte-DIFFERENT content (local = original file bytes, remote =
// engine-serialized copy — PRODID/property order differ, so raw hashes
// differ even though the two sides are the SAME logical event). Every
// subsequent cycle re-diffs, re-declares N phantom conflicts, and fails the
// mapping indefinitely until a human resolves them. The fix: in the
// no-baseline branch, a CANONICAL equality check (already present in
// perrecorddiff.cpp's semanticallyEqual, wired to the domain's
// createCanonicalDiffer()) must gate whether the pair is adopted silently
// (write each side's own contentHash as baseline, no conflict) instead of
// declared Conflict.

#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QFutureWatcher>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <KCalendarCore/Incidence>
#include <KCalendarCore/MemoryCalendar>

#include <memory>

#include "backendconfiguration.h"
#include "backendrecord.h"
#include "backendregistry.h"
#include "baselinestore.h"
#include "caldavprovider.h"
#include "collectioninfo.h"
#include "fakecaldavserver.h"
#include "iblobbackend.h"
#include "isynchost.h"
#include "localbackend.h"
#include "lossprofile.h"
#include "pluginmanager.h"
#include "remotecalendarbackend.h"
#include "shape.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "syncbackend.h"
#include "syncengine.h"
#include "syncrequest.h"
#include "synctypes.h"

using Kalburator::Sync::BackendConfiguration;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::BackendRegistry;
using Kalburator::Storage::BaselineStore;
using Kalburator::Sync::CalDavProvider;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::ConflictResolution;
using Kalburator::Sync::IBlobBackend;
using Kalburator::Sync::ISyncConfigStore;
using Kalburator::Sync::ISyncHost;
using Kalburator::Sync::LocalBackend;
using Kalburator::Sync::RemoteCalendarBackend;
using Kalburator::Sync::SyncBackend;
using Kalburator::Engine::SyncEngine;
using Kalburator::Engine::SyncRequest;
using Kalburator::Sync::SyncMapping;
using Kalburator::Sync::SyncMode;
using Kalburator::Sync::SyncResult;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::Shape;

namespace {

constexpr int kSyncTimeoutMs = 10000;

class CapturingSyncHost final : public ISyncHost
{
public:
    explicit CapturingSyncHost(BackendRegistry *registry) : m_registry(registry) {}

    SyncBackend *backendById(const QString &id) override
    {
        return m_registry ? static_cast<SyncBackend*>(m_registry->backendInstance(id)) : nullptr;
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
    void syncStarted(const QString &, const LossProfile &) override {}
    void recordChanged(const QString &, const QString &, ChangeKind) override {}

private:
    BackendRegistry *m_registry = nullptr;
};

bool waitForFutureBool(QFuture<bool> f, int timeoutMs = 5000)
{
    if (f.isFinished()) return true;
    QFutureWatcher<bool> w;
    QSignalSpy doneSpy(&w, &QFutureWatcher<bool>::finished);
    w.setFuture(f);
    if (f.isFinished()) return true;
    return doneSpy.wait(timeoutMs);
}

QByteArray makeVEventOriginalBytes(const QString &uid, const QString &summary)
{
    // Deliberately shaped like a "foreign" file a real user/other app would
    // have written directly to the local mirror dir: a different PRODID and
    // property order than what RemoteCalendarBackend's own KCalendarCore
    // round-trip serialization would produce for the same logical event —
    // exactly the O28 scenario ("local = original bytes, remote = engine-
    // serialized copy; PRODID/property order differ").
    QByteArray v;
    v += "BEGIN:VCALENDAR\r\n";
    v += "PRODID:-//SomeOtherApp//NONSGML v1.0//EN\r\n";
    v += "VERSION:2.0\r\n";
    v += "BEGIN:VEVENT\r\n";
    v += "DTSTART:20260705T090000Z\r\n";
    v += "DTEND:20260705T100000Z\r\n";
    v += "UID:" + uid.toUtf8() + "\r\n";
    v += "SUMMARY:" + summary.toUtf8() + "\r\n";
    v += "DTSTAMP:20260701T120000Z\r\n";
    v += "END:VEVENT\r\n";
    v += "END:VCALENDAR\r\n";
    return v;
}

// Minimal in-memory SyncBackend with a configurable Shape, used by test (c)
// to exercise a domain with NO canonical transcode pipeline (blob) — the
// engine's real MockBackend (src/calendar/mockbackend.h) is hardcoded to
// parse/serialize iCal internally and cannot represent a genuinely
// non-calendar domain. Mirrors ShapedTestBackend in
// tst_engine_unified_routing.cpp / tst_engine_universal_sink_dispatch.cpp;
// duplicated here (not shared) because the engine-test target links no
// stub library that carries it.
class ShapedTestBackend final : public SyncBackend
{
    Q_OBJECT
public:
    ShapedTestBackend(const QString &id, Shape shape, const QString &collectionId,
                      QObject *parent = nullptr)
        : SyncBackend(parent), m_id(id), m_shape(std::move(shape))
    {
        CollectionInfo info;
        info.id   = collectionId;
        info.name = collectionId;
        info.type = QStringLiteral("blob");
        m_collections.append(info);
    }

    QString backendType() const override { return m_id; }
    QList<Shape> nativeShapes() const override { return { m_shape }; }
    QString backendId()   const override { return m_id; }
    QString displayName() const override { return m_id; }
    bool    isAvailable() const override { return true; }

    QList<CollectionInfo> availableCollections() override { return m_collections; }
    CollectionInfo collectionInfo(const QString &id) override
    {
        for (const auto &c : m_collections)
            if (c.id == id) return c;
        return {};
    }
    QString createCollection(const CollectionInfo &info) override
    {
        m_collections.append(info);
        return info.id;
    }

    QList<BackendRecord> loadRecords(const QString &) override { return m_records; }
    std::optional<BackendRecord> loadRecord(const QString &id) override
    {
        for (const auto &r : m_records)
            if (r.id == id) return r;
        return std::nullopt;
    }
    QString createRecord(const QString &, const BackendRecord &record) override
    {
        m_records.append(record);
        return record.id;
    }
    bool updateRecord(const BackendRecord &record) override
    {
        for (auto &r : m_records) {
            if (r.id == record.id) { r = record; return true; }
        }
        return false;
    }
    bool deleteRecord(const QString &id) override
    {
        for (int i = 0; i < m_records.size(); ++i) {
            if (m_records[i].id == id) { m_records.removeAt(i); return true; }
        }
        return false;
    }

    QList<BackendRecord> modifiedSince(const QString &, const QDateTime &) override { return {}; }
    QStringList deletedSince(const QString &, const QDateTime &) override { return {}; }
    bool supportsDeleteTracking() const override { return true; }

    // Calendar pure-virtuals — never reached (blob domain routes through
    // the generic blob dispatch path, not dispatchCalendarLegacy).
    void loadCalendars(const QString &) override {}
    void storeCalendars(const QString &,
                        const QList<KCalendarCore::MemoryCalendar *> &) override {}
    void startSync(const QString &,
                   KCalendarCore::MemoryCalendar *,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QMap<QString, QString> &) override {}
    void removeItem(const QString &, const QString &) override {}

    void seedRecord(const BackendRecord &record) { m_records.append(record); }

private:
    QString m_id;
    Shape m_shape;
    QList<CollectionInfo> m_collections;
    QList<BackendRecord> m_records;
};

} // namespace

class TstPhantomConflictAdoption : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // RED (a): the H8/O28 crash replay. Push N local creates, kill the fake
    // after k succeed, restart the fake, run the next cycle: today this
    // produces k phantom Conflict ops (no baseline exists for the k ids
    // that landed before the crash) -> mapping success == false. After the
    // fix: zero conflicts, all N present both sides, baselines adopted,
    // mapping success == true.
    void crashMidPush_nextCycleAdoptsSilently_noPhantomConflicts();

    // RED (b): guard against over-adoption. Two sides independently seeded
    // with the SAME uid but genuinely DIFFERENT content, no baseline —
    // must still conflict, not adopt.
    void noBaselineGenuinelyDifferentContent_stillConflicts();

    // RED (c): domain-neutrality. A blob-domain pair (no canonical
    // transcode pipeline — RecordDifferBlob::equal is literal byte
    // equality) with no baseline and byte-different content must keep
    // today's behavior: conflict, never adopt (blob has no canonical form
    // to judge "semantically equal" from; adopting there would guess).
    void blobDomainNoBaselineByteDifferent_stillConflicts_neverAdopts();

private:
    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;
};

void TstPhantomConflictAdoption::initTestCase()
{
    Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
    Kalburator::registerStockPlugins(pm);
}

void TstPhantomConflictAdoption::crashMidPush_nextCycleAdoptsSilently_noPhantomConflicts()
{
    const int N = 10; // total new local items
    const int K = 4;  // items that land on the "server" before it dies

    // ── Fake CalDAV server: empty collection, will die after K item writes.

    FakeCalDavServer server;
    const QString href = QStringLiteral("/calendars/testuser/personal/");
    server.setCalendarComponents(href, { QStringLiteral("VEVENT") });
    QVERIFY(server.startListening());
    server.setDieAfterNWrites(K);

    // ── Real CalDavProvider -> RemoteCalendarBackend as TARGET.

    BackendConfiguration cfg;
    cfg.id          = QStringLiteral("phantom-conflict-account");
    cfg.type        = QStringLiteral("caldav");
    cfg.displayName = QStringLiteral("Fake CalDAV (E8/O28)");
    cfg.connectionParams.insert(QStringLiteral("url"),      server.baseUrl().toString());
    cfg.connectionParams.insert(QStringLiteral("username"), QStringLiteral("testuser"));
    cfg.connectionParams.insert(QStringLiteral("password"), QStringLiteral("testpass"));

    CalDavProvider provider;
    provider.load(cfg);
    QVERIFY(waitForFutureBool(provider.connect(), 5000));
    QVERIFY(provider.isConnected());

    const auto cols = provider.collections();
    QVERIFY(!cols.isEmpty());
    const QString collId = cols.first().id;

    std::unique_ptr<IBlobBackend> rawRemote = provider.createBackend(collId);
    QVERIFY(rawRemote != nullptr);
    auto *remote = dynamic_cast<RemoteCalendarBackend *>(rawRemote.get());
    QVERIFY(remote != nullptr);
    const QString remoteBackendId = remote->backendId();

    QTemporaryDir remoteStateDir;
    QVERIFY(remoteStateDir.isValid());
    remote->setDbPath(remoteStateDir.filePath(QStringLiteral("ctags.db")));
    remote->setCacheDir(remoteStateDir.path());

    // ── Real LocalBackend as SOURCE — N .ics files written directly, as if
    //    a user (or another app) just dropped them into the local mirror.

    QTemporaryDir localDir;
    QVERIFY(localDir.isValid());
    LocalBackend local(localDir.path());
    const QString localBackendId = local.backendId();
    const QString localCollectionId = QStringLiteral("mirror");
    {
        CollectionInfo info;
        info.id   = localCollectionId;
        info.name = localCollectionId;
        info.type = QStringLiteral("calendar");
        QVERIFY(!local.createCollection(info).isEmpty());
    }
    QTemporaryDir fingerprintDir;
    QVERIFY(fingerprintDir.isValid());
    local.setDbPath(fingerprintDir.filePath(QStringLiteral("fingerprints.db")));

    const QString mirrorDir = QDir(localDir.path()).filePath(localCollectionId);
    QStringList uids;
    for (int i = 0; i < N; ++i) {
        const QString uid = QStringLiteral("phantom-%1").arg(i);
        uids << uid;
        const QString path = QDir(mirrorDir).filePath(uid + QStringLiteral(".ics"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray bytes = makeVEventOriginalBytes(
            uid, QStringLiteral("Phantom Item %1").arg(i));
        QCOMPARE(f.write(bytes), qint64(bytes.size()));
    }

    // ── Wire the engine. AskUser + Unmonitored means any Conflict op that
    //    IS declared gets deferred into SyncResult::unresolvedConflicts
    //    rather than silently auto-resolved (LastWriteWins et al. would mask
    //    the phantom-conflict symptom this test exists to catch).

    BackendRegistry registry;
    registry.registerBackendInstance(localBackendId,  &local);
    registry.registerBackendInstance(remoteBackendId, remote);

    CapturingSyncHost host(&registry);
    SyncEngine engine(&registry, &host, m_shape);
    engine.setSkipUnchangedMappings(false);

    QTemporaryDir baselineDir;
    QVERIFY(baselineDir.isValid());
    BaselineStore baselines(baselineDir.filePath(QStringLiteral("baselines.db")));
    engine.setBaselineStore(&baselines);

    SyncMapping mapping;
    mapping.id             = QStringLiteral("phantom-conflict-mapping");
    mapping.sourceBackend  = localBackendId;
    mapping.sourceCalendar = localCollectionId;
    mapping.targetBackend  = remoteBackendId;
    mapping.targetCalendar = collId;
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::AskUser;
    mapping.enabled        = true;
    engine.setSyncMappings({ mapping });

    auto runOnce = [&]() -> SyncResult {
        SyncRequest req;
        req.mappingIds = { mapping.id };
        req.behavior = SyncEngine::SyncBehavior::Unmonitored;
        auto future = engine.runSync(req);
        QDeadlineTimer deadline(kSyncTimeoutMs);
        while (!future.isFinished() && !deadline.hasExpired())
            QTest::qWait(10);
        if (!future.isFinished()) {
            SyncResult timedOut;
            timedOut.success = false;
            timedOut.errorMessage = QStringLiteral("sync did not finish within timeout");
            return timedOut;
        }
        return future.resultAt(0).first();
    };

    // ── Cycle 1: push N creates. The fake dies after K writes succeed; the
    //    rest fail (connection refused — the dead-server shape). Writes can
    //    fire concurrently (E5.1's op queue serializes OPERATIONS per
    //    collection, not the individual jobs a single write operation's
    //    batch may issue in parallel), so the exact number that lands
    //    before the listening socket closes can be >= K, never < K, and is
    //    read back from the server rather than assumed — the point of this
    //    test is "some non-empty, non-total subset survived a crash", not a
    //    precise count. Per O17, the failed mapping must persist no
    //    baseline for ANY record.

    const SyncResult r1 = runOnce();
    QVERIFY2(!r1.success, "cycle 1 must fail: not every create could reach the dead server");

    QStringList survivorUids;
    for (const QString &uid : uids) {
        if (server.hasEvent(href, uid))
            survivorUids << uid;
    }
    QVERIFY2(!survivorUids.isEmpty(), "expected at least one create to land before the crash");
    QVERIFY2(survivorUids.size() < N, "expected at least one create to be interrupted by the crash");

    // ── Simulate the operator restarting the crashed server process on the
    //    same address (a real restart binds the same port).

    QVERIFY(server.reviveOnSamePort());

    // ── Cycle 2: the repair cycle. The K already-landed records now exist
    //    on BOTH sides with NO baseline and byte-different (but canonically
    //    identical) content — exactly O28's phantom-conflict shape.

    const SyncResult r2 = runOnce();

    QVERIFY2(r2.unresolvedConflicts.isEmpty(),
             qUtf8Printable(QStringLiteral("expected zero phantom conflicts, got %1")
                                 .arg(r2.unresolvedConflicts.size())));
    QVERIFY2(r2.success, qUtf8Printable(r2.errorMessage));

    // All N items now present on both sides.
    QCOMPARE(server.storedEvents(href).size(), N);
    QDir dir(mirrorDir);
    QCOMPARE(dir.entryList({ QStringLiteral("*.ics") }, QDir::Files).size(), N);
    for (const QString &uid : uids)
        QVERIFY2(server.hasEvent(href, uid), qUtf8Printable(uid));

    // Baselines were adopted for the records that survived the crash (the
    // ones that could have phantom-conflicted) — each side's OWN hash, per
    // the B4/N2 per-side baseline machinery.
    const auto hashes = baselines.baselineHashesForMappingV4(mapping.id);
    QHash<QString, BaselineStore::BaselineHashes> byId;
    for (const auto &h : hashes) byId.insert(h.recordId, h);
    for (const QString &uid : survivorUids) {
        QVERIFY2(byId.contains(uid),
                 qUtf8Printable(QStringLiteral("expected an adopted baseline for %1").arg(uid)));
        QVERIFY(!byId.value(uid).sourceHash.isEmpty());
        QVERIFY(!byId.value(uid).targetHash.isEmpty());
    }

    // ── Cycle 3: nothing changed since cycle 2 — must be a hard no-op (the
    //    adopted baselines must actually stick, not just paper over cycle
    //    2's diff).

    const int putsAfterCycle2 = server.requestCount(QByteArrayLiteral("PUT"));
    const SyncResult r3 = runOnce();
    QVERIFY2(r3.success, qUtf8Printable(r3.errorMessage));
    QVERIFY(r3.unresolvedConflicts.isEmpty());
    QCOMPARE(server.requestCount(QByteArrayLiteral("PUT")), putsAfterCycle2);
    QCOMPARE(server.storedEvents(href).size(), N);
}

void TstPhantomConflictAdoption::noBaselineGenuinelyDifferentContent_stillConflicts()
{
    // Both sides pre-populated independently (no sync has ever run — no
    // baseline exists), same uid, genuinely different SUMMARY. Nothing here
    // is byte-formatting noise: a real canonical transcode of either side
    // never produces the other's content, so this must conflict exactly as
    // it does today.

    FakeCalDavServer server;
    const QString href = QStringLiteral("/calendars/testuser/personal/");
    server.setCalendarComponents(href, { QStringLiteral("VEVENT") });
    const QString uid = QStringLiteral("diff-1");
    server.setSeedEvents(href, { makeVEventOriginalBytes(uid, QStringLiteral("Remote Version")) });
    QVERIFY(server.startListening());

    BackendConfiguration cfg;
    cfg.id          = QStringLiteral("phantom-conflict-b-account");
    cfg.type        = QStringLiteral("caldav");
    cfg.displayName = QStringLiteral("Fake CalDAV (E8/O28 guard)");
    cfg.connectionParams.insert(QStringLiteral("url"),      server.baseUrl().toString());
    cfg.connectionParams.insert(QStringLiteral("username"), QStringLiteral("testuser"));
    cfg.connectionParams.insert(QStringLiteral("password"), QStringLiteral("testpass"));

    CalDavProvider provider;
    provider.load(cfg);
    QVERIFY(waitForFutureBool(provider.connect(), 5000));
    QVERIFY(provider.isConnected());

    const auto cols = provider.collections();
    QVERIFY(!cols.isEmpty());
    const QString collId = cols.first().id;

    std::unique_ptr<IBlobBackend> rawRemote = provider.createBackend(collId);
    QVERIFY(rawRemote != nullptr);
    auto *remote = dynamic_cast<RemoteCalendarBackend *>(rawRemote.get());
    QVERIFY(remote != nullptr);
    const QString remoteBackendId = remote->backendId();

    QTemporaryDir remoteStateDir;
    QVERIFY(remoteStateDir.isValid());
    remote->setDbPath(remoteStateDir.filePath(QStringLiteral("ctags.db")));
    remote->setCacheDir(remoteStateDir.path());

    QTemporaryDir localDir;
    QVERIFY(localDir.isValid());
    LocalBackend local(localDir.path());
    const QString localBackendId = local.backendId();
    const QString localCollectionId = QStringLiteral("mirror");
    {
        CollectionInfo info;
        info.id   = localCollectionId;
        info.name = localCollectionId;
        info.type = QStringLiteral("calendar");
        QVERIFY(!local.createCollection(info).isEmpty());
    }
    QTemporaryDir fingerprintDir;
    QVERIFY(fingerprintDir.isValid());
    local.setDbPath(fingerprintDir.filePath(QStringLiteral("fingerprints.db")));

    const QString mirrorDir = QDir(localDir.path()).filePath(localCollectionId);
    {
        const QString path = QDir(mirrorDir).filePath(uid + QStringLiteral(".ics"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray bytes = makeVEventOriginalBytes(uid, QStringLiteral("Local Version"));
        QCOMPARE(f.write(bytes), qint64(bytes.size()));
    }

    BackendRegistry registry;
    registry.registerBackendInstance(localBackendId,  &local);
    registry.registerBackendInstance(remoteBackendId, remote);

    CapturingSyncHost host(&registry);
    SyncEngine engine(&registry, &host, m_shape);
    engine.setSkipUnchangedMappings(false);

    QTemporaryDir baselineDir;
    QVERIFY(baselineDir.isValid());
    BaselineStore baselines(baselineDir.filePath(QStringLiteral("baselines.db")));
    engine.setBaselineStore(&baselines);

    SyncMapping mapping;
    mapping.id             = QStringLiteral("phantom-conflict-guard-mapping");
    mapping.sourceBackend  = localBackendId;
    mapping.sourceCalendar = localCollectionId;
    mapping.targetBackend  = remoteBackendId;
    mapping.targetCalendar = collId;
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::AskUser;
    mapping.enabled        = true;
    engine.setSyncMappings({ mapping });

    SyncRequest req;
    req.mappingIds = { mapping.id };
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    auto future = engine.runSync(req);
    QDeadlineTimer deadline(kSyncTimeoutMs);
    while (!future.isFinished() && !deadline.hasExpired())
        QTest::qWait(10);
    QVERIFY(future.isFinished());
    const SyncResult r = future.resultAt(0).first();

    QCOMPARE(r.unresolvedConflicts.size(), 1);
    QVERIFY(!r.success);

    // No baseline must have been adopted for a genuinely-different pair.
    const auto hashes = baselines.baselineHashesForMappingV4(mapping.id);
    for (const auto &h : hashes)
        QVERIFY2(h.recordId != uid,
                 "a genuinely-different no-baseline pair must never adopt a baseline");
}

void TstPhantomConflictAdoption::blobDomainNoBaselineByteDifferent_stillConflicts_neverAdopts()
{
    const Shape blobShape{ DomainId{QStringLiteral("blob")}, EncodingId{QStringLiteral("raw")} };
    const QString collId = QStringLiteral("blob-collection");

    ShapedTestBackend source(QStringLiteral("blob-source"), blobShape, collId);
    ShapedTestBackend target(QStringLiteral("blob-target"), blobShape, collId);

    BackendRecord srcRec;
    srcRec.id   = QStringLiteral("blob-1");
    srcRec.type = QStringLiteral("memo");
    srcRec.data = QByteArrayLiteral("source bytes AAAA");
    srcRec.contentHash = QStringLiteral("hash-source-AAAA");
    srcRec.lastModified = QDateTime::currentDateTimeUtc();
    source.seedRecord(srcRec);

    BackendRecord tgtRec;
    tgtRec.id   = QStringLiteral("blob-1");
    tgtRec.type = QStringLiteral("memo");
    tgtRec.data = QByteArrayLiteral("target bytes BBBB");
    tgtRec.contentHash = QStringLiteral("hash-target-BBBB");
    tgtRec.lastModified = QDateTime::currentDateTimeUtc();
    target.seedRecord(tgtRec);

    BackendRegistry registry;
    registry.registerBackendInstance(source.backendId(), &source);
    registry.registerBackendInstance(target.backendId(), &target);

    CapturingSyncHost host(&registry);
    SyncEngine engine(&registry, &host, m_shape);
    engine.setSkipUnchangedMappings(false);

    QTemporaryDir baselineDir;
    QVERIFY(baselineDir.isValid());
    BaselineStore baselines(baselineDir.filePath(QStringLiteral("baselines.db")));
    engine.setBaselineStore(&baselines);

    SyncMapping mapping;
    mapping.id             = QStringLiteral("blob-no-baseline-mapping");
    mapping.sourceBackend  = source.backendId();
    mapping.sourceCalendar = collId;
    mapping.targetBackend  = target.backendId();
    mapping.targetCalendar = collId;
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::AskUser;
    mapping.enabled        = true;
    engine.setSyncMappings({ mapping });

    SyncRequest req;
    req.mappingIds = { mapping.id };
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    auto future = engine.runSync(req);
    QDeadlineTimer deadline(kSyncTimeoutMs);
    while (!future.isFinished() && !deadline.hasExpired())
        QTest::qWait(10);
    QVERIFY(future.isFinished());
    const SyncResult r = future.resultAt(0).first();

    QCOMPARE(r.unresolvedConflicts.size(), 1);
    QVERIFY(!r.success);

    const auto hashes = baselines.baselineHashesForMappingV4(mapping.id);
    for (const auto &h : hashes)
        QVERIFY2(h.recordId != srcRec.id,
                 "a blob-domain byte-different no-baseline pair must never adopt "
                 "a baseline — blob has no canonical form to judge equality from");
}

QTEST_MAIN(TstPhantomConflictAdoption)
#include "tst_phantom_conflict_adoption.moc"
