# EEE Campaign STATUS

Vendor-convergence (EEE) campaign per
`docs/2026-08-22-campaign-proposal-vendor-convergence-eee.md`. Updated in
the same commit as plan state (phase-status-docs rule).

**Last updated:** 2026-08-25 (A4 live checkpoint PASSED; A5 tag next)

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
| Phase 3 — all four edges (`google-person`, `ms-contact`, `google-task`, `ms-todotask`), stub-level | **done** — both stock-shape registries at **9 edges**; loss profiles declared first under `docs/2026-08-2*-edge-loss-profile.md` |
| Task-fixture promotion (Next-action 1) | **done 2026-08-24** — sanitized 2026-08-24 Tasks/todoTask captures committed under `tests/fixtures/vendor/{google,microsoft}/` via the extended `make-fixtures.py` sanitizers; promotion slots live in both todo-edge suites (`promoteCommittedLiveFixture`); pre-existing fixtures byte-stable |
| Identity layer (§5) | **done** — `src/identity/`: IdentityStore schema v2, resolver, PersonDirectory ("who is in this meeting?"), doctrine pins |
| Phase 6 pipeline gate + matrix | **done** — `tst_gm_pipeline_convergence` byte-pins the generated `CONVERGENCE-MATRIX.md`; engine slice A1 also done (below) |
| Tier A1 engine vendor-shaped hub | **done** — `tst_engine_vendor_shaped_hub`: G-wire ⇄ canon-hub ⇄ MS-wire in one Queue run; O55/O56 re-proven on vendor records; roster payoff slot |
| Tier A2/A3 live session | **done 2026-08-24** — task corpora captured machine-local (`msgraph/captured/`, `google/captured/`); carrier verdicts in FINDINGS O66+correction: all three channels SURVIVE via nav POSTs + filtered collection-level expand (Outlook id prefix `Microsoft.OutlookServices.OpenTypeExtension.*`); quirks: todoTask inline-create echoes-but-does-not-persist; recurring create requires dueDateTime (server rewrites it); consumer contact GET-by-id flaky → listings/delta only |

## Next actions (ordered)

1. ~~Sanitize task fixtures~~ **done 2026-08-24** — six new fixtures
   (`task-lists`, `task-listing-default`, `task-listing-fog`,
   `todo-lists`, `todo-tasks-listing`, `todo-task-carrier-extension`)
   + promotion slots in `tst_google_task_canon_edge` /
   `tst_ms_todotask_canon_edge`.
2. ~~Annotate CONVERGENCE-MATRIX.md + loss profiles~~ **done
   2026-08-24** — carrier-survival verdict section added to the matrix
   generator preamble (`src/shape/convergencematrix.h`, survives
   regeneration; byte pin green) and standing amendments landed in all
   four Phase-3 loss profiles (google-person lifted to live-Reversible;
   ms-contact/ms-todotask live-workable via nav POSTs + filtered expand;
   google-task Dropped rulings corpus-confirmed).
3. ~~A4 Phase-6 live checkpoint~~ **PASSED 2026-08-25** (USER-DELEGATED,
   per runbook protocol) — fresh captures both vendors; single-event G→C→G
   gates green after two new declarations (google attendee `organizer`
   flag; ms empty `body.content` ≡ absent); cross-vendor replays both
   directions with CORPUS probes PASSED (declared-class diffs only);
   findings + create-path wire truths in FINDINGS **O67**. New tool:
   `tools/groundtrip` (`g-roundtrip`, google-event mirror of ms-roundtrip).
   Probes swept clean.
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

195 tests total. As of 2026-08-25 (evening), 4 Radicale/backend slots fail
(`tst_backend_signals`, `tst_backend_thread_relocation`,
`tst_backend_reentrancy_pin`, `tst_remotecalendarbackend`) — KDAV 30s
transfer-timeout pattern against the live Radicale / latency-injected fake
server. Verified PRE-EXISTING environmental: all four reproduce identically
at pre-session commit `e1846a3` (worktree build, same evening). Not caused
by EEE work; re-check when the local Radicale state is reset. All other
191 slots green, including both new fixture-promotion suites and the matrix
byte pin.
