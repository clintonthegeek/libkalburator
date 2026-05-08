#include <QtTest/QtTest>
#include <QObject>

#include <KCalendarCore/Event>
#include <KCalendarCore/Todo>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include "incidencediff.h"

namespace Kalburator::Sync {}
using namespace Kalburator::Sync;


class TestIncidenceDiff : public QObject
{
    Q_OBJECT

private:
    KCalendarCore::Event::Ptr createEvent(const QString &summary,
                                           const QDateTime &start,
                                           const QDateTime &end) {
        auto event = KCalendarCore::Event::Ptr::create();
        event->setUid(QStringLiteral("test-uid"));
        event->setSummary(summary);
        event->setDtStart(start);
        event->setDtEnd(end);
        return event;
    }

    KCalendarCore::Todo::Ptr createTodo(const QString &summary,
                                         const QDateTime &due) {
        auto todo = KCalendarCore::Todo::Ptr::create();
        todo->setUid(QStringLiteral("test-uid"));
        todo->setSummary(summary);
        if (due.isValid()) {
            todo->setDtDue(due);
        }
        return todo;
    }

private slots:
    void testPropertyDisplayName()
    {
        QCOMPARE(IncidenceDiff::propertyDisplayName("SUMMARY"), QObject::tr("Summary"));
        QCOMPARE(IncidenceDiff::propertyDisplayName("DTSTART"), QObject::tr("Start Time"));
        QCOMPARE(IncidenceDiff::propertyDisplayName("DTEND"), QObject::tr("End Time"));
        QCOMPARE(IncidenceDiff::propertyDisplayName("DUE"), QObject::tr("Due Date"));
        QCOMPARE(IncidenceDiff::propertyDisplayName("DESCRIPTION"), QObject::tr("Description"));
        QCOMPARE(IncidenceDiff::propertyDisplayName("LOCATION"), QObject::tr("Location"));
        QCOMPARE(IncidenceDiff::propertyDisplayName("STATUS"), QObject::tr("Status"));
        QCOMPARE(IncidenceDiff::propertyDisplayName("PRIORITY"), QObject::tr("Priority"));
        QCOMPARE(IncidenceDiff::propertyDisplayName("CATEGORIES"), QObject::tr("Categories"));
        QCOMPARE(IncidenceDiff::propertyDisplayName("RRULE"), QObject::tr("Recurrence Rule"));
        QCOMPARE(IncidenceDiff::propertyDisplayName("EXDATE"), QObject::tr("Exception Dates"));

        // Unknown property should return itself
        QCOMPARE(IncidenceDiff::propertyDisplayName("X-CUSTOM"), "X-CUSTOM");
    }

    void testPropertyCategory()
    {
        QCOMPARE(IncidenceDiff::propertyCategory("SUMMARY"), IncidenceDiff::Essential);
        QCOMPARE(IncidenceDiff::propertyCategory("DTSTART"), IncidenceDiff::Essential);
        QCOMPARE(IncidenceDiff::propertyCategory("DTEND"), IncidenceDiff::Essential);
        QCOMPARE(IncidenceDiff::propertyCategory("DUE"), IncidenceDiff::Essential);

        QCOMPARE(IncidenceDiff::propertyCategory("RRULE"), IncidenceDiff::DateTime);
        QCOMPARE(IncidenceDiff::propertyCategory("EXDATE"), IncidenceDiff::DateTime);

        QCOMPARE(IncidenceDiff::propertyCategory("DESCRIPTION"), IncidenceDiff::Descriptive);
        QCOMPARE(IncidenceDiff::propertyCategory("LOCATION"), IncidenceDiff::Descriptive);
        QCOMPARE(IncidenceDiff::propertyCategory("CATEGORIES"), IncidenceDiff::Descriptive);

        QCOMPARE(IncidenceDiff::propertyCategory("STATUS"), IncidenceDiff::Status);
        QCOMPARE(IncidenceDiff::propertyCategory("PRIORITY"), IncidenceDiff::Status);

        QCOMPARE(IncidenceDiff::propertyCategory("ORGANIZER"), IncidenceDiff::Organizational);
        QCOMPARE(IncidenceDiff::propertyCategory("ATTENDEE"), IncidenceDiff::Organizational);

        QCOMPARE(IncidenceDiff::propertyCategory("X-CUSTOM"), IncidenceDiff::Other);
    }

