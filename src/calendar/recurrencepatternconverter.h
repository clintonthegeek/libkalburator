#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace Kalburator::Calendar {
namespace RecurrencePattern {

/// RRULE line "RRULE:FREQ=DAILY;INTERVAL=2" → {{"FREQ","DAILY"},{"INTERVAL","2"}}.
/// Exported (W3) so other modules needing verbatim RRULE KEY=VALUE parts
/// (e.g. todoseriessplitter's text-level UNTIL rewrite) reuse this parser
/// rather than duplicating it — avoids parser drift between two copies.
/// Empty hash when `line` is not an "RRULE:..." line.
QHash<QString, QString> parseRruleParts(const QString& line);

/// Result of demoting canon RFC5545 lines to a Graph patternedRecurrence.
struct DemoteResult {
    /// {pattern:{...}, range:{...}} — empty when no RRULE was present.
    QJsonObject patternedRecurrence;
    /// RFC5545 lines that cannot be represented by Graph and are carried
    /// verbatim (via singleValueExtendedProperties) so a re-promote is
    /// byte-identical. Includes: extra RRULE lines, EXRULE, RDATE,
    /// and the FULL original RRULE whenever any of its features was reduced
    /// to the nearest representable pattern.
    QStringList carriedLines;

    /// EXDATE values parsed to UTC ISO datetimes — the caller maps these to
    /// Graph cancelledOccurrences[] (reference §1.3: "EXDATE → materialized
    /// cancelledOccurrences").
    QStringList exdates;
};

/// Canon `recurrence` lines → Graph patternedRecurrence (the lossy direction;
/// declared per docs/2026-08-23-ms-event-edge-loss-profile.md).
///
/// `dtstartDate` (yyyy-MM-dd or full ISO datetime) seeds range.startDate —
/// Graph requires it and RRULE lines alone do not carry DTSTART. When empty,
/// "1970-01-01" is used.
DemoteResult rruleLinesToPatternedRecurrence(const QStringList& lines,
                                             const QString& dtstartDate = {});

/// Graph patternedRecurrence + master cancelledOccurrences[] → canon
/// `recurrence` RFC5545 lines. Lossless per reference §1.3:
///   - pattern/range fields map 1:1 (index → BYSETPOS, endDate → UNTIL,
///     numberOfOccurrences → COUNT, noEnd → neither)
///   - zero-sentinel numerics (dayOfMonth/month/numberOfOccurrences == 0)
///     are treated as absent (FINDINGS O57(e)); missing values derive from
///     range.startDate where the rule needs them
///   - `index` on non-relative patterns is ignored (FINDINGS O57(f))
///   - cancelledOccurrences entries parseable as ISO date-times become
///     EXDATE lines; unparseable entries (raw MAPI occurrence ids) are
///     appended to `unparsedCancellations` so the caller can stash them.
QStringList patternedRecurrenceToRruleLines(const QJsonObject& patternedRecurrence,
                                            const QStringList& cancelledOccurrences = {},
                                            QStringList* unparsedCancellations = nullptr);

} // namespace RecurrencePattern
} // namespace Kalburator::Calendar
