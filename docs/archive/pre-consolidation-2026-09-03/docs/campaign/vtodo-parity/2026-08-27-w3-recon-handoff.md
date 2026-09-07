# W3 (VP.e) — series-split mechanics + split-association carrier — RECON & handoff

**Status:** EXPLORATION COMPLETE 2026-08-27; implementation NOT started.
This doc persists the code map so a fresh agent picks up W3 without
re-exploring. Decisions come from
`docs/2026-08-25-vtodo-parity-handoff-response.md` §W3 (binding), read
alongside the W1 identity contract
(`docs/campaign/vtodo-parity/2026-08-26-w1-detached-exceptions-contract.md`,
binding) which this work must not violate. Written in the style of, and as
a direct sibling to, `2026-08-26-w4-recon-handoff.md` (W4, implemented —
see `2026-08-27-w4-return-receipt.md`).

**HEADLINE FINDING (read this first):** a test landed as part of W1
currently **pins the opposite of the W3 binding spec**. See "Open decision
1" and the Code map §1 — this is not a hypothetical risk, it is live,
green, checked-in code that W3 must knowingly break and rewrite.

---

## What W3 is (per the response doc, binding)

Verbatim, `docs/2026-08-25-vtodo-parity-handoff-response.md` §"W3 — This-
and-future — **ACCEPTED: capability-gated series split**" (lines 143–154):

> RANGE=THISANDFUTURE is write-hostile on real servers; we will NOT emit it
> even where discovered. Strategy: series split everywhere (master ends
> UNTIL<N>; new master starts at N with copied RRULE remainder);
> re-association via a Reversible carrier row linking new master ← old uid
> (`x-canon-series-split-of`), so your editor MAY display them as one
> logical series where the carrier survived, else as two (honest fallback).
> Capability flag `thisAndFuture` stays false for every v1 backend; flag
> exists so a future server-honoring backend can flip it without API churn.
> No orphaned exceptions: split regenerates exceptions with start ≥ N onto
> the new master (RECURRENCE-ID rebased), asserted by test.

Sequencing (§2, item 6): "**VP.e (W3)** — split mechanics + carrier
association." STATUS.md VP.e row: "W3 series-split mechanics +
split-association carrier — not started."

### My reading of scope (confirm-or-flag, per the task)

- **In scope:** (a) stop emitting `RANGE=THISANDFUTURE` on VTODO/CalDAV
  write-out, unconditionally; (b) a new catalogued canon key
  `seriesSplitOf` (String, old-master uid) that carries as
  `x-canon-series-split-of` on the MS To-Do leg (Reversible) and is Dropped
  on Google Tasks (no extension point, O66(c) precedent) and rides a new
  explicit `X-CANON-SERIES-SPLIT-OF` custom property on the vtodo/CalDAV
  leg (Reversible); (c) a pure, testable **split helper** that computes
  {updated old master, new master, rebased exceptions} from an old master +
  a split instant + its exception set; (d) a test asserting no orphaned
  exceptions (rebase onto the new master, per the binding spec's last
  sentence).
- **NOT in scope (my read, flag if you disagree):** the split helper is
  **not** wired into the automatic sync/diff/engine pipeline. Nothing in
  today's engine architecture performs multi-record atomic operations
  triggered by a single incoming record (W1 §5 already declared
  "engine-level uid-family propagation/cascade remain SPECIFIED-not-
  executed" for the simpler master-delete-cascades-to-exceptions case; a
  series split is a **strictly harder** multi-record operation — one
  update + one create + N delete/create pairs — so the same discipline
  applies with more force). The library's job, matching the W2/W4
  precedent ("caller stages it," "engine never mutates data on diff"), is
  to provide the split **computation** as a callable, and to make the
  **wire representation** (carrier, capability flag, non-emission
  guarantee) correct — not to autonomously detect "this is a this-and-
  future edit" and perform surgery on a live sync. See Open decision 1.
- **VEVENT:** the identical write-hostility bug exists in
  `eventcanonfields.cpp` (Code map §1) with no pinned test either way. The
  campaign is "vtodo-parity" scoped and STATUS.md's VP.e row is
  todo-only; I recommend fixing VTODO only for this item and **explicitly
  flagging** the event-side twin as a known, real, unfixed bug for a
  future pass (not silently ignoring it — see Open decision 8).

---

## Design sketch (decide before coding)

