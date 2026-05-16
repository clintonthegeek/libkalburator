#include <QTest>
#include <QSignalSpy>
#include "akonadiprovider.h"
#include "akonadibackendcontribution.h"

using namespace Kalburator::Sync;

class TstAkonadiProvider : public QObject {
    Q_OBJECT
private slots:
    void identity_isStable() {
        AkonadiProvider p;
        QVERIFY(!p.id().isEmpty());
        QCOMPARE(p.kind(), QStringLiteral("akonadi"));
        QVERIFY(!p.displayName().isEmpty());
        QCOMPARE(p.isConnected(), false);
        QVERIFY(p.collections().isEmpty());
    }

    void createBackend_beforeConnect_returnsNull() {
        AkonadiProvider p;
        auto backend = p.createBackend(QStringLiteral("akonadi-1"));
        QCOMPARE(backend.get(), nullptr);
    }

    void contribution_exposes_akonadiBackendType() {
        Kalburator::Sync::AkonadiBackendContribution c;
        QCOMPARE(c.backendType(), QStringLiteral("akonadi"));
        QVERIFY(!c.nativeShapes().isEmpty());
        auto p = c.createProvider(nullptr);
        QVERIFY(p);
        QCOMPARE(p->kind(), QStringLiteral("akonadi"));
    }

    void connect_live_enumeratesCollections() {
        if (qgetenv("KALBURATOR_AKONADI_LIVE_TEST").isEmpty()) {
            QSKIP("Set KALBURATOR_AKONADI_LIVE_TEST=1 to run against live Akonadi.");
        }
        AkonadiProvider p;
        auto f = p.connect();
        QVERIFY(QTest::qWaitFor([&]{ return f.isFinished(); }, 10000));
        QVERIFY(f.result());
        QVERIFY(p.isConnected());
        Q_UNUSED(p.collections()); // must not crash
    }

    void connect_offline_resolvesWithoutHanging() {
        // Without a live Akonadi, connect() must finish (resolve false or true)
        // within 5 seconds and not hang. Only validates timing.
        AkonadiProvider p;
        QSignalSpy errSpy(&p, &IProvider::error);
        auto f = p.connect();
        // Note: if Akonadi IS running, this resolves true — that's fine.
        QVERIFY(QTest::qWaitFor([&]{ return f.isFinished(); }, 5000));
    }
};

QTEST_MAIN(TstAkonadiProvider)
#include "tst_akonadiprovider.moc"
