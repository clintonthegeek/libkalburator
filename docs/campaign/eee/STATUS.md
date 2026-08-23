# EEE Campaign STATUS

Vendor-convergence (EEE) campaign per
`docs/2026-08-22-campaign-proposal-vendor-convergence-eee.md`. This file is
updated in the same commit as plan state (phase-status-docs rule).

**Last updated:** 2026-08-23

## Current phase snapshot

| Work item | State | Notes |
|---|---|---|
| Phase 0 — corpus + hygiene | **largely done** | Google corpus captured 2026-08-23 (32 events incl. a birthday-type series + 250-instance monthly series, 72 People connections, 9 groups, nextSyncToken walk); sanitized extracts committed under `tests/fixtures/vendor/google/` (generator: `tools/googlecli/make-fixtures.py`). Graph corpus still machine-local (`msgraph/captured/`, ~45 captures) — sanitize before committing any of it. |
| corpus-sweep per-run tags | done | `CORPUS:<runid>:` subjects + `sweep-clean [tag]`; verified live 2026-08-23. Closes the cross-contamination vector from the O57 addenda. |
| Phase 2 — google-event ⇄ canon edge | **done incl. live checkpoint** | Loss profile declared first; stages + registration + `tst_google_event_canon_edge` (8 slots incl. committed-fixture promotion). Wire truths corrected against the live API reference pre-trust (O59). Live checkpoint PASSED 2026-08-23: G→C→G diffs = 4, all declared normalizations (timestamp ms-truncation ×2, offset-form canonicalization ×2); round-tripped body re-created on the real account; both server copies promote to IDENTICAL canon modulo identity fields. Suite baseline **182 total / 180 passing**. Tagged. |
| Stage D — mock Graph server | **done** | `tests/graph/mockgraphserver.*` + `tst_mock_graph_server` (6 slots): $top/$skip+nextLink pagination, /delta walk+replay+fixpoint+410 ResyncRequired, exact routes, 404 shape, request recording. Ready as the test bed for 7.C `MSGraphCalendarBackend`. |
| Phase 7.A/B/C/E | not started | B's loss profile IS declared (`docs/2026-08-23-ms-event-edge-loss-profile.md`); converter implementation next. |

## Next actions (ordered)

1. **7.B implementation:** RFC5545⇄`patternedRecurrence` converter unit
   suite FIRST (`tst_recurrence_pattern_converter`), then the ms-event edge,
   per the declared loss profile.
2. 7.C: `MSGraphCalendarBackend` on top of Stage D's mock server.
3. Sanitize + commit Graph-side corpus fixtures (same two-pass sanitizer).
4. Phase 3: Google People ⇄ contacts canon (fixtures already committed);
   Google Tasks rides along.
5. Phases 4–6; convergence matrix generation.

## Findings index (this campaign)

- **O57** — live Graph payload deltas (OPEN; addenda a–t).
- **O58** — RESOLVED: personal-classification stash assert was parameter-blind.
- **O59** — OPEN: Google wire truths vs reference doc (a) reminders `method`
  key, (b) `eventLabelId` undocumented-in-reference, (c) string-typed
  extendedProperties carriers, (d) cancelled dual-semantics, (e) iCalUID≠id;
  plus tooling notes: moc × raw-string-literal silent failure, AUTOMOC
  timestamp gotcha.

## Baseline

181 tests total / 179 passing. Known failures are the two documented
live-Radicale-state-dependent slots (`tst_backend_signals`,
`tst_remotecalendarbackend`).
