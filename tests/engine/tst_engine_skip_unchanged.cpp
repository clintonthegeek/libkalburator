// Hub-side ChangeDetection — skip-unchanged engine integration test.
//
// Pins the WildPalms efficiency RFC (docs/2026-06-14-hub-changedetection-
// for-skip-response.md): once both sides of a mapping implement
// Sync::ChangeDetection, SyncEngine::prepareSyncFastPath skips an unchanged
// mapping when setSkipUnchangedMappings(true). GenericSqliteBackend stands
// in for the WP hub.
//
// What this test covers:
//   1. The prime path — a real sync records fresh revisions on both sides
//      (engine onWorkerSyncCompleted -> primeRevisionCache); the caches go
//      from empty to populated.
//   2. The skip decision — with both sides' cached revision matching their
//      current content digest, prepareSyncFastPath skips the mapping.
//   3. A source content change defeats the skip on the next pass.
//
// Two facts this test encodes:
//   - The fast path runs only on the multi-mapping driver. A SyncRequest
//     with mappingIds.size()==1 routes to processSingleMapping (no fast
//     path). So every run uses an EMPTY mappingIds (all-enabled ->
//     driveQueue -> prepareSyncFastPath).
//   - The skip is observed via the qInfo line prepareSyncFastPath emits
//     ("SyncEngine: skipping unchanged mapping <id>") — the same observable
//     WP uses on-device (RFC acceptance criterion 3).
//
// Why the skip decision is driven from a primed state rather than by looping
// real syncs to a fixed point: a TwoWay sync between two GenericSqliteBackends
// hits a PRE-EXISTING, ChangeDetection-unrelated quirk — GenericSqliteBackend
// record ids are collection-prefixed (collectionId\x01origId) and createRecord
// stores an incoming (already-encoded) id verbatim, so two sqlite sinks never
// match each other's records and re-create them with ever-growing ids; the
// content digest then never settles. This does not occur in WP's real topology
// (the hub's peer is a Palm/CalDAV backend with stable bare ids). See the
// response doc's "Discovered (out of scope)" note. Priming directly tests the
// engine's skip DECISION — the RFC deliverable — without depending on that.

#include <QtTest/QtTest>
#include <QObject>
#include <QString>
#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFuture>
#include <QMutex>
#include <QStringList>
#include <QTemporaryDir>

#include <KCalendarCore/MemoryCalendar>

#include <memory>

#include "backendrecord.h"
#include "backendregistry.h"
#include "baselinestore.h"
#include "collectioninfo.h"
#include "genericsqlitebackend.h"
#include "isynchost.h"
#include "lossprofile.h"
#include "pluginmanager.h"
#include "shape.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "syncbackend.h"
#include "syncengine.h"
#include "syncrequest.h"
#include "synctypes.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::BackendRegistry;
using Kalburator::Storage::BaselineStore;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::ConflictResolution;
using Kalburator::Sync::ISyncConfigStore;
using Kalburator::Sync::ISyncHost;
using Kalburator::Sync::SyncBackend;
using Kalburator::Engine::SyncEngine;
using Kalburator::Engine::SyncRequest;
using Kalburator::Sync::SyncMapping;
using Kalburator::Sync::SyncMode;
using Kalburator::Sinks::GenericSqliteBackend;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::Shape;

namespace {

constexpr int kSyncTimeoutMs = 30000;

// ---- qInfo capture for the skip log line ----
QMutex        g_logMutex;
QStringList   g_logMessages;
QtMessageHandler g_prevHandler = nullptr;

void captureHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    {
        QMutexLocker lk(&g_logMutex);
        g_logMessages.append(msg);
    }
    if (g_prevHandler)
        g_prevHandler(type, ctx, msg);
}

void clearLog()
{
    QMutexLocker lk(&g_logMutex);
    g_logMessages.clear();
}

