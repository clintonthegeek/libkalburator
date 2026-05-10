// Phase K.3 — contacts engine witness.
//
// Proves the full engine pipeline (fetch → diff → conflict → write → cancel)
// works end-to-end using KalburatorDomainContacts + contacts-shaped backends +
// vCard4 transform, without exercising any calendar-domain code paths.
//
// The five cases in §5 of 04ab-phase-k-engine-generalization-design.md:
//   1. bidirectionalAdd     — A creates contact, B creates different contact,
//                             TwoWay sync propagates both to the other side.
//   2. contentHashSkip      — second sync with no changes produces no write calls.
//   3. conflict_askUser     — both sides modify same record, ConflictManager
//                             resolves SourceWins.
//   4. cancelMidSync        — future.cancel() stops the sync; future completes.
//   5. vcard3To4Transform   — source declares vcard3, target declares vcard4,
//                             engine routes through TransformationRegistry.
//
// NOTE (K.3): These tests use SyncBackend-derived backends because BackendRegistry
// currently accepts SyncBackend* only. The ldd/nm KCalendarCore-absence check
// from the design doc is aspirational until K.4 strips the calendar virtuals
// from SyncBackend. All five test cases exercise zero calendar code paths.

#include <QtTest/QtTest>
#include <QObject>
#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFuture>
#include <QString>
#include <QTemporaryDir>

#include <KCalendarCore/MemoryCalendar>

#include <memory>

#include "backendrecord.h"
#include "backendregistry.h"
#include "blobbaselinestore.h"
#include "canonicalrecord.h"
#include "collectioninfo.h"
#include "conflictmanager.h"
#include "domainregistry.h"
#include "iblobbackend.h"
#include "isynchost.h"
#include "shape.h"
#include "syncbackend.h"
#include "syncconflictstore.h"
#include "syncengine.h"
#include "synctypes.h"
#include "transformationregistry.h"

using Kalburator::Shape::CanonicalRecord;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::DomainRegistry;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::Shape;
using Kalburator::Shape::TransformationRegistry;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::BackendRegistry;
using Kalburator::Storage::BaselineStore;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::ConflictManager;
using Kalburator::Sync::ConflictResolution;
using Kalburator::Sync::ISyncConfigStore;
using Kalburator::Sync::ISyncHost;
using Kalburator::Sync::SyncBackend;
using Kalburator::Sync::SyncConflictStore;
using Kalburator::Sync::SyncEngine;
using Kalburator::Sync::SyncMapping;
using Kalburator::Sync::SyncMode;
using Kalburator::Sync::SyncResult;

namespace {

constexpr int kSyncTimeoutMs = 5000;

// ──────────────────────────────────────────────────────────────────────────────
// ContactsWitnessBackend
//
// In-memory SyncBackend for contacts tests. Shape is configurable; calendar
// pure-virtuals are never reached by the contacts engine path.
// ──────────────────────────────────────────────────────────────────────────────
class ContactsWitnessBackend final : public SyncBackend
{
    Q_OBJECT
public:
    ContactsWitnessBackend(const QString &id,
                            Shape shape,
                            const QString &collectionId,
                            QObject *parent = nullptr)
        : SyncBackend(parent)
        , m_id(id)
        , m_shape(std::move(shape))
        , m_collectionId(collectionId)
    {
        CollectionInfo info;
        info.id   = collectionId;
        info.name = collectionId;
        info.type = QStringLiteral("contacts");
        m_collections.append(info);
    }

    // SyncBackend identity
    QString backendType() const override { return m_id; }
    QList<Shape> nativeShapes() const override { return { m_shape }; }

    // IBlobBackend identity
    QString backendId()   const override { return m_id; }
    QString displayName() const override { return m_id; }
    bool    isAvailable() const override { return true; }

    // Collections
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

