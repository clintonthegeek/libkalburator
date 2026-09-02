# Incidence-parity campaign STATUS

Binding plan: `PLAN.md` in this directory (read it before touching any
item — it carries the execution rules, the per-item acceptance criteria,
and the scope boundary). This file is the **live execution tracker**.

**Opened:** 2026-08-29. **Baseline:** `main` @ `fc1ae61`, 214 slots
(210 green + the 4 known environmental Radicale/KDAV slots). Re-confirmed
at `40854f3` on 2026-09-02.

**Last updated:** 2026-09-02 — **IP.3 DONE. O84 RESOLVED.** Contributed
catalogues landed: `eventCanonContributedIds()`, `journalCanonContributedIds()`,
`vtodoCanonContributedIds()` declared next to their emitters;
`makeCalendarCanonCatalogue()`/`makeTodoCanonCatalogue()` now build from the
union of those exports plus a corrected vendor-only-key list (PLAN.md's own
list had two wrong entries — `descriptionHtml`/`freeBusyStatus` are real
emitter output, not vendor-only; see the receipt). O84 fixed:
`CanonJsonMerger::merge()` threads `_canon.kind` through the re-stamp;
disagreement case gets a deliberate, logged precedence rule, not a silent
pick (follow-up filed as **O92**). No catalogue orphans found; `allDay`
turned out NOT to be one (event/journal both emit it top-level; only VTODO
embeds it in start/due). **IP.9 runs next.**

Earlier the same day: PlanStan answered; PLAN.md Amendment 2 adopted.
Q1 → **(a) converge** (ratified, and (b) is *blocked* on their data model,
not scheduled); Q2 → **DTSTART-wins** with a unifying principle.
**Nothing is blocked on a consumer any more.** Order revised again:
**IP.6 and IP.10 advance ahead of IP.4/IP.5** — PlanStan disclosed that
`{calendar,canon}` VTODO is their *primary and default* task path, so
O83's seven undeclared drops are live on it.

