# Return receipt — IP.12: Demote purity (O90)

**Delivered:** 2026-09-03
**Consumes:** `docs/campaign/incidence-parity/PLAN.md` §1 (execution rules,
binding) and the **IP.12 — Demote purity** body section (Amendment 1);
`docs/campaign/FINDINGS.md` O90 (the finding this item closes).
**Files touched:** `src/calendar/icaltimestamp.{h,cpp}` (new
`stripICalPropertyParameter()`), `src/calendar/incidencecommonfields.{h,cpp}`
(new `stripAttendeeXUid()` wrapper), `src/calendar/eventcanonfields.cpp`,
`src/calendar/journalcanonfields.cpp`, `src/todo/vtodocanonfields.cpp` (one
call-site line each), `tests/calendar/tst_icaltimestamp.cpp` (new unit
slots for the regex helper), `tests/calendar/tst_demote_purity.cpp` (new,
the cross-process acceptance test), `tools/demotepurityprobe/` (new tool),
`tools/CMakeLists.txt` + `tests/calendar/CMakeLists.txt` (registration),
`docs/campaign/FINDINGS.md` (O90 → RESOLVED), `docs/campaign/incidence-parity/STATUS.md`
(this item + **campaign closure**), `CLAUDE.md` (this item + **campaign
closure**), this receipt.
**Not touched:** any loss profile or `TransformationEdge` (X-UID is not a
canon-tracked property — it never reaches canon at all, so there is
nothing to declare), the matrix (regenerated and diffed anyway, byte-
identical, confirming this by execution not just reasoning).

This is the **last item of the incidence-parity campaign**. §6 below
records the campaign closure.

---

## 0. Summary

O90: `KCalendarCore::ICalFormat` stamps a heap-address-derived `X-UID`
parameter into every serialized `ATTENDEE` line, so `demote(canon)` was not
a pure function of `canon` — the same canon record demoted in two
different OS processes produced two different byte streams (stable within
one process, different across processes).

Fixed with a new parameter-level (not line-level) regex strip,
`Kalburator::Calendar::stripICalPropertyParameter()`
(`src/calendar/icaltimestamp.{h,cpp}`), wired via one shared thin wrapper,
`stripAttendeeXUid()` (`src/calendar/incidencecommonfields.{h,cpp}`), called
from the three ical-producing demote functions
(`Calendar::canonObjectToEventBytes`, `Todo::canonObjectToVtodoBytes`,
`Calendar::canonObjectToJournalBytes`) right after the existing
`stripInjectedTimestamps()` call each already makes. `{todo,canon}`'s own
demote leg and the `org-ical` edge both route through
`canonObjectToVtodoBytes()`/`canonObjectToEventBytes()` respectively, so
they inherit the fix with no separate wiring.

**`ORGANIZER` is unaffected** — investigated, not assumed (§2). Only
`ATTENDEE` needed the fix, matching PLAN.md's text exactly.

**Acceptance test**: `tests/calendar/tst_demote_purity.cpp` launches a new
tool, `tools/demotepurityprobe`, TWICE via `QProcess` — two genuinely
separate OS processes — and diffs their stdout byte-for-byte (§4).
Verified non-vacuous by temporarily reverting the fix and confirming a
real, believable failure, both via the test's own guard assertions and via
a raw two-invocation diff showing two different heap-derived `X-UID`
values (§5).

## 1. The strip helper — placement and design

### 1.1 `stripICalPropertyParameter()` — `icaltimestamp.{h,cpp}`

Placed as a sibling to `stripICalPropertyLine()` in `icaltimestamp.cpp`,
per the task's explicit instruction, and because that file has already
outgrown its literal name — `stripICalPropertyLine` has nothing to do with
timestamps either; the file's real scope is "post-serialization iCal-bytes
string surgery primitives," and this is another one.

**A different granularity, not a reuse of the line-stripper.**
`stripICalPropertyLine` deletes an entire property line — correct for
`CREATED`/`LAST-MODIFIED`, which KCalendarCore either emits whole or not at
all. Applying that to `ATTENDEE` would destroy real client content (name,
email, RSVP, role, partstat), so the new helper removes only the
`;X-UID=value` substring, leaving the rest of the line untouched.

