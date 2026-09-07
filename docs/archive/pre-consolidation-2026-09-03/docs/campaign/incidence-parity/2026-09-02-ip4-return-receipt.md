# Return receipt — IP.4: shared VALARM shape module

**Delivered:** 2026-09-02
**Consumes:** `docs/campaign/incidence-parity/PLAN.md` §1 (execution
rules, binding), the IP.4 body section, and Amendment 1 §A.3.1 (adds O85
to scope); `docs/campaign/FINDINGS.md` O79 and O85 (the two defects this
item closes); `src/todo/vtodocanonfields.cpp`'s W5 block (the reference
implementation, moved verbatim); `tests/calendar/tst_incidence_rfc5545_fidelity.cpp`
(IP.8's VALARM sub-gate, already scaffolded with QEXPECT_FAILs naming
this item).
**Scope discipline — `git status` at landing time:** `CLAUDE.md`,
`docs/campaign/FINDINGS.md` (O79, O85 → RESOLVED, landed notes appended),
`docs/campaign/incidence-parity/STATUS.md` + this receipt, new
`src/calendar/alarmshape.{h,cpp}`, `CMakeLists.txt` (registers the new
files), `src/calendar/eventcanonfields.cpp`, `src/todo/vtodocanonfields.cpp`,
`src/calendar/mseventcanonstages.cpp`, `src/calendar/googlecanonstages.cpp`,
`tests/calendar/{tst_calendar_canon_roundtrip,tst_incidence_rfc5545_fidelity,
tst_ms_event_canon_edge,tst_google_event_canon_edge}.cpp`,
`tests/todo/tst_todo_canon_roundtrip.cpp`. `journalcanonfields.cpp`
untouched (confirmed, §5). No loss profile enum values changed, so
`docs/campaign/eee/CONVERGENCE-MATRIX.md` is untouched (regenerated and
diffed byte-identical, not committed since nothing changed).

---

## 0. Summary

O79 (VEVENT alarm promote/demote corrupting non-start-relative triggers)
and O85 (every alarm round-tripping back disabled) are both closed by one
new shared module, `src/calendar/alarmshape.{h,cpp}`, and four call-site
fixes landed together as the plan required (fixing promote alone would
have made VEVENT round-tripping worse).

The module's `alarmToJson()`/`alarmFromJson()` are W5's VTODO logic moved
**verbatim** — same row shape, same trigger-form branching, same
REPEAT/DURATION comment explaining the deliberately-scoped-out
`snoozeTime()` ambiguity (not reopened here). The new
`describeAlarmRow()` classifier (`AlarmRowForm::{StartRelative,
EndRelative, Absolute, Malformed}`) has no prior copy — it exists
specifically so `mseventcanonstages.cpp`/`googlecanonstages.cpp` can ask a
row its actual form instead of inferring one from a possibly-defaulted
key, which is exactly the O79 bug class at those two sites.

The Google-site investigation (§3 below) is the one place this item found
PLAN.md's stated premise wrong: MS's and Google's demote guards are NOT
"the same reader shape". MS's absolute-alarm case was genuinely broken;
Google's was already accidentally correct. Only Google's END-related case
shared the bug. Both are now fixed the same way regardless (via
`describeAlarmRow()`), so the code ends up symmetric even though the
starting bugs were not.

---

## 1. Module placement and design

`src/calendar/alarmshape.{h,cpp}`, per PLAN.md's stated default —
`KCalendarCore::Alarm` is a calendar-layer type, and `src/shape/` is
deliberately domain-neutral (it hosts `CanonJsonDiffer`/`CanonJsonMerger`/
`LossProfile`/registries — generic machinery with no KCalendarCore
dependency at all). Putting a `KCalendarCore::Alarm`-typed API there would
be the odd one out. No reason found to argue against the plan's own
recommendation.

Three functions, matching the plan's spec exactly:

- `QJsonObject alarmToJson(const KCalendarCore::Alarm::Ptr&)`
- `KCalendarCore::Alarm::Ptr alarmFromJson(const QJsonObject&, KCalendarCore::Incidence*)`
- `AlarmRowForm describeAlarmRow(const QJsonObject&)`