bool sawSkipLog(const QString &mappingId)
{
    QMutexLocker lk(&g_logMutex);
    for (const auto &m : g_logMessages)
        if (m.contains(QLatin1String("skipping unchanged mapping"))
            && m.contains(mappingId))
            return true;
    return false;
}

// Minimal ISyncHost over a BackendRegistry (mirrors the one in
// tst_engine_universal_sink_dispatch; duplicated because the engine-test
// target links no shared stub carrying it).
class RegistrySyncHost final : public ISyncHost
{
public:
    explicit RegistrySyncHost(BackendRegistry *registry) : m_registry(registry) {}

    SyncBackend *backendById(const QString &id) override
    {
        return m_registry ? static_cast<SyncBackend*>(m_registry->backendInstance(id))
                          : nullptr;
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

BackendRecord seedRecord(const QString &recordId)
{
    BackendRecord rec;
    rec.id          = recordId;
    rec.type        = QStringLiteral("contact");
    rec.displayName = recordId;
    rec.data =
        "BEGIN:VCARD\r\n"
        "VERSION:4.0\r\n"
        "UID:" + recordId.toUtf8() + "\r\n"
        "FN:Skip Test\r\n"
        "END:VCARD\r\n";
    rec.contentHash = QString::fromLatin1(
        QCryptographicHash::hash(rec.data, QCryptographicHash::Sha1).toHex());
    rec.lastModified = QDateTime::currentDateTimeUtc();
    rec.isDeleted    = false;
    return rec;
}

} // namespace

class TestEngineSkipUnchanged : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void settledMappingSkips_mutationDefeatsSkip();

private:
    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;
};

void TestEngineSkipUnchanged::initTestCase()
{
    // Register the contacts canonical (vcard4) so the engine's unified path
    // can resolve the (identical) source/target shapes.
    Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
    Kalburator::registerStockPlugins(pm);

    g_prevHandler = qInstallMessageHandler(captureHandler);
}

void TestEngineSkipUnchanged::cleanupTestCase()
{
    qInstallMessageHandler(g_prevHandler);
    g_prevHandler = nullptr;
}