    void testPropertySortPriority()
    {
        // More important properties should have lower priority numbers
        QVERIFY(IncidenceDiff::propertySortPriority("SUMMARY") <
                IncidenceDiff::propertySortPriority("DESCRIPTION"));
        QVERIFY(IncidenceDiff::propertySortPriority("DTSTART") <
                IncidenceDiff::propertySortPriority("LOCATION"));
        QVERIFY(IncidenceDiff::propertySortPriority("LOCATION") <
                IncidenceDiff::propertySortPriority("UID"));
    }

    void testFormatPropertyValue_Status()
    {
        QCOMPARE(IncidenceDiff::formatPropertyValue("STATUS", "TENTATIVE"),
                 QObject::tr("Tentative"));
        QCOMPARE(IncidenceDiff::formatPropertyValue("STATUS", "CONFIRMED"),
                 QObject::tr("Confirmed"));
        QCOMPARE(IncidenceDiff::formatPropertyValue("STATUS", "CANCELLED"),
                 QObject::tr("Cancelled"));
        QCOMPARE(IncidenceDiff::formatPropertyValue("STATUS", "IN-PROCESS"),
                 QObject::tr("In Progress"));
        QCOMPARE(IncidenceDiff::formatPropertyValue("STATUS", "COMPLETED"),
                 QObject::tr("Completed"));
    }

    void testFormatPropertyValue_Priority()
    {
        QCOMPARE(IncidenceDiff::formatPropertyValue("PRIORITY", "0"),
                 QObject::tr("None"));
        QCOMPARE(IncidenceDiff::formatPropertyValue("PRIORITY", "1"),
                 QObject::tr("High (1)"));
        QCOMPARE(IncidenceDiff::formatPropertyValue("PRIORITY", "5"),
                 QObject::tr("Medium (5)"));
        QCOMPARE(IncidenceDiff::formatPropertyValue("PRIORITY", "9"),
                 QObject::tr("Low (9)"));
    }

    void testFormatPropertyValue_PercentComplete()
    {
        QCOMPARE(IncidenceDiff::formatPropertyValue("PERCENT-COMPLETE", "50"),
                 QObject::tr("%1%").arg(50));
        QCOMPARE(IncidenceDiff::formatPropertyValue("PERCENT-COMPLETE", "100"),
                 QObject::tr("%1%").arg(100));
    }

    void testFormatPropertyValue_Class()
    {
        QCOMPARE(IncidenceDiff::formatPropertyValue("CLASS", "PUBLIC"),
                 QObject::tr("Public"));
        QCOMPARE(IncidenceDiff::formatPropertyValue("CLASS", "PRIVATE"),
                 QObject::tr("Private"));
        QCOMPARE(IncidenceDiff::formatPropertyValue("CLASS", "CONFIDENTIAL"),
                 QObject::tr("Confidential"));
    }

    void testCompare_IdenticalEvents()
    {
        QDateTime start = QDateTime::currentDateTime();
        QDateTime end = start.addSecs(3600);

        auto eventA = createEvent("Meeting", start, end);
        auto eventB = createEvent("Meeting", start, end);

        QList<PropertyDiff> diffs = IncidenceDiff::compare(eventA, eventB);

        // No differences expected
        QVERIFY(diffs.isEmpty());
    }

    void testCompare_SummaryDifference()
    {
        QDateTime start = QDateTime::currentDateTime();
        QDateTime end = start.addSecs(3600);

        auto eventA = createEvent("Meeting A", start, end);
        auto eventB = createEvent("Meeting B", start, end);

        QList<PropertyDiff> diffs = IncidenceDiff::compare(eventA, eventB);

        QCOMPARE(diffs.size(), 1);
        QCOMPARE(diffs[0].propertyName, QStringLiteral("SUMMARY"));
        QCOMPARE(diffs[0].valueA, QStringLiteral("Meeting A"));
        QCOMPARE(diffs[0].valueB, QStringLiteral("Meeting B"));
        QCOMPARE(diffs[0].state, PropertyDiff::BothDifferent);
        QVERIFY(diffs[0].isConflict());
    }

