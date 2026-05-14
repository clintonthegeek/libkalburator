#ifndef KALBURATOR_SYNC_CALDAVBACKENDCONTRIBUTION_H
#define KALBURATOR_SYNC_CALDAVBACKENDCONTRIBUTION_H

#include "backendcontribution.h"
#include "caldavprovider.h"
#include "iprovider.h"

namespace Kalburator::Sync {

class CalDavBackendContribution : public BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("caldav"); }
    QList<Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<IProvider> createProvider(QObject *parent) const override
    {
        return std::make_unique<CalDavProvider>(parent);
    }
};

} // namespace Kalburator::Sync

#endif
