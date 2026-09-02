# Incidence-parity campaign STATUS

Binding plan: `PLAN.md` in this directory (read it before touching any
item — it carries the execution rules, the per-item acceptance criteria,
and the scope boundary). This file is the **live execution tracker**.

**Opened:** 2026-08-29. **Baseline:** `main` @ `fc1ae61`, 214 slots
(210 green + the 4 known environmental Radicale/KDAV slots). Re-confirmed
at `40854f3` on 2026-09-02.

**Last updated:** 2026-09-02 — **pre-flight audit landed; PLAN.md
Amendment 1 adopted.** Six new findings (**O85–O90**), five new items
(**IP.8–IP.12**), and a **revised execution order**: IP.8 runs next, not
IP.3.

> **New agent, start here.** Read in this order: (1) this file's *Where we
> stand* table for your item, (2) `PLAN.md` **§Amendment 1 first**, then the
> body item it points to, (3) `2026-09-02-preflight-audit.md` for the
> evidence, (4) `probes/run.sh` if you need to see it yourself. Then your
> predecessor's receipt. Do not start an item that is not the next
> un-done row.

> Living document. Update the item row **and** the session log in the same
> commit that changes the item's state (invariant 7). Never leave a row
> saying IN PROGRESS after work has landed.

## Where we stand

