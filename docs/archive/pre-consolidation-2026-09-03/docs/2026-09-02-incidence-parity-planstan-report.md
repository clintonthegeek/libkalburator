# Incidence-parity audit — report to PlanStan

**From:** libkalburator, 2026-09-02, at `main` @ `40854f3`.
**Subject:** results of a deliberate audit of the VEVENT / VTODO / VJOURNAL
canon pipeline, and **two questions that block work here until you answer
them**.

**What you need to do with this:** answer **Q1** and **Q2** in §4. Nothing
else in this report asks anything of you — §2 and §3 are there so you can
judge the questions properly and so you know what will change under you.

---

## 1. Why this exists

The vtodo-parity campaign closed 2026-08-28 with W1–W8 delivered and return
receipts issued per item. The successor campaign (incidence-parity, opened
2026-08-29) then found, twice in a row, that it was *discovering* serious
defects while working on unrelated items rather than by looking for them.
That is a bad way to find data-corruption bugs, so we stopped and audited
the whole incidence surface once, deliberately, code-first.

The audit was run against the real tree with two probe programs, not by
reading our own documentation. Both are committed and re-runnable
(`docs/campaign/incidence-parity/probes/run.sh`), so nothing below has to
be taken on trust.

Full evidence: `docs/campaign/incidence-parity/2026-09-02-preflight-audit.md`.
Execution plan: `docs/campaign/incidence-parity/PLAN.md` §Amendment 1.

## 2. What we found

### 2.1 One root cause

`_canon.kind` — the field that decides whether a canon record becomes a
VEVENT, a VTODO or a VJOURNAL — is **written in one place and read in one
place** in the entire library. No catalogue, differ, merger, loss profile,
engine path or baseline store knows it exists.

Most of what follows is a symptom of that.

### 2.2 The defects, ranked by what they cost you

| # | Defect | Consequence for a PlanStan user |
|---|---|---|
| 1 | **A merged VTODO or VJOURNAL demotes as a VEVENT** (O84) | A user resolving a task conflict with "merge" turns their task into an event. Corrupts source, target **and** baseline in one operation. |
| 2 | **VJOURNAL drops `RECURRENCE-ID`** (O87) | A detached journal instance and its master become indistinguishable — two entries collapse into one. Identity corruption. |
| 3 | **VJOURNAL drops `RRULE` / `EXDATE`** (O87) | A recurring journal is silently flattened to a single entry. |
| 4 | **Every alarm round-trips back disabled** (O85) | Reminders stop firing after a sync, on events *and* tasks. Bounded: the marker is a KDE X-prop, so non-KDE clients still fire. |
| 5 | **VEVENT alarms lose absolute and END-relative triggers** (O79) | `TRIGGER;VALUE=DATE-TIME` and `TRIGGER;RELATED=END` both silently become "at start". The VTODO leg is already correct — W5 fixed it there. |
| 6 | **VTODO through the calendar path drops `ORGANIZER`, `ATTENDEE`, `SEQUENCE`, `CLASS`, `URL`, `COLOR`, `ATTACH`** (O83) | Assigned tasks lose their assignees. See Q1 — whether this hits a given task depends on where it is stored. |
| 7 | **`GEO` is written malformed** (O86) | We emit invalid iCal to servers. **Upstream** — reproduces in kcalendarcore 6.29.0 with none of our code involved. |
| 8 | **The loss warnings you receive are the wrong ones** (O88) | One event-shaped loss profile serves all three kinds, so demoting a task warns about `guestsCanModify` and says nothing about the `ATTENDEE` it just dropped. The convergence matrix inherits the same distortion. |

**None of the drops in rows 2, 3, 6 is declared in any loss profile.** By
the EEE doctrine's "loud about limits" clause, that is the part we consider
a breach of contract with you, independent of the individual bugs.

### 2.3 What we checked and found sound

So you know the blast radius is bounded:

- **Attendees round-trip correctly.** Two separate false positives during
  the audit said otherwise; both were probe artifacts, both are pinned.
- **The CalDAV kind-demux is not double-counting.** The calendar and todo
  views of a hybrid collection are disjoint by construction.
- **No new defect in `contacts`, `note`, `outline` or `blob`.**
- The four red slots in our suite remain the known environmental
  Radicale/KDAV ones, re-verified from their failure text.
- Suite: 214 slots, 210 green — **and green over every defect above.** That
  is the finding behind our remedy, not an excuse: our gates compare our
  catalogue to our emitter, and never compare our output to RFC 5545.

## 3. What we are doing about it, without asking you

Ten items, in a binding order, one agent each
(`PLAN.md` §A.2). Summarised:

1. **IP.8 — an RFC-5545 round-trip fidelity gate.** The missing
   measurement: maximal conformant component → canon → iCal → diff the
   property sets, per kind. Lands **red**, one expected-failure per defect
   above. This is the item that would have prevented the whole audit.
2. **IP.3** — catalogues generated from emitters, plus the O84 fix.
3. **IP.9** — kind-scoped loss profiles, so a VTODO's losses have somewhere
   truthful to be declared. **This is the one that changes what you read in
   the convergence matrix** — see §5.
4. **IP.4** — one shared VALARM implementation (closes O79 + O85).
5. **IP.5** — provider-extras visibility on calendar/contacts.
6. **IP.6** — shared common-incidence-field module; closes the VTODO drops
   and decides the GEO question.
