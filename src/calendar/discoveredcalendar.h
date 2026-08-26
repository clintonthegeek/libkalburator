#ifndef DISCOVEREDCALENDAR_H
#define DISCOVEREDCALENDAR_H

#include <QString>
#include <QList>
#include <QVariantMap>
#include <QColor>
#include <QMetaType>

#include "calendartype.h"
#include "calendarcapabilities.h"

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

    // === VP.a (vtodo-parity W8): unified capability exposure ===

    /**
     * @brief The capabilities/trait report for this collection.
     *
     * Metadata-backed (key "capabilities", JSON-encoded CalendarCapabilities)
     * so existing constructors and serialization stay valid. Backends either
     * derive from discovery facts (CalDAV) or pin the static family report
     * (CapabilityReports). Default-constructed when never set — hosts must
     * treat an all-defaults report as "unknown backend, query the family
     * reports instead".
     */
    Kalburator::Sync::CalendarCapabilities capabilities() const {
        return Kalburator::Sync::CalendarCapabilities::fromJson(
            metadata.value(QStringLiteral("capabilities")).value<QJsonObject>());
    }

    /**
     * @brief Attach a capability report to this discovered calendar.
     */
    void setCapabilities(const Kalburator::Sync::CalendarCapabilities &caps) {
        metadata.insert(QStringLiteral("capabilities"), caps.toJson());
    }

    bool isValid() const {
        return !calendarId.isEmpty() && !backendId.isEmpty();
    }

    bool isHybrid() const {
        return supportsVEvent && supportsVTodo;
    }

    /**
     * @brief The calendar's component type, derived from the supports* flags.
     *
     * Faithfully mirrors the retired per-backend `discoveredCalendarType()`:
     * a calendar that supports exactly one of VEVENT/VTODO is Event/Todo;
     * one that supports both (or, defensively, neither) is Hybrid.
     */
    CalendarType calendarType() const {
        if (supportsVEvent && !supportsVTodo) return CalendarType::Event;
        if (supportsVTodo && !supportsVEvent) return CalendarType::Todo;
        return CalendarType::Hybrid;
    }
};

} // namespace Kalburator::Sync

Q_DECLARE_METATYPE(Kalburator::Sync::DiscoveredCalendar)

#endif // DISCOVEREDCALENDAR_H
