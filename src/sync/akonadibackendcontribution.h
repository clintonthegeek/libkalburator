#ifndef KALBURATOR_SYNC_AKONADIBACKENDCONTRIBUTION_H
#define KALBURATOR_SYNC_AKONADIBACKENDCONTRIBUTION_H

#ifdef HAVE_AKONADI

#include "backendcontribution.h"
#include "akonadiprovider.h"
#include "iprovider.h"

namespace Kalburator::Sync {

class AkonadiBackendContribution : public BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("akonadi"); }
    QList<Shape::Shape> nativeShapes() const override;
    std::unique_ptr<IProvider> createProvider(QObject *parent) const override
    {
        return std::make_unique<AkonadiProvider>(parent);
    }
};

} // namespace Kalburator::Sync

#endif // HAVE_AKONADI
#endif // KALBURATOR_SYNC_AKONADIBACKENDCONTRIBUTION_H
