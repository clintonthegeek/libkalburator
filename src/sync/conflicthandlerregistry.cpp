#include "conflicthandlerregistry.h"

namespace Kalburator::Sync::QSyncCore {

void ConflictHandlerRegistry::registerHandler(const QString &backendId,
                                              ConflictHandler *handler)
{
    if (handler) {
        m_handlers.insert(backendId, handler);
    } else {
        m_handlers.remove(backendId);
    }
}

void ConflictHandlerRegistry::unregisterHandler(const QString &backendId)
{
    m_handlers.remove(backendId);
}

void ConflictHandlerRegistry::setDefaultHandler(ConflictHandler *handler)
{
    m_default = handler;
}

ConflictHandler *ConflictHandlerRegistry::handlerFor(const QString &backendId) const
{
    auto it = m_handlers.constFind(backendId);
    if (it != m_handlers.constEnd()) {
        return it.value();
    }
    return m_default;
}

bool ConflictHandlerRegistry::hasHandler(const QString &backendId) const
{
    return m_handlers.contains(backendId);
}

} // namespace Kalburator::Sync::QSyncCore
