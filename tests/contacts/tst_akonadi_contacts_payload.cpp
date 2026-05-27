#include <QtTest>
#include "akonadicontactsbackend.h"

using namespace Kalburator::Sync;

class TestAkonadiContactsPayload : public QObject {
    Q_OBJECT
private slots:
    void roundTripsUid();
};

void TestAkonadiContactsPayload::roundTripsUid() {
    const QByteArray vcard =
        "BEGIN:VCARD\r\nVERSION:4.0\r\nUID:contact-uid-1\r\n"
        "FN:Jane Doe\r\nEND:VCARD\r\n";
    BackendRecord rec;
    rec.id = "contact-uid-1";
    rec.data = vcard;

    AkonadiContactsBackend backend;
    const auto addressee = backend.addresseeFromRecordForTest(rec);
    QCOMPARE(addressee.uid(), QStringLiteral("contact-uid-1"));
    QCOMPARE(addressee.formattedName(), QStringLiteral("Jane Doe"));
}

QTEST_GUILESS_MAIN(TestAkonadiContactsPayload)
#include "tst_akonadi_contacts_payload.moc"
