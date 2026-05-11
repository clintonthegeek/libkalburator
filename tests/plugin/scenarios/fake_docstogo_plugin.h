#pragma once
#include "plugin.h"
#include "domaindefinition.h"
#include "shapecontribution.h"
#include "transformationedge.h"
#include "manifest.h"
#include <memory>

namespace KalburatorTests {

/// FakeDocsToGoPlugin: defines the "office.document" domain.
/// Provides:
///  - DomainDefinition for {office.document, canonical-v1}
///  - ShapeContribution adding {office.document, pdb-word} peer
///    with edges pdb-word→canonical and canonical→pdb-word
/// No BackendContribution.
class FakeDocsToGoPlugin : public Kalburator::Plugin {
public:
    QList<std::shared_ptr<Kalburator::Shape::DomainDefinition>>  domainDefinitions()  const override;
    QList<std::shared_ptr<Kalburator::Shape::ShapeContribution>> shapeContributions() const override;
    QList<std::shared_ptr<Kalburator::Shape::DomainOperations>>  domainOperations()   const override { return {}; }
    QList<std::shared_ptr<Kalburator::Sync::BackendContribution>> backendContributions() const override { return {}; }
};

Kalburator::PluginManifest fakeDocsToGoManifest();

} // namespace KalburatorTests
