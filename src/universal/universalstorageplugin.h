#ifndef KALBURATOR_UNIVERSAL_STORAGEPLUGIN_H
#define KALBURATOR_UNIVERSAL_STORAGEPLUGIN_H

#include "plugin.h"

namespace Kalburator {

class UniversalStoragePlugin : public Plugin {
public:
    QList<std::shared_ptr<Sync::BackendContribution>> backendContributions() const override;
};

} // namespace Kalburator

#endif
