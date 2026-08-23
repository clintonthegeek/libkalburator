# EEE Campaign STATUS

Vendor-convergence (EEE) campaign per
`docs/2026-08-22-campaign-proposal-vendor-convergence-eee.md`. This file is
updated in the same commit as plan state (phase-status-docs rule).

**Last updated:** 2026-08-23

## Current phase snapshot

| Work item | State | Notes |
|---|---|---|
| Phase 0 — corpus + hygiene | in progress | O58 closed (red canon slot was a test-string bug; baseline moved 180/178). Graph corpus ~45 captures machine-local (`msgraph/captured/`), unsanitized. Google side: OAuth desktop client registered; **googlecli** tool landed; awaiting first user authorization, then capture sweep. |
| corpus-sweep per-run tags | done | `CORPUS:<runid>:` subjects + `sweep-clean [tag]`; verified live 2026-08-23 (create→capture→clean under one tag). Closes the cross-contamination vector from the O57 addenda. |
| Phase 2 — google-event ⇄ canon edge | **done (edge level)** | Loss profile declared first (`docs/2026-08-23-google-event-edge-loss-profile.md`); stages + registration + `tst_google_event_canon_edge` (7 slots). Wire truths corrected against the live API reference pre-trust: `reminders.overrides[].method`, string-typed extendedProperties carriers, `eventLabelId` discovered (O59(b)). Suite baseline now **181 total / 179 passing**. NOT yet tagged: live checkpoint pending (needs Google authorization). |
| Stage D — mock Graph server | **done** | `tests/graph/mockgraphserver.*` + `tst_mock_graph_server` (6 slots): $top/$skip+nextLink pagination, /delta walk+replay+fixpoint+410 ResyncRequired, exact routes, 404 shape, request recording. Ready as the test bed for 7.C `MSGraphCalendarBackend`. |
| Phase 7.A/B/C/E | not started | B partially de-risked by Phase 2's edge work? No — Phase 2 was GOOGLE; the MS event edge (7.B) is still open, including the RFC5545⇄patternedRecurrence converter. |

## Next actions (ordered)

1. **USER-RUN:** `./build/tools/googlecli/googlecli login` → authorize →
   then `googlecli calendars` / `events` / `capture` sweeps to build the
   Google-side golden corpus (sanitize before committing any of it).
2. Live checkpoint for the Phase 2 Google edge: create a scenario event via
   googlecli, promote it through `GoogleEventToCanonStage`, demote back,
   compare — extends the corpus-sweep model.
3. 7.B: MS event ⇄ canon stages (loss profile FIRST; RFC5545⇄patternedRecurrence
   converter inside the stage per invariant 5; vendored CLDR windowsZones map).
4. 7.C: `MSGraphCalendarBackend` on top of Stage D's mock server.
5. Phases 3–5 edges (People/Contacts/Tasks) after calendar proves the pattern.

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