    // Records
    QList<BackendRecord> loadRecords(const QString &) override
    {
        ++m_loadCalls;
        return m_records;
    }
    std::optional<BackendRecord> loadRecord(const QString &id) override
    {
        for (const auto &r : m_records)
            if (r.id == id) return r;
        return std::nullopt;
    }
    QString createRecord(const QString &, const BackendRecord &rec) override
    {
        ++m_createCalls;
        m_records.append(rec);
        return rec.id;
    }
    bool updateRecord(const BackendRecord &rec) override
    {
        ++m_updateCalls;
        for (auto &r : m_records) {
            if (r.id == rec.id) { r = rec; return true; }
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

    // Calendar stubs — unreachable on contacts path.
    void loadCalendars(const QString &) override {}
    void storeCalendars(const QString &,
                        const QList<KCalendarCore::MemoryCalendar *> &) override {}
    void startSync(const QString &,
                   KCalendarCore::MemoryCalendar *,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QMap<QString, QString> &,
                   const Kalburator::Sync::TranscodingPlan &) override {}
    void removeItem(const QString &, const QString &) override {}

    // Test seams
    void seedRecord(const BackendRecord &rec) { m_records.append(rec); }
    QList<BackendRecord> snapshot() const { return m_records; }
    int createCalls() const { return m_createCalls; }
    int updateCalls() const { return m_updateCalls; }
    int loadCalls()   const { return m_loadCalls; }

private:
    QString m_id;
    Shape m_shape;
    QString m_collectionId;
    QList<CollectionInfo> m_collections;
    QList<BackendRecord> m_records;
    int m_createCalls = 0;
    int m_updateCalls = 0;
    int m_loadCalls   = 0;
};

// ──────────────────────────────────────────────────────────────────────────────
// StubHost
// ──────────────────────────────────────────────────────────────────────────────
class StubHost final : public ISyncHost
{
public:
    explicit StubHost(BackendRegistry *reg) : m_registry(reg) {}

    SyncBackend *backendById(const QString &id) override
    {
        return m_registry ? m_registry->backendInstance(id) : nullptr;
    }
    QHash<QString, SyncBackend *> backends() override
    {
        QHash<QString, SyncBackend *> out;
        if (!m_registry) return out;
        for (const auto &id : m_registry->registeredInstanceIds())
            out.insert(id, m_registry->backendInstance(id));
        return out;
    }
    ISyncConfigStore *configStore() override { return nullptr; }
    void syncStarted(const QString &, const Kalburator::Shape::LossProfile &) override {}
    void recordChanged(const QString &, const QString &, ChangeKind) override {}

private:
    BackendRegistry *m_registry = nullptr;
};

// ──────────────────────────────────────────────────────────────────────────────
// Helpers
// ──────────────────────────────────────────────────────────────────────────────
static const Shape kV4Shape{ DomainId{"contacts"}, EncodingId{"vcard4"} };
static const Shape kV3Shape{ DomainId{"contacts"}, EncodingId{"vcard3"} };

BackendRecord makeVCard4Record(const QString &id, const QString &fn)
{
    BackendRecord rec;
    rec.id          = id;
    rec.type        = QStringLiteral("contact");
    rec.displayName = fn;
    rec.data        = QStringLiteral(
        "BEGIN:VCARD\r\nVERSION:4.0\r\nUID:%1\r\nFN:%2\r\nEND:VCARD\r\n")
        .arg(id, fn).toUtf8();
    rec.contentHash = QString::fromUtf8(
        QCryptographicHash::hash(rec.data, QCryptographicHash::Sha1).toHex());
    rec.lastModified = QDateTime::currentDateTimeUtc();
    return rec;
}

BackendRecord makeVCard3Record(const QString &id, const QString &fn)
{
    BackendRecord rec;
    rec.id          = id;
    rec.type        = QStringLiteral("contact");
    rec.displayName = fn;
    rec.data        = QStringLiteral(
        "BEGIN:VCARD\r\nVERSION:3.0\r\nUID:%1\r\nFN:%2\r\nN:%2;;;\r\nEND:VCARD\r\n")
        .arg(id, fn).toUtf8();
    rec.contentHash = QString::fromUtf8(
        QCryptographicHash::hash(rec.data, QCryptographicHash::Sha1).toHex());
    rec.lastModified = QDateTime::currentDateTimeUtc();
    return rec;
}

SyncMapping makeTwoWayMapping(const QString &srcId, const QString &srcCol,
                               const QString &tgtId, const QString &tgtCol,
                               const QString &mappingId,
                               ConflictResolution policy = ConflictResolution::SourceWins)
{
    SyncMapping m;
    m.id             = mappingId;
    m.sourceBackend  = srcId;
    m.sourceCalendar = srcCol;
    m.targetBackend  = tgtId;
    m.targetCalendar = tgtCol;
    m.mode           = SyncMode::TwoWay;
    m.conflictPolicy = policy;
    m.enabled        = true;
    return m;
}

} // namespace

class TstContactsEngineWitness : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // Case 1: A has contact X, B has contact Y → TwoWay sync → both have X+Y.
    void bidirectionalAdd_bothSidesGetEachOthersContact();

