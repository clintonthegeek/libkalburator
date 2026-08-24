// EEE Tier A1 — engine-level vendor-shaped hub convergence (the owed gate).
//
// Two stub calendar backends carrying RAW VENDOR WIRE JSON — google-event
// on one side, ms-event on the other — meet through a canon-shaped
// GenericSqliteBackend hub in ONE Queue-mode runSync. This is the first
// engine test to mix two vendor encodings; it proves the O55 id-aliasing,
// L2 fixpoint, and O56 conflict-hold machinery on records richer than
// iCal, and closes with the Part IV payoff: the converged hub feeds
// PersonDirectory's meeting roster.
//
// NOTE: no terminated raw string literals in this TU (O59 moc tooling rule).

#include <QtTest/QtTest>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

#include "backendrecord.h"
#include "backendregistry.h"
#include "baselinestore.h"
#include "canonenvelope.h"
#include "collectioninfo.h"
#include "genericsqlitebackend.h"
#include "googlepersoncanonstages.h"
#include "googlecanonstages.h"
#include "identityresolver.h"
#include "isynchost.h"
#include "lossprofile.h"
#include "mseventcanonstages.h"
#include "mscontactcanonstages.h"
#include "persondirectory.h"
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
using Kalburator::Engine::SyncEngine;
using Kalburator::Engine::SyncRequest;
using Kalburator::Sync::SyncMapping;
using Kalburator::Sync::SyncMode;
using Kalburator::Sinks::GenericSqliteBackend;
using Kalburator::Identity::IdentityStore;
using Kalburator::Identity::PersonDirectory;
using Kalburator::Shape::CanonEnvelope::serialize;
using Kalburator::Shape::CanonEnvelope::stampEnvelope;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::Shape;
using Kalburator::Shape::ShapeRegistries;

namespace {

constexpr int kSyncTimeoutMs = 30000;
const QLatin1String kCollection("cal");
const QLatin1String kMappingG("g-to-hub");
const QLatin1String kMappingM("hub-to-ms");
const QLatin1String kSharedUid("shared-event-uid@cal.example");

// ---------------------------------------------------------------------------
// VendorWireBackend — in-memory blob backend storing bytes VERBATIM and
// declaring one vendor encoding for its collection (pattern: RawBlobBackend
// + ShapedTestBackend; wire-record pattern: MSGraphCalendarBackend).
// ---------------------------------------------------------------------------
class VendorWireBackend final : public Kalburator::Sync::SyncBackendBase {
public:
    explicit VendorWireBackend(const QString& id, Shape shape)
        : m_id(id), m_shape(std::move(shape))
    {
    }

    QString backendType() const override { return QStringLiteral("vendor-wire"); }
    QString backendId() const override { return m_id; }
    QString displayName() const override { return m_id; }
    bool isAvailable() const override { return true; }

    QList<Shape> nativeShapes() const override { return { m_shape }; }

    QList<CollectionInfo> availableCollections() override
    {
        return m_collections;
    }
    CollectionInfo collectionInfo(const QString& collectionId) override
    {
        for (const auto& c : m_collections)
            if (c.id == collectionId)
                return c;
        return {};
    }
    QString createCollection(const CollectionInfo& info) override
    {
        m_collections.append(info);
        return info.id;
    }

    QList<BackendRecord> loadRecords(const QString& collectionId) override
    {
        QList<BackendRecord> out;
        for (const BackendRecord& r : m_records.value(collectionId))
            if (!r.isDeleted)
                out.append(r);
        return out;
    }
    std::optional<BackendRecord> loadRecord(const QString& recordId) override
    {
        for (const auto& list : m_records)
            for (const BackendRecord& r : list)
                if (r.id == recordId && !r.isDeleted)
                    return r;
        return std::nullopt;
    }
    QString createRecord(const QString& collectionId,
                         const BackendRecord& record) override
    {
        BackendRecord r = record;
        // Mint a vendor-style transport id distinct from any canon uid so
        // the O55 aliasing path is exercised on every create.
        r.id = QStringLiteral("%1-%2").arg(m_id).arg(++m_counter);
        stamp(r);
        m_records[collectionId].append(r);
        return r.id;
    }
    bool updateRecord(const BackendRecord& record) override
    {
        for (auto& list : m_records) {
            for (auto& r : list) {
                if (r.id == record.id) {
                    BackendRecord updated = record;
                    stamp(updated);
                    std::swap(r, updated);
                    return true;
                }
            }
        }
        return false;
    }
    bool deleteRecord(const QString& recordId) override
    {
        for (auto& list : m_records) {
            for (auto it = list.begin(); it != list.end(); ++it) {
                if (it->id == recordId) {
                    list.erase(it);
                    return true;
                }
            }
        }
        return false;
    }

