// Phase K.9 — engine dispatch when one side is a universal sink.
//
// What this pins: a SyncEngine sync from a typed-shape source backend to
// a RawFilesBackend target must succeed. Before K.9 the engine bailed at
// dispatchSync's cross-domain check because RawFilesBackend declared
// Shape::Any (domain "__any__") instead of the source's real domain.
// K.9 abolishes Shape::Any-as-a-backend-shape and makes shape a
// per-collection property declared at createCollection time.
//
// This test was authored as the K.9 red: it fails today with
//   "dispatchSync: cross-domain mappings not supported
//    (src=contacts tgt=__any__)"
// and is expected to pass once dispatchSync resolves the target shape
// via shapeForCollection() and RawFilesBackend's collection declares
// the matching shape.

#include <QtTest/QtTest>
#include <QObject>
#include <QString>
#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFuture>
#include <QTemporaryDir>

#include <KCalendarCore/MemoryCalendar>

#include <memory>

#include "backendrecord.h"
#include "backendregistry.h"
#include "baselinestore.h"
#include "collectioninfo.h"
#include "isynchost.h"
#include "lossprofile.h"
#include "pluginmanager.h"
#include "rawfilesbackend.h"
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
using Kalburator::Sync::SyncResult;
using Kalburator::Sinks::RawFilesBackend;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::Shape;

namespace {

constexpr int kSyncTimeoutMs = 30000;

// Minimal typed SyncBackend that holds a single collection and serves
// seeded BackendRecords. Mirrors tst_engine_unified_routing's
// ShapedTestBackend; duplicated here because the engine-test target
// links no shared stubs library that would carry it.
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
        for (auto &r : m_records)
            if (r.id == record.id) { r = record; return true; }
        return false;
    }
    bool deleteRecord(const QString &id) override
    {
        for (int i = 0; i < m_records.size(); ++i)
            if (m_records[i].id == id) { m_records.removeAt(i); return true; }
        return false;
    }

    QList<BackendRecord> modifiedSince(const QString &, const QDateTime &) override { return {}; }
    QStringList deletedSince(const QString &, const QDateTime &) override { return {}; }
    bool supportsDeleteTracking() const override { return true; }

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
    QString m_collectionId;
    QList<CollectionInfo> m_collections;
    QList<BackendRecord> m_records;
};

class RegistrySyncHost final : public ISyncHost
{
public:
    explicit RegistrySyncHost(BackendRegistry *registry) : m_registry(registry) {}

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
        "FN:Universal Sink Test\r\n"
        "END:VCARD\r\n";
    rec.contentHash = QString::fromLatin1(
        QCryptographicHash::hash(rec.data, QCryptographicHash::Sha1).toHex());
    rec.lastModified = QDateTime::currentDateTimeUtc();
    rec.isDeleted    = false;
    return rec;
}

} // namespace

class TestEngineUniversalSinkDispatch : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void typedSourceToRawFilesTarget_succeeds();

private:
    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;
};

void TestEngineUniversalSinkDispatch::initTestCase()
{
    // Stock plugins register the contacts canonical (vcard4) and its
    // edges into the injected ShapeRegistries bundle; the engine's
    // unified path consults that same bundle (via m_shape) for any
    // non-fast-path mapping.
    Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
    Kalburator::registerStockPlugins(pm);
}

void TestEngineUniversalSinkDispatch::typedSourceToRawFilesTarget_succeeds()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    BackendRegistry registry;

    const QString sourceBackendId  = QStringLiteral("typed-source");
    const QString targetBackendId  = QStringLiteral("raw-files-target");
    const QString sourceCollection = QStringLiteral("src-contacts");
    const QString targetCollection = QStringLiteral("tgt-contacts");
    const QString mappingId        = QStringLiteral("k9-universal-sink");

    const Shape vcard4{ DomainId{"contacts"}, EncodingId{"vcard4"} };

    auto source = std::make_unique<ShapedTestBackend>(
        sourceBackendId, vcard4, sourceCollection);
    source->seedRecord(seedRecord(QStringLiteral("rec-1")));

    const QString rawRoot = tmpDir.filePath(QStringLiteral("rawfiles"));
    auto target = std::make_unique<RawFilesBackend>(rawRoot);

    CollectionInfo tgtCol;
    tgtCol.id   = targetCollection;
    tgtCol.name = targetCollection;
    tgtCol.type = QStringLiteral("contacts");
    target->createCollection(tgtCol, vcard4);

    registry.registerBackendInstance(sourceBackendId, source.get());
    registry.registerBackendInstance(targetBackendId, target.get());

    RegistrySyncHost host(&registry);
    SyncEngine engine(&registry, &host, m_shape);

    BaselineStore baselines(
        tmpDir.filePath(QStringLiteral("blob-baselines.db")));
    engine.setBaselineStore(&baselines);

    SyncMapping mapping;
    mapping.id              = mappingId;
    mapping.sourceBackend   = sourceBackendId;
    mapping.sourceCalendar  = sourceCollection;
    mapping.targetBackend   = targetBackendId;
    mapping.targetCalendar  = targetCollection;
    mapping.mode            = SyncMode::TwoWay;
    mapping.conflictPolicy  = ConflictResolution::SourceWins;
    mapping.enabled         = true;
    engine.setSyncMappings({ mapping });

    SyncRequest req;
    req.mappingIds = { mappingId };
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    auto future = engine.runSync(req);

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);

    const SyncResult result = future.resultAt(0).first();
    QVERIFY2(result.success,
             qUtf8Printable(QStringLiteral("sync failed: ") + result.errorMessage));

    // The target backend received the record verbatim.
    // RawFilesBackend::loadRecords returns BackendRecords whose `id`
    // is the file's absolute path (per readFile() in rawfilesbackend.cpp);
    // assert on the bytes instead.
    const auto stored = target->loadRecords(targetCollection);
    QCOMPARE(stored.size(), 1);
    QVERIFY(stored.first().data.contains(QByteArrayLiteral("VERSION:4.0")));
    QVERIFY(stored.first().data.contains(QByteArrayLiteral("UID:rec-1")));
}

QTEST_MAIN(TestEngineUniversalSinkDispatch)
#include "tst_engine_universal_sink_dispatch.moc"
