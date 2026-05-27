#include <QtTest>
#include "logicalcalendarbuilder.h"
#include "discoveredcalendar.h"
#include "logicalcalendar.h"

using namespace Kalburator::Sync;

static DiscoveredCalendar disc(const QString &backend, const QString &calId,
                               const QString &name, bool writable)
{
    DiscoveredCalendar d;
    d.backendId = backend;
    d.calendarId = calId;
    d.name = name;
    d.writable = writable;
    d.supportsVEvent = true;
    d.supportsVTodo = false;
    return d;
}

class TstReadOnlySeed : public QObject
{
    Q_OBJECT
private slots:
    void nonWritableSyncBindingSeededReadOnly()
    {
        LogicalCalendarBuilder builder;
        builder.setPrimaryBackendId(QStringLiteral("local"));
        builder.setSyncBackendOrder({ QStringLiteral("caldav") });
        builder.addDiscoveredCalendars("local",  { disc("local",  "work", "Work", /*writable*/true) });
        builder.addDiscoveredCalendars("caldav", { disc("caldav", "work", "Work", /*writable*/false) });

        const QList<LogicalCalendar> matched = builder.autoMatch();
        QCOMPARE(matched.size(), 1);

        // Find the caldav binding; it must be ReadOnly (not Sync1) because it's not writable.
        bool found = false;
        for (const auto &b : matched[0].bindings) {
            if (b.backendId == QStringLiteral("caldav")) {
                QCOMPARE(b.role, BackendRole::ReadOnly);
                found = true;
            }
        }
        QVERIFY(found);
    }

    void writableSyncBindingStaysSync1()
    {
        LogicalCalendarBuilder builder;
        builder.setPrimaryBackendId(QStringLiteral("local"));
        builder.setSyncBackendOrder({ QStringLiteral("caldav") });
        builder.addDiscoveredCalendars("local",  { disc("local",  "work", "Work", true) });
        builder.addDiscoveredCalendars("caldav", { disc("caldav", "work", "Work", true) });

        const QList<LogicalCalendar> matched = builder.autoMatch();
        QCOMPARE(matched.size(), 1);
        for (const auto &b : matched[0].bindings)
            if (b.backendId == QStringLiteral("caldav"))
                QCOMPARE(b.role, BackendRole::Sync1);
    }
};

QTEST_MAIN(TstReadOnlySeed)
#include "tst_logicalcalendarbuilder_readonly_seed.moc"
