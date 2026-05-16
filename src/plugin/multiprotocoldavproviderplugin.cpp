#include "multiprotocoldavproviderplugin.h"
#include "../sync/multiprotocoldavbackendcontribution.h"

namespace Kalburator {

QList<std::shared_ptr<Sync::BackendContribution>>
MultiProtocolDavProviderPlugin::backendContributions() const
{
    return { std::make_shared<Sync::MultiProtocolDavBackendContribution>() };
}

} // namespace Kalburator