**Fold-tolerant, not naive.** RFC 5545 §3.1 line folding (CRLF + a single
SPACE/HTAB introduces a continuation) routinely splits `ATTENDEE` across
physical lines — confirmed empirically, not assumed (the real serialized
form observed via `docs/campaign/incidence-parity/probes/kcalendarcore-probe.cpp`
Section D and this item's own probe, §2 below):

```
ATTENDEE;CN=A;RSVP=FALSE;PARTSTAT=NEEDS-ACTION;ROLE=REQ-PARTICIPANT;
 CUTYPE=INDIVIDUAL;X-UID=94023475731472:mailto:a@example.com
```

The fold here lands right after the `;` that precedes `CUTYPE`, but there
is no guarantee it always lands there — a longer `CN` could just as easily
push the fold to land between `;` and `X-UID`, or (in principle) inside the
digits of the X-UID value itself. `stripICalPropertyParameter()` handles
this by first capturing the WHOLE logical property line as one match
(`^PROPERTY(?:[^\r\n]|\r?\n[ \t])*` — any non-terminator char, or a fold
sequence, repeated, which is RFC 5545's own definition of unfolding
applied without discarding the fold bytes elsewhere in the document), then
removing `;PARAM=value` from within that captured line with a second regex
that itself tolerates an embedded fold between the `;` and the parameter
name, and within the value
(`;(?:\r?\n[ \t])?PARAM=(?:[^:;\r\n]|\r?\n[ \t])*`). Everything outside the
targeted property's own logical line — including fold points on OTHER
properties — is left completely untouched (the match is anchored per-line
via `globalMatch`, not a blanket unfold-the-whole-document pass).

Direct unit coverage in `tests/calendar/tst_icaltimestamp.cpp`:
`stripsParameterFromMiddleOfLine`, `stripsParameterAcrossAFoldPoint`
(reproduces the exact real-world fold shape above),
`leavesUnrelatedPropertyWithSameParameterNameAlone` (an `ORGANIZER` with
its own `X-UID` in the same fixture survives when stripping `ATTENDEE`'s),
`noOpWhenParameterAbsent`, `noOpOnEmptyInput`.

### 1.2 `stripAttendeeXUid()` — `incidencecommonfields.{h,cpp}`

A one-line domain wrapper, `stripAttendeeXUid(icalBytes) =
stripICalPropertyParameter(icalBytes, "ATTENDEE", "X-UID")`, in
**exactly the same shape** as the existing `stripInjectedTimestamps()` a
few lines above it in the same file — both are "call the generic
`icaltimestamp.h` primitive with this domain's specific arguments, so
every demote call site does it identically instead of copying the call."
This was the natural place per the task's own framing ("ATTENDEE
promote/demote is already unified here via IP.6... check whether the
X-UID strip belongs here too") — the strip is genuinely a shared
post-serialization step across all three kinds, not domain-specific logic,
which is `incidencecommonfields.cpp`'s whole reason to exist.

Wired at the three call sites, immediately after `stripInjectedTimestamps`
(same line-adjacency the existing timestamp strip already establishes as
the "post-serialization cleanup" spot, before the verbatim-recurrence-line
injection that follows in all three functions):

```
eventcanonfields.cpp:585-588      journalcanonfields.cpp:279-282
vtodocanonfields.cpp:766-769
```

One call site per kind, not three copies of the regex call — matching the
campaign's "no triplicated implementations" discipline (IP.4/IP.6's
precedent).

## 2. `ORGANIZER` — investigated, confirmed unaffected

PLAN.md's text names `ATTENDEE` specifically. Verified rather than
assumed, two ways:

1. **Read `KCalendarCore::Person`** (`kcalendarcore/person.h`) — the class
   backing `Incidence::organizer()`. It has NO `uid`-shaped property at
   all — just name/email. `KCalendarCore::Attendee`
   (`kcalendarcore/attendee.h:123`) is a SEPARATE class (not a `Person`
   subclass) and DOES carry `Q_PROPERTY(QString uid READ uid WRITE
   setUid)` — this is the property whose unset default is heap-derived and
   becomes the serialized `X-UID`.
2. **Empirical probe**, built and run standalone against KCalendarCore
   only (same style as `probes/kcalendarcore-probe.cpp`): an `Event` with
   BOTH an `ORGANIZER` and an `ATTENDEE`, serialized twice in two separate
   process invocations. `ORGANIZER;CN=Org:MAILTO:org@example.com` was
   byte-identical across both runs; the `ATTENDEE` line's `X-UID` differed
   (`94023475731472` vs `94370153249808`).

**Conclusion: `ATTENDEE`-only, confirmed, not expanded.** No code changes
to `ORGANIZER` handling. `tst_icaltimestamp.cpp`'s
`leavesUnrelatedPropertyWithSameParameterNameAlone` slot also pins this
at the regex level (an `ORGANIZER;X-UID=...` in the same fixture survives
when the strip targets `ATTENDEE`'s `X-UID`), belt-and-suspenders against
a future regression that widens the property match too far.

## 3. Nothing round-trips `X-UID` deliberately

Grepped `X-UID` across `src/` and `tests/` before touching anything (per
PLAN.md's explicit instruction to stop and say so if a call site depends
on it): zero hits anywhere in the pre-existing tree. Confirmed again after
landing the fix — the only hits now are this item's own new code (the
strip helper's own name/regex literal, and the new tests that assert its
absence). Safe to remove unconditionally.

## 4. The cross-process test — mechanism and why

PLAN.md offers two named mechanisms (a committed golden-bytes file, or
forking the test process) and asks for the choice to be argued.

**Chose neither literally — a third option in the same spirit as
"forking," safer and more direct than either named one.** `tools/
demotepurityprobe` (new, unconditionally built — no vendor credentials or
network involved, same as `tools/matrixgen`) demotes a small fixed canon
fixture (uid, summary, start, two `ATTENDEE`s) through all three kinds and
prints the results to stdout. `tests/calendar/tst_demote_purity.cpp`
launches this binary TWICE via `QProcess` and diffs the two stdout
captures byte-for-byte.

**Why not a raw `fork()`:** grepped the codebase first, as instructed —
zero existing fork-based test infrastructure (`grep -rln "fork("`
returns nothing). Building one from scratch inside a QTest binary is the
textbook unsafe case: `fork()` in a process that may have multiple
threads (Qt/QTest processes routinely do — the event loop, thread pools)
is only safe up to an immediate `exec()`; anything else risks a hung or
corrupted child. `QProcess` launching a genuinely separate executable
sidesteps this entirely while proving the exact same claim — "two demotes,
two different OS processes" — with a standard, portable, already-idiomatic
Qt mechanism (`tests/plugin/tst_pluginmanager_smoke.cpp` already uses the
identical `$<TARGET_FILE:...>`-compile-definition pattern to locate a
sibling built binary at test time; reused verbatim here as
`DEMOTE_PURITY_PROBE_PATH`).

**Why not (only) a committed golden-bytes file:** a golden file proves
"today's output matches whatever a PAST, uninspectable invocation
produced" — real evidence, but one step removed from the literal claim
("two DIFFERENT PROCESSES, not two moments in time"). The QProcess
mechanism proves the literal claim directly, within a single test run,
with no committed fixture to keep in sync by hand. (A golden file was
considered and rejected as strictly weaker evidence for equal complexity,
not because it wouldn't have worked.)

**DTSTAMP — a real, out-of-scope non-determinism source stumbled on while
designing this test, logged not fixed, per PLAN.md's own "don't dramatise
this... don't chase it" instruction.** `KCalendarCore::ICalFormat::
toICalString()` unconditionally regenerates `DTSTAMP` to wall-clock "now"
on every call. This is CORRECT RFC 5545 semantics (DTSTAMP = "date/time
that the instance of the iCalendar object was created"), already
documented as such in `docs/campaign/FINDINGS.md` (search "DTSTAMP" —
first noted long before this campaign) — not a new finding, not a bug,
and explicitly not O90 (O90 is about a property that should be a pure
function of canon and isn't; DTSTAMP is not, and was never meant to be, a
function of canon at all). Left as-is in `src/`. The probe strips it from
its own printed output before comparison
(`Calendar::stripICalPropertyLine(bytes, "DTSTAMP")`, the EXISTING public
helper, used exactly as any other caller would) so the test is isolated to
the one property IP.12 owns. No new FINDINGS entry — this is not new
information, just relevant context for why the probe needed the extra
line.

## 5. Non-vacuity — verified, not assumed

Temporarily commented out the `stripAttendeeXUid(icalBytes)` call in
`eventcanonfields.cpp` only (leaving the helper functions and the other
two call sites intact — a surgical single-line revert, not a broad
`git stash`), rebuilt `demotepurityprobe` + `tst_demote_purity`, and ran:

```
$ ./build/tests/calendar/tst_demote_purity
FAIL!  : TestDemotePurity::twoProcessesDemoteTheSameCanonToByteIdenticalOutput()
         '!out1.contains("X-UID")' returned FALSE.
         (probe 1 must not carry the heap-derived X-UID)
Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 40ms
```

A real, believable failure for exactly the expected reason. Also ran the
probe twice by hand against the reverted build and diffed the raw output:

```
$ diff <(./build/tools/demotepurityprobe/demotepurityprobe) \
       <(./build/tools/demotepurityprobe/demotepurityprobe)
8c8
<  CUTYPE=INDIVIDUAL;X-UID=94344383628560:mailto:a@example.com
---
>  CUTYPE=INDIVIDUAL;X-UID=93838730773776:mailto:a@example.com
10c10
<  CUTYPE=INDIVIDUAL;X-UID=94344383628752:mailto:b@example.com
---
>  CUTYPE=INDIVIDUAL;X-UID=93838730773968:mailto:b@example.com
```

Confirms the underlying byte-diff the test's `QCOMPARE(out1, out2)` would
independently have caught too, not just the guard assertions. Restored the
one line, rebuilt, reconfirmed green (both `tst_demote_purity` and the
full suite, §6).

## 6. Full suite and campaign closure

Full clean build (`cmake --build build -j$(nproc)`, after a fresh
`cmake -S . -B build`): no errors (only the pre-existing `QDateTime`/
`Qt::LocalTime` deprecation warnings from lines this item did not touch).

`ctest --test-dir build --output-on-failure` (full, unfiltered): **217
tests, 213 passed, 4 known-environmental failed**
(`tst_backend_signals`, `tst_backend_thread_relocation`,
`tst_backend_reentrancy_pin`, `tst_remotecalendarbackend`) — verified by
failure TEXT, not name: `tst_backend_signals` and `tst_remotecalendarbackend`
show the local-Radicale 412/409 create-item-failure class; `tst_backend_
thread_relocation` aborts the subprocess (same crash-handler stack-trace
shape as prior sessions); `tst_backend_reentrancy_pin` shows the
documented KDAV 30s-transfer-timeout / "would have been sufficient"
signature twice. All four match the pre-existing baseline exactly; no new
red. New executable count: **216 → 217 (+1)**, `tst_demote_purity`,
matching the expected growth (`tst_icaltimestamp` gained slots inside its
existing binary, no count change from that file).

`./build/tools/matrixgen/matrixgen` regenerated and diffed against the
committed `docs/campaign/eee/CONVERGENCE-MATRIX.md`: **byte-identical**
(confirmed by `diff`, exit 0) — expected, since X-UID never reaches canon
and no `LossProfile`/`TransformationEdge` was touched.
`tst_gm_pipeline_convergence` green in the full run, confirming this by
execution too. No `edges()`/`peerShapes()` list grew, so house rule O63's
grep-pin requirement does not apply.

### Campaign closure

`docs/campaign/incidence-parity/PLAN.md` §A.5's six success conditions
were all closed by earlier items (1: IP.3+IP.8; 2: IP.8+IP.9; 3: IP.4+
IP.5+IP.6; 4: IP.3; 5: IP.6+IP.7+IP.10; 6: IP.11) — IP.12 (O90) was not one
of the six numbered conditions itself, but was the campaign's last queued
item per `STATUS.md`'s table. With IP.12 landing, **every row in
`STATUS.md`'s "Where we stand" table is DONE** and no un-done item
remains. `STATUS.md` and `CLAUDE.md` are both updated in this commit to
say so explicitly — **campaign CLOSED, 2026-09-03** — following the
one-line-per-campaign pattern of CLAUDE.md's existing "Closed campaigns &
resolved followups" index, rather than being left with a stale "next item"
pointer. No new closing-summary document was written — the existing
`STATUS.md` session log (fourteen dated entries, the accumulated per-item
record) already IS the closing summary; a human can decide whether a
separate consumer-facing closing document is warranted, per the task's
own instruction not to build one unasked.

## 7. Corner cases declared-not-executed

- **Did not build a maximal-RFC-5545-fixture version of the cross-process
  probe** (IP.8's `tst_incidence_rfc5545_fidelity.cpp` discipline). This
  item's fixture is deliberately minimal (uid/summary/start/two
  attendees) — the acceptance criterion is specifically about `ATTENDEE`
  purity, not RFC 5545 coverage breadth, which IP.8 already owns and
  IP.11 already reused for a different question. A maximal fixture would
  exercise more code paths without testing anything new about the ONE
  property this item owns.
- **Did not investigate whether any OTHER KCalendarCore-generated
  identifier is similarly heap-derived**, beyond `ATTENDEE`'s `X-UID` and
  the already-known, already-correct `DTSTAMP` case (§4). Per PLAN.md's
  explicit "don't go hunting for other non-determinism sources... unless
  you stumble on one, in which case log it, don't chase it" — none was
  stumbled on beyond the two discussed above, so none is logged.
- **Did not add a `demotepurityprobe` variant that exercises the vendor
  (Google/MS) demote legs.** Confirmed by reading, not assumed: none of
  `mseventcanonstages.cpp`/`googlecanonstages.cpp`/
  `googletaskcanonstages.cpp`/`mstodotaskcanonstages.cpp` calls
  `KCalendarCore::ICalFormat::toICalString()` at all — they build vendor
  JSON directly — so none of them can produce an `X-UID`-bearing line in
  the first place. Nothing to test there.
