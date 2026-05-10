#include "blobdomainplugin.h"

#include "domainregistry.h"
#include "recorddiffer.h"
#include "recordmerger.h"
#include "propertycatalogue.h"
#include "transformationregistry.h"
#include "conflictpolicy.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::PropertyCatalogue;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::PropertyKind;
using Kalburator::Shape::CanonicalRecord;
using Kalburator::Shape::RecordDiffer;
using Kalburator::Shape::RecordMerger;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::TransformationEdge;
using Kalburator::Shape::TransformationRegistry;
using Kalburator::Shape::IdentityStage;

namespace {

// --- Hash-equality differ for the blob domain ---
// Two blob records are equal iff their payload bytes are identical.
class RecordDifferBlob final : public RecordDiffer {
public:
    QSet<PropertyId> diff(const CanonicalRecord& source,
                           const CanonicalRecord& baseline) const override
    {
        return (source.data == baseline.data) ? QSet<PropertyId>{} : QSet<PropertyId>{PropertyId{"data"}};
    }

    bool equal(const CanonicalRecord& a, const CanonicalRecord& b) const override
    {
        return a.data == b.data;
    }
};

// --- Whole-record-replace merger for the blob domain ---
class RecordMergerBlob final : public RecordMerger {
public:
    CanonicalRecord merge(
        const CanonicalRecord& source,
        const CanonicalRecord& target,
        const CanonicalRecord& baseline,
        const Kalburator::Sync::QSyncCore::ConflictPolicy& policy) const override
    {
        const bool sChanged = (source.data != baseline.data);
        const bool tChanged = (target.data != baseline.data);
        if (!sChanged && !tChanged)
            return baseline;
        if (sChanged && !tChanged)
            return source;
        if (!sChanged && tChanged)
            return target;
        // Both changed — conflict; resolve by policy
        using AR = Kalburator::Sync::QSyncCore::AutoResolveStrategy;
        if (policy.autoResolve == AR::TargetAlwaysWins)
            return target;
        return source; // SourceAlwaysWins or any other auto-resolve strategy
    }
};

PropertyCatalogue makeBlobCatalogue()
{
    PropertyCatalogue cat;
    cat.addProperty({ PropertyId{"id"},   PropertyKind::String, QStringLiteral("ID"),   false });
    cat.addProperty({ PropertyId{"data"}, PropertyKind::Bytes, QStringLiteral("Data") });
    return cat;
}

} // namespace

namespace Kalburator::Blob {

DomainId KalburatorDomainBlob::domain() const
{
    return DomainId{"blob"};
}

Kalburator::Shape::Shape KalburatorDomainBlob::canonicalShape() const
{
    return { DomainId{"blob"}, EncodingId{"raw"} };
}

QList<Kalburator::Shape::Shape> KalburatorDomainBlob::peerShapes() const
{
    return {};
}

PropertyCatalogue KalburatorDomainBlob::canonicalCatalogue() const
{
    return makeBlobCatalogue();
}

PropertyCatalogue KalburatorDomainBlob::catalogueFor(const Kalburator::Shape::Shape& s) const
{
    if (s == canonicalShape())
        return makeBlobCatalogue();
    return {};
}

std::unique_ptr<RecordDiffer> KalburatorDomainBlob::createCanonicalDiffer() const
{
    return std::make_unique<RecordDifferBlob>();
}

std::unique_ptr<RecordMerger> KalburatorDomainBlob::createCanonicalMerger() const
{
    return std::make_unique<RecordMergerBlob>();
}

void KalburatorDomainBlob::registerEdges(TransformationRegistry& registry)
{
    const auto canonical = canonicalShape();
    registry.registerShape(canonical, canonicalCatalogue());
    registry.declareCanonical(domain(), canonical);
    registry.registerEdge(TransformationEdge{
        canonical, canonical,
        LossProfile{},
        std::make_shared<IdentityStage>()
    });
}

int KalburatorDomainBlob::richnessRank(const Kalburator::Shape::Shape& s) const
{
    return s == canonicalShape() ? 10 : 0;
}

} // namespace Kalburator::Blob

namespace {

struct BlobPluginRegistrar {
    BlobPluginRegistrar() {
        Kalburator::Shape::DomainRegistry::instance().registerDomain(
            std::make_shared<Kalburator::Blob::KalburatorDomainBlob>());
    }
};

static BlobPluginRegistrar s_blobPluginRegistrar;

} // namespace
