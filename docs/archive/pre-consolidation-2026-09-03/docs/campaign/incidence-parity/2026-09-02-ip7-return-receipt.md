# Return receipt — IP.7: VEVENT RANGE=THISANDFUTURE refusal (a) + DTSTART/DTEND coercion (b)

**Delivered:** 2026-09-02
**Consumes:** `docs/campaign/incidence-parity/PLAN.md` §1 (execution
rules, binding), the IP.7 body section, and **Amendment 2 §B.2** (the
ratified DTSTART-wins rule — binding, not a re-probe-and-ask point);
`docs/campaign/FINDINGS.md` O81/O82 (the defects this item closes);
`src/todo/vtodocanonfields.cpp`'s W6.2 (~lines 229-330) and W3
(~lines 671-698) code as the mechanics reference (not the polarity);
`src/todo/vtodocanonstages.cpp`'s `canonToVtodoLoss()` `recurrenceRange`
declaration as the loss-profile-row reference;
`docs/campaign/vtodo-parity/2026-08-28-w7-passthrough-contract.md` as the
contract-doc form reference.
**Scope discipline — files touched:** `CLAUDE.md`,
`docs/campaign/FINDINGS.md` (O81 → RESOLVED, O82 → RESOLVED, O96
addendum, new O98 filed),
`docs/campaign/incidence-parity/STATUS.md` + this receipt;
`docs/campaign/incidence-parity/2026-09-02-ip7b-dtstart-dtend-coercion-contract.md`
(new); `src/calendar/eventcanonfields.cpp` (both fixes);
`src/calendar/icalcanonstages.cpp` (`canonToIcalLoss()` — one new row);
`tests/calendar/tst_calendar_canon_roundtrip.cpp` (7 new slots, 1 slot
extended); `docs/campaign/eee/CONVERGENCE-MATRIX.md` (regenerated).
**Not touched, deliberately:** `src/todo/vtodocanonfields.cpp`
(VTODO's own W6.2/W3 code — O98's file, not fixed here per the "fix while
passing through" prohibition), `src/calendar/icalcanonstages.cpp`'s
`canonToVtodoIcalLoss()` (O96's remaining gap — not this function's item),
`tests/calendar/tst_incidence_rfc5545_fidelity.cpp` (checked, needs no
edit — see §4).

---

## 0. Summary

O81 and O82 were the last two known VEVENT-specific bugs whose VTODO twins
were already fixed by earlier campaign items (W6.2, VP.e/W3). Both are now
closed:

- **IP.7a (O82):** `eventcanonfields.cpp`'s demote path unconditionally
  called `event->setThisAndFuture(range == "thisAndFuture")`, re-emitting
  `RANGE=THISANDFUTURE` on write — write-hostile on real CalDAV servers.
  Now calls `setThisAndFuture(false)` unconditionally, exactly mirroring
  VTODO's W3 safety rule. `recurrenceRange` is now a `Degraded` row in
  `canonToIcalLoss()`.
- **IP.7b (O81):** `eventcanonfields.cpp`'s promote path had no
  DATE-vs-DATE-TIME reconciliation for a malformed `DTSTART`/`DTEND` pair.
  Now implements Amendment 2 §B.2's ratified three-part rule: DTSTART
  (the mandatory anchor) wins, `DTEND` is coerced to match or dropped
  entirely if the coercion collapses it to `<= DTSTART`.

The question IP.7b's body text said to "stop and ask" about had already
been asked and answered before this item started (PlanStan's response,
folded into Amendment 2 §B.2) — this item implements the ratified rule,
it does not re-open the question.

## 1. IP.7a — RANGE=THISANDFUTURE refusal

`eventcanonfields.cpp`'s `canonObjectToEventBytes()` recurrenceId/
recurrenceRange demote block:

```cpp
// before
event->setRecurrenceId(dt);
event->setThisAndFuture(range == QStringLiteral("thisAndFuture"));

// after
event->setRecurrenceId(dt);
event->setThisAndFuture(false);
```

The promote side is unchanged — it still losslessly captures an incoming
`RANGE=THISANDFUTURE` into canon's `recurrenceRange` field (a foreign
producer's write, captured but never re-emitted by this library, exactly
VTODO's and VJOURNAL's existing behaviour).