    void testCompare_MultipleProperties()
    {
        QDateTime start = QDateTime::currentDateTime();
        QDateTime end = start.addSecs(3600);

        auto eventA = createEvent("Meeting", start, end);
        eventA->setLocation("Room A");
        eventA->setDescription("Description A");

        auto eventB = createEvent("Meeting", start, end);
        eventB->setLocation("Room B");
        eventB->setDescription("Description B");

        QList<PropertyDiff> diffs = IncidenceDiff::compare(eventA, eventB);

        QCOMPARE(diffs.size(), 2);  // LOCATION and DESCRIPTION differ

        // Find specific diffs
        bool foundLocation = false;
        bool foundDescription = false;
        for (const PropertyDiff &diff : diffs) {
            if (diff.propertyName == "LOCATION") {
                foundLocation = true;
                QCOMPARE(diff.valueA, QStringLiteral("Room A"));
                QCOMPARE(diff.valueB, QStringLiteral("Room B"));
            }
            if (diff.propertyName == "DESCRIPTION") {
                foundDescription = true;
                QCOMPARE(diff.valueA, QStringLiteral("Description A"));
                QCOMPARE(diff.valueB, QStringLiteral("Description B"));
            }
        }
        QVERIFY(foundLocation);
        QVERIFY(foundDescription);
    }

    void testCompare_OnlyInA()
    {
        QDateTime start = QDateTime::currentDateTime();
        QDateTime end = start.addSecs(3600);

        auto eventA = createEvent("Meeting", start, end);
        eventA->setLocation("Room A");

        auto eventB = createEvent("Meeting", start, end);
        // No location set

        QList<PropertyDiff> diffs = IncidenceDiff::compare(eventA, eventB);

        QCOMPARE(diffs.size(), 1);
        QCOMPARE(diffs[0].propertyName, QStringLiteral("LOCATION"));
        QCOMPARE(diffs[0].state, PropertyDiff::OnlyInA);
    }

    void testCompare_OnlyInB()
    {
        QDateTime start = QDateTime::currentDateTime();
        QDateTime end = start.addSecs(3600);

        auto eventA = createEvent("Meeting", start, end);
        // No location set

        auto eventB = createEvent("Meeting", start, end);
        eventB->setLocation("Room B");

        QList<PropertyDiff> diffs = IncidenceDiff::compare(eventA, eventB);

        QCOMPARE(diffs.size(), 1);
        QCOMPARE(diffs[0].propertyName, QStringLiteral("LOCATION"));
        QCOMPARE(diffs[0].state, PropertyDiff::OnlyInB);
    }

    void testCompare_ThreeWay_AMatchesBaseline()
    {
        QDateTime start = QDateTime::currentDateTime();
        QDateTime end = start.addSecs(3600);

        auto eventBaseline = createEvent("Original", start, end);
        auto eventA = createEvent("Original", start, end);  // Same as baseline
        auto eventB = createEvent("Modified", start, end);  // Changed

        QList<PropertyDiff> diffs = IncidenceDiff::compare(eventA, eventB, eventBaseline);

        QCOMPARE(diffs.size(), 1);
        QCOMPARE(diffs[0].propertyName, QStringLiteral("SUMMARY"));
        QCOMPARE(diffs[0].state, PropertyDiff::AMatchesBaseline);
        // B changed, so default resolution should be UseB
        QCOMPARE(diffs[0].resolution, PropertyDiff::UseB);
    }

    void testCompare_ThreeWay_BMatchesBaseline()
    {
        QDateTime start = QDateTime::currentDateTime();
        QDateTime end = start.addSecs(3600);

        auto eventBaseline = createEvent("Original", start, end);
        auto eventA = createEvent("Modified", start, end);  // Changed
        auto eventB = createEvent("Original", start, end);  // Same as baseline

        QList<PropertyDiff> diffs = IncidenceDiff::compare(eventA, eventB, eventBaseline);

        QCOMPARE(diffs.size(), 1);
        QCOMPARE(diffs[0].propertyName, QStringLiteral("SUMMARY"));
        QCOMPARE(diffs[0].state, PropertyDiff::BMatchesBaseline);
        // A changed, so default resolution should be UseA
        QCOMPARE(diffs[0].resolution, PropertyDiff::UseA);
    }

