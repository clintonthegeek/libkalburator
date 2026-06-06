#include "blobdomaindefinition.h"

#include "recorddiffer.h"
#include "recordmerger.h"
#include "propertycatalogue.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::PropertyCatalogue;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::PropertyKind;
using Kalburator::Shape::CanonicalRecord;
using Kalburator::Shape::RecordDiffer;
using Kalburator::Shape::RecordMerger;

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
        Kalburator::Shape::AutoResolveStrategy strategy) const override
    {
        const bool sChanged = (source.data != baseline.data);
        const bool tChanged = (target.data != baseline.data);
        if (!sChanged && !tChanged)
            return baseline;
        if (sChanged && !tChanged)
            return source;
        if (!sChanged && tChanged)
            return target;
        // Both changed — conflict; resolve by strategy
        using AR = Kalburator::Shape::AutoResolveStrategy;
        if (strategy == AR::TargetAlwaysWins)
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

Shape::DomainId BlobDomainDefinition::domain() const
{
    return DomainId{"blob"};
}

Shape::Shape BlobDomainDefinition::canonicalShape() const
{
    return { DomainId{"blob"}, EncodingId{"raw"} };
}

Shape::PropertyCatalogue BlobDomainDefinition::canonicalCatalogue() const
{
    return makeBlobCatalogue();
}

std::unique_ptr<Shape::RecordDiffer> BlobDomainDefinition::createCanonicalDiffer() const
{
    return std::make_unique<RecordDifferBlob>();
}

std::unique_ptr<Shape::RecordMerger> BlobDomainDefinition::createCanonicalMerger() const
{
    return std::make_unique<RecordMergerBlob>();
}

int BlobDomainDefinition::richnessRank(const Shape::Shape& s) const
{
    return s == canonicalShape() ? 10 : 0;
}

} // namespace Kalburator::Blob
