# EEE Campaign STATUS

Vendor-convergence (EEE) campaign per
`docs/2026-08-22-campaign-proposal-vendor-convergence-eee.md`. This file is
updated in the same commit as plan state (phase-status-docs rule).

**Doctrine + roadmap:** `2026-08-24-reconnaissance-assessment-and-roadmap.md`
(adopted 2026-08-24) — the strata assessment, justice verdict, and the
Tier-A/Tier-B expedition order. Future sessions: read this STATUS for live
state, that roadmap for direction.

**Last updated:** 2026-08-24 (Tier A1 landed; A2–A5 remain)

## Current phase snapshot

| Work item | State | Notes |
|---|---|---|
| Phase 0 — corpus + hygiene | **largely done** | Google corpus captured 2026-08-23 (32 events incl. a birthday-type series + 250-instance monthly series, 72 People connections, 9 groups, nextSyncToken walk); sanitized extracts committed under `tests/fixtures/vendor/google/` (generator: `tools/googlecli/make-fixtures.py`). Graph corpus sanitized 2026-08-23: five fixtures committed under `tests/fixtures/vendor/microsoft/` (generator `tools/graphcli/make-fixtures.py`); raw captures stay machine-local. |
| corpus-sweep per-run tags | done | `CORPUS:<runid>:` subjects + `sweep-clean [tag]`; verified live 2026-08-23. Closes the cross-contamination vector from the O57 addenda. |
| Phase 2 — google-event ⇄ canon edge | **done incl. live checkpoint** | Loss profile declared first; stages + registration + `tst_google_event_canon_edge` (8 slots incl. committed-fixture promotion). Wire truths corrected against the live API reference pre-trust (O59). Live checkpoint PASSED 2026-08-23: G→C→G diffs = 4, all declared normalizations (timestamp ms-truncation ×2, offset-form canonicalization ×2); round-tripped body re-created on the real account; both server copies promote to IDENTICAL canon modulo identity fields. Tagged v1.02. |
| Stage D — mock Graph server | **done** | `tests/graph/mockgraphserver.*` + `tst_mock_graph_server` (6 slots): $top/$skip+nextLink pagination, /delta walk+replay+fixpoint+410 ResyncRequired, exact routes, 404 shape, request recording. Ready as the test bed for 7.C `MSGraphCalendarBackend`. |
| Graph fixtures | **done** | Sanitizer + 5 committed extracts + live-fixture promotion slot (see Next actions #2). |
| Phase 7.B — ms-event ⇄ canon edge | **done incl. live checkpoint** | Converter suite FIRST (`tst_recurrence_pattern_converter`, 31 slots: every §1.3 row both directions, every cannot-represent ruling, O57(e)/(f) sentinel handling, carried-set re-promote identity, representable-set convergence). Then stages `mseventcanonstages.{h,cpp}` + catalogue + registration (now 9 edges) + `tst_ms_event_canon_edge` (10 slots): captured-shaped promote (O57 realities), declared-loss demote walk, C→G→C byte-equal identity incl. unrepresentable-rule carrier path, registry inspection, Windows-zone split-brain (O57(b)) via vendored CLDR map (`windowszonesmap.h`, 139 zones), floating pin+carrier, exception⇒recurrenceId keying. Declared-vs-actual divergence = none found. **Live checkpoint PASSED 2026-08-23** (delegated run): caught one BLOCKING bug the stub suite missed — sentinel `range.endDate:"0001-01-01"` on numbered ranges honored as real UNTIL ⇒ series amputation (fixed; O61(a)) — plus three stash/passthrough defects (O61(b)-(d), fixed). Demoted bodies accepted by the server; server copies promote to identical canon modulo per-copy identity. Standing discovery: carriers do NOT survive creates on consumer Outlook.com (O61(e)) — Reversible class is offline-only; backends must prefer PATCH over re-create. |
| Phase 7.C polish — persistence + per-calendar paths | **done (Stage-D verified)** | `setCacheDir()`: delta tokens + merged record caches persist as atomic-replace JSON; a restarted backend presents the PERSISTED token on its first request (no re-listing) and merges changes into the restored cache. Per-calendar event paths: discovered calendars get `/me/calendars/{id}/events` for both reads (delta walks) and writes (POST/PATCH/DELETE). Suite: `tst_ms_graph_calendar_backend` 11 slots. Baseline 186/184. |
| Phase 3 — google-person ⇄ contacts-canon edge | **done (stub-level)** | Loss profile declared first (`docs/2026-08-23-google-person-edge-loss-profile.md`); `googlepersoncanonstages.{h,cpp}` + catalogue, registered in ContactsStockShapes. Carriers ride Google People `clientData` rows (the resource's only extension point) under the x-canon-* key discipline. uid ⇄ resourceName (per-account anchor). Gated by `tst_google_person_canon_edge` (7 slots incl. all-72-connections fixture promotion). Live checkpoint deferred until googlecli grows people write verbs. |
| Phase 3 — ms-contact ⇄ contacts-canon edge | **done (stub-level)** | Loss profile declared first (`docs/2026-08-23-ms-contact-edge-loss-profile.md`); `mscontactcanonstages.{h,cpp}` + catalogue; ContactsStockShapes now **9 edges**. Flat Graph name collapses onto names[0]; fixed phone/address buckets typed on promote; positional primaryEmailAddress ⇒ primary flag; birthday rides verbatim {dateTime} stash form; carriers ride `kalburator.canon` open extensions (survival UNVERIFIED — O61(e) class). uid ⇄ id, no duplicate extras copy (byte-equal C→MS→C). Gated by `tst_ms_contact_canon_edge` (7 slots incl. contacts-listing.json fixture promotion). |
| Phase 3 — google-task ⇄ todo-canon edge | **done (stub-level)** | Loss profile declared first (`docs/2026-08-23-google-task-edge-loss-profile.md`); `googletaskcanonstages.{h,cpp}` + catalogue. **NO carrier channel exists** on the Tasks resource — unrepresented canon props (priority/recurrence/percentComplete/…) declared Dropped honestly. due Degraded to date-part; status collapses to needsAction/completed; parent+position ⇄ parentUid/sortOrder. Gated by `tst_google_task_canon_edge` (6 slots). Fixture-promotion slot DEFERRED until a tasks corpus capture lands. |
| Phase 3 — ms-todotask ⇄ todo-canon edge | **done (stub-level)** | Loss profile declared first (`docs/2026-08-23-ms-todotask-edge-loss-profile.md`); `mstodotaskcanonstages.{h,cpp}` + catalogue; TodoStockShapes now **9 edges**. Recurrence reuses the 7.B converter: MS→RFC5545 lossless promote; cannot-represent rulings + EXDATEs ride the `kalburator.canon` open-extension carrier (byte-equal re-promote). importance⇄priority via {low:9,normal:5,high:1} table; body contentType splits description/descriptionHtml; completed zone dropped to UTC form (declared Simplified); checklistItems/linkedResources = transport (separate endpoints), out of edge scope. Gated by `tst_ms_todotask_canon_edge` (7 slots incl. unrepresentable-RRULE carry drill). Fixture-promotion slot DEFERRED until a /me/todo corpus capture lands. |
| Identity layer (proposal §5) | **done** | `src/identity/{identitystore,identityresolver}`: SQLite registry `(domain, record-uid) → entity-id` (BaselineStore template, schema v2 — v2 adds the display-name projection column). First resolver, one rule: contacts `emails[].value` ↔ calendar `organizer.email`/`attendees[].email` share an entity; deterministic sorted-email adoption; NEVER a merge. Unlink dissolves only the own link; last-unlink prunes email evidence so dead entities don't resurrect. Records earn entities even with no emails (people exist without addresses). Gated RED→GREEN by `tst_identity_links` (10 slots incl. key extraction against real google-person/ms-event/google-event promote output). Additive; opt-in per host. |
| PersonDirectory (§5 payoff) | **done** | `src/identity/persondirectory.h`: composes edges + canon + identity into the Nepomuk moment — `observe()` ingests any canon record (both vendors' committed fixtures bulk-observed in one store); `eventRoster()` answers "who is in this meeting?" resolving attendee emails to named persons ACROSS vendors; unresolved emails stay strangers, never invented. Gated by `tst_person_directory` (7 slots incl. the cross-vendor single-human proof + both-fixture bulk ingestion). This is the Phase-6 demo artifact. |
| Tier A1 — engine-level vendor-shaped hub convergence | **done** | `tst_engine_vendor_shaped_hub` (6 slots, first engine test to mix two vendor encodings): a google-event wire record crosses a canon-shaped GenericSqliteBackend hub into ms-event within ONE Queue-mode runSync — O55 aliases persist sink-anchored (`{hub-prefixed → source-id}`), L2 re-prime carries the cross-mapping create, steady-state run moves zero records; canonically-equal vendor twins (built by demoting ONE minimal canon through both stages) are REFUSED loudly on BOTH mappings; O56 unresolved-conflict holds ALL writes on vendor-shaped records; closing slot feeds the converged hub into PersonDirectory — the roster of a synced event resolves to named persons ingested from both vendors. Gate catch: FINDINGS O65. |
| Part IV doctrine pins | **done** | `tst_doctrine_pins` (7 slots) makes the ethics falsifiable: name-similarity must NEVER merge (rule 1, anti-"smart matching"); forgetting verified at BYTE level via raw sweep of both tables post-unlink (rule 2); 100-stranger bulk roster stays unresolved with nothing invented (rule 3); only email evidence bridges records (rule 4 surface pinned); schema user_version pinned at v2 (rule 7, storage half). |
| Phase 6 — pipeline convergence gate | **done** | `tst_gm_pipeline_convergence`: for every vendor pair + direction (calendar/contacts/todo), canon promoted losslessly from a vendor-A wire crosses vendor B (demote→re-promote); EVERY differing top-level canon property must be declared in B's demote LossProfile — undeclared divergence = RED. First run caught O64 (google-person email displayName drop — fixed in stage, not declared away). All crossings now within declared unions. |
| Phase 6 — convergence matrix | **done** | GENERATED ledger committed at `docs/campaign/eee/CONVERGENCE-MATRIX.md`; generator = `ConvergenceMatrix::generate()` (`src/shape/convergencematrix.h`) + `tools/matrixgen` CLI; byte-enforced by `committedMatrixMatchesGenerated` (O63 discipline applied to the ledger — growing an edges() list without regenerating is RED). |
| Phase 6 — engine-level vendor-shaped hub | not started | Google-shaped and Graph-shaped stub backends against one GenericSqliteBackend hub; fixpoint convergence; O55/O56 aliasing/conflict machinery on vendor-shaped records. |
| Phase 6 — live checkpoint | not started | One real round-trip: capture from Google, translate to Graph shape, replay into a Graph-backed store, return, compare vs canon with only declared losses differing. |


| Phase 7.C — `MSGraphCalendarBackend` delta + discovery | **done (Stage-D verified)** | fetchItems now drives /delta walks: initial walk seeds a merged cache + resume token; later walks upsert changes (@removed ⇒ evict) and report the FULL merged collection (E7 sync-collection semantics); 410 ResyncRequired self-heals via one fresh initial walk (O42 pattern). Discovery: /me/calendars → calendarDiscovered + availableCollections/discoveredCalendar DTOs (VEVENT-only; Graph tasks live in /me/todo/lists). Mock server grew invalidateDeltaTokens() for the expiry drill. Suite: `tst_ms_graph_calendar_backend` 9 slots. |
| Phase 7.C — `MSGraphCalendarBackend` v1 | **done (Stage-D verified)** | `src/calendar/msgraphcalendarbackend.{h,cpp}` + `tst_ms_graph_calendar_backend` (6 slots): records carry RAW ms-event wire JSON (`nativeShapes={calendar,ms-event}`) so the engine promotes via the registered 7.B edge — no backend-side conversion on the unified path; Incidence legacy surface converts ms-event→canon→iCal inside the backend. Writes POST/PATCH/DELETE sequentially-async; creates mint Graph ids bridged via WriteOperation::idAliases (O55); updates are PATCH-in-place per O61(e). Design decision RESOLVED: pipeline-inside-backend for the legacy surface only; the engine boundary stays record-native. |
| Phase 7.C foundation — `GraphApiClient` | **done** | `src/graph/graphapiclient.{h,cpp}` + `tst_graph_api_client` (8 slots vs Stage D mock): multi-page collection walks ($top honored, order-stable), Bearer injection, delta initial/replay/fixpoint steps with typed 410 ResyncRequired surfacing, error.code extraction (O57(j)). Wire nuance pinned: a NON-EMPTY queued change page answers nextLink — the delta fixpoint is "empty change set + deltaLink", so walkers must step until complete, not until a seen token. Backend integration (fetch/applyRecords against these primitives) is the remaining 7.C work. |
| Phase 7.E | not started | — |

## Next actions (ordered)

Per the adopted roadmap (Tier A — owed gates; rationale + Tier-B interiors
in `2026-08-24-reconnaissance-assessment-and-roadmap.md`):

1. ~~**A1** Engine-level vendor-shaped hub convergence~~ DONE 2026-08-24
   (`tst_engine_vendor_shaped_hub`, FINDINGS O65 caught+fixed).
2. **A2** Task-side corpus captures — RUNBOOK READY:
   `2026-08-24-live-session-runbook-a2-a3-a4.md` (scopes widened this
   session: graphcli +Tasks.ReadWrite, googlecli +auth/tasks; re-consent
   required). Covers A2 captures, the four A3 carrier-survival drills,
   and the A4 roundtrip invocation. (Google Tasks list+tasks; Graph
   /me/todo/lists+tasks via the CLI sweep tools) → sanitize → fixture
   promotion slots for both todo edges. Expect wire-lie discoveries.
3. **A3** Carrier-survival drills: People clientData write-back;
   ms-contact/ms-todotask open-extension survival (O61(e) class); Graph
   calendar write-path drill via msroundtrip.
4. **A4** Phase-6 live checkpoint (capture→translate→replay→compare; only
   declared losses may differ).
5. **A5** Tag the phase boundary; consumer pin bumps voluntary.

Completion record of prior sessions:

1. ~~7.B live checkpoint~~ DONE 2026-08-23 (FINDINGS O61) — PASSED after
   fixes; probe events cleaned up.
2. ~~Sanitize + commit Graph-side corpus fixtures~~ DONE (commit 1c1d91f):
   five sanitized fixtures under `tests/fixtures/vendor/microsoft/`
   (generator `tools/graphcli/make-fixtures.py`); committed-fixture slot
   added to `tst_ms_event_canon_edge` (11 slots).
3. ~~7.C~~ DONE (three slices: v1 backend, delta+discovery,
   persistence+per-calendar paths; Stage-D verified throughout).
4. ~~Phase 3 remaining~~ DONE 2026-08-23: Graph `contact` ⇄ canon edge
   (fixture promotion from committed contacts-listing.json) + Tasks/Todos
   edges both vendors (`google-task` ⇄ todos canon; Graph `ms-todotask`).
   All stub-level.
5. ~~Identity layer + Phase 6 pipeline/matrix~~ DONE 2026-08-24 (see
   snapshot rows; FINDINGS O64 caught+fixed by the gate).

## Findings index (this campaign)

- **O57** — live Graph payload deltas (OPEN; addenda a–t).
- **O58** — RESOLVED: personal-classification stash assert was parameter-blind.
- **O59** — OPEN: Google wire truths vs reference doc (a) reminders `method`
  key, (b) `eventLabelId` undocumented-in-reference, (c) string-typed
  extendedProperties carriers, (d) cancelled dual-semantics, (e) iCalUID≠id;
  plus tooling notes: moc × raw-string-literal silent failure, AUTOMOC
  timestamp gotcha.
- **O61** — live checkpoint results: (a) sentinel endDate series-amputation
  RESOLVED, (b)-(d) passthrough/stash defects RESOLVED, (e) carrier loss on
  writes CONFIRMED (Reversible = offline-only; PATCH > re-create for 7.C),
  (f) uid/iCalUId are per-copy anchors, (g) original*TimeZone generational
  decay, (h) Exchange body synthesis noise.
- **O61** — live checkpoint results: (a) sentinel-endDate series amputation
  RESOLVED; (b)-(d) passthrough/stash defects RESOLVED; (e) carriers do NOT
  survive creates (Reversible = offline-only; PATCH > re-create); (f)
  uid/iCalUId are per-copy anchors; (g) original*TimeZone generational
  decay; (h) Exchange body synthesis noise.
- **O62** — RESOLVED: async-lifetime house rule explicit (heap-owned state;
  three occurrences this campaign — GraphApiClient walk, backend apply
  batch, mock-era helpers).
- **O60** — RESOLVED: Qt 6.11 `QJsonValue{}` default-constructs Null
  (`isUndefined()==false`) — carrier-absence asserts need explicit boolean
  helpers; wall-time zone interpretation must never route through the
  process-local zone (both hit and fixed during 7.B).

- **O63** — RESOLVED: stale edge-count pin (`tst_vcard_plugin` expected 5
  while stock shapes had 7 since google-person landed — pre-existing fail
  on main, missed by baseline); Graph dateTimeTimeZone type-name vs
  timeZone property-key trap (O60 family).

- **O64** — RESOLVED: google-person demote dropped canon email display
  names (Google home = `emailAddresses[].displayName`); caught by the
  Phase-6 pipeline convergence gate, fixed in the stage.

## Baseline

194 tests total / 192 passing (two documented live-Radicale-state-dependent
slots `tst_backend_signals`, `tst_remotecalendarbackend`; occasionally
load-flaky under full-suite parallelism but pass isolated). Identity layer added
`tst_identity_links` (10) + `tst_person_directory` (7) + `tst_doctrine_pins`
(7); Phase 6 added `tst_gm_pipeline_convergence` (8 incl. matrix byte-pin);
Tier A1 added `tst_engine_vendor_shaped_hub` (6).
