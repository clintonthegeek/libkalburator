#include <QTest>

#include "domaindefinition.h"
#include "domainregistry.h"
#include "recorddiffer.h"
#include "recordmerger.h"
#include "transformationregistry.h"
#include "shaperegistries.h"

using namespace Kalburator::Shape;

namespace {

class StubDefinition : public DomainDefinition {
public:
    StubDefinition(DomainId d, Shape canonical)
        : m_domain(std::move(d)), m_canonical(canonical) {}

    DomainId domain() const override { return m_domain; }
    Shape canonicalShape() const override { return m_canonical; }
    PropertyCatalogue canonicalCatalogue() const override { return {}; }
    std::unique_ptr<RecordDiffer> createCanonicalDiffer() const override { return nullptr; }
    std::unique_ptr<RecordMerger> createCanonicalMerger() const override { return nullptr; }
    int richnessRank(const Shape&) const override { return 0; }

private:
    DomainId m_domain;
    Shape m_canonical;
};

}  // namespace

class TestDomainRegistry : public QObject {
    Q_OBJECT
private slots:
    void init() { m_shape = {}; }

    void registerAndLookup() {
        auto& r = m_shape.domain;
        const Shape calIcal{ DomainId{"calendar"}, EncodingId{"ical"} };
        r.registerDefinition(std::make_shared<StubDefinition>(DomainId{"calendar"}, calIcal));
        QVERIFY(r.definitionFor(DomainId{"calendar"}) != nullptr);
        QVERIFY(r.definitionFor(DomainId{"unknown"}) == nullptr);
    }

    void multipleDefinitionsListed() {
        auto& r = m_shape.domain;
        r.registerDefinition(std::make_shared<StubDefinition>(
            DomainId{"calendar"}, Shape{ DomainId{"calendar"}, EncodingId{"ical"} }));
        r.registerDefinition(std::make_shared<StubDefinition>(
            DomainId{"contacts"}, Shape{ DomainId{"contacts"}, EncodingId{"vcard4"} }));
        QVERIFY(r.definitionFor(DomainId{"calendar"}) != nullptr);
        QVERIFY(r.definitionFor(DomainId{"contacts"}) != nullptr);
    }

    void firstRegistrationWins() {
        // registerDefinition returns true first time, false on duplicate.
        auto& r = m_shape.domain;
        const Shape calIcal{ DomainId{"calendar"}, EncodingId{"ical"} };
        auto first = std::make_shared<StubDefinition>(DomainId{"calendar"}, calIcal);
        auto second = std::make_shared<StubDefinition>(DomainId{"calendar"}, calIcal);
        QVERIFY(r.registerDefinition(first) == true);
        QVERIFY(r.registerDefinition(second) == false);
        // The definition pointer returned is still the first one.
        QCOMPARE(r.definitionFor(DomainId{"calendar"}), first.get());
    }

    void duplicateRegistrationFirstWins() {
        auto& r = m_shape.domain;
        const Shape calIcal{ DomainId{"calendar"}, EncodingId{"ical"} };
        auto first = std::make_shared<StubDefinition>(DomainId{"calendar"}, calIcal);
        auto second = std::make_shared<StubDefinition>(DomainId{"calendar"}, calIcal);
        r.registerDefinition(first);
        bool accepted = r.registerDefinition(second);
        QVERIFY(!accepted);
        QCOMPARE(r.definitionFor(DomainId{"calendar"}), first.get());
    }

private:
    Kalburator::Shape::ShapeRegistries m_shape;
};

QTEST_GUILESS_MAIN(TestDomainRegistry)
#include "tst_domain_registry.moc"
