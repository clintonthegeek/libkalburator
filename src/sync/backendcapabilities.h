#ifndef BACKENDCAPABILITIES_H
#define BACKENDCAPABILITIES_H

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QList>
#include <QColor>
#include <KCalendarCore/Incidence>

// Include syncbackend.h for RecurrenceCapabilities
// (no circular dependency since syncbackend.h only forward-declares BackendCapabilities)
#include "syncbackend.h"

namespace Kalburator::Sync {

// CalendarType is now in its own header (included via syncbackend.h)
// to avoid circular dependency issues

/**
 * @brief Describes what incidence types a backend supports.
 */
struct IncidenceSupport
{
    bool supportsEvents = true;     ///< Can store VEVENT
    bool supportsTodos = true;      ///< Can store VTODO
    bool supportsJournals = false;  ///< Can store VJOURNAL (rare)
    bool supportsHybrid = true;     ///< Mixed VEVENT/VTODO in same calendar
    bool perCalendarRestrictions = false;  ///< CalDAV: each calendar may differ

    /**
     * @brief Get list of supported component types as strings.
     * @return List like ["VEVENT", "VTODO"]
     */
    QStringList supportedComponentTypes() const;
};

/**
 * @brief Describes property-level support.
 *
 * Some backends have limited support for certain iCal properties.
 */
struct PropertySupport
{
    // Common properties
    bool supportsPriority = true;      ///< PRIORITY field
    bool supportsPercentComplete = true; ///< PERCENT-COMPLETE for todos
    bool supportsCategories = true;    ///< CATEGORIES (tags)
    bool supportsDescription = true;   ///< DESCRIPTION field
    bool supportsLocation = true;      ///< LOCATION field
    bool supportsUrl = true;           ///< URL field
    bool supportsGeo = false;          ///< GEO latitude/longitude

    // Extended properties
    bool supportsCustomProperties = true;  ///< X-* custom properties
    bool supportsRelatedTo = true;         ///< RELATED-TO (hierarchy)

    // Backend-specific extensions
    bool supportsEffort = false;       ///< Org-mode EFFORT property
    bool supportsStateSequence = false; ///< Org-mode TODO sequences
    bool supportsClosed = false;       ///< Org-mode CLOSED timestamp
    bool supportsLogbook = false;      ///< Org-mode :LOGBOOK: drawer

    /**
     * @brief Get list of unsupported standard properties.
     */
    QStringList unsupportedProperties() const;
};

/**
 * @brief Describes structural/hierarchy capabilities.
 */
struct StructuralCapabilities
{
    bool supportsHierarchy = false;    ///< Parent-child relationships
    bool supportsTagInheritance = false; ///< Tags inherit down hierarchy
    bool supportsArchiving = false;    ///< Archive completed items
    int maxNestingDepth = 0;           ///< 0 = unlimited, >0 = max depth
};

/**
 * @brief Parameter definition for calendar creation.
 */
struct CalendarCreationParameter
{
    QString key;          ///< Parameter key (e.g., "timezone")
    QString displayName;  ///< Human-readable name
    QString type;         ///< "string", "color", "bool", "enum"
    QVariant defaultValue;
    QStringList enumOptions;  ///< For type="enum"
    bool required = false;
};

/**
 * @brief Describes calendar-level CRUD capabilities.
 */
struct CalendarCrudCapabilities
{
    bool supportsCreate = true;      ///< Can create new calendars
    bool supportsDelete = true;      ///< Can delete calendars
    bool supportsRename = true;      ///< Can rename calendars
    bool supportsColor = true;       ///< Calendar color support
    bool supportsDescription = true; ///< Calendar description support
    bool supportsOrder = true;       ///< Calendar ordering
    bool requiresNetworkDiscovery = false;  ///< CalDAV: must discover from server

