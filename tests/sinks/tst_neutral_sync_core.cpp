/// P3.T7 — Proof-of-neutrality test.
///
/// Intent: prove at compile time AND at run time that the sync core
/// (BackendRegistry) is calendar-neutral — it stores and returns
/// SyncBackendBase*, not the calendar-typed SyncBackend*.  A
/// non-calendar backend (RawFilesBackend) is used throughout so that
/// a calendar dep could never silently satisfy the assertion.

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <type_traits>

#include "backendregistry.h"
#include "syncbackendbase.h"
#include "rawfilesbackend.h"
#include "collectioninfo.h"
#include "backendrecord.h"
#include "shape.h"

using Kalburator::Sync::BackendRegistry;
using Kalburator::Sync::SyncBackendBase;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sinks::RawFilesBackend;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::Shape;

// ---------------------------------------------------------------------------
// Compile-time proof: the registry's instance API traffics in the neutral
// base, not any calendar-typed subclass.
// ---------------------------------------------------------------------------
static_assert(
    std::is_same_v<
        decltype(std::declval<const BackendRegistry &>().backendInstance(QString{})),
        SyncBackendBase *>,
    "BackendRegistry::backendInstance() must return SyncBackendBase* "
    "(the neutral base), not a calendar-typed SyncBackend*");

// ---------------------------------------------------------------------------
// Helpers (mirrors tst_rawfiles_backend.cpp conventions)
// ---------------------------------------------------------------------------
namespace {

const Shape kTestShape{DomainId{"test"}, EncodingId{"raw"}};

CollectionInfo makeCollection(const QString &id, const QString &name)
{
    CollectionInfo ci;
    ci.id   = id;
    ci.name = name;
    ci.type = QStringLiteral("memos");
    return ci;
}

BackendRecord makeRecord(const QString &id, const QByteArray &data)
{
    BackendRecord r;
    r.id          = id;
    r.displayName = id;
    r.data        = data;
    r.contentHash = QStringLiteral("hash-%1").arg(id);
    r.lastModified = QDateTime::currentDateTimeUtc();
    r.type        = QStringLiteral("raw");
    return r;
}

} // namespace

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------
class TestNeutralSyncCore : public QObject
{
    Q_OBJECT
private slots:
    void registry_storesAndReturnsNeutralBase_forNonCalendarBackend();
    void neutralBackend_roundTripsRecordThroughRegistry();
};

// ---------------------------------------------------------------------------
// Test 1 — registration: a non-calendar backend goes in and comes back out
// as the neutral base type, with correct identity.
// ---------------------------------------------------------------------------
void TestNeutralSyncCore::registry_storesAndReturnsNeutralBase_forNonCalendarBackend()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    BackendRegistry registry;

    // RawFilesBackend IS-A SyncBackendBase but IS-NOT-A calendar SyncBackend.
    // The fact that registerBackendInstance accepts it (compiles) already
    // proves the registry accepts the neutral base.
    auto backend = std::make_unique<RawFilesBackend>(dir.path());
    registry.registerBackendInstance(QStringLiteral("raw-1"), backend.get());

    SyncBackendBase *got = registry.backendInstance(QStringLiteral("raw-1"));
    QCOMPARE(got, backend.get());

    // Identity checks via the neutral SyncBackendBase API only — no calendar cast.
    QCOMPARE(got->backendType(), QStringLiteral("raw-files"));
    QVERIFY(got->discoveredWritable(QStringLiteral("any-collection")));
}

// ---------------------------------------------------------------------------
// Test 2 — round-trip: create a collection + record through the neutral
// SyncBackendBase* pointer obtained from the registry; verify the record
// comes back through loadRecords, again via the neutral interface.
// ---------------------------------------------------------------------------
void TestNeutralSyncCore::neutralBackend_roundTripsRecordThroughRegistry()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    BackendRegistry registry;
    auto backend = std::make_unique<RawFilesBackend>(dir.path());
    registry.registerBackendInstance(QStringLiteral("raw-1"), backend.get());

    // Obtain as neutral base — all subsequent calls go through SyncBackendBase*.
    SyncBackendBase *b = registry.backendInstance(QStringLiteral("raw-1"));
    QVERIFY(b != nullptr);

    // K.9 requires createCollection(info, shape) on a universal sink; that
    // overload is on RawFilesBackend.  We reach it via the concrete pointer
    // held by the owning unique_ptr (the registry doesn't own the object;
    // callers that constructed it can still use the concrete type for setup).
    const QString colId = QStringLiteral("memo+plaintext");
    backend->createCollection(makeCollection(colId, QStringLiteral("Memos")), kTestShape);

    // From here on: ONLY the neutral SyncBackendBase* interface.
    const QString id = b->createRecord(colId, makeRecord(QStringLiteral("r1"),
                                                         QByteArrayLiteral("neutral proof")));
    QVERIFY(!id.isEmpty());

    const QList<BackendRecord> records = b->loadRecords(colId);
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.first().data, QByteArrayLiteral("neutral proof"));
}

QTEST_MAIN(TestNeutralSyncCore)
#include "tst_neutral_sync_core.moc"
