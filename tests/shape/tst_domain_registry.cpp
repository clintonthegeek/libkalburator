#include <QTest>

#include <KCalendarCore/MemoryCalendar>

#include "contactsdomainplugin.h"
#include "domainplugin.h"
#include "domainregistry.h"
#include "recorddiffer.h"
#include "recordmerger.h"
#include "recordwriter.h"
#include "syncbackend.h"
#include "transformationregistry.h"

using namespace Kalburator::Shape;

namespace {

class StubPlugin : public DomainPlugin {
public:
    StubPlugin(DomainId d, Shape canonical, int* counter = nullptr)
        : m_domain(std::move(d)), m_canonical(canonical), m_counter(counter) {}

    DomainId domain() const override { return m_domain; }
    Shape canonicalShape() const override { return m_canonical; }
    QList<Shape> peerShapes() const override { return {}; }
    PropertyCatalogue canonicalCatalogue() const override { return {}; }
    PropertyCatalogue catalogueFor(const Shape&) const override { return {}; }
    std::unique_ptr<RecordDiffer> createCanonicalDiffer() const override { return nullptr; }
    std::unique_ptr<RecordMerger> createCanonicalMerger() const override { return nullptr; }
    void registerEdges(TransformationRegistry& r) override {
        r.registerShape(m_canonical, PropertyCatalogue{});
        r.declareCanonical(m_domain, m_canonical);
        if (m_counter) ++(*m_counter);
    }
    int richnessRank(const Shape&) const override { return 0; }

private:
    DomainId m_domain;
    Shape m_canonical;
    int* m_counter;
};

/// Minimal SyncBackend stub — satisfies all pure virtuals with no-op bodies.
/// Used to test DomainPlugin::createWriter() without pulling in a real backend.
class StubSyncBackend : public Kalburator::Sync::SyncBackend {
    Q_OBJECT
public:
    explicit StubSyncBackend(QObject *parent = nullptr)
        : Kalburator::Sync::SyncBackend(parent) {}

    // SyncBackend pure virtuals
    QString backendType() const override { return QStringLiteral("stub"); }
    QList<Kalburator::Shape::Shape> nativeShapes() const override { return {}; }
    void loadCalendars(const QString &) override {}
    void storeCalendars(const QString &,
                        const QList<KCalendarCore::MemoryCalendar*> &) override {}
    void startSync(const QString &,
                   KCalendarCore::MemoryCalendar *,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QMap<QString, QString> &,
                   const Kalburator::Sync::TranscodingPlan &) override {}
    void removeItem(const QString &, const QString &) override {}

    // IBlobBackend overrides needed for test assertions
    QString createRecord(const QString &, const Kalburator::Sync::BackendRecord &r) override
        { return r.id; }  // Return the record id to signal success
    bool updateRecord(const Kalburator::Sync::BackendRecord &) override { return true; }
    bool deleteRecord(const QString &) override { return true; }
};

}  // namespace

class TestDomainRegistry : public QObject {
    Q_OBJECT
private slots:
    void cleanup() {
        DomainRegistry::instance().clear();
        TransformationRegistry::instance().clear();
    }

    void registerAndLookup() {
        auto& r = DomainRegistry::instance();
        const Shape calIcal{ DomainId{"calendar"}, EncodingId{"ical"} };
        r.registerDomain(std::make_shared<StubPlugin>(DomainId{"calendar"}, calIcal));
        QVERIFY(r.findByDomain(DomainId{"calendar"}) != nullptr);
        QVERIFY(r.findByDomain(DomainId{"unknown"}) == nullptr);
    }

    void multiplePluginsListed() {
        auto& r = DomainRegistry::instance();
        r.registerDomain(std::make_shared<StubPlugin>(
            DomainId{"calendar"}, Shape{ DomainId{"calendar"}, EncodingId{"ical"} }));
        r.registerDomain(std::make_shared<StubPlugin>(
            DomainId{"contacts"}, Shape{ DomainId{"contacts"}, EncodingId{"vcard4"} }));
        QCOMPARE(r.all().size(), 2);
    }

    void initializeCallsRegisterEdgesOncePerPlugin() {
        auto& r = DomainRegistry::instance();
        auto& tr = TransformationRegistry::instance();
        int callsA = 0, callsB = 0;
        r.registerDomain(std::make_shared<StubPlugin>(
            DomainId{"calendar"}, Shape{ DomainId{"calendar"}, EncodingId{"ical"} }, &callsA));
        r.registerDomain(std::make_shared<StubPlugin>(
            DomainId{"contacts"}, Shape{ DomainId{"contacts"}, EncodingId{"vcard4"} }, &callsB));
        r.initialize(tr);
        QCOMPARE(callsA, 1);
        QCOMPARE(callsB, 1);
        // Idempotent: re-initialise is a no-op.
        r.initialize(tr);
        QCOMPARE(callsA, 1);
        QCOMPARE(callsB, 1);
    }

    void duplicateRegistrationFirstWins() {
        auto& r = DomainRegistry::instance();
        const Shape calIcal{ DomainId{"calendar"}, EncodingId{"ical"} };
        auto first = std::make_shared<StubPlugin>(DomainId{"calendar"}, calIcal);
        auto second = std::make_shared<StubPlugin>(DomainId{"calendar"}, calIcal);
        r.registerDomain(first);
        r.registerDomain(second);
        QCOMPARE(r.all().size(), 1);
        QCOMPARE(r.findByDomain(DomainId{"calendar"}), first.get());
    }

    void defaultPluginCreatesBlobWriter() {
        // Verify DomainPlugin::createWriter returns a non-null writer and
        // that it delegates CRUD operations through the backend's IBlobBackend
        // surface (via DefaultBlobWriter).
        StubSyncBackend backend;
        auto plugin = std::make_shared<StubPlugin>(
            DomainId{"test"}, Shape{ DomainId{"test"}, EncodingId{"raw"} });

        auto writer = plugin->createWriter(&backend);
        QVERIFY(writer != nullptr);

        // Stub backend has no records; createRecord will be called.
        // DefaultBlobWriter::apply returns true when all operations succeed.
        Kalburator::Sync::BackendRecord r;
        r.id = QStringLiteral("rec1");
        r.data = QByteArray("hello");
        bool ok = writer->apply(QStringLiteral("col1"), {r}, {}, {});
        QVERIFY(ok);
    }

    void defaultPluginsReturnEmptyCollectionProperties() {
        // Pins that the default DomainPlugin impls return empty / no-op for
        // the collection-property hooks. Calendar plugin overrides in Task 6.
        StubSyncBackend backend;
        const QString col = QStringLiteral("col");
        const QVariantMap someProps{ {QStringLiteral("color"), QStringLiteral("red")} };

        {
            auto plugin = std::make_shared<Kalburator::Contacts::ContactsDomainPlugin>();
            QVERIFY(plugin->collectionProperties(&backend, col).isEmpty());
            plugin->applyCollectionProperties(&backend, col, someProps);
            QVERIFY(plugin->collectionProperties(&backend, col).isEmpty());
        }
    }
};

QTEST_GUILESS_MAIN(TestDomainRegistry)
#include "tst_domain_registry.moc"
