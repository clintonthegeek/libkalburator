# W3 — Series-split mechanics + split-association carrier: contract

**Delivered:** 2026-08-28 (implements W3/VP.e; recon:
`2026-08-27-w3-recon-handoff.md`)
**Consumes:** handoff §W3 (binding,
`docs/2026-08-25-vtodo-parity-handoff-response.md`); the W1 identity
contract (`2026-08-26-w1-detached-exceptions-contract.md`), which this
work must not violate — no rename primitive is introduced.
**Status:** BINDING. Mirrors `2026-08-26-w1-detached-exceptions-contract.md`
§5's framing for the parts of this item that are SPECIFIED but not
executed (§5 below).

---

## 1. Canon key + carrier

- New catalogued todo canon key `seriesSplitOf`
  (`PropertyKind::String`, `src/todo/todocanonproperties.cpp`) — the OLD
  master's uid, present only on a series-split NEW master.
- **vtodo/CalDAV leg:** explicit custom property
  `X-CANON-SERIES-SPLIT-OF` (`src/todo/vtodocanonfields.cpp` — promote and
  demote blocks alongside the `descriptionHtml`/`X-ALT-DESC` precedent).
  Loss profile: **Reversible** (`canonToVtodoLoss()`,
  `src/todo/vtodocanonstages.cpp`).
- **MS To-Do leg:** NO handler code. `seriesSplitOf` is deliberately absent
  from the demote's `handled` set
  (`src/todo/mstodotaskcanonstages.cpp`), so it auto-carries through the
  existing unhandled-canon-prop open-extension loop, landing on the wire as
  `x-canon-series-split-of` on a `kalburator.canon` open-type extension row
  — byte-identical to the binding spec's literal carrier-key string
  (verified: `carrierKey("seriesSplitOf")` → `"x-canon-series-split-of"`,
  the kebab-case derivation is name-derived from the canon key, so the
  camelCase spelling is load-bearing). Loss profile: **Reversible**
  (`canonToMsTodoTaskLoss()`).
- **Google Tasks leg:** **Dropped** (`canonToGoogleTaskLoss()`,
  `src/todo/googletaskcanonstages.cpp`) — no extension point of any kind
  exists on this API (O66(c), already established fact for
  `completionAnchor`/W4; not re-litigated here).

## 2. Demote non-emission guarantee (the correctness fix)

`canonObjectToVtodoBytes` (`src/todo/vtodocanonfields.cpp`, the
recurrenceId/recurrenceRange demote block) now **unconditionally** calls
`todo->setThisAndFuture(false)` — RANGE=THISANDFUTURE is NEVER re-emitted
on write, regardless of what canon's `recurrenceRange` key carries. This
is a hard safety backstop independent of whether
`splitSeriesAtInstant()` (§3) was ever invoked: even a raw canon object
that still carries `recurrenceRange: "thisAndFuture"` (e.g. captured
losslessly from a foreign producer's write on promote, or staged directly
by a caller) cannot cause a write-hostile RANGE=THISANDFUTURE to reach a
real CalDAV server through this seam.

