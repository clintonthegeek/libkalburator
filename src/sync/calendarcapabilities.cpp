#include "calendarcapabilities.h"

namespace Kalburator::Sync {

// ============================================================================
// CalendarCapabilities implementation
// ============================================================================

QJsonObject CalendarCapabilities::toJson() const
{
    QJsonObject json;
    json[QStringLiteral("alarms")] = static_cast<int>(alarms);
    json[QStringLiteral("recurrenceExceptions")] = recurrenceExceptions;
    json[QStringLiteral("thisAndFuture")] = thisAndFuture;
    json[QStringLiteral("completionAnchoredRepeat")] = completionAnchoredRepeat;
    json[QStringLiteral("unknownPropertyPreservation")] =
        static_cast<int>(unknownPropertyPreservation);
    if (!producerId.isEmpty()) {
        json[QStringLiteral("producerId")] = producerId;
    }
    return json;
}

CalendarCapabilities CalendarCapabilities::fromJson(const QJsonObject &json)
{
    CalendarCapabilities caps;
    caps.alarms = static_cast<AlarmSupport>(
        json.value(QStringLiteral("alarms")).toInt(
            static_cast<int>(AlarmSupport::None)));
    caps.recurrenceExceptions =
        json.value(QStringLiteral("recurrenceExceptions")).toBool(false);
    caps.thisAndFuture =
        json.value(QStringLiteral("thisAndFuture")).toBool(false);
    caps.completionAnchoredRepeat =
        json.value(QStringLiteral("completionAnchoredRepeat")).toBool(false);
    caps.unknownPropertyPreservation =
        static_cast<UnknownPropertyPreservation>(
            json.value(QStringLiteral("unknownPropertyPreservation")).toInt(
                static_cast<int>(UnknownPropertyPreservation::None)));
    caps.producerId = json.value(QStringLiteral("producerId")).toString();
    return caps;
}

// ============================================================================
// capabilitiesFromDiscovery — CalDAV facts → struct (handoff §W8)
// ============================================================================

CalendarCapabilities capabilitiesFromDiscovery(
    const PerCalendarCapabilities &caps, const QStringList &contentTypes)
{
    Q_UNUSED(contentTypes);  // contract-stability parameter; see header

    CalendarCapabilities c;
    c.alarms = CalendarCapabilities::AlarmSupport::Full;
    c.recurrenceExceptions = true;
    c.thisAndFuture = false;
    c.completionAnchoredRepeat = false;
    c.unknownPropertyPreservation = CalendarCapabilities::UnknownPropertyPreservation::Full;
    c.producerId = !caps.producerId.isEmpty() ? caps.producerId
                                              : QStringLiteral("caldav");
    return c;
}

// ============================================================================
// CapabilityReports — static per-backend-family pins
// ============================================================================

CalendarCapabilities CapabilityReports::localBlob()
{
    // W7 passthrough table row LocalBlob: X-props/VALARM/VTIMEZONE preserved
    // verbatim, bytes verbatim.
    CalendarCapabilities c;
    c.alarms = CalendarCapabilities::AlarmSupport::Full;
    c.recurrenceExceptions = true;
    c.thisAndFuture = false;
    c.completionAnchoredRepeat = false;
    c.unknownPropertyPreservation = CalendarCapabilities::UnknownPropertyPreservation::Full;
    c.producerId = QStringLiteral("local-blob");
    return c;
}

CalendarCapabilities CapabilityReports::calDav()
{
    // W7 table row CalDAV: server raw bytes preferred over re-serialization.
    // Same derivation as the discovery mapping minus per-server producer id.
    return capabilitiesFromDiscovery(PerCalendarCapabilities{});
}

CalendarCapabilities CapabilityReports::orgBackend()
{
    // W7 table row Org: fixed headline/drawer mapping, unknown X- props
    // DROPPED; W4 decision: ++/.+ repeater semantics representable and
    // executable by the host.
    CalendarCapabilities c;
    c.alarms = CalendarCapabilities::AlarmSupport::None;
    c.recurrenceExceptions = false;
    c.thisAndFuture = false;
    c.completionAnchoredRepeat = true;
    c.unknownPropertyPreservation = CalendarCapabilities::UnknownPropertyPreservation::None;
    c.producerId = QStringLiteral("orgmode");
    return c;
}

CalendarCapabilities CapabilityReports::googleCalendar()
{
    // google-event loss profile + wire notes §1:
    // - alarms Display: reminders.overrides[] carries display/email actions
    //   with minutes triggers (O59); absolute/audio forms are carried only.
    // - recurrenceExceptions TRUE: recurrenceId/recurrenceRange lossless ⇄
    //   recurringEventId + originalStartTime (profile §1.4); A4 live
    //   checkpoint passed both directions incl. the event-instances fixture.
    // - unknown XOnly: extendedProperties.private x-canon-* carriers are the
    //   one live-Reversible channel (O59/O67 checkpoint; wire notes §4.4).
    CalendarCapabilities c;
    c.alarms = CalendarCapabilities::AlarmSupport::Display;
    c.recurrenceExceptions = true;
    c.thisAndFuture = false;
    c.completionAnchoredRepeat = false;
    c.unknownPropertyPreservation = CalendarCapabilities::UnknownPropertyPreservation::XOnly;
    c.producerId = QStringLiteral("google-calendar");
    return c;
}

CalendarCapabilities CapabilityReports::msGraphCalendar()
{
    // ms-event loss profile + wire notes §3:
    // - alarms Display: single isReminderOn + reminderMinutesBeforeStart.
    // - recurrenceExceptions FALSE for v1: exception records promote fine,
    //   but "v1 writes flat events + masters; exceptions expand read-only"
    //   (profile, Out of scope) — a written detached instance does not land.
    // - unknown None: SVEP carriers stripped on consumer creates (O61(e)) —
    //   offline-only channel does not survive sync traffic.
    CalendarCapabilities c;
    c.alarms = CalendarCapabilities::AlarmSupport::Display;
    c.recurrenceExceptions = false;
    c.thisAndFuture = false;
    c.completionAnchoredRepeat = false;
    c.unknownPropertyPreservation = CalendarCapabilities::UnknownPropertyPreservation::None;
    c.producerId = QStringLiteral("msgraph-calendar");
    return c;
}

CalendarCapabilities CapabilityReports::googleTasks()
{
    // google-task loss profile: no extension point of any kind (O66(c));
    // alarms/recurrence declared Dropped.
    CalendarCapabilities c;
    c.alarms = CalendarCapabilities::AlarmSupport::None;
    c.recurrenceExceptions = false;
    c.thisAndFuture = false;
    c.completionAnchoredRepeat = false;
    c.unknownPropertyPreservation = CalendarCapabilities::UnknownPropertyPreservation::None;
    c.producerId = QStringLiteral("google-tasks");
    return c;
}

CalendarCapabilities CapabilityReports::msGraphTodo()
{
    // ms-todotask loss profile + O66 correction: single reminder ⇄ alarms[0];
    // open-extension carrier via nav POST survives live (Reversible).
    CalendarCapabilities c;
    c.alarms = CalendarCapabilities::AlarmSupport::Display;
    c.recurrenceExceptions = false;
    c.thisAndFuture = false;
    c.completionAnchoredRepeat = false;
    c.unknownPropertyPreservation = CalendarCapabilities::UnknownPropertyPreservation::XOnly;
    c.producerId = QStringLiteral("msgraph-todotask");
    return c;
}

} // namespace Kalburator::Sync
