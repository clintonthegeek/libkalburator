// Phase Ia.5 Task 20 — engine-merger unified-path integration test.
//
// Mirrors WP-side tst_contacts_palm_engine_sync.cpp (Task 19) using
// libkalburator-internal shapes only: source declares (contacts, vcard3),
// target declares (contacts, vcard4). The vcard3<->vcard4 edge is
// statically registered by KalburatorDomainContacts::registerEdges.
//
// What this pins (positive end-to-end behavior of the unified path):
//
//   1. Pipeline was compiled — host's syncStarted received a non-default
//      LossProfile. vcard3 -> vcard4 is Lossless per
//      contactsdomainplugin.cpp.
//
//   2. Target receives transformed bytes — target record's `data`
//      contains "VERSION:4.0" and parses via KContacts::VCardConverter
//      as a single Addressee with formattedName matching the seed.
//
//   3. Source bytes are unchanged — still vCard 3 if read back.
//
// The unified dispatchSync path (Phase Ia.5 Tasks 13-18) compiles the
// (contacts, vcard3) -> (contacts, vcard4) Pipeline via
// TransformationRegistry::compile and applies it before pushing to the
// target via the plugin's IRecordWriter (Tasks 8/11).

#include <QtTest/QtTest>
#include <QObject>
#include <QString>
#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFuture>
#include <QTemporaryDir>

#include <KCalendarCore/MemoryCalendar>
#include <KContacts/Addressee>
#include <KContacts/VCardConverter>

#include <memory>

#include "backendrecord.h"
#include "backendregistry.h"
#include "baselinestore.h"
#include "collectioninfo.h"
#include "domainregistry.h"
#include "iblobbackend.h"
#include "isynchost.h"
#include "lossprofile.h"
#include "shape.h"
#include "syncbackend.h"
#include "syncengine.h"
#include "synctypes.h"
#include "transformationregistry.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::BackendRegistry;
using Kalburator::Storage::BaselineStore;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::ConflictResolution;
using Kalburator::Sync::IBlobBackend;
using Kalburator::Sync::ISyncConfigStore;
using Kalburator::Sync::ISyncHost;
using Kalburator::Sync::SyncBackend;
using Kalburator::Sync::SyncEngine;
using Kalburator::Sync::SyncMapping;
using Kalburator::Sync::SyncMode;
using Kalburator::Sync::SyncResult;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::DomainRegistry;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::LossLevel;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::Shape;
using Kalburator::Shape::TransformationRegistry;

namespace {

constexpr int kSyncTimeoutMs = 5000;

// ──────────────────────────────────────────────────────────────────────────────
// ShapedTestBackend
// ──────────────────────────────────────────────────────────────────────────────
// SyncBackend subclass that stores BackendRecords in memory for a single
// collection. Shape is configurable per-instance so the same class can
// serve as the (contacts, vcard3) source and (contacts, vcard4) target.
//
// Mirrors WP's ShapedTestBackend in tst_contacts_palm_engine_sync.cpp;
// kept inline here because the engine-test target doesn't link any
// shared test-stub library beyond kalburator_calendar_test_stubs.
//
// Calendar pure-virtuals are no-op stubs — never reached because the
// unified dispatchSync routes (contacts, …) through the blob path, not
// dispatchCalendarLegacy.
// ──────────────────────────────────────────────────────────────────────────────
class ShapedTestBackend final : public SyncBackend
{
    Q_OBJECT
public:
    ShapedTestBackend(const QString &id,
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
        ++m_loadRecordsCalls;
        return m_records;
    }
    std::optional<BackendRecord> loadRecord(const QString &id) override
    {
        for (const auto &r : m_records)
            if (r.id == id) return r;
        return std::nullopt;
    }
    QString createRecord(const QString &, const BackendRecord &record) override
    {
        ++m_createRecordCalls;
        m_lastCreated = record;
        m_records.append(record);
        return record.id;
    }
    bool updateRecord(const BackendRecord &record) override
    {
        ++m_updateRecordCalls;
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

    // Calendar pure-virtuals — stubs; the unified dispatchSync's
    // contacts branch never invokes these.
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
    void seedRecord(const BackendRecord &record) { m_records.append(record); }
    QList<BackendRecord> snapshot() const { return m_records; }
    int createRecordCalls() const { return m_createRecordCalls; }
    int updateRecordCalls() const { return m_updateRecordCalls; }
    BackendRecord lastCreated() const { return m_lastCreated; }

private:
    QString m_id;
    Shape m_shape;
    QString m_collectionId;
    QList<CollectionInfo> m_collections;
    QList<BackendRecord> m_records;
    int m_createRecordCalls = 0;
    int m_updateRecordCalls = 0;
    int m_loadRecordsCalls = 0;
    BackendRecord m_lastCreated;
};

// ──────────────────────────────────────────────────────────────────────────────
// CapturingSyncHost
// ──────────────────────────────────────────────────────────────────────────────
// Minimal ISyncHost that captures the LossProfile passed to syncStarted.
// recordChanged is a no-op — assertions key off the target backend's
// createRecord call count and snapshot.
// ──────────────────────────────────────────────────────────────────────────────
class CapturingSyncHost final : public ISyncHost
{
public:
    explicit CapturingSyncHost(BackendRegistry *registry) : m_registry(registry) {}

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

