// Reference consumer for libkalburator's K.8 plugin surface.
// Demonstrates: in-process plugin load via registerStockPlugins(),
// backend contribution enumeration, and end-to-end calendar+contacts
// sync between SyncBackend instances using SyncEngine::runSyncFuture.
//
// Usage: reference_consumer --smoke <tmpdir>
//   --smoke runs the smoke scenario in <tmpdir> and exits 0 on success.
//
// Architecture note (K.8a T8):
//   RefBackend inherits SyncBackend (not SyncBackendBase) because
//   BackendRegistry::registerBackendInstance() accepts SyncBackend*.
//   All calendar pure-virtuals on SyncBackend have default no-op
//   implementations since K.4, so RefBackend needs no calendar stubs.

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFuture>
#include <QFutureWatcher>

#include "backendrecord.h"
#include "backendregistry.h"
#include "baselinestore.h"
#include "collectioninfo.h"
#include "domainoperationsregistry.h"
#include "domainregistry.h"
#include "isynchost.h"
#include "pluginmanager.h"
#include "shape.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "syncbackend.h"
#include "syncengine.h"
#include "syncrequest.h"
#include "synctypes.h"
#include "transformationregistry.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::Shape;
using Kalburator::Storage::BaselineStore;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::BackendRegistry;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::ISyncConfigStore;
using Kalburator::Sync::ISyncHost;
using Kalburator::Sync::SyncBackend;
using Kalburator::Sync::SyncMapping;
using Kalburator::Sync::SyncMode;
using Kalburator::Sync::SyncResult;
using Kalburator::Engine::SyncEngine;
using Kalburator::Engine::SyncRequest;

namespace {

// ──────────────────────────────────────────────────────────────────────────────
// RefBackend
//
// In-memory SyncBackend suitable for both calendar and contacts domains.
// Inherits SyncBackend (not SyncBackendBase) so it can be registered
// via BackendRegistry::registerBackendInstance(). Calendar pure-virtuals
// have default no-op implementations on SyncBackend since K.4.
// ──────────────────────────────────────────────────────────────────────────────
class RefBackend final : public SyncBackend
{
    Q_OBJECT
public:
    RefBackend(const QString &id,
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
        info.type = QStringLiteral("generic");
        m_collections.append(info);
    }

    // SyncBackend identity
    QString backendType() const override { return m_id; }
    QList<Shape> nativeShapes() const override { return { m_shape }; }

    // IBlobBackend identity
    QString backendId()   const override { return m_id; }
    QString displayName() const override { return m_id; }
    bool    isAvailable() const override { return true; }
    bool    supportsDeleteTracking() const override { return true; }

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
    QList<BackendRecord> loadRecords(const QString &) override { return m_records; }
    std::optional<BackendRecord> loadRecord(const QString &id) override
    {
        for (const auto &r : m_records)
            if (r.id == id) return r;
        return std::nullopt;
    }
    QString createRecord(const QString &, const BackendRecord &rec) override
    {
        m_records.append(rec);
        return rec.id;
    }
    bool updateRecord(const BackendRecord &rec) override
    {
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

    // Test seams
    void seedRecord(const BackendRecord &rec) { m_records.append(rec); }
    QList<BackendRecord> snapshot() const { return m_records; }

private:
    QString m_id;
    Shape m_shape;
    QString m_collectionId;
    QList<CollectionInfo> m_collections;
    QList<BackendRecord> m_records;
};

// ──────────────────────────────────────────────────────────────────────────────
// RefHost
// ──────────────────────────────────────────────────────────────────────────────
class RefHost final : public ISyncHost
{
public:
    explicit RefHost(BackendRegistry *reg) : m_registry(reg) {}

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
    void recordChanged(const QString &, const QString &, ChangeKind) override {}

private:
    BackendRegistry *m_registry = nullptr;
};

// ──────────────────────────────────────────────────────────────────────────────
// Helper: build a BackendRecord
// ──────────────────────────────────────────────────────────────────────────────
BackendRecord makeRecord(const QString &id, const QString &type, const QByteArray &data)
{
    BackendRecord rec;
    rec.id          = id;
    rec.type        = type;
    rec.displayName = id;
    rec.data        = data;
    rec.contentHash = QString::fromUtf8(
        QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
    rec.lastModified = QDateTime::currentDateTimeUtc();
    return rec;
}

SyncMapping makeTwoWayMapping(const QString &srcId, const QString &srcCol,
                               const QString &tgtId, const QString &tgtCol,
                               const QString &mappingId)
{
    SyncMapping m;
    m.id             = mappingId;
    m.sourceBackend  = srcId;
    m.sourceCalendar = srcCol;
    m.targetBackend  = tgtId;
    m.targetCalendar = tgtCol;
    m.mode           = SyncMode::TwoWay;
    m.conflictPolicy = Kalburator::Sync::ConflictResolution::SourceWins;
    m.enabled        = true;
    return m;
}

} // namespace

#include "main.moc"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("reference_consumer");

    QCommandLineParser parser;
    QCommandLineOption smokeOpt("smoke", "Run smoke scenario in <tmpdir>", "tmpdir");
    parser.addOption(smokeOpt);
    parser.process(app);

    if (!parser.isSet(smokeOpt)) {
        qWarning() << "Usage: reference_consumer --smoke <tmpdir>";
        return 2;
    }

    const QString workdir = parser.value(smokeOpt);
    // Always start from a clean state so the content-hash skip optimisation
    // doesn't cause propagation checks to fail on re-runs.
    QDir(workdir).removeRecursively();
    QDir().mkpath(workdir);

