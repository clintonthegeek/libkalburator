# Campaign Proposal: Vendor Convergence (EEE) — Google ⇄ Microsoft Graph Through the Canon

**Date:** 2026-08-22
**Status:** Proposal for review. Not yet opened; no `feature/eee-*` branches exist.
**End goal (one sentence):** libkalburator translates losslessly-within-limits between Google's PIM formats and Microsoft Graph's PIM formats — each side degraded only as the vendor's own schema demands, both subordinate to our richer canon — and the canon becomes the primary local-first store that vendor formats merely cache.
**Inputs:** `docs/2026-08-22-canon-domains-and-cross-format-readiness.md` (the readiness assessment this campaign operationalizes), `docs/2026-05-23-vendor-api-shapes-reference.md` (field evidence), `docs/2026-05-23-canon-schema-design.md` (schema), `docs/campaign/architectural-redress/INVARIANTS.md` (standing rules).

---

## Status (2026-08-23, late): Phase 2 CLOSED incl. live checkpoint — tagged v1.02

Session progress (authoritative detail in `docs/campaign/eee/STATUS.md`):

- **Phase 2 (google-event ⇄ canon) DONE AND TAGGED v1.02** — loss profile
  declared first per invariant 2
  (`docs/2026-08-23-google-event-edge-loss-profile.md`), stages + registry
  edges + `tst_google_event_canon_edge` (8 slots, including promotion of a
  committed live-capture fixture). Wire truths corrected against the live
  Calendar API reference before trusting fixtures (FINDINGS **O59**).
  **Live checkpoint PASSED**: G→C→G diffs = 4, all declared
  normalizations; round-tripped body re-created on the real account; both
  server copies promote to identical canon.
- **Stage D (mock Graph server) done** — `tests/graph/`, CI-able, replaying
  pagination/delta/error semantics; ready as the 7.C test bed.
- **googlecli** (`tools/googlecli/`) — loopback-OAuth Google Calendar lab
  mirroring graphcli; credentials machine-local in gitignored `/google/`.
  Research input: `docs/google_rest.md`. Authorized; scopes verified at
  login (Google silently drops consent-screen-unapproved scopes — O59(f)).
- **Google golden corpus captured; sanitized fixtures committed**
  (`tests/fixtures/vendor/google/`, generator `tools/googlecli/make-fixtures.py`).
- 7.B ms-event loss profile DECLARED
  (`docs/2026-08-23-ms-event-edge-loss-profile.md`) — implementation next.
- corpus-sweep mints per-run tags (closes the O57 cross-contamination
  vector; verified live). O58 closed: the red canon slot was a test-string
  bug, not data loss. Suite baseline: **182 total / 180 passing**.

Earlier status (2026-08-23): Phase 0 experiment in flight — GraphCLI.

Phase 0 of this campaign has started early, via a live-account experiment
pinned here so future sessions don't have to rediscover it:

- **The tool:** `tools/graphcli/` (`graphcli`, target `KALBURATOR_BUILD_GRAPHCLI`,
  Qt6 Core+Network only, does not link the library). Device-code auth against
  a personal Outlook.com account through a fresh Entra app registration;
  verbs for read (`me`/`calendars`/`events`/`contacts`), write
  (`create`/`patch`/`delete`), corpus capture (`capture` → pretty JSON files),
  series inspection (`instances`, `calendarview`), incremental sync
  (`delta`, saves/resumes `msgraph/delta-link.txt`), and bulk cleanup
  (`sweep-clean`). Scenario-matrix driver:
  **`tools/graphcli/corpus-sweep.sh`** (`list` shows scenarios).
- **The folder:** gitignored `msgraph/` holds everything machine-local —
  `GraphCLIinfo.md` (app-registration credentials; never commit),
  `token-cache.json` (0600; auto-refreshing), `delta-link.txt`,
  `captured/*.json` (**the Phase-0 golden corpus**, raw real payloads),
  plus two planning inputs: `general_plan.md` (the auth/architecture
  research conversation that scoped this experiment) and
  `graphCLIuse.txt` (first-session console transcript). Nothing in this
  folder is tracked; only the tool and sanitized fixture extracts will be.
- **First findings:** FINDINGS **O57** — seven documented-vs-real deltas
  from the first payloads, including: Graph events now carry a top-level
  `uid` (= `iCalUId`) usable as an aliasing anchor; split-brain zone
  vocabulary per event; zero-sentinel recurrence numerics; and default
  event listing returns series MASTERS ONLY (exceptions need
  `calendarview`/`instances` — a hard requirement for any Graph backend).