    void syncStarted(const QString &mappingId, const LossProfile &loss) override
    {
        m_lastMappingId = mappingId;
        m_lastLoss = loss;
        ++m_syncStartedCount;
    }

    void recordChanged(const QString &, const QString &, ChangeKind) override {}

    int syncStartedCount() const { return m_syncStartedCount; }
    LossProfile lastLossProfile() const { return m_lastLoss; }

private:
    BackendRegistry *m_registry = nullptr;
    int m_syncStartedCount = 0;
    LossProfile m_lastLoss;
    QString m_lastMappingId;
};

constexpr const char *kSeedFn = "Jane Doe";

BackendRecord seedVCard3Record(const QString &recordId)
{
    // KContacts is tolerant of \n line endings; the registered
    // VCard3To4Stage parses via VCardConverter::parseVCards which
    // handles either CRLF or LF.
    BackendRecord rec;
    rec.id          = recordId;
    rec.type        = QStringLiteral("contact");
    rec.displayName = QString::fromLatin1(kSeedFn);
    rec.data =
        "BEGIN:VCARD\r\n"
        "VERSION:3.0\r\n"
        "UID:jane-doe-1\r\n"
        "FN:Jane Doe\r\n"
        "N:Doe;Jane;;;\r\n"
        "EMAIL:jane@example.com\r\n"
        "END:VCARD\r\n";
    rec.contentHash = QString::fromLatin1(
        QCryptographicHash::hash(rec.data, QCryptographicHash::Sha1).toHex());
    rec.lastModified = QDateTime::currentDateTimeUtc();
    rec.isDeleted    = false;
    return rec;
}

} // namespace

class TestEngineUnifiedRouting : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // The unified dispatchSync path compiles a (vcard3 -> vcard4)
    // pipeline and applies it before pushing to the target.
    void unifiedPath_transformsBytesAtEdge();
};

void TestEngineUnifiedRouting::initTestCase()
{
    // KalburatorDomainContacts is registered via static initializer
    // pulled in by --whole-archive. Run initialize() so the contacts
    // plugin's registerEdges() populates the TransformationRegistry
    // with vcard3<->vcard4 edges (and the canonical identity edge).
    DomainRegistry::instance().initialize(TransformationRegistry::instance());
}

void TestEngineUnifiedRouting::cleanupTestCase()
{
    // Process-wide singletons leak across test classes; reset before
    // any other test in this binary touches them. (No other slots
    // exist here today, but this matches the FINDINGS guidance.)
    TransformationRegistry::instance().clear();
    DomainRegistry::instance().clear();
}

