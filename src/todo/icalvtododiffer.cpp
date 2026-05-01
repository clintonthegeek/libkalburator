#include "icalvtododiffer.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Todo>

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

// Compare two optional DateTime values.
bool dtEqual(const QDateTime &a, const QDateTime &b)
{
    return a == b;
}

} // namespace

namespace Kalburator::Todo {

QSet<PropertyId> IRecordDifferVTodo::diff(
    const CanonicalRecord &source,
    const CanonicalRecord &baseline) const
{
    if (source.data == baseline.data)
        return {};

    const auto src  = parseTodo(source.data);
    const auto base = parseTodo(baseline.data);

    if (!src && !base)
        return {};

    QSet<PropertyId> changed;

    if (!src || !base) {
        changed.insert(PropertyId{"uid"});
        changed.insert(PropertyId{"summary"});
        return changed;
    }

    if (src->uid()             != base->uid())             changed.insert(PropertyId{"uid"});
    if (src->summary()         != base->summary())         changed.insert(PropertyId{"summary"});
    if (src->description()     != base->description())     changed.insert(PropertyId{"description"});
    if (!dtEqual(src->dtStart(), base->dtStart()))         changed.insert(PropertyId{"dtstart"});
    if (!dtEqual(src->dtDue(),   base->dtDue()))           changed.insert(PropertyId{"due"});
    if (!dtEqual(src->completed(), base->completed()))     changed.insert(PropertyId{"completed"});
    if (!dtEqual(src->created(), base->created()))         changed.insert(PropertyId{"created"});
    if (!dtEqual(src->lastModified(), base->lastModified())) changed.insert(PropertyId{"lastmodified"});
    if (src->status()          != base->status())          changed.insert(PropertyId{"status"});
    if (src->priority()        != base->priority())        changed.insert(PropertyId{"priority"});
    if (src->percentComplete() != base->percentComplete()) changed.insert(PropertyId{"percentcomplete"});
    if (src->categories()      != base->categories())      changed.insert(PropertyId{"categories"});

    return changed;
}

bool IRecordDifferVTodo::equal(
    const CanonicalRecord &a,
    const CanonicalRecord &b) const
{
    if (a.data == b.data)
        return true;

    const auto todoA = parseTodo(a.data);
    const auto todoB = parseTodo(b.data);

    if (!todoA && !todoB)
        return true;
    if (!todoA || !todoB)
        return false;

    return diff(a, b).isEmpty();
}

} // namespace Kalburator::Todo