`alarmToJson`/`alarmFromJson` are declared in `Kalburator::Calendar` (same
namespace `incidencecommonfields.{h,cpp}` already uses) so
`eventcanonfields.cpp` — itself in that namespace — calls them
unqualified, and `vtodocanonfields.cpp` pulls them in with the same
`using Kalburator::Calendar::X;` discipline it already uses for
`incidencecommonfields.h`'s exports. `mseventcanonstages.cpp` and
`googlecanonstages.cpp` (both in an anonymous namespace, not
`Kalburator::Calendar`) pull in only `describeAlarmRow`/`AlarmRowForm` —
they never construct or read a real `KCalendarCore::Alarm`, only the JSON
row shape, so `alarmToJson`/`alarmFromJson` are not relevant there.

Deliberately **not** merged into `incidencecommonfields.{h,cpp}`: that
module's functions all share the `(QJsonObject&, Incidence::Ptr)` /
`(const QJsonObject&, Incidence::Ptr)` signature pair operating on a whole
incidence's field; the alarm functions operate on one alarm row at a time
inside a loop the caller owns, and `describeAlarmRow` has no incidence
parameter at all. Different enough shape that a separate file reads
cleaner than shoehorning it into the existing one.

---

## 2. O85 — the `enabled` decision

**Decision: PLAN.md's recommended option, adopted as-is.**
`alarmFromJson()` calls `alarm->setEnabled(true)` unconditionally, on
every row, for both VEVENT and VTODO. No `"enabled"` row key was added —
the row shape is unchanged from W5's.

**Argument.** RFC 5545 has no VALARM property or parameter that
represents "this alarm is disabled". `X-KDE-KCALCORE-ENABLED` is
KCalendarCore's own invention with no counterpart on the wire and no
meaning to any non-KDE client. Concretely: take a real KOrganizer alarm
with `enabled() == false`, serialize it with stock
`KCalendarCore::ICalFormat::toICalString()` (no libkalburator involved at
all), and re-parse those bytes with the same library — the alarm comes
back `enabled() == true`, because the X-prop only round-trips through
KCalendarCore's own reader/writer pair when nothing else touches the
bytes in between; any other client (a phone, a CalDAV server's own ICS
export, another desktop app) reading the identical wire bytes has never
seen the disabled bit at all. So a disabled KOrganizer alarm crossing
*any* serialization boundary that isn't KCalendarCore-to-KCalendarCore
already loses this information today, independent of libkalburator. There
is nothing on the wire for a row key to preserve that isn't already lost
the moment the alarm leaves KCalendarCore's own process.

**Proof, not just argument.** New slots
`veventAlarmEnabledSurvivesRoundTrip()`
(`tests/calendar/tst_calendar_canon_roundtrip.cpp`) and
`vtodoAlarmEnabledSurvivesRoundTrip()`
(`tests/todo/tst_todo_canon_roundtrip.cpp`) each parse a source ICS with
an alarm that has no `X-KDE-KCALCORE-ENABLED` marker (so KCalendarCore's
own parser defaults it to `enabled()==true`, matching every realistic
non-KDE-authored ICS), assert the fixture is enabled before promoting,
round-trip through promote→demote, and assert the demoted-and-reparsed
alarm is still enabled. Both pass. `tst_incidence_rfc5545_fidelity.cpp`'s
eight VALARM sub-gate slots also assert `rtAlarm->enabled()` for every
trigger form on both kinds — all real green now (QEXPECT_FAIL removed).

No case was found where this loses something a source COULD represent —
because RFC 5545 never gave it anything to represent in the first place.

---

## 3. Google-site investigation — PLAN.md's premise did NOT hold

The task brief (and PLAN.md) frame `googlecanonstages.cpp`'s demote site
as reading "the same reader shape" as MS's. Read both before touching
either:

- **MS (`mseventcanonstages.cpp`, pre-fix):** `offsetSecs <= 0 &&
  offsetSecs % 60 == 0`. An `"at"`-shaped row has no `"offset"` key,
  `.toInt()` defaults to 0, `0 <= 0` is **true** → the absolute alarm was
  silently mapped to `reminderMinutesBeforeStart: 0`. **Genuinely broken.**
- **Google (`googlecanonstages.cpp`, pre-fix):** `offsetSecs < 0 &&
  offsetSecs % 60 == 0` — **strictly** negative. The same defaulted-zero
  absolute row fails `0 < 0` and was **already routed to the carrier**,
  correctly, before this item touched the file. **Not broken.**