    void testCompare_ThreeWay_BothChangedDifferent()
    {
        QDateTime start = QDateTime::currentDateTime();
        QDateTime end = start.addSecs(3600);

        auto eventBaseline = createEvent("Original", start, end);
        auto eventA = createEvent("Modified A", start, end);  // Changed differently
        auto eventB = createEvent("Modified B", start, end);  // Changed differently

        QList<PropertyDiff> diffs = IncidenceDiff::compare(eventA, eventB, eventBaseline);

        QCOMPARE(diffs.size(), 1);
        QCOMPARE(diffs[0].propertyName, QStringLiteral("SUMMARY"));
        QCOMPARE(diffs[0].state, PropertyDiff::BothChangedDifferent);
        QVERIFY(diffs[0].isConflict());
        QVERIFY(diffs[0].needsResolution());
    }

    void testCompare_ThreeWay_BothChangedSame()
    {
        QDateTime start = QDateTime::currentDateTime();
        QDateTime end = start.addSecs(3600);

        auto eventBaseline = createEvent("Original", start, end);
        auto eventA = createEvent("Same Change", start, end);  // Changed same way
        auto eventB = createEvent("Same Change", start, end);  // Changed same way

        QList<PropertyDiff> diffs = IncidenceDiff::compare(eventA, eventB, eventBaseline);

        // Both changed to the same value - this should be detected
        // Actually, since A == B, this should be Identical and not appear in diffs
        QVERIFY(diffs.isEmpty());
    }

    void testCompare_TodoSpecificProperties()
    {
        auto todoA = createTodo("Task", QDateTime::currentDateTime().addDays(1));
        todoA->setPercentComplete(50);
        todoA->setStatus(KCalendarCore::Incidence::StatusInProcess);

        auto todoB = createTodo("Task", QDateTime::currentDateTime().addDays(1));
        todoB->setPercentComplete(75);
        todoB->setStatus(KCalendarCore::Incidence::StatusCompleted);

        QList<PropertyDiff> diffs = IncidenceDiff::compare(todoA, todoB);

        QVERIFY(diffs.size() >= 2);  // At least PERCENT-COMPLETE and STATUS

        bool foundPercent = false;
        bool foundStatus = false;
        for (const PropertyDiff &diff : diffs) {
            if (diff.propertyName == "PERCENT-COMPLETE") {
                foundPercent = true;
                QCOMPARE(diff.valueA, QStringLiteral("50"));
                QCOMPARE(diff.valueB, QStringLiteral("75"));
            }
            if (diff.propertyName == "STATUS") {
                foundStatus = true;
            }
        }
        QVERIFY(foundPercent);
        QVERIFY(foundStatus);
    }

    void testMerge_UseA()
    {
        QDateTime start = QDateTime::currentDateTime();
        QDateTime end = start.addSecs(3600);

        auto eventA = createEvent("Meeting A", start, end);
        eventA->setLocation("Room A");

        auto eventB = createEvent("Meeting B", start, end);
        eventB->setLocation("Room B");

        QList<PropertyDiff> diffs = IncidenceDiff::compare(eventA, eventB);

        // Set all resolutions to UseA
        for (PropertyDiff &diff : diffs) {
            diff.resolution = PropertyDiff::UseA;
        }

        auto merged = IncidenceDiff::merge(eventB, diffs);
        QVERIFY(merged);

        QCOMPARE(merged->summary(), QStringLiteral("Meeting A"));
        QCOMPARE(merged->location(), QStringLiteral("Room A"));
    }

    void testMerge_UseB()
    {
        QDateTime start = QDateTime::currentDateTime();
        QDateTime end = start.addSecs(3600);

        auto eventA = createEvent("Meeting A", start, end);
        eventA->setLocation("Room A");

        auto eventB = createEvent("Meeting B", start, end);
        eventB->setLocation("Room B");

        QList<PropertyDiff> diffs = IncidenceDiff::compare(eventA, eventB);

        // Set all resolutions to UseB
        for (PropertyDiff &diff : diffs) {
            diff.resolution = PropertyDiff::UseB;
        }

        auto merged = IncidenceDiff::merge(eventA, diffs);
        QVERIFY(merged);

        QCOMPARE(merged->summary(), QStringLiteral("Meeting B"));
        QCOMPARE(merged->location(), QStringLiteral("Room B"));
    }

