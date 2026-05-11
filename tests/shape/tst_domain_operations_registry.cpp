#include <QtTest/QtTest>
#include <memory>

#include "domainoperations.h"
#include "domainoperationsregistry.h"
#include "recordwriter.h"
#include "syncbackendbase.h"

using namespace Kalburator;

namespace {
class StubOps : public Shape::DomainOperations {
public:
    explicit StubOps(const QString &id) : m_id(id) {}
    Shape::DomainId targetDomain() const override { return Shape::DomainId{m_id}; }
    std::unique_ptr<Shape::RecordWriter> createWriter(Sync::SyncBackendBase *) const override
    {
        return nullptr;
    }
private:
    QString m_id;
};
} // namespace

class TestDomainOperationsRegistry : public QObject {
    Q_OBJECT
private slots:
    void cleanup() { Shape::DomainOperationsRegistry::instance().clear(); }

    void registerAndLookup() {
        auto &reg = Shape::DomainOperationsRegistry::instance();
        QVERIFY(reg.registerOperations(std::make_shared<StubOps>(QStringLiteral("calendar"))));
        QVERIFY(reg.operationsFor(Shape::DomainId{QStringLiteral("calendar")}) != nullptr);
    }

    void doubleBindingRejected() {
        auto &reg = Shape::DomainOperationsRegistry::instance();
        QVERIFY(reg.registerOperations(std::make_shared<StubOps>(QStringLiteral("calendar"))));
        QVERIFY(!reg.registerOperations(std::make_shared<StubOps>(QStringLiteral("calendar"))));
    }

    void unknownDomainReturnsNullptr() {
        QCOMPARE(Shape::DomainOperationsRegistry::instance().operationsFor(Shape::DomainId{QStringLiteral("nope")}),
                 nullptr);
    }
};

QTEST_MAIN(TestDomainOperationsRegistry)
#include "tst_domain_operations_registry.moc"
