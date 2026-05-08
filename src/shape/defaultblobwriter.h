#pragma once

#include "irecordwriter.h"

namespace Kalburator::Sync { class IBlobBackend; }

namespace Kalburator::Shape {

class DefaultBlobWriter : public IRecordWriter {
public:
    explicit DefaultBlobWriter(Kalburator::Sync::IBlobBackend *backend)
        : m_backend(backend) {}

    bool apply(
        const QString &collectionId,
        const QList<Kalburator::Sync::BackendRecord> &creates,
        const QList<Kalburator::Sync::BackendRecord> &updates,
        const QStringList &deletes) override;

private:
    Kalburator::Sync::IBlobBackend *m_backend;
};

} // namespace Kalburator::Shape
