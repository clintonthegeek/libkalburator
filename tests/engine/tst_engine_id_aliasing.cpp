// O55 — hub record-id aliasing regression gate.
//
// WildPalms handoff
// (~/dev/WildPalms/docs/2026-08-21-libkalburator-hub-record-id-join-churn-handoff.md):
// a TwoWay mapping between a bare-id backend and GenericSqliteBackend (which
// presents ids as <collectionId>\x01<origId> on read) churned and silently
// emptied the hub from pass 2 on — perRecordDiff() joined strictly by raw
// BackendRecord::id, so the same logical record looked like "created on one
// side + deleted on the other" on every pass after the first.
//
// The fix (this branch): the engine captures the id a backend actually
// assigned on create (WriteOperation::idAliases, populated by the default
// SyncBackendBase::applyRecords when createRecord returns a different id),
// persists it per mapping (BaselineStore blob_id_aliases, schema v8), and
// resolves it during the per-record join. A cross-create whose two sides are
// canonically equal but unjoined (the churn signature) now fails the mapping
// loudly instead of silently duplicating/emptying.
//
// Both slots below were RED on main @ v0.99 (churn emptied the hub; the
// cross-create churn reported success) and are GREEN only with the fix.

#include <QtTest/QtTest>
#include <QObject>
#include <QString>
#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFuture>
#include <QTemporaryDir>

#include <memory>

#include "backendrecord.h"
#include "backendregistry.h"
#include "baselinestore.h"
#include "collectioninfo.h"
#include "genericsqlitebackend.h"
#include "isynchost.h"
#include "lossprofile.h"
#include "mockbackend.h"
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
using Kalburator::Sync::ISyncHost;
using Kalburator::Sync::MockBackend;
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

const char *kRecordId = "palm:datebook:101";

// Minimal ISyncHost over a BackendRegistry (same idiom as
// tst_engine_skip_unchanged; the engine-test target links no shared stub
// carrying one).
class RegistrySyncHost final : public ISyncHost
{
public:
    explicit RegistrySyncHost(BackendRegistry *registry) : m_registry(registry) {}

    Kalburator::Sync::SyncBackend *backendById(const QString &id) override
    {
        return m_registry
            ? static_cast<Kalburator::Sync::SyncBackend *>(m_registry->backendInstance(id))
            : nullptr;
    }
    QHash<QString, Kalburator::Sync::SyncBackend *> backends() override
    {
        QHash<QString, Kalburator::Sync::SyncBackend *> out;
        if (!m_registry) return out;
        for (const auto &id : m_registry->registeredInstanceIds())
            out.insert(id, static_cast<Kalburator::Sync::SyncBackend *>(
                               m_registry->backendInstance(id)));
        return out;
    }
    Kalburator::Sync::ISyncConfigStore *configStore() override { return nullptr; }

    void syncStarted(const QString &, const LossProfile &) override {}
    void recordChanged(const QString &, const QString &, ChangeKind) override {}

private:
    BackendRegistry *m_registry = nullptr;
};

BackendRecord seedEvent(const QString &uid)
{
    BackendRecord rec;
    rec.id  = uid;
    rec.type = QStringLiteral("calendar");
    rec.displayName = uid;
    rec.data =
        "BEGIN:VCALENDAR\r\n"
        "VERSION:2.0\r\n"
        "PRODID:-//test//test//EN\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:" + uid.toUtf8() + "\r\n"
        "DTSTAMP:20260822T000000Z\r\n"
        "DTSTART:20260901T100000Z\r\n"
        "DTEND:20260901T110000Z\r\n"
        "SUMMARY:O55 fixture\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";
    rec.contentHash = QString::fromLatin1(
        QCryptographicHash::hash(rec.data, QCryptographicHash::Sha256).toHex());
    rec.lastModified = QDateTime::currentDateTimeUtc();
    rec.isDeleted    = false;
    return rec;
}

} // namespace

class TestEngineIdAliasing : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void twoWayBareIdToSqliteHub_convergesWithoutChurn();
    void unjoinedEqualTwins_failLoudlyInsteadOfChurning();
    void recategorizationViaHubEdit_anchorStaysConsolidated();
    void poisonedCrossedAliasStore_healsWithoutDataLoss();
    void unresolvedConflict_deferredMovesNothing();

private:
    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;
};

void TestEngineIdAliasing::initTestCase()
{
    Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
    Kalburator::registerStockPlugins(pm);
}

