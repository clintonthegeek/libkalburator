#include "memodomainplugin.h"

#include "domainregistry.h"
#include "memoproperties.h"
#include "textdiffer.h"
#include "textmerger.h"
#include "transformationregistry.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::PropertyCatalogue;
using Kalburator::Shape::RecordDiffer;
using Kalburator::Shape::RecordMerger;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::TransformationEdge;
using Kalburator::Shape::TransformationRegistry;
using Kalburator::Shape::IdentityStage;

namespace Kalburator::Memo {

DomainId KalburatorDomainMemo::domain() const
{
    return DomainId{"memo"};
}

Kalburator::Shape::Shape KalburatorDomainMemo::canonicalShape() const
{
    return { DomainId{"memo"}, EncodingId{"text"} };
}

QList<Kalburator::Shape::Shape> KalburatorDomainMemo::peerShapes() const
{
    return {};
}

PropertyCatalogue KalburatorDomainMemo::canonicalCatalogue() const
{
    return makeMemoCatalogue();
}

PropertyCatalogue KalburatorDomainMemo::catalogueFor(
    const Kalburator::Shape::Shape &s) const
{
    if (s == canonicalShape())
        return makeMemoCatalogue();
    return {};
}

std::unique_ptr<RecordDiffer> KalburatorDomainMemo::createCanonicalDiffer() const
{
    return std::make_unique<TextDiffer>();
}

std::unique_ptr<RecordMerger> KalburatorDomainMemo::createCanonicalMerger() const
{
    return std::make_unique<TextMerger>();
}

void KalburatorDomainMemo::registerEdges(TransformationRegistry &registry)
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

int KalburatorDomainMemo::richnessRank(
    const Kalburator::Shape::Shape &s) const
{
    return s == canonicalShape() ? 10 : 0;
}

} // namespace Kalburator::Memo

namespace {

struct MemoPluginRegistrar {
    MemoPluginRegistrar() {
        Kalburator::Shape::DomainRegistry::instance().registerDomain(
            std::make_shared<Kalburator::Memo::KalburatorDomainMemo>());
    }
};

static MemoPluginRegistrar s_memoPluginRegistrar;

} // namespace
