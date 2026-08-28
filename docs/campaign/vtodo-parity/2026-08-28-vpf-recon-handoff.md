# VP.f (W5 + W6.2 + W7 + O74) — alarm shape extension, malformed-date coercion, passthrough verification — RECON & handoff

**Status:** EXPLORATION COMPLETE 2026-08-28; implementation NOT started.
This doc persists the code map so a fresh agent picks up VP.f without
re-exploring. Written as a direct sibling to `2026-08-26-w4-recon-handoff.md`
(W4, implemented) and `2026-08-27-w3-recon-handoff.md` (W3, implemented) —
same section structure, same citation discipline. Decisions come from
`docs/2026-08-25-vtodo-parity-handoff-response.md` (binding, quoted
verbatim below per sub-item) and `docs/campaign/FINDINGS.md` O74 (binding
problem statement for the differ-blind-spot fix).

VP.f bundles three handoff items that share files but are otherwise
independent: **W5** (VALARM shape extension), **W6.2** (malformed
DTSTART/DUE coercion + a bonus DATE round-trip fix), and **W7** (passthrough
verification table + round-trip tests), with **O74** (todo differ blind to
X-prop-only edits) folded into W7 per the response doc's own instruction.
Recommend landing them as up to three separate commits (W5, W6.2, W7+O74)
rather than one — they touch overlapping files but are logically
severable and each has its own test surface.

