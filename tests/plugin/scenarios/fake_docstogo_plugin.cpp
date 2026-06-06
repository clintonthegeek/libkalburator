#include "fake_docstogo_plugin.h"
#include "recorddiffer.h"
#include "recordmerger.h"
#include "lossprofile.h"
#include "manifest.h"
#include "transformationedge.h"

using namespace Kalburator;
using namespace Kalburator::Shape;

namespace KalburatorTests {
namespace {

// Shared trivial differ/merger:
class TrivialDiffer : public Shape::RecordDiffer {
public:
    QSet<Shape::PropertyId> diff(const Shape::CanonicalRecord&, const Shape::CanonicalRecord&) const override { return {}; }
    bool equal(const Shape::CanonicalRecord&, const Shape::CanonicalRecord&) const override { return true; }
};

class TrivialMerger : public Shape::RecordMerger {
public:
    Shape::CanonicalRecord merge(const Shape::CanonicalRecord &s, const Shape::CanonicalRecord&, const Shape::CanonicalRecord&,
                          Shape::AutoResolveStrategy) const override { return s; }
};

// Reverse-bytes stage — any deterministic non-trivial transform.
class ReverseBytesStage : public Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray &src) const override {
        QByteArray out = src;
        std::reverse(out.begin(), out.end());
        return out;
    }
};

class OfficeDD : public Shape::DomainDefinition {
public:
    Shape::DomainId domain() const override { return Shape::DomainId{QStringLiteral("office.document")}; }
    Shape::Shape canonicalShape() const override { return Shape::Shape{ domain(), Shape::EncodingId{QStringLiteral("canonical-v1")} }; }
    Shape::PropertyCatalogue canonicalCatalogue() const override { return {}; }
    std::unique_ptr<Shape::RecordDiffer> createCanonicalDiffer() const override { return std::make_unique<TrivialDiffer>(); }
    std::unique_ptr<Shape::RecordMerger> createCanonicalMerger() const override { return std::make_unique<TrivialMerger>(); }
    int richnessRank(const Shape::Shape &s) const override {
        if (s == canonicalShape()) return 10;
        return 0;
    }
};

class DocsToGoSC : public Shape::ShapeContribution {
public:
    Shape::DomainId targetDomain() const override { return Shape::DomainId{QStringLiteral("office.document")}; }

    QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>> peerShapes() const override {
        return { { Shape::Shape{ Shape::DomainId{QStringLiteral("office.document")}, Shape::EncodingId{QStringLiteral("pdb-word")} }, {} } };
    }

    QList<Shape::TransformationEdge> edges() const override {
        const Shape::Shape canonical{ Shape::DomainId{QStringLiteral("office.document")}, Shape::EncodingId{QStringLiteral("canonical-v1")} };
        const Shape::Shape pdbWord{ Shape::DomainId{QStringLiteral("office.document")}, Shape::EncodingId{QStringLiteral("pdb-word")} };
        return {
            // canonical → canonical (identity)
            Shape::TransformationEdge{
                canonical, canonical,
                Shape::LossProfile{},
                std::make_shared<Shape::IdentityStage>()
            },
            // pdb-word → canonical (reverse bytes)
            Shape::TransformationEdge{
                pdbWord, canonical,
                Shape::LossProfile{},
                std::make_shared<ReverseBytesStage>()
            },
            // canonical → pdb-word (reverse bytes)
            Shape::TransformationEdge{
                canonical, pdbWord,
                Shape::LossProfile{},
                std::make_shared<ReverseBytesStage>()
            }
        };
    }
};

} // namespace

QList<std::shared_ptr<Shape::DomainDefinition>> FakeDocsToGoPlugin::domainDefinitions() const {
    return {std::make_shared<OfficeDD>()};
}

QList<std::shared_ptr<Shape::ShapeContribution>> FakeDocsToGoPlugin::shapeContributions() const {
    return {std::make_shared<DocsToGoSC>()};
}

PluginManifest fakeDocsToGoManifest() {
    PluginManifest m;
    m.id = QStringLiteral("docstogo");
    m.definesDomains = {QStringLiteral("office.document")};
    return m;
}

} // namespace KalburatorTests
