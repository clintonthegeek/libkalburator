#include "manifest.h"
#include <QJsonArray>
#include <QJsonValue>

namespace Kalburator {

static QStringList readStringList(const QJsonObject &o, const QString &key) {
    QStringList out;
    const auto v = o.value(key);
    if (!v.isArray()) return out;
    for (const auto &el : v.toArray()) {
        if (el.isString()) out << el.toString();
    }
    return out;
}

std::optional<PluginManifest> PluginManifest::fromJson(const QJsonObject &obj, QString *err) {
    auto fail = [&](const QString &msg) -> std::optional<PluginManifest> {
        if (err) *err = msg;
        return std::nullopt;
    };
    PluginManifest m;
    m.id = obj.value(QStringLiteral("id")).toString();
    if (m.id.isEmpty()) return fail(QStringLiteral("manifest missing required field: id"));
    m.version = obj.value(QStringLiteral("version")).toString();
    if (m.version.isEmpty()) return fail(QStringLiteral("manifest missing required field: version"));
    m.displayName = obj.value(QStringLiteral("displayName")).toString();
    m.kalburatorPluginVersion = obj.value(QStringLiteral("kalburatorPluginVersion")).toString();
    if (m.kalburatorPluginVersion.isEmpty())
        return fail(QStringLiteral("manifest missing required field: kalburatorPluginVersion"));
    m.definesDomains = readStringList(obj, QStringLiteral("definesDomains"));
    m.requiresDomains = readStringList(obj, QStringLiteral("requiresDomains"));
    return m;
}

} // namespace Kalburator
