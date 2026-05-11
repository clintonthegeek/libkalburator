#include <QObject>
#include <QtTest/QtTest>
#include <QStringList>

#include "calendardomaindefinition.h"

using Kalburator::Calendar::CalendarDomainDefinition;

class TstCalendarDomainBaselineProperties : public QObject {
    Q_OBJECT
private slots:
    void declaresColorAndDescription();
};

void TstCalendarDomainBaselineProperties::declaresColorAndDescription() {
    CalendarDomainDefinition def;
    const QStringList keys = def.baselineProperties();
    QVERIFY(keys.contains(QStringLiteral("color")));
    QVERIFY(keys.contains(QStringLiteral("description")));
}

QTEST_APPLESS_MAIN(TstCalendarDomainBaselineProperties)
#include "tst_calendardomain_baseline_properties.moc"
