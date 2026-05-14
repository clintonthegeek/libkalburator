#ifndef KALBURATOR_PLUGIN_CALDAVPROVIDERPLUGIN_H
#define KALBURATOR_PLUGIN_CALDAVPROVIDERPLUGIN_H

#include "plugin.h"

namespace Kalburator {

class CalDavProviderPlugin : public Plugin {
public:
    QList<std::shared_ptr<Sync::BackendContribution>>
        backendContributions() const override;
};

} // namespace Kalburator

#endif
