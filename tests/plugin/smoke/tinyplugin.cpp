// tests/plugin/smoke/tinyplugin.cpp
#include "tinyplugin.h"
#include "backendcontribution.h"
#include "iprovider.h"

namespace {
class TinyBC : public Kalburator::Sync::BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("tiny"); }
    QList<Kalburator::Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<Kalburator::Sync::IProvider> createProvider(QObject*) const override { return nullptr; }
};
} // anonymous namespace

QList<std::shared_ptr<Kalburator::Sync::BackendContribution>>
TinyPlugin::backendContributions() const {
    return { std::make_shared<TinyBC>() };
}
