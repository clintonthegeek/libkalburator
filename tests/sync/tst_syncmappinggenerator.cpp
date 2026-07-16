#include <QtTest>
#include "syncmappinggenerator.h"
#include "logicalcalendar.h"
#include "logicalcalendarjson.h"
#include "synctypes.h"

using namespace Kalburator::Sync;

static bool mappingsEqual(const QList<SyncMapping> &a, const QList<SyncMapping> &b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i) {
        if (a[i].id != b[i].id
            || a[i].sourceBackend != b[i].sourceBackend
            || a[i].sourceCalendar != b[i].sourceCalendar
            || a[i].targetBackend != b[i].targetBackend
            || a[i].targetCalendar != b[i].targetCalendar
            || a[i].mode != b[i].mode) {
            return false;
        }
    }
    return true;
}

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

    // === Task 3: Per-LC WiringPolicy ===

    void testPerLcPolicyOverridesDefault()
    {
        LogicalCalendar lc1;
        lc1.id = QStringLiteral("lc1");
        lc1.displayName = QStringLiteral("LC1");
        lc1.syncEnabled = true;
        lc1.wiringPolicy = WiringPolicy::Manual;
        lc1.bindings = { bind("local", BackendRole::Primary),
                          bind("caldav", BackendRole::Sync1) };

        LogicalCalendar lc2;
        lc2.id = QStringLiteral("lc2");
        lc2.displayName = QStringLiteral("LC2");
        lc2.syncEnabled = true;
        // lc2.wiringPolicy left at default (CollectionDefault)
        lc2.bindings = { bind("local", BackendRole::Primary),
                          bind("caldav", BackendRole::Sync1) };

        const auto m = generateMappings(QList<LogicalCalendar>{ lc1, lc2 }, SyncTopology::Star);
        QCOMPARE(m.size(), 1);
        QVERIFY(m[0].id.startsWith(QStringLiteral("auto_lc2_")));
    }

    void testHubMeshChainMapToPresets()
    {
        LogicalCalendar lc = threeNode();

        LogicalCalendar hub = lc;
        hub.wiringPolicy = WiringPolicy::Hub;
        const auto hubOut = generateMappings(QList<LogicalCalendar>{ hub }, SyncTopology::Star);
        const auto starOut = generateMappings(lc, SyncTopology::Star);
        QVERIFY(mappingsEqual(hubOut, starOut));

        LogicalCalendar mesh = lc;
        mesh.wiringPolicy = WiringPolicy::Mesh;
        const auto meshOut = generateMappings(QList<LogicalCalendar>{ mesh }, SyncTopology::Star);
        const auto mirrorOut = generateMappings(lc, SyncTopology::Mirror);
        QVERIFY(mappingsEqual(meshOut, mirrorOut));

        LogicalCalendar chain = lc;
        chain.wiringPolicy = WiringPolicy::Chain;
        const auto chainOut = generateMappings(QList<LogicalCalendar>{ chain }, SyncTopology::Star);
        const auto chainPreset = generateMappings(lc, SyncTopology::Chain);
        QVERIFY(mappingsEqual(chainOut, chainPreset));
    }

    void testJsonRoundTrip()
    {
        LogicalCalendar lc;
        lc.id = QStringLiteral("c");
        lc.displayName = QStringLiteral("C");
        lc.wiringPolicy = WiringPolicy::Chain;

        const QJsonObject obj = logicalCalendarToJson(lc);
        QCOMPARE(obj.value(QStringLiteral("wiringPolicy")).toString(), QStringLiteral("chain"));

        const LogicalCalendar back = logicalCalendarFromJson(obj);
        QCOMPARE(back.wiringPolicy, WiringPolicy::Chain);

        // Absent key => CollectionDefault
        QJsonObject noPolicy;
        noPolicy[QStringLiteral("id")] = QStringLiteral("c2");
        noPolicy[QStringLiteral("displayName")] = QStringLiteral("C2");
        const LogicalCalendar defaultBack = logicalCalendarFromJson(noPolicy);
        QCOMPARE(defaultBack.wiringPolicy, WiringPolicy::CollectionDefault);
    }
};

QTEST_MAIN(TstSyncMappingGenerator)
#include "tst_syncmappinggenerator.moc"