    void testMerge_Mixed()
    {
        QDateTime start = QDateTime::currentDateTime();
        QDateTime end = start.addSecs(3600);

        auto eventA = createEvent("Meeting A", start, end);
        eventA->setLocation("Room A");
        eventA->setDescription("Desc A");

        auto eventB = createEvent("Meeting B", start, end);
        eventB->setLocation("Room B");
        eventB->setDescription("Desc B");

        QList<PropertyDiff> diffs = IncidenceDiff::compare(eventA, eventB);

        // Set mixed resolutions: SUMMARY from A, LOCATION from B, etc.
        for (PropertyDiff &diff : diffs) {
            if (diff.propertyName == "SUMMARY") {
                diff.resolution = PropertyDiff::UseA;
            } else if (diff.propertyName == "LOCATION") {
                diff.resolution = PropertyDiff::UseB;
            } else if (diff.propertyName == "DESCRIPTION") {
                diff.resolution = PropertyDiff::UseA;
            }
        }

        auto merged = IncidenceDiff::merge(eventA, diffs);
        QVERIFY(merged);

        QCOMPARE(merged->summary(), QStringLiteral("Meeting A"));
        QCOMPARE(merged->location(), QStringLiteral("Room B"));
        QCOMPARE(merged->description(), QStringLiteral("Desc A"));
    }

    void testMerge_CustomValue()
    {
        QDateTime start = QDateTime::currentDateTime();
        QDateTime end = start.addSecs(3600);

        auto eventA = createEvent("Meeting A", start, end);
        auto eventB = createEvent("Meeting B", start, end);

        QList<PropertyDiff> diffs = IncidenceDiff::compare(eventA, eventB);

        // Set custom resolution
        QVERIFY(diffs.size() >= 1);
        diffs[0].resolution = PropertyDiff::UseCustom;
        diffs[0].customValue = QStringLiteral("Custom Meeting");

        auto merged = IncidenceDiff::merge(eventA, diffs);
        QVERIFY(merged);

        QCOMPARE(merged->summary(), QStringLiteral("Custom Meeting"));
    }

    void testParseIcalProperties()
    {
        QString ical = QStringLiteral(
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Test//Test//EN\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:test-uid-123\r\n"
            "SUMMARY:Test Event\r\n"
            "DTSTART:20250120T100000\r\n"
            "DTEND:20250120T110000\r\n"
            "LOCATION:Conference Room\r\n"
            "DESCRIPTION:Test description\r\n"
            "STATUS:CONFIRMED\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n"
        );

        QMap<QString, QString> props = IncidenceDiff::parseIcalProperties(ical);

        QCOMPARE(props.value("UID"), QStringLiteral("test-uid-123"));
        QCOMPARE(props.value("SUMMARY"), QStringLiteral("Test Event"));
        QCOMPARE(props.value("DTSTART"), QStringLiteral("20250120T100000"));
        QCOMPARE(props.value("DTEND"), QStringLiteral("20250120T110000"));
        QCOMPARE(props.value("LOCATION"), QStringLiteral("Conference Room"));
        QCOMPARE(props.value("DESCRIPTION"), QStringLiteral("Test description"));
        QCOMPARE(props.value("STATUS"), QStringLiteral("CONFIRMED"));
    }

    void testParseIcalProperties_WithTimezone()
    {
        QString ical = QStringLiteral(
            "BEGIN:VCALENDAR\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:tz-test\r\n"
            "DTSTART;TZID=America/New_York:20250120T100000\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n"
        );

        QMap<QString, QString> props = IncidenceDiff::parseIcalProperties(ical);

        QCOMPARE(props.value("UID"), QStringLiteral("tz-test"));
        // DTSTART should include timezone info
        QVERIFY(props.value("DTSTART").contains("America/New_York") ||
                props.value("DTSTART").contains("20250120T100000"));
    }

    void testParseIcalProperties_MultipleExdates()
    {
        QString ical = QStringLiteral(
            "BEGIN:VCALENDAR\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:exdate-test\r\n"
            "SUMMARY:Recurring\r\n"
            "EXDATE:20250121T100000\r\n"
            "EXDATE:20250122T100000\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n"
        );

        QMap<QString, QString> props = IncidenceDiff::parseIcalProperties(ical);

        // Multiple EXDATE lines should be combined
        QString exdates = props.value("EXDATE");
        QVERIFY(exdates.contains("20250121T100000"));
        QVERIFY(exdates.contains("20250122T100000"));
    }

