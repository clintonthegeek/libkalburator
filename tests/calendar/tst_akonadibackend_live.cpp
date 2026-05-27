#include <QtTest>
#include "akonadibackend.h"
#include <Akonadi/ServerManager>

using namespace Kalburator::Sync;

class TestAkonadiBackendLive : public QObject {
    Q_OBJECT
private slots:
    void init();
    void createUpdateDeleteRoundTrip();
private:
    QString m_collectionId;
};

void TestAkonadiBackendLive::init() {
    if (qEnvironmentVariableIsEmpty("KALBURATOR_AKONADI_LIVE_TEST"))
        QSKIP("set KALBURATOR_AKONADI_LIVE_TEST=1 and run a local Akonadi");
    if (!Akonadi::ServerManager::isRunning())
        QSKIP("Akonadi server not running");
    m_collectionId = qEnvironmentVariable("KALBURATOR_AKONADI_CALENDAR_ID");
    if (m_collectionId.isEmpty())
        QSKIP("set KALBURATOR_AKONADI_CALENDAR_ID to a writable calendar id");
}

void TestAkonadiBackendLive::createUpdateDeleteRoundTrip() {
    AkonadiBackend backend;
    backend.loadCalendars(m_collectionId);
    QTest::qWait(500);  // let the monitor/cache populate

    const QByteArray ical =
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\n"
        "UID:kalb-live-1\r\nSUMMARY:Created\r\n"
        "DTSTART:20260601T120000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    BackendRecord rec;
    rec.id = "kalb-live-1";
    rec.type = "calendar";
    rec.data = ical;

    const QString newId = backend.createRecord(m_collectionId, rec);
    QCOMPARE(newId, QStringLiteral("kalb-live-1"));

    rec.data.replace("Created", "Updated");
    QVERIFY(backend.updateRecord(rec));

    QVERIFY(backend.deleteRecord("kalb-live-1"));
}

QTEST_GUILESS_MAIN(TestAkonadiBackendLive)
#include "tst_akonadibackend_live.moc"
