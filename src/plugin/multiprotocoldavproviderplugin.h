#ifndef KALBURATOR_PLUGIN_MULTIPROTOCOLDAVPROVIDERPLUGIN_H
#define KALBURATOR_PLUGIN_MULTIPROTOCOLDAVPROVIDERPLUGIN_H

#include "plugin.h"

namespace Kalburator {

class MultiProtocolDavProviderPlugin : public Plugin {
public:
    QList<std::shared_ptr<Sync::BackendContribution>>
        backendContributions() const override;
};

} // namespace Kalburator

#endif