    QList<BackendRecord> modifiedSince(const QString& collectionId,
                                       const QDateTime& since) override
    {
        QList<BackendRecord> out;
        for (const BackendRecord& r :
             m_records.value(collectionId))
            if (!r.isDeleted && r.lastModified > since)
                out.append(r);
        return out;
    }
    QStringList deletedSince(const QString&, const QDateTime&) override
    {
        return {};
    }

    /// Test seam: seed a record under a CHOSEN transport id.
    QString seedRecord(const QString& collectionId, const QString& id,
                       const QByteArray& wireJson)
    {
        BackendRecord r;
        r.id = id;
        r.type = QStringLiteral("event");
        r.displayName = QStringLiteral("(wire)");
        r.data = wireJson;
        stamp(r);
        m_records[collectionId].append(r);
        return r.id;
    }

private:
    void stamp(BackendRecord& r)
    {
        r.contentHash = QString::fromLatin1(
            QCryptographicHash::hash(r.data, QCryptographicHash::Sha256)
                .toHex());
        r.lastModified = QDateTime::currentDateTimeUtc();
        r.isDeleted = false;
    }

    QString m_id;
    Shape m_shape;
    QList<CollectionInfo> m_collections;
    QHash<QString, QList<BackendRecord>> m_records;
    int m_counter = 0;
};

// ---------------------------------------------------------------------------
// Minimal host over a BackendRegistry (engine-test idiom).
// ---------------------------------------------------------------------------
class RegistrySyncHost final : public ISyncHost {
public:
    explicit RegistrySyncHost(BackendRegistry* registry) : m_registry(registry)
    {
    }
    Kalburator::Sync::SyncBackend* backendById(const QString& id) override
    {
        return m_registry ? static_cast<Kalburator::Sync::SyncBackend*>(
                                m_registry->backendInstance(id))
                          : nullptr;
    }
    QHash<QString, Kalburator::Sync::SyncBackend*> backends() override
    {
        return {};
    }
    Kalburator::Sync::ISyncConfigStore* configStore() override
    {
        return nullptr;
    }
    void syncStarted(const QString&, const Kalburator::Shape::LossProfile&)
        override
    {
    }
    void recordChanged(const QString&, const QString&, ChangeKind) override {}

private:
    BackendRegistry* m_registry;
};

// ---------------------------------------------------------------------------
// Wire fixtures — logically THE SAME event expressed in both vendors'
// dialects. Promoted uids align on kSharedUid by construction:
//   google promote: uid <- iCalUID (fallback id)
//   ms     promote: uid <- uid (fallback iCalUId, then id)
// ---------------------------------------------------------------------------
QJsonObject googleWire()
{
    QJsonObject start;
    start.insert(QStringLiteral("dateTime"),
                 QStringLiteral("2026-09-01T10:00:00Z"));
    QJsonObject end;
    end.insert(QStringLiteral("dateTime"),
               QStringLiteral("2026-09-01T11:00:00Z"));
    QJsonObject ev;
    ev.insert(QStringLiteral("id"), QStringLiteral("google-transport-1"));
    ev.insert(QStringLiteral("iCalUID"), QString(kSharedUid));
    ev.insert(QStringLiteral("summary"), QStringLiteral("Cross-vendor sync"));
    ev.insert(QStringLiteral("start"), start);
    ev.insert(QStringLiteral("end"), end);
    ev.insert(QStringLiteral("organizer"),
              QJsonObject{ { QStringLiteral("email"),
                             QStringLiteral("ada@example.com") } });
    QJsonArray attendees;
    attendees.append(QJsonObject{
        { QStringLiteral("email"), QStringLiteral("bob@example.com") },
        { QStringLiteral("responseStatus"), QStringLiteral("accepted") } });
    ev.insert(QStringLiteral("attendees"), attendees);
    return ev;
}

QJsonObject msWireFromCanonUid()
{
    QJsonObject start;
    start.insert(QStringLiteral("dateTime"),
                 QStringLiteral("2026-09-01T10:00:00"));
    start.insert(QStringLiteral("timeZone"), QStringLiteral("UTC"));
    QJsonObject end;
    end.insert(QStringLiteral("dateTime"),
               QStringLiteral("2026-09-01T11:00:00"));
    end.insert(QStringLiteral("timeZone"), QStringLiteral("UTC"));
    QJsonObject ev;
    ev.insert(QStringLiteral("id"), QStringLiteral("ms-transport-1"));
    ev.insert(QStringLiteral("uid"), QString(kSharedUid));
    ev.insert(QStringLiteral("subject"), QStringLiteral("Cross-vendor sync"));
    ev.insert(QStringLiteral("start"), start);
    ev.insert(QStringLiteral("end"), end);
    return ev;
}

QByteArray compact(const QJsonObject& o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

} // namespace

class TestEngineVendorShapedHub : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
        Kalburator::registerStockPlugins(pm);
    }

    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
    }
    void cleanup()
    {
        m_dir.reset();
    }

    // The headline slot: seed a google-event record; ONE Queue-mode runSync
    // must carry it G -> canon-hub -> MS across two mappings via the L2
    // re-prime, with O55 aliases persisted and all three endpoints holding
    // the same logical event at fixpoint.
    void firstSyncCarriesGoogleEventThroughHubToMicrosoft()
    {
        buildWorld();

        QVERIFY(!m_g->seedRecord(kCollection, QStringLiteral("g-seed-1"),
                                 compact(googleWire()))
                     .isEmpty());

        SyncRequest req;
        req.behavior = SyncEngine::SyncBehavior::Unmonitored;
        auto f = m_engine->runSync(req);
        QTest::qWaitFor([&] { return f.isFinished(); }, kSyncTimeoutMs);
        QVERIFY2(f.isFinished(), "runSync timed out");

        const auto results = f.resultAt(0);
        // Queue mode may re-prime mappings after intra-run writes (L2);
        // a mapping can appear more than once. Every appearance must
        // succeed, and the FINAL state assertions below are the oracle.
        QVERIFY(results.size() >= 2);
        for (const auto& r : results)
            QVERIFY2(r.success,
                     qPrintable(QStringLiteral("mapping failed: %1")
                                    .arg(r.errorMessage)));

        // Hub holds exactly one CANON record with the shared uid.
        QCOMPARE(m_hub->loadRecords(kCollection).size(), 1);
        const QJsonObject hubCanon =
            Kalburator::Shape::CanonEnvelope::parse(
                m_hub->loadRecords(kCollection).first().data);
        QCOMPARE(hubCanon.value(QStringLiteral("_canon"))
                     .toObject()
                     .value(QStringLiteral("domain"))
                     .toString(),
                 QStringLiteral("calendar"));
        QCOMPARE(hubCanon.value(QStringLiteral("uid")).toString(),
                 QString(kSharedUid));

        // MS side received the demoted ms-event wire under its own
        // transport id; promoting IT yields the same canon uid.
        const auto msRecords = m_ms->loadRecords(kCollection);
        QCOMPARE(msRecords.size(), 1);
        const QJsonObject msPromoted =
            Kalburator::Shape::CanonEnvelope::parse(
                Kalburator::Calendar::MsEventToCanonStage().transform(
                    msRecords.first().data));
        QCOMPARE(msPromoted.value(QStringLiteral("uid")).toString(),
                 QString(kSharedUid));

        // O55: alias rows persist per mapping (source transport id ->
        // hub prefixed id).
        const auto gAliases = m_baselines->idAliasesForMapping(kMappingG);
        QCOMPARE(gAliases.size(), 1);
        // O56 anchor discipline: aliases chain-resolve to the component
        // SINK, so the KEY carries the hub's namespaced id.
        const QString hubPrefixed = gAliases.constBegin().key();
        QVERIFY2(hubPrefixed.startsWith(QString(kCollection)
                                        + QChar(u'\x01')),
                 "alias key must be the namespaced sink id");

        // Steady state: an immediate second run moves nothing anywhere.
        auto f2 = m_engine->runSync(req);
        QTest::qWaitFor([&] { return f2.isFinished(); }, kSyncTimeoutMs);
        const auto results2 = f2.resultAt(0);
        for (const auto& r : results2) {
            QVERIFY(r.success);
            QCOMPARE(r.sourceStats.created, 0);
            QCOMPARE(r.targetStats.created, 0);
            QCOMPARE(r.sourceStats.deleted, 0);
            QCOMPARE(r.targetStats.deleted, 0);
        }
        QCOMPARE(m_hub->loadRecords(kCollection).size(), 1);
        QCOMPARE(m_ms->loadRecords(kCollection).size(), 1);
    }

    // Canonically-equal twins under unjoined ids on BOTH vendor sides AND
    // the hub, no baselines: the run must REFUSE loudly instead of
    // silently emptying any store. Twins are built by demoting ONE canon
    // through each vendor stage, so canonical equality is guaranteed.
    void unjoinedVendorTwins_failLoudly()
    {
        buildWorld();

        // One MINIMAL canon record, demoted to both vendor dialects —
        // minimal so each demote/re-promote cycle converges byte-equal.
        QJsonObject twinCanonObj;
        twinCanonObj.insert(QStringLiteral("uid"), QString(kSharedUid));
        twinCanonObj.insert(QStringLiteral("summary"),
                            QStringLiteral("Cross-vendor sync"));
        stampEnvelope(twinCanonObj, QStringLiteral("calendar"),
                      QString(kSharedUid));
        const QByteArray canonBytes = serialize(twinCanonObj);
        const QByteArray gTwin =
            Kalburator::Calendar::CanonToGoogleEventStage().transform(
                canonBytes);
        QVERIFY(!gTwin.isEmpty());
        const QByteArray msTwin =
            Kalburator::Calendar::CanonToMsEventStage().transform(canonBytes);
        QVERIFY(!msTwin.isEmpty());

        m_g->seedRecord(kCollection, QStringLiteral("g-twin"), gTwin);
        m_ms->seedRecord(kCollection, QStringLiteral("ms-twin"), msTwin);
        // Hub already holds the canon twin under its own encoding.
        QVERIFY(!m_hub
                     ->createRecord(kCollection,
                                    [&] {
                                        BackendRecord r;
                                        r.id =
                                            QStringLiteral("hub-twin");
                                        r.type =
                                            QStringLiteral("event");
                                        r.data = canonBytes;
                                        r.contentHash =
                                            QString::fromLatin1(
                                                QCryptographicHash::hash(
                                                    canonBytes,
                                                    QCryptographicHash::Sha256)
                                                    .toHex());
                                        r.lastModified = QDateTime::
                                            currentDateTimeUtc();
                                        return r;
                                    }())
                     .isEmpty());

        SyncRequest req;
        req.behavior = SyncEngine::SyncBehavior::Unmonitored;
        auto f = m_engine->runSync(req);
        QTest::qWaitFor([&] { return f.isFinished(); }, kSyncTimeoutMs);

        bool refused = false;
        for (const auto& r : f.resultAt(0)) {
            if (!r.success
                && r.errorMessage.contains(QLatin1String("identity")))
                refused = true;
        }
        QVERIFY2(refused,
                 "unjoined canonically-equal vendor twins must fail loud");
        QCOMPARE(m_g->loadRecords(kCollection).size(), 1);
        QCOMPARE(m_hub->loadRecords(kCollection).size(), 1);
        QCOMPARE(m_ms->loadRecords(kCollection).size(), 1);
    }

    // An unresolved AskUser conflict on vendor-shaped records holds ALL
    // writes for that mapping (O56 semantics hold on rich records too).
    void unresolvedConflictOnVendorShapes_movesNothing()
    {
        buildWorld();

        QVERIFY(!m_g->seedRecord(kCollection, QStringLiteral("g-c"),
                                 compact(googleWire()))
                     .isEmpty());

        // First run establishes baselines + creates on hub/MS.
        {
            SyncRequest req;
            req.behavior = SyncEngine::SyncBehavior::Unmonitored;
            auto f = m_engine->runSync(req);
            QTest::qWaitFor([&] { return f.isFinished(); }, kSyncTimeoutMs);
            for (const auto& r : f.resultAt(0))
                QVERIFY2(r.success, qPrintable(r.errorMessage));
        }
        QCOMPARE(m_ms->loadRecords(kCollection).size(), 1);

        // Diverge both sides of the MS mapping: edit the HUB canon AND the
        // MS wire copy, then poison the baseline so both read as modified.
        const auto hubRecs = m_hub->loadRecords(kCollection);
        QVERIFY(!hubRecs.isEmpty());
        QJsonObject editedCanon =
            Kalburator::Shape::CanonEnvelope::parse(hubRecs.first().data);
        editedCanon.insert(QStringLiteral("summary"),
                           QStringLiteral("Hub-side edit"));
        QVERIFY(m_hub->updateRecord(
            withData(hubRecs.first(),
                     Kalburator::Shape::CanonEnvelope::serialize(editedCanon))));

        const auto msRecs = m_ms->loadRecords(kCollection);
        QVERIFY(!msRecs.isEmpty());
        QJsonObject editedWire =
            QJsonDocument::fromJson(msRecs.first().data).object();
        editedWire.insert(QStringLiteral("subject"),
                          QStringLiteral("MS-side edit"));
        QVERIFY(m_ms->updateRecord(withData(msRecs.first(), compact(editedWire))));

        QVERIFY(m_baselines->setBaselineHashesV4(
            kMappingM, msRecs.first().id, QStringLiteral("garbage"),
            QStringLiteral("garbage")));

        QSignalSpy conflicts(m_engine.get(), &SyncEngine::conflictDetected);
        SyncRequest req;
        req.behavior = SyncEngine::SyncBehavior::Unmonitored;
        auto f = m_engine->runSync(req);
        QTest::qWaitFor([&] { return f.isFinished(); }, kSyncTimeoutMs);

        bool sawHold = false;
        for (const auto& r : f.resultAt(0)) {
            if (!r.success && !r.unresolvedConflicts.isEmpty()) {
                sawHold = true;
                // O56: no data was written for the conflicted mapping.
                QCOMPARE(r.sourceStats.updated, 0);
                QCOMPARE(r.targetStats.updated, 0);
                QCOMPARE(r.sourceStats.deleted, 0);
                QCOMPARE(r.targetStats.deleted, 0);
            }
        }
        QVERIFY2(sawHold, "diverged vendor edits must defer as AskUser");
        QVERIFY(conflicts.count() >= 1);

        // Both divergent copies survive untouched.
        QCOMPARE(m_hub->loadRecords(kCollection).size(), 1);
        QCOMPARE(m_ms->loadRecords(kCollection).size(), 1);
        QCOMPARE(Kalburator::Shape::CanonEnvelope::parse(
                     m_hub->loadRecords(kCollection).first().data)
                     .value(QStringLiteral("summary"))
                     .toString(),
                 QStringLiteral("Hub-side edit"));
    }

    // Part IV payoff slot: the CONVERGED world answers a human question.
    // Ingest contacts from both vendors into PersonDirectory, observe the
    // hub's promoted canon event, resolve the roster to NAMED persons.
    void rosterResolvesNamedPersonsAfterConvergence()
    {
        buildWorld();

        QVERIFY(!m_g->seedRecord(kCollection, QStringLiteral("g-r"),
                                 compact(googleWire()))
                     .isEmpty());
        SyncRequest req;
        req.behavior = SyncEngine::SyncBehavior::Unmonitored;
        auto f = m_engine->runSync(req);
        QTest::qWaitFor([&] { return f.isFinished(); }, kSyncTimeoutMs);
        for (const auto& r : f.resultAt(0))
            QVERIFY(r.success);

        IdentityStore istore(m_dir->filePath("identity.db"));
        PersonDirectory directory(istore);

        // Both vendors' people, one directory: Google contact for the
        // organizer, Graph contact for the attendee.
        using Kalburator::Contacts::GooglePersonToCanonStage;
        using Kalburator::Contacts::MsContactToCanonStage;
        QVERIFY2(!directory.observe(GooglePersonToCanonStage{}.transform(
                     QByteArray(
                         "{\"resourceName\": \"people/ada-g\","
                         "\"names\": [{\"displayName\": \"Ada Lovelace\"}],"
                         "\"emailAddresses\": "
                         "[{\"value\": \"ada@example.com\"}]}")))
                     .isEmpty(),
                 "ada observe failed");
        QVERIFY2(!directory.observe(MsContactToCanonStage{}.transform(
                     QByteArray(
                         "{\"id\": \"AAMk-bob-ms\","
                         "\"displayName\": \"Bob Attendee\","
                         "\"emailAddresses\": [{\"address\": \"bob@example.com\", "
                         "\"name\": \"Bob Attendee\"}]}")))
                     .isEmpty(),
                 "bob observe failed");

        // Observe the hub's CANON event record directly.
        const auto hubRecs = m_hub->loadRecords(kCollection);
        QCOMPARE(hubRecs.size(), 1);
        QVERIFY(!directory.observe(hubRecs.first().data).isEmpty());

        const auto roster = directory.eventRoster(hubRecs.first().data);
        QCOMPARE(roster.size(), 2);
        QCOMPARE(roster[0].email, QStringLiteral("ada@example.com"));
        QCOMPARE(roster[0].displayName, QStringLiteral("Ada Lovelace"));
        QCOMPARE(roster[1].email, QStringLiteral("bob@example.com"));
        QCOMPARE(roster[1].displayName, QStringLiteral("Bob Attendee"));
        for (const auto& entry : roster)
            QVERIFY2(!entry.entityId.isEmpty(),
                     "every rostered person resolved to an entity");
    }

