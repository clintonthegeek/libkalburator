#include <QtTest/QtTest>
#include "neutralprovider.h"
#include "iblobbackend.h"
#include "backendrecord.h"
#include "collectioninfo.h"

using namespace Kalburator;
using namespace Kalburator::Sync;

namespace {

class StubBackend : public IBlobBackend {
public:
    // --- Identity ---
    QString backendId() const override { return QStringLiteral("stub"); }
    QString displayName() const override { return QStringLiteral("Stub"); }
    bool isAvailable() const override { return true; }

    // --- Collections ---
    QList<CollectionInfo> availableCollections() override { return {}; }
    CollectionInfo collectionInfo(const QString &) override { return {}; }
    QString createCollection(const CollectionInfo &) override { return {}; }

    // --- Records ---
    QList<BackendRecord> loadRecords(const QString &) override { return {}; }
    std::optional<BackendRecord> loadRecord(const QString &) override { return std::nullopt; }
    QString createRecord(const QString &, const BackendRecord &) override { return {}; }
    bool updateRecord(const BackendRecord &) override { return false; }
    bool deleteRecord(const QString &) override { return false; }

    // --- Change detection ---
    QList<BackendRecord> modifiedSince(const QString &, const QDateTime &) override { return {}; }
    QStringList deletedSince(const QString &, const QDateTime &) override { return {}; }
};

} // anonymous namespace

class TestNeutralProvider : public QObject {
    Q_OBJECT
private slots:
    void producesBackend() {
        CollectionInfo info;
        info.id = QStringLiteral("solo");
        info.name = QStringLiteral("Solo Collection");
        auto factory = []{ return std::make_unique<StubBackend>(); };
        NeutralProvider provider(QStringLiteral("kind-x"), info, factory);
        auto future = provider.connect();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 1000);
        QCOMPARE(provider.kind(), QStringLiteral("kind-x"));
        const auto cols = provider.collections();
        QCOMPARE(cols.size(), 1);
        QCOMPARE(cols.first().id, QStringLiteral("solo"));
        auto backend = provider.createBackend(QStringLiteral("solo"));
        QVERIFY(backend);
    }

    void createBackendBeforeConnectReturnsNullptr() {
        NeutralProvider p(QStringLiteral("k"), CollectionInfo{},
                          [] { return std::make_unique<StubBackend>(); });
        // Not connected yet
        QCOMPARE(p.createBackend(QStringLiteral("any")), nullptr);
    }

    void unknownCollectionReturnsNullptr() {
        NeutralProvider p(QStringLiteral("k"), CollectionInfo{},
                          [] { return std::make_unique<StubBackend>(); });
        auto future = p.connect();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 1000);
        QCOMPARE(p.createBackend(QStringLiteral("nope")), nullptr);
    }

    // ── PHASE2-TASK2.3 — v2 contract tests ────────────────────────────
    // createBackends() mirrors createBackend(): up to one spec, only
    // when the requested collectionId matches the wrapped
    // CollectionInfo. BackendKind inference rules:
    //   * info.type == "calendar" → Calendar (no contentTypes set)
    //   * info.type == "contacts" → Contacts + contentTypes={"VCARD"}
    //   * unset / unknown → Calendar (default per Phase 2 spec)
    // backendId shape is the DAV providers' three-segment triple so
    // Phase 2.4+ BackendRegistry can register uniformly.

    void createBackends_returnsOneCalendarSpec_forDefaultKind() {
        CollectionInfo info;
        info.id = QStringLiteral("local-cal");
        info.name = QStringLiteral("Local Calendar");
        info.type = QStringLiteral("calendar");
        NeutralProvider p(QStringLiteral("local"), info,
                          [] { return std::make_unique<StubBackend>(); });
        // connect() for NeutralProvider finishes synchronously.
        auto future = p.connect();
        QVERIFY(future.isFinished());

        const auto specs = p.createBackends(QStringLiteral("local-cal"));
        QCOMPARE(specs.size(), 1);
        QCOMPARE(specs.first().collectionId, QStringLiteral("local-cal"));
        QVERIFY(specs.first().kind == BackendKind::Calendar);
        QCOMPARE(specs.first().displayName, QStringLiteral("Local Calendar"));

        // backendId shape: "<providerId>:<collectionId>:<collectionId>".
        // NeutralProvider has no href so the slug is the collectionId.
        const QStringList segs = specs.first().backendId.split(QLatin1Char(':'));
        QCOMPARE(segs.size(), 3);
        QCOMPARE(segs.at(1), QStringLiteral("local-cal"));
        QCOMPARE(segs.at(2), QStringLiteral("local-cal"));
    }

    void createBackends_returnsContactsSpec_whenCollectionTypeIsContacts() {
        CollectionInfo info;
        info.id = QStringLiteral("local-ab");
        info.name = QStringLiteral("Local Addressbook");
        info.type = QStringLiteral("contacts");
        NeutralProvider p(QStringLiteral("local"), info,
                          [] { return std::make_unique<StubBackend>(); });
        auto future = p.connect();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 1000);

        const auto specs = p.createBackends(QStringLiteral("local-ab"));
        QCOMPARE(specs.size(), 1);
        QVERIFY(specs.first().kind == BackendKind::Contacts);
        QCOMPARE(specs.first().contentTypes,
                 (QStringList{ QStringLiteral("VCARD") }));
    }

    void createBackends_defaultsToCalendar_whenTypeIsUnset() {
        // Phase 2 task: "always Calendar for its use case" when no
        // kind hint is available. Real fixtures rarely set info.type;
        // inferred Calendar keeps registration aligned.
        CollectionInfo info;  // empty info.type
        info.id = QStringLiteral("default");
        info.name = QStringLiteral("Default");
        NeutralProvider p(QStringLiteral("local"), info,
                          [] { return std::make_unique<StubBackend>(); });
        auto future = p.connect();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 1000);

        const auto specs = p.createBackends(QStringLiteral("default"));
        QCOMPARE(specs.size(), 1);
        QVERIFY(specs.first().kind == BackendKind::Calendar);
    }

    void createBackends_returnsEmpty_forUnconnected() {
        CollectionInfo info;
        info.id = QStringLiteral("solo");
        NeutralProvider p(QStringLiteral("local"), info,
                          [] { return std::make_unique<StubBackend>(); });
        // No connect() call yet — provider is not connected.
        QCOMPARE(p.createBackends(QStringLiteral("solo")).size(), 0);
    }

    void createBackends_returnsEmpty_forUnknownCollection() {
        CollectionInfo info;
        info.id = QStringLiteral("solo");
        NeutralProvider p(QStringLiteral("local"), info,
                          [] { return std::make_unique<StubBackend>(); });
        auto future = p.connect();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 1000);
        // collectionId doesn't match m_info.id — {}.
        QCOMPARE(p.createBackends(QStringLiteral("not-solo")).size(), 0);
    }

    void createBackends_returnsEmpty_forEmptyCollectionId() {
        CollectionInfo info;
        info.id = QStringLiteral("solo");
        NeutralProvider p(QStringLiteral("local"), info,
                          [] { return std::make_unique<StubBackend>(); });
        auto future = p.connect();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 1000);
        QCOMPARE(p.createBackends(QString()).size(), 0);
    }
};

QTEST_MAIN(TestNeutralProvider)
#include "tst_neutral_provider.moc"
