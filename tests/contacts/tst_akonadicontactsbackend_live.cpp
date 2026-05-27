#include <QtTest>
#include "akonadicontactsbackend.h"
#include <Akonadi/ServerManager>

using namespace Kalburator::Sync;

class TestAkonadiContactsLive : public QObject {
    Q_OBJECT
private slots:
    void init();
    void createUpdateDeleteRoundTrip();
    void changeDetectionSkipsUnchanged();
private:
    QString m_collectionId;
};

void TestAkonadiContactsLive::init() {
    if (qEnvironmentVariableIsEmpty("KALBURATOR_AKONADI_LIVE_TEST"))
        QSKIP("set KALBURATOR_AKONADI_LIVE_TEST=1 and run a local Akonadi");
    if (!Akonadi::ServerManager::isRunning())
        QSKIP("Akonadi server not running");
    m_collectionId = qEnvironmentVariable("KALBURATOR_AKONADI_CONTACTS_ID");
    if (m_collectionId.isEmpty())
        QSKIP("set KALBURATOR_AKONADI_CONTACTS_ID to a writable addressbook id");
}

void TestAkonadiContactsLive::createUpdateDeleteRoundTrip() {
    AkonadiContactsBackend backend;
    backend.fetchItems(m_collectionId);
    QTest::qWait(500);
    const QByteArray vcard =
        "BEGIN:VCARD\r\nVERSION:4.0\r\nUID:kalb-contact-1\r\n"
        "FN:Created Person\r\nEND:VCARD\r\n";
    BackendRecord rec; rec.id = "kalb-contact-1"; rec.type = "contact"; rec.data = vcard;
    QCOMPARE(backend.createRecord(m_collectionId, rec), QStringLiteral("kalb-contact-1"));
    rec.data.replace("Created Person", "Updated Person");
    QVERIFY(backend.updateRecord(rec));
    QVERIFY(backend.deleteRecord("kalb-contact-1"));
}

void TestAkonadiContactsLive::changeDetectionSkipsUnchanged() {
    AkonadiContactsBackend backend;
    backend.fetchItems(m_collectionId);
    QTest::qWait(500);
    const QString r1 = backend.collectionRevision(m_collectionId);
    QVERIFY(!r1.isEmpty());
    const QString r2 = backend.collectionRevision(m_collectionId);
    QCOMPARE(r1, r2);
}

QTEST_GUILESS_MAIN(TestAkonadiContactsLive)
#include "tst_akonadicontactsbackend_live.moc"