    // Case 2: After first sync, a second sync with no changes produces no writes.
    void contentHashSkip_secondSyncProducesNoWrites();

    // Case 3: BothModified conflict with AskUser policy → ConflictManager
    //         auto-resolves SourceWins.
    void conflict_askUserAutoResolves_sourceWins();

    // Case 4: future.cancel() before sync completes → future ends cleanly.
    void cancelBeforeSync_futureCompletesCleanly();

    // Case 5: Source (vcard3) → Target (vcard4) → engine applies transform.
    void vcard3To4Transform_targetBytesContainVersion4();
};

void TstContactsEngineWitness::initTestCase()
{
    DomainRegistry::instance().initialize(TransformationRegistry::instance());
}

void TstContactsEngineWitness::cleanupTestCase()
{
    TransformationRegistry::instance().clear();
    DomainRegistry::instance().clear();
}

void TstContactsEngineWitness::bidirectionalAdd_bothSidesGetEachOthersContact()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    BackendRegistry registry;
    auto src = std::make_unique<ContactsWitnessBackend>(
        QStringLiteral("src"), kV4Shape, QStringLiteral("col"));
    auto tgt = std::make_unique<ContactsWitnessBackend>(
        QStringLiteral("tgt"), kV4Shape, QStringLiteral("col"));

    src->seedRecord(makeVCard4Record(QStringLiteral("alice"), QStringLiteral("Alice")));
    tgt->seedRecord(makeVCard4Record(QStringLiteral("bob"),   QStringLiteral("Bob")));

    registry.registerBackendInstance(QStringLiteral("src"), src.get());
    registry.registerBackendInstance(QStringLiteral("tgt"), tgt.get());

    StubHost host(&registry);
    SyncEngine engine(&registry, &host);

    BaselineStore baselines(tmp.filePath(QStringLiteral("bl.db")));
    engine.setBaselineStore(&baselines);
    engine.setSyncMappings({ makeTwoWayMapping(
        QStringLiteral("src"), QStringLiteral("col"),
        QStringLiteral("tgt"), QStringLiteral("col"),
        QStringLiteral("m")) });

    auto future = engine.runSyncFuture(
        QStringLiteral("m"), SyncEngine::SyncBehavior::Unmonitored);
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);

    const SyncResult result = future.resultAt(0);
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    // Both sides now have both contacts.
    const QList<BackendRecord> srcSnap = src->snapshot();
    const QList<BackendRecord> tgtSnap = tgt->snapshot();
    QCOMPARE(srcSnap.size(), 2);
    QCOMPARE(tgtSnap.size(), 2);

    auto hasId = [](const QList<BackendRecord> &list, const QString &id) {
        for (const auto &r : list) if (r.id == id) return true;
        return false;
    };
    QVERIFY(hasId(srcSnap, QStringLiteral("alice")));
    QVERIFY(hasId(srcSnap, QStringLiteral("bob")));
    QVERIFY(hasId(tgtSnap, QStringLiteral("alice")));
    QVERIFY(hasId(tgtSnap, QStringLiteral("bob")));
}

