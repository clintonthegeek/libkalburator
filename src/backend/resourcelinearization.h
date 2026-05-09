#ifndef KALBURATOR_BACKEND_RESOURCELINEARIZATION_H
#define KALBURATOR_BACKEND_RESOURCELINEARIZATION_H

#include <QString>

namespace Kalburator::Backend {

/**
 * @brief Capability interface — resource-linearization key.
 *
 * Backends backed by a resource that can serve only one client at
 * a time (Palm hot-sync cradle, USB-attached single-host store, a
 * shared serial device, …) opt into this interface. The engine's
 * mapping scheduler reads `linearizationKey()` and serializes
 * mappings whose source-or-target backend returns the same key.
 *
 * Backends without contention return the default empty key, meaning
 * "no constraint — can run in parallel with anything else."
 *
 * Pure-virtual non-QObject interface. Backends inherit via multiple
 * inheritance alongside their main backend base class.
 *
 * The interface has a default body so backends can opt in by simply
 * deriving and overriding `linearizationKey()` only when they have
 * a contended resource.
 */
class ResourceLinearization
{
public:
    virtual ~ResourceLinearization() = default;

    /**
     * @brief Linearization key for this backend's resource.
     *
     * Mappings whose source or target backend returns the same
     * non-empty key from this method must execute serially. Empty
     * string (the default) means no linearization required.
     *
     * Examples:
     *   - PalmHotSyncBackend:  return "palm:" + serialDevicePath();
     *   - LocalSqliteBackend (single-writer DB): return dbPath();
     *   - RemoteCalDavBackend: return {} (no contention; many parallel
     *                                    HTTP connections fine).
     */
    virtual QString linearizationKey() const { return {}; }
};

} // namespace Kalburator::Backend

#endif // KALBURATOR_BACKEND_RESOURCELINEARIZATION_H