- **Verified live so far:** login/token-refresh cycle, list+capture,
  delta walk, scenario create→capture→calendarview→instances→delete cycle
  (all-day and authored-in-Eastern events confirmed O57(b) on client-
  authored data).

This accelerates Phases 0→4 (corpus is accumulating now) and gives Phase 7
a running start — see the reworked Phase 7 below.

---

## 0. The end state — definition of victory

The campaign is done when all of the following hold:

1. **Translation proven:** for every property in `calendar+canon`, `contacts+canon`, and `todo+canon`, a published convergence matrix states what happens crossing `google ⇄ canon ⇄ ms-graph` in each direction, and the *actual* loss matches the *declared* `LossProfile` — verified by fixture tests over a committed corpus of captured real vendor payloads.
2. **Convergence proven:** a sync-engine-level test runs Google-shaped and Graph-shaped stub backends against one canon hub and reaches fixpoint (L2 re-prime) with id aliasing and conflict holds intact — the O55/O56 machinery working on vendor-shaped records.
3. **The matrix published:** G→M and M→G per-property tables (the "each somewhat degraded from our superior canon" ledger) shipped as docs, generated from the composed `LossProfile`s, not hand-written.
4. **A canon-native consumer exists** (scenario A from the readiness doc) exercising superset fields end-to-end through `GenericSqliteBackend` — the local-first proof.
5. **The identity layer exists** (§5 below): the first "people-object"-style link across domains, without breaking the no-cross-domain-edge invariant.

Transport (real Google/Graph API clients with OAuth) is **explicitly out of campaign scope** — see §4 Phase 7. Translation correctness is provable at the edge level; transport is a separate, follow-on campaign with its own risk profile (OAuth, quotas, API churn). This mirrors the house lesson that stub-backend coverage plus one live checkpoint catches what matters without coupling correctness to network flakiness.

---

## 1. Strategy — how EEE maps onto the architecture

- **Embrace (already done, v0.85–v1.01):** the canon catalogues are the union superset of Google Calendar/People/Tasks and Microsoft Graph event/contact/todoTask, field-by-field justified in the vendor-shapes reference. We accepted their vocabulary.
- **Extend (this campaign, first half):** the canon is already *richer than either vendor* — it holds what neither can (three coexisting task hierarchies, multi-location + conferenceData together, reversible X-prop carriers). This campaign makes that real by building the edges that prove it, and the matrix that advertises it.
- **Extinguish (this campaign, second half + horizon):** once both vendor edges exist, *we* are the interop layer. Any consumer that stores canon can join ecosystems neither vendor bridges natively. The endgame is consumers treating canon as the primary store and vendor payloads as demote targets — the local-first reclamation. The identity layer (§5) is the first structure that exists *only* in our canon and in no vendor API.

The strategic asset is the four-kind loss model. Every vendor's format is honest about what it destroys *because our edges are forced to declare it*. No vendor can say that about its own out-bound conversions.

---

## 2. Nepomuk post-mortem → campaign constraints

The user's framing names the graveyard this campaign must avoid. Each historical failure mode gets a binding countermeasure:

| Nepomuk failure | Countermeasure (binding) |
|---|---|
| Inadequate hardware for the semantic database (Virtuoso) | **No new storage engine.** Canon already persists in SQLite (`BaselineStore` v8, `GenericSqliteBackend`). The identity layer (§5) is a table, not a daemon. |
| Lack of developer uptake | **Library-embedded, not app-facing.** Two real consumers (PlanStan, WildPalms) adopt by pin bump only — every phase is additive, zero consumer code change required until they *want* the new edges. |
| Unfamiliar paradigm for users and developers | **iCal/vCard stay first-class peers forever.** Nothing about existing CalDAV/CardDAV sync changes. The vendor edges are new spokes on the existing hub — the paradigm (shape graph, loss profiles) is already shipped and tested. |
| Ambition outran incremental value | **Every phase shippable and independently valuable.** Google edge alone gives Google-fidelity to CalDAV consumers; Graph edge alone likewise. The G⇄M payoff needs both, but neither phase is dead weight if the campaign stalls. |
| Data/link orientation vs filesystem orientation never landed | The link layer (§5) is deliberately minimal: one registry, one use case (contact↔attendee resolution), proven before generalization. |

---

