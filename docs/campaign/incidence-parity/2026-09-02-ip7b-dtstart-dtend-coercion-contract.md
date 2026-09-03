# IP.7b — malformed DTSTART/DTEND coercion: contract

**Delivered:** 2026-09-02 (implements IP.7b, part of IP.7; body work order:
`PLAN.md` §IP.7; ratified rule: `PLAN.md` Amendment 2 §B.2)
**Consumes:** FINDINGS.md O81 (binding problem statement);
`docs/2026-09-02-incidence-parity-planstan-response.md` Q2 (PlanStan's
ratified answer, DTSTART-wins).
**Status:** BINDING. Mirrors the W7 passthrough contract's precedent
(small, dedicated, quotable) and the W6.2/W3 contract shape more broadly.

---

## 1. The rule

`src/calendar/eventcanonfields.cpp`'s promote path (`eventFieldsToCanon()`)
reconciles a `DTSTART`/`DTEND` `DATE`-vs-`DATE-TIME` type mismatch as
follows, evaluated in this order:

1. **Coerce `DTEND` to `DTSTART`'s value type. Never the reverse.**
   - `DTSTART` `DATE` + `DTEND` `DATE-TIME` ⇒ take `DTEND`'s own date part
     (not `DTSTART`'s date — the day the author wrote for `DTEND` is kept,
     only its time-of-day is discarded).
   - `DTSTART` `DATE-TIME` + `DTEND` `DATE` ⇒ `DTEND` at `00:00` **in
     `DTSTART`'s timezone**, constructed directly in that zone (house rule
     O60 — never build elsewhere and `.toTimeZone()` convert).
2. **If the coerced `DTEND <= DTSTART`, drop `DTEND` entirely** and let RFC
   5545 §3.6.1's default stand — do NOT clamp to `DTSTART + 1 day`.
3. **`DURATION` present instead of `DTEND` ⇒ nothing to coerce.** Leave it.

## 2. The principle, and why DTSTART wins (not DTEND)

PlanStan's ratified answer supplied the principle that unifies this rule
with VTODO's W6.2 DUE-wins rule rather than treating them as two separate
decisions:

> **The mandatory temporal anchor wins; the optional derived bound is
> coerced to match it.**

- **VTODO** — `DTSTART` is optional, `DUE` is the semantically primary
  anchor (a task is defined by its deadline). Anchor = `DUE` ⇒ **DUE-wins**
  (W6.2).
- **VEVENT** — polarity reversed. `DTSTART` is mandatory (RFC 5545 requires
  it on every `VEVENT`); `DTEND` is optional, may be replaced by
  `DURATION`, and is defined *relative to* `DTSTART`. Anchor = `DTSTART` ⇒
  **DTSTART-wins**.