| Order | Item | Work | Closes | State |
|---|---|---|---|---|
| — | IP.1 | Catalogue/emitter coverage gate — computed subset gate over every `(domain, kind)` pair | *proves* O78 | **DONE 2026-08-29** — landed RED on `(calendar, vtodo)` with `QEXPECT_FAIL`; tests only. Receipt: `2026-08-29-ip1-return-receipt.md`. |
| — | IP.2 | Catalogue the three drifted keys in `calendarcanonproperties.cpp` | **O78** | **DONE 2026-09-01** — gate green, 0 XFAIL; 4 new merger slots; matrix byte-identical. Receipt: `2026-09-01-ip2-return-receipt.md`. |
| — | — | **Pre-flight audit** — deliberate code-first sweep of the whole incidence surface | *files* O85–O90 | **DONE 2026-09-02** — evidence: `2026-09-02-preflight-audit.md`; re-runnable probes: `probes/run.sh`. PLAN.md **Amendment 1** adopted. No `src/` change. |
| **1** | **IP.8** | **RFC-5545 round-trip fidelity gate** — maximal conformant fixture → promote → demote → diff property sets, per kind; + VALARM sub-gate | *proves* **O85, O86, O87**; re-pins O79, O83 | **NEXT** — tests only, lands RED. The measurement nothing in the suite makes today. |
| 2 | IP.3 | Contributed catalogues — each canon-fields module exports the ids it emits | O78 *class*, **O84** | NOT STARTED — inherits the **O84 fix** (with the whose-kind-wins decision written down) and the `allDay` orphan check. |
| 3 | **IP.9** | **Kind-scoped loss profiles** — one edge currently carries an event-only profile for all three kinds; `canonToVjournalLoss()` is dead code | **O88** | NOT STARTED — **gates IP.4/IP.6/IP.10**: until it lands there is nowhere truthful to declare a VTODO or VJOURNAL loss. |
| 4 | IP.4 | Shared VALARM module + VEVENT promote/demote + both vendor event legs, one commit | **O79**, **+O85** | NOT STARTED — see Amendment §A.3.1 (the `enabled` flag). |
| 5 | IP.5 | `CanonEnvelope::stampProviderExtrasDigest()` across calendar/journal/contacts; retrofit the 3 todo sites | **O80** | NOT STARTED |
| 6 | IP.6 | `incidencecommonfields` extraction (3 kinds), then the missing VTODO fields as a separate commit | **O83**, **+O86** | NOT STARTED — see Amendment §A.3.2 (the GEO decision, and VEVENT's `RELATED-TO` drop). |
| 7 | **IP.10** | **VJOURNAL parity** — `RECURRENCE-ID` identity first, then `RRULE`/`EXDATE`, then the common fields from IP.6 | **O87** | NOT STARTED |
| 8 | IP.7 | VEVENT RANGE=THISANDFUTURE refusal (a) + DTSTART/DTEND coercion contract (b) | O81, O82 | NOT STARTED — **IP.7b blocked** on PlanStan Q2; IP.7a may land alone. |
| 9 | **IP.11** | **VTODO representation unification** — converge or route | **O89** | NOT STARTED — **blocked on PlanStan Q1.** |
| 10 | **IP.12** | Demote purity — strip the heap-derived attendee `X-UID` | **O90** | NOT STARTED |

**Consumer dependency:** `docs/2026-09-02-incidence-parity-planstan-report.md`
carries two blocking questions (Q1 → IP.11, Q2 → IP.7b). Everything else
proceeds without PlanStan. Check for an answer before reaching item 8.

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
- **2026-08-29 — IP.1 DONE.** `tests/calendar/tst_calendar_kind_dispatch.cpp`'s
  hand-listed `catalogueIncludesTodoAndJournalFields()` (four hardcoded
  keys, the drift's own tombstone) replaced with seven computed-subset
  slots, one per `(domain, kind)` pair (`(calendar,vevent)`,
  `(calendar,vtodo)`, `(calendar,vjournal)`, `(todo,vtodo)`, and the three
  contacts legs `vcard4`/`google-person`/`ms-contact`), each promoting a
  newly-built maximal fixture and checking its emitted top-level key set
  (minus the envelope keys, read from `CanonEnvelope`) against the real
  catalogue at runtime via a new shared helper,
  `tests/shape/canonkeycoverage.h`. `(calendar, vtodo)` comes up RED
  exactly as PLAN.md predicted, naming `providerExtrasDigest`,
  `seriesSplitOf`, `completionAnchor` by name in the failure message —
  verified by temporarily removing the `QEXPECT_FAIL` and re-running before
  landing it. All six other pairs are GREEN, including both contacts legs
  that round-trip catalogued keys generically through an
  `x-canon-*`/`clientData` string carrier (the closest thing in the
  codebase to a second O78-shaped risk) — no second live drift found. Hit
  and worked around the already-documented O59 moc/raw-string-literal trap
  while building the two JSON vendor fixtures (switched to concatenated
  quoted literals). No `src/` change. Full suite: 214 tests, 210 passed, 4
  failed (the same 4 known environmental Radicale/KDAV slots as baseline;
  `tst_calendar_kind_dispatch` itself is among the 210 passed — its one red
  assertion is a QTest-level XFAIL, not a ctest-level failure). Receipt:
  `2026-08-29-ip1-return-receipt.md`.
- **2026-09-01 — IP.2 DONE. O78 RESOLVED.**
  `src/calendar/calendarcanonproperties.cpp` now declares `seriesSplitOf`
  (String), `completionAnchor` (Json) and `providerExtrasDigest` (String)
  in its union block, matching `todocanonproperties.cpp:47-60` character
  for character. IP.1's `QEXPECT_FAIL` is gone and
  `calendarCatalogueDeclaresVtodoKeys()` is green — the gate now reports
  14/14 PASS, 0 XFAIL. Four new slots in
  `tests/shape/tst_canonjson_diff_merge.cpp`, all built from the **real**
  `calendarCanonPropertyIds()` rather than the file's existing
  hand-listed-`PropertyId` convention — written the conventional way they
  would have passed *before* the fix too, i.e. reproduced the tombstone
  IP.1 deleted; non-vacuity verified by reverting the `src/` change,
  rebuilding, and confirming all three regression slots go red. Matrix
  regenerated: **byte-identical** (no loss profile or edge changed), pin
  green. Full suite: 214 tests, 210 passed, 4 failed — the same 4
  environmental Radicale/KDAV slots.
- **2026-09-01 — O84 FILED, not fixed.** Building the merger slot revealed
  that `CanonJsonMerger::merge()` re-stamps via the 3-arg
  `CanonEnvelope::stampEnvelope` (`canonjsonmerger.cpp:60`), which builds a
  fresh `_canon` and therefore **erases** `_canon.kind` — so a merged
  `{calendar,canon}` VTODO or VJOURNAL demotes as a **VEVENT**
  (`icalcanonstages.cpp:85` treats absent kind as vevent). Verified
  end-to-end, not inferred: the merged record demoted to `BEGIN:VEVENT`.
  Strictly worse than O78 (component type, not three field values), and
  confined to the calendar domain — every other domain is single-kind and
  does not dispatch on kind. Per PLAN.md §1's "no fix while passing
  through" it is **pinned, not fixed**: `mergerPreservesIncidenceKind()`
  carries two `QEXPECT_FAIL` assertions (consequence, then symptom) so
  ctest stays green and the fix will XPASS. The open question the fix must
  answer — whose kind wins when source and target disagree — is why this
  did not belong to IP.2.
- **2026-09-02 — PRE-FLIGHT AUDIT. No `src/` change. O85–O90 filed;
  PLAN.md Amendment 1 adopted; execution order revised.** Commissioned
  because the campaign kept finding defects sideways (O84 surfaced while
  building IP.2's test, not by looking for it). A deliberate code-first
  sweep of the whole incidence surface, measured with two probe programs
  now kept in `probes/` and re-runnable via `probes/run.sh`.

  **The finding that reframes the campaign:** `_canon.kind` is written in
  exactly one place (`icalcanonstages.cpp:65`) and read in exactly one
  place (`:81`). Grep-confirmed — nothing else in the library knows it
  exists, yet it alone decides which component a canon record demotes as.
  O78, O83, O84, O87 and O88 are all symptoms of that single fact.

  **Measured round-trip loss** (maximal RFC 5545 component → canon → iCal),
  none of it declared in any loss profile:
  VEVENT loses `GEO`, `RELATED-TO`; VTODO loses `ATTACH`, `ATTENDEE`,
  `CLASS`, `COLOR`, `ORGANIZER`, `SEQUENCE`, `URL` **and corrupts `GEO`**
  (so VTODO promote→demote→promote is **not a fixpoint**); VJOURNAL loses
  `ATTACH`, `ATTENDEE`, `EXDATE`, `ORGANIZER`, `RECURRENCE-ID`,
  `RELATED-TO`, `RRULE` — the `RECURRENCE-ID` drop collapsing a detached
  instance onto its master, which is identity corruption, not field loss.

  **Six findings filed:** O85 (every alarm round-trips back *disabled*, all
  four sites, VTODO included — so W5 fixed the trigger form and left the
  symptom), O86 (kcalendarcore 6.29.0 serializes `GEO` corrupt — upstream,
  reproduces with no libkalburator in the picture), O87 (VJOURNAL's
  undeclared drops), O88 (one edge-level loss profile serves three kinds;
  `canonToVjournalLoss()` is dead code with a false comment), O89 (VTODO's
  canonical representation depends on transport metadata — the non-DAV
  backends never demux), O90 (demote is not a pure function of canon:
  heap-derived attendee `X-UID`).

  **Why they kept surfacing sideways, and the fix:** IP.1's gate asserts
  *emitted ⊆ catalogued* — agreement between two of **our own** artifacts.
  Every defect above is a disagreement between our emitter and **RFC
  5545**, and nothing measures that axis. Hence **IP.8**, which now runs
  next, before IP.3.

  **Two false positives recorded, not filed** (both cost time; both are
  pinned in `probes/README.md` so they cost nobody else any): libical drops
  the whole `ATTENDEE` property when the mail domain is single-label
  (`a@x`), and a per-line iCal parse misreports `ATTENDEE` as lost because
  KCalendarCore folds that line before its colon. Attendees round-trip
  correctly.

  Suite re-confirmed at `40854f3`: 214 slots, 210 green, the same 4
  environmental Radicale/KDAV reds — diagnosis re-verified from their
  failure text (*"the requested timeout (15000 ms) was too short, 29700 ms
  would have been sufficient"*), not from their names.

  Consumer report issued:
  `docs/2026-09-02-incidence-parity-planstan-report.md`, carrying two
  blocking questions (Q1 → IP.11, Q2 → IP.7b).

- **NEXT:** **IP.8** — the RFC-5545 round-trip fidelity gate. Tests only;
  lands RED, one `QEXPECT_FAIL` per known defect, each naming the item that
  will close it. Read `PLAN.md` §A.4 IP.8 for the acceptance criteria, and
  `2026-09-02-preflight-audit.md` §2.1 for the expected red list. Build the
  fixtures from **RFC 5545**, not from what the emitters happen to handle —
  a fixture derived from our own capabilities makes the gate vacuous, which
  is the exact failure mode IP.8 exists to end.