7. **IP.10** — VJOURNAL parity, `RECURRENCE-ID` identity first.
8. **IP.7** — remaining VEVENT corrections. *(IP.7b blocked — Q2.)*
9. **IP.11** — VTODO representation unification. *(Blocked — Q1.)*
10. **IP.12** — demote reproducibility.

Return receipts per item, as during vtodo-parity.

---

## 4. The two questions

### Q1 — VTODO has two canonical representations. Which do you want?

**The finding (O89).** A VTODO reaches one of two canonical shapes, and
which one is decided by *where it is stored*, not by the data:

| Storage | Representation | What the task gets |
|---|---|---|
| CalDAV collection advertising `VTODO` in `supported-calendar-component-set` | `{todo,canon}` | The full vtodo-parity result: W1 composite exception identity, W2 per-instance completion, W3 series split, W4 completion anchors, O74 extras digest, and a thorough loss profile |
| A CalDAV server that does not advertise component types | `{calendar,canon}` | None of the above. Seven undeclared drops. O84. |
| **A local `.ics` file, Akonadi, DecSync, or an org file** | `{calendar,canon}` | Same — **always**. These backends never demux, under any configuration. |

So the same task, in a local file, silently loses its `ORGANIZER` and its
composite exception identity; on a well-advertising Radicale it keeps them.

**Why we are not choosing alone.** The two fixes differ in something you
can see:

- **(a) Converge** — bring `{calendar,canon}` VTODO to full parity so the
  two representations become equivalent and it stops mattering which one a
  task gets. **Invisible to you**: a task stays in whichever domain it is
  in today; it just stops losing data. Most of the work is already in
  IP.3/IP.6/IP.9; IP.11 becomes a proof item plus four residual vendor keys.
- **(b) Route** — send every VTODO to `{todo,canon}` regardless of
  transport, by giving the non-DAV backends a demux path. Eliminates the
  duplicate representation outright and is the cleaner end state. **Visible
  to you**: a task in a local `.ics` or in Akonadi would move from the
  `calendar` domain to the `todo` domain. If PlanStan enumerates,
  filters, maps or persists anything by domain id, that is a breaking
  change on your side.

**Our recommendation: (a) converge now, and treat (b) as a separate,
scheduled migration** if you want it. (a) fixes the data loss without
moving anything under you; (b) is the better architecture but should not
ride in on a bug fix.

**What we need from you:** (a) or (b) — and if (b), whether PlanStan reads
`{calendar,canon}` VTODOs anywhere today, so we can size the migration.
**If you have no strong view, say so and we will take (a);** that is the
safe default and we will not block on a non-answer.

### Q2 — What should a VEVENT do with a mismatched `DTSTART` / `DTEND`?

**The context.** W6.2 gave VTODO a coercion rule for a malformed
`DTSTART` / `DUE` pair (one `VALUE=DATE`, the other `DATE-TIME`). Our
receipt records that rule as a **deliberate divergence** from tasks.org's
symmetric rule, adopted because your handoff response specified it: DUE's
type wins.

VEVENT has no such rule today. A `VALUE=DATE` `DTSTART` with a `DATE-TIME`
`DTEND` promotes with a type-mismatched pair.

**Why we are not just mirroring W6.2.** `DTEND` is a bound *derived from*
`DTSTART`, which `DUE` is not — so the argument that made DUE win does not
transfer, and **DTSTART-wins is likely correct for events**. Mirroring
W6.2 blindly would be exactly the copy-a-fix-across-kinds reflex that
produced the defects in §2.

**What we need from you:** confirm **DTSTART-wins for VEVENT**, or tell us
the rule you want. This is the one point in the plan where a wrong default
silently corrupts user data, which is why we are asking rather than
guessing. We will write the answer up as a contract document before
implementing, in the form of
`docs/campaign/vtodo-parity/2026-08-28-w7-passthrough-contract.md`.

---

## 5. What will change under you, no action needed

Flagging these so they are not surprises:

- **The convergence matrix will change shape** (IP.9). It currently reports
  the calendar `ical` leg as though every record on it were a VEVENT. After
  IP.9 it reports per kind. If you parse or pin the matrix, expect a
  substantial diff — we will call it out explicitly in the IP.9 receipt.
- **You will start receiving loss warnings you have never seen**, because
  today the drops in §2.2 rows 2/3/6 are undeclared. This is new
  *information*, not new loss; the loss has been happening.
- **`geo` may disappear from the VTODO canon shape** (IP.6, O86), if we
  choose to drop it rather than hand-serialize around the upstream bug.
  Tell us if you consume it — it is currently corrupt on the wire either
  way, so we do not expect you do.
- **VJOURNAL will gain fields** (IP.10): `organizer`, `attendees`,
  `attachments`, `relatedTo`, `recurrence`, `recurrenceId`. Additive.
- **Alarm rows may gain an `enabled` key** (IP.4), or may not — see
  `PLAN.md` §A.3.1. Either way, pre-existing rows stay valid, as with W5.

## 6. Timing

IP.8 starts immediately and does not depend on you. **Q1 blocks item 9 of
10; Q2 blocks half of item 8.** Realistically that is a good deal of work
away, so there is no urgency — but an early answer to Q1 lets IP.3/IP.6
aim at the ratified end state instead of hedging.

Please reply in whatever form suits; we will file it in
`docs/campaign/incidence-parity/` and cite it from the items it unblocks.
