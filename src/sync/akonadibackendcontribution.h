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
    QString displayName() const override { return QStringLiteral("Akonadi"); }
    QList<Shape::Shape> nativeShapes() const override;
    std::unique_ptr<IProvider> createProvider(QObject *parent) const override
    {
        // calendarsOnly=false preserves the pre-mode behavior for
        // registry-created providers; passing `parent` alone would bind the
        // QObject* to the bool parameter and drop the parent (2026-06-10
        // audit). Per-account mode selection is a pending design decision.
        return std::make_unique<AkonadiProvider>(false, parent);
    }
};

} // namespace Kalburator::Sync

#endif // HAVE_AKONADI
#endif // KALBURATOR_SYNC_AKONADIBACKENDCONTRIBUTION_H