void TestEngineSkipUnchanged::settledMappingSkips_mutationDefeatsSkip()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    const Shape vcard4{ DomainId{"contacts"}, EncodingId{"vcard4"} };

    const QString sourceBackendId  = QStringLiteral("sqlite-source");
    const QString targetBackendId  = QStringLiteral("sqlite-target");
    const QString sourceCollection = QStringLiteral("src+contacts");
    const QString targetCollection = QStringLiteral("tgt+contacts");
    const QString mappingId        = QStringLiteral("hub-skip-mapping");

    auto source = std::make_unique<GenericSqliteBackend>(
        tmpDir.filePath(QStringLiteral("source.db")));
    auto target = std::make_unique<GenericSqliteBackend>(
        tmpDir.filePath(QStringLiteral("target.db")));

    CollectionInfo srcCol;
    srcCol.id = sourceCollection; srcCol.name = sourceCollection; srcCol.type = QStringLiteral("contacts");
    source->createCollection(srcCol, vcard4);
    CollectionInfo tgtCol;
    tgtCol.id = targetCollection; tgtCol.name = targetCollection; tgtCol.type = QStringLiteral("contacts");
    target->createCollection(tgtCol, vcard4);

    // Seed one record on the source.
    QVERIFY(!source->createRecord(sourceCollection, seedRecord(QStringLiteral("rec-1"))).isEmpty());

    BackendRegistry registry;
    registry.registerBackendInstance(sourceBackendId, source.get());
    registry.registerBackendInstance(targetBackendId, target.get());

    RegistrySyncHost host(&registry);
    SyncEngine engine(&registry, &host, m_shape);
    engine.setSkipUnchangedMappings(true);

    BaselineStore baselines(tmpDir.filePath(QStringLiteral("baselines.db")));
    engine.setBaselineStore(&baselines);

    SyncMapping mapping;
    mapping.id             = mappingId;
    mapping.sourceBackend  = sourceBackendId;
    mapping.sourceCalendar = sourceCollection;
    mapping.targetBackend  = targetBackendId;
    mapping.targetCalendar = targetCollection;
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::SourceWins;
    mapping.enabled        = true;
    engine.setSyncMappings({ mapping });

    // EMPTY mappingIds => all-enabled => multi-mapping driver => fast path.
    // qWaitFor (not QTRY_VERIFY_WITH_TIMEOUT, whose return-on-timeout can't
    // live in a value-returning lambda) spins the event loop until the run
    // finishes; callers QVERIFY isFinished() before reading the result.
    auto runOnce = [&]() {
        clearLog();
        SyncRequest req;
        req.behavior = SyncEngine::SyncBehavior::Unmonitored;
        auto f = engine.runSync(req);
        (void)QTest::qWaitFor([&]{ return f.isFinished(); }, kSyncTimeoutMs);
        return f;
    };

    // Record each side's current revision as its synced baseline — exactly
    // what onWorkerSyncCompleted does via primeRevisionCache after a
    // successful sync. Used to put the mapping into a known "settled" state.
    auto primeToCurrent = [](GenericSqliteBackend *b, const QString &col) {
        b->primeRevisionCache({{col, b->collectionRevision(col)}});
    };

    // --- Prime path: a real first sync records fresh revisions on BOTH sides
    // (the engine's onWorkerSyncCompleted -> primeRevisionCache path). No
    // baseline yet, so it does not skip. We assert the caches went from empty
    // to populated; the actual record propagation between two sqlite sinks is
    // exercised elsewhere (and is irrelevant to the skip machinery). ---
    {
        QVERIFY(source->cachedCollectionRevision(sourceCollection).isEmpty());
        QVERIFY(target->cachedCollectionRevision(targetCollection).isEmpty());

        auto f = runOnce();
        QVERIFY(f.isFinished());
        QVERIFY2(f.resultAt(0).first().success,
                 qUtf8Printable(QStringLiteral("first sync failed: ")
                                + f.resultAt(0).first().errorMessage));
        QVERIFY2(!sawSkipLog(mappingId), "first sync must not skip (no baseline)");

        QVERIFY2(!source->cachedCollectionRevision(sourceCollection).isEmpty(),
                 "engine must prime the source revision after a successful sync");
        QVERIFY2(!target->cachedCollectionRevision(targetCollection).isEmpty(),
                 "engine must prime the target revision after a successful sync");
    }

    // --- Skip decision: with both sides' cached revision matching their
    // current content digest (nothing changed since the recorded baseline),
    // prepareSyncFastPath must skip the mapping. ---
    primeToCurrent(source.get(), sourceCollection);
    primeToCurrent(target.get(), targetCollection);
    {
        auto f = runOnce();
        QVERIFY(f.isFinished());
        QVERIFY(f.resultAt(0).first().success);
        QVERIFY2(sawSkipLog(mappingId),
                 "an unchanged mapping must be skipped");
    }

    // --- It keeps skipping while nothing changes (a skipped run does not
    // dispatch to the worker, so the recorded baselines stay put). ---
    {
        auto f = runOnce();
        QVERIFY(f.isFinished());
        QVERIFY(f.resultAt(0).first().success);
        QVERIFY2(sawSkipLog(mappingId), "a settled mapping keeps skipping");
    }

    // --- Mutation defeats the skip: a new source record changes the source
    // content digest, so fresh != cached and the next pass must NOT skip. ---
    QVERIFY(!source->createRecord(sourceCollection,
                                  seedRecord(QStringLiteral("rec-2"))).isEmpty());
    {
        auto f = runOnce();
        QVERIFY(f.isFinished());
        QVERIFY(f.resultAt(0).first().success);
        QVERIFY2(!sawSkipLog(mappingId),
                 "a source content change must defeat the skip");
    }
}

QTEST_MAIN(TestEngineSkipUnchanged)
#include "tst_engine_skip_unchanged.moc"
