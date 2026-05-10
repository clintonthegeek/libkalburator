#pragma once

#include <QList>
#include <QStringList>
#include <QVariantMap>
#include <memory>

#include "propertycatalogue.h"
#include "shape.h"

namespace Kalburator::Sync { class SyncBackend; }

namespace Kalburator::Shape {

class RecordDiffer;     // defined in recorddiffer.h (Task 11)
class RecordMerger;     // defined in recordmerger.h (Task 11)
class RecordWriter;     // defined in recordwriter.h
class TransformationRegistry;

/// Domain plugins own the canonical shape for a domain plus the
/// edges that connect peer shapes to the canonical hub. Stock
/// plugins (calendar, contacts, memo, todo) are registered with
/// the DomainRegistry at static-init time; user code can register
/// additional plugins before using the engine.
class DomainPlugin {
public:
    virtual ~DomainPlugin() = default;

    virtual DomainId domain() const = 0;
    virtual Shape canonicalShape() const = 0;

    /// Other shapes this domain plugin knows about. Edges to/from
    /// the canonical shape are registered for each.
    virtual QList<Shape> peerShapes() const = 0;

    virtual PropertyCatalogue canonicalCatalogue() const = 0;
    virtual PropertyCatalogue catalogueFor(const Shape&) const = 0;

    /// Differ / merger over the canonical shape. The engine uses
    /// these on records that have been promoted to canonical.
    virtual std::unique_ptr<RecordDiffer> createCanonicalDiffer() const = 0;
    virtual std::unique_ptr<RecordMerger> createCanonicalMerger() const = 0;

    /// Populate the TransformationRegistry with this plugin's
    /// shapes, canonical declaration, and edges. Called once per
    /// process by DomainRegistry::initialize().
    virtual void registerEdges(TransformationRegistry& registry) = 0;

    /// Intra-domain richness rank. Domains with multiple peer shapes
    /// declare a partial order so first-sync `RicherSideWins` can
    /// pick the more expressive side. Higher = richer.
    virtual int richnessRank(const Shape&) const = 0;

    /// Writer hook. Default: wraps the backend's SyncBackend surface
    /// via DefaultBlobWriter. Calendar plugin overrides to drive its
    /// SyncTransaction machinery.
    virtual std::unique_ptr<RecordWriter> createWriter(
        Kalburator::Sync::SyncBackend *backend) const;

    /// Collection-level metadata (calendar color/description, etc.).
    /// Default: empty map. Calendar plugin overrides.
    virtual QVariantMap collectionProperties(
        Kalburator::Sync::SyncBackend *backend,
        const QString &collectionId) const;

    /// Apply collection-level metadata changes. Default: no-op.
    virtual void applyCollectionProperties(
        Kalburator::Sync::SyncBackend *backend,
        const QString &collectionId,
        const QVariantMap &props) const;

    /// Property keys whose collection-level snapshots the engine should
    /// persist via Storage::BaselineStore::setCollectionBaseline. The
    /// engine queries collectionProperties() at sync time and stores
    /// the subset corresponding to these keys. Default: empty list
    /// (no property baselines kept). Calendar plugin overrides to
    /// declare {"color", "description"}.
    virtual QStringList baselineProperties() const { return {}; }
};

}  // namespace Kalburator::Shape
