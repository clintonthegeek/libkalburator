#include <QObject>
#include <QtTest/QtTest>
#include <QStringList>

#include "calendardomainplugin.h"

using Kalburator::Calendar::CalendarDomainPlugin;

class TstCalendarDomainBaselineProperties : public QObject {
    Q_OBJECT
private slots:
    void declaresColorAndDescription();
};

void TstCalendarDomainBaselineProperties::declaresColorAndDescription() {
    CalendarDomainPlugin plugin;
    const QStringList keys = plugin.baselineProperties();
    QVERIFY(keys.contains(QStringLiteral("color")));
    QVERIFY(keys.contains(QStringLiteral("description")));
}

QTEST_APPLESS_MAIN(TstCalendarDomainBaselineProperties)
#include "tst_calendardomain_baseline_properties.moc"