void TestEngineUnifiedRouting::unifiedPath_transformsBytesAtEdge()
{
    // ── Arrange ───────────────────────────────────────────────────────
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    BackendRegistry registry;

    const QString sourceBackendId  = QStringLiteral("vcard3-source");
    const QString targetBackendId  = QStringLiteral("vcard4-target");
    const QString sourceCollection = QStringLiteral("src-contacts");
    const QString targetCollection = QStringLiteral("tgt-contacts");
    const QString mappingId        = QStringLiteral("ia5-task20-mapping");

    const Shape v3Shape{ DomainId{"contacts"}, EncodingId{"vcard3"} };
    const Shape v4Shape{ DomainId{"contacts"}, EncodingId{"vcard4"} };

    auto source = std::make_unique<ShapedTestBackend>(
        sourceBackendId, v3Shape, sourceCollection);
    auto target = std::make_unique<ShapedTestBackend>(
        targetBackendId, v4Shape, targetCollection);

    const BackendRecord seeded = seedVCard3Record(QStringLiteral("rec-1"));
    source->seedRecord(seeded);

    registry.registerBackendInstance(sourceBackendId, source.get());
    registry.registerBackendInstance(targetBackendId, target.get());

    CapturingSyncHost host(&registry);

    SyncEngine engine(&registry, &host);

    // The unified dispatchSync path uses BaselineStore for the
    // contacts domain; provide one so first-sync baseline writes don't
    // silently no-op. Cleaned by tmpDir.
    BaselineStore baselines(
        tmpDir.filePath(QStringLiteral("blob-baselines.db")));
    engine.setBaselineStore(&baselines);

    SyncMapping mapping;
    mapping.id              = mappingId;
    mapping.sourceBackend   = sourceBackendId;
    mapping.sourceCalendar  = sourceCollection;
    mapping.targetBackend   = targetBackendId;
    mapping.targetCalendar  = targetCollection;
    // OneWayUpload pushes source -> target only, simplifying the
    // assertions (no bidirectional baseline comparisons).
    mapping.mode            = SyncMode::OneWayUpload;
    mapping.conflictPolicy  = ConflictResolution::SourceWins;
    mapping.enabled         = true;
    engine.setSyncMappings({ mapping });

    // ── Act ───────────────────────────────────────────────────────────
    auto future = engine.runSyncFuture(
        mappingId, SyncEngine::SyncBehavior::Unmonitored);

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    const SyncResult result = future.resultAt(0);
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    // ── Assert: Pipeline was compiled (host received LossProfile) ─────
    // vcard3 -> vcard4 is the lossless direction (LossProfile defaults
    // to Lossless; contactsdomainplugin.cpp registers this edge with a
    // default LossProfile). A non-Lossless or sentinel value would
    // mean the engine took the no-edge path or skipped registry consult.
    QCOMPARE(host.syncStartedCount(), 1);
    QCOMPARE(host.lastLossProfile().level, LossLevel::Lossless);

    // ── Assert: target received exactly one createRecord ──────────────
    QCOMPARE(target->createRecordCalls(), 1);
    QCOMPARE(target->updateRecordCalls(), 0);
    const QList<BackendRecord> targetSnapshot = target->snapshot();
    QCOMPARE(targetSnapshot.size(), 1);

    // ── Assert: target bytes are vCard 4.0, not the source verbatim ───
    const BackendRecord arrived = targetSnapshot.first();
    QCOMPARE(arrived.id, seeded.id);
    QVERIFY2(arrived.data.contains(QByteArrayLiteral("BEGIN:VCARD")),
             qPrintable(QStringLiteral(
                 "Expected vCard 4.0 markers after pipeline; got:\n%1")
                     .arg(QString::fromUtf8(arrived.data.left(200)))));
    QVERIFY2(arrived.data.contains(QByteArrayLiteral("VERSION:4.0")),
             qPrintable(QStringLiteral(
                 "Expected VERSION:4.0 marker (transformed); got:\n%1")
                     .arg(QString::fromUtf8(arrived.data.left(200)))));
    QVERIFY2(!arrived.data.contains(QByteArrayLiteral("VERSION:3.0")),
             "Target bytes still contain VERSION:3.0 — pipeline didn't run");
    // The transformed bytes must differ from the source bytes.
    QVERIFY(arrived.data != seeded.data);

    // ── Assert: target bytes parse via KContacts ──────────────────────
    KContacts::VCardConverter conv;
    const auto addressees = conv.parseVCards(arrived.data);
    QCOMPARE(addressees.size(), 1);
    const KContacts::Addressee &a = addressees.first();
    QCOMPARE(a.formattedName(), QString::fromLatin1(kSeedFn));

    // ── Assert: source bytes still parse as vCard 3 ───────────────────
    // Source-side data is unchanged; the engine pushes transformed
    // bytes to the target without writing back to the source.
    const auto sourceSnapshot = source->snapshot();
    QCOMPARE(sourceSnapshot.size(), 1);
    QVERIFY(sourceSnapshot.first().data.contains(QByteArrayLiteral("VERSION:3.0")));
}

QTEST_MAIN(TestEngineUnifiedRouting)
#include "tst_engine_unified_routing.moc"
