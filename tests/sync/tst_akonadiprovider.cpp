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
};

QTEST_MAIN(TstAkonadiProvider)
#include "tst_akonadiprovider.moc"
