#ifndef DISCOVEREDCALENDAR_H
#define DISCOVEREDCALENDAR_H

#include <QString>
#include <QList>
#include <QVariantMap>
#include <QColor>
#include <QMetaType>

namespace Kalburator::Sync {

/**
 * @brief Information about a discovered calendar from any backend.
 *
 * Each backend populates metadata with its specific information:
 * - CalDAV: {"davUrl": "https://...", "etag": "..."}
 * - OrgMode: {"filePath": "/home/user/org/work.org"}
 * - Local: {"directory": "/home/user/calendars/Work/"}
 */
struct DiscoveredCalendar {
    QString name;           ///< Display name
    QString calendarId;     ///< Calendar ID on this backend
    QString backendId;      ///< Backend ID (e.g., "primary", "secondary")
    QString backendType;    ///< Backend type (e.g., "local", "orgmode", "caldav")
    QColor color;           ///< Calendar color (from CalDAV apple:calendar-color, or from local metadata)

    // CalDAV component type support (discovered from supported-calendar-component-set)
    bool supportsVEvent = true;   ///< Calendar supports VEVENT components
    bool supportsVTodo = true;    ///< Calendar supports VTODO components
    bool supportsVJournal = false; ///< Calendar supports VJOURNAL components

    // Writable status (discovered from CalDAV privileges or filesystem permissions)
    bool writable = true;         ///< User can create/modify/delete items in this calendar

    /**
     * @brief Backend-specific metadata discovered from the source.
     *
     * The backend populates this during discovery. Examples:
     * - CalDAV: {"davUrl": "https://...", "etag": "..."}
     * - OrgMode: {"filePath": "/home/user/org/work.org"}
     * - Local: {"directory": "/home/user/calendars/Work/"}
     */
    QVariantMap metadata;

    // === Convenience Accessors ===

    /**
     * @brief Get the CalDAV URL (for backwards compatibility).
     */
    QString davUrl() const { return metadata.value(QStringLiteral("davUrl")).toString(); }

    /**
     * @brief Set the CalDAV URL.
     */
    void setDavUrl(const QString &url) { metadata.insert(QStringLiteral("davUrl"), url); }

    bool isValid() const {
        return !calendarId.isEmpty() && !backendId.isEmpty();
    }

    bool isHybrid() const {
        return supportsVEvent && supportsVTodo;
    }
};

} // namespace Kalburator::Sync

Q_DECLARE_METATYPE(Kalburator::Sync::DiscoveredCalendar)

#endif // DISCOVEREDCALENDAR_H
