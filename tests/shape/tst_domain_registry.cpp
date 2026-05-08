#include <QTest>

#include "domainplugin.h"
#include "domainregistry.h"
#include "irecorddiffer.h"
#include "irecordmerger.h"
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
    std::unique_ptr<IRecordDiffer> createCanonicalDiffer() const override { return nullptr; }
    std::unique_ptr<IRecordMerger> createCanonicalMerger() const override { return nullptr; }
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
};

QTEST_GUILESS_MAIN(TestDomainRegistry)
#include "tst_domain_registry.moc"
