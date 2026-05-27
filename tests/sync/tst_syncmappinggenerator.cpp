#include <QtTest>
#include "syncmappinggenerator.h"
#include "logicalcalendar.h"
#include "synctypes.h"

using namespace Kalburator::Sync;

static CalendarBackendBinding bind(const QString &backend, BackendRole role)
{
    CalendarBackendBinding b;
    b.backendId = backend;
    b.calendarId = QStringLiteral("cal");
    b.role = role;
    b.enabled = true;
    return b;
}

// Primary(local) + Sync1(caldav) + Sync2(akonadi), syncEnabled.
static LogicalCalendar threeNode()
{
    LogicalCalendar lc;
    lc.id = QStringLiteral("work");
    lc.displayName = QStringLiteral("Work");
    lc.syncEnabled = true;
    lc.bindings = { bind("local", BackendRole::Primary),
                    bind("caldav", BackendRole::Sync1),
                    bind("akonadi", BackendRole::Sync2) };
    return lc;
}

class TstSyncMappingGenerator : public QObject
{
    Q_OBJECT
private slots:
    void starIsHubAndSpoke()
    {
        const auto m = generateMappings(threeNode(), SyncTopology::Star);
        QCOMPARE(m.size(), 2);   // local<->caldav, local<->akonadi
        for (const auto &mapping : m) {
            QCOMPARE(mapping.sourceBackend, QStringLiteral("local"));
            QCOMPARE(mapping.mode, SyncMode::TwoWay);
        }
    }

    void mirrorIsFullMesh()
    {
        const auto m = generateMappings(threeNode(), SyncTopology::Mirror);
        QCOMPARE(m.size(), 3);   // local-caldav, local-akonadi, caldav-akonadi
    }

    void chainIsSequential()
    {
        const auto m = generateMappings(threeNode(), SyncTopology::Chain);
        QCOMPARE(m.size(), 2);   // local-caldav, caldav-akonadi
        QCOMPARE(m[0].sourceBackend, QStringLiteral("local"));
        QCOMPARE(m[0].targetBackend, QStringLiteral("caldav"));
        QCOMPARE(m[1].sourceBackend, QStringLiteral("caldav"));
        QCOMPARE(m[1].targetBackend, QStringLiteral("akonadi"));
    }

    void readOnlyBindingExcluded()
    {
        LogicalCalendar lc = threeNode();
        lc.bindings[2].role = BackendRole::ReadOnly;   // akonadi read-only
        const auto m = generateMappings(lc, SyncTopology::Star);
        QCOMPARE(m.size(), 1);   // only local<->caldav
        QCOMPARE(m[0].targetBackend, QStringLiteral("caldav"));
    }

    void syncDisabledYieldsNothing()
    {
        LogicalCalendar lc = threeNode();
        lc.syncEnabled = false;
        QVERIFY(generateMappings(lc, SyncTopology::Star).isEmpty());
    }

    void noSyncBindingsYieldsNothing()
    {
        LogicalCalendar lc;
        lc.id = "x"; lc.displayName = "X"; lc.syncEnabled = true;
        lc.bindings = { bind("local", BackendRole::Primary) };
        QVERIFY(generateMappings(lc, SyncTopology::Star).isEmpty());
    }

    void deterministicIds()
    {
        const auto a = generateMappings(threeNode(), SyncTopology::Star);
        const auto b = generateMappings(threeNode(), SyncTopology::Star);
        QCOMPARE(a.size(), b.size());
        for (int i = 0; i < a.size(); ++i) QCOMPARE(a[i].id, b[i].id);
        QVERIFY(!a[0].id.isEmpty());
    }

    void listOverloadConcatenatesEnabled()
    {
        LogicalCalendar other = threeNode();
        other.id = "personal";
        const auto m = generateMappings(QList<LogicalCalendar>{ threeNode(), other },
                                        SyncTopology::Star);
        QCOMPARE(m.size(), 4);   // 2 + 2
    }
};

QTEST_MAIN(TstSyncMappingGenerator)
#include "tst_syncmappinggenerator.moc"
