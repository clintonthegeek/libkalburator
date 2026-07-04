#pragma once

#include <QByteArray>
#include <QDateTime>

namespace Kalburator::Calendar {

/// Extract an authoritative modification timestamp from raw iCal bytes,
/// preferring (in order) LAST-MODIFIED, then DTSTAMP, then CREATED. Returns
/// an invalid QDateTime when none of the three properties is present or
/// parseable — callers must NOT default to "now" in that case (N3 fix):
/// an invalid timestamp keeps LastWriteWins' "valid > invalid" modify-delete
/// rule meaningful, whereas stamping "now" makes every remote record look
/// freshly modified on every load and defeats the LWW tie-bias fix (v0.64).
QDateTime extractICalTimestamp(const QByteArray &icalBytes);

}  // namespace Kalburator::Calendar
