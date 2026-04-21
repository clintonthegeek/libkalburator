#ifndef INCIDENCEDIFF_H
#define INCIDENCEDIFF_H

#include <QString>
#include <QList>
#include <QMap>
#include <QDateTime>
#include <KCalendarCore/Incidence>

namespace Kalburator::Sync {

/**
 * Represents the comparison state of a single property across versions.
 *
 * Used for both sync conflict resolution and recurrence exception management.
 */
struct PropertyDiff {
    QString propertyName;       // e.g., "SUMMARY", "DTSTART", "DESCRIPTION"
    QString displayName;        // Human-readable: "Summary", "Start Time"

    QString valueA;             // Version A value (raw iCal format)
    QString valueB;             // Version B value
    QString valueBaseline;      // Baseline value (empty if no baseline)

    QString displayValueA;      // Human-readable version A
    QString displayValueB;      // Human-readable version B
    QString displayValueBaseline;

    enum State {
        Identical,              // A == B (no conflict)
        OnlyInA,                // Property exists only in A
        OnlyInB,                // Property exists only in B
        BothDifferent,          // A != B, both present (2-way diff)
        AMatchesBaseline,       // A == Baseline, B changed
        BMatchesBaseline,       // B == Baseline, A changed
        BothChangedSame,        // Both changed from baseline, but to same value
        BothChangedDifferent    // Both changed from baseline to different values (true conflict)
    };
    State state = Identical;

    enum Resolution {
        UseA,           // Use version A's value
        UseB,           // Use version B's value
        UseBaseline,    // Revert to baseline
        UseCustom,      // Use custom edited value
        Unresolved      // Not yet decided
    };
    Resolution resolution = Unresolved;

    QString customValue;        // If resolution == UseCustom

    // Helpers
    bool isConflict() const {
        return state == BothDifferent || state == BothChangedDifferent;
    }
    bool needsResolution() const {
        return isConflict() && resolution == Unresolved;
    }
    bool hasValue() const {
        return !valueA.isEmpty() || !valueB.isEmpty();
    }
};

/**
 * Engine for computing and applying property-level diffs between incidences.
 *
 * This class provides the core comparison logic used by both:
 * - Sync conflict resolution (local vs remote, optionally with baseline)
 * - Recurrence exception management (master vs exception)
 */
class IncidenceDiff {
public:
    // Property categories for UI grouping
    enum PropertyCategory {
        Essential,      // SUMMARY, DTSTART, DTEND, DUE
        DateTime,       // RRULE, EXDATE, RDATE, DURATION
        Descriptive,    // DESCRIPTION, LOCATION, URL, CATEGORIES
        Status,         // STATUS, PRIORITY, PERCENT-COMPLETE, COMPLETED
        Organizational, // ORGANIZER, ATTENDEE, RELATED-TO
        Other           // Everything else
    };

    /**
     * Compare two incidences, optionally with baseline for 3-way merge.
     *
     * @param incidenceA First incidence (e.g., local version or master)
     * @param incidenceB Second incidence (e.g., remote version or exception)
     * @param baseline Optional baseline for 3-way merge (e.g., last sync state)
     * @return List of PropertyDiff for each differing property
     */
    static QList<PropertyDiff> compare(
        const KCalendarCore::Incidence::Ptr &incidenceA,
        const KCalendarCore::Incidence::Ptr &incidenceB,
        const KCalendarCore::Incidence::Ptr &baseline = nullptr);

    /**
     * Compare using raw iCal strings (for conflicts loaded from storage).
     *
     * @param icalA First iCal data
     * @param icalB Second iCal data
     * @param icalBaseline Optional baseline iCal data
     * @return List of PropertyDiff for each differing property
     */
    static QList<PropertyDiff> compareIcal(
        const QString &icalA,
        const QString &icalB,
        const QString &icalBaseline = QString());

    /**
     * Create merged incidence from base + resolutions.
     *
     * @param base The incidence to start from
     * @param diffs List of PropertyDiff with resolutions set
     * @return New incidence with resolved properties applied
     */
    static KCalendarCore::Incidence::Ptr merge(
        const KCalendarCore::Incidence::Ptr &base,
        const QList<PropertyDiff> &diffs);

    /**
     * Parse iCal string into property map.
     * Extracts each property and its value as raw strings.
     *
     * @param ical Raw iCal data string
     * @return Map of property name to value
     */
    static QMap<QString, QString> parseIcalProperties(const QString &ical);

    /**
     * Get human-readable property name.
     *
     * @param propertyName iCal property name (e.g., "DTSTART")
     * @return Human-readable name (e.g., "Start Time")
     */
    static QString propertyDisplayName(const QString &propertyName);

    /**
     * Format property value for display.
     * Converts raw iCal values to human-readable format.
     *
     * @param propertyName iCal property name
     * @param value Raw iCal value
     * @return Human-readable value
     */
    static QString formatPropertyValue(const QString &propertyName, const QString &value);

