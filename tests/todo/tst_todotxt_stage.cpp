#include <QTest>

#include "todotxttransformation.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Todo>

using Kalburator::Todo::ICalToTodoTxtStage;
using Kalburator::Todo::TodoTxtToICalStage;

namespace {

QByteArray makeTodo(const QString &uid, const QString &summary,
                    int priority = 0, bool completed = false,
                    const QDate &due = {})
{
    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo);
    todo->setUid(uid);
    todo->setSummary(summary);
    todo->setPriority(priority);
    if (completed)
        todo->setCompleted(true);
    if (due.isValid())
        todo->setDtDue(QDateTime(due, QTime(), QTimeZone::utc()));
    KCalendarCore::ICalFormat fmt;
    return fmt.toICalString(todo).toUtf8();
}

} // namespace

class TestTodoTxtStage : public QObject {
    Q_OBJECT
private slots:
    void simpleTodoRountrips()
    {
        const auto icalIn = makeTodo(QStringLiteral("u1"), QStringLiteral("Buy milk"));
        ICalToTodoTxtStage fwd;
        const auto txt = fwd.transform(icalIn);
        QVERIFY(!txt.isEmpty());
        QVERIFY(txt.contains("Buy milk"));
    }

    void completedPrefixedWithX()
    {
        const auto icalIn = makeTodo(QStringLiteral("u1"), QStringLiteral("Done"), 0, true);
        ICalToTodoTxtStage fwd;
        const auto txt = QString::fromUtf8(fwd.transform(icalIn));
        QVERIFY(txt.startsWith(QStringLiteral("x ")));
    }

    void priorityEncodedAsLetter()
    {
        const auto icalIn = makeTodo(QStringLiteral("u1"), QStringLiteral("Urgent"), 1);
        ICalToTodoTxtStage fwd;
        const auto txt = QString::fromUtf8(fwd.transform(icalIn));
        QVERIFY(txt.contains(QStringLiteral("(A)")));
    }

    void dueDateEncoded()
    {
        const auto icalIn = makeTodo(QStringLiteral("u1"), QStringLiteral("Task"), 0, false,
                                     QDate(2026, 6, 1));
        ICalToTodoTxtStage fwd;
        const auto txt = QString::fromUtf8(fwd.transform(icalIn));
        QVERIFY(txt.contains(QStringLiteral("due:2026-06-01")));
    }

    void roundtrip_summaryPreserved()
    {
        const auto icalIn = makeTodo(QStringLiteral("u1"), QStringLiteral("Groceries"));
        ICalToTodoTxtStage fwd;
        TodoTxtToICalStage rev;
        const auto txt = fwd.transform(icalIn);
        const auto icalOut = rev.transform(txt);
        QVERIFY(!icalOut.isEmpty());

        KCalendarCore::ICalFormat fmt;
        auto todo = fmt.fromString(QString::fromUtf8(icalOut)).dynamicCast<KCalendarCore::Todo>();
        QVERIFY(todo);
        QCOMPARE(todo->summary(), QStringLiteral("Groceries"));
    }

    void emptyInputReturnsEmpty()
    {
        ICalToTodoTxtStage fwd;
        QVERIFY(fwd.transform({}).isEmpty());
        TodoTxtToICalStage rev;
        QVERIFY(rev.transform({}).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestTodoTxtStage)
#include "tst_todotxt_stage.moc"