## 3. Campaign invariants (extends `docs/campaign/` INVARIANTS; does not replace them)

1. **No third mechanism.** Vendor support enters as `ShapeContribution` edges to the existing canon hub. No parallel conversion path, ever (standing INVARIANTS §1).
2. **Loss profile first.** An edge's `LossProfile` is written and reviewed *before* the stage code. The matrix (§0.3) is generated from these declarations; stage code is then forced to either honor them or trigger a declaration change through review. A silent divergence between declared and actual loss is a RED test.
3. **Additive-only for consumers.** New domains/edges/stages only; schema-version bumps only if a storage change is unavoidable (consumers pin-bump, no code change — the v0.98→v1.01 pattern). PlanStan and WildPalms must stay green on every tag.
4. **GA surfaces only.** Graph `beta` fields (e.g. `profile`, typed emails) are excluded; they are the documented v2 spine triggers, not campaign scope.
5. **Recurrence stays opaque in canon.** The RFC5545⇄`patternedRecurrence` parser lives inside the Microsoft stages (schema-design §1.4). It may be a shared internal utility, but it never becomes a canon-side parse.
6. **Live checkpoint discipline.** Each vendor edge gets one live verification against a real account before its phase closes (the O54/O55 lesson: green stubs missed real-world failures twice; checkpoints caught O25, O27).
7. **Findings discipline.** New issues take the next O-number in `docs/campaign/FINDINGS.md`; `STATUS.md` updated in the same commit as plan state.

---

## 4. Phase plan

Gates are RED→GREEN test slots in the named suites, per house convention. Each phase ends tagged and suite-verified against the standing baseline (180/177 at proposal time; Phase 0 fixes the one red canon slot, moving the baseline).

### Phase 0 — Hygiene and corpus (small, ~1 session)
- Fix `tst_calendar_canon_roundtrip::canonPersonalClassificationProducesPrivateAndStash` (the uncatalogued red slot; log it in FINDINGS first).
- **Build the golden payload corpus:** capture real Google Calendar/People/Tasks and Graph event/contact/todoTask JSON payloads (from live accounts, sanitized) into `tests/fixtures/vendor/`. Every later phase tests against these, not hand-invented JSON. This is the single highest-leverage act of the campaign — the vendor docs always lie somewhere, and captured payloads are how we find where.
- Decide and record: API surface versions pinned (Graph v1.0, Calendar v3, People v1, Tasks v1).

### Phase 1 — Canon-native consumer (scenario A; no new edges)
- A test-suite consumer speaking canon JSON through `GenericSqliteBackend`, populating superset fields (`locations[]`, `checklistItems`, `significantDates`, `onlineMeeting`, …) and running full engine sync: id aliasing, conflict holds, L2 fixpoint.
- **Gate:** RED→GREEN in a new `tst_engine_canon_native_consumer`. Proves the engine machinery on records richer than iCal/vCard can carry — before any vendor edge exists to muddy attribution.

### Phase 2 — Google Calendar events edge (`google-event ⇄ calendar/canon`)
- Friendliest first: recurrence is already RFC5545 text (no parser needed), zones already IANA.
- `LossProfile` written first from the reference doc §1.1 table; stage code second; corpus tests third; **one live checkpoint** against a real Google account via manual payload capture/replay.
- Deliverable: `GoogleCalendarStockShapes` + stages + generated loss docs. Also the template every later edge copies.

### Phase 3 — Google People ⇄ contacts/canon
- Near-1:1 with the typed-multi-value canon. This phase *settles* the open contacts-uid question (schema-design §7.3): mint/normalize uids against real People payloads where `metadata.source` varies.
- Google Tasks ⇄ `todo/canon` rides along if cheap (tiny schema; note: no API recurrence, date-only due → the precision flag earns its keep here).

### Phase 4 — Microsoft Graph event ⇄ calendar/canon (the hard one)
- Builds the campaign's one deep component: the **RFC5545 ⇄ `patternedRecurrence` converter**, inside the Graph stage (invariant 5). Reference doc §1.3 is the spec; the cannot-represent list (sub-daily FREQ, BYWEEKNO, general BYSETPOS, RDATE, multi-RULE…) becomes explicit `Simplified`/`Dropped` declarations with `X-` carriers where reversible.
- Windows zone vocabulary: vendor the CLDR `windowsZones.xml` mapping (many-to-one → `Degraded`, original IANA kept verbatim in canon).
- `dateTimeTimeZone`, `responseStatus`/PARTSTAT, `cancelledOccurrences`⇄EXDATE materialization.

