#include <QtTest/QtTest>
#include <memory>
#include "backendcontribution.h"
#include "backendregistry.h"
#include "iprovider.h"
#include "iblobbackend.h"
#include "shape.h"

using namespace Kalburator;

namespace {
class StubProvider : public Sync::IProvider {
    Q_OBJECT
public:
    explicit StubProvider(QObject *parent = nullptr) : Sync::IProvider(parent) {}
    QString id() const override { return QStringLiteral("stub-id"); }
    QString kind() const override { return QStringLiteral("stub"); }
    QString displayName() const override { return {}; }
    void load(const Sync::BackendConfiguration&) override {}
    Sync::BackendConfiguration save() const override { return {}; }
    QWidget* createConfigWidget(QWidget*) override { return nullptr; }
    QFuture<bool> connect() override { return {}; }
    void disconnect() override {}
    bool isConnected() const override { return false; }
    QList<Sync::CollectionInfo> collections() const override { return {}; }
    std::unique_ptr<Sync::IBlobBackend> createBackend(const QString&) override { return nullptr; }
};

class StubBC : public Sync::BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("stub"); }
    QList<Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<Sync::IProvider> createProvider(QObject*) const override {
        return std::make_unique<StubProvider>();
    }
};
}

class TestBackendContribution : public QObject {
    Q_OBJECT
private slots:
    void registerAndLookup() {
        Sync::BackendRegistry reg;
        auto contrib = std::make_shared<StubBC>();
        reg.registerContribution(contrib);
        QVERIFY(reg.contributionFor(QStringLiteral("stub")) != nullptr);
        QCOMPARE(reg.contributionFor(QStringLiteral("stub"))->backendType(),
                 QStringLiteral("stub"));
    }

    void duplicateTypeReturnsFalse() {
        Sync::BackendRegistry reg;
        QVERIFY(reg.registerContribution(std::make_shared<StubBC>()));
        QVERIFY(!reg.registerContribution(std::make_shared<StubBC>()));
    }

    void unknownTypeReturnsNullptr() {
        Sync::BackendRegistry reg;
        QCOMPARE(reg.contributionFor(QStringLiteral("nope")), nullptr);
    }
};

QTEST_MAIN(TestBackendContribution)
#include "tst_backend_contribution.moc"
