# Incidence-parity campaign STATUS

Binding plan: `PLAN.md` in this directory (read it before touching any
item — it carries the execution rules, the per-item acceptance criteria,
and the scope boundary). This file is the **live execution tracker**.

**Opened:** 2026-08-29. **Baseline:** `main` @ `fc1ae61`, 214 slots
(210 green + the 4 known environmental Radicale/KDAV slots).

**Last updated:** 2026-08-29 (campaign OPENED; recon landed, no code yet —
IP.1 is next and lands a deliberately RED gate).

> Living document. Update the item row **and** the session log in the same
> commit that changes the item's state (invariant 7). Never leave a row
> saying IN PROGRESS after work has landed.

## Where we stand

| Item | Work | Closes | State |
|---|---|---|---|
| IP.1 | Catalogue/emitter coverage gate — replaces the hand-listed `catalogueIncludesTodoAndJournalFields()` slot with a computed subset gate over every `(domain, kind)` pair | *proves* O78 | **NEXT** — lands RED with `QEXPECT_FAIL("IP.2")`; tests only, no `src/` change |
| IP.2 | Catalogue the three drifted keys in `calendarcanonproperties.cpp` | **O78** | NOT STARTED — gated on IP.1 |
| IP.3 | Contributed catalogues — each canon-fields module exports the ids it emits; catalogues become unions | O78 *class* | NOT STARTED — gates IP.4–IP.7 |
| IP.4 | Shared VALARM shape module (`alarmToJson`/`alarmFromJson`/`describeAlarmRow`) + VEVENT promote **and** demote **and** both vendor event legs, one commit | **O79** | NOT STARTED |
| IP.5 | `CanonEnvelope::stampProviderExtrasDigest()` + catalogue + declare, across calendar/journal/contacts; retrofit the 3 todo sites onto it | **O80** | NOT STARTED |
| IP.6 | `incidencecommonfields` extraction (3 kinds), then the missing VTODO fields as a separate commit | **O83** | NOT STARTED |
| IP.7 | VEVENT RANGE=THISANDFUTURE refusal (a) + DTSTART/DTEND coercion contract (b) | **O81, O82** | NOT STARTED |

## Why this campaign exists

The vtodo-parity campaign (CLOSED 2026-08-28) hardened the VTODO canon
pipeline correctly and within its charter. Three structural facts, all
verified against `fc1ae61`, mean its work did not settle cleanly:

1. **The calendar domain shares the todo emitter.**
   `src/calendar/icalcanonstages.cpp:56,:83` call
   `Kalburator::Todo::todoFieldsToCanon()` /
   `canonObjectToVtodoBytes()`. One VTODO emitter, two domains. Any
   framing that treats calendar and todo as independent hand-written
   files is wrong for VTODO and hides O78.

2. **Catalogue and emitters are independent sources of truth.** W3/W4/O74
   added three keys to the shared emitter and catalogued them only in
   `todocanonproperties.cpp`. `{calendar,canon}` emits keys its own
   catalogue lacks ⇒ differ blind **and** merger drops them (O78).

3. **VTODO is the poorest-covered incidence kind**, poorer than VJOURNAL.
   `vtodocanonfields.cpp` has zero references to `revision()`,
   `secrecy()`, `url()`, `organizer()`, `attendees()`, `attachments()`,
   `color()` (O83) — and none of those drops is declared in a loss
   profile.

The campaign's remedy is structural, not a copy pass. Copying is what
produced this state; three copies drift faster than two.

## Recon findings pinned 2026-08-29 (evidence for the plan)

- **`CanonJsonMerger` drops uncatalogued keys silently.**
  `src/shape/canonjsonmerger.cpp:29` starts `QJsonObject out = t` and
  overrides only catalogued ids — so an uncatalogued key always takes the
  **target's** value. This is what makes O78 a data-loss mechanism and not
  just differ blindness.
- **O78's live blast radius is narrow — say so.** Of the three drifted
  keys: `completionAnchor` is live but only for VTODOs carrying
  `X-ORG-REPEATER` through a CalDAV calendar; `seriesSplitOf` is **latent**
  (only `splitSeriesAtInstant()` writes it, and W3 left it host-invoked —
  its sole callers are in `tests/todo/tst_todo_series_split.cpp`);
  `providerExtrasDigest` is **benign now** (calendar ignoring it = the
  pre-O74 status quo, which is O80; a stale digest is recomputed at the
  next promote). IP.2 is therefore a small fix, not an emergency. The
  weight of this campaign sits on IP.1/IP.3 closing the *class*.
- **`tests/calendar/tst_calendar_kind_dispatch.cpp:176-186` is the drift's
  own tombstone** — it hand-lists exactly four union keys, was never
  updated when three more appeared, and stayed green throughout. IP.1
  replaces it; it is the concrete case for the "no hand-maintained key
  lists" prohibition.
- **Fixing VEVENT alarm promote alone would make things worse.** Four call
  sites read the row shape: `eventcanonfields.cpp:366-379` (promote),
  `:662-675` (demote), `mseventcanonstages.cpp:1211-1232`,
  `googlecanonstages.cpp:345-367`. The MS reader does
  `a.value("offset").toInt()` ⇒ **0** on an absolute-trigger row, which
  passes `offsetSecs <= 0 && offsetSecs % 60 == 0` and silently becomes
  `reminderMinutesBeforeStart: 0`. Latent today; live the moment promote
  is fixed. Hence IP.4 is one item, not four.
- **The digest gap is exactly three domains wide.** `note` (`TextDiffer`),
  `outline` (`OutlineDiffer`) and `blob` (`RecordDifferBlob`) do not use
  `CanonJsonDiffer` and are structurally immune. Scope is calendar, todo,
  contacts — no more.
- **O41's literal-presence CREATED/LAST-MODIFIED guard exists in three
  copies** and was fixed late in one (`journalcanonfields.cpp:53` records
  that journal "never got the same guard"). IP.6's extraction retires that
  recurrence.
- **`allDay` may be a catalogue orphan.** The emitters write `allDay`
  *inside* the `start`/`due` time objects (`vtodocanonfields.cpp:43-50`),
  yet `calendarcanonproperties.cpp` carries a top-level `allDay` Boolean.
  Verify under IP.3 before removing.
- **W6.2's rule (a) must not be mirrored blindly onto VEVENT.** It lets
  DUE's type win — a deliberate divergence from tasks.org adopted because
  the vtodo-parity response doc said so. DTEND is a bound derived from
  DTSTART, so the symmetric argument is weaker and DTSTART-wins is likely
  correct. IP.7b probes and writes a contract first.

## Session log

- **2026-08-29 — campaign OPENED.** Recon only; no code changed. Triggered
  by a gap assessment of the closed vtodo-parity campaign; that assessment
  was superseded in full by `PLAN.md` and deleted rather than corrected,
  so this campaign has exactly one authoritative document. Findings
  **O78–O83** filed. Successor pointer added to
  `docs/campaign/vtodo-parity/STATUS.md`; `CLAUDE.md` campaign section
  added.
- **NEXT:** IP.1 — land the coverage gate RED. Do not fix anything in the
  same commit; the point is a red slot that pins O78 before IP.2 removes
  it.
