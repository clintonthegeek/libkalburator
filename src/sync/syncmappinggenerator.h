#ifndef KALBURATOR_SYNCMAPPINGGENERATOR_H
#define KALBURATOR_SYNCMAPPINGGENERATOR_H

#include <QList>
#include "synctypes.h"   // SyncMapping, SyncTopology

namespace Kalburator::Sync {

struct LogicalCalendar;

/**
 * @brief Translate a logical calendar's bindings + a topology into the flat
 *        SyncMapping list the SyncEngine consumes. Pure; no engine/store state.
 *
 * Star   = Primary <-> each enabled non-ReadOnly Sync* (hub-and-spoke).
 * Mirror = full mesh (every backend <-> every other).
 * Chain  = sequential (Primary <-> Sync1 <-> Sync2 ...).
 * Returns empty when !syncEnabled, no valid primary, or no sync bindings.
 *
 * Promoted from PlanStan CollectionController so libkalburator owns the verb
 * that animates its policy vocabulary; both consumers (PlanStan, WildPalms)
 * call this instead of forking their own generator.
 */
QList<SyncMapping> generateMappings(const LogicalCalendar &lc, SyncTopology topology);

/// Convenience: concatenates the single-lc overload over a list.
QList<SyncMapping> generateMappings(const QList<LogicalCalendar> &lcs, SyncTopology topology);

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNCMAPPINGGENERATOR_H
