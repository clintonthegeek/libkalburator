#include <QtTest>
#include "logicalcalendar.h"

using namespace Kalburator::Sync;
namespace Shape = Kalburator::Shape;

class TstLogicalCalendarDomain : public QObject
{
    Q_OBJECT
private slots:
    void defaultDomainIsCalendar()
    {
        LogicalCalendar lc;
        QCOMPARE(lc.domain.toString(), QStringLiteral("calendar"));
    }

    void collectionIdAliasesId()
    {
        LogicalCalendar lc;
        lc.id = QStringLiteral("work");
        QCOMPARE(lc.collectionId(), QStringLiteral("work"));
    }

    void logicalCollectionAliasCompiles()
    {
        LogicalCollection lc;            // alias of LogicalCalendar
        lc.id = QStringLiteral("x");
        QCOMPARE(lc.collectionId(), QStringLiteral("x"));
    }

    void calendarDomainOmitsKey()
    {
        // A default (calendar) logical calendar must NOT write a "domain" key,
        // so existing .kalb files round-trip byte-for-byte.
        LogicalCalendar lc;
        lc.id = QStringLiteral("c");
        lc.displayName = QStringLiteral("C");
        const QJsonObject obj = logicalCalendarToJson(lc);
        QVERIFY(!obj.contains(QStringLiteral("domain")));
    }

    void nonCalendarDomainRoundTrips()
    {
        LogicalCalendar lc;
        lc.id = QStringLiteral("c");
        lc.displayName = QStringLiteral("C");
        lc.domain = Shape::DomainId(QStringLiteral("contacts"));
        const QJsonObject obj = logicalCalendarToJson(lc);
        QCOMPARE(obj.value(QStringLiteral("domain")).toString(), QStringLiteral("contacts"));

        const LogicalCalendar back = logicalCalendarFromJson(obj);
        QCOMPARE(back.domain.toString(), QStringLiteral("contacts"));
    }

    void absentDomainKeyDefaultsToCalendar()
    {
        QJsonObject obj;
        obj[QStringLiteral("id")] = QStringLiteral("c");
        obj[QStringLiteral("displayName")] = QStringLiteral("C");
        const LogicalCalendar back = logicalCalendarFromJson(obj);
        QCOMPARE(back.domain.toString(), QStringLiteral("calendar"));
    }
};

QTEST_MAIN(TstLogicalCalendarDomain)
#include "tst_logicalcalendar_domain.moc"
