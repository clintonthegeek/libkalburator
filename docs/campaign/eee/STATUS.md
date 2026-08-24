# EEE Campaign STATUS

Vendor-convergence (EEE) campaign per
`docs/2026-08-22-campaign-proposal-vendor-convergence-eee.md`. Updated in
the same commit as plan state (phase-status-docs rule).

**Last updated:** 2026-08-24 (Tier A2/A3 done; sanitize + A4 remain)

## Reading order for a fresh session

1. This file — live state.
2. `2026-08-24-reconnaissance-assessment-and-roadmap.md` — adopted
   doctrine: Tier-A/B expedition order, Part IV ethics of the data model
   (never a merge; the graph forgets; strangers stay strangers; one rule;
   local custody; loud about limits; seizure test), and the four binding
   working rules (corpus-first per interior; crossing-gate coverage
   mandatory; matrix regenerated with edge growth; PATCH over re-create).
3. Archive (only if needed): `archive/2026-08-23-24-session-log-phase0-to-tierA3.md`.

## Where we stand

| Work item | State |
|---|---|
| Phase 0 corpus; Phase 2 google-event (+live checkpoint, v1.02); 7.B ms-event (+live checkpoint); 7.C GraphApiClient + MSGraphCalendarBackend + mock server | **done** |
| Phase 3 — all four edges (`google-person`, `ms-contact`, `google-task`, `ms-todotask`), stub-level | **done** — both stock-shape registries at **9 edges**; loss profiles declared first under `docs/2026-08-2*-edge-loss-profile.md`; task-side fixture promotion slots still pending (captures exist machine-local) |
| Identity layer (§5) | **done** — `src/identity/`: IdentityStore schema v2, resolver, PersonDirectory ("who is in this meeting?"), doctrine pins |
| Phase 6 pipeline gate + matrix | **done** — `tst_gm_pipeline_convergence` byte-pins the generated `CONVERGENCE-MATRIX.md`; engine slice A1 also done (below) |
| Tier A1 engine vendor-shaped hub | **done** — `tst_engine_vendor_shaped_hub`: G-wire ⇄ canon-hub ⇄ MS-wire in one Queue run; O55/O56 re-proven on vendor records; roster payoff slot |
| Tier A2/A3 live session | **done 2026-08-24** — task corpora captured machine-local (`msgraph/captured/`, `google/captured/`); carrier verdicts in FINDINGS O66+correction: all three channels SURVIVE via nav POSTs + filtered collection-level expand (Outlook id prefix `Microsoft.OutlookServices.OpenTypeExtension.*`); quirks: todoTask inline-create echoes-but-does-not-persist; recurring create requires dueDateTime (server rewrites it); consumer contact GET-by-id flaky → listings/delta only |

## Next actions (ordered)

1. **Sanitize task fixtures** (from machine-local captures) via
   `tools/{googlecli,graphcli}/make-fixtures.py` conventions → commit
   under `tests/fixtures/vendor/{google,microsoft}/` → promotion slots in
   `tst_google_task_canon_edge` / `tst_ms_todotask_canon_edge`.
2. **Annotate CONVERGENCE-MATRIX.md + loss profiles** with the O66
   carrier verdicts (People clientData = proven live-Reversible;
   Graph channels live-workable via nav POSTs only; regenerate matrix).
3. **A4 Phase-6 live checkpoint** (USER-RUN per
   `2026-08-24-live-session-runbook-a2-a3-a4.md`): capture→translate→
   replay→compare; only declared losses may differ.
4. **A5 tag the phase boundary**; consumer pin bumps voluntary.
5. Then Tier-B interiors (roadmap doc): scheduling/free-busy,
   visibility/ACLs, resource calendars, taxonomy entities, MAPI named
   properties, beta-horizon watch.

## Findings index (this campaign)

- **O57–O62** — see FINDINGS.md (Graph payload realities; tooling traps:
  moc × raw strings; QJsonValue Null default; async heap-state rule).
- **O63** — stale edge-count pins must be grepped/updated in the same
  commit that grows an `edges()` list; Graph type is `dateTimeTimeZone`
  but its zone key is plain `timeZone`.
- **O64** — the Phase-6 crossing gate catches per-edge suite blindness
  (google-person email displayName drop; fixed in stage).
- **O65** — events never index participant emails; convergence belongs
  to persons, not meetings (pinned three ways).
- **O66 + correction** — carrier-survival verdicts per channel (all
  survive via documented paths); todoTask inline-create wire-lie;
  recurring-create dueDateTime requirement + server rewrite; consumer
  contact GET-by-id flaky (use listings/delta). Doctrine note added:
  drill by the book FIRST.

## Baseline

195 tests total / 193 passing. The two failures are the documented
live-Radicale-state-dependent slots (`tst_backend_signals`,
`tst_remotecalendarbackend`) — occasionally load-flaky under full-suite
parallelism but green isolated.
