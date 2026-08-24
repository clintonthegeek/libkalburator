# EEE Campaign STATUS

Vendor-convergence (EEE) campaign per
`docs/2026-08-22-campaign-proposal-vendor-convergence-eee.md`. This file is
updated in the same commit as plan state (phase-status-docs rule).

**Last updated:** 2026-08-23

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
| Phase 3 — google-person ⇄ contacts-canon edge | **done (stub-level)** | Loss profile declared first (`docs/2026-08-23-google-person-edge-loss-profile.md`); `googlepersoncanonstages.{h,cpp}` + catalogue, registered in ContactsStockShapes (**7 edges** now). Carriers ride Google People `clientData` rows (the resource's only extension point) under the x-canon-* key discipline. uid ⇄ resourceName (per-account anchor). Gated by `tst_google_person_canon_edge` (7 slots incl. all-72-connections fixture promotion). Live checkpoint deferred until googlecli grows people write verbs. |


| Phase 7.C — `MSGraphCalendarBackend` delta + discovery | **done (Stage-D verified)** | fetchItems now drives /delta walks: initial walk seeds a merged cache + resume token; later walks upsert changes (@removed ⇒ evict) and report the FULL merged collection (E7 sync-collection semantics); 410 ResyncRequired self-heals via one fresh initial walk (O42 pattern). Discovery: /me/calendars → calendarDiscovered + availableCollections/discoveredCalendar DTOs (VEVENT-only; Graph tasks live in /me/todo/lists). Mock server grew invalidateDeltaTokens() for the expiry drill. Suite: `tst_ms_graph_calendar_backend` 9 slots. |
| Phase 7.C — `MSGraphCalendarBackend` v1 | **done (Stage-D verified)** | `src/calendar/msgraphcalendarbackend.{h,cpp}` + `tst_ms_graph_calendar_backend` (6 slots): records carry RAW ms-event wire JSON (`nativeShapes={calendar,ms-event}`) so the engine promotes via the registered 7.B edge — no backend-side conversion on the unified path; Incidence legacy surface converts ms-event→canon→iCal inside the backend. Writes POST/PATCH/DELETE sequentially-async; creates mint Graph ids bridged via WriteOperation::idAliases (O55); updates are PATCH-in-place per O61(e). Design decision RESOLVED: pipeline-inside-backend for the legacy surface only; the engine boundary stays record-native. |
| Phase 7.C foundation — `GraphApiClient` | **done** | `src/graph/graphapiclient.{h,cpp}` + `tst_graph_api_client` (8 slots vs Stage D mock): multi-page collection walks ($top honored, order-stable), Bearer injection, delta initial/replay/fixpoint steps with typed 410 ResyncRequired surfacing, error.code extraction (O57(j)). Wire nuance pinned: a NON-EMPTY queued change page answers nextLink — the delta fixpoint is "empty change set + deltaLink", so walkers must step until complete, not until a seen token. Backend integration (fetch/applyRecords against these primitives) is the remaining 7.C work. |
| Phase 7.E | not started | — |

## Next actions (ordered)

1. ~~7.B live checkpoint~~ DONE 2026-08-23 (FINDINGS O61) — PASSED after
   fixes; probe events cleaned up.
2. ~~Sanitize + commit Graph-side corpus fixtures~~ DONE (commit 1c1d91f):
   five sanitized fixtures under `tests/fixtures/vendor/microsoft/`
   (generator `tools/graphcli/make-fixtures.py`); committed-fixture slot
   added to `tst_ms_event_canon_edge` (11 slots).
3. ~~7.C~~ DONE (three slices: v1 backend, delta+discovery,
   persistence+per-calendar paths; Stage-D verified throughout).
4. Phase 3 remaining: Graph `contact` ⇄ canon edge
   (`tests/fixtures/vendor/microsoft/contacts-listing.json` fixture
   committed), then Tasks/Todos edges for both vendors
   (`google-task` ⇄ todos canon; Graph `todoTask`).
5. Phases 4–6; convergence matrix generation.
6. Deferred live checkpoints: People clientData write-back semantics and
   a Graph calendar write-path drill via msroundtrip (O61(e)-class).

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

## Baseline

187 tests total / 184 passing. Known failures are the two documented
live-Radicale-state-dependent slots (`tst_backend_signals`,
`tst_remotecalendarbackend`).