    void testCompareIcal()
    {
        QString icalA = QStringLiteral(
            "BEGIN:VCALENDAR\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:test\r\n"
            "SUMMARY:Meeting A\r\n"
            "LOCATION:Room A\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n"
        );

        QString icalB = QStringLiteral(
            "BEGIN:VCALENDAR\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:test\r\n"
            "SUMMARY:Meeting B\r\n"
            "LOCATION:Room A\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n"
        );

        QList<PropertyDiff> diffs = IncidenceDiff::compareIcal(icalA, icalB);

        // Only SUMMARY differs (UID is often skipped in comparison)
        bool foundSummary = false;
        for (const PropertyDiff &diff : diffs) {
            if (diff.propertyName == "SUMMARY") {
                foundSummary = true;
                QCOMPARE(diff.valueA, QStringLiteral("Meeting A"));
                QCOMPARE(diff.valueB, QStringLiteral("Meeting B"));
                QCOMPARE(diff.state, PropertyDiff::BothDifferent);
            }
        }
        QVERIFY(foundSummary);
    }

    void testApplyPropertyToIncidence()
    {
        auto event = createEvent("Original", QDateTime::currentDateTime(),
                                  QDateTime::currentDateTime().addSecs(3600));

        // Apply SUMMARY
        QVERIFY(IncidenceDiff::applyPropertyToIncidence(event, "SUMMARY", "New Summary"));
        QCOMPARE(event->summary(), QStringLiteral("New Summary"));

        // Apply LOCATION
        QVERIFY(IncidenceDiff::applyPropertyToIncidence(event, "LOCATION", "New Location"));
        QCOMPARE(event->location(), QStringLiteral("New Location"));

        // Apply STATUS
        QVERIFY(IncidenceDiff::applyPropertyToIncidence(event, "STATUS", "CONFIRMED"));
        QCOMPARE(event->status(), KCalendarCore::Incidence::StatusConfirmed);

        // Apply PRIORITY
        QVERIFY(IncidenceDiff::applyPropertyToIncidence(event, "PRIORITY", "3"));
        QCOMPARE(event->priority(), 3);

        // Apply CATEGORIES
        QVERIFY(IncidenceDiff::applyPropertyToIncidence(event, "CATEGORIES", "Work,Important"));
        QVERIFY(event->categories().contains("Work"));
        QVERIFY(event->categories().contains("Important"));
    }

    void testPropertyDiffHelpers()
    {
        PropertyDiff diff;

        // Test isConflict
        diff.state = PropertyDiff::Identical;
        QVERIFY(!diff.isConflict());

        diff.state = PropertyDiff::OnlyInA;
        QVERIFY(!diff.isConflict());

        diff.state = PropertyDiff::BothDifferent;
        QVERIFY(diff.isConflict());

        diff.state = PropertyDiff::BothChangedDifferent;
        QVERIFY(diff.isConflict());

        // Test needsResolution
        diff.state = PropertyDiff::BothDifferent;
        diff.resolution = PropertyDiff::Unresolved;
        QVERIFY(diff.needsResolution());

        diff.resolution = PropertyDiff::UseA;
        QVERIFY(!diff.needsResolution());

        // Test hasValue
        diff.valueA = "";
        diff.valueB = "";
        QVERIFY(!diff.hasValue());

        diff.valueA = "something";
        QVERIFY(diff.hasValue());
    }

    void testDiffsSortedByPriority()
    {
        QDateTime start = QDateTime::currentDateTime();
        QDateTime end = start.addSecs(3600);

        auto eventA = createEvent("Meeting A", start, end);
        eventA->setLocation("Room A");
        eventA->setDescription("Desc A");
        eventA->setUrl(QUrl("http://a.com"));

        auto eventB = createEvent("Meeting B", start.addSecs(3600), end.addSecs(3600));
        eventB->setLocation("Room B");
        eventB->setDescription("Desc B");
        eventB->setUrl(QUrl("http://b.com"));

        QList<PropertyDiff> diffs = IncidenceDiff::compare(eventA, eventB);

        // Verify diffs are sorted by priority
        for (int i = 1; i < diffs.size(); ++i) {
            int prevPriority = IncidenceDiff::propertySortPriority(diffs[i-1].propertyName);
            int currPriority = IncidenceDiff::propertySortPriority(diffs[i].propertyName);
            QVERIFY2(prevPriority <= currPriority,
                     qPrintable(QString("Property %1 (priority %2) should come before %3 (priority %4)")
                         .arg(diffs[i-1].propertyName).arg(prevPriority)
                         .arg(diffs[i].propertyName).arg(currPriority)));
        }
    }
};

QTEST_MAIN(TestIncidenceDiff)
#include "tst_incidencediff.moc"

