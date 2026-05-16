#include <QTest>
#include "akonadicontactsbackend.h"

#include <Akonadi/ServerManager>

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
        // availableCollections() is empty until collections are loaded
        QVERIFY(b.availableCollections().isEmpty());

        // isAvailable() reflects live server state; can only assert false
        // when the server is not running — QSKIP otherwise.
        if (Akonadi::ServerManager::isRunning())
            QSKIP("Akonadi server is running — cannot test unavailable state");
        QCOMPARE(b.isAvailable(), false);
    }
};

QTEST_GUILESS_MAIN(TstAkonadiContactsBackend)
#include "tst_akonadicontactsbackend.moc"