- **Promote is unaffected** — canon still losslessly CAPTURES an incoming
  RANGE=THISANDFUTURE (an already-existing foreign producer's write);
  `recurrenceRange` in canon is a purely READ-SIDE fact from here on.
- Loss profile: `recurrenceRange` → **Degraded**
  (`canonToVtodoLoss()`) — the RANGE modifier itself is dropped on write.
  The bare `recurrenceId` exception identity is unaffected and stays fully
  Reversible (catalogued separately, needs no profile row of its own).
- Pinned by `tst_todo_canon_roundtrip.cpp::vtodoDemoteNeverEmitsThisAndFutureRange`
  (replaces the W1-era `vtodoRoundTripPreservesThisAndFutureRange`, which
  pinned the opposite, pre-W3 behavior).
- **VEVENT twin bug — flagged, NOT fixed here.** The identical
  write-hostility bug exists on the calendar/VEVENT side:
  `src/calendar/eventcanonfields.cpp:594-596`
  (`event->setThisAndFuture(range == QStringLiteral("thisAndFuture"))`,
  inside the demote block starting at `:586`) still re-emits
  RANGE=THISANDFUTURE unconditionally whenever canon carries
  `recurrenceRange: "thisAndFuture"`. No test pins either behavior on the
  VEVENT side (`grep -r ThisAndFuture tests/calendar/` — zero hits, both
  before and after this item). This is real, unfixed, and out of scope for
  vtodo-parity's VP.e (todo-only) — a future event-focused pass must fix
  it using the identical unconditional-false pattern applied here.

## 3. The split helper's contract

`Kalburator::Todo::splitSeriesAtInstant()`
(`src/todo/todoseriessplitter.{h,cpp}`) is a **pure, host-invoked**
function — representation + computation ONLY. It is NOT wired into
`SyncEngine`, the differ, or any backend (§5).

**Signature:**
```cpp
struct SeriesSplitResult {
    bool ok = false;
    QString error;                          // non-empty iff !ok
    QJsonObject updatedOldMaster;
    QJsonObject newMaster;
    QList<QJsonObject> rebasedExceptions;
};
SeriesSplitResult splitSeriesAtInstant(const QJsonObject &masterCanon,
                                       const QDateTime &splitInstant,
                                       const QList<QJsonObject> &allExceptions);
```

**Inputs:**
- `masterCanon` — a series MASTER canon object (no `recurrenceId` of its
  own) carrying a `recurrence` array with at least one verbatim RRULE
  line.
- `splitInstant` — the this-and-future edit's boundary instant (a valid
  `QDateTime`).
- `allExceptions` — the master's full exception set, both before AND
  after `splitInstant`. Callers must NOT pre-filter; the function
  partitions internally.

**Outputs on success (`ok == true`):**
- `updatedOldMaster` — the old master, unchanged except its RRULE line's
  `UNTIL=` is tightened to just before `splitInstant`, text-level
  (find/replace the `UNTIL=` token in the verbatim RRULE line string —
  Open decision 6), NEVER loosened past whatever bound the RRULE already
  had (`min(originalUntil, splitInstant - 1)`). Every other recurrence
  line (RDATE, EXDATE, unrelated RRULE params) is untouched. The old
  master's `uid` is unchanged.
