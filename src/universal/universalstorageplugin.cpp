#include "universalstorageplugin.h"
#include "backendcontribution.h"
#include "iprovider.h"

namespace Kalburator {

namespace {

class RawFilesBC : public Sync::BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("raw-files"); }
    QString displayName() const override { return QStringLiteral("Raw Files"); }
    QList<Shape::Shape> nativeShapes() const override {
        return {{ Shape::DomainId{QStringLiteral("blob")}, Shape::EncodingId{QStringLiteral("raw")} }};
    }
    std::unique_ptr<Sync::IProvider> createProvider(QObject *) const override {
        return nullptr;  // path-based backend requires configuration; wired in K.8
    }
};

class GenericSqliteBC : public Sync::BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("generic-sqlite"); }
    QString displayName() const override { return QStringLiteral("Generic SQLite"); }
    QList<Shape::Shape> nativeShapes() const override {
        return {{ Shape::DomainId{QStringLiteral("blob")}, Shape::EncodingId{QStringLiteral("raw")} }};
    }
    std::unique_ptr<Sync::IProvider> createProvider(QObject *) const override {
        return nullptr;  // path-based backend requires configuration; wired in K.8
    }
};

} // anonymous namespace

QList<std::shared_ptr<Sync::BackendContribution>>
UniversalStoragePlugin::backendContributions() const {
    return { std::make_shared<RawFilesBC>(), std::make_shared<GenericSqliteBC>() };
}

} // namespace Kalburator
