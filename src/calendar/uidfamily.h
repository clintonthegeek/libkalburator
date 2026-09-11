#ifndef KALBURATOR_CALENDAR_UIDFAMILY_H
#define KALBURATOR_CALENDAR_UIDFAMILY_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMap>
#include <QString>
#include <QTimeZone>

#include <KCalendarCore/Incidence>

#include <kalburator/calendar/icalcodec.h>

namespace Kalburator::Calendar::UidFamily {

/// UID-family assembly, shared by every transport that stores a whole family
/// as one unit.
///
/// A recurring master and its detached RECURRENCE-ID overrides share a UID.
/// RFC 4791 §4.1 requires them to live in the SAME calendar object resource,
/// and the vdir format permits multiple components per file for exactly this
/// case. ADR 0008 established that; ADR 0010 decision 2 requires that both
/// write paths — the engine's record path and PlanStan's staging flush — use
/// ONE implementation of it rather than each growing its own.
///
/// Everything here works on bytes and incidences, never on files or URLs, so
/// LocalBackend can feed it a file's contents and RemoteCalendarBackend a
/// GET's response body.
///
/// A flush routinely carries only PART of a family: editing one occurrence
/// stages the override AND the master's new EXDATE, while deleting one stages
/// only the override. Writing the staged components alone would destroy
/// whatever else the resource holds, so every write merges into what is
/// already there.

/// Slot of a component within its family: empty for the master, the UTC ISO
/// recurrence-id for an override. Normalized to UTC the same way
/// composeRecordIdentity() is, so one instant keys one slot whatever time zone
/// it is spelled in.
inline QString slotKey(const KCalendarCore::Incidence::Ptr &inc)
{
    if (!inc || !inc->recurrenceId().isValid())
        return QString();
    return inc->recurrenceId().toUTC().toString(Qt::ISODate);
}

/// A family keyed by slot. QMap, so iteration is ordered by recurrence-id and
/// the master (empty key) sorts first.
using Family = QMap<QString, KCalendarCore::Incidence::Ptr>;

/// Parse a stored resource into its slots. Unparseable or empty bytes yield an
/// empty family rather than an error: a merge over it then simply writes what
/// it was given, which is the right behaviour for a resource that does not
/// exist yet.
inline Family parse(const QByteArray &ics)
{
    Family family;
    for (const auto &inc : Kalburator::Sync::incidencesFromIcal(ics)) {
        if (inc)
            family.insert(slotKey(inc), inc);
    }
    return family;
}

/// Master first, then overrides in recurrence-id order. Deterministic, because
/// fingerprint-based change detection compares the resulting bytes.
///
/// An override whose master is absent is emitted as-is. RFC 4791 permits an
/// orphan override and ADR 0008 decision 4 requires carrying it through
/// verbatim; no master is ever synthesized.
inline QList<KCalendarCore::Incidence::Ptr> ordered(const Family &family)
{
    QList<KCalendarCore::Incidence::Ptr> out;
    if (auto master = family.value(QString()))
        out.append(master);
    for (auto it = family.cbegin(); it != family.cend(); ++it) {
        if (!it.key().isEmpty() && it.value())
            out.append(it.value());
    }
    return out;
}

/// Merge staged components over an existing family. A staged component
/// replaces whatever occupies its slot; every other slot survives untouched.
/// Later entries in @p staged win over earlier ones for the same slot.
inline Family merge(Family existing,
                    const QList<KCalendarCore::Incidence::Ptr> &staged)
{
    for (const auto &inc : staged) {
        if (inc)
            existing.insert(slotKey(inc), inc);
    }
    return existing;
}

/// Remove one component by slot, leaving the rest. Deleting one occurrence
/// must not destroy its master, and deleting a master leaves its overrides as
/// orphans (ADR 0008 decision 4).
inline Family without(Family family, const QString &slot)
{
    family.remove(slot);
    return family;
}

/// Serialize a family as one VCALENDAR. Empty families produce empty bytes,
/// which callers treat as "remove the resource" rather than "write nothing".
inline QByteArray serialize(const Family &family)
{
    const auto components = ordered(family);
    if (components.isEmpty())
        return QByteArray();
    return Kalburator::Sync::icalFromIncidences(components);
}

/// Group a flat list of staged writes into families, and report the order the
/// UIDs were first seen so callers can dispatch deterministically. Components
/// with an empty UID are dropped; a caller that cares should warn before
/// calling.
inline QHash<QString, QList<KCalendarCore::Incidence::Ptr>> groupByUid(
    const QList<KCalendarCore::Incidence::Ptr> &writes,
    QList<QString> *uidOrder = nullptr)
{
    QHash<QString, QList<KCalendarCore::Incidence::Ptr>> byUid;
    for (const auto &inc : writes) {
        if (!inc || inc->uid().isEmpty())
            continue;
        const QString uid = inc->uid();
        if (uidOrder && !byUid.contains(uid))
            uidOrder->append(uid);
        byUid[uid].append(inc);
    }
    return byUid;
}

} // namespace Kalburator::Calendar::UidFamily

#endif // KALBURATOR_CALENDAR_UIDFAMILY_H