This is one rule, not a divergence, applied to two components with
opposite optionality of their two temporal properties — see PLAN.md
Amendment 2 §B.2 for the full framing (which corrects Amendment 1 §A.3.3's
"deliberate divergence" language: right about the action for VTODO, wrong
about the reason for treating it as unrelated to VEVENT's case).

**Why drop, not clamp (rule 2):** RFC 5545 §3.6.1 requires `DTEND` to be
strictly greater than `DTSTART` — a non-conforming pair (after coercion)
has no valid value to clamp *to*. The same section already defines the
absent-`DTEND` default (one day for a `DATE` `DTSTART`, zero duration for
a `DATE-TIME` one). Dropping therefore falls back to a **defined RFC
behaviour**, while clamping to `DTSTART + 1 day` would invent a bound the
author never wrote and make a malformed all-day event indistinguishable
from a well-formed one. PlanStan offered this as a preference and invited
a different call on an RFC read; the RFC read agrees with their
preference, so it was taken as given, not merely deferred to.

**Why it matters in practice:** the common real-world malformed case is an
all-day event from a sloppy producer — `VALUE=DATE` `DTSTART` with a stray
`DATE-TIME` `DTEND`. Under DTEND-wins it would promote to *timed* and move
out of an all-day banner into a 00:00 slot. DTSTART-wins keeps it where the
author meant it. `KCalendarCore::Incidence::allDay()` is one boolean for
the whole incidence, so a mismatched pair is unrepresentable downstream
regardless — it collapses by whichever setter ran last, "which is not a
rule, it is an accident" (Amendment 2 §B.2).

## 3. Detection mechanism — probed, not assumed

The rule above depends on being able to detect each of `DTSTART`/`DTEND`'s
date-only-ness *independently*, after a malformed pair has already
round-tripped through `KCalendarCore::ICalFormat`'s parser. This was
probed directly against `KCalendarCore::Event` (throwaway program, Qt
6.11.1, kcalendarcore 6.29.0-1, no libkalburator involved) before writing
any promote code, mirroring the question `vtodocanonfields.cpp`'s W6.2
comment already answered for `Todo::dtStart()`/`dtDue()`.

**Result: VEVENT's `Event::dtStart()`/`dtEnd()` behave identically to
VTODO's `Todo::dtStart()`/`dtDue()` on this exact point.** Five cases:

| Case | Source | `allDay()` | `dtStart()` isDateOnly | `dtEnd()` isDateOnly |
|---|---|---|---|---|
| 1 | `DTSTART;VALUE=DATE` + `DTEND` DATE-TIME | `0` | `1` | `0` |
| 2 | `DTSTART` DATE-TIME + `DTEND;VALUE=DATE` | `1` | `0` | `1` |
| 3 | both DATE (well-formed all-day) | `1` | `1` | `1` |
| 4 | both DATE-TIME (well-formed timed) | `0` | `0` | `0` |
| 5 | `DTSTART;VALUE=DATE` + `DTEND` DATE-TIME, same-day | `0` | `1` | `0` |

(`isDateOnly` is the established heuristic: `dt.time() == QTime(0,0) &&
dt.timeSpec() == Qt::LocalTime`.)

Two facts fall out, both matching VTODO's documented behaviour exactly:

1. `dtStart()`/`dtEnd()` come back as **two independently-typed
   `QDateTime`s** — each individually detectable via the same heuristic,
   surviving the mismatch rather than being silently fused at parse time.
2. `event->allDay()` is a **single incidence-level flag that reflects only
   one side's date-only-ness — empirically, `DTEND`'s** (case 1: `DTEND`
   is DATE-TIME ⇒ `allDay()=0` even though `DTSTART` is DATE-only; case 2:
   `DTEND` is DATE ⇒ `allDay()=1` even though `DTSTART` is DATE-TIME). It
   is **not** a fused view of both sides and is therefore **not used** for
   mismatch detection — the same conclusion VTODO's W6.2 comment reaches
   for `Todo::allDay()` reflecting only `DUE`'s date-only-ness.

**No adaptation was needed.** The mechanism mirrors VTODO's exactly; only
the coercion *polarity* differs (§2 above), which the implementation
expresses by branching on `DTSTART`'s type rather than `DTEND`'s.

**One deliberate deviation from VTODO's own reference code, not a mirror:**
`vtodocanonfields.cpp`'s rule (a) constructs its DATE-TIME-promotion target
zone via `due.timeZone()` unconditionally, falling back to UTC only if
`!tz.isValid()`. A second probe found this guard is dead code for a
floating anchor: `QDateTime::timeZone()` on a `Qt::LocalTime`-spec datetime
returns the **system timezone** (`America/Toronto` in the probe
environment), not an invalid/floating marker — so VTODO's existing code
would silently anchor a floating `DUE` to whichever machine runs it. Filed
as **FINDINGS O98**, not fixed there (VTODO's file, out of IP.7's scope).
`eventcanonfields.cpp`'s equivalent branch avoids this: it checks
`start.timeSpec() == Qt::TimeZone` explicitly before calling
`.timeZone()`, constructing an explicit floating (`Qt::LocalTime`) result
otherwise, so a floating `DTSTART` correctly keeps a coerced `DTEND`
floating.

## 3.5 A second, load-bearing probe finding: the all-day DTEND getter/setter is asymmetric with the wire (RFC 5545 §3.6.1 exclusivity)

Implementing rule 1 against the mechanism in §3 first produced two failing
round-trip tests, both traced to a fact §3's probe did not surface because
it only compared **date-only-ness**, not the **literal date values**
returned. A follow-up probe, constructing `Event` objects directly and
serializing them (no parsing involved), found:

```
dtStart=2026-06-01, dtEnd(set)=2026-06-01  →  DTSTART;VALUE=DATE:20260601 / DTEND;VALUE=DATE:20260602
dtStart=2026-06-01, dtEnd(set)=2026-06-02  →  DTSTART;VALUE=DATE:20260601 / DTEND;VALUE=DATE:20260603
```

`KCalendarCore::Event::dtEnd()` (getter) and `setDtEnd()`+serialize
(writer) implement RFC 5545 §3.6.1's `DTEND`-is-**exclusive** convention
for a `DATE`-typed range transparently: the getter returns the wire date
**minus one day** (the inclusive last day of the range), and the writer
adds **plus one day** back when serializing an all-day `DTEND`. This pair
is symmetric and invisible as long as a date-only value is only ever
round-tripped between the two unmodified — which is exactly how a
native, uncoerced all-day pair has always flowed through this code.

**This item's coercion crosses that boundary in both directions** (turning
a `DATE-TIME` into a `DATE`, or a `DATE` into a `DATE-TIME`), so it must
compensate explicitly or the stored date silently drifts by a day:

- Rule 1 bullet 1 (`DATE-TIME` `DTEND` truncated to `DATE`): the raw
  `dtEnd()` here is **not** getter-adjusted (the original wire property
  was never all-day), so `end.date()` already IS the true wire date. The
  implementation stores `end.date().addDays(-1)` — pre-adjusting into
  getter/canon space — so the writer's automatic `+1` reproduces the true
  date on demote.
