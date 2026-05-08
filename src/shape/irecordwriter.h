#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "backendrecord.h"

namespace Kalburator::Shape {

/// Apply create/update/delete operations against a backend's
/// target collection. One implementation per domain, returned by
/// DomainPlugin::createWriter().
class IRecordWriter {
public:
    virtual ~IRecordWriter() = default;

    /// Apply a batch of operations. Implementations may run them
    /// inside a transaction (calendar plugin does), or as
    /// independent calls (default IBlobBackend writer).
    virtual bool apply(
        const QString &collectionId,
        const QList<Kalburator::Sync::BackendRecord> &creates,
        const QList<Kalburator::Sync::BackendRecord> &updates,
        const QStringList &deletes) = 0;
};

} // namespace Kalburator::Shape
