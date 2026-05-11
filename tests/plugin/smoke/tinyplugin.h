// tests/plugin/smoke/tinyplugin.h
#pragma once
#include <QObject>
#include "plugin.h"

class TinyPlugin : public QObject, public Kalburator::Plugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kalburator.Plugin/1.0" FILE "tinyplugin.json")
    Q_INTERFACES(Kalburator::Plugin)
public:
    QList<std::shared_ptr<Kalburator::Sync::BackendContribution>> backendContributions() const override;
};
