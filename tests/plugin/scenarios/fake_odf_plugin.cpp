#include "fake_odf_plugin.h"
#include "lossprofile.h"
#include "manifest.h"
#include "transformationedge.h"
#include "iprovider.h"

using namespace Kalburator;

namespace KalburatorTests {
namespace {

// Reverse-bytes stage
class ReverseBytesStage : public Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray &src) const override {
        QByteArray out = src;
        std::reverse(out.begin(), out.end());
        return out;
    }
};

class OdfSC : public Shape::ShapeContribution {
public:
    Shape::DomainId targetDomain() const override { return Shape::DomainId{QStringLiteral("office.document")}; }

    QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>> peerShapes() const override {
        return { { Shape::Shape{ Shape::DomainId{QStringLiteral("office.document")}, Shape::EncodingId{QStringLiteral("odt")} }, {} } };
    }

    QList<Shape::TransformationEdge> edges() const override {
        const Shape::Shape canonical{ Shape::DomainId{QStringLiteral("office.document")}, Shape::EncodingId{QStringLiteral("canonical-v1")} };
        const Shape::Shape odt{ Shape::DomainId{QStringLiteral("office.document")}, Shape::EncodingId{QStringLiteral("odt")} };
        return {
            // odt → canonical (reverse bytes)
            Shape::TransformationEdge{
                odt, canonical,
                Shape::LossProfile{},
                std::make_shared<ReverseBytesStage>()
            },
            // canonical → odt (reverse bytes)
            Shape::TransformationEdge{
                canonical, odt,
                Shape::LossProfile{},
                std::make_shared<ReverseBytesStage>()
            }
        };
    }
};

class OdfBC : public Sync::BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("office-odt"); }
    QList<Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<Sync::IProvider> createProvider(QObject*) const override { return nullptr; }
};

} // namespace

QList<std::shared_ptr<Shape::ShapeContribution>> FakeOdfPlugin::shapeContributions() const {
    return {std::make_shared<OdfSC>()};
}

QList<std::shared_ptr<Sync::BackendContribution>> FakeOdfPlugin::backendContributions() const {
    return {std::make_shared<OdfBC>()};
}

PluginManifest fakeOdfManifest() {
    PluginManifest m;
    m.id = QStringLiteral("odf");
    m.requiresDomains = {QStringLiteral("office.document")};
    return m;
}

} // namespace KalburatorTests
