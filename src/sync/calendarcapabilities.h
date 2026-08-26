#ifndef CALENDARCAPABILITIES_H
#define CALENDARCAPABILITIES_H

#include <QString>
#include <QStringList>
#include <QJsonObject>

#include "backendconfiguration.h"

namespace Kalburator::Sync {

/**
 * @brief Unified capability/trait query surface for one logical calendar
 *        (vtodo-parity VP.a / handoff item W8).
 *
 * Consumer-facing contract agreed with PlanStan
 * (docs/2026-08-25-vtodo-parity-handoff-response.md §W8): hosts query this
 * struct instead of string-matching backend types. Values come from two
 * sources:
 * - CalDAV: derived from live RFC 4791 discovery facts via
 *   capabilitiesFromDiscovery().
 * - Every other backend family: static per-type reports
 *   (CapabilityReports), pinned against the EEE edge loss profiles and
 *   docs/campaign/eee/vendor-rest-api-wire-notes.md.
 */
struct CalendarCapabilities
{
    /** VALARM support class. Full = every alarm form survives byte-exact. */
    enum class AlarmSupport {
        None,     ///< no alarm representation at all
        Display,  ///< display-action reminders only (single or limited form)
        Full      ///< all VALARM forms preserved/executable
    };

    /** How properties without a native home survive a round trip. */
    enum class UnknownPropertyPreservation {
        Full,   ///< stored verbatim (raw bytes)
        XOnly,  ///< survives only via an extension/carrier channel
        None    ///< dropped
    };

    AlarmSupport alarms = AlarmSupport::None;

    /// Detached RECURRENCE-ID instances survive end-to-end.
    bool recurrenceExceptions = false;

    /// RANGE=THISANDFUTURE honored (false everywhere in v1: series-split
    /// instead, per handoff W3).
    bool thisAndFuture = false;

    /// Org ++/.+ completion-anchored repeaters representable + executable.
    bool completionAnchoredRepeat = false;

    UnknownPropertyPreservation unknownPropertyPreservation =
        UnknownPropertyPreservation::None;

    /// PRODID-derived where discoverable (CalDAV), else stable backend-type id.
    QString producerId;

    bool operator==(const CalendarCapabilities &other) const
    {
        return alarms == other.alarms
            && recurrenceExceptions == other.recurrenceExceptions
            && thisAndFuture == other.thisAndFuture
            && completionAnchoredRepeat == other.completionAnchoredRepeat
            && unknownPropertyPreservation == other.unknownPropertyPreservation
            && producerId == other.producerId;
    }

    bool operator!=(const CalendarCapabilities &other) const
    { return !(*this == other); }

    QJsonObject toJson() const;
    static CalendarCapabilities fromJson(const QJsonObject &json);
};

/**
 * @brief Map discovered CalDAV facts onto CalendarCapabilities.
 *
 * Derivation pinned by the handoff §W8: recurrenceExceptions=true,
 * thisAndFuture=false, completionAnchoredRepeat=false, alarms=Full,
 * unknownPropertyPreservation=Full; producerId = PRODID/server-sniff from
 * discovery, falling back to "caldav".
 *
 * @param caps per-calendar facts from CalDavCapabilityDiscovery
 * @param contentTypes collection-level content types; accepted for contract
 *        stability with CollectionInfo-shaped callers — current CalDAV facts
 *        do not modulate any field beyond @p caps.
 */
CalendarCapabilities capabilitiesFromDiscovery(
    const PerCalendarCapabilities &caps,
    const QStringList &contentTypes = QStringList());

/**
 * @brief Static per-backend-family capability reports (handoff Q4: static
 *        for everything non-CalDAV). Each pin cites its evidence source.
 */
namespace CapabilityReports {

/// Raw local iCal blob store — bytes verbatim (W7 passthrough table).
CalendarCapabilities localBlob();

/// Generic CalDAV family default (server raw bytes preferred; W7 table).
/// Prefer capabilitiesFromDiscovery() when discovery facts exist.
CalendarCapabilities calDav();

/// Org-mode files — fixed headline/drawer mapping, ++/.+ repeaters native
/// (W7 table: unknown X- props dropped; W4: repeater semantics executable).
CalendarCapabilities orgBackend();

/// Google Calendar v3 events (google-event loss profile + wire notes §1).
CalendarCapabilities googleCalendar();

/// Microsoft Graph v1.0 events (ms-event loss profile + wire notes §3).
CalendarCapabilities msGraphCalendar();

/// Google Tasks API (google-task loss profile: no extension point, O66(c)).
CalendarCapabilities googleTasks();

/// Microsoft Graph todoTask (ms-todotask loss profile: single reminder +
/// open-extension carrier channel, O66 correction).
CalendarCapabilities msGraphTodo();

} // namespace CapabilityReports

} // namespace Kalburator::Sync

#endif // CALENDARCAPABILITIES_H