Earlier still the same day: pre-flight audit landed, six findings
(**O85–O90**), five new items (**IP.8–IP.12**).

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
| — | IP.8 | **RFC-5545 round-trip fidelity gate** — maximal conformant fixture → promote → demote → diff property sets, per kind; + VALARM sub-gate | *proves* **O85, O86, O87**; re-pins O79, O83; *files* **O91** | **DONE 2026-09-02** — landed RED exactly as predicted, plus four newly-discovered undeclared drops (O91). Tests only. Receipt: `2026-09-02-ip8-return-receipt.md`. |
| — | IP.3 | Contributed catalogues — each canon-fields module exports the ids it emits | O78 *class*, **O84** | **DONE 2026-09-02** — catalogues now built structurally from contributor unions; O84 fixed (kind threaded through merge); no orphans; matrix byte-identical. Receipt: `2026-09-02-ip3-return-receipt.md`. |
| **2** | **IP.9** | **Kind-scoped loss profiles** — one edge currently carries an event-only profile for all three kinds; `canonToVjournalLoss()` is dead code | **O88** | NOT STARTED — **gates IP.4/IP.6/IP.10**: until it lands there is nowhere truthful to declare a VTODO or VJOURNAL loss. |
| 3 | IP.6 | `incidencecommonfields` extraction (3 kinds), then the missing VTODO fields as a separate commit; **drop `geo`** | **O83**, **O86** | NOT STARTED — **advanced from 6.** Highest-impact user-data fix: these drops are live on PlanStan's *default* task path. GEO question **settled — drop it** (Amendment §B.5). |
| 4 | **IP.10** | **VJOURNAL parity** — `RECURRENCE-ID` identity first, then `RRULE`/`EXDATE`, then the common fields from IP.6 | **O87** | NOT STARTED — **advanced from 7.** Depends on IP.6's extraction. Closes the second-highest severity item (identity corruption). |
| 5 | IP.4 | Shared VALARM module + VEVENT promote/demote + both vendor event legs, one commit | **O79**, **+O85** | NOT STARTED — see Amendment §A.3.1. Moved because IP.6/IP.10 got *more* urgent, **not** because PlanStan lacks alarm UI — they asked us explicitly not to deprioritise it (they passthrough other clients' alarms). |
| 6 | IP.5 | `CanonEnvelope::stampProviderExtrasDigest()` across calendar/journal/contacts; retrofit the 3 todo sites | **O80** | NOT STARTED |
| 7 | IP.7 | VEVENT RANGE=THISANDFUTURE refusal (a) + DTSTART/DTEND coercion contract (b) | O81, O82 | NOT STARTED — **IP.7b UNBLOCKED**: DTSTART-wins ratified, precise rule in Amendment §B.2. Contract doc first. |
| 8 | **IP.11** | **Convergence proof** — crossing gate showing the two VTODO paths yield equivalent canon; make the silent fallback loud | **O89** | NOT STARTED — **UNBLOCKED and rescoped** (§B.4). No longer a design choice. **Do not implement (b) routing or leave hooks for it.** |
| 9 | **IP.12** | Demote purity — strip the heap-derived attendee `X-UID` | **O90** | NOT STARTED |

**Consumer dependency: NONE — both questions answered 2026-09-02.**
Report: `docs/2026-09-02-incidence-parity-planstan-report.md`. Response:
`docs/2026-09-02-incidence-parity-planstan-response.md` (Q1 → (a) converge;
Q2 → DTSTART-wins). Both ratified, neither provisional. Read the response
before IP.6, IP.7b or IP.11 — it settles the GEO question and rescopes
IP.11 entirely.

**Tag owed to PlanStan: CUT.** `v1.04` pushed 2026-09-02 at `76bbcf3`
(B2C P0–P3 + vtodo-parity W1–W8 + IP.1/IP.2), with a `KNOWN DEFECTS AT THIS
TAG` section naming O79/O83/O84/O85/O86/O87 and their owning items.
**Nothing owed; do not re-raise.** If you cut another tag mid-campaign,
carry the same defect section forward.

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

- **2026-09-02 — PLANSTAN ANSWERED. Amendment 2 adopted; nothing blocked on
  a consumer.** Response filed at
  `docs/2026-09-02-incidence-parity-planstan-response.md` (PlanStan @
  `master` `e1856650`, pinned `v1.01`). Both answers evidenced against their
  tree, neither provisional.

  **Q1 → (a) converge**, and *not* as the "no strong view" default our
  report offered. Two things came back that we could not have seen from
  this side:

  1. **`{calendar,canon}` VTODO is their PRIMARY and DEFAULT task path.**
     `todo_work.kalb` — the fixture their whole todo-UX campaign was built
     against — binds to the `local` backend, which never demuxes.
     `Test6.kalb` is a real GTD vault whose seven task lists are each
     *mirrored* across `local` + `multiproto-dav`, so every task is a
     `{calendar,canon}` VTODO on both legs at once. Their org backend is
     task-first and likewise never demuxed. **So O83's seven undeclared
     drops are live on the default task path of the consuming application,
     and W1's composite exception identity is not reaching the vault their
     todo work is tested against.** O83 annotated accordingly.
  2. **(b) route is BLOCKED, not scheduled.** Their domain axis is binary
     and hardcoded; domain ids are persisted verbatim in every vault (and
     the local backend's id is the bare string `local`, with no domain
     segment to move); a mismatched binding fails *silently* by loading the
     calendar unfiltered; and decisively, `CalendarType::Hybrid` is their
     **default** — under (b) a hybrid LC would need two primary bindings in
     two domains, which their model cannot express, so half of every hybrid
     calendar would stop loading. Recorded in FINDINGS O89 so it is not
     re-proposed as a rename.

  **Q2 → DTSTART-wins, confirmed** — with a better principle than the plan
  had: *the mandatory temporal anchor wins; the optional derived bound is
  coerced to match it.* That makes W6.2 (DUE-wins for VTODO) and this **the
  same rule** applied to components with opposite optionality, not a
  divergence — and VJOURNAL then falls out for free at IP.10 instead of
  needing a third decision. Amendment 1 §A.3.3's "deliberate divergence"
  framing was right about the action and wrong about the reason; corrected
  in §B.2. Their three-part rule adopted, including item 2 (drop a
  degenerate `DTEND` rather than clamp) — we took their stated *preference*
  after checking RFC 5545 §3.6.1, which agrees: a non-conforming pair has no
  valid value to clamp to, and the absent-`DTEND` default is already
  defined, so dropping falls back to a defined behaviour while clamping
  would invent a bound the author never wrote.

  **Order revised (§B.3):** IP.6 and IP.10 advance ahead of IP.4/IP.5 on
  finding 1 above. IP.4 moved because the others got *more* urgent — **not**
  because PlanStan lacks alarm UI; they raised that themselves and asked us
  not to deprioritise it, since they passthrough alarms other clients
  authored.

  **Settled, stop flagging (§B.5):** matrix reshape is a no-op for them
  (they parse and pin nothing); new loss warnings are wanted with no spam
  risk; **`geo` — drop it**, they don't consume it and asked us not to
  hand-serialize around an upstream bug on their account, closing Amendment
  1 §A.3.2 as option (b); VJOURNAL additive fields and the alarm `enabled`
  key are fine either way.

  **Received, not ours (§B.6):** they acknowledged the W1 composite-id
  decomposition warning and named the three places it bites them. Tracked on
  their side. Do not re-issue it.

  **We owe them a tag (§B.7):** they are pinned at `v1.01` and all of
  vtodo-parity W1–W7 is on `main` untagged, so they cannot consume the work
  they asked for. Not gating anything, by their own statement.
  — **RESOLVED same day: `v1.04` cut and pushed, see the next entry.**

- **2026-09-02 — `v1.04` CUT AND PUSHED** (annotated, at `76bbcf3`).
  Closes the one thing this repo owed PlanStan (PLAN §B.7): they were
  pinned at `v1.01` with all of B2C P0–P3 and vtodo-parity W1–W8 landed on
  `main` and untagged, unable to consume the work they asked for. The tag
  message deliberately carries a **`KNOWN DEFECTS AT THIS TAG`** section
  naming O79, O83, O84, O85, O86 and O87 with their owning items — an
  adopter should not have to read this plan to learn that the default VTODO
  path drops seven properties. **Carry that section forward on any future
  mid-campaign tag.** Suite at the tag: 214 slots, 210 green, the 4 known
  environmental reds.

- **2026-09-02 — IP.8 DONE.** New file
  `tests/calendar/tst_incidence_rfc5545_fidelity.cpp` (13 slots, registered
  in `tests/calendar/CMakeLists.txt` next to `tst_calendar_kind_dispatch`)
  — landed RED for exactly the reasons PLAN.md predicted, **plus** four
  properties the pre-flight audit's own fixture had not been maximal
  enough to catch. Fixtures were built directly from RFC 5545 §3.6.1/
  §3.6.2/§3.6.3/§3.6.6's ABNF grammar (every property each component
  PERMITS, not what `eventcanonfields.cpp`/`vtodocanonfields.cpp`/
  `journalcanonfields.cpp` happen to read) — verified against the WebFetch
  of the RFC text plus this session's own knowledge of the grammar,
  cross-checked empirically against the real pipeline via scratch probes
  before being committed to the QTest file (see the return receipt for the
  exact probe transcripts).

  **Red list observed, by kind** (property NAME lost on promote→demote,
  computed on RFC 5545 §3.1-unfolded text, master+exception fixtures
  unioned):
  - `(calendar, vevent)`: `COMMENT`, `CONTACT`, `GEO`, `RELATED-TO`,
    `REQUEST-STATUS`, `RESOURCES` (6). Fixpoint: stable (as predicted).
  - `(calendar, vtodo)`: `ATTACH`, `ATTENDEE`, `CLASS`, `COLOR`, `COMMENT`,
    `CONTACT`, `ORGANIZER`, `REQUEST-STATUS`, `RESOURCES`, `SEQUENCE`,
    `URL` (11). Fixpoint: **NOT** stable (O86 GEO corruption, as
    predicted).
  - `(calendar, vjournal)`: `ATTACH`, `ATTENDEE`, `COMMENT`, `CONTACT`,
    `EXDATE`, `ORGANIZER`, `RDATE`, `RECURRENCE-ID`, `RELATED-TO`,
    `REQUEST-STATUS`, `RRULE` (11). Fixpoint: stable (as predicted).
  - VALARM sub-gate (VEVENT + VTODO × 4 trigger forms): VEVENT's
    end-relative/absolute/repeat-duration forms corrupted (O79, as
    predicted; start-relative survives); **every** alarm on **both** kinds
    comes back disabled (O85, as predicted).

  **Deviation from the predicted list — filed as O91, not fixed, not
  silently pinned**, per PLAN.md §1's prohibition: `COMMENT`, `CONTACT`,
  `RESOURCES` (all three kinds where RFC-valid) and `REQUEST-STATUS` (all
  three kinds) are ALSO lost, beyond the pre-flight audit's declared list.
  `COMMENT`/`CONTACT`/`RESOURCES` are ours — `KCalendarCore` models all
  three natively and no emitter reads any of them. `REQUEST-STATUS` is
  upstream — `KCalendarCore` has no public accessor for it at all (grep
  across `/usr/include/KF6/KCalendarCore/` confirms), so no emitter can
  promote what the toolkit never exposes. Full detail, evidence and
  ownership: FINDINGS.md O91; receipt `2026-09-02-ip8-return-receipt.md`.

  Non-vacuity verified the IP.1/IP.2 way: temporarily disabled one
  `QEXPECT_FAIL` (VEVENT's GEO check), rebuilt, confirmed a real `FAIL!`
  with the exact expected message, restored, rebuilt clean (13 passed, 0
  failed). No `src/` change. Matrix untouched (confirmed, not assumed —
  no loss profile or edge changed). Full suite: 214 → **227** slots (+13),
  223 passed, 4 failed — the same 4 known environmental Radicale/KDAV
  slots, verified by failure text not name.

- **2026-09-02 — IP.3 DONE. O84 RESOLVED.** Each canon-fields module now
  exports the top-level `PropertyId`s its emitter can produce, declared
  next to the emitter: `eventCanonContributedIds()`
  (`eventcanonfields.{h,cpp}`), `journalCanonContributedIds()`
  (`journalcanonfields.{h,cpp}`), `vtodoCanonContributedIds()`
  (`vtodocanonfields.{h,cpp}` — shared by both the `todo` and `calendar`
  domains, matching `icalcanonstages.cpp` calling `todoFieldsToCanon`
  directly). `makeCalendarCanonCatalogue()` and `makeTodoCanonCatalogue()`
  no longer hand-list ids: each keeps a local `id → {kind, displayName}`
  metadata table (unchanged content from before this item, just
  reorganised into a lookup) and builds its catalogue from the UNION of
  the relevant contributor exports plus a corrected vendor-only-key list,
  looking up metadata per id (falling back to a safe `Json` default for an
  id with no metadata entry yet, so a brand-new contributed id is never
  silently dropped).

  **PLAN.md's vendor-only list had two wrong entries — corrected, not
  transcribed.** Grepping every top-level `obj.insert(...)` across
  `eventcanonfields.cpp`/`journalcanonfields.cpp`/`vtodocanonfields.cpp`
  against every id in both catalogues (43 calendar keys + 26 todo keys,
  full sweep) showed `descriptionHtml` (X-ALT-DESC carrier) and
  `freeBusyStatus` (X-MICROSOFT-CDO-BUSYSTATUS carrier) are produced
  directly by `eventcanonfields.cpp` (both) and `vtodocanonfields.cpp`
  (`descriptionHtml` only) — real emitter output, not vendor-JSON-only.
  PLAN.md's IP.3 body listed both as vendor-only; corrected to 12
  vendor-only event keys (`locations`, `onlineMeeting`, `eventType`,
  `typedProperties`, three `guestsCan*`, `allowNewTimeProposals`,
  `hideAttendees`, `locked`, `privateCopy`, `responseRequested`) instead
  of 14. The catalogue's actual id SET is unchanged — this only moves
  which list declares two ids, so no behavior change.

  **`allDay` is NOT an orphan — the plan's suspicion was wrong, verified
  by reading (not just grepping) all three emitters.** `eventcanonfields.cpp`
  and `journalcanonfields.cpp` both `obj.insert("allDay", ...)` at the TOP
  level (inside the start/end construction block) AND both demote paths
  read it back (`obj.value("allDay").toBool()`) to call `setAllDay()`.
  Only `vtodocanonfields.cpp` embeds `allDay` solely inside the
  `start`/`due` sub-objects and never reads/writes it top-level — PLAN.md's
  text was describing the VTODO leg correctly but generalising it
  incorrectly to all three kinds. Left in place; noted as the surprise it
  is.

  **No other orphans** — full sweep confirmed: every one of
  `calendarcanonproperties.cpp`'s 43 non-uid keys and
  `todocanonproperties.cpp`'s 26 non-uid keys is accounted for by exactly
  one of {event contributor, todo contributor, journal contributor,
  vendor-only list for that domain}. Method: grepped every catalogue key
  against `eventcanonfields.cpp`/`journalcanonfields.cpp`/
  `vtodocanonfields.cpp`/`mseventcanonstages.cpp`/`googlecanonstages.cpp`/
  `googletaskcanonstages.cpp`/`mstodotaskcanonstages.cpp`, then hand-read
  every hit to reject false positives (e.g. a quoted `"end"` inside an
  alarm's `related` VALUE, not a top-level key) before accepting a key as
  produced.

  **O84 fixed.** `CanonJsonMerger::merge()` now threads
  `CanonEnvelope::kind(t)`/`kind(s)` into the 5-arg `stampEnvelope` instead
  of calling the 3-arg overload that erased it. Decision on whose kind
  wins recorded in FINDINGS.md O84's resolution and `canonjsonmerger.cpp`'s
  own comment: agreement or only-one-side-has-a-kind is the easy case
  (target-preferred, source-fallback, mirroring `mergedUid`'s own rule
  immediately above); genuine disagreement (both sides non-empty and
  different) is treated as an O55-class identity conflict — logged loudly
  via `qWarning()` (uid, domain, both kinds) rather than picked silently,
  then resolved by a deliberate precedence rule (target's kind wins,
  matching the function's existing target-primary bias) rather than a true
  abort-the-sync fail-loud, because `RecordMerger::merge()` has no error
  channel to abort through — that gap is filed as **O92**, not built here
  (out of IP.3's catalogue/envelope-seam scope). `mergerPreservesIncidenceKind()`
  in `tests/shape/tst_canonjson_diff_merge.cpp` now passes for real — both
  `QEXPECT_FAIL`s removed, no XPASS. Two new slots added:
  `mergerPreservesIncidenceKindWhenOnlySourceHasOne()` (easy case, only one
  side has a kind) and `mergerKindDisagreementKeepsTargetKindDeliberately()`
  (pins the deliberate precedence rule so it cannot silently drift).

  **Structural-coverage demonstration** (acceptance criterion): added a
  throwaway `PropertyId{"ip3ThrowawayDemoKey"}` to `vtodoCanonContributedIds()`'s
  returned list only, rebuilt `kalburator` + `tst_property_catalogue` +
  `tst_calendar_kind_dispatch`, and ran a small scratch probe linking the
  built static library that calls `Kalburator::Calendar::calendarCanonPropertyIds()`
  and `Kalburator::Todo::todoCanonPropertyIds()` directly — both returned
  lists contained the throwaway id with **zero edits** to
  `calendarcanonproperties.cpp` or `todocanonproperties.cpp`. Confirmed
  `tst_calendar_kind_dispatch` (IP.1's gate) still 14/14 green with the
  throwaway key present (a superset addition can only help the
  emitted-⊆-catalogued check, never break it). Reverted the throwaway key
  (`diff` against the pre-edit backup confirmed byte-identical revert),
  rebuilt clean.

  Matrix regenerated (`matrixgen`): **byte-identical** to the committed
  copy — confirmed by diff, not assumed, per house rule O63; no loss
  profile or edge changed by this item. `tst_gm_pipeline_convergence`
  green.

  **Deferred, per PLAN.md's own recommendation** (not built, logged only):
  whether vendor-only keys should become a fourth contributor exported
  from the vendor canon stages (`mseventcanonstages`, `googlecanonstages`,
  the todo vendor stages). PLAN.md's own text already recommends yes-but-
  not-in-this-item; left as a follow-up for whichever future item touches
  the vendor stages' catalogue surface.

  Full suite: **215 tests, 211 passed, 4 known-environmental failed**
  (`tst_backend_signals`, `tst_backend_thread_relocation`,
  `tst_backend_reentrancy_pin`, `tst_remotecalendarbackend`) — unchanged
  from the IP.8 baseline; IP.3 added test slots inside an existing ctest
  binary (`tst_canonjson_diff_merge`), not a new ctest executable, so the
  ctest-level count does not move. Verified by failure TEXT (KDAV
  30s-transfer-timeout / "would have been sufficient" pattern), not by
  name, per CLAUDE.md's standing instruction. One incidental build-
  environment issue hit and fixed during this session, unrelated to the
  code change: a corrupted/zero-length `tst_property_catalogue` binary
  from an earlier interrupted parallel build attempt (`file` reported
  `data`, no ELF header) — deleted and rebuilt clean, not a real defect.
  New finding filed: **O92** (`CanonJsonMerger` has no error channel for a
  genuine kind-disagreement fail-loud — see FINDINGS.md).

  Receipt: `2026-09-02-ip3-return-receipt.md`.

- **NEXT:** **IP.9** — kind-scoped loss profiles (closes O88). Gates
  IP.4/IP.6/IP.10. Read `PLAN.md`'s IP.9 section plus Amendment 1/2 for
  the current binding order.
