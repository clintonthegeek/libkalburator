#ifndef KALBURATOR_SYNC_CARDDAVBACKENDCONTRIBUTION_H
#define KALBURATOR_SYNC_CARDDAVBACKENDCONTRIBUTION_H

#include <kalburator/sync/backendcontribution.h>
#include <kalburator/sync/carddavprovider.h>
#include <kalburator/sync/iprovider.h>

namespace Kalburator::Sync {

class CardDavBackendContribution : public BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("carddav"); }
    QString displayName() const override { return QStringLiteral("CardDAV Contacts"); }
    QList<Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<IProvider> createProvider(QObject *parent) const override
    {
        return std::make_unique<CardDavProvider>(parent);
    }
};

} // namespace Kalburator::Sync

#endif
