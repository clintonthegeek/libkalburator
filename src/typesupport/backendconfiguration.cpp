#include "backendconfiguration.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QColor>
#include <QObject>

namespace Kalburator::Sync {

// ============================================================================
// PerCalendarCapabilities implementation
// ============================================================================

PerCalendarCapabilities PerCalendarCapabilities::fromJson(const QJsonObject &json)
{
    PerCalendarCapabilities caps;
    caps.supportsVEvent = json.value(QStringLiteral("supportsVEvent")).toBool(true);
    caps.supportsVTodo = json.value(QStringLiteral("supportsVTodo")).toBool(true);
    caps.supportsVJournal = json.value(QStringLiteral("supportsVJournal")).toBool(false);
    caps.writable = json.value(QStringLiteral("writable")).toBool(true);
    caps.maxResourceSize = json.value(QStringLiteral("maxResourceSize")).toInt(0);
    caps.serverDisplayName = json.value(QStringLiteral("displayName")).toString();
    caps.producerId = json.value(QStringLiteral("producerId")).toString();
    caps.supportsSyncCollection =
        json.value(QStringLiteral("supportsSyncCollection")).toBool(false);

    const QString colorStr = json.value(QStringLiteral("color")).toString();
    if (!colorStr.isEmpty()) {
        caps.serverColor = QColor(colorStr);
    }

    return caps;
}

QJsonObject PerCalendarCapabilities::toJson() const
{
    QJsonObject json;
    json[QStringLiteral("supportsVEvent")] = supportsVEvent;
    json[QStringLiteral("supportsVTodo")] = supportsVTodo;
    json[QStringLiteral("supportsVJournal")] = supportsVJournal;
    if (!writable) {
        json[QStringLiteral("writable")] = false;  // Only write if not writable (default is true)
    }
    if (maxResourceSize > 0) {
        json[QStringLiteral("maxResourceSize")] = maxResourceSize;
    }
    if (!serverDisplayName.isEmpty()) {
        json[QStringLiteral("displayName")] = serverDisplayName;
    }
    if (serverColor.isValid()) {
        json[QStringLiteral("color")] = serverColor.name(QColor::HexArgb);
    }
    if (!producerId.isEmpty()) {
        json[QStringLiteral("producerId")] = producerId;
    }
    if (supportsSyncCollection) {
        json[QStringLiteral("supportsSyncCollection")] = true;
    }
    return json;
}

// ============================================================================
// DiscoveredCapabilities implementation
// ============================================================================

DiscoveredCapabilities DiscoveredCapabilities::fromJson(const QJsonObject &json)
{
    DiscoveredCapabilities caps;

    const QString discoveredStr = json.value(QStringLiteral("discoveredAt")).toString();
    if (!discoveredStr.isEmpty()) {
        caps.discoveredAt = QDateTime::fromString(discoveredStr, Qt::ISODate);
    }

    caps.serverProduct = json.value(QStringLiteral("serverProduct")).toString();
    caps.serverVersion = json.value(QStringLiteral("serverVersion")).toString();
    caps.supportsCalendarCreation = json.value(QStringLiteral("supportsCalendarCreation")).toBool(true);
    caps.maxResourceSize = json.value(QStringLiteral("maxResourceSize")).toInt(0);

    // Parse supported component types
    const QJsonArray typesArray = json.value(QStringLiteral("supportedComponentTypes")).toArray();
    for (const QJsonValue &val : typesArray) {
        caps.supportedComponentTypes.append(val.toString());
    }

    // Parse per-calendar capabilities
    const QJsonObject calendarsObj = json.value(QStringLiteral("calendars")).toObject();
    for (auto it = calendarsObj.begin(); it != calendarsObj.end(); ++it) {
        caps.perCalendarCapabilities[it.key()] = PerCalendarCapabilities::fromJson(it.value().toObject());
    }

    // Palm-specific
    caps.hasDateBook = json.value(QStringLiteral("hasDateBook")).toBool(false);
    caps.hasToDo = json.value(QStringLiteral("hasToDo")).toBool(false);

    return caps;
}

QJsonObject DiscoveredCapabilities::toJson() const
{
    QJsonObject json;

    if (discoveredAt.isValid()) {
        json[QStringLiteral("discoveredAt")] = discoveredAt.toString(Qt::ISODate);
    }
    if (!serverProduct.isEmpty()) {
        json[QStringLiteral("serverProduct")] = serverProduct;
    }
    if (!serverVersion.isEmpty()) {
        json[QStringLiteral("serverVersion")] = serverVersion;
    }
    json[QStringLiteral("supportsCalendarCreation")] = supportsCalendarCreation;
    if (maxResourceSize > 0) {
        json[QStringLiteral("maxResourceSize")] = maxResourceSize;
    }

    // Supported component types
    if (!supportedComponentTypes.isEmpty()) {
        QJsonArray typesArray;
        for (const QString &type : supportedComponentTypes) {
            typesArray.append(type);
        }
        json[QStringLiteral("supportedComponentTypes")] = typesArray;
    }

    // Per-calendar capabilities
    if (!perCalendarCapabilities.isEmpty()) {
        QJsonObject calendarsObj;
        for (auto it = perCalendarCapabilities.begin(); it != perCalendarCapabilities.end(); ++it) {
            calendarsObj[it.key()] = it.value().toJson();
        }
        json[QStringLiteral("calendars")] = calendarsObj;
    }

    // Palm-specific
    if (hasDateBook) {
        json[QStringLiteral("hasDateBook")] = true;
    }
    if (hasToDo) {
        json[QStringLiteral("hasToDo")] = true;
    }

    return json;
}

// ============================================================================
// BackendConfiguration implementation
// ============================================================================

QString BackendConfiguration::friendlyTypeName(const QString &type)
{
    if (type == QLatin1String("local"))        return QStringLiteral("Local Storage");
    if (type == QLatin1String("orgmode"))       return QStringLiteral("Org Mode");
    if (type == QLatin1String("caldav"))        return QStringLiteral("CalDAV");
    if (type == QLatin1String("decsync"))       return QStringLiteral("DecSync");
    if (type == QLatin1String("akonadi"))       return QStringLiteral("Akonadi");
    if (type == QLatin1String("subscription"))  return QStringLiteral("Subscription");
    if (type == QLatin1String("planstan"))      return QStringLiteral("PlanStan");
    if (type.isEmpty())                         return QString();

    // Unknown type: title-case it
    QString result = type;
    result[0] = result[0].toUpper();
    return result;
}

QString BackendConfiguration::resolvedDisplayName() const
{
    if (!displayName.isEmpty())
        return displayName;

    QString friendly = friendlyTypeName(type);
    if (friendly.isEmpty()) {
        return id.isEmpty() ? QObject::tr("Unknown Backend") : id;
    }

    if (id.isEmpty())
        return friendly;

    return QStringLiteral("%1 (%2)").arg(friendly, id);
}

BackendConfiguration BackendConfiguration::fromJson(const QJsonObject &json)
{
    BackendConfiguration config;
    config.id = json.value(QStringLiteral("id")).toString();
    config.type = json.value(QStringLiteral("type")).toString();
    config.displayName = json.value(QStringLiteral("displayName")).toString();
    config.enabled = json.value(QStringLiteral("enabled")).toBool(true);

    // Parse connection parameters - all other fields except known metadata
    config.connectionParams = json.toVariantMap();

    // Remove metadata fields from connection params
    config.connectionParams.remove(QStringLiteral("id"));
    config.connectionParams.remove(QStringLiteral("type"));
    config.connectionParams.remove(QStringLiteral("displayName"));
    config.connectionParams.remove(QStringLiteral("enabled"));
    config.connectionParams.remove(QStringLiteral("discoveredCapabilities"));

    // Parse discovered capabilities
    const QJsonObject capsObj = json.value(QStringLiteral("discoveredCapabilities")).toObject();
    if (!capsObj.isEmpty()) {
        config.discoveredCapabilities = DiscoveredCapabilities::fromJson(capsObj);
    }

    return config;
}

QJsonObject BackendConfiguration::toJson() const
{
    QJsonObject json;
    json[QStringLiteral("id")] = id;
    json[QStringLiteral("type")] = type;

    if (!displayName.isEmpty()) {
        json[QStringLiteral("displayName")] = displayName;
    }
    if (!enabled) {
        json[QStringLiteral("enabled")] = false;
    }

    // Add connection parameters
    for (auto it = connectionParams.begin(); it != connectionParams.end(); ++it) {
        json[it.key()] = QJsonValue::fromVariant(it.value());
    }

    // Add discovered capabilities if present
    if (discoveredCapabilities.isValid()) {
        json[QStringLiteral("discoveredCapabilities")] = discoveredCapabilities.toJson();
    }

    return json;
}


} // namespace Kalburator::Sync
