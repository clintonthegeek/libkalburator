#pragma once
#include <QList>
#include <memory>
#include "plugin.h"
#include "domaindefinition.h"
#include "shapecontribution.h"
#include "domainoperations.h"
#include "backendcontribution.h"

namespace KalburatorTests {

class FakePlugin : public Kalburator::Plugin {
public:
    QList<std::shared_ptr<Kalburator::Shape::DomainDefinition>> dds;
    QList<std::shared_ptr<Kalburator::Shape::ShapeContribution>> scs;
    QList<std::shared_ptr<Kalburator::Shape::DomainOperations>> dos;
    QList<std::shared_ptr<Kalburator::Sync::BackendContribution>> bcs;

    QList<std::shared_ptr<Kalburator::Shape::DomainDefinition>> domainDefinitions() const override { return dds; }
    QList<std::shared_ptr<Kalburator::Shape::ShapeContribution>> shapeContributions() const override { return scs; }
    QList<std::shared_ptr<Kalburator::Shape::DomainOperations>> domainOperations() const override { return dos; }
    QList<std::shared_ptr<Kalburator::Sync::BackendContribution>> backendContributions() const override { return bcs; }
};

std::shared_ptr<Kalburator::Shape::DomainDefinition> makeTrivialDD(const QString &domain);
std::shared_ptr<Kalburator::Shape::ShapeContribution> makeTrivialSC(const QString &targetDomain);
std::shared_ptr<Kalburator::Sync::BackendContribution> makeTrivialBC(const QString &type);

} // namespace KalburatorTests
