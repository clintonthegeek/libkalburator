#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace Kalburator::Todo {

/// W3 (VP.e) — pure, host-invoked series-split computation.
///
/// RANGE=THISANDFUTURE is write-hostile on real CalDAV servers; the
/// library NEVER emits it on write (see vtodocanonfields.cpp's demote
/// seam). Instead, a this-and-future edit is realized as a SERIES SPLIT:
/// the old master's RRULE is tightened to end just before the split
/// instant, a brand-new master starts at the split instant carrying the
/// RRULE remainder, and any exceptions at/after the split instant are
/// rebased onto the new master's uid.
///
/// This function is representation + pure computation ONLY — it is NOT
/// wired into SyncEngine, the differ, or any backend. Deciding WHEN to
/// split (detecting "the user just made a this-and-future edit") and
/// REALIZING the result as backend operations (update old master / create
/// new master / delete+create each rebased exception) are host
/// responsibilities. See
/// docs/campaign/vtodo-parity/2026-08-27-w3-series-split-contract.md for
/// the full contract, including the declared engine/transport atomicity
/// gap.
///
/// Operates entirely on canon JSON objects already in memory (never raw
/// ICS bytes or KCalendarCore incidences) — this preserves invariant 3
/// (verbatim RRULE lines) by construction: the only text edit performed
/// is a find/replace of the `UNTIL=` token inside the master's existing
/// RRULE line string. Every other recurrence line (RDATE, EXDATE,
/// unrelated RRULE params) is left untouched on the old master; the new
/// master carries only its own (verbatim-copied) RRULE line.
struct SeriesSplitResult {
    bool ok = false;

    /// Non-empty iff !ok — e.g. the master's RRULE is COUNT-bounded (v1
    /// does not attempt COUNT recomputation — see the contract doc).
    QString error;

    /// The old master's canon object, unchanged except its RRULE line's
    /// UNTIL= is tightened to just before `splitInstant` (never loosened
    /// past whatever bound the RRULE already had — see
    /// splitSeriesAtInstant()). Empty when !ok.
    QJsonObject updatedOldMaster;

    /// A fresh master canon object: deterministic uid
    /// `<oldUid>-split-<sanitizedSplitInstantUtcIsoStamp>` (same
    /// sanitization algorithm as
    /// RemoteCalendarBackend::generateItemUrlForCreate, so a retried split
    /// call is idempotent); all other fields copied from `masterCanon`
    /// except: `recurrence` is replaced by the ORIGINAL (untightened)
    /// RRULE line only, `start`/`due` (whichever were present) are
    /// retimed to `splitInstant`, `seriesSplitOf` is set to the old
    /// master's uid, and `recurrenceId`/`recurrenceRange` are absent (a
    /// master never carries them — mirrors
    /// vtodoMasterHasNoRecurrenceId). Empty when !ok.
    QJsonObject newMaster;

    /// The subset of `allExceptions` whose recurrenceId is at/after
    /// `splitInstant`, each a plain canon object with `uid` rewritten to
    /// the new master's uid and `recurrenceId` unchanged (same instant,
    /// same value) — NOT a rename: the W1 composite-identity contract has
    /// no in-place rename primitive, so this is structurally a new record
    /// under a new composite identity; realizing it as
    /// delete(oldComposite)+create(newComposite) is the host's job.
    /// Exceptions before `splitInstant`, or lacking a parseable
    /// recurrenceId, are excluded (they stay keyed to the old master,
    /// untouched — not returned here at all). Empty when !ok.
    QList<QJsonObject> rebasedExceptions;
};

/// Compute a this-and-future series split.
///
/// `masterCanon` must be a master (no `recurrenceId` of its own) carrying
/// a `recurrence` array with at least one verbatim RRULE line; anything
/// else is a fail-loud `ok=false`.
///
/// `allExceptions` may contain exceptions both before AND after
/// `splitInstant` — the function partitions internally and returns only
/// the rebased (>= splitInstant) subset. Callers should NOT pre-filter.
SeriesSplitResult splitSeriesAtInstant(const QJsonObject &masterCanon,
                                       const QDateTime &splitInstant,
                                       const QList<QJsonObject> &allExceptions);

}  // namespace Kalburator::Todo