    /// Additional parameters for calendar creation
    QList<CalendarCreationParameter> creationParameters;
};

/**
 * @brief Describes sync characteristics.
 */
struct SyncCharacteristics
{
    bool supportsDeltaSync = false;   ///< Can sync only changed items
    bool supportsEtags = false;       ///< ETag-based change detection
    bool supportsBatching = false;    ///< Can batch multiple operations
    int maxBatchSize = 0;             ///< 0 = unlimited, >0 = max items per batch
    bool requiresFullFetch = true;    ///< Must fetch all items to detect changes
};

/**
 * @brief Comprehensive capability advertisement for a backend.
 *
 * This struct describes what a backend can and cannot do, allowing
 * the UI to make informed decisions about feature availability and
 * warn users about potential data loss.
 *
 * Example usage:
 * @code
 * BackendCapabilities caps = backend->capabilities();
 * if (!caps.incidenceSupport.supportsHybrid) {
 *     // Show calendar type selector
 * }
 * if (caps.calendarCrud.requiresNetworkDiscovery) {
 *     // Run capability discovery
 * }
 * @endcode
 */
struct BackendCapabilities
{
    // Identity
    QString backendType;    ///< E.g., "local", "orgmode", "caldav"
    QString displayName;    ///< Human-readable name for UI
    QString description;    ///< Brief description of backend

    // Capability categories
    IncidenceSupport incidenceSupport;
    RecurrenceCapabilities recurrence;
    PropertySupport propertySupport;
    StructuralCapabilities structural;
    CalendarCrudCapabilities calendarCrud;
    SyncCharacteristics syncCharacteristics;

    /**
     * @brief Check if this backend supports a specific calendar type.
     * @param type The calendar type to check
     * @return true if the type is supported
     */
    bool supportsCalendarType(CalendarType type) const;

    /**
     * @brief Describe what would be lost when saving an incidence to this backend.
     *
     * Checks the incidence's properties against backend capabilities and
     * returns a list of human-readable descriptions of information that
     * would be lost or degraded.
     *
     * @param incidence The incidence to analyze
     * @return List of loss descriptions (empty if no loss)
     */
    QStringList describeLoss(const KCalendarCore::Incidence::Ptr &incidence) const;

    /**
     * @brief Describe what would be lost when saving an incidence to a specific
     * calendar type within this backend.
     *
     * For backends with perCalendarRestrictions=true (DecSync, CalDAV),
     * this checks the incidence type against the target calendar's type
     * constraint in addition to property/recurrence checks.
     *
     * @param incidence The incidence to analyze
     * @param targetType The calendar type constraint of the target collection
     * @return List of loss descriptions (empty if no loss)
     */
    QStringList describeLoss(const KCalendarCore::Incidence::Ptr &incidence,
                             CalendarType targetType) const;

    /**
     * @brief Create default capabilities for local iCalendar backend.
     */
    static BackendCapabilities localDefaults();

    /**
     * @brief Create default capabilities for org-mode backend.
     */
    static BackendCapabilities orgmodeDefaults();

    /**
     * @brief Create default capabilities for CalDAV backend.
     */
    static BackendCapabilities caldavDefaults();

    /**
     * @brief Create default capabilities for DecSync backend.
     */
    static BackendCapabilities decsyncDefaults();

    /**
     * @brief Create default capabilities for Akonadi backend.
     *
     * Akonadi delegates to underlying resources (CalDAV, Google, EWS, etc.)
     * so capabilities reflect full iCalendar support. Per-calendar restrictions
     * apply since different Akonadi resources may have different capabilities.
     */
    static BackendCapabilities akonadiDefaults();

    /**
     * @brief Create default capabilities for PlanStan project backend.
     */
    static BackendCapabilities planstanDefaults();
};

/**
 * @brief Per-calendar capability information (discovered from server).
 *
 * CalDAV servers can restrict individual calendars to specific component
 * types. This struct stores discovered per-calendar capabilities.
 */
struct CalendarCapabilities
{
    QString calendarId;
    bool supportsVEvent = true;
    bool supportsVTodo = true;
    bool supportsVJournal = false;
    QColor serverColor;        ///< Color from calendar-color property
    QString serverDisplayName; ///< Display name from server
    int maxResourceSize = 0;   ///< Max item size in bytes (0 = unlimited)

    /**
     * @brief Check if this calendar supports a specific calendar type.
     */
    bool supportsCalendarType(CalendarType type) const;

    /**
     * @brief Get supported component types as string list.
     */
    QStringList supportedComponentTypes() const;
};

} // namespace Kalburator::Sync

#endif // BACKENDCAPABILITIES_H
