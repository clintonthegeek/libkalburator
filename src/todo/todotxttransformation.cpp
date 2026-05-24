#include "todotxttransformation.h"

#include "propertycatalogue.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Todo>

#include <QRegularExpression>

using namespace Kalburator::Shape;

namespace {

KCalendarCore::Todo::Ptr parseTodo(const QByteArray &data)
{
    if (data.isEmpty())
        return {};
    KCalendarCore::ICalFormat fmt;
    auto inc = fmt.fromString(QString::fromUtf8(data));
    return inc.dynamicCast<KCalendarCore::Todo>();
}

// Serialise a KCalendarCore::Todo::Ptr to a full VCALENDAR iCal string.
QByteArray serializeTodo(const KCalendarCore::Todo::Ptr &todo)
{
    if (!todo)
        return {};
    KCalendarCore::ICalFormat fmt;
    return fmt.toICalString(todo).toUtf8();
}

// Build one todo.txt line from a VTODO.
// Format: [x ][(priority) ]<summary>[ due:YYYY-MM-DD][ +project][ @context]
QByteArray vtodoToTodoTxt(const KCalendarCore::Todo::Ptr &todo)
{
    QString line;

    if (todo->isCompleted())
        line += QStringLiteral("x ");

    const int pri = todo->priority();
    if (pri > 0 && pri <= 9) {
        const QChar letter = QChar(QLatin1Char('A' + pri - 1));
        line += QStringLiteral("(%1) ").arg(letter);
    }

    line += todo->summary();

    if (todo->hasDueDate())
        line += QStringLiteral(" due:%1").arg(todo->dtDue().date().toString(Qt::ISODate));

    for (const QString &cat : todo->categories()) {
        if (cat.startsWith(QLatin1Char('+')))
            line += QStringLiteral(" %1").arg(cat);
        else if (cat.startsWith(QLatin1Char('@')))
            line += QStringLiteral(" %1").arg(cat);
        else
            line += QStringLiteral(" +%1").arg(cat);
        break; // keep only the first category (known loss)
    }

    return line.toUtf8();
}

// Parse one todo.txt line into a new KCalendarCore::Todo.
KCalendarCore::Todo::Ptr todoTxtToVTodo(const QString &line)
{
    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo);

    QString rest = line.trimmed();

    // Completion prefix
    if (rest.startsWith(QLatin1String("x "))) {
        todo->setCompleted(true);
        rest = rest.mid(2).trimmed();
    }

    // Priority prefix: (A) … (Z)
    static const QRegularExpression priRe(QStringLiteral("^\\(([A-Z])\\)\\s+"));
    const auto priMatch = priRe.match(rest);
    if (priMatch.hasMatch()) {
        todo->setPriority(priMatch.captured(1).at(0).toLatin1() - 'A' + 1);
        rest = rest.mid(priMatch.capturedLength()).trimmed();
    }

    // Extract due:YYYY-MM-DD
    static const QRegularExpression dueRe(QStringLiteral("\\bdue:(\\d{4}-\\d{2}-\\d{2})\\b"));
    const auto dueMatch = dueRe.match(rest);
    if (dueMatch.hasMatch()) {
        const QDate d = QDate::fromString(dueMatch.captured(1), Qt::ISODate);
        if (d.isValid())
            todo->setDtDue(QDateTime(d, QTime(), Qt::UTC));
        rest.remove(dueMatch.capturedStart(), dueMatch.capturedLength());
    }

    // Extract +project / @context tokens as categories.
    QStringList cats;
    static const QRegularExpression tagRe(QStringLiteral("\\s([+@][^\\s]+)"));
    QRegularExpressionMatchIterator it = tagRe.globalMatch(rest);
    while (it.hasNext()) {
        cats.append(it.next().captured(1));
    }
    // Remove tag tokens from rest.
    rest.remove(tagRe);

    todo->setSummary(rest.trimmed());
    if (!cats.isEmpty())
        todo->setCategories(cats);

    return todo;
}

} // namespace

namespace Kalburator::Todo {

QByteArray ICalToTodoTxtStage::transform(const QByteArray &sourceBytes) const
{
    const auto todo = parseTodo(sourceBytes);
    if (!todo)
        return {};
    return vtodoToTodoTxt(todo);
}

QByteArray TodoTxtToICalStage::transform(const QByteArray &sourceBytes) const
{
    if (sourceBytes.isEmpty())
        return {};
    const auto todo = todoTxtToVTodo(QString::fromUtf8(sourceBytes));
    return serializeTodo(todo);
}

LossProfile todoTxtLoss()
{
    LossProfile p;
    p.affected.insert(PropertyId{QStringLiteral("description")}, LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("attendees")}, LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("rrule")}, LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("attachments")}, LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("alarms")}, LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("customproperties")}, LossKind::Dropped);
    return p;
}

} // namespace Kalburator::Todo