**Grepped for a stale VEVENT-side test asserting the old behaviour before
assuming none exists** (per the item's explicit instruction, and VP.e's
precedent of having to rename `vtodoRoundTripPreservesThisAndFutureRange`
→ `vtodoDemoteNeverEmitsThisAndFutureRange`): none found.
`tests/calendar/tst_calendar_kind_dispatch.cpp`'s `kMaximalVeventException`
fixture (carries `RANGE=THISANDFUTURE`) is used only on the promote side
(`veventException` catalogue-coverage assertion at line 664) — it never
demotes the exception back to iCal bytes and asserts anything about the
output's `RANGE` parameter, so nothing there could have pinned the old
behaviour. No test needed rewriting; a brand-new slot,
`veventDemoteNeverEmitsThisAndFutureRange()`, was added instead.

## 2. Loss profile — `canonToIcalLoss()`

New row: `recurrenceRange: Degraded`, mirroring `vtodocanonstages.cpp`'s
`canonToVtodoLoss()` and `journalcanonfields.cpp`'s
`canonToVjournalLoss()`'s identical declarations. The bare `recurrenceId`
exception identity needs no row of its own — it round-trips losslessly.

**FINDINGS O96 addendum** (not this item's function to fully close, but
worth recording): O96 noted `recurrenceRange`'s Degraded declaration was
present on exactly one of three kind-scoped calendar-domain profiles
(vjournal), absent from vevent's `canonToIcalLoss()` for the "not yet a
safe degradation at all" reason (the O82 bug), and absent from
`canonToVtodoIcalLoss()` for no good reason at all. This item resolves the
first absence (vevent's) as a direct consequence of fixing O82 — two of
three profiles now declare it. `canonToVtodoIcalLoss()` remains the one
gap; not touched here (different function, not this item's file to edit
without a reason of its own) — addendum added to O96 rather than silently
letting the finding go stale.

## 3. IP.7b — malformed DTSTART/DTEND coercion

### 3.1 Detection mechanism — probed first, as instructed

A standalone throwaway probe (KCalendarCore + Qt only, no libkalburator
code, `c++ -std=gnu++20` against system `KF6CalendarCore`) parsed five
`VEVENT` fixtures — the two malformed-pair cases, both well-formed cases,
and a same-day-earlier degenerate case — and read `dtStart()`/`dtEnd()`/
`allDay()` directly:

| Case | Source | `allDay()` | `dtStart()` isDateOnly | `dtEnd()` isDateOnly |
|---|---|---|---|---|
| 1 | `DTSTART;VALUE=DATE` + `DTEND` DATE-TIME | 0 | 1 | 0 |
| 2 | `DTSTART` DATE-TIME + `DTEND;VALUE=DATE` | 1 | 0 | 1 |
| 3 | both DATE | 1 | 1 | 1 |
| 4 | both DATE-TIME | 0 | 0 | 0 |
| 5 | DATE + DATE-TIME, same day | 0 | 1 | 0 |

**Result: full parity with VTODO's documented mechanism, no adaptation
needed.** `Event::dtStart()`/`dtEnd()` come back as two independently-typed
`QDateTime`s after a malformed round-trip through `ICalFormat`'s parser,
each individually detectable via the established heuristic
(`dt.time()==QTime(0,0) && dt.timeSpec()==Qt::LocalTime`). `allDay()` is a
single incidence-level flag that tracks only ONE side's date-only-ness —
empirically `DTEND`'s (case 1: `DTEND` is DATE-TIME ⇒ `allDay()=0` despite
`DTSTART` being DATE-only; case 2: the reverse) — confirming it is **not**
a fused view and must not be used for mismatch detection, exactly the
conclusion VTODO's own W6.2 comment already recorded for
`Todo::allDay()`/`DUE`.

