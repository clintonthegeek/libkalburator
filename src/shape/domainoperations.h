#ifndef KALBURATOR_SHAPE_DOMAINOPERATIONS_H
#define KALBURATOR_SHAPE_DOMAINOPERATIONS_H

#include <QVariantMap>
#include <memory>

#include "shape.h"

namespace Kalburator::Sync { class SyncBackendBase; }

namespace Kalburator::Shape {

class RecordWriter;

/// Read/write operations side of the plugin contract: given a concrete
/// sync backend, create a writer and expose collection-level metadata.
///
/// DomainOperations is intentionally paired with DomainDefinition —
/// DomainDefinition owns the structural/geometric description; this
/// interface owns the I/O surface. This separation lets shape-graph
/// builders and UI catalogues depend only on DomainDefinition while
/// the engine links DomainOperations only where it actually does I/O.
class DomainOperations {
public:
    virtual ~DomainOperations() = default;

    /// The domain this implementation handles.
    virtual DomainId targetDomain() const = 0;

    /// Return a RecordWriter bound to the given backend. Ownership is
    /// transferred to the caller. May return nullptr if the backend is
    /// not supported by this domain (e.g. type mismatch).
    virtual std::unique_ptr<RecordWriter> createWriter(
        Kalburator::Sync::SyncBackendBase *backend) const = 0;

    /// Collection-level metadata (calendar color/description, etc.).
    /// Default: empty map.
    virtual QVariantMap collectionProperties(
        Kalburator::Sync::SyncBackendBase * /*backend*/,
        const QString & /*collectionId*/) const
    {
        return {};
    }

    /// Apply collection-level metadata changes. Default: no-op.
    virtual void applyCollectionProperties(
        Kalburator::Sync::SyncBackendBase * /*backend*/,
        const QString & /*collectionId*/,
        const QVariantMap & /*props*/) const
    {}
};

} // namespace Kalburator::Shape

#endif // KALBURATOR_SHAPE_DOMAINOPERATIONS_H
