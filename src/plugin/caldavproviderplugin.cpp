#include <kalburator/plugin/caldavproviderplugin.h>
#include <kalburator/sync/caldavbackendcontribution.h>

namespace Kalburator {

QList<std::shared_ptr<Sync::BackendContribution>>
CalDavProviderPlugin::backendContributions() const
{
    return { std::make_shared<Sync::CalDavBackendContribution>() };
}

} // namespace Kalburator