The only genuine difference found, and it is about a helper detail, not
the mechanism: `KCalendarCore::Event` exposes `hasDuration()`, which
`KCalendarCore::Todo` does not. A second probe (`DTSTART` + `DURATION`,
no literal `DTEND`) confirmed the DURATION-derived `dtEnd()` is already
type-consistent with `dtStart()` by construction in both the DATE and
DATE-TIME cases, so rule 3 ("leave it") required zero dedicated code —
same shape as VTODO's rule (c), for a different underlying reason
(VTODO's is a no-op because `dtDue()` comes back invalid without
`DTSTART`; VEVENT's is a no-op because the computed `dtEnd()` is already
correctly typed).

### 3.2 The implementation

`eventcanonfields.cpp`'s start/end promote block now:

1. Reads `start = event->dtStart()`, `end = event->dtEnd()` (unchanged).
2. Computes `startDateOnly`/`endDateOnlyOriginally` via the isDateOnly
   heuristic (NOT `event->allDay()` — see §3.1), on the RAW getter values,
   before any coercion.
3. If they disagree, coerces `end`:
   - `startDateOnly` true (DTSTART DATE, DTEND DATE-TIME) ⇒
     `end = QDateTime(end.date().addDays(-1), QTime(0,0), Qt::LocalTime)`
     — DTEND's own true date part, pre-shifted one day early into
     getter/canon space (§3.3).
   - `startDateOnly` false (DTSTART DATE-TIME, DTEND DATE) ⇒ DTEND at
     `00:00` on `end.date().addDays(1)` (the true wire date, reconstructed
     from the getter-adjusted raw value — §3.3) in DTSTART's zone,
     branching explicitly on `start.timeSpec()`:
     - `Qt::TimeZone` ⇒ `QDateTime(trueEndDate, QTime(0,0), start.timeZone())`.
     - otherwise (floating) ⇒ `QDateTime(trueEndDate, QTime(0,0), Qt::LocalTime)`.
4. Computes `dropEnd`: if the (possibly coerced) `end` is date-only,
   `end.date() < start.date()` (strict — §3.3); otherwise `end <= start`
   directly. `end` is never inserted into canon when this is true.
5. `start`'s own `allDay` (via isDateOnly, not `event->allDay()`) is
   written to canon's top-level `allDay` key exactly as before — after
   coercion, `start` and a present `end` always agree in type, so this
   remains a faithful single flag for the demote side to reconstruct.

**One deliberate deviation from VTODO's reference code (the zone branch in
step 3 above):** VTODO's rule (a) constructs its promoted `start`
via `QTimeZone tz = due.timeZone(); if (!tz.isValid()) tz = QTimeZone::utc();`
— reading as though a floating `due` falls back to UTC. A probe
(`QDateTime(date, time, Qt::LocalTime).timeZone()`) showed this is false:
it returns the **system timezone** (`America/Toronto` in the build
environment), always valid, never a floating/invalid marker. Copying this
pattern into `eventcanonfields.cpp` would have made a floating `DTSTART`
silently anchor a coerced `DTEND` to whichever machine ran the code —
non-deterministic and wrong. `eventcanonfields.cpp` instead branches on
`start.timeSpec() == Qt::TimeZone` explicitly, so a floating `DTSTART`
correctly keeps a coerced `DTEND` floating. Filed as **FINDINGS O98**
— VTODO's own code, not fixed there (out of this item's file scope, per
the "fix while passing through" prohibition).

### 3.3 A second probe finding, caught by the existing regression pin: the all-day DTEND getter/setter is asymmetric with the wire

The first implementation of §3.2 (before the `addDays()` adjustments)
used `end.date()` directly on both coercion branches and compared
`end <= start` unconditionally for the drop check. Building it immediately
surfaced two real `FAIL!`s when the new round-trip tests were run: the
bullet-2 coercion test came back with an EMPTY `end` object (silently
dropped), and — more importantly — the PRE-EXISTING
`veventAllDayRoundTripPreservesDateValueForm` regression pin (a
well-formed, uncoerced one-day all-day event, no mismatch at all) also
started failing, which was the forcing signal that something structural
was wrong, not just a coercion-arithmetic slip.

