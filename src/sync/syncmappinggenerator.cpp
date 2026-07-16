#include "syncmappinggenerator.h"
#include "logicalcalendar.h"
#include "logicalcalendarjson.h"

namespace Kalburator::Sync {

QList<SyncMapping> generateMappings(const LogicalCalendar &lc, SyncTopology topology)
{
    QList<SyncMapping> out;
    if (!lc.syncEnabled)
        return out;

    const CalendarBackendBinding primary = lc.primaryBinding();
    if (!primary.isValid())
        return out;

    // Writable sync bindings only (exclude the primary itself and ReadOnly).
    QList<CalendarBackendBinding> syncBindings;
    for (const auto &b : lc.orderedSyncBindings()) {
        if (b.backendId == primary.backendId && b.calendarId == primary.calendarId)
            continue;
        if (b.role == BackendRole::ReadOnly)
            continue;
        syncBindings.append(b);
    }
    if (syncBindings.isEmpty())
        return out;

    auto make = [](const QString &id,
                   const CalendarBackendBinding &s,
                   const CalendarBackendBinding &t) {
        SyncMapping m;
        m.id = id;
        m.enabled = true;
        m.mode = SyncMode::TwoWay;
        m.conflictPolicy = ConflictResolution::AskUser;
        m.sourceBackend = s.backendId;
        m.sourceCalendar = s.calendarId;
        m.targetBackend = t.backendId;
        m.targetCalendar = t.calendarId;
        return m;
    };

    switch (topology) {
    case SyncTopology::Star:
        for (const auto &s : syncBindings)
            out.append(make(QStringLiteral("auto_%1_%2").arg(lc.id, backendRoleToString(s.role)),
                            primary, s));
        break;
    case SyncTopology::Mirror: {
        QList<CalendarBackendBinding> all;
        all.append(primary);
        all.append(syncBindings);
        for (int i = 0; i < all.size(); ++i)
            for (int j = i + 1; j < all.size(); ++j)
                out.append(make(QStringLiteral("auto_%1_mirror_%2_%3")
                                    .arg(lc.id, all[i].backendId, all[j].backendId),
                                all[i], all[j]));
        break;
    }
    case SyncTopology::Chain: {
        QList<CalendarBackendBinding> chain;
        chain.append(primary);
        chain.append(syncBindings);
        for (int i = 0; i + 1 < chain.size(); ++i)
            out.append(make(QStringLiteral("auto_%1_chain_%2_%3")
                                .arg(lc.id, chain[i].backendId, chain[i + 1].backendId),
                            chain[i], chain[i + 1]));
        break;
    }
    }
    return out;
}

QList<SyncMapping> generateMappings(const QList<LogicalCalendar> &lcs, SyncTopology topology)
{
    QList<SyncMapping> out;
    for (const auto &lc : lcs) {
        SyncTopology effective = topology;
        switch (lc.wiringPolicy) {
        case WiringPolicy::Manual:            continue; // persisted mappings only
        case WiringPolicy::Hub:               effective = SyncTopology::Star;   break;
        case WiringPolicy::Mesh:              effective = SyncTopology::Mirror; break;
        case WiringPolicy::Chain:             effective = SyncTopology::Chain;  break;
        case WiringPolicy::CollectionDefault: break;
        }
        out.append(generateMappings(lc, effective));
    }
    return out;
}

} // namespace Kalburator::Sync
