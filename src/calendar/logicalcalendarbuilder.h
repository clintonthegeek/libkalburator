#ifndef LOGICALCALENDARBUILDER_H
#define LOGICALCALENDARBUILDER_H

#include "logicalcalendar.h"
#include "backendcapabilities.h"
#include "discoveredcalendar.h"
#include <QList>
#include <QMap>
#include <QStringList>

namespace Kalburator::Sync {

/**
 * @brief Utility class for building LogicalCalendars from discovered calendars.
 *
 * SCAFFOLDING (WP-C4 2026-06-10): used only in lib-internal tests; no consumer
 * has adopted it. Decide: promote to supported API or remove. See FINDINGS.md.
 *
 * This class centralizes the logic for:
 * - Matching discovered calendars across backends by name
 * - Creating CalendarBackendBinding objects with proper metadata
 * - Validating bindings against BackendCapabilities
 * - Supporting N-way bindings (not limited to 2 backends)
 *
 * It is NOT a UI class - it provides the logic that UI classes use.
 *
 * Usage:
 * @code
 * LogicalCalendarBuilder builder;
 * builder.setBackendCapabilities(backendId, caps);
 * builder.addDiscoveredCalendars(backendId, discoveredList);
 * builder.setExistingCalendars(existingLogicalCalendars);
 *
 * QList<LogicalCalendar> matched = builder.autoMatch();
 * QStringList warnings = builder.validationWarnings();
 * @endcode
 */
class LogicalCalendarBuilder
{
public:
    LogicalCalendarBuilder();

    // === Input Configuration ===

    /**
     * @brief Register backend capabilities for validation.
     */
    void setBackendCapabilities(const QString &backendId,
                                const BackendCapabilities &caps);

    /**
     * @brief Add discovered calendars from a backend.
     *
     * Can be called multiple times for different backends.
     */
    void addDiscoveredCalendars(const QString &backendId,
                                const QList<DiscoveredCalendar> &calendars);

    /**
     * @brief Set existing LogicalCalendars for deduplication.
     */
    void setExistingCalendars(const QList<LogicalCalendar> &existing);

    /**
     * @brief Configure the primary backend ID.
     *
     * In auto-matching, calendars from this backend become Primary bindings.
     */
    void setPrimaryBackendId(const QString &backendId);

    /**
     * @brief Configure sync backend order (for N-way sync).
     *
     * Order determines Secondary, Tertiary, etc. roles.
     * The primaryBackendId is automatically excluded.
     */
    void setSyncBackendOrder(const QStringList &backendIds);

    // === Building Operations ===

    /**
     * @brief Auto-match discovered calendars by name.
     *
     * Algorithm:
     * 1. Group discovered calendars by normalized name (case-insensitive)
     * 2. For each unique name, create a LogicalCalendar
     * 3. Add bindings for each backend that has this calendar
     * 4. Set needsCreation=false for discovered, true for to-be-created
     * 5. Validate against capabilities and collect warnings
     *
     * @return List of matched LogicalCalendars (not including existing)
     */
    QList<LogicalCalendar> autoMatch();

    /**
     * @brief Create a single LogicalCalendar with specified bindings.
     *
     * Used when user explicitly configures bindings (creation wizard).
     *
     * @param displayName User-visible calendar name
     * @param type Calendar type (Event, Todo, Hybrid)
     * @param bindings Pre-configured bindings
     * @return Configured LogicalCalendar
     */
    LogicalCalendar createCalendar(const QString &displayName,
                                   CalendarType type,
                                   const QList<CalendarBackendBinding> &bindings);

    /**
     * @brief Create a binding from a discovered calendar.
     *
     * Copies metadata from discovery and validates against capabilities.
     *
     * @param discovered The discovered calendar
     * @param role The binding role (Primary, Secondary, etc.)
     * @return Configured binding
     */
    CalendarBackendBinding createBindingFromDiscovery(
        const DiscoveredCalendar &discovered,
        BackendRole role);

    /**
     * @brief Create a binding for a calendar that needs to be created.
     *
     * Sets needsCreation=true and prepares metadata template.
     *
     * @param backendId Target backend
     * @param calendarId Desired calendar ID
     * @param role The binding role
     * @return Configured binding with needsCreation=true
     */
    CalendarBackendBinding createPendingBinding(
        const QString &backendId,
        const QString &calendarId,
        BackendRole role);

    // === Validation ===

    /**
     * @brief Get validation warnings from last operation.
     *
     * Warnings include:
     * - Calendar type not supported by backend
     * - Recurrence features that will be lost
     * - Property support mismatches
     */
    QStringList validationWarnings() const;

    /**
     * @brief Validate a binding against backend capabilities.
     *
     * @param binding The binding to validate
     * @param calendarType The calendar type being bound
     * @return Warning message, or empty if valid
     */
    QString validateBinding(const CalendarBackendBinding &binding,
                           CalendarType calendarType) const;

    // === Utilities ===

    /**
     * @brief Generate a normalized calendar ID from display name.
     *
     * Converts "Work Calendar" -> "work-calendar"
     */
    static QString normalizeCalendarId(const QString &displayName);

    /**
     * @brief Check if a calendar ID is already used in existing calendars.
     */
    bool isCalendarIdUsed(const QString &calendarId,
                          const QString &backendId) const;

    /**
     * @brief Clear all builder state for reuse.
     */
    void clear();

private:
    QString normalizeNameForMatching(const QString &name) const;
    BackendRole roleForBackendIndex(int index) const;
    CalendarType determineCalendarType(const QList<DiscoveredCalendar> &calendars) const;

    QMap<QString, BackendCapabilities> m_capabilities;
    QMap<QString, QList<DiscoveredCalendar>> m_discovered;
    QList<LogicalCalendar> m_existing;
    QString m_primaryBackendId;
    QStringList m_syncBackendOrder;
    QStringList m_warnings;
};

} // namespace Kalburator::Sync

#endif // LOGICALCALENDARBUILDER_H
