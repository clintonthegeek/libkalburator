# EEE session log — Phase 0 through Tier A3 (2026-08-23/24) [ARCHIVED]

Historical record; live state lives in ../STATUS.md. Nothing here is
binding. Kept because the phase-status-docs rule demands history be
findable, not because a fresh session needs it.

## 2026-08-23

- Phase 0: Graph+Google corpora captured live, sanitized fixtures
  committed under tests/fixtures/vendor/{google,microsoft}/ (generators:
  tools/{googlecli,graphcli}/make-fixtures.py; @odata.context rewrite).
- Phase 2 google-event edge: tagged v1.02, live checkpoint PASSED.
- Phase 7.B ms-event edge + recurrencepatternconverter + CLDR zone map;
  live checkpoint PASSED after fixing sentinel endDate amputation
  (O61(a)) and three passthrough defects (O61(b)-(d)). O61(e): event SVEP
  carriers die on create (later refined: PATCH-in-place works). O61(f):
  uid/iCalUId are per-copy anchors.
- Phase 7.C MSGraphCalendarBackend (3 slices) + GraphApiClient + mock
  server + persistence; tst_ms_graph_calendar_backend 11 slots.
- Phase 3 google-person edge landed (clientData carriers), 7 edges.

## 2026-08-24

- Phase 3 COMPLETED: ms-contact edge (open-extension carriers;
  contacts-listing.json fixture promotion), google-task edge (NO carrier
  channel — Dropped rulings honest), ms-todotask edge (recurrence via the
  7.B converter; importance<=>priority table). Both stock-shape registries
  at 9 edges. Suites: tst_ms_contact_canon_edge (7),
  tst_google_task_canon_edge (6), tst_ms_todotask_canon_edge (7).
- Stale edge-count pins fixed in tst_vcard_plugin/tst_vtodo_plugin
  (O63(a)); dateTimeTimeZone-vs-timeZone trap recorded (O63(b)).
- Identity layer (proposal §5): src/identity/ IdentityStore schema v2
  (+display-name projection), identityresolver, PersonDirectory
  ("who is in this meeting?"), doctrine pins (tst_doctrine_pins).
- Phase 6 pipeline gate tst_gm_pipeline_convergence (8 slots incl.
  byte-pinned generated CONVERGENCE-MATRIX.md via tools/matrixgen);
  caught O64 (google-person email displayName drop — fixed).
- Tier A1 engine-level vendor-shaped hub convergence
  (tst_engine_vendor_shaped_hub, 6 slots); caught O65 (events must never
  index participant emails — convergence belongs to persons).
- Reconnaissance assessment + Tier-A/B roadmap adopted
  (2026-08-24-reconnaissance-assessment-and-roadmap.md, Part IV ethics).
- Tier A2/A3 LIVE SESSION: task corpora captured machine-local
  (/me/todo + Google Tasks both lists); carrier verdicts after
  docs-audited re-drills (O66 + correction): ALL THREE channels survive
  when spoken to per docs (People clientData; todoTask/contact extensions
  via nav POSTs + filtered collection-level expand with the RETURNED full
  id — Outlook prefix Microsoft.OutlookServices.OpenTypeExtension.*).
  True quirks kept: todoTask inline-at-create echoes-but-does-not-
  persist; recurring todoTask create REQUIRES dueDateTime and the server
  rewrites it to align with the pattern; consumer contact GET-by-id
  flaky/broken (listings/delta only). googlecli grew `raw` verb; scopes
  widened both CLIs (Tasks.ReadWrite / auth/tasks).

Suite progression across the two sessions: 187/184 → 195/193.
