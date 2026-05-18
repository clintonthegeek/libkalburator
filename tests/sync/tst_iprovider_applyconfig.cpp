#include "iprovider.h"
#include "backendconfiguration.h"

#include <QTest>
#include <QFutureInterface>

using namespace Kalburator::Sync;

/// Minimal stub IProvider that records what applyConfig drove it to do.
class StubProvider : public IProvider {
    Q_OBJECT
public:
    QString id() const override { return m_id; }
    QString kind() const override { return QStringLiteral("stub"); }
    QString displayName() const override { return m_cfg.displayName; }

    void load(const BackendConfiguration &cfg) override {
        m_cfg = cfg;
        m_loadCount++;
    }
    BackendConfiguration save() const override { return m_cfg; }
    QWidget *createConfigWidget(QWidget *) override { return nullptr; }

    QFuture<bool> connect() override {
        m_connected = true;
        m_connectCount++;
        emit connectionStateChanged(true);
        QFutureInterface<bool> fi;
        fi.reportStarted();
        fi.reportResult(true);
        fi.reportFinished();
        return fi.future();
    }
    void disconnect() override {
        if (m_connected) {
            m_connected = false;
            m_disconnectCount++;
            emit connectionStateChanged(false);
        }
    }
    bool isConnected() const override { return m_connected; }
    QList<CollectionInfo> collections() const override { return {}; }
    std::unique_ptr<IBlobBackend> createBackend(const QString &) override { return nullptr; }

    int loadCount() const { return m_loadCount; }
    int connectCount() const { return m_connectCount; }
    int disconnectCount() const { return m_disconnectCount; }

private:
    QString m_id = QStringLiteral("stub-1");
    BackendConfiguration m_cfg;
    bool m_connected = false;
    int m_loadCount = 0;
    int m_connectCount = 0;
    int m_disconnectCount = 0;
};

class TestApplyConfig : public QObject
{
    Q_OBJECT
private slots:
    void applyConfig_whileDisconnected_justLoads()
    {
        StubProvider p;
        BackendConfiguration cfg;
        cfg.displayName = QStringLiteral("New Name");

        p.applyConfig(cfg);

        QCOMPARE(p.loadCount(), 1);
        QCOMPARE(p.connectCount(), 0);
        QCOMPARE(p.disconnectCount(), 0);
        QCOMPARE(p.displayName(), QStringLiteral("New Name"));
    }

    void applyConfig_whileConnected_reconnectsWithNewConfig()
    {
        StubProvider p;
        BackendConfiguration first;
        first.displayName = QStringLiteral("First");
        p.load(first);
        // Safe: StubProvider::connect() returns a synchronously-resolved
        // future; no event loop needed.
        p.connect().waitForFinished();
        QVERIFY(p.isConnected());
        QCOMPARE(p.loadCount(), 1);
        QCOMPARE(p.connectCount(), 1);

        BackendConfiguration second;
        second.displayName = QStringLiteral("Second");
        p.applyConfig(second);

        QVERIFY(p.isConnected());                 // reconnected
        QCOMPARE(p.disconnectCount(), 1);         // torn down once
        QCOMPARE(p.loadCount(), 2);               // loaded with new cfg
        QCOMPARE(p.connectCount(), 2);            // and reconnected
        QCOMPARE(p.displayName(), QStringLiteral("Second"));
    }
};

QTEST_GUILESS_MAIN(TestApplyConfig)
#include "tst_iprovider_applyconfig.moc"