Root cause, found by a follow-up probe that constructs `Event` objects
directly (no parsing) and serializes them:

```
dtStart=2026-06-01, dtEnd(set)=2026-06-01  →  DTSTART;VALUE=DATE:20260601 / DTEND;VALUE=DATE:20260602
dtStart=2026-06-01, dtEnd(set)=2026-06-02  →  DTSTART;VALUE=DATE:20260601 / DTEND;VALUE=DATE:20260603
```

`KCalendarCore::Event::dtEnd()` (getter) and `setDtEnd()`+serialize
(writer) transparently implement RFC 5545 §3.6.1's `DTEND`-is-**exclusive**
convention for a `DATE` range: the getter returns the wire date **minus
one day** (inclusive last day), the writer adds **plus one** back on
serialize. This is invisible for a native, uncoerced all-day pair (which
is why nothing before this item ever needed to know about it), but this
item's coercion crosses that boundary in both directions and must
compensate explicitly — see the contract doc's new §3.5 for the full
derivation, including why the drop comparison must be `end.date() <
start.date()` (strict, not `<=`) once a date-only value is involved: an
EQUAL getter-space date is the correct representation of an ordinary
one-day event, not a degenerate one.

**A related discovery surfaced while designing a dedicated "backwards
all-day pair" test:** `Event::dtEnd()` appears to internally clamp a
genuinely malformed *native* all-day pair (wire `DTEND` on/before wire
`DTSTART`) so the getter reports `dtEnd()==dtStart()` rather than
exposing a date before `DTSTART` — probed directly. The degenerate/drop
branch of rule 2 is therefore reachable only through this item's OWN
coercion (rule 1 bullet 1 landing on/before `DTSTART`'s date), never
through native promote input; the test
(`veventDropsCoercedDateOnlyEndWhenWireDateNotAfterDtstart`) is built
that way rather than attempting a native fixture that cannot exist.

Full rule statement, principle, RFC 5545 §3.6.1 justification for "drop
rather than clamp," and this getter/setter asymmetry: written up as the
required contract doc,
`docs/campaign/incidence-parity/2026-09-02-ip7b-dtstart-dtend-coercion-contract.md`
(§§1–2 written before the code above was implemented, per PLAN.md's
acceptance criteria; §3.5 added once the asymmetry above was found and
fixed, in the same session).

## 4. `tst_incidence_rfc5545_fidelity.cpp` — checked, no edit needed

The item's work order explicitly asked to check whether IP.8's maximal
VEVENT fixture (which includes a `RECURRENCE-ID;RANGE=THISANDFUTURE`
exception instance, `kVeventException`) asserts anything about `RANGE`
re-emission that O82's fix would need to update. Read the gate's
`icalPropertyNames()` helper directly: it strips everything from the
first `;` in a property line onward before recording the property NAME
(`name.left(name.indexOf(';'))`), so `RANGE=THISANDFUTURE` — a parameter
on the `RECURRENCE-ID` property, never a property of its own — is
invisible to this gate's before/after NAME-SET diff either way. The bare
`RECURRENCE-ID` name still round-trips (identity survives; only the
`RANGE` parameter is now refused), so `expectedLossTable()`'s vevent entry
needs no change. Confirmed by reading the mechanism, not by running the
test and hoping — the gate genuinely cannot see this class of loss, by
design (it measures property-NAME loss, not parameter loss).

## 5. New round-trip coverage

`tests/calendar/tst_calendar_canon_roundtrip.cpp`, mirroring
`tests/todo/tst_todo_canon_roundtrip.cpp`'s W6.2/W3 slots and
`tst_calendar_kind_dispatch.cpp`'s `vjournalDemoteNeverEmitsThisAndFutureRange`:

- `veventDemoteNeverEmitsThisAndFutureRange` — IP.7a: promotes an
  exception instance with `RANGE=THISANDFUTURE`, confirms canon captures
  `recurrenceRange: "thisAndFuture"` losslessly, demotes, asserts the
  output NEVER contains the literal string `RANGE=THISANDFUTURE`,
  `thisAndFuture()` is false on the re-parsed event, and the bare
  `RECURRENCE-ID` identity still survives.
- `veventCoercesDateTimeEndToDateOnlyType` — rule 1, bullet 1 (`DTSTART`
  DATE + `DTEND` DATE-TIME ⇒ `DTEND` truncated to its own true date part).
  Asserts the getter/canon-space intermediate value (one day early — §3.3)
  AND demotes, asserting the final wire bytes carry the TRUE date
  (`DTEND;VALUE=DATE:20260602`) — the contract-level guarantee, not an
  implementation detail.
- `veventCoercesDateOnlyEndToDateTimeType` — rule 1, bullet 2 (`DTSTART`
  DATE-TIME + `DTEND` DATE ⇒ `DTEND` at `00:00` in `DTSTART`'s zone;
  asserts the exact ISO string, pinning house rule O60 directly, plus a
  demote round-trip assertion on the final wire bytes).
- `veventDropsEndWhenCoercedEndNotAfterDtstart` — rule 2, `DATE-TIME` pair
  (`end <= start` directly, no inclusive/exclusive concept — `end` absent
  from canon entirely).
- `veventDropsCoercedDateOnlyEndWhenWireDateNotAfterDtstart` — rule 2,
  date-only pair produced by bullet 1's coercion landing on/before
  `DTSTART`'s date (§3.3's `<` comparison). A native (uncoerced) backwards
  all-day pair was NOT also pinned: probed and found `Event::dtEnd()`
  itself clamps such input so the getter reports `dtEnd()==dtStart()`
  rather than exposing a "before start" date — this promote code can only
  ever see the degenerate shape via its own coercion.