What Google's site actually had wrong: an END-related row
(`"related":"end"`, still carrying a genuine negative `"offset"`, e.g.
`-300` for "5 minutes before the end") was never distinguished from a
start-relative row. Since its offset is negative and a multiple of 60, it
passed the exact same guard a legitimate start-relative alarm would, and
would have been wrongly mapped to a Google `reminders.overrides[]` entry
measured from the event's **start**, not its end.

Both sites are now fixed identically — require
`describeAlarmRow(a) == AlarmRowForm::StartRelative` before mapping to
the vendor's native field, else route to the carrier — even though only
one of the two starting bugs was real. This is recorded explicitly in
both files' new comments so a future reader doesn't assume the two edges
were symmetric before this item, when they weren't.

New coverage: `absoluteAlarmIsCarriedNotCoerced()` and
`endRelatedAlarmIsCarriedNotCoerced()` in both
`tst_ms_event_canon_edge.cpp` and `tst_google_event_canon_edge.cpp` —
each asserts the non-start-relative row is NOT coerced into the native
field, IS carried (SVEP for MS, `extendedProperties.private` for Google),
and re-promotes to the identical row via the carrier merge logic already
in place. All four pass.

The MS promote direction (`isReminderOn`+minutes → row) and the Google
promote direction (`reminders.overrides[]` → row) were also verified, not
assumed: both vendor wire models can only ever express "N minutes before
start", so both promote directions only ever construct a bare-`"offset"`
(start-relative) row — never `"at"` or `"related"`. No fix was needed on
either promote side; verification comments added at both sites so the
next reader doesn't have to re-derive this.

---

## 4. Loss profile verdicts — re-verified, left unchanged

`alarms: LossKind::Simplified` stays correct on both
`canonToMsEventLoss()` (`mseventcanonstages.cpp`) and the Google
equivalent (`googlecanonstages.cpp`). Reasoning: the ONE alarm that maps
to a vendor's native reminder field (MS `isReminderOn` +
`reminderMinutesBeforeStart`; Google `reminders.overrides[]`) still loses
its `text`/`repeatCount`/`repeatIntervalSecs` on that leg — neither
vendor's native reminder concept carries a message string or a snooze
pairing, and the demote code does not additionally stash the "extra"
fields of the native-mapped alarm into the carrier (only the alarms that
DON'T become the native field are carried). That loss is real, was
already true before this item (it is why the profile said Simplified in
the first place, not a byproduct of O79), and is orthogonal to O79 (which
was about a row being *misclassified* into the wrong vendor concept
entirely, not about the native mapping's own reduced fidelity once
classification is correct).

Confirmed mechanically, not just argued: `./build/tools/matrixgen/matrixgen`
output diffed byte-identical against the committed
`docs/campaign/eee/CONVERGENCE-MATRIX.md`, and
`tst_gm_pipeline_convergence`'s `committedMatrixMatchesGenerated` slot
passes. No matrix regen was committed since there was nothing to
regenerate.

The two loss-profile prose docs
(`docs/2026-08-23-ms-event-edge-loss-profile.md`,
`docs/2026-08-23-google-event-edge-loss-profile.md`) already described the
INTENDED behavior this item's fix now actually implements ("VALARMs
beyond the first / non-display actions / absolute triggers → carried…" /
"…audio/procedure/absolute-TRIGGER alarms carried verbatim…") — the code
just didn't match that prose until now. Neither doc needed editing.

---

## 5. O64 crossing-gate check

IP.4 does not add a new vendor pair or domain edge — the
`{calendar,canon} ⇄ {ms-event,google-event}` edges already existed and
already carried an `alarms` loss-profile entry; this item corrects
existing behavior on those edges, it doesn't create new ones. So O64's
"crossing-gate coverage mandatory for any new vendor pair/domain edge"
does not strictly apply. Checked `tests/convergence/tst_gm_pipeline_convergence.cpp`
directly (the crossing gate for calendar/todo/contacts): its fixtures
(`kGoogleEvent`, `kMsEvent`) carry no `reminders`/alarms at all, before or
after this item — so alarms have never been exercised by that gate. Left
untouched; not a regression this item introduces, and closing that
pre-existing gap is not required by O64 (no new edge) or by any of IP.4's
named acceptance criteria. Noted here rather than silently left for a
future reader to wonder about.

