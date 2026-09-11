#include <QtTest>

#include <kalburator/sync/secretstore.h>

using namespace Kalburator::Sync;

class TestSecretStore : public QObject
{
    Q_OBJECT
private slots:
    void storesByOpaqueReference()
    {
        InMemorySecretStore store;
        const QString ref = store.put(QStringLiteral("super-secret"));
        QVERIFY(ref.startsWith(QStringLiteral("secret:")));
        QVERIFY(!ref.contains(QStringLiteral("super-secret")));
        QCOMPARE(store.get(ref), QStringLiteral("super-secret"));
    }

    void removesReference()
    {
        InMemorySecretStore store;
        const QString ref = store.put(QStringLiteral("secret"));
        QVERIFY(store.remove(ref));
        QVERIFY(store.get(ref).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSecretStore)
#include "tst_secretstore.moc"
