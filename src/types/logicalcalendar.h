#ifndef LOGICALCALENDAR_H
#define LOGICALCALENDAR_H

#include <QString>
#include <QColor>
#include <QList>
#include <QVariantMap>
#include <QSet>
#include <QPair>
#include <algorithm>
#include "calendartype.h"  // For CalendarType
#include "shape.h"         // For Shape::DomainId

namespace Kalburator::Sync {

/**
 * @file logicalcalendar.h
 * @brief Types for logical calendars that may span multiple backends.
 *
 * A LogicalCalendar presents a unified view to the user while potentially
 * existing on multiple backends (e.g., local storage + CalDAV server).
 *
 * Architecture:
 * - User sees "Work Calendar" (LogicalCalendar)
 * - Internally maps to: local/work.ics (primary) + caldav/Work (secondary)
 * - Edits go to primary on auto-save timer or manual save
 * - Sync operations push/pull between primary and secondary
 */

/**
 * @brief Role of a backend binding within a logical calendar.
 *
 * Supports N-way bindings with multiple sync targets using numerical ordering:
 * - Primary: Main storage (required, exactly 1)
 * - Sync1/Sync2/Sync3/etc.: Sync targets (expandable without limit)
 * - ReadOnly: View only, no sync writes
 */
enum class BackendRole {
    Primary = 0,      ///< Main storage - edits go here first (required, exactly 1)
    Sync1 = 1,        ///< First sync target - receives changes via sync
    Sync2 = 2,        ///< Second sync target (for N-way sync)
    Sync3 = 3,        ///< Third sync target
    Sync4 = 4,        ///< Fourth sync target
    // ... expandable to Sync5, Sync6, etc. (no limit)
    ReadOnly = -1     ///< View only - no writes allowed (negative to avoid collision)
};

/**
 * @brief Check if a role is a sync target (Sync1, Sync2, etc.)
 */
inline bool isSyncRole(BackendRole role) {
    return static_cast<int>(role) > 0;  // Positive values are writable sync roles
}

/**
 * @brief Binds a logical calendar to a specific backend/calendar pair.
 *
 * Each binding stores backend-specific metadata in a QVariantMap, allowing
 * different backend types to store their own required data:
 * - CalDAV: {"davUrl": "https://...", "etag": "..."}
 * - OrgMode: {"filePath": "/path/to/file.org", "headline": "* Tasks"}
 * - Local: {"directory": "/path/to/calendar/"}
 */
struct CalendarBackendBinding {
    QString backendId;      ///< Backend identifier (e.g., "local", "caldav")
    QString calendarId;     ///< Calendar ID on that backend
    BackendRole role = BackendRole::Primary;
    bool enabled = true;    ///< Whether this binding is active
    bool needsCreation = false; ///< True if calendar must be created on this backend
    int syncOrder = 0;      ///< Sync priority (0=first, higher=later). Used for ordering sync operations.

    /**
     * @brief Backend-specific metadata for this binding.
     *
     * The backend is responsible for interpreting these keys.
     */
    QVariantMap metadata;

    // === Convenience Accessors ===

    /**
     * @brief Get a string value from metadata.
     */
    QString metadataString(const QString &key) const {
        return metadata.value(key).toString();
    }

    /**
     * @brief Set a metadata value.
     */
    void setMetadata(const QString &key, const QVariant &value) {
        metadata.insert(key, value);
    }

    // === Backwards Compatibility: davUrl Accessor ===

    /**
     * @brief Get the CalDAV URL (for backwards compatibility).
     * @deprecated Use metadataString("davUrl") instead.
     */
    QString davUrl() const { return metadataString(QStringLiteral("davUrl")); }

    /**
     * @brief Set the CalDAV URL (for backwards compatibility).
     * @deprecated Use setMetadata("davUrl", url) instead.
     */
    void setDavUrl(const QString &url) { setMetadata(QStringLiteral("davUrl"), url); }

    // === Validation & Comparison ===

    bool isValid() const {
        return !backendId.isEmpty() && !calendarId.isEmpty();
    }

    bool operator==(const CalendarBackendBinding &other) const {
        return backendId == other.backendId &&
               calendarId == other.calendarId &&
               metadata == other.metadata &&
               role == other.role &&
               enabled == other.enabled &&
               needsCreation == other.needsCreation &&
               syncOrder == other.syncOrder;
    }
};

/**
 * @brief A user-facing calendar that may span multiple backends.
 *
 * LogicalCalendar is the abstraction users interact with. They see
 * "Personal Calendar" without knowing it's stored locally and synced to CalDAV.
 *
 * Key features:
 * - Single display name for user
 * - Primary backend binding (required)
 * - Optional secondary binding for sync
 * - SaveInstantly mode (auto-save vs explicit)
 * - Standard calendar properties (color, visibility, order)
 */
struct LogicalCalendar {
    QString id;                 ///< Unique logical calendar ID
    QString displayName;        ///< User-visible name
    QString description;        ///< Optional description
    QColor color;               ///< Display color
    int displayOrder = 0;       ///< Sort order in lists
    CalendarType type = CalendarType::Hybrid;  ///< Event, Todo, or Hybrid
    Shape::DomainId domain = Shape::DomainId(QStringLiteral("calendar")); ///< Data domain (calendar/contacts/todo/...)

