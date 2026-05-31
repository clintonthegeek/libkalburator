#include "logicalcalendarjson.h"

namespace Kalburator::Sync {

QString backendRoleToString(BackendRole role) {
    switch (role) {
        case BackendRole::Primary:  return QStringLiteral("primary");
        case BackendRole::Sync1:    return QStringLiteral("sync1");
        case BackendRole::Sync2:    return QStringLiteral("sync2");
        case BackendRole::Sync3:    return QStringLiteral("sync3");
        case BackendRole::Sync4:    return QStringLiteral("sync4");
        case BackendRole::ReadOnly: return QStringLiteral("readonly");
    }
    // Handle any future Sync5, Sync6, etc.
    int roleInt = static_cast<int>(role);
    if (roleInt > 0) {
        return QString("sync%1").arg(roleInt);
    }
    return QStringLiteral("primary");
}

BackendRole backendRoleFromString(const QString &str) {
    // New role names
    if (str == QLatin1String("sync1"))    return BackendRole::Sync1;
    if (str == QLatin1String("sync2"))    return BackendRole::Sync2;
    if (str == QLatin1String("sync3"))    return BackendRole::Sync3;
    if (str == QLatin1String("sync4"))    return BackendRole::Sync4;
    if (str == QLatin1String("readonly")) return BackendRole::ReadOnly;

    // Migration: Old role names (backward compatibility)
    if (str == QLatin1String("secondary"))  return BackendRole::Sync1;
    if (str == QLatin1String("tertiary"))   return BackendRole::Sync2;
    if (str == QLatin1String("quaternary")) return BackendRole::Sync3;

    // Handle dynamic sync roles (sync5, sync6, etc.)
    if (str.startsWith(QLatin1String("sync"))) {
        bool ok;
        int num = str.mid(4).toInt(&ok);  // Extract number after "sync"
        if (ok && num > 0) {
            return static_cast<BackendRole>(num);
        }
    }

    return BackendRole::Primary;
}

QJsonObject calendarBindingToJson(const CalendarBackendBinding &binding) {
    QJsonObject obj;
    obj[QStringLiteral("backendId")] = binding.backendId;
    obj[QStringLiteral("calendarId")] = binding.calendarId;
    obj[QStringLiteral("role")] = backendRoleToString(binding.role);
    obj[QStringLiteral("enabled")] = binding.enabled;
    obj[QStringLiteral("syncOrder")] = binding.syncOrder;  // NEW: Always serialize syncOrder
    if (binding.needsCreation) {
        obj[QStringLiteral("needsCreation")] = true;
    }

    // Serialize metadata as nested object
    if (!binding.metadata.isEmpty()) {
        obj[QStringLiteral("metadata")] = QJsonObject::fromVariantMap(binding.metadata);
    }

    // Backwards compatibility: also write davUrl at top level if present
    QString davUrl = binding.davUrl();
    if (!davUrl.isEmpty()) {
        obj[QStringLiteral("davUrl")] = davUrl;
    }

    return obj;
}

CalendarBackendBinding calendarBindingFromJson(const QJsonObject &obj) {
    CalendarBackendBinding binding;
    binding.backendId = obj.value(QStringLiteral("backendId")).toString();
    binding.calendarId = obj.value(QStringLiteral("calendarId")).toString();
    binding.role = backendRoleFromString(obj.value(QStringLiteral("role")).toString());
    binding.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
    binding.needsCreation = obj.value(QStringLiteral("needsCreation")).toBool(false);

    // Migration: syncOrder field (auto-assign if missing)
    if (obj.contains(QStringLiteral("syncOrder"))) {
        binding.syncOrder = obj.value(QStringLiteral("syncOrder")).toInt(0);
    } else {
        // Auto-assign syncOrder based on role if not present (migration from old format)
        switch (binding.role) {
            case BackendRole::Primary:  binding.syncOrder = 0; break;
            case BackendRole::Sync1:    binding.syncOrder = 1; break;
            case BackendRole::Sync2:    binding.syncOrder = 2; break;
            case BackendRole::Sync3:    binding.syncOrder = 3; break;
            case BackendRole::Sync4:    binding.syncOrder = 4; break;
            case BackendRole::ReadOnly: binding.syncOrder = 99; break;  // ReadOnly last
            default: {
                int roleInt = static_cast<int>(binding.role);
                binding.syncOrder = (roleInt > 0) ? roleInt : 0;
                break;
            }
        }
    }

    // Read metadata object
    if (obj.contains(QStringLiteral("metadata"))) {
        binding.metadata = obj.value(QStringLiteral("metadata")).toObject().toVariantMap();
    }

    // Backwards compatibility: read davUrl from top level if not in metadata
    QString topLevelDavUrl = obj.value(QStringLiteral("davUrl")).toString();
    if (!topLevelDavUrl.isEmpty() && !binding.metadata.contains(QStringLiteral("davUrl"))) {
        binding.setDavUrl(topLevelDavUrl);
    }

    return binding;
}

QJsonObject logicalCalendarToJson(const LogicalCalendar &cal) {
    QJsonObject obj;
    obj[QStringLiteral("id")] = cal.id;
    obj[QStringLiteral("displayName")] = cal.displayName;
    if (!cal.description.isEmpty()) {
        obj[QStringLiteral("description")] = cal.description;
    }
    if (cal.color.isValid()) {
        obj[QStringLiteral("color")] = cal.color.name();
    }
    obj[QStringLiteral("displayOrder")] = cal.displayOrder;
    obj[QStringLiteral("type")] = static_cast<int>(cal.type);  // CalendarType enum
    // Domain: omit for the calendar domain so existing .kalb files round-trip
    // byte-for-byte (no new key); emit only for non-calendar domains.
    if (cal.domain != Shape::DomainId(QStringLiteral("calendar"))) {
        obj[QStringLiteral("domain")] = cal.domain.toString();
    }
    obj[QStringLiteral("enabled")] = cal.enabled;
    obj[QStringLiteral("visible")] = cal.visible;
    if (cal.secret) {
        obj[QStringLiteral("secret")] = true;  // Only write if true (backward compat)
    }
    // saveInstantly removed — all calendars use auto-save timer now
    obj[QStringLiteral("syncEnabled")] = cal.syncEnabled;
    obj[QStringLiteral("autoSyncOnLoad")] = cal.autoSyncOnLoad;
    if (cal.isProject) {
        obj[QStringLiteral("isProject")] = true;  // Only write if true (backward compat)
    }

    QJsonArray bindingsArr;
    for (const auto &binding : cal.bindings) {
        bindingsArr.append(calendarBindingToJson(binding));
    }
    obj[QStringLiteral("bindings")] = bindingsArr;

    return obj;
}

LogicalCalendar logicalCalendarFromJson(const QJsonObject &obj) {
    LogicalCalendar cal;
    cal.id = obj.value(QStringLiteral("id")).toString();
    cal.displayName = obj.value(QStringLiteral("displayName")).toString();
    cal.description = obj.value(QStringLiteral("description")).toString();
    QString colorStr = obj.value(QStringLiteral("color")).toString();
    if (!colorStr.isEmpty()) {
        cal.color = QColor(colorStr);
    }
    cal.displayOrder = obj.value(QStringLiteral("displayOrder")).toInt(0);
    // CalendarType: default to Hybrid (2) for backward compatibility
    cal.type = static_cast<CalendarType>(obj.value(QStringLiteral("type")).toInt(static_cast<int>(CalendarType::Hybrid)));
    cal.domain = obj.contains(QStringLiteral("domain"))
        ? Shape::DomainId(obj.value(QStringLiteral("domain")).toString())
        : Shape::DomainId(QStringLiteral("calendar"));  // absent ⇒ calendar (back-compat)
    cal.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
    cal.visible = obj.value(QStringLiteral("visible")).toBool(true);
    cal.secret = obj.value(QStringLiteral("secret")).toBool(false);
    // saveInstantly removed — silently ignore old configs
    cal.syncEnabled = obj.value(QStringLiteral("syncEnabled")).toBool(false);
    cal.autoSyncOnLoad = obj.value(QStringLiteral("autoSyncOnLoad")).toBool(false);
    cal.isProject = obj.value(QStringLiteral("isProject")).toBool(false);

    QJsonArray bindingsArr = obj.value(QStringLiteral("bindings")).toArray();
    for (const auto &val : bindingsArr) {
        if (val.isObject()) {
            cal.bindings.append(calendarBindingFromJson(val.toObject()));
        }
    }

    return cal;
}

QJsonArray logicalCalendarsToJson(const QList<LogicalCalendar> &calendars) {
    QJsonArray arr;
    for (const auto &cal : calendars) {
        arr.append(logicalCalendarToJson(cal));
    }
    return arr;
}

QList<LogicalCalendar> logicalCalendarsFromJson(const QJsonArray &arr) {
    QList<LogicalCalendar> calendars;
    for (const auto &val : arr) {
        if (val.isObject()) {
            calendars.append(logicalCalendarFromJson(val.toObject()));
        }
    }
    return calendars;
}

} // namespace Kalburator::Sync
