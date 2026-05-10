#include "icalvtodomerger.h"

#include "conflictpolicy.h"
#include "icalvtododiffer.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Todo>

using namespace Kalburator::Shape;
using namespace Kalburator::Conflict;

namespace {

KCalendarCore::Todo::Ptr parseTodo(const QByteArray &data)
{
    if (data.isEmpty())
        return {};
    KCalendarCore::ICalFormat fmt;
    auto inc = fmt.fromString(QString::fromUtf8(data));
    return inc.dynamicCast<KCalendarCore::Todo>();
}

QByteArray serializeTodo(const KCalendarCore::Todo::Ptr &todo)
{
    if (!todo)
        return {};
    KCalendarCore::ICalFormat fmt;
    return fmt.toICalString(todo).toUtf8();
}

// Decide which of src/tgt is "newer" when policy is NewerWins.
bool srcWinsOnPolicy(const KCalendarCore::Todo::Ptr &src,
                     const KCalendarCore::Todo::Ptr &tgt,
                     AutoResolveStrategy strategy)
{
    switch (strategy) {
    case AutoResolveStrategy::SourceAlwaysWins:
        return true;
    case AutoResolveStrategy::TargetAlwaysWins:
        return false;
    case AutoResolveStrategy::NewerWins:
        return src->lastModified() >= tgt->lastModified();
    default:
        return true;
    }
}

} // namespace

namespace Kalburator::Todo {

CanonicalRecord RecordMergerVTodo::merge(
    const CanonicalRecord &source,
    const CanonicalRecord &target,
    const CanonicalRecord &baseline,
    const ConflictPolicy &policy) const
{
    const auto src  = parseTodo(source.data);
    const auto tgt  = parseTodo(target.data);
    const auto base = parseTodo(baseline.data);

    if (!src && !tgt)
        return baseline;
    if (!src)
        return target;
    if (!tgt)
        return source;

    // Which side wins on true conflict?
    const bool preferSrc = srcWinsOnPolicy(src, tgt, policy.autoResolve);

    // Build merged todo from src (authoritative side for conflicts).
    KCalendarCore::Todo::Ptr merged(new KCalendarCore::Todo(*src));

    // For each property: if only tgt changed relative to base, take tgt.
    // If both changed (conflict), apply policy.
    auto pick = [&](bool srcChanged, bool tgtChanged, auto setFn, auto srcVal, auto tgtVal) {
        if (!srcChanged && tgtChanged) {
            setFn(merged, tgtVal);
        } else if (srcChanged && tgtChanged && !preferSrc) {
            setFn(merged, tgtVal);
        }
        // else: src value already in merged (either src-only change, or conflict→src wins)
        (void)srcVal;
    };

    auto srcChanged = [&](auto diff) { return !base || diff; };
    auto tgtChanged = [&](auto diff) { return !base || diff; };
    (void)srcChanged; (void)tgtChanged;

    if (base) {
        pick(src->summary() != base->summary(),
             tgt->summary() != base->summary(),
             [](KCalendarCore::Todo::Ptr &t, const QString &v){ t->setSummary(v); },
             src->summary(), tgt->summary());

        pick(src->description() != base->description(),
             tgt->description() != base->description(),
             [](KCalendarCore::Todo::Ptr &t, const QString &v){ t->setDescription(v); },
             src->description(), tgt->description());

        pick(src->priority() != base->priority(),
             tgt->priority() != base->priority(),
             [](KCalendarCore::Todo::Ptr &t, int v){ t->setPriority(v); },
             src->priority(), tgt->priority());

        pick(src->percentComplete() != base->percentComplete(),
             tgt->percentComplete() != base->percentComplete(),
             [](KCalendarCore::Todo::Ptr &t, int v){ t->setPercentComplete(v); },
             src->percentComplete(), tgt->percentComplete());

        pick(src->status() != base->status(),
             tgt->status() != base->status(),
             [](KCalendarCore::Todo::Ptr &t, KCalendarCore::Incidence::Status v){ t->setStatus(v); },
             src->status(), tgt->status());

        pick(src->categories() != base->categories(),
             tgt->categories() != base->categories(),
             [](KCalendarCore::Todo::Ptr &t, const QStringList &v){ t->setCategories(v); },
             src->categories(), tgt->categories());

        pick(src->dtDue() != base->dtDue(),
             tgt->dtDue() != base->dtDue(),
             [](KCalendarCore::Todo::Ptr &t, const QDateTime &v){ t->setDtDue(v); },
             src->dtDue(), tgt->dtDue());
    } else {
        // No baseline — conflict on all differing fields; apply policy.
        if (!preferSrc) {
            merged->setSummary(tgt->summary());
            merged->setDescription(tgt->description());
            merged->setPriority(tgt->priority());
            merged->setPercentComplete(tgt->percentComplete());
            merged->setStatus(tgt->status());
            merged->setCategories(tgt->categories());
            merged->setDtDue(tgt->dtDue());
        }
    }

    CanonicalRecord result;
    result.shape    = source.shape;
    result.recordId = source.recordId;
    result.data     = serializeTodo(merged);
    return result;
}

} // namespace Kalburator::Todo