All prior VTODO-parity items are COMPLETE: W8 (VP.a), W2 (VP.b), W1 (VP.c),
W4 (VP.d), W3 (VP.e). Nothing in VP.f code has been written. Suite baseline
per STATUS.md: 214 tests, 210 passing + 4 known environmental Radicale
failures (unrelated to this repo's own code — reproduce pre-session).

---

## W5 — VALARM shape extension

### What W5 is (per the response doc, binding — verbatim)

`docs/2026-08-25-vtodo-parity-handoff-response.md` §"W5 — VALARM —
**ACCEPTED, partially built already**" (lines 156–165):

> Todo canon ALREADY models alarms (`alarms[] {type, offsetSecs, text}`,
> vtodocanonfields.cpp:216/:417; catalogued; MS maps single reminder ⇄
> alarms[0]; Google declares Dropped). Gap vs your spec: no absolute-trigger
> form, no RELATED=START/END discrimination, no REPEAT/DURATION. Delivery:
> extend the alarm row shape (additive JSON keys — old rows stay valid),
> per-backend trait flags via W8, producer-distrust list = none for us
> (your Nextcloud<0.6-style gating is tasks.org-specific; we expose
> producerId and let YOU gate).

**Correction to the response doc's own citation:** the field is literally
named `"offset"` in the current code, not `"offsetSecs"` — see Code map §1.
This matters because "additive JSON keys, old rows stay valid" is the
binding constraint; do not rename `"offset"` to match the prose.

### Design sketch (decide before coding)

1. **No new catalogued key needed.** `alarms` (`PropertyKind::Json`) is
   already catalogued (`todocanonproperties.cpp:56`) and covers the whole
   array structurally — W5 only adds new optional sub-keys inside each row
   object. Unlike W3/W4/O74, this item touches zero lines of
   `todocanonproperties.cpp`.
2. **New row shape (additive, superset of today's):**
   ```jsonc
   {
     "type": 1,            // unchanged: KCalendarCore::Alarm::Type int
     "text": "…",          // unchanged: optional
     // exactly one of the following two forms (mutually exclusive per
     // RFC5545 TRIGGER value type):
     "offset": -900,        // unchanged key name; seconds; offset-form trigger
     "related": "end",      // NEW, optional; only meaningful alongside "offset".
                             // Absent == "start" (back-compat: every existing
                             // row implicitly meant start-relative, so old rows
                             // stay valid with zero change).
     "at": "2026-09-01T16:30:00Z",  // NEW, optional; absolute-form trigger
                                     // (RFC5545 TRIGGER;VALUE=DATE-TIME, which
                                     // RFC5545 §3.8.6.3 REQUIRES to be UTC — a
                                     // plain ISO string is sufficient, no tz
                                     // sidecar needed, unlike `start`/`due`'s
                                     // {dateTime,tz} shape).
     // REPEAT/DURATION — RFC5545 requires these as a PAIR (if one occurs the
     // other MUST occur); only emit/consume both together:
     "repeatCount": 3,          // NEW, optional; VALARM REPEAT
     "repeatIntervalSecs": 300  // NEW, optional; VALARM DURATION (companion)
   }
   ```
   `"at"` and `"offset"` are mutually exclusive per row (mirrors
   `KCalendarCore::Alarm::hasTime()` vs `hasStartOffset()`/`hasEndOffset()`,
   Code map §2 — these are already mutually exclusive at the KCalendarCore
   level, so promote/demote just needs to preserve that exclusivity, not
   invent it).
3. **Promote** (`todoFieldsToCanon`, alarm block
   `vtodocanonfields.cpp:328-341`): for each `KCalendarCore::Alarm::Ptr`:
   - if `alarm->hasTime()` → emit `"at"` = `alarm->time().toUTC().toString(Qt::ISODate)`, omit `"offset"`/`"related"`.
   - else if `alarm->hasEndOffset()` → emit `"offset"` = `alarm->endOffset().asSeconds()`, `"related"` = `"end"`.
   - else (default / `hasStartOffset()`) → emit `"offset"` = `alarm->startOffset().asSeconds()` (unchanged), omit `"related"` (implicit `"start"`).
   - if `alarm->repeatCount() > 0` → emit `"repeatCount"` = `alarm->repeatCount()`
     and `"repeatIntervalSecs"` = `alarm->snoozeTime().asSeconds()` (both
     together; KCalendarCore's own REPEAT/DURATION pairing invariant means
     a nonzero `repeatCount()` implies a meaningful `snoozeTime()`, but
     verify empirically — see Open decision 3).
4. **Demote** (`canonObjectToVtodoBytes`, alarm block
   `vtodocanonfields.cpp:622-636`): read `"at"` first (if present,
   `alarm->setTime(QDateTime::fromString(...))`); else read `"offset"` +
   `"related"` (`"related"=="end"` → `setEndOffset`, else `setStartOffset`,
   unchanged default path); then if both `"repeatCount"` and
   `"repeatIntervalSecs"` are present, `alarm->setRepeatCount(n)` +
   `alarm->setSnoozeTime(KCalendarCore::Duration(intervalSecs))`.
5. **Real existing bug this motivates fixing (not hypothetical):** today's
   promote *unconditionally* reads `alarm->startOffset()`
   (`vtodocanonfields.cpp:334`) regardless of trigger form. For an
   **absolute-trigger** VALARM or an **END-related** offset VALARM already
   present on an incoming wire VTODO, `startOffset()` returns a zero
   `Duration` (per the KCalendarCore doc comment: "If the alarm's time is
   not defined in terms of an offset relative to the start… returns
   zero" — Code map §2), so today's promote silently corrupts such alarms
   into a bogus `offset: 0`. W5 is not purely additive in effect — it also
   **fixes a live data-loss bug** for any VTODO carrying either of those
   two (currently-mishandled) trigger forms. State this plainly in the
   return receipt.
6. **Capability struct — recommend NO new field.** See Open decision 1.
7. **Loss profile:** no NEW LossProfile entries needed on any of the three
   todo edges purely for this shape extension — `alarms` is not currently
   listed in any of the three `canonTo*Loss()` functions at all (grep
   confirms), meaning it is implicitly treated as losslessly-mapped on the
   vtodo leg (true: symmetric promote/demote) — **except** MS, where
   `alarms` already appears in `canonToMsTodoTaskLoss()`'s **Simplified**
   list (`mstodotaskcanonstages.cpp:554`, single-reminder-only ruling,
   unchanged by W5) and Google's `canonToGoogleTaskLoss()`'s **Dropped**
   list (`googletaskcanonstages.cpp:270`, unchanged — Google Tasks has no
   alarm field at all). Confirm these two existing entries remain correct
   (they do — MS/Google structural limits are unrelated to the new
   sub-keys) rather than adding new ones.

### The MS-leg shape mismatch (real, found during recon — Open decision 2)

`mstodotaskcanonstages.cpp` does **not** produce or consume the
`{type, offset/at, related, text, repeatCount, repeatIntervalSecs}` row
shape at all. Promote (`:236-247`) builds `alarms[0] = {"reminder":
{dateTime, tz}}` — a **different sub-shape** nested under a `"reminder"`
key, copying Graph's raw `reminderDateTime` object verbatim. Demote
(`:418-428`) reads that same `"reminder"` sub-key back out. This is
self-consistent in isolation (MS→canon→MS round-trips fine) but:

- It does **not** match the vtodo leg's `{type, offset, text}` shape at
  all — the W7 truth table's "single reminder ⇄ alarms[0]" claim is only
  true by field-array-position coincidence, not by JSON-shape agreement.
- **Concrete existing bug:** an MS-sourced canon record (`alarms[0] =
  {"reminder": {...}}`) demoted through `canonObjectToVtodoBytes` today
  reads `a.value("type").toInt()` (missing → `0` = `Alarm::Invalid`) and
  `a.value("offset").toInt()` (missing → `0`), producing a bogus
  zero-offset Invalid-type VALARM on the VTODO/CalDAV leg — i.e., **any
  cross-vendor sync of an MS-To-Do reminder to a CalDAV/local VTODO
  destination today silently mangles the alarm.** This is exactly the
  kind of cross-edge corruption vendor-convergence exists to catch (O64
  discipline: crossing-gate coverage is mandatory for every vendor pair).
- MS's `reminderDateTime` is **inherently an absolute trigger** (Graph
  gives an absolute `{dateTime, timeZone}`, never an offset) — W5's new
  `"at"` key is the natural, already-motivated home for it.

Recommend folding the MS-shape unification into this item (small, and the
new `"at"` key makes it nearly free): promote emits
`alarms[0] = {"type": <KCalendarCore::Alarm::Display>, "at": <UTC ISO from
reminderDateTime.dateTime, timezone-converted via the same
`msDateTimeToJson`/date-parsing helpers already in this file>}`; demote
reads `alarms[0]`'s `"at"` (falling back to `"offset"` computed against
`start`/`due` only if no `"at"` present, for the — currently
never-populated, since MS only ever emits `"at"` after this fix — case of
an offset-only alarm arriving from elsewhere). This is flagged as an
**explicit recommendation, not binding response-doc text** — the
implementing agent should confirm before folding it in, since it goes
beyond the literal W5 ask (see Open decision 2).

### Code map (verified 2026-08-28)

1. **Current alarm shape (vtodo leg):**
   - Promote: `src/todo/vtodocanonfields.cpp:326-341`. Fields emitted:
     `type` (int, `alarm->type()`), `offset` (int seconds, **not**
     `offsetSecs` — `alarm->startOffset().asSeconds()`, unconditionally
     start-offset — see the bug above), `text` (optional,
     `alarm->text()`).
   - Demote: `src/todo/vtodocanonfields.cpp:621-636`. Reads `type`,
     `offset` (always via `setStartOffset`), `text`.
   - Catalogue entry: `src/todo/todocanonproperties.cpp:56` —
     `PropertyId{"alarms"}, PropertyKind::Json`.
2. **KCalendarCore::Alarm API surface** (verified against
   `/usr/include/KF6/KCalendarCore/kcalendarcore/alarm.h`):
   - `void setTime(const QDateTime&)` / `QDateTime time() const` /
     `bool hasTime() const` (`:441-473`) — absolute trigger.
   - `void setStartOffset(const Duration&)` / `Duration startOffset()
     const` / `bool hasStartOffset() const` (`:483-500`) — RELATED=START
     (or unspecified, which RFC5545 defaults to START).
   - `void setEndOffset(const Duration&)` / `Duration endOffset() const` /
     `bool hasEndOffset() const` (`:510-527`) — RELATED=END.
   - `void setRepeatCount(int)` / `int repeatCount() const` (`:572-579`) —
     VALARM REPEAT.
   - `void setSnoozeTime(const Duration&)` / `Duration snoozeTime() const`
     (`:554-561`) — VALARM DURATION (the companion property to REPEAT;
     despite the "snooze" naming this is literally the RFC5545
     REPEAT/DURATION pair, per the class's own doc comments cross-
     referencing `repeatCount()`).
   - `Duration duration() const` (`:613`) — *computed* total span (initial
     occurrence → final repetition), not itself a settable RFC5545
     property; not needed for promote/demote, only mentioned for
     completeness.
   - `KCalendarCore::Duration::asSeconds()` — already used by the current
     code (`vtodocanonfields.cpp:334`) for the offset form; reuse for
     `repeatIntervalSecs` too.
3. **MS single-reminder mapping:**
   - Promote: `src/todo/mstodotaskcanonstages.cpp:236-247` —
     `alarms[0] = {"reminder": <raw reminderDateTime object>}`.
   - Demote: `src/todo/mstodotaskcanonstages.cpp:418-428` — reads
     `alarms[0].reminder` back out verbatim.
   - Test (promote-only, no demote/crossing coverage):
     `tests/todo/tst_ms_todotask_canon_edge.cpp:118-123`.
   - Loss profile: `alarms` → `LossKind::Simplified`,
     `mstodotaskcanonstages.cpp:554` (in the `{description, descriptionHtml,
     due, start, completed, alarms}` Simplified list).
4. **Google declares Dropped:**
   - `src/todo/googletaskcanonstages.cpp:270` (in the Dropped-list loop
     `:266-274`) — no promote/demote code touches `alarms` at all on this
     leg (grep-confirmed zero hits for "alarm"/"reminder" in
     `googletaskcanonstages.cpp`).
5. **Capability struct:**
   - `src/sync/calendarcapabilities.h:26-75` — `AlarmSupport {None,
     Display, Full}` (doc comment: "Full = every alarm form survives
     byte-exact"), no other alarm-shape field.
   - `src/sync/calendarcapabilities.cpp` — todo-relevant values:
     `localBlob()` → Full (`:73`); `calDav()` delegates to
     `capabilitiesFromDiscovery()` → Full (`:54`); `googleTasks()` → None
     (`:148`); `msGraphTodo()` → Display, `unknownPropertyPreservation` =
     XOnly (`:162`, `:166`).
6. **No existing VTODO-leg alarm round-trip test at all.** Grepped
   `tests/todo/tst_todo_canon_roundtrip.cpp` case-insensitively for
   `"alarm"` — **zero matches**. This is the headline W7 gap for the
   VALARM truth-table cell (see W7 §Code map below) — W5 should land its
   own promote/demote tests in the same commit, not defer them to W7.

### Open decisions for the implementing agent

*(numbered 1–3 here; continues into the consolidated list at the end of
this doc)*

1. **Does `CalendarCapabilities` need a new field for absolute-
   trigger/RELATED/REPEAT-DURATION support?** Recommend **no**. Reasoning:
   the only three backend families that touch alarms in the todo domain
   are localBlob/CalDAV (already `AlarmSupport::Full`, and both are
   byte-passthrough-preferring backends — see W7 §Code map — so the new
   JSON sub-keys only matter when a record actually crosses the canon-JSON
   demote seam, and `Full` already correctly promises "every alarm form
   survives"), and MS To-Do (`AlarmSupport::Display`, which already
   correctly signals "not full fidelity" — MS structurally supports
   exactly one absolute-time reminder and nothing else, so there is no
   finer distinction left for a new flag to carry; RELATED/REPEAT/absolute
   are all inapplicable-by-construction on that leg, not "sometimes
   supported"). Google is `None` — nothing to refine. If the implementing
   agent disagrees, the alternative is a `bool alarmAdvancedForms`-style
   flag, but no current caller/consumer of `CalendarCapabilities` was
   found that would read it (grep for `.alarms ==` / `AlarmSupport::` outside
   `calendarcapabilities.{h,cpp}` turns up only the struct's own tests) —
   recommend not speculatively adding an unconsumed field.
2. **Fold the MS-leg `{reminder: {...}}` → unified-shape fix into W5?**
   Recommend **yes**, explicitly flagged as beyond-the-literal-ask (see
   above): it fixes a real, demonstrable cross-vendor data-loss bug
   (MS-sourced alarm mangled to `type=Invalid, offset=0` when demoted to
   VTODO through canon), the fix is small, and the new `"at"` key is
   exactly the right home for MS's inherently-absolute reminder. If the
   implementing session prefers to keep W5 narrowly scoped to "add new
   optional sub-keys," recommend at minimum filing a FINDINGS entry (O-nn)
   documenting the MS↔vtodo alarm-shape mismatch so it isn't silently lost,
   mirroring how the W3 recon flagged (not fixed) the VEVENT
   `RANGE=THISANDFUTURE` twin bug.
3. **REPEAT/DURATION pairing enforcement — verify empirically, don't
   assume.** KCalendarCore's doc comments imply REPEAT and DURATION are
   used as a pair but do not explicitly say `repeatCount()`/`snoozeTime()`
   enforce that pairing at the object level (e.g., can `repeatCount()` be
   nonzero while `snoozeTime()` is a zero/invalid `Duration`, e.g. for an
   alarm built programmatically rather than parsed?). Recommend the
   implementing agent write a quick throwaway KCalendarCore probe (parse a
   VALARM with `REPEAT:3` but no `DURATION`, and vice versa) before writing
   promote code, and on promote only emit the pair when **both**
   `repeatCount() > 0` AND `snoozeTime().asSeconds() != 0`; on demote, only
   call both setters when **both** JSON keys are present (never call one
   without the other — an unpaired REPEAT or DURATION is itself malformed
   per RFC5545 and should not be synthesized).

---

## W6.2 — malformed DTSTART/DUE coercion

### What W6.2 is (per the response doc, binding — verbatim)

`docs/2026-08-25-vtodo-parity-handoff-response.md` §"W6 — Producer shims"
point 2 (lines 174–180; point 1 is N/A, point 3 is declined — **out of
this item's scope**, do not touch priority-band or PRODID-trust logic):

> 2. Malformed DTSTART/DUE coercion: **ACCEPTED** — rules land in
>    `vtodocanonfields` promote with unit tests on real-world broken
>    samples (we'll lift fixtures from the tasks.org audit §9 list:
>    DATE/DATE-TIME mismatch ⇒ coerce to DUE's type; DUE ≤ DTSTART ⇒ drop
>    DTSTART; DURATION-without-DTSTART ⇒ drop DURATION). Bonus from our own
>    recon: DATE-value DTSTART currently does not round-trip as DATE
>    (demote reconstructs midnight-UTC DATE-TIME) — fixing alongside.

The three coercion rules plus the bonus DATE-round-trip fix are **all** in
scope. The response doc's rule (a) is deliberately simpler than tasks.org's
actual (symmetric) rule — see Open decision 4.

### The tasks.org "§9 list" — FOUND, but it is prose, not fixtures (item 4 of the task brief)

Located: `~/dev/PlanStan/docs/audits/2026-08-25-vtodo-parity/1-tasksorg-inventory.md`,
section **"9. CALDAV/VTODO SPECIFICS"** (heading at line 139). The
relevant passage is the "Malformed-input fixes" bullet at **line 144**:

> **Malformed-input fixes** (lines 263-286): DTSTART DATE + DUE DATE-TIME
> ⇒ rewrite DTSTART to DATE-TIME in DUE's zone; reverse case ⇒ rewrite DUE;
> `DUE <= DTSTART` ⇒ **drop DTSTART**; `DURATION` without DTSTART ⇒ drop
> DURATION. Comment: "There seem to be many invalid tasks out there
> because of some defect clients".

This is a **description of tasks.org's Kotlin source**
(`kmp/src/jvmCommonMain/kotlin/org/tasks/caldav/Task.kt:263-286`, a file
that does not exist in this repo or PlanStan — it's tasks.org's own
upstream, referenced only by line-number citation in the audit doc), not a
set of literal broken-ICS byte fixtures. **No fixture file/list exists
anywhere in this repo or PlanStan matching "tasks.org audit §9 list" as a
data artifact.** Recommend: synthesize equivalent fixtures directly from
the three rules (small hand-written VTODO blocks with each malformed
combination — DATE start/DATE-TIME due, DATE-TIME start/DATE due, DUE
before/equal to DTSTART, a DURATION-bearing VTODO with no DTSTART),
following the existing fixture style in `tests/todo/tst_todo_canon_roundtrip.cpp`
(e.g. `kTestVTodo`, `kTestVTodoWithRecurrence`, lines 58–135). Do not block
on locating a fixture list that does not exist as a discrete artifact.

Also worth noting (not binding, FYI only, from the same audit section,
line 175–180): a companion "10. PRODUCER COMPATIBILITY SHIMS" section
lists producer-specific normalizations (Synology TZ-offset repair,
malformed TRIGGER:DURATION garbage, ical4j `COUNT=-1` with `UNTIL`
stripping) that are explicitly **out of scope** here — W6 point 1/3
already declined the general producer-shim posture; only the two coercion
rules quoted above are in W6.2.

### Design sketch (decide before coding)

1. **Verify KCalendarCore's parse-time behavior FIRST — this determines
   the entire implementation shape (Open decision 5, load-bearing).**
   `KCalendarCore::Incidence` exposes a single incidence-level
   `allDay()`/`setAllDay(bool)` flag (`incidence.h:371`), not independent
   per-property (DTSTART vs DUE) value-type flags. RFC5545 §3.6.2 requires
   DTSTART and DUE to share the same value type when both are present — a
   "malformed" producer violates this MUST. It is **not yet established**
   whether, by the time `todoFieldsToCanon` sees a parsed `Todo::Ptr`,
   KCalendarCore has already silently collapsed a genuine DTSTART-DATE /
   DUE-DATE-TIME mismatch onto one shared interpretation (making the
   mismatch **undetectable** from `todo->dtStart()`/`todo->dtDue()` alone),
   or whether it preserves each property's literal form independently
   enough for the promote code to still observe the mismatch. **Write a
   throwaway probe before designing further**: feed
   `KCalendarCore::ICalFormat::fromString()` a VTODO with
   `DTSTART;VALUE=DATE:20260601` and `DUE:20260601T170000Z`, then inspect
   `todo->dtStart()`, `todo->dtDue()`, `todo->allDay()`. Two branches:
   - **If KCalendarCore preserves the mismatch** (e.g. `dtStart()` comes
     back midnight-local/floating and `dtDue()` comes back a real
     date-time, with `allDay()` reflecting only one of them or something
     inspectable): coercion can be implemented purely against the parsed
     `Todo::Ptr`, using the same `dt.time()==QTime(0,0) &&
     dt.timeSpec()==Qt::LocalTime` heuristic `dateTimeToJson` already uses
     privately (`vtodocanonfields.cpp:47`) to detect "this one is
     date-only" per property, independently for start and due.
   - **If KCalendarCore collapses it** (most likely, given the single
     `allDay()` flag): the mismatch must be detected from **raw bytes**,
     mirroring the existing `extractICalPropertyLiteral`/raw-text-scan
     precedent already used in this exact file for CREATED/LAST-MODIFIED
     (`vtodocanonfields.cpp:172-175`, via `icaltimestamp.h`) and for
     recurrence lines (`icalcomponentscan.h`). A small new helper —
     e.g. `bool isDateOnlyICalProperty(const QByteArray& icalBytes, const
     QString& propertyName)` (grep for `PROPNAME;...VALUE=DATE...:` vs
     `PROPNAME:` forms, component-scoped the same way
     `extractComponentRecurrenceLines` already is) — would need to be
     added, most naturally to `icaltimestamp.h`/`.cpp` alongside its
     existing raw-text-literal siblings, or as a `vtodocanonfields.cpp`
     -local static (narrower, following that file's own
     `dateTimeToJson`/`jsonToDateTime` local-helper convention). Recommend
     the narrower local-static option unless a second caller emerges
     (YAGNI, same reasoning W3's recon gave for not building a shared
     `src/sync/` split helper prematurely).
   Either branch is buildable; **do not guess** — the probe is cheap and
   removes real risk of building against the wrong data model.
2. **Coercion rule (a) — DATE/DATE-TIME mismatch ⇒ coerce to DUE's type.**
   Per the binding response-doc wording (not tasks.org's actual symmetric
   rule — see Open decision 4): if DTSTART and DUE are both present and one
   is DATE-only while the other is DATE-TIME, treat the DATE-only one as if
   it were DATE-TIME at the same clock reading (midnight, in DUE's
   zone/UTC as appropriate) — i.e. **DUE's type always wins**, regardless
   of which property (DTSTART or DUE) was the DATE-only one. Insertion
   point: `todoFieldsToCanon`'s start/due block (`vtodocanonfields.cpp:228-244`),
   before building the `start`/`due` JSON objects — decide the effective
   "is this DATE-only" flag for each side first (per decision 1's
   resolution), then if they disagree, force the DTSTART side to DUE's
   flag before calling `dateTimeToJson`.
3. **Coercion rule (b) — DUE ≤ DTSTART ⇒ drop DTSTART.** Straightforward:
   compare `todo->dtStart()` and `todo->dtDue()` as ordinary `QDateTime`s
   (both valid, `due <= start`) → skip inserting `"start"` into canon
   entirely (same "just omit the key" pattern already used throughout this
   file for absent-optional fields). Insertion point: same block,
   `vtodocanonfields.cpp:228-244`, after the DATE/DATE-TIME reconciliation
   in rule (a) so the comparison uses already-reconciled values.
4. **Coercion rule (c) — DURATION-without-DTSTART ⇒ drop DURATION.**
   **Likely already a non-issue in this pipeline — verify, don't
   implement blind.** `KCalendarCore::Todo` (grepped
   `/usr/include/KF6/KCalendarCore/kcalendarcore/todo.h` for "Duration" —
   **zero matches**) exposes no direct DURATION accessor; `ICalFormat`'s
   parser almost certainly resolves a `DURATION` property (an alternative
   RFC5545 way to spell DUE, as `DTSTART + DURATION`) into an absolute
   `dtDue()` internally at parse time, before `todoFieldsToCanon` ever
   runs — meaning a DURATION-without-DTSTART VTODO would already fail to
   produce a valid `dtDue()` (since there is no DTSTART to add the
   duration to) with no code change needed on our side; the "drop" already
   happens by construction. **Verify this empirically** (same throwaway
   KCalendarCore probe session as decision 1: parse a VTODO with
   `DURATION:PT1H` and no `DTSTART`/`DUE`, check `todo->dtDue().isValid()`)
   before writing any promote-side code for rule (c) — if the probe
   confirms KCalendarCore already drops it silently and safely, this rule
   requires **zero implementation**, just a test pinning the already-correct
   behavior (`vtodoPromoteDropsDurationWithoutDtstart` or similar,
   asserting canon has no `"due"` key and no crash/garbage value).
5. **Bonus fix — DATE-value DTSTART/DUE demote round-trip.** Confirmed
   real, both promote and demote sides mapped:
   - **Promote is correct today** (verified): `dateTimeToJson`
     (`vtodocanonfields.cpp:42-61`) detects date-only via
     `dt.time()==QTime(0,0) && dt.timeSpec()==Qt::LocalTime`, called with
     no explicit `allDay` argument (`:232`, `:240` — the function's second
     param defaults `false` and is currently **never passed `true` by any
     call site**, an existing-but-unused capability). Assuming
     KCalendarCore parses `DTSTART;VALUE=DATE:...` into a floating
     midnight `QDateTime` (needs the same decision-1 probe to fully
     confirm, but is the standard/expected KCalendarCore behavior for
     VALUE=DATE), promote already correctly emits `{"date": ..., "allDay":
     true}`.
   - **Demote is the actual bug**: `jsonToDateTime`
     (`vtodocanonfields.cpp:64-84`) reconstructs a `{"date": ...}` object
     as `QDateTime(d, QTime(0,0,0), QTimeZone::utc())` — a **UTC
     midnight DATE-TIME**, not a DATE value — and the demote block
     (`vtodocanonfields.cpp:500-519`) calls `todo->setDtStart(dt)` /
     `todo->setDtDue(dt)` with that reconstructed value but **never calls
     `todo->setAllDay(true)`** (or the equivalent — `KCalendarCore::Todo`
     inherits `setAllDay(bool)` from `Incidence`, overridden at
     `todo.h:271`). Since KCalendarCore's iCal writer decides
     `VALUE=DATE` vs a full `DATE-TIME` based on the incidence's
     `allDay()` flag (not by inspecting the QDateTime's own
     time-of-day — needs the decision-1 probe to fully confirm, but this
     is the standard KCalendarCore contract), the demoted VTODO re-emits
     `DTSTART:20260601T000000Z` instead of `DTSTART;VALUE=DATE:20260601`.
     **Fix:** in the demote start/due block
     (`vtodocanonfields.cpp:500-519`), when the JSON object contains a
     `"date"` key (not `"dateTime"`), call `todo->setAllDay(true)` in
     addition to `setDtStart`/`setDtDue`. Confirm ordering — does
     `setAllDay` need to be called before or after `setDtStart`/`setDtDue`
     to take effect? (Some KCalendarCore versions re-derive `allDay` as a
     side effect of `setDtStart`; verify via the same probe.) Also confirm
     `setAllDay(true)` is safe to call once even if BOTH start and due are
     date-only (it's a single incidence-level flag, so calling it twice
     with the same value is harmless — but calling it `true` for DTSTART
     and never for DUE, or vice versa, when only ONE of the pair is
     date-only, is now covered by rule (a)'s reconciliation, which by
     construction should make both sides agree before reaching this point).
6. **Tests:** new fixtures in `tests/todo/tst_todo_canon_roundtrip.cpp`
   (pattern: `kTestVTodo` et al., lines 58-135) covering: (i) DATE
   DTSTART + DATE-TIME DUE (assert canon `start` coerced to DUE's
   DATE-TIME-ness); (ii) DATE-TIME DTSTART + DATE DUE (assert `start`
   coerced to DATE); (iii) DUE ≤ DTSTART (assert canon has no `"start"`
   key); (iv) DURATION with no DTSTART/DUE (assert graceful — no crash,
   canon has no `"due"`/`"start"`, per decision 4's probe outcome); (v) a
   plain all-day DTSTART/DUE round-trip through **both** promote AND
   demote, asserting the re-emitted bytes contain `DTSTART;VALUE=DATE:`
   (not a `T000000Z` DATE-TIME) — the bonus-fix pin, the one genuinely new
   round-trip assertion this item needs beyond the coercion rules
   themselves.

### Open decisions for the implementing agent

*(numbered 4–6 here)*

4. **Rule (a) simplification vs tasks.org's actual (symmetric) rule.**
   tasks.org's real behavior (§9, line 144) is symmetric: "DTSTART DATE +
   DUE DATE-TIME ⇒ rewrite DTSTART to DATE-TIME in DUE's zone; **reverse
   case ⇒ rewrite DUE**" — i.e. whichever property is DATE-TIME wins,
   regardless of which one that is. The **response doc's binding text**
   instead says unconditionally "coerce to DUE's type" — DUE always wins,
   even in the case where DTSTART is DATE-TIME and DUE is the DATE-only
   one (which tasks.org would resolve the *other* way, rewriting DUE
   up to DATE-TIME). **Recommend following the binding response-doc text
   exactly** (DUE's type always wins) since it was the accepted/ratified
   wording, not tasks.org's literal behavior — but flag this explicitly in
   the return receipt as a deliberate, documented divergence from the
   audit's source material, so a future reviewer comparing against
   tasks.org's behavior doesn't read it as an oversight.
5. **(Already threaded through the design sketch, restated for
   visibility.)** Whether rules (a)/(b) can be implemented against
   `todo->dtStart()`/`dtDue()` directly or require raw-bytes VALUE=DATE
   detection is **not yet known** — resolve via the KCalendarCore probe
   described in Design sketch step 1 before writing promote code. This is
   the single most load-bearing unknown in this sub-item, structurally
   analogous to W3's Open decision 6 (text-level vs KCalendarCore-object
   RRULE editing) — get it right before investing in either code path.
6. **Rule (c)'s implementation surface may be empty.** Per Design sketch
   step 4, KCalendarCore most likely already drops
   DURATION-without-DTSTART for free (no DTSTART to add the duration to →
   no valid computed DUE). Recommend confirming via probe and, if
   confirmed, landing **only a pinning test**, not speculative
   "defensive" code for a case that structurally cannot occur once
   KCalendarCore's own parser has run.

---

## W7 — passthrough verification table + round-trip tests, and O74 fix

### What W7 is (per the response doc, binding — verbatim)

`docs/2026-08-25-vtodo-parity-handoff-response.md` §"W7 — Passthrough
verification table — **ACCEPTED (recon done, tests pending)**"
(lines 185–199):

> Current truth table (from code read, 2026-08-25):
>
> | Backend | X-props | VALARM | VTIMEZONE | Ordering/extras |
> |---|---|---|---|---|
> | LocalBlob | preserved verbatim | preserved | preserved | bytes verbatim |
> | CalDAV (RemoteCalendarBackend) | preserved (server raw bytes preferred over re-serialization) | preserved | preserved | bytes verbatim |
> | Org | **DROPPED** (fixed headline mapping; only OrgRoundtripData{keyword, descHash, repeater} survives) | dropped | n/a | prose kept only while description MD5 unchanged |
> | Google Tasks | native keys stashed verbatim C→G; foreign extras (x-vtodo) do NOT ride; recurrence/alarms/priority etc. declared Dropped | dropped (declared) | n/a | — |
> | MS To-Do | unmapped wire keys stashed + x-canon-* carrier (live-Reversible) | single reminder ⇄ alarms[0] | n/a | carrier via nav POST (O73 upsert) |
>
> Plus the O74 differ-blind-spot fix. Round-trip tests per "preserved"
> cell land with W7; the org row gets an editor-warning-worthy contract
> sentence ("org saves lose unknown iCal properties").

And FINDINGS O74 (`docs/campaign/FINDINGS.md:3262-3279`, quote the fix
guidance verbatim):

> The todo domain's canonical differ is `CanonJsonDiffer(todoCanonPropertyIds())`
> (`src/todo/tododomaindefinition.cpp:27`) — it diffs catalogued canon
> property ids only. `providerExtras`… is NOT catalogued, so a change
> confined to X-/extra properties never produces a diff… Fix folds into
> parity VP.f/W7: either catalogue a derived extras digest or add an
> explicit extras key to the compared set.

### Design sketch — round-trip tests for each "preserved" cell

Only the two byte-preserving legs (LocalBlob, CalDAV) have "preserved"
cells to prove; Org/Google/MS cells are declared drops/simplifications,
already covered by their existing Dropped/Simplified/Reversible
LossProfile entries and edge tests — no new work needed there beyond the
org contract sentence (see below).

1. **VALARM — LocalBlob + CalDAV: NO test exists today.** Grepped
   `tests/todo/tst_todo_canon_roundtrip.cpp` case-insensitively for
   `"alarm"` — **zero hits**. This is the single biggest W7 gap. Land the
   VALARM round-trip tests here (or, per W5's own recommendation, land
   them alongside W5's promote/demote code in the same commit — either
   ordering is fine, but they must exist by the time VP.f is declared
   done). Cover: offset-form (existing shape, regression-pin), absolute
   `"at"`-form (new), `related:"end"`-form (new), REPEAT/DURATION pair
   (new).
2. **X-props — LocalBlob + CalDAV: indirectly covered, not directly.**
   The only existing passthrough proof is via the `X-ORG-REPEATER`
   round-trip tests (`tst_todo_canon_roundtrip.cpp:540-800`,
   `providerExtras["x-vtodo"]` assertion at `:567-571`) — but
   `X-ORG-REPEATER` is a **recognized** property (consumed to derive
   `completionAnchor`), not a genuinely arbitrary/unknown one. No test
   feeds a plain, semantically-meaningless custom property (e.g.
   `X-SOME-RANDOM-CLIENT-FIELD:foo`) through promote→demote and asserts it
   survives untouched via `providerExtras["x-vtodo"]` alone. Recommend
   adding one such test — small, proves the *generic* mechanism rather
   than only its two special-cased consumers (X-ALT-DESC,
   X-CANON-SERIES-SPLIT-OF, X-ORG-REPEATER all happen to also be
   recognized/consumed, which is a weaker proof of the *general*
   passthrough claim than a genuinely unknown property would be).
3. **VTIMEZONE — LocalBlob + CalDAV: already covered.** Confirmed:
   `tst_todo_canon_roundtrip.cpp` has dedicated VTIMEZONE-preservation
   fixtures/tests around lines 93-130 (fixture setup,
   `kTestVTodoWithVtimezone*`) and 352-371 (assertion that KCalendarCore's
   own re-serialization is not trusted to preserve DST rules — the
   recurrence-lines-verbatim invariant covers this). No new work needed;
   cite these lines in the return receipt as the existing proof.
4. **Ordering/extras "bytes verbatim" — LocalBlob: already covered.**
   `tests/calendar/tst_localbackend_blob_view.cpp` has
   `coLocatedMasterAndException_preservesFullFileBytes` (referenced in the
   W1 contract doc §7 and W1 return receipt) proving `recordFromBytes()`
   keeps the full original file bytes, not just KCalendarCore's
   re-serialization. **CalDAV: needs a targeted check** — the "server raw
   bytes preferred over re-serialization" claim is a `RemoteCalendarBackend`-level
   behavior; recon did not locate a test with an assertion phrased around
   byte-verbatim VTODO preservation specifically (as opposed to the
   composite-identity tests, which check href/record-count behavior, not
   byte fidelity). Recommend a quick grep of
   `tests/calendar/tst_remotecalendarbackend_blob_view.cpp` for an
   existing byte-equality assertion on VTODO fetch before deciding whether
   a new test is needed — this recon ran out of budget to fully confirm
   this one cell; flagged rather than guessed (see Open decision 7).
5. **Org contract sentence.** The response doc asks for "an
   editor-warning-worthy contract sentence" placed somewhere
   consumer-visible. Precedent for where this campaign puts binding
   consumer-facing warnings: the W1 contract doc
   (`2026-08-26-w1-detached-exceptions-contract.md`) is a dedicated
   binding-contract file consumed by return receipts and referenced from
   STATUS.md; W3's contract doc follows the same pattern. Recommend the
   analogous location: either (a) a short new dedicated doc
   `docs/campaign/vtodo-parity/2026-08-28-vpf-w7-passthrough-contract.md`
   containing just the finalized truth table + the org warning sentence +
   the O74 fix note (mirrors W1/W3's "binding contract doc" precedent,
   appropriately small), or (b) fold the sentence directly into this
   item's return receipt if a fresh doc feels like too much ceremony for
   one sentence. Recommend (a) — the truth table itself is exactly the
   kind of durable, quotable artifact PlanStan will want to link from
   their editor's UI/docs, and a dedicated small doc is cheap and
   consistent with how W1/W3 handled their own binding contracts.
6. **Existing coverage recap (from Code map, EEE loss-profile tests
   already in place, no new work):** Org's X-prop-drop is already the
   `unknownPropertyPreservation = None` capability value
   (`calendarcapabilities.cpp:99`) plus the documented behavior in W1's
   response doc Q3 answer (already quoted, already binding, already
   landed) — the "editor-warning-worthy sentence" is the only remaining
   deliverable for that row, not new code.

### O74 fix design

**Root cause (confirmed, file:line):**
`src/todo/tododomaindefinition.cpp:27-30` —
`CanonJsonDiffer(todoCanonPropertyIds())` diffs only catalogued property
ids (`src/shape/canonjsondiffer.cpp:14-26`, iterates `m_properties` and
compares `CanonEnvelope::valuesEqual` per key). `providerExtras` is a
reserved envelope-adjacent key (like `_canon`), never added to any
domain's catalogue by design — confirmed by the existing generic unit test
`differIgnoresProviderExtrasAndCanon`
(`tests/shape/tst_canonjson_diff_merge.cpp:53-60`), which pins the
differ's "only catalogued keys" contract in the abstract (this test is
**correct and does not need to change** — see below).

**Design:**

1. **New catalogued key `providerExtrasDigest`** (`PropertyKind::String`)
   — a fingerprint of a record's `providerExtras` content, computed at
   PROMOTE time. Insertion point: `todocanonproperties.cpp`, after the
   `completionAnchor`/`seriesSplitOf` block (~line 54), before "Alarms and
   extra data":
   ```cpp
   cat.addProperty({ PropertyId{"providerExtrasDigest"}, PropertyKind::String, QStringLiteral("Provider Extras Digest") });
   ```
   This alone (once catalogued, `todoCanonPropertyIds()` auto-includes it,
   `tododomaindefinition.cpp:60-67`-equivalent for todo is the loop at
   `todocanonproperties.cpp:73-79`) makes it differ-visible for free — the
   exact O63/W4 pattern already proven three times this campaign.
2. **Shared canonicalizing-hash primitive**, domain-neutral (O74's own
   text: "same shape presumably holds for any domain whose differ is
   catalogue-scoped (contacts/events use the same CanonJsonDiffer
   pattern)" — build this once, reusably, even though only the todo domain
   consumes it in this item). Recommend adding to `canonenvelope.h`/`.cpp`
   (the natural home — it already owns `providerExtrasKey()`,
   `valuesEqual()`, `serialize()`):
   ```cpp
   // canonenvelope.h
   QString canonicalDigest(const QJsonValue& value);
   ```
   Implementation: recursively rebuild the value with **object keys
   sorted** (QJsonObject already preserves insertion order for
   serialization; sorting defensively before hashing removes any
   dependency on a given producer's iteration-order stability — cheap
   insurance, not proven strictly necessary for the vtodo leg alone; see
   Open decision 8 for why it matters more on the MS/Google legs), then
   `QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()`
   — reuse the existing project convention (this exact
   canonicalize-then-SHA256-hex pattern is already used, non-recursively,
   in `src/blob/localblobbackend.cpp:32-40`,
   `src/calendar/subscriptionbackend.cpp:273`,
   `src/calendar/mockbackend.cpp:628-641`, etc. — SHA256+hex is the
   house convention, just apply it recursively-key-sorted here since the
   input is a JSON tree, not flat bytes).
3. **Per-producer call sites — three insertion points, one per todo
   promote path**, each computing the digest from **exactly the extras
   object it wants compared** (see Open decision 8 for why this must NOT
   simply be `obj.value(providerExtrasKey())` unfiltered on the MS/Google
   legs):
   - `vtodocanonfields.cpp` — tail of the `providerExtras["x-vtodo"]`
     block (`:372-385`), before `return obj;`: hash the whole `xvtodo`
     object as built (no filtering needed — see Open decision 8, the
     vtodo leg's extras are genuine X- custom properties only, no vendor
     bookkeeping stash).
   - `mstodotaskcanonstages.cpp` — tail of the "everything unmapped"
     block (`:296-315`), before `stampEnvelope`: hash a **filtered** copy
     of `extras` excluding known-volatile bookkeeping keys (see Open
     decision 8 for exactly which).
   - `googletaskcanonstages.cpp` — tail of the "everything unmapped"
     block (`:127-141`), before `stampEnvelope`: hash a **filtered** copy
     excluding `etag` at minimum (see Open decision 8; the comment at
     `:128-129` already names the full unfiltered stash list).
   Only insert the `providerExtrasDigest` key when the (filtered) extras
   object is non-empty — an absent key already correctly signals "no
   extras" for diff purposes (an absent key vs. a present digest string
   already differ under `CanonEnvelope::valuesEqual`, no special-casing
   needed — matches the sparse-key convention every other optional canon
   field in this file already follows).
4. **Loss profile — add `providerExtrasDigest` → `LossKind::Dropped`** to
   all three `canonTo*Loss()` functions (`vtodocanonstages.cpp:65-102`,
   `googletaskcanonstages.cpp` loss function, `mstodotaskcanonstages.cpp:541-570`),
   with a comment explaining this is **not** a traditional information
   loss — the digest has no wire representation by design (it is
   recomputed fresh from the real extras content on the next promote of
   whatever gets written), so demote correctly never re-emits it. This
   mirrors how every other purely-derived/meta canon value in this
   codebase is documented, not left silently unhandled.
5. **Matrix regen + byte-pin, same commit** (O63 discipline):
   `./build/tools/matrixgen/matrixgen > docs/campaign/eee/CONVERGENCE-MATRIX.md`,
   verify `tests/convergence/tst_gm_pipeline_convergence.cpp` green. No
   `edges()` growth (`todostockshapes.cpp:42-79` unaffected — this is a
   new catalogued key + three loss-profile rows only, matching the W3/W4
   precedent exactly).
6. **Differ pin test.** Extend `tests/shape/tst_canonjson_diff_merge.cpp`
   with a `providerExtrasDigest` variant of
   `differMarksChangedPropertyOnly`/`differMarksCompletionAnchorAdvanceAsOrdinaryChange`
   (`:43-51`, `:77-90` — copy the pattern): construct a
   `CanonJsonDiffer({PropertyId{"providerExtrasDigest"}})`, two
   `CanonicalRecord`s differing only in `providerExtrasDigest`'s string
   value, assert `diff()` contains it. **Do not modify**
   `differIgnoresProviderExtrasAndCanon`
   (`tst_canonjson_diff_merge.cpp:53-60`) — it tests the differ's generic
   "only catalogued keys" contract using an explicitly narrow
   `{PropertyId{"summary"}}` catalogue that never included
   `providerExtrasDigest`; it remains correct and unaffected by this fix
   (unlike W3's `vtodoRoundTripPreservesThisAndFutureRange`, which
   directly contradicted the new behavior — there is no such contradiction
   here).

### Open decisions for the implementing agent

*(numbered 7–9 here; continues the doc's running numbering)*

7. **CalDAV byte-verbatim VTODO test — confirm or write.** Recon located
   the LocalBlob-side proof
   (`coLocatedMasterAndException_preservesFullFileBytes`) but did not
   fully confirm an equivalent byte-verbatim assertion exists for
   `RemoteCalendarBackend`/VTODO specifically (as opposed to the many
   composite-identity-focused tests in
   `tst_remotecalendarbackend_blob_view.cpp`, which check href/record-count
   shape, not byte content). Recommend the implementing agent grep that
   file for an existing raw-bytes-equality assertion on a fetched VTODO
   before writing a new one — five minutes of grep avoids either a
   redundant test or a false "already covered" claim in the return
   receipt.
8. **O74 digest volatility risk on the MS/Google legs — audit and filter
   before hashing, this is real and evidenced, not speculative.** Both
   vendor legs' "everything unmapped → providerExtras verbatim" blocks
   stash fields that **churn on every server-side write regardless of
   semantic content**:
   - Google: the promote code's own comment
     (`googletaskcanonstages.cpp:128-129`) explicitly lists
     `kind/etag/deleted/hidden/links/webViewLink/selfLink/assignmentInfo`
     as riding the verbatim stash — **`etag` bumps on every edit of any
     kind**, unrelated to whether the edit touched anything otherwise
     uncatalogued.
   - MS: the "everything unmapped" block's `consumed` set
     (`mstodotaskcanonstages.cpp:298-305`) does **not** include
     `lastModifiedDateTime`/`createdDateTime`/any etag-equivalent — these
     Graph todoTask wire fields (if present in the JSON, needs a quick
     check against a captured sample under `msgraph/captured/` or the
     mock fixtures) would fall straight into the verbatim stash the same
     way, and `lastModifiedDateTime` bumps on every write.
   **If the digest hashes these unfiltered**, `providerExtrasDigest` would
   change on essentially every MS/Google-side edit of *any* field, not
   just genuine X-prop/extras edits — turning a property-level differ
   signal into a near-useless "always dirty" flag for that key, and
   risking **false conflicts**: two sides each making an unrelated,
   non-overlapping real edit would BOTH show `providerExtrasDigest` as
   changed (since both sides' bookkeeping timestamps also moved), which a
   conflict-resolution layer comparing "did both sides touch the same
   property" could misread as a genuine collision on extras data neither
   side actually edited. **Recommend**: before computing the digest on
   each vendor leg, build a filtered copy of that leg's `extras` object
   excluding the specific known-volatile bookkeeping keys (Google: `etag`
   at minimum — the other five listed fields are real content, not
   bookkeeping, and should stay hashed; MS: whatever the empirical check
   above finds, most likely `lastModifiedDateTime`/`createdDateTime` and
   any etag-shaped field). Document the excluded-key list in a code
   comment at each call site (Part IV ethics: loud about limits) and in
   the return receipt, so a future reviewer knows exactly what
   `providerExtrasDigest` does and does not cover on each leg.
9. **Return receipts owed.** Per the response doc §2/§"Pin/release
   expectation," every delivered item gets a return receipt (exact JSON
   keys, public headers/signatures, contracts, test names, deprecations).
   VP.f closing the whole vtodo-parity campaign's W-item list means this
   is also a natural point to write a **campaign-closing** note in
   STATUS.md (mirroring how each prior VP.* row got a "DONE" state and
   summary) — recommend one consolidated return receipt covering W5 +
   W6.2 + W7/O74 together (or three small ones, implementer's choice —
   this recon doc's own three-subsection structure supports either), but
   either way STATUS.md's VP.f row and session log must be updated in the
   same commit as the code lands (house rule, CLAUDE.md "Phase-status docs
   are living documents").

---

## Session note

Exploration ran as a subagent (2026-08-28), reading CLAUDE.md (repo root),
STATUS.md, `FINDINGS.md`'s O74 entry, both prior recon docs (W3, W4) as
structural templates, the W1 detached-exceptions contract doc, the full
binding response doc (§W5/§W6/§W7), and PlanStan's
`1-tasksorg-inventory.md`/`4-parity-diff-and-plan.md` (the "tasks.org
audit" the response doc cites). Traced concrete code seams:
`vtodocanonfields.cpp` (full file read), `todocanonproperties.cpp` (full
file read), `tododomaindefinition.cpp`, `canonjsondiffer.cpp`,
`canonenvelope.cpp`, `calendarcapabilities.{h,cpp}` (full files),
`vtodocanonstages.cpp`, `mstodotaskcanonstages.cpp` (most of the file),
`googletaskcanonstages.cpp` (relevant sections), `todostockshapes.cpp`,
plus the KCalendarCore `Alarm`/`Incidence`/`Todo` headers under
`/usr/include/KF6/KCalendarCore/` and the closest existing tests
(`tst_todo_canon_roundtrip.cpp`, `tst_ms_todotask_canon_edge.cpp`,
`tst_google_task_canon_edge.cpp`, `tst_canonjson_diff_merge.cpp`).

W1 (VP.c), W2 (VP.b), W8 (VP.a), W4 (VP.d), W3 (VP.e) are all COMPLETE and
committed. No VP.f code has been written. The three most load-bearing
findings, in order of how much they should shape implementation order:

1. **W6.2's rules (a)/(b) may or may not be implementable against
   KCalendarCore's parsed `Todo::Ptr` at all** — depends on whether
   KCalendarCore preserves independent DATE/DATE-TIME-ness per property or
   collapses it via the single incidence-level `allDay()` flag. This is
   genuinely unknown without a probe and determines whether the fix is a
   small `Todo::Ptr`-level change or needs a new raw-bytes text-scan
   helper. **Resolve this first**, before either W6.2 or the DATE-round-trip
   bonus fix.
2. **O74's digest must be filtered on the MS/Google legs** (Open decision
   8) — this is not a hypothetical edge case, it is directly evidenced by
   an existing code comment (`googletaskcanonstages.cpp:128-129`) naming
   `etag` as part of the unfiltered verbatim stash. Hashing it unfiltered
   would make the whole fix counterproductive (spurious always-dirty
   signal) on exactly the two vendor legs where the fix matters most (the
   vtodo/CalDAV leg already propagates X-prop changes fine via raw-bytes
   preference in practice — MS/Google are the legs that actually needed
   O74).
3. **The MS-leg alarm shape (`{reminder: {...}}`) does not match the
   vtodo-leg shape (`{type, offset, text}`) at all**, and demoting an
   MS-sourced alarm to VTODO today silently produces a zero-offset
   Invalid-type VALARM — a real, demonstrable bug independent of anything
   W5 was asked to add. Recommended (not mandatory) to fold into W5's
   delivery since the new `"at"` key is the natural fix.

Recommend tackling in the order W6.2 → W5 → W7/O74 (W6.2 first isolates
the one genuinely unknown unknown; W5 second is mostly additive/local; W7
last both consumes W5's new shape for its round-trip tests and closes out
O74, naturally the capstone of the campaign).