// The O55 repro, minimal shape (no Palm hardware): MockBackend presents the
// record under its bare payload uid; GenericSqliteBackend stores that id but
// re-reads it collection-prefixed. Three consecutive TwoWay runs must leave
// exactly one record in the hub, zero steady-state writes after run 1, and
// exactly ONE baseline row for the mapping (the churn wrote two — one per id
// form).
void TestEngineIdAliasing::twoWayBareIdToSqliteHub_convergesWithoutChurn()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    const Shape ical{ DomainId{"calendar"}, EncodingId{"ical"} };

    const QString sourceBackendId  = QStringLiteral("palm-mock");
    const QString targetBackendId  = QStringLiteral("sqlite-hub");
    const QString collection       = QStringLiteral("datebook");
    const QString mappingId        = QStringLiteral("o55-mapping");

    auto source = std::make_unique<MockBackend>(sourceBackendId);
    auto target = std::make_unique<GenericSqliteBackend>(
        tmpDir.filePath(QStringLiteral("hub.db")));

    CollectionInfo hubCol;
    hubCol.id = collection; hubCol.name = collection;
    hubCol.type = QStringLiteral("calendar");
    QVERIFY(!target->createCollection(hubCol, ical).isEmpty());

    QVERIFY(!source->createRecord(collection, seedEvent(QString::fromLatin1(kRecordId))).isEmpty());

    BackendRegistry registry;
    registry.registerBackendInstance(sourceBackendId, source.get());
    registry.registerBackendInstance(targetBackendId, target.get());

    RegistrySyncHost host(&registry);
    SyncEngine engine(&registry, &host, m_shape);

    BaselineStore baselines(tmpDir.filePath(QStringLiteral("baselines.db")));
    engine.setBaselineStore(&baselines);

    SyncMapping mapping;
    mapping.id             = mappingId;
    mapping.sourceBackend  = sourceBackendId;
    mapping.sourceCalendar = collection;
    mapping.targetBackend  = targetBackendId;
    mapping.targetCalendar = collection;
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::SourceWins;
    mapping.enabled        = true;
    engine.setSyncMappings({ mapping });

    auto runOnce = [&]() {
        SyncRequest req;
        req.behavior = SyncEngine::SyncBehavior::Unmonitored;
        auto f = engine.runSync(req);
        (void)QTest::qWaitFor([&]{ return f.isFinished(); }, kSyncTimeoutMs);
        return f;
    };

    auto hubRowCount = [&]() {
        return target->loadRecords(collection).size();
    };

    // --- Run 1: the create lands in the hub (under its prefixed read id). ---
    {
        auto f = runOnce();
        QVERIFY(f.isFinished());
        const auto results = f.resultAt(0);
        QCOMPARE(results.size(), 1);
        QVERIFY2(results.first().success,
                 qUtf8Printable(QStringLiteral("run 1 failed: ")
                                + results.first().errorMessage));
        QCOMPARE(hubRowCount(), 1);
    }

    // --- Runs 2 and 3: converged — no churn, hub keeps its record. ---
    for (int pass = 2; pass <= 3; ++pass) {
        auto f = runOnce();
        QVERIFY(f.isFinished());
        const auto results = f.resultAt(0);
        QVERIFY2(results.first().success,
                 qUtf8Printable(QStringLiteral("run %1 failed: %2")
                                    .arg(pass)
                                    .arg(results.first().errorMessage)));
        QCOMPARE(hubRowCount(), 1);
        QCOMPARE(results.first().targetStats.created, 0);
        QCOMPARE(results.first().targetStats.deleted, 0);
        QCOMPARE(results.first().sourceStats.created, 0);
        QCOMPARE(results.first().sourceStats.deleted, 0);
    }

    // The churn's fingerprint: one baseline row per id form. The fix keeps
    // exactly one row, keyed by the bare (source-space) id.
    const auto rows = baselines.baselineHashesForMappingV4(mappingId);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().recordId, QString::fromLatin1(kRecordId));

    // And the alias the hub-side create produced must be persisted, so the
    // join survives process restarts (a fresh BaselineStore instance over
    // the same file sees it).
    BaselineStore reopened(tmpDir.filePath(QStringLiteral("baselines.db")));
    Q_UNUSED(reopened);
    QCOMPARE(hubRowCount(), 1);
}