void TstContactsEngineWitness::contentHashSkip_secondSyncProducesNoWrites()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    BackendRegistry registry;
    auto src = std::make_unique<ContactsWitnessBackend>(
        QStringLiteral("src"), kV4Shape, QStringLiteral("col"));
    auto tgt = std::make_unique<ContactsWitnessBackend>(
        QStringLiteral("tgt"), kV4Shape, QStringLiteral("col"));

    src->seedRecord(makeVCard4Record(QStringLiteral("alice"), QStringLiteral("Alice")));

    registry.registerBackendInstance(QStringLiteral("src"), src.get());
    registry.registerBackendInstance(QStringLiteral("tgt"), tgt.get());

    StubHost host(&registry);
    SyncEngine engine(&registry, &host);

    BaselineStore baselines(tmp.filePath(QStringLiteral("bl.db")));
    engine.setBaselineStore(&baselines);
    engine.setSyncMappings({ makeTwoWayMapping(
        QStringLiteral("src"), QStringLiteral("col"),
        QStringLiteral("tgt"), QStringLiteral("col"),
        QStringLiteral("m")) });

    // First sync: propagates alice from src → tgt.
    {
        auto f = engine.runSyncFuture(
            QStringLiteral("m"), SyncEngine::SyncBehavior::Unmonitored);
        QTRY_VERIFY_WITH_TIMEOUT(f.isFinished(), kSyncTimeoutMs);
        QVERIFY(f.resultAt(0).success);
    }
    QCOMPARE(tgt->createCalls(), 1);
    QCOMPARE(tgt->updateCalls(), 0);

    // Second sync: nothing changed — contentHash in baselines matches fetched
    // records on both sides. No new writes expected.
    {
        auto f = engine.runSyncFuture(
            QStringLiteral("m"), SyncEngine::SyncBehavior::Unmonitored);
        QTRY_VERIFY_WITH_TIMEOUT(f.isFinished(), kSyncTimeoutMs);
        QVERIFY(f.resultAt(0).success);
    }
    QCOMPARE(tgt->createCalls(), 1);  // still 1, no new create
    QCOMPARE(tgt->updateCalls(), 0);
    QCOMPARE(src->createCalls(), 0);
    QCOMPARE(src->updateCalls(), 0);
}

void TstContactsEngineWitness::conflict_askUserAutoResolves_sourceWins()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString dbPath = tmp.filePath(QStringLiteral("sync.db"));

    BackendRegistry registry;
    auto src = std::make_unique<ContactsWitnessBackend>(
        QStringLiteral("src"), kV4Shape, QStringLiteral("col"));
    auto tgt = std::make_unique<ContactsWitnessBackend>(
        QStringLiteral("tgt"), kV4Shape, QStringLiteral("col"));

    // Seed a baseline so the engine sees BothModified (not BothCreated).
    const BackendRecord baseline =
        makeVCard4Record(QStringLiteral("alice"), QStringLiteral("Alice Original"));
    {
        BaselineStore bl(dbPath);
        CanonicalRecord cr;
        cr.shape    = kV4Shape;
        cr.recordId = baseline.id;
        cr.data     = baseline.data;
        bl.setBaselineV3(QStringLiteral("m"), cr);
    }

    // Both sides have divergent versions of the same record.
    src->seedRecord(makeVCard4Record(QStringLiteral("alice"), QStringLiteral("Alice From Source")));
    tgt->seedRecord(makeVCard4Record(QStringLiteral("alice"), QStringLiteral("Alice From Target")));

    registry.registerBackendInstance(QStringLiteral("src"), src.get());
    registry.registerBackendInstance(QStringLiteral("tgt"), tgt.get());

    StubHost host(&registry);
    BaselineStore baselines(dbPath);
    SyncConflictStore conflictStore(dbPath);

    ConflictManager mgr;
    mgr.setSyncConflictStore(&conflictStore);
    mgr.setWorkflowMode(ConflictManager::WorkflowMode::AutoResolve);
    mgr.setAutoResolutionPolicy(ConflictResolution::SourceWins);

    SyncEngine engine(&registry, &host);
    engine.setBaselineStore(&baselines);
    engine.setSyncConflictStore(&conflictStore);
    engine.setConflictManager(&mgr);
    engine.setSyncMappings({ makeTwoWayMapping(
        QStringLiteral("src"), QStringLiteral("col"),
        QStringLiteral("tgt"), QStringLiteral("col"),
        QStringLiteral("m"), ConflictResolution::AskUser) });

    auto future = engine.runSyncFuture(
        QStringLiteral("m"), SyncEngine::SyncBehavior::Monitored);
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    QVERIFY(future.resultAt(0).success);

    // SourceWins: target should have the source version.
    const QList<BackendRecord> tgtSnap = tgt->snapshot();
    QCOMPARE(tgtSnap.size(), 1);
    QVERIFY(tgtSnap.first().data.contains("Alice From Source"));
}