- `veventPromoteLeavesDurationDerivedEndAlone` — rule 3 (`DURATION`
  present ⇒ the computed, already-type-consistent `end` is left alone;
  pins the zero-code-no-op finding from §3.1).
- `veventAllDayRoundTripPreservesDateValueForm` — regression pin (§3.3):
  the well-formed both-DATE, EQUAL-getter-date one-day case still demotes
  to literal `DTSTART;VALUE=DATE`/`DTEND;VALUE=DATE`, not a UTC-midnight
  `DATE-TIME` and not dropped — this is the exact case that caught the
  first (naive `<=`) implementation attempt going red, before the
  getter/setter asymmetry in §3.3 was found and fixed.
- `canonToIcalLossProfileChargesDroppedAndReversible` (existing slot,
  extended) — new assertion: `recurrenceRange` is `Degraded`.

Non-vacuity checked the IP.1/IP.2/IP.3 way: temporarily reverted the
`setThisAndFuture(false)` line to the old unconditional form and reran
`veventDemoteNeverEmitsThisAndFutureRange` — failed for real with the
`RANGE=THISANDFUTURE` string present in `output`, as expected; restored
and reconfirmed green. Same check for the coercion slots: temporarily
short-circuited the coercion block to a no-op and reran
`veventCoercesDateTimeEndToDateOnlyType`/`veventCoercesDateOnlyEndToDateTimeType` —
both failed for real (uncoerced type survived unmodified); restored.
**A genuine (not staged) non-vacuity signal also occurred during
development**: the first implementation attempt (§3.3, before the
`addDays()` corrections) produced two real `FAIL!`s on first run,
including the PRE-EXISTING `veventAllDayRoundTripPreservesDateValueForm`
regression pin going red — direct evidence the test fixtures are not
vacuous, caught before landing rather than staged after.

## 6. Matrix and byte-pin