private:
    void buildWorld()
    {
        const Shape googleShape{ DomainId{QStringLiteral("calendar")},
                                 EncodingId{QStringLiteral("google-event")} };
        const Shape msShape{ DomainId{QStringLiteral("calendar")},
                             EncodingId{QStringLiteral("ms-event")} };
        const Shape canonShape{ DomainId{QStringLiteral("calendar")},
                                EncodingId{QStringLiteral("canon")} };

        m_g = std::make_unique<VendorWireBackend>(
            QStringLiteral("vendor-google"), googleShape);
        m_hub = std::make_unique<GenericSqliteBackend>(
            m_dir->filePath("hub.db"));
        m_ms = std::make_unique<VendorWireBackend>(
            QStringLiteral("vendor-ms"), msShape);

        CollectionInfo col;
        col.id = kCollection;
        col.name = kCollection;
        col.type = QStringLiteral("calendar");
        QVERIFY(!m_g->createCollection(col).isEmpty());
        QVERIFY(!m_ms->createCollection(col).isEmpty());
        QVERIFY(!m_hub->createCollection(col, canonShape).isEmpty());

        m_registry.registerBackendInstance(m_g->backendId(), m_g.get());
        m_registry.registerBackendInstance(m_hub->backendId(), m_hub.get());
        m_registry.registerBackendInstance(m_ms->backendId(), m_ms.get());
        m_host = std::make_unique<RegistrySyncHost>(&m_registry);
        m_engine =
            std::make_unique<SyncEngine>(&m_registry, m_host.get(), m_shape);

        m_baselines =
            std::make_unique<BaselineStore>(m_dir->filePath("baselines.db"));
        m_engine->setBaselineStore(m_baselines.get());

        SyncMapping mg;
        mg.id = kMappingG;
        mg.sourceBackend = m_g->backendId();
        mg.sourceCalendar = kCollection;
        mg.targetBackend = m_hub->backendId();
        mg.targetCalendar = kCollection;
        mg.mode = SyncMode::TwoWay;
        mg.conflictPolicy = ConflictResolution::AskUser;
        mg.enabled = true;

        SyncMapping mm;
        mm.id = kMappingM;
        mm.sourceBackend = m_hub->backendId();
        mm.sourceCalendar = kCollection;
        mm.targetBackend = m_ms->backendId();
        mm.targetCalendar = kCollection;
        mm.mode = SyncMode::TwoWay;
        mm.conflictPolicy = ConflictResolution::AskUser;
        mm.enabled = true;

        m_engine->setSyncMappings({ mg, mm });
    }

    static BackendRecord withData(const BackendRecord& base,
                                  const QByteArray& data)
    {
        BackendRecord r = base;
        r.data = data;
        r.contentHash = QString::fromLatin1(
            QCryptographicHash::hash(data, QCryptographicHash::Sha256)
                .toHex());
        r.lastModified = QDateTime::currentDateTimeUtc();
        return r;
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    ShapeRegistries m_shape;
    BackendRegistry m_pmRegistry;
    BackendRegistry m_registry;
    std::unique_ptr<VendorWireBackend> m_g;
    std::unique_ptr<GenericSqliteBackend> m_hub;
    std::unique_ptr<VendorWireBackend> m_ms;
    std::unique_ptr<RegistrySyncHost> m_host;
    std::unique_ptr<SyncEngine> m_engine;
    std::unique_ptr<BaselineStore> m_baselines;
};

QTEST_MAIN(TestEngineVendorShapedHub)
#include "tst_engine_vendor_shaped_hub.moc"
