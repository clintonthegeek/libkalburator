#include <QTest>
#include "transcodeguard.h"

using Kalburator::Sync::transcodeEmptiedRecord;

class TestTranscodeGuard : public QObject {
    Q_OBJECT
private slots:
    void nonEmptyToEmptyIsFailure()
    {
        QVERIFY(transcodeEmptiedRecord(QByteArray("BEGIN:VCALENDAR"), QByteArray()));
    }
    void emptyInputIsNotFailure()
    {
        QVERIFY(!transcodeEmptiedRecord(QByteArray(), QByteArray()));
    }
    void nonEmptyToNonEmptyIsNotFailure()
    {
        QVERIFY(!transcodeEmptiedRecord(QByteArray("a"), QByteArray("b")));
    }
};

QTEST_GUILESS_MAIN(TestTranscodeGuard)
#include "tst_transcode_guard.moc"
