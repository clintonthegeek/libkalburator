#ifndef KALBURATOR_PLUGIN_MANIFEST_H
#define KALBURATOR_PLUGIN_MANIFEST_H

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <optional>

namespace Kalburator {

struct PluginManifest {
    QString id;
    QString version;
    QString displayName;
    QString kalburatorPluginVersion;
    QStringList definesDomains;
    QStringList requiresDomains;

    static std::optional<PluginManifest> fromJson(const QJsonObject &obj,
                                                   QString *errorOut = nullptr);
};

} // namespace Kalburator

#endif