1. **Kill the write-hostility bug first (small, independent, testable
   immediately).** In `canonObjectToVtodoBytes`
   (`src/todo/vtodocanonfields.cpp:571-588`), the demote currently does
   `todo->setThisAndFuture(range == QStringLiteral("thisAndFuture"))` —
   i.e. it **re-emits `RANGE=THISANDFUTURE`** whenever canon carries
   `recurrenceRange: "thisAndFuture"`. Change this to **never** set
   `thisAndFuture(true)` on demote (hard-code `false`, or simply delete the
   call — `setRecurrenceId(dt)` alone is fine and still correct for a
   plain detached exception). Read-side (promote, `:265-280`) is untouched
   — canon must still losslessly represent an *incoming* RANGE=THISANDFUTURE
   from a foreign producer; only the *outgoing* re-emission is forbidden.
2. **Fix the loss profile + the now-contradicted pinned test in the same
   commit** (house rule: behavior change + its pin together).
   `canonToVtodoLoss()` (`src/todo/vtodocanonstages.cpp:65-91`): add
   `recurrenceRange` → `LossKind::Degraded` (the RANGE modifier itself is
   dropped on write; the bare `recurrenceId` exception identity is
   unaffected and still Reversible via the existing `recurrenceId`
   catalogue entry, which needs no profile row since it round-trips
   losslessly on its own). Rewrite
   `vtodoRoundTripPreservesThisAndFutureRange`
   (`tests/todo/tst_todo_canon_roundtrip.cpp:430-464`) — see Open decision
   1 for exact required assertion flips.
3. **Catalogue the carrier key.** `src/todo/todocanonproperties.cpp`, add
   after the `recurrenceRange` line (`:40`):
   ```cpp
   cat.addProperty({ PropertyId{"seriesSplitOf"}, PropertyKind::String, QStringLiteral("Series Split Of") });
   ```
   **The name must be exactly `seriesSplitOf`** (camelCase) — see Code map
   §5 for why (the MS carrier-key kebab-converter is name-derived and must
   produce `x-canon-series-split-of` byte-for-byte to match the binding
   spec).
4. **vtodo/CalDAV leg — explicit custom-prop carrier** (this key is
   canon-only; it does NOT arrive as a pre-existing X-prop on the wire the
   way `providerExtras["x-vtodo"]` round-trips *unknown* props, so it needs
   its own promote+demote block, mirroring the `X-ORG-REPEATER` (W4,
   `vtodocanonfields.cpp:305-312`) and `X-ALT-DESC`
   (`vtodocanonfields.cpp:432-436`, promote at `:190-194`) precedents):
   - Promote: `const QString splitOf = todo->nonKDECustomProperty("X-CANON-SERIES-SPLIT-OF"); if (!splitOf.isEmpty()) obj.insert("seriesSplitOf", splitOf);`
   - Demote: `const QString splitOf = obj.value("seriesSplitOf").toString(); if (!splitOf.isEmpty()) todo->setNonKDECustomProperty("X-CANON-SERIES-SPLIT-OF", splitOf);`
   - Loss profile (`canonToVtodoLoss()`): add `seriesSplitOf` →
     `LossKind::Reversible`.
5. **ms-todotask leg — zero new code, verify it, declare it.** Do **not**
   add `seriesSplitOf` to the `handled` set
   (`src/todo/mstodotaskcanonstages.cpp:476-497`) — it then auto-carries
   through the existing unhandled-canon-prop loop (`:474-511`) exactly like
   `completionAnchor` (W4) and the recurrence-cannot-represent case. Add
   `seriesSplitOf` to `canonToMsTodoTaskLoss()`'s Reversible list
   (`:539-567`, alongside `completionAnchor` at `:559-564`). Write a test
   mirroring the existing MS `completionAnchor` carrier round-trip test
   (find it in `tests/todo/tst_ms_todotask_canon_edge.cpp`, added by W4) to
   confirm the carrier key literally reads `x-canon-series-split-of` on the
   wire (extension row), not some other kebab spelling.
6. **google-task leg — declare Dropped.** Add `"seriesSplitOf"` to the
   `Dropped` list in `canonToGoogleTaskLoss()`
   (`src/todo/googletaskcanonstages.cpp:248-274`), same ruling as
   `completionAnchor` (no Task extension point at all, O66(c), already
   established fact — not something this item re-litigates).
