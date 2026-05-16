#include <QTest>
#include "akonadicontactsbackend.h"

using namespace Kalburator::Sync;

class TstAkonadiContactsBackend : public QObject {
    Q_OBJECT
private slots:
    void identity() {
        AkonadiContactsBackend b;
        QCOMPARE(b.backendType(), QStringLiteral("akonadi-contacts"));
        QVERIFY(!b.nativeShapes().isEmpty());
    }

    void availableCollections_offline_isEmpty() {
        AkonadiContactsBackend b;
        QVERIFY(b.availableCollections().isEmpty());
        QCOMPARE(b.isAvailable(), false);
    }
};

QTEST_GUILESS_MAIN(TstAkonadiContactsBackend)
#include "tst_akonadicontactsbackend.moc"