### Phase 5 — Microsoft Graph contact + todoTask ⇄ canon
- Mostly mechanical after Phase 4; the interesting bits are Graph's positional email typing (v1.0 has no labels), fixed address slots, and `checklistItems`/`linkedResources` finally meeting their designed-in canon fields.

### Phase 6 — The payoff: G⇄M convergence (the campaign's namesake)
- Pipeline-level: `google-event → canon → ms-event` and reverse, composed-loss asserted equal to the product of the declared profiles; same for contacts and todos.
- Engine-level: Google-shaped and Graph-shaped stub backends against one hub; fixpoint convergence; the O55/O56 aliasing/conflict machinery verified on vendor-shaped records.
- **Deliverable: the generated convergence matrix** (docs + test-enforced). This artifact *is* the "extinguish" exhibit: a public, per-property ledger of where Google loses to canon and where Graph loses to canon, and exactly what survives crossing between them.
- **Live checkpoint:** one real round-trip — capture from Google, translate to Graph shape, write into a Graph-backed store (or replay-captured), return, compare against canon with only declared losses differing.

### Phase 7 — Transport and the Microsoft Graph backend

Transport (real Google/Graph API clients with OAuth) is out of the early
phases' scope so translation correctness is never gated on network plumbing.
The corpus + replay harness from Phase 0/6 is this phase's starting test bed.

**Consumer alignment note (2026-08-23):** PlanStan currently has **zero
event-organization affordances** — events are solo affairs, no attendees UI,
no iTIP. The Phase 7 MVP therefore targets exactly that model: attendees/
organizer map read-only into canon (preserved for later), RSVP and
counter-proposal flows are explicitly out of scope until PlanStan ships
organization. This is not a limitation to apologize for — it deletes the
hardest O57(t)/(s) concerns from the critical path and defers them to the
identity layer phase where they belong.

#### 7.a Work breakdown (pinned 2026-08-23; calendar-only MVP ≈ 7–10 focused sessions)

