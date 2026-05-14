#ifndef KALBURATOR_PLUGIN_CARDDAVPROVIDERPLUGIN_H
#define KALBURATOR_PLUGIN_CARDDAVPROVIDERPLUGIN_H

#include "plugin.h"

namespace Kalburator {

class CardDavProviderPlugin : public Plugin {
public:
    QList<std::shared_ptr<Sync::BackendContribution>>
        backendContributions() const override;
};

} // namespace Kalburator

#endif
