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
    { return std::make_unique<MultiProtocolDavProvider>(parent); }
};

} // namespace Kalburator::Sync

#endif
