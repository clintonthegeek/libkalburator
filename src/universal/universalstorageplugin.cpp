#include <kalburator/universal/universalstorageplugin.h>
#include <kalburator/sync/backendcontribution.h>
#include <kalburator/sync/iprovider.h>

namespace Kalburator {

QList<std::shared_ptr<Sync::BackendContribution>>
UniversalStoragePlugin::backendContributions() const {
    // These backends require a configured filesystem/database path. Until a
    // provisioned provider contract exists, do not advertise null factories.
    return {};
}

} // namespace Kalburator
