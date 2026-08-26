#ifndef BACKENDCONFIGURATION_H
#define BACKENDCONFIGURATION_H

#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QMap>
#include <QVariantMap>
#include <QJsonObject>
#include <QColor>

namespace Kalburator::Sync {

/**
 * @brief Per-calendar discovered capability information.
 *
 * CalDAV servers can restrict individual calendars to specific component types.
 * This struct stores discovered per-calendar capabilities.
 */
struct PerCalendarCapabilities
{
    bool supportsVEvent = true;
    bool supportsVTodo = true;
    bool supportsVJournal = false;
    bool writable = true;       ///< Can user create/modify/delete items in this calendar?
    QColor serverColor;
    QString serverDisplayName;
    int maxResourceSize = 0;  // 0 = unlimited

    // VP.a (vtodo-parity W8): producer identity + report-set probing.
    /// Server producer id — PRODID-derived where the PROPFIND exposes it,
    /// else the known-product sniff (body + Server header). Empty when
    /// undiscoverable (capabilitiesFromDiscovery() then falls back to
    /// "caldav").
    QString producerId;
    /// RFC 6578 sync-collection REPORT advertised in supported-report-set.
    bool supportsSyncCollection = false;

    static PerCalendarCapabilities fromJson(const QJsonObject &json);
    QJsonObject toJson() const;
};

/**
 * @brief Discovered capabilities from server.
 *
 * This struct stores capability information discovered during backend
 * configuration (e.g., CalDAV PROPFIND during collection creation).
 * It is persisted in the .kalb file to avoid re-discovery.
 */
struct DiscoveredCapabilities
{
    QDateTime discoveredAt;
    QString serverProduct;              // e.g., "Nextcloud", "Radicale"
    QString serverVersion;              // Server version if available
    bool supportsCalendarCreation = true;
    QStringList supportedComponentTypes;  // ["VEVENT", "VTODO"]
    int maxResourceSize = 0;              // 0 = unlimited

    // Per-calendar capabilities (for CalDAV servers with restrictions)
    QMap<QString, PerCalendarCapabilities> perCalendarCapabilities;

    // Palm-specific fields (for future Palm/HotSync backend)
    bool hasDateBook = false;
    bool hasToDo = false;

    bool isValid() const { return discoveredAt.isValid(); }

    static DiscoveredCapabilities fromJson(const QJsonObject &json);
    QJsonObject toJson() const;
};

/**
 * @brief Complete backend configuration including connection and discovered capabilities.
 *
 * This struct represents a configured backend in the .kalb file:
 * - Connection parameters (url, username, password, etc.)
 * - Discovered capabilities (cached from server)
 *
 * Example JSON:
 * @code
 * {
 *   "id": "caldav-primary",
 *   "type": "caldav",
 *   "url": "https://...",
 *   "username": "user",
 *   "discoveredCapabilities": { ... }
 * }
 * @endcode
 */
struct BackendConfiguration
{
    QString id;         // Unique identifier (e.g., "caldav-primary")
    QString type;       // Backend type (e.g., "local", "orgmode", "caldav")

    // Connection parameters
    QVariantMap connectionParams;

    // Discovered capabilities (cached from server discovery)
    DiscoveredCapabilities discoveredCapabilities;

    // Human-readable display name
    QString displayName;

    // Whether this backend is enabled
    bool enabled = true;

    bool isValid() const { return !id.isEmpty() && !type.isEmpty(); }

    /// Human-readable display name. Returns displayName if set,
    /// otherwise "FriendlyTypeName (id)", or just id if type is empty.
    QString resolvedDisplayName() const;

    /// Human-readable type name (e.g., "CalDAV" for type "caldav").
    static QString friendlyTypeName(const QString &type);

    static BackendConfiguration fromJson(const QJsonObject &json);
    QJsonObject toJson() const;

    // Convenience accessors for common connection parameters
    QString url() const { return connectionParams.value(QStringLiteral("url")).toString(); }
    QString username() const { return connectionParams.value(QStringLiteral("username")).toString(); }
    QString password() const { return connectionParams.value(QStringLiteral("password")).toString(); }
    QString rootPath() const { return connectionParams.value(QStringLiteral("rootPath")).toString(); }
};

} // namespace Kalburator::Sync

#endif // BACKENDCONFIGURATION_H