    /**
     * Get property category for UI grouping.
     *
     * @param propertyName iCal property name
     * @return Category for grouping/display
     */
    static PropertyCategory propertyCategory(const QString &propertyName);

    /**
     * Get priority for sorting (lower = more important).
     *
     * @param propertyName iCal property name
     * @return Sort priority (0 = highest)
     */
    static int propertySortPriority(const QString &propertyName);

    /**
     * Apply a specific property value to an incidence.
     *
     * @param incidence The incidence to modify
     * @param propertyName iCal property name
     * @param value Value to set (raw iCal format)
     * @return true if property was successfully applied
     */
    static bool applyPropertyToIncidence(
        const KCalendarCore::Incidence::Ptr &incidence,
        const QString &propertyName,
        const QString &value);

    // === Exception Modified Properties Tracking ===
    // Custom property name for tracking which properties an exception has
    // intentionally modified from its master event.
    static constexpr const char* MODIFIED_PROPS_PROPERTY = "X-PLANSTAN-MODIFIED-PROPS";

    /**
     * Get the list of property names that differ between master and exception.
     * This compares the two incidences and returns all property names that
     * are different (excluding RECURRENCE-ID which is always different).
     *
     * @param master The master recurring incidence
     * @param exception The exception incidence
     * @return List of property names that differ
     */
    static QStringList modifiedPropertiesFromMaster(
        const KCalendarCore::Incidence::Ptr &master,
        const KCalendarCore::Incidence::Ptr &exception);

    /**
     * Get the stored modified properties marker from an exception.
     * Returns the list of properties that were marked as intentionally
     * modified when the exception was created or last updated.
     *
     * @param incidence The exception incidence
     * @return List of property names, or empty if not set
     */
    static QStringList getModifiedPropertiesMarker(
        const KCalendarCore::Incidence::Ptr &incidence);

    /**
     * Set the modified properties marker on an exception.
     * Records which properties have been intentionally modified from the master,
     * so future master edits know which properties to skip.
     *
     * @param incidence The exception incidence to update
     * @param properties List of property names that have been modified
     */
    static void setModifiedPropertiesMarker(
        const KCalendarCore::Incidence::Ptr &incidence,
        const QStringList &properties);

    /**
     * Clear the modified properties marker from an incidence.
     * Used when reverting an exception to match the master.
     *
     * @param incidence The incidence to clear the marker from
     */
    static void clearModifiedPropertiesMarker(
        const KCalendarCore::Incidence::Ptr &incidence);

    // === Exception Inherent Properties ===
    // Properties that are expected to differ between master and exception
    // and should not be treated as conflicts.
    static const QStringList EXCEPTION_INHERENT_PROPERTIES;

    /**
     * Filter out inherent exception differences from a diff list.
     * Removes properties like UID, RECURRENCE-ID, RELATED-TO, EXDATE, RRULE, RDATE
     * that are structurally different between master and exception.
     *
     * @param diffs The original diff list
     * @return Filtered diff list with inherent properties removed
     */
    static QList<PropertyDiff> filterExceptionDiffs(const QList<PropertyDiff> &diffs);

    /**
     * Check if an exception is semantically identical to the master series.
     * This means all properties match except for inherent structural differences.
     * Used to detect when an exception should be reabsorbed into the series.
     *
     * @param master The master recurring incidence
     * @param exception The exception incidence
     * @param occurrenceDateTime The datetime this exception represents in the series
     * @return true if exception can be reabsorbed (is identical to what the series would generate)
     */
    static bool isExceptionIdenticalToMaster(
        const KCalendarCore::Incidence::Ptr &master,
        const KCalendarCore::Incidence::Ptr &exception,
        const QDateTime &occurrenceDateTime);

    /**
     * Extract a property value from an incidence.
     * Returns the raw iCal format value.
     *
     * @param incidence The incidence to extract from
     * @param propertyName iCal property name (e.g., "LOCATION", "SUMMARY")
     * @return The property value, or empty string if not set
     */
    static QString getPropertyValue(
        const KCalendarCore::Incidence::Ptr &incidence,
        const QString &propertyName);

private:

    // Compare property maps and generate diffs
    static QList<PropertyDiff> comparePropertyMaps(
        const QMap<QString, QString> &mapA,
        const QMap<QString, QString> &mapB,
        const QMap<QString, QString> &mapBaseline);

    // Format date/time properties
    static QString formatDateTime(const QString &value);

    // Format recurrence rule
    static QString formatRRule(const QString &value);

    // Format status values
    static QString formatStatus(const QString &value);

    // Format priority values
    static QString formatPriority(const QString &value);
};

} // namespace Kalburator::Sync

#endif // INCIDENCEDIFF_H