7. **Build the split helper as a new, pure, testable library function** —
   new files `src/todo/todoseriessplitter.{h,cpp}` (new CMake sources;
   register in `src/todo/CMakeLists.txt` next to
   `vtodocanonfields.{h,cpp}`). Suggested shape:
   ```cpp
   namespace Kalburator::Todo {
   struct SeriesSplitResult {
       bool ok = false;
       QString error;                    // non-empty iff !ok — see decision 5
       QJsonObject updatedOldMaster;      // old master canon obj, RRULE UNTIL tightened
       QJsonObject newMaster;             // fresh uid, seriesSplitOf = old uid, no recurrenceId
       QList<QJsonObject> rebasedExceptions; // uid rewritten to newMaster's uid; recurrenceId unchanged
   };
   SeriesSplitResult splitSeriesAtInstant(const QJsonObject &masterCanon,
                                          const QDateTime &splitInstant,
                                          const QList<QJsonObject> &allExceptions);
   }
   ```
   Contract: operates entirely on **canon JSON objects already in memory**
   (not raw ICS bytes, not KCalendarCore incidences) — this preserves
   invariant 3 (verbatim RRULE lines) by construction: the function only
   text-edits the RRULE line's `UNTIL=`/absence, leaving every other
   recurrence line (`RDATE`, `EXDATE`, unrelated `RRULE` params) untouched.
   `allExceptions` may include exceptions before AND after `splitInstant`;
   the function partitions internally and returns only the rebased
   (`>= splitInstant`) subset in `rebasedExceptions` — callers should NOT
   pre-filter (simpler contract, matches the binding spec's "regenerates
   exceptions with start ≥ N").
   - UID minting: deterministic —
     `<oldUid>-split-<sanitizedSplitInstantStamp>`, reusing the exact
     sanitization already used for exception hrefs
     (`RemoteCalendarBackend::generateItemUrlForCreate`,
     `src/calendar/remotecalendarbackend.cpp:869-879` — strip
     non-alphanumerics from the UTC-ISO stamp). Deterministic (not
     `QUuid::createUuid()`) so a retried/idempotent split call produces the
     same new-master identity.
   - RRULE UNTIL math: parse the RRULE line's `KEY=VALUE` parts (mirror
     `parseRruleParts`, `src/calendar/recurrencepatternconverter.cpp:67-83`
     — currently file-local/anonymous-namespace; either export it via
     `recurrencepatternconverter.h` or write a small local twin in the new
     file — recommend exporting to avoid parser drift, see Open decision
     6). If `COUNT` is present, return `ok=false` (see Open decision 5 —
     v1 does not attempt COUNT recomputation). Otherwise: old master gets
     `UNTIL=` set to the instant just before `splitInstant`, formatted in
     the same value-type (DATE vs DATE-TIME) as the existing RRULE/DTSTART
     — mirror the UNTIL-emission pattern at
     `recurrencepatternconverter.cpp:204-223` for the DATE-only case. New
     master's RRULE line is copied verbatim (same FREQ/INTERVAL/BYDAY/
     UNTIL, since a still-open UNTIL-bounded rule remains valid unchanged
     for the remainder) with `start`/`due` set to `splitInstant`.
   - Master hygiene: `newMaster` must NOT carry `recurrenceId`/
     `recurrenceRange` (mirrors the existing invariant pinned by
     `vtodoMasterHasNoRecurrenceId`,
     `tests/todo/tst_todo_canon_roundtrip.cpp:467+`).