// The churn signature, held in a test tube: both sides hold canonically
// equal records under different ids, with no baseline and no alias (the
// state a pre-fix run leaves behind). The engine must refuse to cross-create
// — fail the mapping with a precise error — instead of silently duplicating
// and churning toward an empty hub.
void TestEngineIdAliasing::unjoinedEqualTwins_failLoudlyInsteadOfChurning()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    const Shape ical{ DomainId{"calendar"}, EncodingId{"ical"} };

    const QString sourceBackendId  = QStringLiteral("palm-mock");
    const QString targetBackendId  = QStringLiteral("sqlite-hub");
    const QString collection       = QStringLiteral("datebook");
    const QString mappingId        = QStringLiteral("o55-guard-mapping");

    auto source = std::make_unique<MockBackend>(sourceBackendId);
    auto target = std::make_unique<GenericSqliteBackend>(
        tmpDir.filePath(QStringLiteral("hub.db")));

    CollectionInfo hubCol;
    hubCol.id = collection; hubCol.name = collection;
    hubCol.type = QStringLiteral("calendar");
    QVERIFY(!target->createCollection(hubCol, ical).isEmpty());

    // Source holds the record under its bare id; the hub holds the SAME
    // logical record under the prefixed form (as if a pre-fix pass had
    // written it). Seed the hub from the mock's own read-back so both
    // sides' payloads are byte-identical even after MockBackend's
    // parse/re-serialize round trip. No baselines — a fresh store.
    QVERIFY(!source->createRecord(collection, seedEvent(QString::fromLatin1(kRecordId))).isEmpty());
    const QList<BackendRecord> onPalm = source->loadRecords(collection);
    QCOMPARE(onPalm.size(), 1);
    QVERIFY(!target->createRecord(collection, onPalm.first()).isEmpty());

    BackendRegistry registry;
    registry.registerBackendInstance(sourceBackendId, source.get());
    registry.registerBackendInstance(targetBackendId, target.get());

    RegistrySyncHost host(&registry);
    SyncEngine engine(&registry, &host, m_shape);

    BaselineStore baselines(tmpDir.filePath(QStringLiteral("baselines.db")));
    engine.setBaselineStore(&baselines);

    SyncMapping mapping;
    mapping.id             = mappingId;
    mapping.sourceBackend  = sourceBackendId;
    mapping.sourceCalendar = collection;
    mapping.targetBackend  = targetBackendId;
    mapping.targetCalendar = collection;
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::SourceWins;
    mapping.enabled        = true;
    engine.setSyncMappings({ mapping });

    SyncRequest req;
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    auto f = engine.runSync(req);
    (void)QTest::qWaitFor([&]{ return f.isFinished(); }, kSyncTimeoutMs);
    QVERIFY(f.isFinished());

    const auto results = f.resultAt(0);
    QVERIFY2(!results.first().success,
             "unjoined canonically-equal twins must fail the mapping, not "
             "silently cross-create");
    QVERIFY2(results.first().errorMessage.contains(QLatin1String("identity")),
             qUtf8Printable(QStringLiteral("expected an identity-mismatch error, got: ")
                            + results.first().errorMessage));

    // Fail loud, not destructive: both records survive untouched.
    QCOMPARE(source->loadRecords(collection).size(), 1);
    QCOMPARE(target->loadRecords(collection).size(), 1);
}