- `newMaster` — a fresh canon object: deterministic uid
  `<oldUid>-split-<sanitizedSplitInstantUtcIsoStamp>` (same sanitization
  as `RemoteCalendarBackend::generateItemUrlForCreate`,
  `src/calendar/remotecalendarbackend.cpp:869-879` — strip
  non-alphanumerics from the UTC-ISO stamp; deterministic so a
  retried/idempotent split call reproduces the same identity); all other
  fields copied from `masterCanon` except `recurrence` (replaced by the
  ORIGINAL, untightened RRULE line only — "copied RRULE remainder"),
  `start`/`due` (whichever were present, retimed to `splitInstant`,
  preserving the original's date-only-vs-date-time/tz/floating shape),
  `seriesSplitOf` (set to the old master's uid), and
  `recurrenceId`/`recurrenceRange` (absent — a master never carries them,
  mirrors `vtodoMasterHasNoRecurrenceId`).
- `rebasedExceptions` — the subset of `allExceptions` whose
  `recurrenceId` is at/after `splitInstant` (UTC comparison), each a
  plain canon object with `uid` rewritten to the new master's uid and
  `recurrenceId` unchanged (same instant, same value). This is **not a
  rename** — the W1 composite-identity contract
  (`src/sync/recordidentity.h`) has no in-place rename primitive; a
  rebased exception is structurally a new record under a new composite
  identity. Exceptions before `splitInstant`, or lacking a parseable
  `recurrenceId`, are excluded entirely (not returned at all — they stay
  keyed to the old master, untouched by this function).

**Error cases (`ok == false`, `error` non-empty, all other fields
empty):**
- `masterCanon` has no `uid`.
- `masterCanon` carries a `recurrenceId` (it is a detached exception, not
  a master — this function operates on masters only).
- `splitInstant` is invalid.
- `masterCanon.recurrence` has no RRULE line.
- The RRULE line is **COUNT-bounded** (`COUNT=` present) — v1 does not
  attempt COUNT recomputation (Open decision 5: a correct recompute needs
  `KCalendarCore::Recurrence::timesInInterval()` occurrence-walking
  machinery and its own test matrix; guessing risks silent
  over-generation of future occurrences on the new master).

## 4. Intended host-side realization sequence

The library computes; **the host decides when to call this and executes
the write-out.** No engine/backend code performs any of this
automatically (Open decisions 1 and 9). The intended sequence, reusing the
exact create/delete href mechanics already proven for detached-exception
creates (W1 contract §7,
`RemoteCalendarBackend::generateItemUrlForCreate`/`applyRecords()`):

1. Host detects a this-and-future edit (its own UI/business logic — NOT
   this library's job) and computes `splitSeriesAtInstant(oldMaster,
   splitInstant, exceptions)`.
2. **Update** the old master (`updatedOldMaster`) at its existing
   composite/bare record id — an ordinary update, same href.
3. **Create** the new master (`newMaster`) — an ordinary create; the
   backend mints its href from the fresh `uid` exactly as it would for any
   other new master (no composite-id involvement — a master is always a
   bare uid per the W1 contract §1).
4. For each entry in `rebasedExceptions`: **delete** the OLD composite
   record (`composeRecordIdentity(oldUid, recurrenceId)`) and **create**
   the new one (`composeRecordIdentity(newMaster.uid, recurrenceId)`) —
   the distinct-href-minting path already proven by
   `detachedException_applyRecordsCreate_mintsDistinctHref` (W1 contract
   §7) covers this create; the delete uses the ordinary exception-delete
   path (`detachedException_deleteRecord_removesOnlyExceptionHref`).

This is `1 update + 1 create + N (delete + create)` — ordinary
per-record backend operations end to end; no new backend code is required
(confirmed against `RemoteCalendarBackend::applyRecords()`'s
create/update/delete loops, `remotecalendarbackend.cpp:3186+`).

## 5. What W3 does NOT promise (engine/transport atomicity gap)

Mirrors `2026-08-26-w1-detached-exceptions-contract.md` §5's identical
framing for the simpler cascade-delete case — a series split is a
**strictly harder** multi-record operation and inherits the same
limitation with more force:

- **No cross-record transaction at the transport layer.** A split's
  write-out is `1 update + 1 create + N (delete+create)` across
  possibly-independent HTTP calls (CalDAV). `BaselineStore::transaction()`
  (`src/storage/baselinestore.h:203-216`) wraps LOCAL BASELINE bookkeeping
  atomically (as the engine's W2 persist loop already does for a simpler
  master-edit+exception-create pair) — it says nothing about the remote
  transport, which has no equivalent primitive.
- **Declared failure mode:** a partial failure mid-split (e.g. the old
  master's UNTIL update lands but the new master's create fails, or a
  rebased exception's delete succeeds while its create fails) leaves a
  genuinely inconsistent remote state — for example, the series can have
  a gap where no instances exist for `[UNTIL, ...)` until the new master
  successfully lands, or a rebased exception can vanish (deleted from the
  old master, never created under the new one).
- **Retry/reconciliation policy is host-side**, by design (Part IV
  ethics, `2026-08-24-reconnaissance-assessment-and-roadmap.md`: the
  library states facts and limits; judgment stays host-side). The
  deterministic new-master uid (§3) makes a full retry of steps 2-3
  idempotent at the create-or-update-by-id level; retrying step 4 for an
  already-migrated exception should be idempotent for the same reason
  (delete of an already-deleted composite id is a graceful no-op per the
  existing backend contract).
- **No partial-failure rollback logic is attempted inside this item's
  scope.** A correct implementation would need something like a
  saga/compensating-transaction layer, which does not exist anywhere else
  in this codebase either (matches the W1 contract's identical
  declaration for cascade-delete).

## 6. Public API / headers

- `src/todo/todoseriessplitter.h` — `SeriesSplitResult`,
  `splitSeriesAtInstant()`.
- `src/calendar/recurrencepatternconverter.h` — `parseRruleParts()` newly
  exported (was file-local/anonymous-namespace) so the split helper reuses
  the existing RRULE `KEY=VALUE` parser rather than duplicating it (Open
  decision 6).
- Todo canon key `seriesSplitOf` (`PropertyKind::String`).
