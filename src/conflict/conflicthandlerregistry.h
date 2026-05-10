#ifndef KALBURATOR_CONFLICT_CONFLICTHANDLERREGISTRY_H
#define KALBURATOR_CONFLICT_CONFLICTHANDLERREGISTRY_H

#include <QHash>
#include <QString>

namespace Kalburator::Conflict {

class ConflictHandler;

/**
 * @brief Per-backend ConflictHandler registry with a default fallback.
 *
 * Supports the two-tier dispatch model from Audit 3
 * (docs/phase0/04a-followups.md): when the library owns the sync
 * session, the coordinator consults this registry to pick the handler
 * for a given backend. When an external orchestrator (e.g. Wild Palms'
 * SyncConduitBase) owns the session, it queries the registry to
 * populate its SyncContext before calling the handler directly.
 *
 * The registry does not take ownership of handler instances — handlers
 * outlive the registry by convention. Typical lifetime: handlers
 * created once at app-shell init, registered with one backend id each,
 * destroyed at app shutdown.
 */
class ConflictHandlerRegistry
{
public:
    ConflictHandlerRegistry() = default;
    ~ConflictHandlerRegistry() = default;

    /// Register a handler for a specific backend id.
    void registerHandler(const QString &backendId, ConflictHandler *handler);

    /// Remove the handler for a backend id. Safe if absent.
    void unregisterHandler(const QString &backendId);

    /// Set the fallback handler used when no per-backend handler is
    /// registered. Pass nullptr to clear.
    void setDefaultHandler(ConflictHandler *handler);

    /// Lookup. Returns the per-backend handler if registered, else the
    /// default, else nullptr.
    ConflictHandler *handlerFor(const QString &backendId) const;

    /// Introspection.
    bool hasHandler(const QString &backendId) const;
    ConflictHandler *defaultHandler() const { return m_default; }

private:
    QHash<QString, ConflictHandler *> m_handlers;
    ConflictHandler *m_default = nullptr;
};

} // namespace Kalburator::Conflict

#endif // KALBURATOR_CONFLICT_CONFLICTHANDLERREGISTRY_H