    // Backend bindings
    QList<CalendarBackendBinding> bindings;

    // Runtime state (whether calendar is active)
    bool enabled = true;        ///< True = calendar loaded into memory (alarms work, items queryable)
    bool visible = true;        ///< True = incidences shown in combined views (toggle in explorer)
    bool secret = false;        ///< True = calendar hidden from Collection Explorer entirely

    // Behavior settings
    bool syncEnabled = false;   ///< True if secondary binding should sync
    bool autoSyncOnLoad = false; ///< True = auto-sync after loading items (disabled by default)

    // Project planning
    bool isProject = false;     ///< True = managed by ProjectStore, not a regular calendar

    // Helper methods
    bool isValid() const {
        return !id.isEmpty() && !displayName.isEmpty() && hasPrimaryBinding();
    }

    /// Domain-agnostic alias for `id` (collection identity).
    QString collectionId() const { return id; }

    /**
     * @brief Neutral fact: does an enabled Sync* (writable remote spoke) binding exist?
     *
     * Pure config-level query, no backend access. Consumers apply their own policy:
     * a conduit (WildPalms) demotes user editing when true; a two-way editor (PlanStan)
     * may ignore it. ReadOnly (-1) and Primary (0) bindings do not count — only the
     * positive Sync1..N roles, which is exactly what isSyncRole() selects.
     */
    bool hasWritableRemoteSyncTarget() const {
        for (const auto &b : bindings) {
            if (b.enabled && isSyncRole(b.role)) {
                return true;
            }
        }
        return false;
    }

    bool hasPrimaryBinding() const {
        for (const auto &binding : bindings) {
            if (binding.role == BackendRole::Primary && binding.enabled) {
                return true;
            }
        }
        return false;
    }

    CalendarBackendBinding primaryBinding() const {
        for (const auto &binding : bindings) {
            if (binding.role == BackendRole::Primary && binding.enabled) {
                return binding;
            }
        }
        return {};  // Empty binding
    }

    CalendarBackendBinding secondaryBinding() const {
        // For backward compatibility, return first sync binding (Sync1)
        for (const auto &binding : bindings) {
            if (binding.role == BackendRole::Sync1 && binding.enabled) {
                return binding;
            }
        }
        return {};  // Empty binding
    }

    // === Generic N-Way Binding Methods ===

    /**
     * @brief Get binding by role.
     * @return First enabled binding with this role, or invalid binding if none.
     */
    CalendarBackendBinding bindingWithRole(BackendRole role) const {
        for (const auto &b : bindings) {
            if (b.enabled && b.role == role) return b;
        }
        return CalendarBackendBinding();
    }

    /**
     * @brief Get all sync bindings (Secondary, Tertiary, etc.).
     */
    QList<CalendarBackendBinding> syncBindings() const {
        QList<CalendarBackendBinding> result;
        for (const auto &b : bindings) {
            if (b.enabled && isSyncRole(b.role)) {
                result.append(b);
            }
        }
        return result;
    }

    /**
     * @brief Set or replace binding for a specific role.
     */
    void setBindingForRole(const CalendarBackendBinding &binding) {
        // Remove existing binding with same role
        for (auto it = bindings.begin(); it != bindings.end(); ) {
            if (it->role == binding.role) {
                it = bindings.erase(it);
            } else {
                ++it;
            }
        }
        if (binding.isValid()) {
            bindings.append(binding);
        }
    }

    QList<CalendarBackendBinding> enabledBindings() const {
        QList<CalendarBackendBinding> result;
        for (const auto &binding : bindings) {
            if (binding.enabled) {
                result.append(binding);
            }
        }
        return result;
    }

    void setPrimaryBinding(const QString &backendId, const QString &calendarId) {
        // Remove existing primary
        for (auto it = bindings.begin(); it != bindings.end(); ) {
            if (it->role == BackendRole::Primary) {
                it = bindings.erase(it);
            } else {
                ++it;
            }
        }
        // Add new primary
        CalendarBackendBinding binding;
        binding.backendId = backendId;
        binding.calendarId = calendarId;
        binding.role = BackendRole::Primary;
        binding.enabled = true;
        bindings.prepend(binding);  // Primary first
    }

    void setSecondaryBinding(const QString &backendId, const QString &calendarId) {
        // Remove existing Sync1 binding
        for (auto it = bindings.begin(); it != bindings.end(); ) {
            if (it->role == BackendRole::Sync1) {
                it = bindings.erase(it);
            } else {
                ++it;
            }
        }
        // Add new Sync1 binding if valid
        if (!backendId.isEmpty() && !calendarId.isEmpty()) {
            CalendarBackendBinding binding;
            binding.backendId = backendId;
            binding.calendarId = calendarId;
            binding.role = BackendRole::Sync1;
            binding.enabled = true;
            binding.syncOrder = 1;  // First sync target gets order 1 (Primary is 0)
            bindings.append(binding);
            syncEnabled = true;
        } else {
            syncEnabled = false;
        }
    }

