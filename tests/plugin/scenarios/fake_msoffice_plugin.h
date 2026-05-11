#pragma once
#include "plugin.h"
#include "shapecontribution.h"
#include "backendcontribution.h"
#include "manifest.h"
#include <memory>

namespace KalburatorTests {

/// FakeMsOfficePlugin: contributes to the "office.document" domain.
/// Provides:
///  - ShapeContribution adding {office.document, docx} peer
///    with edges docx↔canonical
///  - BackendContribution with backendType = "office-docx"
/// Does NOT define the domain (DocsToGo does).
class FakeMsOfficePlugin : public Kalburator::Plugin {
public:
    QList<std::shared_ptr<Kalburator::Shape::DomainDefinition>>  domainDefinitions()  const override { return {}; }
    QList<std::shared_ptr<Kalburator::Shape::ShapeContribution>> shapeContributions() const override;
    QList<std::shared_ptr<Kalburator::Shape::DomainOperations>>  domainOperations()   const override { return {}; }
    QList<std::shared_ptr<Kalburator::Sync::BackendContribution>> backendContributions() const override;
};

Kalburator::PluginManifest fakeMsOfficeManifest();

} // namespace KalburatorTests
