# Incidence-parity campaign STATUS

Binding plan: `PLAN.md` in this directory (read it before touching any
item — it carries the execution rules, the per-item acceptance criteria,
and the scope boundary). This file is the **live execution tracker**.

**Opened:** 2026-08-29. **Baseline:** `main` @ `fc1ae61`, 214 slots
(210 green + the 4 known environmental Radicale/KDAV slots). Re-confirmed
at `40854f3` on 2026-09-02.

**Last updated:** 2026-09-02 — **IP.4 DONE. O79, O85 RESOLVED.** Full
session log entry below; IP.5 is next.

**Previously — 2026-09-02 — IP.10 DONE. O87 RESOLVED** (RELATED-TO
excepted — see O95).

**Previously — 2026-09-02 — IP.6 DONE. O83, O86, O91 (partially —
comment/contact) and O93 RESOLVED.** New finding **O94** (upstream).

**Previously — 2026-09-02 — IP.9 DONE. O88 RESOLVED.** One
`{calendar,canon}→{calendar,ical}` edge is kind-polymorphic (VEVENT/VTODO/
VJOURNAL); `TransformationEdge` now carries a `lossByKind` override map
(design (b), PLAN's recommendation — verified (a) is not expressible,
`TransformationRegistry` keys edges strictly on `(from,to)` and asserts on
a second registration of the same pair) so ONE edge can carry three
profiles, selected by `Pipeline::composedLoss(kind)` /
`SyncEngine::materializedLoss()`. `canonToVjournalLoss()` (was dead code)
and a new `canonToVtodoIcalLoss()` are populated with TODAY's actual O83/
O91/O86 drops (declared, not fixed — that stays IP.6/IP.10's job).
Matrix regenerated (substantial, expected diff — three new `canon → ical
(kind)` sections). IP.8's `expectedLossTable()` TODO(IP.9) closed for
vtodo/vjournal (now derived from the real profiles via a canon-id→RFC-name
translation); vevent stays a literal list on purpose (its own profile
content is IP.6's scope, not O88's). New FINDINGS: **O93** (the sibling
`{todo,canon}` edge shares the exact same undeclared VTODO drops — out of
scope, logged not fixed). Receipt: `2026-09-02-ip9-return-receipt.md`.

**Previously — 2026-09-02 — IP.3 DONE. O84 RESOLVED.** Contributed
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
embeds it in start/due). **IP.9 landed the same day, see above; IP.6 runs
next.**

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
| — | IP.9 | **Kind-scoped loss profiles** — one edge currently carries an event-only profile for all three kinds; `canonToVjournalLoss()` is dead code | **O88** | **DONE 2026-09-02** — `TransformationEdge::lossByKind` (design (b)); `canonToVjournalLoss()` repopulated, new `canonToVtodoIcalLoss()`; matrix now kind-aware (substantial, expected diff); IP.8's TODO(IP.9) closed for vtodo/vjournal. New finding **O93** (sibling `{todo,canon}` edge shares the same undeclared drops — logged, not fixed). Receipt: `2026-09-02-ip9-return-receipt.md`. |
| 2 | IP.6 | `incidencecommonfields` extraction (3 kinds), then the missing VTODO fields as a separate commit; **drop `geo`** | **O83**, **O86** | **DONE 2026-09-02** — two commits (structural extraction, zero behaviour change; then the field fixes). **O83, O86, O91 (VEVENT/VTODO/VJOURNAL comment/contact only) and O93 RESOLVED.** New finding **O94** (upstream: KCalendarCore's `ICalFormat` never reads/writes RESOURCES at all — corrects part of O91). Receipt: `2026-09-02-ip6-return-receipt.md`. |
| 3 | **IP.10** | **VJOURNAL parity** — `RECURRENCE-ID` identity first, then `RRULE`/`EXDATE`, then the common fields from IP.6 | **O87** | **DONE 2026-09-02** — RECURRENCE-ID identity (VTODO's W1 shape, W3 safety fix included), RRULE/RDATE/EXDATE (verbatim-lines convention), organizer/attendees/attachments/comments/contacts, descriptionHtml (X-ALT-DESC, newly wired) and the phantom `classification` key all fixed. **RELATED-TO is the one exception** — wired identically but blocked upstream on the promote side only; new finding **O95**. New finding **O96** (a sibling declaration gap, logged not fixed). Receipt: `2026-09-02-ip10-return-receipt.md`. |
| 4 | IP.4 | Shared VALARM module + VEVENT promote/demote + both vendor event legs, one commit | **O79**, **+O85** | **DONE 2026-09-02** — new `src/calendar/alarmshape.{h,cpp}` (W5's VTODO logic moved verbatim; new `describeAlarmRow()`); `eventcanonfields.cpp` and `vtodocanonfields.cpp` both point at it; `mseventcanonstages.cpp` demote fixed (route non-start-relative rows to carrier instead of misreading offset=0); `googlecanonstages.cpp` investigated independently — its absolute-alarm case was ALREADY correct (guard is strictly `< 0`), only its END-related case was broken, fixed the same way. O85: demote always `setEnabled(true)` (PLAN's recommended option; RFC 5545 has no disabled-alarm wire form, argued in the receipt). Loss profiles re-verified, **unchanged** (`alarms: Simplified` stays correct on both legs — the native-mapped alarm still drops text/repeat, independent of O79). Matrix byte-identical. Receipt: `2026-09-02-ip4-return-receipt.md`. |
| 5 | IP.5 | `CanonEnvelope::stampProviderExtrasDigest()` across calendar/journal/contacts; retrofit the 3 todo sites | **O80** | NOT STARTED |
| 6 | IP.7 | VEVENT RANGE=THISANDFUTURE refusal (a) + DTSTART/DTEND coercion contract (b) | O81, O82 | NOT STARTED — **IP.7b UNBLOCKED**: DTSTART-wins ratified, precise rule in Amendment §B.2. Contract doc first. |
| 7 | **IP.11** | **Convergence proof** — crossing gate showing the two VTODO paths yield equivalent canon; make the silent fallback loud | **O89** | NOT STARTED — **UNBLOCKED and rescoped** (§B.4). No longer a design choice. **Do not implement (b) routing or leave hooks for it.** |
| 8 | **IP.12** | Demote purity — strip the heap-derived attendee `X-UID` | **O90** | NOT STARTED |

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

- **2026-09-02 — IP.9 DONE. O88 RESOLVED.** Design (b) chosen —
  `TransformationEdge::lossByKind` (a `QHash<QString, LossProfile>`
  override map; `.loss` stays the default/untagged-kind profile) — after
  verifying (a) is genuinely not expressible: `TransformationRegistry`
  keys `m_edgesFrom`/`findEdge`/`registerEdge` strictly on `(from, to)`
  `Shape` pairs, and `registerEdge` asserts on a conflicting second
  registration of the same pair, so three kind-discriminated edges for
  the same shape pair cannot be registered without a real interface
  change. `Pipeline::composedLoss(kind = QString())` selects per-edge via
  the new `TransformationEdge::lossFor(kind)`, defaulting to exact
  pre-IP.9 behaviour for every edge outside the calendar domain.
  `SyncEngine::materializedLoss()` now extracts `_canon.kind` from the
  already-parsed `canonData` and passes it through.

  Two per-kind profiles landed: `Kalburator::Calendar::canonToVtodoIcalLoss()`
  (new, `icalcanonstages.{h,cpp}` — deliberately NOT alongside
  `Kalburator::Todo::canonToVtodoLoss()`, a different function for a
  different edge, `{todo,canon}→{todo,vtodo}`) and `canonToVjournalLoss()`
  (repopulated in place, `journalcanonfields.{h,cpp}` — was dead code with
  a false "no loss" comment). Both declare TODAY's actual O83/O91 drops
  (11 rows for vtodo incl. `geo` as `Degraded` for O86's value-corruption-
  not-name-loss; 9 `PropertyId`s / 11 RFC names for vjournal, `recurrence`
  covering RRULE+RDATE+EXDATE at once) — declared, not fixed, per PLAN.md
  §1. Four properties (`comments`/`contacts`/`resources`/`requestStatus`,
  O91) have no existing catalogued `PropertyId` at all (no emitter
  produces them) — declared as new uncatalogued `PropertyId` literals
  anyway, verified safe by reading `LossProfile`'s own contract (no
  catalogue cross-check anywhere in its use); deliberately NOT added to
  `calendarcanonproperties.cpp`, which would misrepresent them as
  emitter-producible. vevent's `canonToIcalLoss()` was left untouched —
  its own RFC-name drops (`GEO`/`RELATED-TO`/`COMMENT`/`CONTACT`/
  `RESOURCES`/`REQUEST-STATUS`) are explicitly IP.6's scope per Amendment
  1 §A.3.2, not O88's.

  `ConvergenceMatrix` now renders one subsection per kind for a
  `lossByKind`-carrying edge, plus a `(kind-scoped: ...)` inventory
  annotation. Matrix diff (expected substantial, per PLAN.md): the
  calendar domain's `### canon → ical` section relabeled `(default)`
  (byte-identical content — still `canonToIcalLoss()`), two new sections
  `(vjournal)` (9 rows) and `(vtodo)` (12 rows) appended; every other
  domain/edge section (contacts, todo, calendar's org-ical/google-event/
  ms-event) byte-identical, confirmed by full diff not assumed.
  `tst_gm_pipeline_convergence` green.

  IP.8's `expectedLossTable()` `TODO(IP.9)` closed for vtodo/vjournal — a
  new `droppedRfcNames()` helper translates each profile's `Dropped`
  `PropertyId`s to RFC 5545 property names via a small, necessarily
  hand-declared canon-id→RFC-name table (one-to-many for `recurrence`),
  with a `Q_ASSERT_X` guarding against a future untranslated id silently
  under-reporting. vevent's table stays a hand-typed literal, deliberately
  — its real profile is a different vocabulary of loss (canon-JSON vendor
  keys, no RFC-name counterpart) from what this gate measures, and wiring
  it now would mean fabricating entries IP.6 hasn't ratified.

  New test coverage: three slots in `tests/calendar/tst_calendar_kind_dispatch.cpp`
  (`vtodoDemoteLossProfileIsVtodoShapedNotEventShaped`,
  `vjournalDemoteLossProfileIsVjournalShapedNotEventShaped`,
  `veventDemoteLossProfileUnchangedByIp9`) pin the FULL declared content
  of both new profiles against the REAL registered `CalendarStockShapes`
  graph — 14→17 QTest slots in that binary (non-vacuity verified: removed
  one profile entry, confirmed a real `FAIL!`, reverted). A full
  `SyncEngine`-level `transcodingWarning` demonstration was attempted
  (using `geo`, the one VTODO-profile property that actually round-trips
  into canon today) and abandoned: `MockBackend::addIncidence()`
  round-trips through `KCalendarCore::ICalFormat` immediately at insert
  time, so O86's GEO corruption strikes before the sync engine ever runs,
  making `geo` never "present" in canon by the time `materializedLoss()`
  would check — no property currently exists that is both actually
  materializable in canon AND one of this item's new drops. See the
  receipt §10. Consequence: the ctest-level EXECUTABLE count does not
  move (215→215, same as IP.3's own precedent for slots added to an
  existing binary) even though QTest-slot count grew (+3).

  Dead-code grep proof (`grep -rn "canonToVjournalLoss" src/ tests/`):
  now three real call sites (edge registration + two IP.8-gate wiring
  references), zero-caller state gone. Spot-checked all 12
  `LossProfile`-returning functions declared under `src/` for the same
  "declared, zero callers" pattern — none found.

  New finding: **O93** — `{todo,canon}`'s own `canonToVtodoLoss()`
  (`src/todo/vtodocanonstages.cpp`) demotes through the IDENTICAL emitter
  code as the calendar-domain leg IP.9 just fixed, so it carries the exact
  same undeclared O83/O91 drops — PLAN.md's IP.9 body called this leg
  "already good," verified false. Logged, not fixed (out of IP.9's
  scope — O88 is the calendar domain's kind-polymorphism specifically);
  not owned by any item yet.

  Full suite: `cmake --build build -j$(nproc)` (clean, no errors) +
  `ctest --output-on-failure` (full, unfiltered): **215 tests, 211
  passed, 4 failed** — same known-environmental set as the IP.3 baseline,
  verified by failure TEXT (three of four carry the documented KDAV
  30s-transfer-timeout signature; the other two show the local Radicale
  server at `127.0.0.1:5232` returning 412/409 on calendar/item creation
  — a different transient symptom of the same local-Radicale-dependency
  class, not by name alone).

  Receipt: `2026-09-02-ip9-return-receipt.md`.

- **2026-09-02 — IP.6 DONE. O83, O86, O91 (comment/contact half) and O93
  RESOLVED. New finding O94 (upstream).** Two commits, strictly separated
  per PLAN.md's instruction.

  **Commit 1 (structural, zero behaviour change).** New
  `src/calendar/incidencecommonfields.{h,cpp}` extracts exactly the fields
  verified — by reading all three emitters, not by trusting PLAN.md's
  starting list — to already be promoted/demoted identically across VEVENT/
  VTODO/VJOURNAL today: `created`/`lastModified` (the O41 literal-presence
  guard, previously three independent copies), `summary`, `description`,
  `categories`, and the generic X-prop passthrough into `providerExtras`
  (parameterized by sub-key name + skip-list, preserving VEVENT's two-key
  skip and VTODO's `providerExtrasDigest` stamp exactly).
  `eventcanonfields.cpp`/`vtodocanonfields.cpp`/`journalcanonfields.cpp`
  rewired to call it. **Deviation from PLAN.md's suggested list, argued in
  the receipt:** `sequence`, `classification`, `color`, `url`, `location`,
  `organizer`, `attendees`, `attachments`, `descriptionHtml`, `priority`
  were NOT extracted here — each is only 2-of-3 kinds identical today (the
  third kind's absence is either the O83 defect itself or, for
  `descriptionHtml`, deliberately deferred to IP.10 per PLAN.md's own
  text) — extracting a field only two kinds use is not "genuinely
  identical across all three," it would just relocate a divergence.
  Full suite unchanged: 215 tests, 211 passed, 4 known-environmental.

  **Commit 2 (the fixes, all together).**
  1. **O83 CLOSED**: VTODO gains `sequence`/`classification`/`color`/`url`/
     `organizer`/`attendees`/`attachments` — `vtodocanonfields.cpp` now
     calls the newly-extended `incidencecommonfields` functions (moved
     there from `eventcanonfields.cpp`'s own copies, since all three kinds
     are now genuinely identical). `vtodoCanonContributedIds()` and both
     catalogues (`calendarcanonproperties.cpp`,
     `todocanonproperties.cpp`) updated with matching `PropertyKind` +
     display-name metadata (todo's catalogue previously had NONE of these
     seven declared at all — the generic-Json fallback would have silently
     caught them, IP.3's safety net working as designed, but real metadata
     is more honest).
  2. **VEVENT gains `RELATED-TO`** (Amendment 1 §A.3.2) via the same
     common `promoteRelatedTo`/`demoteRelatedTo` VTODO's existing code was
     extracted into.
  3. **O86 RESOLVED — `geo` dropped entirely**, per the already-ratified
     decision (option (b), Amendment 2 §B.5): removed from
     `vtodocanonfields.cpp` (the only place it lived) and from
     `vtodoCanonContributedIds()`; metadata entries left in both
     catalogues (still valid RFC 5545 properties, argued in the receipt).
     Declared `Dropped` (not `Degraded`) in `canonToIcalLoss()`,
     `canonToVtodoIcalLoss()`, `canonToVtodoLoss()`. VTODO's O86
     promote→demote→promote fixpoint failure is resolved as a direct
     consequence.
  4. **O91 — judgment call, VEVENT+VTODO+VJOURNAL all wired for
     `comments`/`contacts`.** RFC 5545 §3.6.3's jourprop grammar permits
     both on VJOURNAL (verified against the grammar, not assumed); the fix
     was the same one-line common-module call VEVENT/VTODO needed, so
     VJOURNAL was wired here rather than left for IP.10 — closing part of
     O87/O91's VJOURNAL scope early. **PLAN.md's IP.10 body text
     attributing VJOURNAL's COMMENT/CONTACT to IP.10 is now stale**;
     corrected in this entry and in the IP.10 row above so the next agent
     does not re-do them or get confused reading the stale attribution.
     `resources` stayed VEVENT+VTODO-only (RFC-excluded from VJOURNAL).
     `requestStatus` stays permanently uncatalogued/undeclared-fixable (no
     KCalendarCore accessor exists anywhere) — declared `Dropped` in every
     relevant profile, will never go green, noted so in IP.8's gate.
  5. **New finding O94 (upstream, filed this item)**: while adding round-
     trip coverage for O91's fields, direct probing found
     `KCalendarCore::ICalFormat` 6.29.0 **never reads or writes a
     RESOURCES line at all** — the object model (`resources()`/
     `setResources()`) is fully correct, but neither the parser nor the
     serializer touches the wire property. This **corrects O91's own
     claim** that resources() "round-trips fine through KCalendarCore's
     own ICalFormat" (true for COMMENT/CONTACT, false for RESOURCES,
     verified in the same fixture). Resolved the same way as O86: kept the
     correct object-model code (`promoteResources`/`demoteResources`,
     forward-compatible with a future kcalendarcore fix), declared
     `resources: Dropped` on all three affected ical wire edges
     (`canonToIcalLoss()`, `canonToVtodoIcalLoss()`, `canonToVtodoLoss()`
     — NOT the Google Tasks/MS To-Do vendor legs, which are pure JSON and
     unaffected).
  6. **Vendor todo legs updated honestly**: Google Tasks
     (`canonToGoogleTaskLoss()`) declares all ten new O83/O91 properties
     `Dropped` (no wire home, verified by reading the promote/demote code
     — no generic carrier exists there). MS To-Do
     (`canonToMsTodoTaskLoss()`) declares all ten `Reversible` — verified
     by reading its `kalburator.canon` open-extension auto-carry loop
     (`propFromCarrierKey`/`carrierKey`, type-generic JSON string
     round-trip), which was already carrying `percentComplete`/`geo`/etc
     the same way; none of the ten new ids is in that file's `handled`
     set, so they auto-carry without any code change there.
  7. **IP.8's gate flipped GREEN for VEVENT and VTODO.** Removed the
     per-defect `QEXPECT_FAIL` blocks for GEO/RELATED-TO (VEVENT), O83's
     seven (VTODO), and COMMENT/CONTACT/RESOURCES/REQUEST-STATUS (both) —
     the permanent drops (GEO, REQUEST-STATUS, RESOURCES) are folded
     honestly into the "real gate" `expectedLost` lists instead (same
     treatment `onlineMeeting`/`eventType` already get — no dedicated
     `QEXPECT_FAIL`, because no future item owns "fixing" a permanent,
     ratified drop). VTODO's `expectFixpoint` flipped `false`→`true` (O86
     resolved). VJOURNAL's remaining `QEXPECT_FAIL` (ATTACH/ATTENDEE/
     EXDATE/ORGANIZER/RECURRENCE-ID/RELATED-TO/RRULE/RDATE, still IP.10's)
     is unchanged; its COMMENT/CONTACT/REQUEST-STATUS block was removed
     (COMMENT/CONTACT fixed, REQUEST-STATUS folded into the permanent
     list the same way).
  8. **New test coverage**: `tst_calendar_kind_dispatch.cpp`'s two IP.9-era
     pins (`vtodoDemoteLossProfileIsVtodoShapedNotEventShaped`,
     `vjournalDemoteLossProfileIsVjournalShapedNotEventShaped`) updated to
     match the fixed profiles, plus positive "must NOT still drop the
     fixed properties" regression guards. New slots:
     `tests/todo/tst_todo_canon_roundtrip.cpp`
     (`vtodoRoundTripPreservesO83Fields`,
     `vtodoCommentsContactsRoundTripResourcesDoesNot`,
     `vtodoNeverPromotesGeoAnyMore`,
     `canonToVtodoLossProfileMatchesFixedEmitter` — the O93 resolution,
     directly pinned); `tests/calendar/tst_calendar_canon_roundtrip.cpp`
     (`icalRoundTripPreservesRelatedToCommentsContacts`, plus the three
     new `Dropped` entries added to the existing loss-profile slot).
  9. **Matrix regenerated** — diff exactly matches the loss-profile edits
     above (new `geo`/`requestStatus`/`resources` rows on the VEVENT/VTODO
     ical edges; `comments`/`contacts` rows removed from the VTODO-via-
     calendar and VJOURNAL sections; ten new rows on both todo vendor
     legs). `tst_gm_pipeline_convergence` green. No edge added — O63's
     edge-count grep not applicable (confirmed, only edge *content*
     changed).

  Full suite after commit 2: **215 tests, 211 passed, 4 known-
  environmental** (`tst_backend_signals`, `tst_backend_thread_relocation`,
  `tst_backend_reentrancy_pin`, `tst_remotecalendarbackend`), verified by
  failure TEXT not name — same KDAV-timeout/Radicale-412/409 signatures as
  every prior baseline in this campaign. ctest executable count unchanged
  (215→215): new QTest slots landed inside existing binaries, matching
  IP.3/IP.9's own precedent.

  Receipt: `2026-09-02-ip6-return-receipt.md`.

- **2026-09-02 — IP.10 DONE. O87 RESOLVED** (with one honestly-declared
  exception — see O95). VJOURNAL was the worst-attended kind
  (`journalcanonfields.{h,cpp}`) and is now at parity with VEVENT/VTODO on
  everything RFC 5545 §3.6.3's jourprop grammar permits.

  **Wired "for free" via IP.6's `incidencecommonfields.cpp`, exactly as
  PLAN.md predicted — genuinely wiring-only, no new field-specific logic
  needed:** `organizer`/`attendees`/`attachments` (`promoteOrganizer`/
  `demoteOrganizer` etc.) and `classification` (switched from the old
  hand-written, unconditional-insert block to the shared, guarded
  `promoteClassification`/`demoteClassification` — closes the phantom-key
  bug: a VJOURNAL with no CLASS no longer gains `classification: "public"`
  in canon). `comments`/`contacts` were confirmed ALREADY wired by IP.6
  (verified by reading `journalcanonfields.cpp` before touching it) — not
  re-done.

  **New journal-specific code, modeled on existing shapes rather than
  invented:**
  1. **RECURRENCE-ID identity (the most important part of this item)** —
     modeled on VTODO's W1 composite-exception-identity shape
     (`vtodocanonfields.cpp` ~340/~706), NOT VEVENT's (which still carries
     the O82 bug, IP.7's to fix, deliberately not copied here). Promote
     captures `recurrenceId`/`recurrenceRange`; demote sets the identity
     back and calls `journal->setThisAndFuture(false)`
     **unconditionally**, regardless of what canon's `recurrenceRange`
     says (the W3 safety rule — `RANGE=THISANDFUTURE` is never re-emitted,
     write-hostile on real CalDAV servers). A new slot,
     `vjournalMasterAndExceptionRemainDistinctThroughRoundTrip`
     (`tests/calendar/tst_calendar_kind_dispatch.cpp`), promotes a master
     and a detached-exception fixture sharing one uid, round-trips both
     through demote→promote, and asserts they remain distinguishable (the
     master carries no `recurrenceId` key, the exception does, and the two
     canon objects as a whole are not equal) — this is the identity-
     corruption fix PLAN.md called the single most important slot in this
     item.
  2. **RRULE/RDATE/EXDATE** — wired via
     `extractComponentRecurrenceLines(originalBytes, "VJOURNAL", uid)`,
     the exact same "recurrence" canon key VEVENT/VTODO already use
     (invariant 3, verbatim capture, never re-derived); demote re-injects
     the lines before `END:VJOURNAL`. New slot:
     `vjournalRoundTripPreservesRecurrenceVerbatim`.
  3. **`descriptionHtml`** — wired (X-ALT-DESC), mirroring VEVENT/VTODO's
     identical inline three-line pattern (not extracted into
     `incidencecommonfields.cpp` — IP.6 deliberately left that
     un-extracted too, three near-identical one-liners not being worth a
     shared function). Judgment call, decided in favour of wiring: RFC
     5545 gives VJOURNAL a real DESCRIPTION property, and X-ALT-DESC is a
     KOrganizer-family convention already generic across component kinds
     — there was no RFC or object-model reason to leave VJOURNAL out.

  **One exception, honestly declared rather than silently absorbed:**
  `relatedTo` is wired identically to VEVENT/VTODO (same
  `promoteRelatedTo`/`demoteRelatedTo` calls) and the WRITE/demote
  direction works correctly (probe-verified: a programmatically-set
  `relatedTo()` serializes to a correct `RELATED-TO` line on a `Journal`).
  But `KCalendarCore::ICalFormat`'s VJOURNAL **parser** never populates
  `relatedTo()` from a source `RELATED-TO` line — an upstream gap, verified
  against `ICalFormat` alone with VEVENT parsing the identical line
  correctly for comparison. New finding **O95** (full probe transcript in
  `docs/campaign/FINDINGS.md`). `canonToVjournalLoss()` correctly does NOT
  declare `relatedTo` (that function is scoped to the demote direction,
  which is not lossy here); the RFC 5545 fidelity gate
  (`tst_incidence_rfc5545_fidelity.cpp`) instead adds `RELATED-TO` to
  vjournal's expected-lost list by hand via a new `vjournalExpectedLostList()`
  helper, since the shape graph has no promote-direction loss-profile
  mechanism to derive it from (a structural gap, not this item's to build).

  **Also declared honestly:** `recurrenceRange` is `Degraded` (not
  Dropped) in `canonToVjournalLoss()` — the bare RECURRENCE-ID identity
  survives losslessly, only the RANGE modifier is never re-emitted (the
  W3 safety rule). New finding **O96**: the sibling `canonToVtodoIcalLoss()`
  demotes through the identical code but doesn't declare this same
  degradation — a pre-existing, low-severity declaration gap, logged not
  fixed (out of VJOURNAL's scope).

  **Catalogue**: `journalCanonContributedIds()` gained `organizer`,
  `attendees`, `attachments`, `relatedTo`, `recurrence`, `recurrenceId`,
  `recurrenceRange`, `descriptionHtml` — all already had `PropertyKind`/
  display-name metadata in `calendarPropertyMetadata()` (VEVENT/VTODO
  already declare them), so no new metadata entries were needed, only
  contributor-list membership, confirming PLAN.md's prediction.

  **Loss profile** (`canonToVjournalLoss()`): six entries removed
  (`attachments`, `attendees`, `organizer`, `relatedTo`, `recurrence`,
  `recurrenceId`), `recurrenceRange: Degraded` added, `requestStatus:
  Dropped` (permanent, upstream) kept. Matrix diff: exactly this — six
  `Dropped` rows removed from the `canon → ical (vjournal)` section, one
  `recurrenceRange | Degraded` row added. `tst_gm_pipeline_convergence`
  green, including `committedMatrixMatchesGenerated`.

  **IP.8's gate**: `vjournalRfc5545RoundTrip()`'s `QEXPECT_FAIL` block
  removed entirely — the real (non-XFAIL) gate now passes for real, with
  `vjournalExpectedLostList()` correctly listing only `RELATED-TO` (O95)
  and `REQUEST-STATUS` (permanent, upstream, unchanged) as lost.

  VEVENT/VTODO suites re-run before and after this item's changes — both
  unaffected (this item never edits `incidencecommonfields.{h,cpp}`,
  `eventcanonfields.cpp` or `vtodocanonfields.cpp`, confirmed by `git
  status` scope discipline; only calls functions already built there).

  Full suite: 215 tests (unchanged — new slots landed inside existing
  binaries, per IP.3/IP.6/IP.9's own precedent), 211 passed, 4 known-
  environmental failed, verified by failure TEXT not name.

  New findings **O95** (upstream, VJOURNAL RELATED-TO promote gap) and
  **O96** (recurrenceRange declaration gap on the sibling VTODO profile),
  neither owned by any item yet. Receipt:
  `2026-09-02-ip10-return-receipt.md`.

- **2026-09-02 — IP.4 DONE. O79, O85 RESOLVED.**

  New `src/calendar/alarmshape.{h,cpp}` — placement per PLAN.md's default
  (`src/calendar/`, not `src/shape/`: `KCalendarCore::Alarm` is a
  calendar-layer type and `src/shape/` is deliberately domain-neutral).
  `alarmToJson()`/`alarmFromJson()` moved **verbatim** from
  `vtodocanonfields.cpp`'s W5 block — the tested-correct implementation,
  not reimplemented. New `describeAlarmRow()` (`enum class AlarmRowForm
  { StartRelative, EndRelative, Absolute, Malformed }`) — a NEW helper, no
  prior copy — lets a vendor leg ask a row's actual form instead of
  inferring one from a possibly-defaulted key, which is precisely the O79
  bug class.

  **All four call sites moved in one commit** (fixing promote alone would
  have made VEVENT round-tripping worse, per PLAN.md's own reasoning):
  `eventcanonfields.cpp` promote/demote now call the shared module
  (gaining W5's REPEAT/DURATION pairing and `related` key, which VEVENT's
  own pre-IP.4 shape never had); `vtodocanonfields.cpp` now calls the same
  module instead of keeping its own copy (proof: `tests/todo/` VALARM
  slots re-run unchanged and green, before and after); `mseventcanonstages.cpp`'s
  demote site (~line 1211) now requires `describeAlarmRow(a) ==
  AlarmRowForm::StartRelative` before mapping to
  `isReminderOn`/`reminderMinutesBeforeStart`, routing Absolute/
  EndRelative/Malformed rows to the existing `x-canon-alarms` carrier
  instead of misreading a defaulted `offset:0`.

  **`googlecanonstages.cpp` investigated independently, per the task's
  explicit instruction not to assume PLAN.md's "same reader shape" premise
  — and it does NOT hold there.** Google's demote guard is `offsetSecs < 0`
  (strictly negative), not MS's `<= 0`; an "at"-shaped absolute row
  defaults `offsetSecs` to 0, `0 < 0` is false, so that case was **already
  correctly routed to the carrier before this item touched the file**.
  What WAS broken: an END-related row (`related:"end"`, still carrying a
  negative numeric `offset`) was never distinguished from a start-relative
  one and could be wrongly mapped to a Google `reminders.overrides[]`
  entry measured from the start. Fixed the same way, via
  `describeAlarmRow()`, requiring `StartRelative` before mapping.

  **O85 decision** (PLAN.md's own recommended option, adopted as-is):
  `alarmFromJson()` unconditionally calls `setEnabled(true)` for every
  row, on both kinds — no `"enabled"` row key was added. Argument: RFC
  5545 has no wire representation for a disabled alarm at all — demoting
  a disabled KOrganizer alarm through *any* calendar client's serializer
  (not just this library's) loses the same bit, since the format has no
  slot for it; `X-KDE-KCALCORE-ENABLED` was the only thing making the
  round trip inconsistently disabled, and it carries no cross-client
  meaning. Proof: new `veventAlarmEnabledSurvivesRoundTrip` /
  `vtodoAlarmEnabledSurvivesRoundTrip` slots pin an enabled source alarm
  survives promote→demote enabled on both legs.

  **New tests:** four VEVENT alarm round-trip slots in
  `tst_calendar_canon_roundtrip.cpp` (absolute, end-relative, repeat/
  duration, enabled — VEVENT's first-ever coverage of the non-start-
  relative forms); one VTODO enabled-survival slot in
  `tst_todo_canon_roundtrip.cpp`; two "carried not coerced" slots each in
  `tst_ms_event_canon_edge.cpp` and `tst_google_event_canon_edge.cpp`
  (absolute + end-related, both verifying the carrier round-trips the row
  back intact via re-promote). `tst_incidence_rfc5545_fidelity.cpp`'s
  VALARM sub-gate: all eight slots' `QEXPECT_FAIL`s (O79 form-corruption +
  O85 disabled) removed — real green now, not vacuous.

  **Loss profiles re-verified, left UNCHANGED.** `alarms: Simplified`
  stays the correct verdict on both `mseventcanonstages.cpp` and
  `googlecanonstages.cpp`: the one alarm that maps to the vendor's own
  native reminder field (MS `isReminderOn`+minutes; Google
  `reminders.overrides[]`) still loses its `text`/`repeatCount`/
  `repeatIntervalSecs` on that leg, because neither vendor's native
  reminder model carries them — that loss is independent of O79 (which was
  about *misclassifying* a row's form, not about the native mapping's own
  reduced fidelity) and was already the reason for the Simplified verdict
  before this item. Confirmed by matrix regeneration:
  `./build/tools/matrixgen/matrixgen` output byte-identical to the
  committed matrix; `tst_gm_pipeline_convergence`'s
  `committedMatrixMatchesGenerated` green.

  O64 crossing-gate check: IP.4 does not add a new vendor pair/domain
  edge (the `{calendar,canon}⇄{ms-event,google-event}` edges already
  existed), so O64 does not mandate new crossing-gate coverage; the
  dedicated per-vendor "carried not coerced" slots above give direct
  coverage of the fixed behavior. `tst_gm_pipeline_convergence.cpp`'s
  fixtures carry no alarms/reminders either before or after this item —
  left untouched, noted in the receipt as a pre-existing gap, not one this
  item's scope requires closing.

  VJOURNAL confirmed untouched — takes no VALARM per RFC 5545 §3.6.3;
  `journalcanonfields.cpp` was not touched.

  Full suite: **215 tests (unchanged — new slots landed inside existing
  binaries, per IP.3/IP.6/IP.9/IP.10's own precedent), 211 passed, 4
  known-environmental failed** (`tst_backend_signals`,
  `tst_backend_thread_relocation`, `tst_backend_reentrancy_pin`,
  `tst_remotecalendarbackend` — verified by failure TEXT: CalDAV 412/
  timeout against the local Radicale, not by name).

  No new FINDINGS filed — no bug found outside O79/O85's scope. Receipt:
  `2026-09-02-ip4-return-receipt.md`.

- **NEXT:** **IP.5** — `CanonEnvelope::stampProviderExtrasDigest()` across
  calendar/journal/contacts; retrofit the 3 todo sites. Closes O80.