    void clearSecondaryBinding() {
        for (auto it = bindings.begin(); it != bindings.end(); ) {
            if (it->role == BackendRole::Sync1) {  // Updated: Secondary -> Sync1
                it = bindings.erase(it);
            } else {
                ++it;
            }
        }
        syncEnabled = false;
    }

    // === Multi-Backend Support Methods ===

    /**
     * @brief Get bindings sorted by syncOrder (ascending).
     * @return List of enabled bindings ordered by syncOrder (0=first).
     */
    QList<CalendarBackendBinding> orderedSyncBindings() const {
        QList<CalendarBackendBinding> result = enabledBindings();
        std::sort(result.begin(), result.end(), [](const CalendarBackendBinding &a, const CalendarBackendBinding &b) {
            return a.syncOrder < b.syncOrder;
        });
        return result;
    }

    /**
     * @brief Validation result for logical calendar configuration.
     */
    struct ValidationResult {
        bool valid = true;
        QStringList errors;
        QStringList warnings;
    };

    /**
     * @brief Validate logical calendar (exactly one primary, no conflicts).
     * @return Validation result with errors and warnings.
     */
    ValidationResult validate() const {
        ValidationResult result;

        // Check: Must have displayName and id
        if (displayName.isEmpty()) {
            result.errors << QStringLiteral("Calendar must have a display name");
            result.valid = false;
        }
        if (id.isEmpty()) {
            result.errors << QStringLiteral("Calendar must have an ID");
            result.valid = false;
        }

        // Check: Exactly one enabled Primary binding
        int primaryCount = 0;
        for (const auto &binding : bindings) {
            if (binding.enabled && binding.role == BackendRole::Primary) {
                primaryCount++;
            }
        }
        if (primaryCount == 0) {
            result.errors << QStringLiteral("Calendar must have exactly one primary binding");
            result.valid = false;
        } else if (primaryCount > 1) {
            result.errors << QStringLiteral("Calendar has multiple primary bindings (only 1 allowed)");
            result.valid = false;
        }

        // Check: No duplicate backend+calendar pairs
        QSet<QPair<QString, QString>> seen;
        for (const auto &binding : bindings) {
            if (!binding.enabled) continue;
            QPair<QString, QString> key{binding.backendId, binding.calendarId};
            if (seen.contains(key)) {
                result.warnings << QString("Duplicate binding: backend=%1, calendar=%2")
                    .arg(binding.backendId, binding.calendarId);
            }
            seen.insert(key);
        }

        // Check: ReadOnly bindings should not be Primary
        for (const auto &binding : bindings) {
            if (binding.enabled && binding.role == BackendRole::ReadOnly && binding.role == BackendRole::Primary) {
                result.errors << QStringLiteral("Primary binding cannot be read-only");
                result.valid = false;
            }
        }

        return result;
    }

    /**
     * @brief Check if a binding can be safely removed.
     * @param backendId Backend ID to check.
     * @return False if this is the only enabled binding, true otherwise.
     */
    bool canRemoveBinding(const QString &backendId) const {
        int enabledCount = 0;
        for (const auto &binding : bindings) {
            if (binding.enabled) {
                enabledCount++;
            }
        }
        // Can remove if there's more than one binding
        return enabledCount > 1;
    }

    /**
     * @brief Promote a binding to Primary role (demotes current primary).
     * @param backendId Backend ID to promote.
     */
    void promoteBindingToPrimary(const QString &backendId) {
        // Demote current primary to Sync1
        for (auto &binding : bindings) {
            if (binding.role == BackendRole::Primary && binding.enabled) {
                binding.role = BackendRole::Sync1;
                if (binding.syncOrder == 0) {
                    binding.syncOrder = 1;  // Give it a non-zero order
                }
            }
        }

        // Promote target binding to Primary
        for (auto &binding : bindings) {
            if (binding.backendId == backendId && binding.enabled) {
                binding.role = BackendRole::Primary;
                binding.syncOrder = 0;  // Primary always has syncOrder=0
                break;
            }
        }
    }
};

/// Domain-agnostic alias for LogicalCalendar. Both names are permanent; new
/// domain-neutral code (and external consumers like WildPalms) may use this name.
using LogicalCollection = LogicalCalendar;

// Qt metatype declaration

} // namespace Kalburator::Sync

Q_DECLARE_METATYPE(Kalburator::Sync::BackendRole)
Q_DECLARE_METATYPE(Kalburator::Sync::CalendarBackendBinding)
Q_DECLARE_METATYPE(Kalburator::Sync::LogicalCalendar)

#endif // LOGICALCALENDAR_H
