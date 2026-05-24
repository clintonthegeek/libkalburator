#include <QtTest/QtTest>
#include <memory>

#include "domainoperations.h"
#include "domainoperationsregistry.h"
#include "recordwriter.h"
#include "syncbackendbase.h"
#include "shaperegistries.h"

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
    void init() { m_shape = {}; }

    void registerAndLookup() {
        auto &reg = m_shape.operations;
        QVERIFY(reg.registerOperations(std::make_shared<StubOps>(QStringLiteral("calendar"))));
        QVERIFY(reg.operationsFor(Shape::DomainId{QStringLiteral("calendar")}) != nullptr);
    }

    void doubleBindingRejected() {
        auto &reg = m_shape.operations;
        QVERIFY(reg.registerOperations(std::make_shared<StubOps>(QStringLiteral("calendar"))));
        QVERIFY(!reg.registerOperations(std::make_shared<StubOps>(QStringLiteral("calendar"))));
    }

    void unknownDomainReturnsNullptr() {
        QCOMPARE(m_shape.operations.operationsFor(Shape::DomainId{QStringLiteral("nope")}),
                 nullptr);
    }

private:
    Kalburator::Shape::ShapeRegistries m_shape;
};

QTEST_MAIN(TestDomainOperationsRegistry)
#include "tst_domain_operations_registry.moc"