// The WildPalms O55-followup scenario (their 2026-08-22 recategorization
// handoff), lib-side: after a converged first sync, the HUB record is edited
// in place under its prefixed id. Pass 1 of the next sync pushes the change
// back to the palm — where the backend stores by payload uid, so the write
// lands under the BARE id and (pre-fix) persisted a CROSSED alias
// (bare→prefixed) plus a SECOND baseline row keyed prefixed. Pass 2 then
// misjoined again: a phantom AskUser conflict AND a phantom delete that
// emptied the hub while reporting failure. With anchor-stable aliasing and
// baseline sink-keying, both passes converge and exactly ONE baseline row
// and ONE alias direction ever exist.
void TestEngineIdAliasing::recategorizationViaHubEdit_anchorStaysConsolidated()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    const Shape ical{ DomainId{"calendar"}, EncodingId{"ical"} };

    const QString sourceBackendId  = QStringLiteral("palm-mock");
    const QString targetBackendId  = QStringLiteral("sqlite-hub");
    const QString collection       = QStringLiteral("datebook");
    const QString mappingId        = QStringLiteral("o56-mapping");

    auto source = std::make_unique<MockBackend>(sourceBackendId);
    auto target = std::make_unique<GenericSqliteBackend>(
        tmpDir.filePath(QStringLiteral("hub.db")));

    CollectionInfo hubCol;
    hubCol.id = collection; hubCol.name = collection;
    hubCol.type = QStringLiteral("calendar");
    QVERIFY(!target->createCollection(hubCol, ical).isEmpty());

    QVERIFY(!source->createRecord(collection, seedEvent(QString::fromLatin1(kRecordId))).isEmpty());

    BackendRegistry registry;
    registry.registerBackendInstance(sourceBackendId, source.get());
    registry.registerBackendInstance(targetBackendId, target.get());

    RegistrySyncHost host(&registry);
    SyncEngine engine(&registry, &host, m_shape);

    BaselineStore baselines(tmpDir.filePath(QStringLiteral("baselines.db")));
    engine.setBaselineStore(&baselines);

    SyncMapping mapping;
    mapping.id             = mappingId;
    mapping.sourceBackend  = sourceBackendId;
    mapping.sourceCalendar = collection;
    mapping.targetBackend  = targetBackendId;
    mapping.targetCalendar = collection;
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::SourceWins;
    mapping.enabled        = true;
    engine.setSyncMappings({ mapping });

    auto runOnce = [&]() {
        SyncRequest req;
        req.behavior = SyncEngine::SyncBehavior::Unmonitored;
        auto f = engine.runSync(req);
        (void)QTest::qWaitFor([&]{ return f.isFinished(); }, kSyncTimeoutMs);
        return f;
    };

    // --- First sync converges (the O55 gate covers this; here just setup). ---
    {
        auto f = runOnce();
        QVERIFY(f.isFinished());
        const auto results = f.resultAt(0);
        QVERIFY2(results.first().success,
                 qUtf8Printable(QStringLiteral("first sync failed: ")
                                + results.first().errorMessage));
        QCOMPARE(target->loadRecords(collection).size(), 1);
    }

    // encodeRecordId() is private; the format is "<collectionId>\x01<origId>".
    const QString prefixedId = collection + QChar(u'\x01') + QString::fromLatin1(kRecordId);

    // --- Recategorize by editing the HUB record in place (prefixed id). ---
    {
        QList<BackendRecord> hubRecords = target->loadRecords(collection);
        QCOMPARE(hubRecords.size(), 1);
        BackendRecord edited = hubRecords.first();
        QCOMPARE(edited.id, prefixedId);
        edited.data.replace("O55 fixture", "O55 fixture - Home");
        edited.contentHash = QString::fromLatin1(
            QCryptographicHash::hash(edited.data, QCryptographicHash::Sha256).toHex());
        QVERIFY(target->updateRecord(edited));
    }

    // --- Pass 1 propagates the edit back to the palm (a create there — the
    // palm keys by payload uid). Must succeed with zero deletes. ---
    {
        auto f = runOnce();
        QVERIFY(f.isFinished());
        const auto results = f.resultAt(0);
        QVERIFY2(results.first().success,
                 qUtf8Printable(QStringLiteral("pass 1 failed: ")
                                + results.first().errorMessage));
        QCOMPARE(results.first().targetStats.deleted, 0);
        QCOMPARE(results.first().sourceStats.deleted, 0);
        QCOMPARE(target->loadRecords(collection).size(), 1);
        QCOMPARE(source->loadRecords(collection).size(), 1);
    }

    // --- Pass 2 must CONVERGE (the defect made it misjoin into a phantom
    // conflict + hub delete): zero movement on either side. ---
    {
        auto f = runOnce();
        QVERIFY(f.isFinished());
        const auto results = f.resultAt(0);
        QVERIFY2(results.first().success,
                 qUtf8Printable(QStringLiteral("pass 2 failed: ")
                                + results.first().errorMessage
                                + QStringLiteral(" unresolved=")
                                + QString::number(
                                      results.first().unresolvedConflicts.size())));
        QCOMPARE(results.first().targetStats.created, 0);
        QCOMPARE(results.first().targetStats.deleted, 0);
        QCOMPARE(results.first().sourceStats.created, 0);
        QCOMPARE(results.first().sourceStats.deleted, 0);
        QCOMPARE(target->loadRecords(collection).size(), 1);
        QCOMPARE(source->loadRecords(collection).size(), 1);
    }

    // --- Exactly one baseline row and one alias direction, forever. ---
    const auto rows = baselines.baselineHashesForMappingV4(mappingId);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().recordId, QString::fromLatin1(kRecordId));

    const auto aliases = baselines.idAliasesForMapping(mappingId);
    QCOMPARE(aliases.size(), 1);
    QCOMPARE(aliases.value(prefixedId), QString::fromLatin1(kRecordId));
}

