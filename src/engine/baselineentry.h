#ifndef KALBURATOR_ENGINE_BASELINEENTRY_H
#define KALBURATOR_ENGINE_BASELINEENTRY_H

#include <QString>

namespace Kalburator::Engine {

/// Phase B4 (N2 fix): a baseline is no longer a single content hash
/// compared against both sides of a mapping. Two backends never serialize
/// the same logical record identically (PRODID, property order, folding,
/// server normalization) — a single stored hash checked against both
/// sides' native bytes means at least one side reads "modified" forever
/// after any cross-backend write, which is exactly the non-convergence
/// bug this phase fixes (finding N2).
///
/// BaselineEntry records what EACH side's native bytes hashed to at the
/// moment of the last successful sync. It replaces smuggling a single
/// hash through `BackendRecord::contentHash` for the baseline row.
struct BaselineEntry
{
    QString id;          ///< record id (matches BackendRecord::id)
    QString sourceHash;  ///< source backend's native-bytes hash at last sync
    QString targetHash;  ///< target backend's native-bytes hash at last sync
};

} // namespace Kalburator::Engine

#endif // KALBURATOR_ENGINE_BASELINEENTRY_H
