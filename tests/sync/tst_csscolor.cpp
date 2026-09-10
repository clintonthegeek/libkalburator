// Apple's CalDAV `calendar-color` is #RRGGBBAA; Qt reads a nine-character
// hex string as #AARRGGBB. Handing one straight to QColor rotated every
// channel one position, so a colour written to a server never matched the
// colour read back, and the sync engine reported an unresolvable
// collection-property conflict on every run of every coloured calendar.
// See docs/bugs/caldav-calendar-color-roundtrip-rotates-channels.md.
#include <QtTest>
#include <kalburator/types/csscolor.h>

class TestCssColor : public QObject
{
    Q_OBJECT
private slots:
    void rgbaHexKeepsChannelsInPlace()
    {
        // The exact value the RRD-007 rig stored, and the rotation it produced.
        const QColor parsed = Kalburator::colorFromCssHex(QStringLiteral("#1e88e5FF"));
        QVERIFY(parsed.isValid());
        QCOMPARE(parsed.red(), 0x1e);
        QCOMPARE(parsed.green(), 0x88);
        QCOMPARE(parsed.blue(), 0xe5);
        QCOMPARE(parsed.alpha(), 0xFF);
    }

    void alphaIsHonouredNotDiscarded()
    {
        const QColor parsed = Kalburator::colorFromCssHex(QStringLiteral("#1e88e580"));
        QVERIFY(parsed.isValid());
        QCOMPARE(parsed.red(), 0x1e);
        QCOMPARE(parsed.alpha(), 0x80);
    }

    void roundTripsWhatTheBackendWrites()
    {
        // remotecalendarbackend.cpp builds the property as name() + "FF".
        const QColor original(QStringLiteral("#1e88e5"));
        const QString onTheWire = original.name() + QStringLiteral("FF");
        QCOMPARE(Kalburator::colorFromCssHex(onTheWire), original);
    }

    void sixDigitAndNamedFormsAreUntouched()
    {
        QCOMPARE(Kalburator::colorFromCssHex(QStringLiteral("#1e88e5")),
                 QColor(QStringLiteral("#1e88e5")));
        QCOMPARE(Kalburator::colorFromCssHex(QStringLiteral("#abc")),
                 QColor(QStringLiteral("#abc")));
        QCOMPARE(Kalburator::colorFromCssHex(QStringLiteral("red")), QColor(Qt::red));
    }

    void garbageStaysInvalid()
    {
        QVERIFY(!Kalburator::colorFromCssHex(QStringLiteral("#zzzzzzzz")).isValid());
        QVERIFY(!Kalburator::colorFromCssHex(QString()).isValid());
    }
};

QTEST_MAIN(TestCssColor)
#include "tst_csscolor.moc"
