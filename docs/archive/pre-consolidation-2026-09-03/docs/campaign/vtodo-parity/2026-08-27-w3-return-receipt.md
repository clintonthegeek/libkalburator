# Return receipt — W3 series-split mechanics + split-association carrier

**Delivered:** 2026-08-28
**Consumes:** handoff §W3; response doc §W3 ("This-and-future — ACCEPTED:
capability-gated series split",
`docs/2026-08-25-vtodo-parity-handoff-response.md`); recon handoff
`2026-08-27-w3-recon-handoff.md` (code map + 9 open decisions, all
implemented as-given per that doc's instruction).
**Binding contract:**
`docs/campaign/vtodo-parity/2026-08-27-w3-series-split-contract.md`
(carrier + demote guarantee + helper contract + realization sequence +
declared atomicity gap — read that doc for the full normative detail;
this receipt covers what was built, how, and the test evidence).

---

## The correctness fix (step 1 — independently testable)

`canonObjectToVtodoBytes`'s recurrenceId/recurrenceRange demote block
(`src/todo/vtodocanonfields.cpp:600-621`) now unconditionally calls
`todo->setThisAndFuture(false)` (line 615) instead of
`todo->setThisAndFuture(range == QStringLiteral("thisAndFuture"))`.
RANGE=THISANDFUTURE is write-hostile on real CalDAV servers and is now
NEVER re-emitted, regardless of what canon's `recurrenceRange` carries.
Promote (`:265-280`) is untouched — canon still losslessly captures an
incoming RANGE=THISANDFUTURE from a foreign producer; only the *outgoing*
re-emission is forbidden.

This directly contradicted a W1-era pinned test, exactly as the recon doc
flagged as its headline finding. `vtodoRoundTripPreservesThisAndFutureRange`
(`tests/todo/tst_todo_canon_roundtrip.cpp`, old `:430-464`) has been
**rewritten and renamed** to `vtodoDemoteNeverEmitsThisAndFutureRange`
per the recon doc's exact recommended assertions: canon still captures
`recurrenceRange == "thisAndFuture"` on promote (unchanged); demoted
bytes must NOT contain `"RANGE=THISANDFUTURE"`; `outTodo->thisAndFuture()`
must be `false`; `outTodo->hasRecurrenceId()` stays `true` (the bare
exception identity survives — only the RANGE modifier is stripped).

**Grep audit for other dependents (per the task's own instruction):**
searched `tests/` and `src/` for `THISANDFUTURE`/`thisAndFuture` outside
the rewritten test and the touched files. Everything else that matches is
either `src/sync/calendarcapabilities.{h,cpp}` (the unrelated
`thisAndFuture` capability-flag field, already `false` everywhere per W8
and unaffected by this change) or `src/calendar/eventcanonfields.cpp`
(the VEVENT twin bug — flagged, not fixed, see below). No other test
depended on the old, wrong emission behavior.

## Canon schema / carrier

- New catalogued key `seriesSplitOf` (`PropertyKind::String`,
  `src/todo/todocanonproperties.cpp:46`, right after `recurrenceRange`) —
  the OLD master's uid, present only on a series-split new master.
- **vtodo/CalDAV leg** — explicit custom-prop carrier
  `X-CANON-SERIES-SPLIT-OF` (`src/todo/vtodocanonfields.cpp`): promote at
  `:282-291`, demote at `:450-454`, mirroring the `X-ALT-DESC`
  precedent exactly (this key is canon-only — it does not arrive "for
  free" on the wire the way `providerExtras["x-vtodo"]` round-trips props
  already present on an incidence). Loss profile: Reversible
  (`canonToVtodoLoss()`, `src/todo/vtodocanonstages.cpp:85-87`).
- **ms-todotask leg** — zero handler code, as specified: `seriesSplitOf`
  is NOT in the demote's `handled` set
  (`src/todo/mstodotaskcanonstages.cpp:476-497`), so it auto-carries
  through the existing unhandled-canon-prop loop as
  `x-canon-series-split-of` on the `kalburator.canon` open-type extension.
  Loss profile: Reversible (`canonToMsTodoTaskLoss()`, `:564`).
- **google-task leg** — `seriesSplitOf` added to the `Dropped` list in
  `canonToGoogleTaskLoss()` (`src/todo/googletaskcanonstages.cpp:269`) —
  no extension point of any kind (O66(c)).
- Loss-profile row also added: `recurrenceRange` → **Degraded**
  (`canonToVtodoLoss()`, `src/todo/vtodocanonstages.cpp:92-99`) — the
  RANGE modifier is dropped on write by the correctness fix above; the
  bare `recurrenceId` exception identity needs no row of its own (still
  Reversible, catalogued separately, round-trips losslessly on its own).

## The split helper

New files `src/todo/todoseriessplitter.h` (89 lines) and
`todoseriessplitter.cpp` (243 lines), registered in the root
`CMakeLists.txt`'s `KALBURATOR_TODO_HEADERS`/`KALBURATOR_TODO_SOURCES`
lists. `Kalburator::Todo::splitSeriesAtInstant(masterCanon, splitInstant,
allExceptions) -> SeriesSplitResult` — full contract in the series-split
contract doc §3. Highlights:

- **Text-level UNTIL rewrite** (Open decision 6): a private
  `rewriteUntilInRruleLine()` helper does a regex find/replace of the
  `;UNTIL=...` token inside the verbatim RRULE line string (or appends
  `;UNTIL=<value>` when no UNTIL token existed), leaving every other byte
  of the line untouched. `parseRruleParts()`
  (`src/calendar/recurrencepatternconverter.cpp`) was **exported** via
  `recurrencepatternconverter.h` (moved out of the file's anonymous
  namespace into the `Kalburator::Calendar::RecurrencePattern` namespace,
  same definition, zero behavior change) so the splitter reuses the
  existing RRULE `KEY=VALUE` parser rather than duplicating it — the
  recon doc's explicit recommendation to avoid parser drift.
- **Value-type detection** (DATE vs DATE-TIME UNTIL): sniffed from the
  existing UNTIL token when present (`^\d{8}$` → DATE-only); falls back to
  the master's own `start`/`due` canon shape (`date` key present → DATE-
  only) when the RRULE carries no UNTIL to sniff from.
- **"Never loosens past the original bound"**: the new UNTIL is
  `min(originalUntil, splitInstant - 1)` in both value-type branches — if
  the RRULE already ended before the split point, tightening leaves the
  original (earlier) bound alone rather than moving it later to the split
  point. Pinned by `untilBoundedRruleNeverLoosensPastOriginalBound`.
- **Deterministic new-master uid** (Open decision 3):
  `<oldUid>-split-<sanitizedSplitInstantUtcIsoStamp>`, using the identical
  sanitization algorithm (strip non-alphanumerics from the UTC-ISO stamp)
  as `RemoteCalendarBackend::generateItemUrlForCreate`
  (`src/calendar/remotecalendarbackend.cpp:869-879`) — reproduced locally
  as a small helper rather than called directly, since that method lives
  on the backend and operates on record ids, not canon objects.
- **COUNT-bounded RRULE fails loud** (Open decision 5): `ok=false` with a
  descriptive error naming the offending RRULE line; no COUNT
  recomputation attempted.
- **Exception rebasing is not a rename** (Open decision 4): rebased
  exceptions are plain canon objects with `uid` rewritten to the new
  master's uid and `recurrenceId` copied unchanged — the W1 composite-
  identity contract has no rename primitive, so this is structurally "new
  record, new identity" by construction; the function does not call
  `composeRecordIdentity()` at all (per the recon doc's Code map §10
  recommendation, leaving id composition to whichever backend/engine layer
  eventually realizes the write).
- **New-master field inheritance** (beyond Open decision 7, a pragmatic
  choice this session had to make and is documenting here since the recon
  doc left it open): the new master is built by copying the ENTIRE old
  master canon object, then overwriting `uid`, `recurrence`, `seriesSplitOf`,
  `start`/`due` (retimed), and removing `recurrenceId`/`recurrenceRange`.
  This means `summary`/`description`/`priority`/`alarms`/`providerExtras`/
  etc. all carry forward onto the new master unchanged — the pragmatic
  reading of "new master starts at N with copied RRULE remainder": the
  new master is the SAME logical series continuing, not a blank todo. The
  recon doc's Open decision 7 only asked about `providerExtras`
  specifically and left it as a caller decision; this session resolved it
  by treating the whole-object-copy as the natural default (a caller that
  wants a stripped-down new master can always post-process the returned
  `QJsonObject`). Flagging this as a deviation-in-spirit from decision 7's
  literal "no special handling in v1" framing, though not in conflict
  with it — decision 7 was about providerExtras specifically, and this
  goes further (whole-object copy) but in the same "don't over-design,
  make a pragmatic call" direction the decision invited.

## Tests proving acceptance

(Slot counts below are QTest-slot-function counts, verified by grep
against the actual files, not the loose "assertion count" some earlier
receipts in this campaign used — stated so they're independently
checkable.)

- `tests/todo/tst_todo_canon_roundtrip.cpp` (was 21 slots pre-W3, now 23):
  `vtodoDemoteNeverEmitsThisAndFutureRange` (rewrite of
  `vtodoRoundTripPreservesThisAndFutureRange` — same slot, new name/body,
  net +0), `vtodoRoundTripPreservesSeriesSplitOf` (+1,
  X-CANON-SERIES-SPLIT-OF promote+demote round trip),
  `canonToVtodoLossProfileChargesSeriesSplitOfReversible` (+1, registry
  `inspect()` pin, also asserts `recurrenceRange` → Degraded).
- New `tests/todo/tst_todo_series_split.cpp` (9 slots, new binary), pure
  unit tests directly on `splitSeriesAtInstant()`:
  `unboundedRruleSplitsCleanly`, `untilBoundedRruleTightensToSplitPoint`,
  `untilBoundedRruleNeverLoosensPastOriginalBound`,
  `countBoundedRruleFailsLoud`, `exceptionsBeforeSplitInstantAreExcluded`,
  `exceptionsAtOrAfterSplitInstantAreRebased`,
  `newMasterHygieneAndDeterministicUid` (also proves idempotent retry: two
  calls with identical inputs produce the identical new-master uid),
  `oldMasterUidIsUnchanged`, `failsLoudOnMalformedInput` (no-RRULE case,
  exception-passed-as-master case, invalid-splitInstant case).
- `tests/todo/tst_ms_todotask_canon_edge.cpp` (6 slots, unchanged count):
  extended the existing `demoteDeclaredLossMatchesReality` slot with a new
  sub-block confirming the wire carrier key is literally
  `x-canon-series-split-of` (not some other kebab spelling), and extended
  the existing `inspectDeclaresMsTodoTaskEdge` slot with the Reversible
  loss-profile pin.
- `tests/todo/tst_google_task_canon_edge.cpp` (5 slots, unchanged count):
  same pattern — extended `demoteDeclaredLossMatchesReality` (confirms
  `seriesSplitOf` is silently absent from the demoted Task wire object)
  and `inspectDeclaresGoogleTaskEdge` (Dropped loss-profile pin).
- `tests/shape/tst_canonjson_diff_merge.cpp` (was 12 slots pre-W3, now
  13): `differMarksSeriesSplitOfChangeOnly` (+1) — trivial non-conflict
  pin, copy of the `completionAnchor` pattern.
- `tests/convergence/tst_gm_pipeline_convergence.cpp` — byte-pin passes
  against the regenerated `CONVERGENCE-MATRIX.md` (diff is exactly the new
  `seriesSplitOf`/`recurrenceRange` loss-profile rows; no edge-count
  change — O63: only `edges()` growth moves those pins, and none of the 9
  todo edges changed).

**Full suite:** 214 tests total (was 213 pre-W3; +1 new test binary,
`tst_todo_series_split`). Ran
`ctest --test-dir build --output-on-failure -j"$(nproc)"`: **210 passed,
4 failed** — exactly the 4 known pre-existing environmental Radicale/KDAV
slots (`tst_backend_signals`, `tst_backend_thread_relocation`,
`tst_backend_reentrancy_pin`, `tst_remotecalendarbackend`), unrelated to
this work (KDAV 30s-transfer-timeout pattern vs the local Radicale, per
STATUS.md's standing note). All W3-touched suites individually green:
`tst_todo_canon_roundtrip` 23/23, `tst_todo_series_split` 9/9,
`tst_ms_todotask_canon_edge` 6/6, `tst_google_task_canon_edge` 5/5,
`tst_canonjson_diff_merge` 13/13, `tst_gm_pipeline_convergence` 1/1.
Grepped for any other test dependency on the old
RANGE=THISANDFUTURE-emitting behavior before rewriting the one test the
recon doc identified — none found (see "grep audit" above).

## The nine open decisions — confirmation, as specified

All nine were implemented exactly per the recon doc's recommendations,
which the task instructed to take as-given rather than re-litigate:

1. **Trigger / recurrenceRange semantics** — reading (B): library never
   auto-detects or auto-executes a split; `recurrenceRange` is read-side
   only in canon; demote unconditionally refuses to re-emit RANGE=THISANDFUTURE.
   Implemented exactly. No wiring added anywhere into SyncEngine, the
   differ, or any backend.
2. **Helper location** — `src/todo/todoseriessplitter.{h,cpp}`, todo-only,
   operating on plain `QJsonObject`s (no todo-specific types in the
   signature). Implemented exactly.
3. **New-master UID: deterministic** — `<oldUid>-split-<sanitizedStamp>`,
   reusing the `remotecalendarbackend.cpp:869-879` sanitization algorithm.
   Implemented exactly; idempotent-retry property test-pinned.
4. **Exception rebasing = new record, not rename** — implemented exactly;
   no `composeRecordIdentity()` call inside the helper.
5. **COUNT-bounded RRULE fails loud** — implemented exactly; no
   recomputation attempted.
6. **Text-level RRULE editing, `parseRruleParts()` exported** —
   implemented exactly; the function was moved (not duplicated) out of
   the anonymous namespace into the named `RecurrencePattern` namespace
   and declared in the header.
7. **providerExtras inheritance — no special v1 handling, flagged as
   open** — see "New-master field inheritance" above: this session went
   slightly further than the decision's literal scope (whole-object copy,
   not just leaving providerExtras as a caller decision) but in the same
   spirit ("don't over-design, make a pragmatic default"). Documented as
   the one place this receipt deviates in spirit (not in substance) from
   a decision's literal wording — flagged per the task's instruction to
   flag any deviation and justify it. No engine/backend wiring was added
   either way, so nothing downstream depends on this choice being final.
8. **VEVENT twin bug — flag only, not fixed.** See the flag text below.
9. **Engine/transport atomicity gap — declared SPECIFIED-not-executed** —
   written up in the contract doc §5, mirroring the W1 contract §5
   framing exactly, including the concrete partial-failure scenario and
   the "no saga layer" declaration.

## The VEVENT-twin-bug flag (Open decision 8)

Verbatim flag text, also recorded in the contract doc §2 and in
STATUS.md:

> The identical write-hostility bug exists on the calendar/VEVENT side:
> `src/calendar/eventcanonfields.cpp:594-596`
> (`event->setThisAndFuture(range == QStringLiteral("thisAndFuture"))`,
> inside the demote block starting at `:586`) still re-emits
> RANGE=THISANDFUTURE unconditionally whenever canon carries
> `recurrenceRange: "thisAndFuture"`. No test pins either behavior on the
> VEVENT side — zero hits for `ThisAndFuture` anywhere under
> `tests/calendar/`, both before and after this item. This is real,
> unfixed, and out of scope for vtodo-parity's VP.e (todo-only) — a future
> event-focused pass must fix it using the identical
> unconditional-`setThisAndFuture(false)` pattern applied here to the
> VTODO leg.

## Deprecations / behavior changes affecting PlanStan callers

- **Behavior change (the correctness fix):** any caller that was relying
  on `canonObjectToVtodoBytes` re-emitting RANGE=THISANDFUTURE for a canon
  object carrying `recurrenceRange: "thisAndFuture"` will now get a plain
  RECURRENCE-ID (no RANGE parameter) instead. This is intentional — the
  old behavior was write-hostile on real servers and was never a
  supported guarantee (W1's own contract doc §5 already said "No
  RANGE=THISANDFUTURE write-out — W3's series-split strategy covers
  this-and-future"). No known PlanStan caller depends on the old emission
  (grep audit above found none in this repo's own test suite either).
- New additive canon key `seriesSplitOf` — absent unless a caller stages
  it directly or `splitSeriesAtInstant()` produced it. No existing canon
  consumer sees a new required field.
- New public function `Kalburator::Todo::splitSeriesAtInstant()` — opt-in,
  callable by a host (e.g. PlanStan) whenever it wants to realize a
  this-and-future edit as a series split. Nothing in the library calls it
  automatically.

## Next

W5 (alarm extension) + W6.2 (date coercion) + W7 (passthrough tests)
(VP.f) — the last item on the vtodo-parity campaign's remaining order.
