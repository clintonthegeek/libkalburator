#include <QtTest/QtTest>
#include <QDir>
#include <QTemporaryDir>
#include <QColor>

#include "../../src/calendar/calendarmetadatamanager.h"

using Kalburator::Sync::CalendarMetadataManager;

class TstCalendarMetadataManager : public QObject
{
    Q_OBJECT
private slots:
    void isValid_requiresExistingPath();
    void color_roundTrip();
    void color_removeFile();
    void displayName_roundTrip();
    void displayName_removeFile();
    void description_roundTrip();
    void description_removeFile();
    void order_roundTrip();
    void order_removeFile();
    void hasXxx_falseAfterRemove();
    void staticHelpers_returnCorrectPaths();
};

void TstCalendarMetadataManager::isValid_requiresExistingPath()
{
    CalendarMetadataManager invalid(QStringLiteral("/nonexistent/path/12345"));
    QVERIFY(!invalid.isValid());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    CalendarMetadataManager valid(dir.path());
    QVERIFY(valid.isValid());
}

void TstCalendarMetadataManager::color_roundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    CalendarMetadataManager mgr(dir.path());

    QVERIFY(!mgr.hasColor());
    QVERIFY(mgr.setColor(QColor(Qt::red)));
    QVERIFY(mgr.hasColor());

    const QColor got = mgr.color();
    QVERIFY(got.isValid());
    QCOMPARE(got.name(), QColor(Qt::red).name());
}

void TstCalendarMetadataManager::color_removeFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    CalendarMetadataManager mgr(dir.path());

    QVERIFY(mgr.setColor(QColor(Qt::blue)));
    QVERIFY(mgr.hasColor());
    QVERIFY(mgr.removeColor());
    QVERIFY(!mgr.hasColor());
}

void TstCalendarMetadataManager::displayName_roundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    CalendarMetadataManager mgr(dir.path());

    QVERIFY(!mgr.hasDisplayName());
    QVERIFY(mgr.setDisplayName(QStringLiteral("My Calendar")));
    QVERIFY(mgr.hasDisplayName());
    QCOMPARE(mgr.displayName(), QStringLiteral("My Calendar"));
}

void TstCalendarMetadataManager::displayName_removeFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    CalendarMetadataManager mgr(dir.path());

    QVERIFY(mgr.setDisplayName(QStringLiteral("Work")));
    QVERIFY(mgr.removeDisplayName());
    QVERIFY(!mgr.hasDisplayName());
    QVERIFY(mgr.displayName().isEmpty());
}

void TstCalendarMetadataManager::description_roundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    CalendarMetadataManager mgr(dir.path());

    QVERIFY(!mgr.hasDescription());
    QVERIFY(mgr.setDescription(QStringLiteral("Work meetings and events")));
    QVERIFY(mgr.hasDescription());
    QCOMPARE(mgr.description(), QStringLiteral("Work meetings and events"));
}

void TstCalendarMetadataManager::description_removeFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    CalendarMetadataManager mgr(dir.path());

    QVERIFY(mgr.setDescription(QStringLiteral("desc")));
    QVERIFY(mgr.removeDescription());
    QVERIFY(!mgr.hasDescription());
}

void TstCalendarMetadataManager::order_roundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    CalendarMetadataManager mgr(dir.path());

    QVERIFY(!mgr.hasOrder());
    QVERIFY(mgr.setOrder(42));
    QVERIFY(mgr.hasOrder());
    QCOMPARE(mgr.order(), 42);

    // Overwrite with different value
    QVERIFY(mgr.setOrder(7));
    QCOMPARE(mgr.order(), 7);
}

void TstCalendarMetadataManager::order_removeFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    CalendarMetadataManager mgr(dir.path());

    QVERIFY(mgr.setOrder(1));
    QVERIFY(mgr.removeOrder());
    QVERIFY(!mgr.hasOrder());
}

void TstCalendarMetadataManager::hasXxx_falseAfterRemove()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    CalendarMetadataManager mgr(dir.path());

    mgr.setColor(QColor(Qt::green));
    mgr.setDisplayName(QStringLiteral("X"));
    mgr.setDescription(QStringLiteral("Y"));
    mgr.setOrder(3);

    QVERIFY(mgr.hasColor());
    QVERIFY(mgr.hasDisplayName());
    QVERIFY(mgr.hasDescription());
    QVERIFY(mgr.hasOrder());

    mgr.removeColor();
    mgr.removeDisplayName();
    mgr.removeDescription();
    mgr.removeOrder();

    QVERIFY(!mgr.hasColor());
    QVERIFY(!mgr.hasDisplayName());
    QVERIFY(!mgr.hasDescription());
    QVERIFY(!mgr.hasOrder());
}

void TstCalendarMetadataManager::staticHelpers_returnCorrectPaths()
{
    const QString base = QStringLiteral("/some/cal");
    QVERIFY(CalendarMetadataManager::colorFilePath(base).contains(QStringLiteral("color")));
    QVERIFY(CalendarMetadataManager::displayNameFilePath(base).contains(QStringLiteral("displayname")));
    QVERIFY(CalendarMetadataManager::descriptionFilePath(base).contains(QStringLiteral("description")));
    QVERIFY(CalendarMetadataManager::orderFilePath(base).contains(QStringLiteral("order")));
}

QTEST_GUILESS_MAIN(TstCalendarMetadataManager)
#include "tst_calendarmetadatamanager.moc"
