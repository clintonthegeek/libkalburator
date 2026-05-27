#include <QtTest>
#include "akonadibackend.h"
#include <KCalendarCore/Event>

using namespace Kalburator::Sync;

class TestAkonadiPayload : public QObject {
    Q_OBJECT
private slots:
    void roundTripsUid();
};

void TestAkonadiPayload::roundTripsUid() {
    const QByteArray ical =
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\n"
        "UID:test-uid-123\r\nSUMMARY:Hello\r\n"
        "DTSTART:20260101T120000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    BackendRecord rec;
    rec.id = "test-uid-123";
    rec.data = ical;

    AkonadiBackend backend;
    auto incidence = backend.incidenceFromRecordForTest(rec);
    QVERIFY(incidence);
    QCOMPARE(incidence->uid(), QStringLiteral("test-uid-123"));
    QCOMPARE(incidence->summary(), QStringLiteral("Hello"));
}

QTEST_MAIN(TestAkonadiPayload)
#include "tst_akonadi_payload.moc"
