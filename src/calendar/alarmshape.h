#pragma once

#include <KCalendarCore/Alarm>
#include <KCalendarCore/Incidence>
#include <QJsonObject>

// IP.4 (incidence-parity campaign) — the single shared VALARM ⇄ canon-JSON
// row shape, used by both VEVENT and VTODO promote/demote (VJOURNAL takes
// no VALARM per RFC 5545 §3.6.3 and never calls into this module).
//
// Row shape (all keys optional except "type"):
//   { type: int,
//     at?: ISO-8601 string,           // absolute trigger (VALUE=DATE-TIME)
//     offset?: int seconds,           // relative trigger, sign per RFC5545
//     related?: "end",                // present only alongside offset, for
//                                      // an end-relative trigger; absent ⇒
//                                      // start-relative
//     text?: string,
//     repeatCount?: int,
//     repeatIntervalSecs?: int }
//
// "at" / ("offset" + "related") / bare "offset" are mutually exclusive —
// this mirrors KCalendarCore::Alarm's own hasTime()/hasEndOffset()/
// hasStartOffset() mutual exclusivity (probe-confirmed 2026-08-28, W5).
//
// This module was extracted verbatim from vtodocanonfields.cpp's W5 block
// (the tested-correct implementation — see
// docs/campaign/vtodo-parity/2026-08-28-w7-passthrough-contract.md and
// tests/todo/tst_todo_canon_roundtrip.cpp) so VEVENT and VTODO share
// exactly one implementation instead of two that happen to agree. Closes
// O79 (trigger-form corruption) and O85 (every alarm round-trips back
// disabled) — see docs/campaign/incidence-parity/PLAN.md IP.4 +
// Amendment §A.3.1.
//
// O85 decision (recorded here per PLAN.md's instruction to record the
// choice either way): `alarmFromJson` unconditionally calls
// `setEnabled(true)`. RFC 5545 has no "disabled alarm" concept — a
// disabled KOrganizer alarm has no wire representation to preserve in the
// first place, so there is nothing for a row key to carry. See the IP.4
// return receipt for the proof this doesn't silently eat a deliberately
// disabled source alarm.
namespace Kalburator::Calendar {

/// Encode one KCalendarCore alarm as a canon JSON row. Never returns a
/// null/default object for a null `alarm` — callers only invoke this over
/// `incidence->alarms()`, which never contains null pointers.
QJsonObject alarmToJson(const KCalendarCore::Alarm::Ptr& alarm);

/// Decode one canon JSON alarm row back into a KCalendarCore alarm parented
/// to `parent` (matches KCalendarCore::Alarm's own constructor contract —
/// pass the owning incidence, NOT a null pointer). The returned alarm is
/// NOT yet added to `parent`'s alarm list; the caller does that.
KCalendarCore::Alarm::Ptr alarmFromJson(const QJsonObject& row, KCalendarCore::Incidence* parent);

/// The trigger form a canon alarm row actually encodes, so a vendor leg can
/// *ask* rather than infer from a possibly-defaulted-zero key — this is
/// precisely the shape of the O79 bug class (mseventcanonstages.cpp read
/// `row.value("offset").toInt()` on an "at"-shaped row, silently got 0, and
/// mapped it to "remind at start").
enum class AlarmRowForm {
    StartRelative,  ///< bare "offset" (no "at", no "related").
    EndRelative,    ///< "offset" + "related" == "end".
    Absolute,       ///< "at" present.
    Malformed,      ///< none of the above — e.g. neither "at" nor "offset".
};

/// Classify a canon alarm row by its actual trigger-form keys.
AlarmRowForm describeAlarmRow(const QJsonObject& row);

}  // namespace Kalburator::Calendar