Direct coverage of the fixed behavior instead comes from the dedicated
per-vendor "carried not coerced" slots (§3) plus the VEVENT/VTODO
round-trip slots (§6) — narrower than a full pipeline crossing gate, but
targeted exactly at what changed.

---

## 6. New tests

- `tests/calendar/tst_calendar_canon_roundtrip.cpp`: `veventAlarmAbsoluteFormRoundTrips`,
  `veventAlarmEndRelatedFormRoundTrips`, `veventAlarmRepeatDurationPairRoundTrips`,
  `veventAlarmEnabledSurvivesRoundTrip` — VEVENT's first-ever coverage of
  the non-start-relative trigger forms (it previously had none beyond an
  implicit plain start-relative alarm inside `kTestIcal`).
- `tests/todo/tst_todo_canon_roundtrip.cpp`: `vtodoAlarmEnabledSurvivesRoundTrip`
  (new — proves O85's fix on the VTODO leg too, not just VEVENT). All
  pre-existing W5 VALARM slots re-run **unchanged** and green — proof the
  extraction into `alarmshape.cpp` was behaviour-preserving.
- `tests/calendar/tst_ms_event_canon_edge.cpp`: `absoluteAlarmIsCarriedNotCoerced`,
  `endRelatedAlarmIsCarriedNotCoerced`.
- `tests/calendar/tst_google_event_canon_edge.cpp`: `absoluteAlarmIsCarriedNotCoerced`,
  `endRelatedAlarmIsCarriedNotCoerced`.
- `tests/calendar/tst_incidence_rfc5545_fidelity.cpp`: all eight VALARM
  sub-gate slots (`{vevent,vtodo}Alarm{StartRelative,EndRelative,Absolute,
  RepeatDuration}`) lost their O79/O85 `QEXPECT_FAIL`s — real green now,
  per the house convention ("when the fix lands, the slot must lose its
  QEXPECT_FAIL rather than be deleted"). `runAlarmCase()`'s
  `expectFormCorrupted` parameter was removed since it was always `false`
  after the fix.

---

## 7. VJOURNAL — confirmed untouched

RFC 5545 §3.6.3's `jourprop` grammar has no VALARM component for
VJOURNAL. `src/calendar/journalcanonfields.cpp` was not opened for
editing by this item (confirmed by the `git status` scope list above) and
has no alarm-related code to have been affected either way. Recorded here
explicitly per the task brief, so the next reader doesn't re-derive it.

---

## 8. Full suite

Build: clean, zero errors (two 10-minute builds were needed only because
of environmental memory pressure killing the first `make -j$(nproc)` run
partway through at 98% — not a code issue; the resumed incremental build
completed cleanly to 100%).

`ctest --test-dir build`: **215 tests, 211 passed, 4 failed** — the same
4 pre-existing environmental failures the campaign has carried since
before this item (`tst_backend_signals`, `tst_backend_thread_relocation`,
`tst_backend_reentrancy_pin`, `tst_remotecalendarbackend`), verified by
failure TEXT: CalDAV `412 Precondition Failed` / KDAV transfer timeouts
against the local Radicale server, not by test name. Ctest binary count
unchanged at 215 (new coverage landed as QTest slots inside existing
binaries, matching IP.3/IP.6/IP.9/IP.10's own precedent — the ctest
binary count is not the axis that grows here).

Targeted runs (all green, run first for fast feedback before the full
suite): `tst_calendar_canon_roundtrip` (19/19), `tst_todo_canon_roundtrip`
(42/42), `tst_incidence_rfc5545_fidelity` (13/13),
`tst_ms_event_canon_edge` (13/13), `tst_google_event_canon_edge` (10/10),
`tst_gm_pipeline_convergence` (10/10, including
`committedMatrixMatchesGenerated`).

---

## 9. New FINDINGS

None. No bug was found outside O79/O85's declared scope during this item.
(The native-mapped-alarm text/repeat loss discussed in §4 is not a new
finding — it's the pre-existing, already-declared reason the `Simplified`
verdict existed before this item touched either vendor file.)
