// tests/calendar/differs/tst_ical_record_differ.cpp
#include <QTest>
#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>

#include "icalrecorddiffer.h"
#include "canonicalrecord.h"
#include "shape.h"

using namespace Kalburator::Calendar;
using Kalburator::Shape::CanonicalRecord;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::PropertyId;

namespace {

/// Helper to create iCal-encoded event data for testing.
static QByteArray makeIcal(const QString& uid,
                            const QString& summary,
                            int priority = 0,
                            const QString& description = {},
                            const QStringList& categories = {})
{
    auto ev = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    ev->setUid(uid);
    ev->setSummary(summary);
    if (priority > 0)
        ev->setPriority(priority);
    if (!description.isEmpty())
        ev->setDescription(description);
    if (!categories.isEmpty())
        ev->setCategories(categories);
    ev->setDtStart(QDateTime(QDate(2026, 1, 1), QTime(9, 0), Qt::UTC));

    KCalendarCore::ICalFormat fmt;
    return fmt.toICalString(ev).toUtf8();
}

/// Helper to create a CanonicalRecord with iCal shape.
static CanonicalRecord makeRecord(const QString& uid,
                                  const QString& summary,
                                  int priority = 0,
                                  const QString& description = {},
                                  const QStringList& categories = {})
{
    CanonicalRecord rec;
    rec.shape = Kalburator::Shape::Shape{DomainId{"calendar"}, EncodingId{"ical"}};
    rec.data = makeIcal(uid, summary, priority, description, categories);
    rec.recordId = uid;
    rec.isDeleted = false;
    return rec;
}

} // namespace

class TestICalRecordDiffer : public QObject
{
    Q_OBJECT

private slots:
    /// Two records with identical iCal text should have empty diff and equal() returns true.
    void identicalRecords()
    {
        const auto source = makeRecord(QStringLiteral("uid-1"), QStringLiteral("Meeting"));
        const auto baseline = makeRecord(QStringLiteral("uid-1"), QStringLiteral("Meeting"));

        IRecordDifferICal differ;
        const QSet<PropertyId> changed = differ.diff(source, baseline);
        QVERIFY(changed.isEmpty());
        QVERIFY(differ.equal(source, baseline));
    }

    /// Changing summary should be detected in diff set.
    void differentSummary()
    {
        const auto source = makeRecord(QStringLiteral("uid-1"), QStringLiteral("Meeting"));
        const auto baseline = makeRecord(QStringLiteral("uid-1"), QStringLiteral("Old"));

        IRecordDifferICal differ;
        const QSet<PropertyId> changed = differ.diff(source, baseline);
        QVERIFY(!changed.isEmpty());
        QVERIFY(changed.contains(PropertyId{"summary"}));
        QVERIFY(!differ.equal(source, baseline));
    }

    /// Changing priority should be detected in diff set.
    void differentPriority()
    {
        const auto source = makeRecord(QStringLiteral("uid-1"), QStringLiteral("Meeting"), 5);
        const auto baseline = makeRecord(QStringLiteral("uid-1"), QStringLiteral("Meeting"), 1);

        IRecordDifferICal differ;
        const QSet<PropertyId> changed = differ.diff(source, baseline);
        QVERIFY(!changed.isEmpty());
        QVERIFY(changed.contains(PropertyId{"priority"}));
        QVERIFY(!differ.equal(source, baseline));
    }

    /// Multiple field changes should all appear in the diff result.
    void multipleFieldChanges()
    {
        const auto source = makeRecord(
            QStringLiteral("uid-1"),
            QStringLiteral("New Summary"),
            5,
            QStringLiteral("New description"));
        const auto baseline = makeRecord(
            QStringLiteral("uid-1"),
            QStringLiteral("Old Summary"),
            0,
            QStringLiteral("Old description"));

        IRecordDifferICal differ;
        const QSet<PropertyId> changed = differ.diff(source, baseline);
        QVERIFY(!changed.isEmpty());
        QVERIFY(changed.contains(PropertyId{"summary"}));
        QVERIFY(changed.contains(PropertyId{"description"}));
        QVERIFY(changed.contains(PropertyId{"priority"}));
        QVERIFY(!differ.equal(source, baseline));
    }

    /// Adding a category should be detected.
    void categoriesModified()
    {
        const auto source = makeRecord(
            QStringLiteral("uid-1"),
            QStringLiteral("Meeting"),
            0,
            {},
            QStringList{QStringLiteral("Work")});
        const auto baseline = makeRecord(
            QStringLiteral("uid-1"),
            QStringLiteral("Meeting"));

        IRecordDifferICal differ;
        const QSet<PropertyId> changed = differ.diff(source, baseline);
        QVERIFY(!changed.isEmpty());
        QVERIFY(changed.contains(PropertyId{"categories"}));
        QVERIFY(!differ.equal(source, baseline));
    }
};

QTEST_GUILESS_MAIN(TestICalRecordDiffer)
#include "tst_ical_record_differ.moc"
