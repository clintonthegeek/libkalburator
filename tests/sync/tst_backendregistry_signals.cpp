#include "backendregistry.h"
#include "backendcontribution.h"
#include "iprovider.h"

#include <QTest>
#include <QSignalSpy>

using namespace Kalburator::Sync;

// Minimal stub BackendContribution for signal-emission tests. The
// production base class is abstract; we override only the pure
// virtuals we need for these tests.
class StubContribution : public BackendContribution {
public:
    explicit StubContribution(QString type) : m_type(std::move(type)) {}
    QString backendType() const override { return m_type; }
    QString displayName() const override { return QStringLiteral("Stub (%1)").arg(m_type); }
    QList<Kalburator::Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<IProvider> createProvider(QObject * /*parent*/ = nullptr) const override {
        return nullptr;
    }
private:
    QString m_type;
};

class TestBackendRegistrySignals : public QObject
{
    Q_OBJECT
private slots:
    void registerContribution_emitsSignal()
    {
        BackendRegistry reg;
        QSignalSpy spy(&reg, &BackendRegistry::contributionRegistered);

        const bool ok = reg.registerContribution(
            std::make_shared<StubContribution>(QStringLiteral("stub-a")));

        QVERIFY(ok);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("stub-a"));
    }

    void duplicateRegister_doesNotEmit()
    {
        BackendRegistry reg;
        reg.registerContribution(std::make_shared<StubContribution>(QStringLiteral("stub-a")));

        QSignalSpy spy(&reg, &BackendRegistry::contributionRegistered);
        const bool ok = reg.registerContribution(
            std::make_shared<StubContribution>(QStringLiteral("stub-a")));

        QVERIFY(!ok);
        QCOMPARE(spy.count(), 0);
    }

    void unregisterContribution_emitsSignal()
    {
        BackendRegistry reg;
        reg.registerContribution(std::make_shared<StubContribution>(QStringLiteral("stub-a")));

        QSignalSpy spy(&reg, &BackendRegistry::contributionUnregistered);
        reg.unregisterContribution(QStringLiteral("stub-a"));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("stub-a"));
    }

    void unregisterUnknown_doesNotEmit()
    {
        BackendRegistry reg;
        QSignalSpy spy(&reg, &BackendRegistry::contributionUnregistered);

        reg.unregisterContribution(QStringLiteral("not-registered"));

        QCOMPARE(spy.count(), 0);
    }
};

QTEST_GUILESS_MAIN(TestBackendRegistrySignals)
#include "tst_backendregistry_signals.moc"
