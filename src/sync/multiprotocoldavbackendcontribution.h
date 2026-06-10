#ifndef KALBURATOR_SYNC_MULTIPROTOCOLDAVBACKENDCONTRIBUTION_H
#define KALBURATOR_SYNC_MULTIPROTOCOLDAVBACKENDCONTRIBUTION_H

#include "backendcontribution.h"
#include "multiprotocoldavprovider.h"
#include "iprovider.h"

namespace Kalburator::Sync {

class MultiProtocolDavBackendContribution : public BackendContribution {
public:
    QString backendType() const override
    { return QStringLiteral("multiproto-dav"); }
    QString displayName() const override
    { return QStringLiteral("Multi-protocol DAV"); }

    QList<Shape::Shape> nativeShapes() const override { return {}; }

    std::unique_ptr<IProvider> createProvider(QObject *parent) const override
    {
        // calendarsOnly=false: registry-created providers keep the full
        // multi-protocol surface (WildPalms binds contacts conduits to
        // multiproto accounts). Passing `parent` alone would bind the
        // QObject* to the bool parameter and drop the parent (2026-06-10
        // audit). Per-account mode selection is a pending design decision.
        return std::make_unique<MultiProtocolDavProvider>(false, parent);
    }
};

} // namespace Kalburator::Sync

#endif
