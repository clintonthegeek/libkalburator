#ifdef HAVE_AKONADI

#include <kalburator/plugin/akonadiproviderplugin.h>
#include <kalburator/sync/akonadibackendcontribution.h>

namespace Kalburator {

QList<std::shared_ptr<Sync::BackendContribution>>
AkonadiProviderPlugin::backendContributions() const
{
    return { std::make_shared<Sync::AkonadiBackendContribution>() };
}

} // namespace Kalburator

#endif // HAVE_AKONADI