| # | Deliverable | Est. | Notes |
|---|---|---|---|
| **A** | Library-ize auth + HTTP | ~1 session | Port `GraphAuthentication` (device-code, token cache w/ refresh rotation, multi-account profiles) + async `GraphClient` (QNetworkAccessManager event-driven per E5 contract; GraphCLI's blocking loop is reference only). Base-URL injection stays (mock server). |
| **B** | `ms-event ⇄ calendar/canon` stages | ~2–3 sessions | **Loss profile written first** (invariant 2). Promote: nested JSON → canon properties (near-mechanical). Demote: the campaign's one deep component — RFC5545 ⇄ `patternedRecurrence` converter (reference §1.3; cannot-represent list → declared Simplified/Dropped w/ X-carriers) + vendored CLDR Windows-zone map (O57(b)). Stage-local unit suite before edge integration. |
| **C** | `MSGraphCalendarBackend` | ~2–3 sessions | `SyncBackendBase` subclass. Fetch: delta queries (deltaLink persistence, 410-expiry fallback), series expansion via calendarView/instances → master+override records keyed on `uid` (series-stable per reference §5.4); `seriesMasterId`+`type` for grouping; O57 zero-sentinel/null normalization. Write: POST/PATCH/DELETE with changeKey concurrency; id aliasing feeds O55/O56 machinery. |
| **D** | Mock Graph server test bed | ~1 session (parallel w/ C) | Replay corpus captures with pagination/etag/deltaLink semantics; CI-able; the "deliberately annoying" server from msgraph/general_plan.md. |
| **E** | Provider + config + plugin contribution | ~1–2 sessions | `IProvider` impl, device-code consent UX, secure token storage, `BackendContribution` registration — patterns copied from CalDAV/CardDAV pair. |

Then PlanStan-side (their repo): pin bump, provider registration, mapping
wiring (~1–2 sessions; they have done this for CalDAV).

#### 7.b MVP scoping decisions

- **Domain:** calendar only. Contacts rides cheap afterward (~1–2 sessions;
  beta-typed schema maps near-1:1 — reference §5.2). Todo last.
- **Recurrence:** v1 writes flat events and recurring masters; **exceptions
  expand read-only** (override records surface into canon but attendee-side
  edits don't write back as instance PATCHes yet). Write-back of overrides =
  v2 pass. This cuts B and C meaningfully and matches PlanStan's solo-event
  present tense.
- **Attendees:** promoted/demoted faithfully at the stage level (canon holds
  them), ignored by PlanStan UI. No RSVP endpoints called by anyone until
  PlanStan ships organization features.
- **Live checkpoints:** house discipline applies — stub-backed green first
  (mock server), then ONE live outlook.com account checkpoint before any
  consumer sees it (the O25/O27 lesson).

Google-side transport equivalents follow the same layering once Phases 2–3
edges exist.

### Phase 8 — Horizon (not in this campaign's exit criteria)
- People-canon v2 on Graph `profile` GA (the standing spine trigger); per-element composite diffing if conflict volume demands; cross-domain edges if a consumer ever proves a need.

---

## 5. The identity layer — the "people object," done survivably

Nepomuk's ambition — a person transcending apps and protocols — is the right *destination* and the wrong *first step*. The shape graph's "no cross-domain in v1" rule exists because conversion across domains is semantically fraught. Identity is **not conversion**: it is a link index above domains, and it enters without touching the invariant.

- **Shape:** a new `identity` concern — a `DomainDefinition`-free registry table (`GenericSqliteBackend`-persisted) mapping `(domain, record-uid) → entity-id`, where entity-id is a canon-minted stable id.
- **First resolver, one rule:** contacts' `emails[].value` ↔ calendar `attendees[].email` ↔ `organizer.email`. When a contact and an event participant share an email, they link to one entity. That single rule delivers the Nepomuk moment — "this meeting's people are these contacts" — with zero inference machinery.
- **Never a merge:** entities link records; they never collapse them. Deleting a contact leaves the event's attendee untouched; the link dissolves.
- **Phase placement:** after Phase 3 (real contact uids exist), before Phase 6 (so the convergence demo can show a person, not just records). Gated by its own RED→GREEN suite; additive; opt-in per host.

---

## 6. Risk register

| Risk | Likelihood | Mitigation |
|---|---|---|
| Vendor payload realities contradict reference doc (stale fields, output-only surprises) | High, per-vendor | Phase 0 corpus first; loss-profile review against real payloads, not docs |
| Recurrence converter complexity explodes (Phase 4) | Medium | Reference §1.3 cannot-represent list pre-declares the loss surface; parser is stage-local with its own unit suite before edge integration |
| Windows↔IANA zone ambiguity (many-to-one) | Certain | Declared `Degraded` up front; IANA verbatim in canon; CLDR table vendored with version stamp |
| Graph id instability (folder-move rekeying) | Medium | `providerExtras` carries `changeKey`/immutable-id per design; O55 aliasing machinery already solves id churn at the engine level |
| Live-account access (OAuth for capture) | Medium | Capture is manual/one-time per phase, replayable offline; campaign never requires CI network access |
| API churn mid-campaign | Low-Medium | Surfaces pinned in Phase 0; corpus fixtures double as regression pins |
| Campaign sprawl (the Nepomuk trap) | Medium | Phase gates are shippable tags; Phase 7/8 explicitly out of scope; §2 countermeasures binding |

---

## 7. Consumer coordination impact

Per invariant 3: **zero required changes** for PlanStan or WildPalms at every phase boundary. All work is new edges/domains/stages behind the existing plugin contract; storage schema unchanged (identity layer gets its own tables, additive). Pin bumps are voluntary per consumer. The coordination page (`docs/2026-07-19-consumer-coordination-status.md`) gets a §-entry at campaign open and at each tag.

---

## 8. Session protocol

Reuse the standing campaign discipline (sync-excellence §0): strong-model checkpoints per phase — the two prior campaigns' live checkpoints each caught a green-suite-missing blocking bug (O25, O27), and CP-C caught two more (O42, O45). FINDINGS numbering continues. `STATUS.md` (a new `docs/campaign/eee/STATUS.md`) updated in the same commit as plan state, per the phase-status-docs rule.

---

## 9. Why this wins where Nepomuk lost, in one line each

- No new infrastructure: SQLite we already have.
- No uptake problem: two consumers, additive pins.
- No paradigm break: CalDAV/CardDAV keep working identically; vendor edges are new spokes, not a new wheel.
- Incremental value at every gate: Google fidelity alone, Graph fidelity alone, G⇄M bridging alone, identity alone — each is a releasable advantage.
- And the strategic close: the convergence matrix is an artifact **no vendor will ever publish about itself**. Whoever holds the honest ledger of everyone's losses holds the interop layer. That is embrace, extend, extinguish — with the receipts.
