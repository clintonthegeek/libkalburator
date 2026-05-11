#include "fake_msoffice_plugin.h"
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

class MsOfficeSC : public Shape::ShapeContribution {
public:
    Shape::DomainId targetDomain() const override { return Shape::DomainId{QStringLiteral("office.document")}; }

    QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>> peerShapes() const override {
        return { { Shape::Shape{ Shape::DomainId{QStringLiteral("office.document")}, Shape::EncodingId{QStringLiteral("docx")} }, {} } };
    }

    QList<Shape::TransformationEdge> edges() const override {
        const Shape::Shape canonical{ Shape::DomainId{QStringLiteral("office.document")}, Shape::EncodingId{QStringLiteral("canonical-v1")} };
        const Shape::Shape docx{ Shape::DomainId{QStringLiteral("office.document")}, Shape::EncodingId{QStringLiteral("docx")} };
        return {
            // docx → canonical (reverse bytes)
            Shape::TransformationEdge{
                docx, canonical,
                Shape::LossProfile{},
                std::make_shared<ReverseBytesStage>()
            },
            // canonical → docx (reverse bytes)
            Shape::TransformationEdge{
                canonical, docx,
                Shape::LossProfile{},
                std::make_shared<ReverseBytesStage>()
            }
        };
    }
};

class MsOfficeBC : public Sync::BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("office-docx"); }
    QList<Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<Sync::IProvider> createProvider(QObject*) const override { return nullptr; }
};

} // namespace

QList<std::shared_ptr<Shape::ShapeContribution>> FakeMsOfficePlugin::shapeContributions() const {
    return {std::make_shared<MsOfficeSC>()};
}

QList<std::shared_ptr<Sync::BackendContribution>> FakeMsOfficePlugin::backendContributions() const {
    return {std::make_shared<MsOfficeBC>()};
}

PluginManifest fakeMsOfficeManifest() {
    PluginManifest m;
    m.id = QStringLiteral("msoffice");
    m.requiresDomains = {QStringLiteral("office.document")};
    return m;
}

} // namespace KalburatorTests
