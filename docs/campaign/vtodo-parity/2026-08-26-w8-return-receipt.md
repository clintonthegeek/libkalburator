# Return receipt — W8 unified capability/trait query API

**Delivered:** 2026-08-26 (post-B2C P3; commit `7833a0c`)
**Consumes:** handoff §W8; response doc §1 W8.

## Public headers / API (exact)

`src/sync/calendarcapabilities.h` — namespace `Kalburator::Sync`:

```cpp
struct CalendarCapabilities {
    enum class AlarmSupport { None, Display, Full };
    enum class UnknownPropertyPreservation { Full, XOnly, None };
    AlarmSupport alarms;
    bool recurrenceExceptions;
    bool thisAndFuture;
    bool completionAnchoredRepeat;
    UnknownPropertyPreservation unknownPropertyPreservation;
    QString producerId;
    bool operator==(…);            // all fields
    QJsonObject toJson() const;
    static CalendarCapabilities fromJson(const QJsonObject &);
};

CalendarCapabilities capabilitiesFromDiscovery(
    const PerCalendarCapabilities &, const QStringList &contentTypes = {});

namespace CapabilityReports {
    CalendarCapabilities googleCalendar();
    CalendarCapabilities msGraphCalendar();
    CalendarCapabilities googleTasks();
    CalendarCapabilities msGraphTodo();
    CalendarCapabilities orgBackend();
    CalendarFunctions localBlob();      // → localBlob()
    CalendarCapabilities caldavDefault();
}
```

Exposure on discovered collections: `DiscoveredCalendar::setCapabilities()`
/ `capabilities()` — typed pair backed by `metadata["capabilities"]`
(JSON), zero constructor/serialization breakage. Wired into
GoogleCalendarBackend, MSGraphCalendarBackend, GoogleTasksBackend,
GraphTodoTaskBackend, RemoteCalendarBackend (derives via
capabilitiesFromDiscovery). Org/local have no discovery surface — hosts
use the `CapabilityReports` statics directly.

## Discovery extensions

- `PerCalendarCapabilities` gained additive `producerId` +
  `supportsSyncCollection` fields with JSON round-trip (existing keys
  untouched; old .kalb files load unchanged).
- CalDAV Depth-1 PROPFIND now parses `supported-report-set`
  (sync-collection detection) and extracts PRODID: explicit `<prodid>`
  element anywhere in the multistat > known-product sniff (body + HTTP
  Server header) > stable `"caldav"` fallback. PRODID has no standard
  RFC 4791 exposure — this is deliberately best-effort.

## Behavior contracts / edge cases decided

- **Value corrections vs the original proposal** (evidence-pinned):
  - googleCalendar `recurrenceExceptions` = **true** — google-event loss
    profile declares recurrenceId/originalStartTime lossless
    (recurringEventId); A4 live checkpoint passed both directions.
  - googleCalendar `unknownPropertyPreservation` = **XOnly** —
    extendedProperties.private x-canon-* is the one live-Reversible
    carrier.
  - msGraphCalendar `recurrenceExceptions` = **false** — v1 writes flat
    events + masters; exceptions expand READ-ONLY (ms-event profile);
    flips when write-back lands.
  - localBlob + calDAV `alarms` = **Full** — bytes-verbatim passthrough
    preserves every alarm form (W7 table).
- `thisAndFuture` = false for every v1 backend (series-split strategy,
  response doc §W3); flag exists so a future server-honoring backend can
  flip without API churn.
- orgBackend is the only `completionAnchoredRepeat=true` family.
- `contentTypes` param on capabilitiesFromDiscovery accepted for
  contract stability; modulates nothing today (documented in header).
- Legacy dead `struct CalendarCapabilities` in backendcapabilities.h
  (unused near-duplicate, audit residue) was REMOVED so the name belongs
  to this contract — grep confirms zero prior usages; deprecation-free.

## Tests proving acceptance

`tst_calendar_capabilities` (19 slots): per-family value pins citing
loss-profile/wire-notes evidence; JSON round-trip across all enum
values; derivation from a .kalb-shaped persisted discovery fixture;
DiscoveredCalendar metadata round-trip; 4 live-shape discovery slots
(PRODID extraction, product-sniff fallback, sync-collection detection).
Full suite green excluding the 4 documented Radicale-environmental slots.

## Deprecations affecting PlanStan callers

None. Additive header + additive discovery fields + additive DTO accessors.
PlanStan can replace `RecurrenceCapabilities` string-matching by consuming
`DiscoveredCalendar::capabilities()` / `CapabilityReports::*` at its next
pin bump. Editor module visibility/loss-warning wiring per your §W8 plan.
