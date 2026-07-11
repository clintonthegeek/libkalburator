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
};

QTEST_MAIN(TestNeutralProvider)
#include "tst_neutral_provider.moc"