- Rule 1 bullet 2 (`DATE` `DTEND` promoted to `DATE-TIME`): the raw
  `dtEnd()` here **is** getter-adjusted (one day short of the literal wire
  date). The implementation adds the day back
  (`end.date().addDays(1)`) before constructing the `00:00` moment, so the
  result matches the literal wire date, not the getter-shifted one.

**Consequence for rule 2's drop comparison:** because canon's date-only
`end` is always in getter/canon space (native or coerced, per the above),
comparing `end.date() < start.date()` (strict) — not `<=` — is the correct
test. An **equal** date-only pair (`end.date() == start.date()`) is the
getter-space representation of a perfectly ordinary **one-day** all-day
event (probe-confirmed above: setting `dtEnd()` to the same date as
`dtStart()` serializes to a valid `DTEND` one day later) and must **not**
be dropped. A naive `<=` comparison — this item's first attempt — silently
discarded every well-formed one-day all-day `VEVENT`'s `DTEND`, caught by
the pre-existing `veventAllDayRoundTripPreservesDateValueForm` regression
pin going red. `DATE-TIME` ends (both sides timed, no coercion) have no
such inclusive/exclusive concept and are compared directly (`end <=
start`).

**A related discovery, not itself part of the rule:** `Event::dtEnd()`
appears to internally clamp a genuinely malformed *native* all-day pair
(wire `DTEND` on or before wire `DTSTART`, both already `DATE`-typed) —
probed directly, such a source reports `dtEnd()==dtStart()` rather than a
"before start" date, i.e. KCalendarCore itself absorbs that malformation
before this code ever sees it. The degenerate/drop path (rule 2) is
therefore only reachable through **this item's own coercion** (rule 1
bullet 1, when the coerced `DATE-TIME`'s date part lands on or before
`DTSTART`'s date) — pinned by
`veventDropsCoercedDateOnlyEndWhenWireDateNotAfterDtstart`, not by a
native-input variant (which cannot be constructed).


## 4. `DURATION` (rule 3) — probed, confirmed zero-code no-op

Unlike `KCalendarCore::Todo` (no `DURATION` accessor at all — VTODO's rule
(c) is a no-op because `dtDue()` simply comes back invalid without a
`DTSTART` to add the duration to), `KCalendarCore::Event` exposes
`hasDuration()`/`duration()`, and `dtEnd()` is a **computed** value
(`DTSTART + DURATION`) when no literal `DTEND` was on the wire. Probed
directly: a `DTSTART;VALUE=DATE` + `DURATION:P1D` pair produces a
date-only `dtEnd()`; a `DTSTART` DATE-TIME + `DURATION:PT1H` pair produces
a DATE-TIME `dtEnd()` — in both cases already type-consistent with
`dtStart()` by construction, so rule 1's coercion never fires and rule 2's
drop condition (`end <= start`) is never met for a well-formed duration.
No dedicated code was written for rule 3; the general coercion logic
already leaves a `DURATION`-derived pair alone, the same shape as VTODO's
rule (c).

(Separately, and out of this contract's scope: `KCalendarCore::ICalFormat`
re-serializes a `DURATION`-only source back out as `DURATION`, not a
literal `DTEND` — this is **pre-existing** promote→demote behaviour,
unrelated to the DATE/DATE-TIME mismatch this item addresses, and not
introduced or changed by it.)

## 5. Round-trip coverage

`tests/calendar/tst_calendar_canon_roundtrip.cpp`:

- `veventCoercesDateTimeEndToDateOnlyType` — rule 1, bullet 1. Asserts
  BOTH the getter/canon-space intermediate canon value (one day early —
  see §3.5) AND the final demoted wire bytes (`DTEND;VALUE=DATE:20260602`,
  the true date), so the contract-level guarantee is pinned directly, not
  just an implementation detail.
- `veventCoercesDateOnlyEndToDateTimeType` — rule 1, bullet 2 (O60 pin: the
  coerced value lands at `00:00` in `DTSTART`'s zone, verified by exact
  ISO string match, plus a demote round-trip assertion).
- `veventDropsEndWhenCoercedEndNotAfterDtstart` — rule 2, `DATE-TIME` pair
  (no inclusive/exclusive concept involved).
- `veventDropsCoercedDateOnlyEndWhenWireDateNotAfterDtstart` — rule 2,
  date-only pair produced by rule 1 bullet 1's coercion landing on or
  before `DTSTART`'s date (see §3.5's note on why a NATIVE backwards
  all-day pair cannot be constructed as a separate test case).
- `veventPromoteLeavesDurationDerivedEndAlone` — rule 3.
- `veventAllDayRoundTripPreservesDateValueForm` — regression pin: the
  well-formed (both-DATE), EQUAL-getter-date one-day case still demotes to
  a literal `VALUE=DATE` pair, not a UTC-midnight `DATE-TIME` and not
  dropped — the exact case §3.5's `<` (not `<=`) comparison exists to
  protect.
