#include <QTest>

#include "icalvtododiffer.h"
#include "canonicalrecord.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Todo>

using Kalburator::Shape::CanonicalRecord;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::Shape;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Todo::IRecordDifferVTodo;

namespace {

const Shape kShape{ DomainId{"todo"}, EncodingId{"ical-vtodo"} };

QByteArray makeTodo(const QString &uid, const QString &summary,
                    int priority = 0, int percent = 0)
{
    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo);
    todo->setUid(uid);
    todo->setSummary(summary);
    todo->setPriority(priority);
    todo->setPercentComplete(percent);
    KCalendarCore::ICalFormat fmt;
    return fmt.toICalString(todo).toUtf8();
}

CanonicalRecord makeRecord(const QByteArray &data)
{
    CanonicalRecord rec;
    rec.shape    = kShape;
    rec.recordId = QStringLiteral("r1");
    rec.data     = data;
    return rec;
}

} // namespace

class TestVTodoDiffer : public QObject {
    Q_OBJECT
private slots:
    void equalRecordsProduceEmptyDiff()
    {
        IRecordDifferVTodo differ;
        const auto data = makeTodo(QStringLiteral("uid-1"), QStringLiteral("Buy milk"));
        QVERIFY(differ.diff(makeRecord(data), makeRecord(data)).isEmpty());
        QVERIFY(differ.equal(makeRecord(data), makeRecord(data)));
    }

    void changedSummaryIsDetected()
    {
        IRecordDifferVTodo differ;
        const auto a = makeRecord(makeTodo(QStringLiteral("uid-1"), QStringLiteral("Old")));
        const auto b = makeRecord(makeTodo(QStringLiteral("uid-1"), QStringLiteral("New")));
        const auto changed = differ.diff(a, b);
        QVERIFY(changed.contains(PropertyId{"summary"}));
        QVERIFY(!differ.equal(a, b));
    }

    void changedPriorityIsDetected()
    {
        IRecordDifferVTodo differ;
        const auto a = makeRecord(makeTodo(QStringLiteral("uid-1"), QStringLiteral("T"), 3, 0));
        const auto b = makeRecord(makeTodo(QStringLiteral("uid-1"), QStringLiteral("T"), 5, 0));
        QVERIFY(differ.diff(a, b).contains(PropertyId{"priority"}));
    }

    void changedPercentIsDetected()
    {
        IRecordDifferVTodo differ;
        const auto a = makeRecord(makeTodo(QStringLiteral("uid-1"), QStringLiteral("T"), 0, 0));
        const auto b = makeRecord(makeTodo(QStringLiteral("uid-1"), QStringLiteral("T"), 0, 50));
        QVERIFY(differ.diff(a, b).contains(PropertyId{"percentcomplete"}));
    }

    void emptyVsNonEmptyTreatsAllChanged()
    {
        IRecordDifferVTodo differ;
        const auto a = makeRecord(makeTodo(QStringLiteral("uid-1"), QStringLiteral("T")));
        CanonicalRecord empty;
        empty.shape    = kShape;
        empty.recordId = QStringLiteral("r1");
        const auto changed = differ.diff(a, empty);
        QVERIFY(!changed.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestVTodoDiffer)
#include "tst_vtodo_differ.moc"