    // ── Step 1: Load plugins ─────────────────────────────────────────────────
    // One ShapeRegistries bundle, owned here at the composition root and shared
    // by reference with both the PluginManager (which populates it) and the
    // SyncEngine (which reads it). This is the sole construction site — there is
    // no process-global default.
    BackendRegistry registry;
    Kalburator::Shape::ShapeRegistries shape;
    Kalburator::PluginManager pm(&registry, shape);
    Kalburator::registerStockPlugins(pm);
    qInfo() << "Loaded plugins:" << pm.loaded().size();

    // ── Step 2: Verify provider contributions ────────────────────────────────
    auto *caldav  = registry.contributionFor("caldav");
    auto *carddav = registry.contributionFor("carddav");
    if (!caldav || !carddav) {
        qCritical() << "Missing provider contributions: caldav="
                    << (caldav != nullptr) << "carddav=" << (carddav != nullptr);
        return 3;
    }
    qInfo() << "Provider contributions verified: caldav + carddav";

    // ── Step 3: Build backends ───────────────────────────────────────────────
    const Shape calShape  { DomainId{"calendar"}, EncodingId{"ical"}   };
    const Shape conShape  { DomainId{"contacts"}, EncodingId{"vcard4"} };

    auto calSrc = std::make_unique<RefBackend>("cal-src", calShape, "cal-col");
    auto calTgt = std::make_unique<RefBackend>("cal-tgt", calShape, "cal-col");
    auto conSrc = std::make_unique<RefBackend>("con-src", conShape, "con-col");
    auto conTgt = std::make_unique<RefBackend>("con-tgt", conShape, "con-col");

    // Seed source backends
    calSrc->seedRecord(makeRecord(
        "ref-evt-1", "event",
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
        "BEGIN:VEVENT\r\nUID:ref-evt-1\r\nSUMMARY:Reference Event\r\n"
        "DTSTART:20260514T120000Z\r\nDTEND:20260514T130000Z\r\n"
        "END:VEVENT\r\nEND:VCALENDAR\r\n"));

    conSrc->seedRecord(makeRecord(
        "ref-contact-1", "contact",
        "BEGIN:VCARD\r\nVERSION:4.0\r\nUID:ref-contact-1\r\n"
        "FN:Reference Person\r\nEND:VCARD\r\n"));

    // ── Step 4: Register backends in the same local registry ─────────────────
    registry.registerBackendInstance("cal-src", calSrc.get());
    registry.registerBackendInstance("cal-tgt", calTgt.get());
    registry.registerBackendInstance("con-src", conSrc.get());
    registry.registerBackendInstance("con-tgt", conTgt.get());

    // ── Step 5: Configure engine ─────────────────────────────────────────────
    RefHost host(&registry);
    SyncEngine engine(&registry, &host, shape);

    BaselineStore baselines(QDir(workdir).filePath("baselines.db"));
    engine.setBaselineStore(&baselines);

    const SyncMapping calMapping = makeTwoWayMapping(
        "cal-src", "cal-col", "cal-tgt", "cal-col", "cal-mapping");
    const SyncMapping conMapping = makeTwoWayMapping(
        "con-src", "con-col", "con-tgt", "con-col", "con-mapping");

    engine.setSyncMappings({ calMapping, conMapping });

    // ── Step 6: Run sync (all mappings) ─────────────────────────────────────
    SyncRequest req;
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    auto future = engine.runSync(req);

    // Wait WITHOUT QFuture::waitForFinished() — Qt6's waitForFinished()
    // does not spin the event loop, so the worker thread's queued
    // signals never deliver and the future never completes. Use a
    // QFutureWatcher + QEventLoop instead (per libkalburator CLAUDE.md).
    QFutureWatcher<QList<SyncResult>> watcher;
    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcher<QList<SyncResult>>::finished,
                     &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    if (!future.isFinished())
        loop.exec();

    // ── Step 7: Check results ────────────────────────────────────────────────
    const auto results = future.resultAt(0);
    for (const auto &r : results) {
        if (!r.success) {
            qCritical() << "Sync failed for mapping:" << r.errorMessage;
            return 4;
        }
    }
    qInfo() << "All" << results.size() << "mappings succeeded";

    // ── Step 8: Verify record propagation ────────────────────────────────────
    // Contacts: con-tgt should have received the seeded vCard from con-src.
    const int conTgtCount = conTgt->snapshot().size();
    qInfo() << "Contacts target records after sync:" << conTgtCount;
    if (conTgtCount < 1) {
        qCritical() << "Contacts propagation failed: con-tgt is empty after sync";
        return 5;
    }

    // Calendar: cal-tgt should have received the seeded ICS from cal-src.
    // Note: the calendar domain path routes through CalendarDomainOperations
    // which uses CalendarPluginWriter for writes. This path works correctly
    // when the backend's IBlobBackend::createRecord() is called by the
    // blob write path (DefaultBlobWriter).
    const int calTgtCount = calTgt->snapshot().size();
    qInfo() << "Calendar target records after sync:" << calTgtCount;
    if (calTgtCount < 1) {
        qCritical() << "Calendar propagation failed: cal-tgt is empty after sync";
        return 5;
    }

    qInfo() << "Smoke test passed: both calendar and contacts records propagated";

    // No global cleanup needed: the `shape` bundle, `registry`, and `engine`
    // are stack-owned and destroyed automatically. (Pre-DI code had to clear
    // process-global Shape singletons here.)
    return 0;
}
