#ifndef KALBURATOR_SYNC_BACKENDCONTRIBUTION_H
#define KALBURATOR_SYNC_BACKENDCONTRIBUTION_H

#include <QList>
#include <QString>
#include <memory>
#include "shape.h"

class QObject;

namespace Kalburator::Sync {

class IProvider;

class BackendContribution {
public:
    virtual ~BackendContribution() = default;
    virtual QString backendType() const = 0;
    virtual QList<Shape::Shape> nativeShapes() const = 0;
    virtual std::unique_ptr<IProvider> createProvider(QObject *parent = nullptr) const = 0;
};

} // namespace Kalburator::Sync

#endif
