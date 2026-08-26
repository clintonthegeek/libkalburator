#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QStringList>

namespace Kalburator::Calendar {

/// Component-scoped extraction of verbatim RRULE/RDATE/EXDATE lines
/// (invariant 3 — recurrence is custody, not interpretation, never
/// re-derived from parsed parts).
///
/// Scans `icalBytes` and collects recurrence lines only from `componentName`
/// components (e.g. "VEVENT", "VTODO", "VJOURNAL") whose own UID line matches
/// `uid`. Never descends into `VTIMEZONE` (whose DST-transition
/// sub-components can carry their own RRULE — the N1 corruption: a whole-blob
/// line scan harvests those as if they were event recurrence) nor into a
/// nested `VALARM`. Folded continuation lines (RFC 5545 §3.1) are unfolded
/// before matching so a long RRULE split across physical lines is not
/// mangled.
///
/// When several same-uid components of the requested kind are present (a
/// recurring master plus RECURRENCE-ID override instances), the block
/// WITHOUT a RECURRENCE-ID line — the master, which alone carries the
/// authoritative recurrence rule — is preferred. If no such master-shaped
/// block exists, every matching block's recurrence lines are concatenated
/// (defensive fallback; does not occur for well-formed input).
///
/// Pass a non-empty `recurrenceIdUtc` to select a SPECIFIC override block
/// instead: the block whose UID matches AND whose RECURRENCE-ID value,
/// normalized to UTC ISO-8601, equals `recurrenceIdUtc` (compare like with
/// like — compose the selector from the same normalization, e.g.
/// QDateTime::toUTC().toString(Qt::ISODate)). In this mode only that block's
/// lines are returned; an empty result means no block matched (no fallback
/// to master preference — the caller asked for a specific instance).
/// The default (empty selector) keeps the master-preference behavior.
QStringList extractComponentRecurrenceLines(const QByteArray &icalBytes,
                                             QByteArrayView componentName,
                                             const QString &uid,
                                             const QString &recurrenceIdUtc = {});

}  // namespace Kalburator::Calendar
