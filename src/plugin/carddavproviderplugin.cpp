#include <kalburator/plugin/carddavproviderplugin.h>
#include <kalburator/sync/carddavbackendcontribution.h>

namespace Kalburator {

QList<std::shared_ptr<Sync::BackendContribution>>
CardDavProviderPlugin::backendContributions() const
{
    return { std::make_shared<Sync::CardDavBackendContribution>() };
}

} // namespace Kalburator