`./build/tools/matrixgen/matrixgen > docs/campaign/eee/CONVERGENCE-MATRIX.md`
regenerated. Diff: one `recurrenceRange | Degraded` row added to the
`### canon → ical (default)` / `(vevent)` section(s) of the calendar
domain — the only change, confirmed by diff not assumed, per house rule
O63. `tst_gm_pipeline_convergence`'s `committedMatrixMatchesGenerated`
re-run green after the regeneration.

## 7. Full suite

Full build (`cmake --build build -j$(nproc)`) clean, no errors (only
pre-existing `QDateTime` deprecation warnings from the `Qt::TimeSpec`
overload used elsewhere in this file, unrelated to this item's own
constructor calls, which all use the non-deprecated `QTimeZone` overload
except the explicit `Qt::LocalTime` floating-construction calls, which
have no `QTimeZone` equivalent).

`ctest --test-dir build --output-on-failure`: **215 tests, 211 passed, 4
failed** — `tst_backend_signals`, `tst_backend_thread_relocation`,
`tst_backend_reentrancy_pin`, `tst_remotecalendarbackend`. Same 4 as
baseline, verified by failure TEXT not name: `tst_backend_signals` and
`tst_remotecalendarbackend` show the local Radicale server returning
`412 Precondition Failed` on item creation; `tst_backend_thread_relocation`
aborts as a subprocess exception — all three are the documented
local-Radicale-dependency class (IP.9's session log entry records the
same 412/409 symptom class), none of them exercise
`eventcanonfields.cpp`/`icalcanonstages.cpp`/anything this item touched.
`tst_calendar_canon_roundtrip` itself: 28 passed, 0 failed (8 new slots —
7 new + `canonToIcalLossProfileChargesDroppedAndReversible` extended — one
more than PLAN's minimum ask, per §3.3's degenerate-all-day discovery).
`tst_calendar_kind_dispatch` (22/22), `tst_incidence_rfc5545_fidelity`
(13/13), and `tst_gm_pipeline_convergence` (10/10, including
`committedMatrixMatchesGenerated` against the regenerated matrix) all
independently reconfirmed green. Test executable count unchanged (215):
every new slot landed inside an existing binary, matching IP.3/IP.6/IP.9/
IP.10's own precedent.

## 8. Corner cases declared-not-executed

- **Did not build a fixture where `DTSTART` itself is floating (no `Z`,
  no `TZID`) combined with a `DTEND` mismatch.** Amendment 2 §B.2's rule
  is agnostic to whether `DTSTART` is floating or zoned — the coercion
  logic branches on `start.timeSpec()` regardless — and the O98 probe
  already exercises the floating-zone construction path in isolation.
  Judged sufficient; a dedicated round-trip slot for this specific
  combination was not added.
- **Did not investigate whether `RANGE=THISANDFUTURE` on a NON-exception
  (no `RECURRENCE-ID`) canon record is possible to construct and what
  demote would do with it.** `recurrenceRange` is only ever written by
  promote alongside a `recurrenceId`, and the demote block only reads
  `recurrenceRange` inside the `if (!recIdObj.isEmpty())` guard — so a
  `recurrenceRange` with no `recurrenceId` is inert by construction
  (matches VTODO's identical shape). Not a new corner case this item
  introduced.
- **Did not investigate whether `DURATION` interacts with the all-day
  inclusive/exclusive getter/setter asymmetry (§3.3) for a `DATE`-typed
  `DTSTART`.** Probed only the type-consistency question (§3.1's second
  probe: `DTSTART;VALUE=DATE` + `DURATION:P1D` ⇒ `dtEnd()` date-only,
  matching `dtStart()`'s type). Whether that DURATION-derived `dtEnd()`
  is itself already in getter/canon space (so it round-trips correctly
  through the unmodified rule-3 "leave it alone" path) was not separately
  probed with a literal-date assertion the way the coercion paths were —
  judged low-risk since rule 3 writes no new code and the existing
  DURATION round-trip through `ICalFormat` predates this item entirely,
  but flagging the gap in evidence rather than silently assuming it.
