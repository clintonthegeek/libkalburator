#ifndef KALBURATOR_PLUGIN_AKONADIPROVIDERPLUGIN_H
#define KALBURATOR_PLUGIN_AKONADIPROVIDERPLUGIN_H

#ifdef HAVE_AKONADI

#include "plugin.h"

namespace Kalburator {

class AkonadiProviderPlugin : public Plugin {
public:
    QList<std::shared_ptr<Sync::BackendContribution>>
        backendContributions() const override;
};

} // namespace Kalburator

#endif // HAVE_AKONADI
#endif // KALBURATOR_PLUGIN_AKONADIPROVIDERPLUGIN_H
