# Reconnaissance Assessment & Practical Roadmap (EEE) — 2026-08-24

**Status:** Adopted doctrine for all future EEE sessions. Read together with
`STATUS.md` (live state) and the campaign proposal (phase definitions).
This document records the post-Phase-3/6-pipeline assessment of what the
vendor reconnaissance has taught us, judges where we stand against the
campaign's ambition, and fixes the order of expeditions that follow.

---

## Part I — What the probes taught us (the strata record)

Each vendor API is a fossil record of its company's epistemology. We have
been reading strata, and the readings are now evidence-backed:

### Microsoft conceptualizes time as STRUCTURE

`patternedRecurrence` is a normalized object graph; exceptions are
materialized instances; zones are Windows registry names — civil time as
*corporate property* (hence the vendored CLDR map,
`windowszonesmap.h`, 139 zones). The tell that produced O61(a): sentinel
`range.endDate:"0001-01-01"` leaking through REST is `.NET`'s
`DateTime.MinValue`. The CLR pokes through the JSON. A type system's null
convention reached through the wire and amputated recurring series until we
honored the sentinel.

### Google conceptualizes time as TEXT and BEHAVIOR

Recurrence stays RFC5545 verbatim — Google kept iCal's soul. But Tasks
exposes no recurrence while the UI creates repeating tasks: repetition is a
*behavior*, not data. Due dates silently discard time-of-day. And Google
digitizes attention states nobody else has fields for (`focusTime`,
`workingLocation`) — attention itself as schedulable resource.

### Persons: testimony vs the single mailbox

Google People keeps provenance per row (`metadata.sourcePrimary`) — truth
as layered testimony. Exchange flattens to one scalar name with positional
email typing (`primaryEmailAddress`) — the single-mailbox MAPI message
model wearing a REST costume (beta §5.2 of the reference confirms: typed
collections are coming). Neither vendor sells you a *person*; both sell
records only. The identity layer (`src/identity/`) is therefore the one
artifact in our stack **neither titan offers** — and it took ~600 lines
because we refused Nepomuk's merge-the-world mistake again.

## Part II — Justice assessment

**Done and provable:** six bidirectional vendor⇄canon edges with loss
declared before code; byte-pinned round-trips; two green calendar live
checkpoints (O57/O61); the pipeline crossing gate
(`tst_gm_pipeline_convergence`) which immediately caught O64; the generated
convergence matrix (`docs/campaign/eee/CONVERGENCE-MATRIX.md`) — an honest
per-property loss ledger **no vendor will ever publish about itself**. That
ledger is the "extinguish" exhibit. Edge-level translation justice: done.

**Not yet just:** the engine-level hub has not been re-proven on
vendor-shaped records; the Phase-6 live checkpoint (capture→translate→
replay→compare) hasn't run; both todo edges were written against
documentation only — *the docs always lie somewhere*, and we haven't found
those lies yet. House history is unambiguous: live checkpoints caught
O25/O27/O42/O45/O61 — bugs every green suite missed.

## Part III — The roadmap