// A store poisoned by a pre-fix (v1.00) run: crossed aliases BOTH directions
// and two unconsolidated baseline rows for one logical record — the exact
// shape of the WildPalms followup's evidence dump. The load-time heal must
// collapse the component to one join key and dedupe the baselines; the run
// must not manufacture a conflict or move/delete ANY data.
void TestEngineIdAliasing::poisonedCrossedAliasStore_healsWithoutDataLoss()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    const Shape ical{ DomainId{"calendar"}, EncodingId{"ical"} };

    const QString sourceBackendId  = QStringLiteral("palm-mock");
    const QString targetBackendId  = QStringLiteral("sqlite-hub");
    const QString collection       = QStringLiteral("datebook");
    const QString mappingId        = QStringLiteral("o56-poisoned");

    auto source = std::make_unique<MockBackend>(sourceBackendId);
    auto target = std::make_unique<GenericSqliteBackend>(
        tmpDir.filePath(QStringLiteral("hub.db")));

    CollectionInfo hubCol;
    hubCol.id = collection; hubCol.name = collection;
    hubCol.type = QStringLiteral("calendar");
    QVERIFY(!target->createCollection(hubCol, ical).isEmpty());

    // Both sides hold the SAME logical record under their own id forms.
    QVERIFY(!source->createRecord(collection, seedEvent(QString::fromLatin1(kRecordId))).isEmpty());
    const QList<BackendRecord> onPalm = source->loadRecords(collection);
    QCOMPARE(onPalm.size(), 1);
    QVERIFY(!target->createRecord(collection, onPalm.first()).isEmpty());

    const QString bareId     = QString::fromLatin1(kRecordId);
    const QString prefixedId = collection + QChar(u'\x01') + bareId;

    // Realistic side hashes (the v1.00 poison carried each side's ACTUAL
    // read-back hash on its row — the rows were real, only the anchors were
    // crossed).
    const QString palmHash = source->loadRecords(collection).first().contentHash;
    const QString hubHash  = target->loadRecords(collection).first().contentHash;

    BaselineStore baselines(tmpDir.filePath(QStringLiteral("baselines.db")));

    // Poison exactly like the v1.00 defect left it: stale symmetric row +
    // fresh per-side row keyed at the OTHER id form + crossed aliases.
    QVERIFY(baselines.setBaselineHashesV4(mappingId, bareId,
                                          QStringLiteral("deadbeef"),
                                          QStringLiteral("deadbeef")));
    QVERIFY(baselines.setBaselineHashesV4(mappingId, prefixedId, palmHash, hubHash));
    QVERIFY(baselines.setIdAlias(mappingId, prefixedId, bareId));
    QVERIFY(baselines.setIdAlias(mappingId, bareId, prefixedId));

    BackendRegistry registry;
    registry.registerBackendInstance(sourceBackendId, source.get());
    registry.registerBackendInstance(targetBackendId, target.get());

    RegistrySyncHost host(&registry);
    SyncEngine engine(&registry, &host, m_shape);
    engine.setBaselineStore(&baselines);

    SyncMapping mapping;
    mapping.id             = mappingId;
    mapping.sourceBackend  = sourceBackendId;
    mapping.sourceCalendar = collection;
    mapping.targetBackend  = targetBackendId;
    mapping.targetCalendar = collection;
    // AskUser + Unmonitored: any manufactured conflict defers unresolved —
    // and NOTHING may move while it pends.
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::AskUser;
    mapping.enabled        = true;
    engine.setSyncMappings({ mapping });

    SyncRequest req;
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    auto f = engine.runSync(req);
    (void)QTest::qWaitFor([&]{ return f.isFinished(); }, kSyncTimeoutMs);
    QVERIFY(f.isFinished());

    // The heal collapses the crossed component onto one sink key and dedupes
    // the baseline rows (the fresh row matches both sides' current hashes).
    // The run must therefore CONVERGE — no manufactured conflict, no
    // phantom delete, zero movement.
    const auto results = f.resultAt(0);
    QVERIFY2(results.first().success,
             qUtf8Printable(QStringLiteral("healed run failed: ")
                            + results.first().errorMessage));
    QCOMPARE(results.first().unresolvedConflicts.size(), 0);
    QCOMPARE(results.first().targetStats.created, 0);
    QCOMPARE(results.first().targetStats.deleted, 0);
    QCOMPARE(results.first().sourceStats.created, 0);
    QCOMPARE(results.first().sourceStats.deleted, 0);
    QCOMPARE(source->loadRecords(collection).size(), 1);
    QCOMPARE(target->loadRecords(collection).size(), 1);
}