void TstContactsEngineWitness::cancelBeforeSync_futureCompletesCleanly()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    BackendRegistry registry;
    auto src = std::make_unique<ContactsWitnessBackend>(
        QStringLiteral("src"), kV4Shape, QStringLiteral("col"));
    auto tgt = std::make_unique<ContactsWitnessBackend>(
        QStringLiteral("tgt"), kV4Shape, QStringLiteral("col"));

    registry.registerBackendInstance(QStringLiteral("src"), src.get());
    registry.registerBackendInstance(QStringLiteral("tgt"), tgt.get());

    StubHost host(&registry);
    SyncEngine engine(&registry, &host);

    BaselineStore baselines(tmp.filePath(QStringLiteral("bl.db")));
    engine.setBaselineStore(&baselines);
    engine.setSyncMappings({ makeTwoWayMapping(
        QStringLiteral("src"), QStringLiteral("col"),
        QStringLiteral("tgt"), QStringLiteral("col"),
        QStringLiteral("m")) });

    auto future = engine.runSyncFuture(
        QStringLiteral("m"), SyncEngine::SyncBehavior::Unmonitored);
    future.cancel();

    // The future must complete (cancelled or succeeded) — no hang.
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    // Either cancelled or ran to completion before the cancel was observed.
    QVERIFY(future.isCanceled() || future.resultAt(0).success);
}

void TstContactsEngineWitness::vcard3To4Transform_targetBytesContainVersion4()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    BackendRegistry registry;
    auto src = std::make_unique<ContactsWitnessBackend>(
        QStringLiteral("src"), kV3Shape, QStringLiteral("col"));
    auto tgt = std::make_unique<ContactsWitnessBackend>(
        QStringLiteral("tgt"), kV4Shape, QStringLiteral("col"));

    src->seedRecord(makeVCard3Record(
        QStringLiteral("alice"), QStringLiteral("Alice")));

    registry.registerBackendInstance(QStringLiteral("src"), src.get());
    registry.registerBackendInstance(QStringLiteral("tgt"), tgt.get());

    StubHost host(&registry);
    SyncEngine engine(&registry, &host);

    BaselineStore baselines(tmp.filePath(QStringLiteral("bl.db")));
    engine.setBaselineStore(&baselines);

    SyncMapping mapping = makeTwoWayMapping(
        QStringLiteral("src"), QStringLiteral("col"),
        QStringLiteral("tgt"), QStringLiteral("col"),
        QStringLiteral("m"));
    mapping.mode = SyncMode::OneWayUpload;
    engine.setSyncMappings({ mapping });

    auto future = engine.runSyncFuture(
        QStringLiteral("m"), SyncEngine::SyncBehavior::Unmonitored);
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    QVERIFY2(future.resultAt(0).success,
             qUtf8Printable(future.resultAt(0).errorMessage));

    const QList<BackendRecord> tgtSnap = tgt->snapshot();
    QCOMPARE(tgtSnap.size(), 1);
    QVERIFY2(tgtSnap.first().data.contains("VERSION:4.0"),
             qPrintable(QStringLiteral("Expected VERSION:4.0 in target; got: %1")
                .arg(QString::fromUtf8(tgtSnap.first().data.left(200)))));
    QVERIFY(!tgtSnap.first().data.contains("VERSION:3.0"));
}

QTEST_MAIN(TstContactsEngineWitness)
#include "tst_contacts_engine_witness.moc"
