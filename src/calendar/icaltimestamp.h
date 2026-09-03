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

/// Extract exactly one named timestamp property's raw textual value from
/// iCal bytes (e.g. "CREATED", "LAST-MODIFIED") — no fallback to any other
/// property. Returns an invalid QDateTime when that specific property is
/// not literally present, or is present but unparseable.
///
/// Exists because KCalendarCore::Incidence's created()/lastModified()
/// accessors return a construction-time default (effectively "now") when
/// the parsed source lacks an explicit CREATED/LAST-MODIFIED property —
/// they do not distinguish "explicitly zero/absent" from "never set". A
/// canon encoder that trusts those accessors directly re-derives a
/// different "now" on every independent re-parse of byte-identical source
/// text, which defeats change detection for any event lacking those
/// properties (the same class of bug as N3, just at the canon-encode layer
/// instead of blobRecordFromIcal). Callers needing "was this literally
/// stamped in the source" must check presence via this function rather
/// than trusting the parsed object's accessor.
QDateTime extractICalPropertyLiteral(const QByteArray &icalBytes,
                                      const QString &propertyName);

/// Remove a named property's line (and its line terminator) from serialized
/// iCal bytes, wherever it appears. No-op if the property isn't present.
///
/// Exists for the write-side twin of the bug extractICalPropertyLiteral
/// documents (O41): KCalendarCore::Incidence's created()/lastModified()
/// always hold a valid construction-time "now" — there is no API to leave
/// them unset — so KCalendarCore::ICalFormat::toICalString() unconditionally
/// stamps CREATED/LAST-MODIFIED into the serialized bytes even when the
/// canon record never had that field. Canon→ical materialization must call
/// this to strip the injected default when the corresponding canon key was
/// absent, or the write side silently manufactures data the read side
/// (which trusts literal presence only) will disagree about forever.
QByteArray stripICalPropertyLine(const QByteArray &icalBytes,
                                  const QString &propertyName);

/// Remove a single named PARAMETER (e.g. "X-UID") from every occurrence of a
/// named PROPERTY's line (e.g. "ATTENDEE"), leaving the rest of that line —
/// its other parameters and its value — untouched. A different granularity
/// from stripICalPropertyLine: that helper deletes an entire line, which
/// would destroy an ATTENDEE's real content; this one deletes only the
/// ";PARAM=value" substring.
///
/// Exists for O90 (incidence-parity IP.12): KCalendarCore::ICalFormat stamps
/// a heap-address-derived "X-UID" parameter into every serialized ATTENDEE
/// line, so two demotes of byte-identical canon in two different processes
/// produce different bytes. Verified ATTENDEE-only (KCalendarCore::Attendee
/// carries a uid() property that defaults to a heap-derived value when
/// unset and is what gets serialized as X-UID; KCalendarCore::Person, which
/// backs ORGANIZER, has no such property and never emits an X-UID).
///
/// RFC 5545 §3.1 line folding can place PROPERTY's parameters across several
/// physical lines (CRLF followed by a single SPACE/HTAB introduces a
/// continuation) — ATTENDEE lines routinely fold. The match tolerates a fold
/// occurring anywhere inside the targeted PROPERTY's own logical line,
/// including between the removed parameter's ";" and its name, or inside
/// its value, without touching folds anywhere else in the document.
QByteArray stripICalPropertyParameter(const QByteArray &icalBytes,
                                       const QString &propertyName,
                                       const QString &parameterName);

}  // namespace Kalburator::Calendar