8. **Do NOT wire the helper into `SyncEngine`, the differ, or any backend
   automatically.** Per Open decision 1/9, this is a host-invoked pure
   function. Document the intended calling convention (old master →
   `update`, new master → `create`, each rebased exception →
   `delete`(old composite id) + `create`(new composite id under the new
   master's uid) — mirroring the exact create/delete href mechanics already
   proven for detached exceptions, W1 contract §7) in a short **contract
   doc** analogous to `2026-08-26-w1-detached-exceptions-contract.md`, OR
   fold it into this recon doc's "Open decisions" if the implementing
   session decides a separate contract doc is premature before the helper
   exists. Recommend: land the helper + tests first; write the contract
   doc alongside (small, mirrors W1 §5's "SPECIFIED-not-executed" framing)
   in the same commit series.
9. **Tests** (see Code map §8 for exact template files/line numbers):
   - `tst_todo_canon_roundtrip.cpp`: rewritten `...ThisAndFutureRange` test
     (step 2); new `seriesSplitOf` promote/demote round-trip test
     (mirror the `completionAnchor` X-prop pattern).
   - New `tests/todo/tst_todo_series_split.cpp`: unit tests directly on
     `splitSeriesAtInstant()` — unbounded RRULE (clean split), UNTIL-bounded
     RRULE (old master's UNTIL tightens, never loosens past the original
     bound), COUNT-bounded RRULE (`ok=false`), exceptions before N pass
     through NOT in `rebasedExceptions`, exceptions ≥ N appear in
     `rebasedExceptions` with uid == newMaster's uid and unchanged
     `recurrenceId`, new master has no `recurrenceId`/`recurrenceRange`
     and does carry `seriesSplitOf` == old uid.
   - `tst_ms_todotask_canon_edge.cpp`: `seriesSplitOf` carrier round-trip
     (mirror the W4 `completionAnchor` MS test).
   - `tst_google_task_canon_edge.cpp`: `seriesSplitOf` Dropped-list pin
     (mirror the W4 `completionAnchor` Google test).
   - `tst_canonjson_diff_merge.cpp`: trivial non-conflict pin for
     `seriesSplitOf` (falls out automatically from cataloguing, per O63/W4
     precedent — copy the `completionAnchor` pin pattern).
10. **Matrix regen + byte-pin, same commit** (O63 discipline):
    `./build/tools/matrixgen/matrixgen > docs/campaign/eee/CONVERGENCE-MATRIX.md`,
    verify `tests/convergence/tst_gm_pipeline_convergence.cpp` green. No
    edge-count change expected (new canon key + loss-profile rows only, no
    new `edges()` entries).

---

## Code map (verified 2026-08-27)

### 1. The write-hostility bug + the pinned test that currently contradicts W3
- `src/todo/vtodocanonfields.cpp:571-588` — demote block; `:582-584` is the
  exact re-emission (`todo->setThisAndFuture(range == "thisAndFuture")`).
  Promote counterpart (correct, keep as-is): `:265-280`.
- `src/calendar/eventcanonfields.cpp:586-598` — **identical bug on the
  VEVENT side** (`:594-596`); no pinned test either way (grepped
  `tests/calendar/*.cpp` for `ThisAndFuture` — zero hits). Out of scope
  per my reading (Open decision 8), but real.
- **`tests/todo/tst_todo_canon_roundtrip.cpp:430-464`,
  `vtodoRoundTripPreservesThisAndFutureRange`** — landed as part of W1
  (composite identity work). Currently asserts (a) demoted bytes DO
  contain `"RANGE=THISANDFUTURE"` (`:455-458`) and (b)
  `outTodo->thisAndFuture()` is `true` (`:463`). **Both assertions are the
  exact opposite of the W3 binding spec** ("we will NOT emit it even where
  discovered"). This is not a hypothetical conflict — it is checked-in,
  currently-green test code that step 2 of the design sketch must rewrite.
  The canon-capture half of the same test (`:448-450`, promote reads
  `recurrenceRange == "thisAndFuture"`) is correct and must be KEPT.

### 2. recurrenceRange / recurrenceId canon plumbing (both domains)
- `src/todo/todocanonproperties.cpp:38-40` — catalogue entries
  (`recurrenceId` Json, `recurrenceRange` String). Insertion point for the
  new `seriesSplitOf` key: immediately after `:40`.
- `src/todo/vtodocanonfields.cpp:265-280` (promote), `:571-588` (demote).
- `src/calendar/calendarcanonproperties.cpp:43-44` — event-side catalogue
  mirror (same two keys, byte-identical shape).
- `src/calendar/eventcanonfields.cpp:287-298` (promote), `:586-598`
  (demote) — event-side mirror.

### 3. Composite record identity (W1, binding, must not be violated)
- `src/sync/recordidentity.h` (whole file, ~70 lines) —
  `composeRecordIdentity(uid, recurrenceId)`,
  `decomposeRecordIdentity(id)`, `isExceptionRecordId(id)`. Separator
  `'\x01'`. **No rename primitive exists** — changing an exception's `uid`
  (the rebase operation) is definitionally a new composite identity; there
  is no in-place "move" of a record id anywhere in the codebase.
- `docs/campaign/vtodo-parity/2026-08-26-w1-detached-exceptions-contract.md`
  §1 (keying), §3 (delete semantics — cascade tombstone precedent), §4
  (flatten strategy: Google/MS never carry `recurrenceId` at all — a
  series split's rebased exceptions are therefore **caldav/subscription/
  local/org-only in practice**; on Google/MS, a detached exception was
  already flattened to a standalone completed task before it could ever
  need rebasing), §5 ("engine-level uid-family propagation/cascade remain
  SPECIFIED-not-executed" — the precedent W3 extends, see Open decision 9).

### 4. RemoteCalendarBackend distinct-href mechanics (model for new-master + rebased-exception hrefs)
- `src/calendar/remotecalendarbackend.cpp:852-880` —
  `generateItemUrlForCreate()`: composite-aware href minting, stamp
  sanitization (`:869-879`) — the exact pattern the split helper's
  deterministic new-master-uid minting should reuse conceptually (not by
  calling this function — it lives in the backend and operates on record
  ids, not canon objects — but the sanitization algorithm should match).
- `:893-906` — `resolveItemUrl()` (delete-path href resolution, graceful
  master fallback for stale composite ids).
- `:3186-3237` — `applyRecords()` creates loop (per-record job dispatch);
  `:3239-3258` — updates loop; `:3260+` — deletes loop. A split's write-out
  (1 update + 1 create + N delete/create pairs) is just N+2 ordinary items
  across these three loops — no new backend code needed, confirming Open
  decision 1/9 (orchestration, not backend surgery).

### 5. Carrier mechanism (MS auto-carry + name-derivation constraint)
- `src/todo/mstodotaskcanonstages.cpp:20-36` — `kCarrierPrefix`,
  `carrierKey()` (camelCase → kebab, prefixed `x-canon-`),
  `propFromCarrierKey()` (inverse). **Verified**: `carrierKey("seriesSplitOf")`
  → `"x-canon-series-split-of"`, byte-identical to the binding spec's
  literal string. The canon key name is load-bearing — any other spelling
  (e.g. `splitOf`, `seriesSplit`) produces a different wire key.
- `:263-294` — promote: generic open-extension-carrier reader (loops
  `extensions[]`, strips via `propFromCarrierKey`, inserts into canon).
  Already generic — zero changes needed for a new carrier key.
- `:474-511` — demote: generic unhandled-canon-prop auto-carry loop.
  `handled` set at `:476-497` — `seriesSplitOf` must NOT be added here.
- `:539-567` — `canonToMsTodoTaskLoss()`; `completionAnchor` Reversible
  entry at `:559-564` is the exact template line to duplicate for
  `seriesSplitOf`.
- `src/todo/vtodocanonfields.cpp:305-312` — `X-ORG-REPEATER` promote (W4)
  and `:432-436`/`:190-194` — `X-ALT-DESC` promote/demote — both are the
  "new canon-only key needs an explicit custom-prop block" precedent
  `seriesSplitOf` on the vtodo leg must follow (providerExtras'
  `x-vtodo` channel at `:360-373`/`:279-292`(promote) only round-trips
  props ALREADY on the incidence — it cannot manufacture a new one from a
  canon-only key).
- `src/todo/googletaskcanonstages.cpp:248-274` — `canonToGoogleTaskLoss()`;
  Dropped list (`:266-274`) — template for adding `"seriesSplitOf"`.

### 6. RRULE text manipulation
- `src/calendar/recurrencepatternconverter.cpp:67-83` — `parseRruleParts()`
  (anonymous-namespace, file-local — NOT currently exported via
  `recurrencepatternconverter.h`). `:204-223` — UNTIL/COUNT emission
  pattern (range→UNTIL wins over COUNT, DATE-only UNTIL formatted
  `yyyyMMddT235959Z`) — the shape to mirror for the split helper's old-
  master UNTIL rewrite, though that code is itself Graph-`patternedRecurrence`-
  specific (demote direction, not raw-RRULE-line rewriting) — treat as a
  formatting reference only, not reusable as-is.
- `src/calendar/icalcomponentscan.cpp:78-186` —
  `extractComponentRecurrenceLines()`: the actual promote-side source of
  canon's `recurrence` StringList (verbatim RRULE/RDATE/EXDATE line
  capture, component-scoped by UID + optional `recurrenceIdUtc` selector).
  The split helper receives these lines already-extracted (as canon JSON),
  so it does NOT need this scanner — only a small UNTIL find/replace over
  the RRULE line string.
- KCalendarCore `Recurrence` API
  (`/usr/include/KF6/KCalendarCore/kcalendarcore/recurrence.h`):
  `timesInInterval()` (`:282`), `getPreviousDateTime()` (`:306`),
  `setEndDateTime()` (`:363`), `duration()`/`setDuration()` (`:318`/`:324`)
  — available if the helper is instead built by parsing the RRULE line(s)
  into a real `KCalendarCore::Recurrence` object (via a throwaway
  `KCalendarCore::Todo`/`ICalFormat`) rather than hand-editing text. This
  would make COUNT recomputation tractable (walk occurrences via
  `timesInInterval(origStart, splitInstant)`, subtract count from
  `duration()`) at the cost of re-serialization risk to invariant 3
  (KCalendarCore may reformat BYDAY ordering, canonicalize case, etc. on
  round trip — same general risk already accepted for W4's *derived*
  RRULE, but NOT previously accepted for a *verbatim* RRULE line). See
  Open decision 5/6.

### 7. Loss-profile / stock-shape registration (no edge-count change)
- `src/todo/vtodocanonstages.cpp:65-91` — `canonToVtodoLoss()`.
- `src/todo/mstodotaskcanonstages.cpp:539-567` — `canonToMsTodoTaskLoss()`.
- `src/todo/googletaskcanonstages.cpp:248-274` — `canonToGoogleTaskLoss()`.
- `src/todo/todostockshapes.cpp:42-79` — `edges()`; unaffected (O63: only
  `edges()` growth changes edge-count pins in
  `tests/todo/tst_vtodo_plugin.cpp` etc. — a new canon key + loss-profile
  rows do not).

### 8. Capability flag (already correct, no change needed)
- `src/sync/calendarcapabilities.h:47-49` — `thisAndFuture` field + doc
  comment already says "false everywhere in v1: series-split instead, per
  handoff W3".
- `src/sync/calendarcapabilities.cpp` — every `CapabilityReports::*()`
  factory sets `c.thisAndFuture = false;` (lines 56, 75, 97, 117, 136, 150,
  164) and `capabilitiesFromDiscovery()` too (`:33` reading, default
  `false`). Confirmed already landed correctly (W8); W3 needs no capability
  code changes, only confirm `tst_calendar_capabilities` stays green (it
  pins the false-everywhere fact already, per the W8 receipt).

### 9. BaselineStore transaction precedent (for host-side atomic staging)
- `src/storage/baselinestore.h:203-216` — `transaction<Callable>(fn)`;
  used by the engine's persist loop (W2) to wrap a master-edit +
  exception-create pair atomically at the **local baseline** level. A
  split's local-baseline bookkeeping (old master hash update, new master
  hash insert, rebased-exception hash inserts/deletes) should use the same
  wrapper if/when the engine-side orchestration is built — but this is
  **storage-layer** atomicity only; it says nothing about the CalDAV
  transport layer, which is N independent HTTP calls (see Open decision
  9).

### 10. Test precedents
- `tests/todo/tst_todo_canon_roundtrip.cpp:386-464` — `recurrenceId`/
  `recurrenceRange` round-trip tests (rewrite target: `:430-464`, template
  for new tests: `:386-426`'s pattern).
- `tests/todo/tst_todo_canon_roundtrip.cpp:467+` —
  `vtodoMasterHasNoRecurrenceId` — the invariant the new master must also
  satisfy.
- `tests/sync/tst_record_identity.cpp` — compose/decompose precedent (no
  change needed, but the split helper's rebase output must stay consistent
  with this file's fail-loud/malformed-input contract if it ever composes
  ids itself — recommend it does NOT compose record ids at all, only emits
  plain `uid` values, leaving `composeRecordIdentity()` to whichever
  backend/engine layer mints the actual record id, matching how
  `todoFieldsToCanon`/`canonObjectToVtodoBytes` never touch
  `recordidentity.h` today either).
- `tests/calendar/tst_remotecalendarbackend_blob_view.cpp:1112`
  (`detachedException_applyRecordsCreate_mintsDistinctHref`), `:1192`
  (`detachedException_dualWrite_masterEditAndExceptionCreate_twoHrefs`),
  `:1344` (`detachedException_masterDelete_removesOnlyMasterHref`) — the
  closest existing precedent for the "N-record write lands as N ordinary
  backend ops" shape a future engine-level split test would extend (not
  required for W3 per Open decision 1, but the template to reach for if a
  later item wires orchestration in).
- `tests/shape/tst_canonjson_diff_merge.cpp:43,62` —
  `differMarksChangedPropertyOnly`/`differTreatsCompositeAsWhole` — copy
  for a trivial `seriesSplitOf` non-conflict pin.
- `tools/matrixgen/main.cpp` + `tests/convergence/tst_gm_pipeline_convergence.cpp` —
  regen + byte-pin, same commit (O63).

---

## Open decisions for the implementing agent

1. **What triggers a split, and what happens to `recurrenceRange:
   "thisAndFuture"` at the demote seam?** Two readings were possible: (A)
   the library automatically detects an incoming this-and-future canon
   record and performs the split itself somewhere in the pipeline, or (B)
   the library only (i) guarantees it never *writes* RANGE=THISANDFUTURE
   and (ii) offers a pure, host-invoked split-computation helper, while the
   *decision* "the user just made a this-and-future edit, split now" is a
   host (PlanStan) call. **Recommend (B)**, for three convergent reasons:
   the engine has no multi-record-atomic-write primitive today (W1 §5
   already declined to build one for the simpler cascade-delete case); W2
   and W4 both settled the "who decides" question the same way ("caller
   stages it," "engine never mutates data on diff" — a hard invariant, not
   a preference); and reading (A) would require the demote seam
   (`canonObjectToVtodoBytes`, single incidence in/out) to somehow also
   mutate a *different* existing record (the old master) and mint a brand
   new one, which is not a shape that function (or any single-record
   canon-transform stage in this codebase) can express. Concretely this
   means: `recurrenceRange: "thisAndFuture"` in canon becomes purely a
   **read-side** fact (an already-existing foreign write, captured
   losslessly); the demote seam **unconditionally** refuses to re-emit
   `RANGE=THISANDFUTURE` (design sketch step 1) regardless of what canon
   says, as a hard safety backstop independent of whether the split helper
   was ever invoked. This directly requires rewriting
   `vtodoRoundTripPreservesThisAndFutureRange` (Code map §1) — I recommend
   renaming it to `vtodoDemoteNeverEmitsThisAndFutureRange` and asserting:
   canon still captures `recurrenceRange == "thisAndFuture"` on promote
   (unchanged); demoted bytes must NOT contain `"RANGE=THISANDFUTURE"`;
   `outTodo->thisAndFuture()` must be `false`; `outTodo->hasRecurrenceId()`
   stays `true` (the bare exception identity is preserved, only the RANGE
   modifier is stripped — this is the new `Degraded` loss-profile entry
   from design step 2).

2. **Where does the split helper live?** Recommend `src/todo/`
   (`todoseriessplitter.{h,cpp}`), todo-only for this campaign, but with a
   domain-neutral internal shape (operates on plain `QJsonObject`s, no
   todo-specific types in its signature beyond canon JSON) so an
   event-side twin can be added later with minimal duplication. Do NOT
   build a shared `src/sync/` version now — YAGNI until VEVENT is actually
   in scope (Open decision 8), and premature sharing before a second real
   caller exists tends to guess the wrong seam.

3. **New-master UID minting: deterministic vs random.** Recommend
   deterministic — `<oldUid>-split-<sanitizedSplitInstantUtcIsoStamp>`,
   reusing the sanitization algorithm at
   `remotecalendarbackend.cpp:869-879` (strip non-alphanumerics from the
   UTC-ISO stamp). Rationale: a sync host may retry a failed/partial split
   (see Open decision 9); a deterministic uid makes the retry idempotent
   (recompute → same new-master identity → the backend's own create-or-
   update-by-id semantics naturally converge) whereas a random `QUuid`
   would mint a second, orphaned new-master on every retry.

4. **How exception rebasing maps onto the W1 composite-identity contract.**
   The W1 contract (`recordidentity.h`) has no "rename" primitive — a
   composite id is `uid + '\x01' + recurrenceId`, and there is no function
   anywhere that changes the `uid` half of an *existing* record's identity
   in place. Recommend treating rebase as exactly what it structurally is:
   **a new record under a new composite identity, plus a tombstone of the
   old one** — the split helper's `rebasedExceptions` output is a plain
   canon object with `uid` set to the new master's uid and `recurrenceId`
   copied unchanged (same instant, same value). The *realization* of this
   as backend operations — `delete(oldComposite)` +
   `create(newComposite)` — is the host's job (same "specified, not
   auto-executed" posture as the rest of this doc), using the exact same
   distinct-href minting `RemoteCalendarBackend` already proves works for
   ordinary detached-exception creates (Code map §4, §7 of the W1
   contract). Do not attempt to invent a "move" operation anywhere in the
   backend layer — none of the four compounding backends (CalDAV,
   subscription (read-only anyway), local (doesn't compound at all),
   org (doesn't compound)) has an addressing scheme that could represent
   an in-place rename cheaper than delete+create.

5. **COUNT-bounded RRULE recompute: solve it or fail loud?** The binding
   spec says "new master starts at N with copied RRULE remainder" without
   specifying COUNT arithmetic. An UNTIL-bounded or unbounded RRULE splits
   cleanly (tighten UNTIL on the old master; copy the RRULE verbatim onto
   the new master, since the *same* UNTIL still correctly bounds the
   remainder). A COUNT-bounded RRULE does not: naively copying `COUNT=N`
   onto the new master over-generates future occurrences (the count should
   have been decremented by however many occurrences already happened
   before the split instant). Recommend: **v1 fails loud** —
   `splitSeriesAtInstant()` returns `ok=false` with a descriptive error for
   any RRULE containing `COUNT=`, rather than silently mis-computing.
   This matches the campaign's own stated house doctrine (CLAUDE.md
   binding roadmap doc, Part IV ethics: "loud about limits") and is
   consistent with W4's "declared corner case, tested" pattern rather than
   a guessed heuristic. A correct COUNT recompute is possible later (walk
   occurrences via `KCalendarCore::Recurrence::timesInInterval` on a
   throwaway parsed incidence — Code map §6) but is real recurrence-math
   work that deserves its own test matrix, not a rushed guess inside W3.

6. **Text-level RRULE editing vs KCalendarCore-object editing.** Recommend
   **text-level** (find/replace the `UNTIL=`/`COUNT=` token inside the
   verbatim RRULE line string) for the old master's UNTIL tightening,
   preserving every other byte of the RRULE (and all RDATE/EXDATE lines)
   exactly — this is what invariant 3 ("verbatim RRULE always wins")
   demands, and W4's precedent for a *derived* (non-verbatim) RRULE does
   NOT license reformatting an already-verbatim one. This requires
   exporting (or duplicating) `parseRruleParts()`
   (`recurrencepatternconverter.cpp:67`, currently file-local) — recommend
   exporting it via `recurrencepatternconverter.h` since it is already
   exactly the right small utility and duplicating regex/parsing logic
   invites drift between two copies.

7. **Does the split helper need to touch `providerExtras`/`x-vtodo`
   custom props on the new master?** Recommend: no special handling for
   v1 — the new master is a fresh canon object; if the caller wants it to
   inherit the old master's *other* unmapped X-props (besides
   `seriesSplitOf` itself), that's a caller decision (e.g. copy
   `providerExtras` verbatim from the old master canon object before
   calling the helper, or have the helper do so — flag as a follow-up
   question for the implementing session to resolve pragmatically rather
   than over-designing here; the binding spec is silent on it).

8. **VEVENT twin bug (`eventcanonfields.cpp:594-596`) — fix now or flag
   only?** Recommend flag-only for this item (matches STATUS.md's VP.e
   scoping to todo; the campaign is explicitly "vtodo-parity"). But do
   **not** let this silently vanish — the return receipt for this item
   should explicitly call out that the identical write-hostility bug
   exists, unfixed, on the calendar/VEVENT side, with the exact file:line,
   so a future event-focused pass (or an alert PlanStan reviewer) doesn't
   have to rediscover it.

9. **Engine/transport atomicity gap.** A split's write-out is 1 update + 1
   create + N delete/create pairs across possibly-independent HTTP calls
   (CalDAV) — there is no cross-record transaction at the transport layer
   (BaselineStore's `transaction()` is local-storage only, Code map §9). A
   partial failure mid-split (e.g. old master's UNTIL update lands, new
   master's create fails) leaves a genuinely inconsistent remote state
   (the series has a gap — no instances exist for `[UNTIL, ...)` until the
   new master lands). Recommend: **explicitly declare this
   SPECIFIED-not-executed** in whatever contract doc follows this recon
   (mirroring W1 §5's identical framing for cascade-delete), document the
   failure mode plainly, and leave retry/reconciliation policy to the host
   layer (Part IV ethics: the library states facts and limits, judgment
   stays host-side). Do not attempt partial-failure rollback logic inside
   this item's scope — it's a materially bigger project (would need
   something like a saga/compensating-transaction layer that doesn't exist
   anywhere else in this codebase either).

---

## Session note

Exploration ran as a subagent (2026-08-27), reading CLAUDE.md, STATUS.md,
the W1 contract doc, the W4 recon doc (template) and W4 return receipt,
and the binding response doc's W3 section, then tracing the concrete code
seams (`vtodocanonfields.cpp`, `eventcanonfields.cpp`,
`todocanonproperties.cpp`, `mstodotaskcanonstages.cpp`,
`googletaskcanonstages.cpp`, `recurrencepatternconverter.cpp`,
`remotecalendarbackend.cpp`, `recordidentity.h`, `baselinestore.h`) and the
closest existing tests. W1 (VP.c), W2 (VP.b), W8 (VP.a), W4 (VP.d) are all
COMPLETE and committed. No W3 code has been written. The single most
load-bearing finding is Code map §1 / Open decision 1: an existing,
green, W1-era pinned test
(`vtodoRoundTripPreservesThisAndFutureRange`) currently asserts the
opposite of what the binding W3 spec requires, and rewriting it is not
optional cleanup — it is the correctness fix W3 exists to make.