Two tiers: **owed gates** (finish the campaign's own exit criteria) and
**interior expeditions** (the mapped shoreline's interiors). Every interior
follows the same discipline, learned in Phase 0: **corpus first, loss
profile second, stage third, gate fourth, live checkpoint fifth.**

### Tier A — Owed gates (in order; completes EEE)

| # | Work | Notes |
|---|---|---|
| A1 | Engine-level vendor-shaped hub convergence | Stub G-shaped/MS-shaped backends vs one `GenericSqliteBackend` hub; fixpoint; O55/O56 aliasing/conflict machinery on vendor-shaped records. RED→GREEN suite. |
| A2 | Task-side corpus captures | Google Tasks list+tasks; Graph `/me/todo/lists`+tasks via the CLI sweep tools; sanitize → fixtures; promote into both todo edges' suites. Expect wire-lie discoveries. |
| A3 | Deferred carrier-survival drills | People clientData write-back; ms-contact/ms-todotask open-extension survival (O61(e) class); Graph calendar write-path drill via `tools/msroundtrip`. |
| A4 | Phase-6 live checkpoint | Capture from Google → translate to Graph shape → replay into Graph-backed store → return → compare vs canon; only declared losses may differ. |
| A5 | Tag the phase boundary | House convention; consumer pin bumps voluntary. |

### Tier B — Interior expeditions (post-A5 or interleaved when blocked)

Ordered by value-per-session; each gets its own mini-campaign page under
`docs/campaign/eee/` when opened.

| # | Interior | First act | Why |
|---|---|---|---|
| B1 | **Scheduling negotiation**: Graph `getSchedule`, VFREEBUSY, iTIP (RFC 5546), iMIP | Corpus capture + reference-doc section | How machines argue over time. Richest vein. Consumer-facing value gated on PlanStan organization features, but RECON is unblocked now. |
| B2 | **Visibility**: who-may-see-whose-time — Google Calendar ACLs vs Graph calendar permissions vs RFC 3744 privileges | Loss profile sketch: canon needs a sharing/ACL concern | We translate events, not visibility. Any real multi-calendar consumer hits this immediately. |
| B3 | **Resource calendars**: rooms/equipment — Graph `places`, Google Directory resources | Corpus capture | Corporate time isn't only humans; rooms are the second-most-synced entity. |
| B4 | **Taxonomy entities**: Outlook master categories (mailbox-wide, not per-item) | Canon design note: category-as-entity vs string-list | We flatten taxonomies into strings; the matrix shows it. An entity design earns fidelity everywhere. |
| B5 | **MAPI named properties deep probe**: PidLid*, userConfiguration | Targeted GraphCLI captures | The TRUE schema beneath Graph; categories/reminders ride MAPI under the hood. Informs every future MS edge. |
| B6 | **Beta horizon watch** (standing, zero-cost): `exceptionOccurrences`, `typedEmailAddress` GA | None until trigger | Already diffed (reference §5). When GA lands: spine-v2 widening, designed node, not surprise (invariant 4). |

### Doctrine additions (binding)

1. **Corpus-first per interior** — no edge code before sanitized captured
   payloads exist (Phase-0 lesson, restated).
2. **Crossing-gate coverage is mandatory** — any new vendor pair or domain
   edge must join `tst_gm_pipeline_convergence`; per-edge suites cannot see
   foreign-edge canon richness (O64 lesson).
3. **Matrix regeneration in the same commit** — growing/re-ruling any
   `edges()` list requires regenerating CONVERGENCE-MATRIX.md (O63 rule;
   byte-pin enforces).
4. **PATCH over re-create** wherever carriers matter (O61(e)) — until
   carrier survival is proven per channel, treat Reversible rulings as
   offline-only.

---

## Part IV — Ethics of the data model (adopted 2026-08-24, binding)

The person-object the titans already run (Google People aggregation,
Graph `person` scoring) is an *assertion* held in central custody. Ours
must remain a *hypothesis with receipts* held by its subject. The
distinction is architectural, not aspirational — each ruling below is
enforced by tests and must survive every future phase:

1. **Never a merge.** Entities LINK records; they never collapse them.
   There is no authoritative person record — only a join table whose
   links are individually dissolvable. The totalitarian operation is
   collapse-into-one-master-record; this invariant forbids it as code,
   not policy. (`tst_identity_links`, `tst_person_directory`.)
2. **The graph forgets.** Unlinking a record dissolves only its own link;
   when an entity's last record unlinks, its email evidence is pruned.
   Dead testimony must not resurrect dead entities.
3. **Strangers stay strangers.** Unresolvable roster entries return
   empty — never invented, never confabulated, no relevance scores, no
   behavioral inference. Graph's `person`/`profile` resources are
   quarantined as insight metadata, permanently outside authored canon.
4. **One rule, explicit.** The resolver grows only by deliberate,
   documented, tested rules (currently: shared email). No inference
   machinery, ever, without a new adopted doctrine page.
5. **Local custody, opt-in per host.** Identity state lives in a local
   SQLite file owned by the host application; nothing in the identity
   path performs network I/O; consumers adopt voluntarily.
6. **Loud about limits.** The convergence matrix publishes every declared
   loss and is machine-checked against regeneration. A model that
   confesses its blindness cannot credibly pose as omniscient.
7. **Seizure test.** Every component must survive the question: *could
   this survive being seized?* A merge-graph could not; a disjointed
   link-index plus a confession of unknowns can. When a design fails the
   seizure test, redesign it.

Rationale: interoperability is a defensive technology. A local, honest,
portable representation of one's own time and relations is what keeps
exit cost from growing inside platforms that triangulate their users.
Embrace-and-extend here means re-opening fences using the vendors' own
documented front doors.