// WildPalms followup ask #2, as a standing contract: when an Unmonitored
// AskUser conflict defers unresolved, NOTHING moves — no write, and above
// all no destructive op — even though the walk's non-conflict bookkeeping
// has already accumulated. The record stays on both sides; the run reports
// failure honestly.
void TestEngineIdAliasing::unresolvedConflict_deferredMovesNothing()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    const Shape ical{ DomainId{"calendar"}, EncodingId{"ical"} };

    const QString sourceBackendId  = QStringLiteral("palm-mock");
    const QString targetBackendId  = QStringLiteral("sqlite-hub");
    const QString collection       = QStringLiteral("datebook");
    const QString mappingId        = QStringLiteral("o56-deferred");

    auto source = std::make_unique<MockBackend>(sourceBackendId);
    auto target = std::make_unique<GenericSqliteBackend>(
        tmpDir.filePath(QStringLiteral("hub.db")));

    CollectionInfo hubCol;
    hubCol.id = collection; hubCol.name = collection;
    hubCol.type = QStringLiteral("calendar");
    QVERIFY(!target->createCollection(hubCol, ical).isEmpty());

    QVERIFY(!source->createRecord(collection, seedEvent(QString::fromLatin1(kRecordId))).isEmpty());
    const QList<BackendRecord> onPalm = source->loadRecords(collection);
    QCOMPARE(onPalm.size(), 1);
    QVERIFY(!target->createRecord(collection, onPalm.first()).isEmpty());

    BaselineStore baselines(tmpDir.filePath(QStringLiteral("baselines.db")));
    // A garbage baseline makes BOTH sides read as modified vs baseline — a
    // genuine BothModified for AskUser to defer (same id on both sides here,
    // so this isolates the defer semantics from any aliasing).
    QVERIFY(baselines.setBaselineHashesV4(mappingId, QString::fromLatin1(kRecordId),
                                          QStringLiteral("garbage"),
                                          QStringLiteral("garbage")));

    BackendRegistry registry;
    registry.registerBackendInstance(sourceBackendId, source.get());
    registry.registerBackendInstance(targetBackendId, target.get());

    RegistrySyncHost host(&registry);
    SyncEngine engine(&registry, &host, m_shape);
    engine.setBaselineStore(&baselines);

    SyncMapping mapping;
    mapping.id             = mappingId;
    mapping.sourceBackend  = sourceBackendId;
    mapping.sourceCalendar = collection;
    mapping.targetBackend  = targetBackendId;
    mapping.targetCalendar = collection;
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::AskUser;
    mapping.enabled        = true;
    engine.setSyncMappings({ mapping });

    SyncRequest req;
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    auto f = engine.runSync(req);
    (void)QTest::qWaitFor([&]{ return f.isFinished(); }, kSyncTimeoutMs);
    QVERIFY(f.isFinished());

    const auto results = f.resultAt(0);
    QVERIFY2(!results.first().success,
             "a deferred AskUser conflict must fail the run");
    QCOMPARE(results.first().unresolvedConflicts.size(), 1);

    // Nothing moved — in particular nothing was deleted.
    QCOMPARE(results.first().targetStats.created, 0);
    QCOMPARE(results.first().targetStats.updated, 0);
    QCOMPARE(results.first().targetStats.deleted, 0);
    QCOMPARE(results.first().sourceStats.created, 0);
    QCOMPARE(results.first().sourceStats.updated, 0);
    QCOMPARE(results.first().sourceStats.deleted, 0);
    QCOMPARE(source->loadRecords(collection).size(), 1);
    QCOMPARE(target->loadRecords(collection).size(), 1);
}

QTEST_MAIN(TestEngineIdAliasing)
#include "tst_engine_id_aliasing.moc"
