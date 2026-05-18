#include "fake_plugin.h"
#include "recorddiffer.h"
#include "recordmerger.h"
#include "transformationedge.h"
#include "conflictpolicy.h"
#include "iprovider.h"

using namespace Kalburator;

namespace {

class TrivialDiffer : public Shape::RecordDiffer {
public:
    QSet<Shape::PropertyId> diff(const Shape::CanonicalRecord&, const Shape::CanonicalRecord&) const override { return {}; }
    bool equal(const Shape::CanonicalRecord&, const Shape::CanonicalRecord&) const override { return true; }
};

class TrivialMerger : public Shape::RecordMerger {
public:
    Shape::CanonicalRecord merge(const Shape::CanonicalRecord &s,
                                  const Shape::CanonicalRecord&, const Shape::CanonicalRecord&,
                                  const Kalburator::Conflict::ConflictPolicy&) const override { return s; }
};

class TrivialDD : public Shape::DomainDefinition {
public:
    explicit TrivialDD(QString d) : m_d(std::move(d)) {}
    Shape::DomainId domain() const override { return Shape::DomainId{m_d}; }
    Shape::Shape canonicalShape() const override { return { domain(), Shape::EncodingId{"canon"} }; }
    Shape::PropertyCatalogue canonicalCatalogue() const override { return {}; }
    std::unique_ptr<Shape::RecordDiffer> createCanonicalDiffer() const override { return std::make_unique<TrivialDiffer>(); }
    std::unique_ptr<Shape::RecordMerger> createCanonicalMerger() const override { return std::make_unique<TrivialMerger>(); }
    int richnessRank(const Shape::Shape&) const override { return 0; }
private:
    QString m_d;
};

class TrivialSC : public Shape::ShapeContribution {
public:
    explicit TrivialSC(QString d) : m_d(std::move(d)) {}
    Shape::DomainId targetDomain() const override { return Shape::DomainId{m_d}; }
    QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>> peerShapes() const override { return {}; }
    QList<Shape::TransformationEdge> edges() const override { return {}; }
private:
    QString m_d;
};

class TrivialBC : public Sync::BackendContribution {
public:
    explicit TrivialBC(QString t) : m_t(std::move(t)) {}
    QString backendType() const override { return m_t; }
    QString displayName() const override { return QStringLiteral("Trivial"); }
    QList<Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<Sync::IProvider> createProvider(QObject*) const override { return nullptr; }
private:
    QString m_t;
};

} // anonymous namespace

namespace KalburatorTests {
std::shared_ptr<Shape::DomainDefinition> makeTrivialDD(const QString &d) { return std::make_shared<TrivialDD>(d); }
std::shared_ptr<Shape::ShapeContribution> makeTrivialSC(const QString &d) { return std::make_shared<TrivialSC>(d); }
std::shared_ptr<Sync::BackendContribution> makeTrivialBC(const QString &t) { return std::make_shared<TrivialBC>(t); }
} // namespace KalburatorTests
