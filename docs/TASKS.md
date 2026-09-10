# Task queue

**Last updated:** 2026-09-10 (`RRD-009` DONE — scenario 01 generated end to
end against a live `tools/davrig` rig: sync succeeded, credentials verified
from a second process, and a human inspected the real topology widget
(screenshot in `../PlanStan/docs/testing/rrd-009-scenario01-evidence.md`).
`RRD-010` is `READY` next.)
This is the only active work queue. Stable IDs are used by code, tests, issues, and commits.

The `DONE` entries below are retained as historical implementation evidence.
They do not certify the production consumer workflows: `AUD-003` established
the host evidence, and `STB-013` is the workflow-certification gate. Reopen
only the exact task contradicted by new evidence; retain its prior result and
verification under a historical heading.

States: `READY`, `IN PROGRESS`, `QUEUED`, `BLOCKED`, `DONE`, `REMOVED`.

## Now

Work top to bottom unless a task is blocked or the user chooses otherwise.
`RRD-*` is the active queue. `STB-*` is the closed production-stabilization
programme defined in PlanStan's maintained architecture and baseline documents;
its rows are retained below as historical evidence.
The older `DONE` tasks below remain useful implementation evidence, but do not
make the corresponding real application workflow certified.  Keep exactly one
task `IN PROGRESS`; the first unblocked task is deliberately the only `READY`
entry until it is completed and the next selected task is promoted.

| Order | Task | State | Depends on | Outcome |
|---:|---|---|---|---|
| 1 | STB-001 | DONE 2026-09-07 | AUD-003 | Safe runtime topology transaction under invalid, failed, held, and reentrant operations |
| 2 | STB-002 | DONE 2026-09-07 | AUD-003 | Revision-bounded staging save acknowledgments and recovery |
| 3 | STB-003 | DONE 2026-09-07 | AUD-003 | Exact record and generated-occurrence identity end to end |
| 4 | STB-004 | DONE 2026-09-08 | STB-001 | One durable production desired-state commit |
| 5 | STB-005 | DONE 2026-09-08 | STB-004 | Production run policy and trigger wiring |
| 6 | STB-006 | DONE 2026-09-08 | STB-004 | Credential reference/materialization wiring |
| 7 | STB-007 | DONE 2026-09-08 | STB-004 | Account and discovery production wiring |
| 8 | STB-008 | DONE 2026-09-08 | STB-004, STB-005 | Conflict interaction and truthful terminal results |
| 9 | STB-009 | DONE 2026-09-08 | STB-003 | Shared recurrence/occurrence query adopted by all calendar views |
| 10 | STB-010 | DONE 2026-09-08 | STB-001 | Runtime cancellation, executor dispatch, and worker teardown safety |
| 11 | STB-011 | DONE 2026-09-08 (reopened acceptance repaired) | STB-002, STB-003, STB-004, STB-005, STB-006, STB-007, STB-008, STB-010 | One operational runtime owner for the full PlanStan slice |
| 12 | STB-012 | DONE 2026-09-08 | STB-011 | Application close lifecycle integration |
| 13 | STB-013 | DONE 2026-09-08 | STB-009, STB-012 | Certified local and hermetic-DAV desktop workflow matrix |
| 14 | STB-014 | DONE 2026-09-09 | STB-013 | Retire legacy characterization paths after replacement evidence |
| 15 | STB-015 | DONE 2026-09-09 | STB-014 | Independent-consumer and package proof |
| 16 | STB-016 | DONE 2026-09-09 | STB-013 | Measured desktop responsiveness and polish |
| 17 | STB-017 | DONE 2026-09-09 | STB-014, STB-015, STB-016 | Final stabilization/release evidence and closure decision |

### Release readiness — calendar topology UX and inspectable fixtures

`RRD-*` is the successor campaign to the closed `STB-*` programme. Its
specification is `../PlanStan/docs/release-readiness-spec.md`; that document
owns contracts and acceptance, this file owns state. A section number cited
below (§2.1, §3) refers to that specification.

| Order | Task | State | Depends on | Outcome |
|---:|---|---|---|---|
| 1 | RRD-001 | DONE 2026-09-09 | STB-017 | The application and every registered test target compile against public headers |
| 2 | RRD-002 | DONE 2026-09-09 | RRD-001 | A classified pass/fail/timeout/skip baseline for every registered target |
| 3 | RRD-003 | DONE 2026-09-09 | RRD-001 | Draft loss, count truth, and inherited impact pinned in the real widget |
| 4 | RRD-004 | DONE 2026-09-09 | RRD-003 | The pinned topology defects repaired |
| 5 | RRD-005 | DONE 2026-09-09 | RRD-004 | One observable topology draft, owned above the views |
| 6 | RRD-006 | DONE 2026-09-10 | RRD-005 | One testable apply pipeline with a typed review and result |
| 7 | RRD-007 | DONE 2026-09-10 | RRD-002 | A project-local DAV rig with real per-account outage |
| 8 | RRD-008 | DONE 2026-09-10 | RRD-002 | Bundle contract, manifest schema, and guarded generator |
| 9 | RRD-009 | DONE 2026-09-10 | RRD-007, RRD-008 | Scenario 01 as a retained, openable, credentialed bundle |
| 10 | RRD-010 | READY | RRD-009 | Chain relay and mesh scenarios with independent oracles |
| 11 | RRD-011 | QUEUED | RRD-009 | Directional and shared-destination scenarios |
| 12 | RRD-012 | QUEUED | RRD-009 | Component restrictions, properties, and seven distinct states |
| 13 | RRD-013 | QUEUED | RRD-009 | Invalid corpus rejected with no side effect, behind a safe diagnostic open |
| 14 | RRD-014 | QUEUED | RRD-010, RRD-011, RRD-012 | Mutations, recurrence identity, and clone-only destructive operations |
| 15 | RRD-015 | QUEUED | RRD-013, RRD-014 | A truthful capability matrix with explicit gaps |
| 16 | RRD-016 | QUEUED | RRD-006, RRD-009 | Calendars, copies, and rules page as a correct read-only projection |
| 17 | RRD-017 | QUEUED | RRD-016 | Arrangement, copy, primary, and rule editing without a port drag |
| 18 | RRD-018 | QUEUED | RRD-011, RRD-017 | Account discovery states and four distinct removal verbs |
| 19 | RRD-019 | QUEUED | RRD-017 | Truthful run feedback and separated draft, save, and run |
| 20 | RRD-020 | QUEUED | RRD-010, RRD-006 | Graph focus, groups, stable layout, legend, and keyboard traversal |
| 21 | RRD-021 | QUEUED | RRD-011, RRD-020 | Route tracing, cross-calendar warnings, and a scale variant |
| 22 | RRD-022 | QUEUED | RRD-015, RRD-018, RRD-019, RRD-021 | Measured usability against the stated acceptance targets |
| 23 | RRD-023 | QUEUED | RRD-022 | Campaign closure and the release decision |

The detailed scope, source-entry map, fixtures, and acceptance cases for these
tasks are maintained in `../PlanStan/docs/architecture.md` under
“Stabilization strategy and execution gates.”  The compact mapping in
`../PlanStan/docs/stabilization-baseline.md` is the required starting point for
new agents.  Update this queue, the corresponding task record, and affected
architecture facts in the same change; do not create a parallel campaign.

## Historical implementation evidence

| Order | Task | State | Depends on | Outcome |
|---:|---|---|---|---|
| 1 | AUD-003 | DONE 2026-09-07 | — | Established the tagged cross-repository baseline and real PlanStan host fixture |
| 2 | AUD-001 | DONE 2026-09-04 | — | Revalidated completed gates and reordered remaining work |
| 2 | SAF-005 | DONE 2026-09-04 | SAF-002, PS-002 | Restored a clean executor-owned default lifecycle baseline |
| 3 | TST-001 | DONE 2026-09-04 | SAF-001 | Separate executed, skipped, optional, and live coverage |
| 4 | TOP-001 | DONE 2026-09-04 | DES-001 | Define the operational factory and topology-materialization contract |
| 5 | RUN-001 | DONE 2026-09-04 | TOP-001 | Complete runtime construction and backend ownership |
| 6 | KRN-001 | DONE 2026-09-04 | RUN-001, DES-004 | Close the inseparable runtime execution/contract slice |
| 7 | RUN-003 | DONE 2026-09-05 | KRN-001 | Complete store and schema lifecycle ownership |
| 8 | TOP-002 | DONE 2026-09-05 | RUN-003 | Atomic fallible topology application |
| 9 | API-005 | DONE 2026-09-05 | KRN-001 | Make library capability acquisition narrow and explicit |
| 10 | AUD-002 | DONE 2026-09-05 | — | Revalidated and repaired the decomposed PlanStan cutover plan |
| 11 | DOC-002 | DONE 2026-09-05 | AUD-002 | Aligned PlanStan's authoritative guidance with the adopted ownership migration |
| 12 | API-007 | DONE 2026-09-05 | TOP-002 | Publish typed provider state and discovery facts through the facade |
| 13 | API-008 | DONE 2026-09-05 | RUN-003 | Replay/query unresolved conflicts without exposing the store |
| 14 | API-009 | DONE 2026-09-05 | KRN-001, RUN-003 | Publish mapping state and typed run telemetry through the facade |
| 15 | RUN-007 | DONE 2026-09-06 | KRN-001 | Preserve PlanStan run policy through a runtime-owned contract |
| 16 | RUN-008 | DONE 2026-09-06 | RUN-003 | Resolve and implement the journal/recovery ownership boundary |
| 17 | TOP-004 | DONE 2026-09-06 | TOP-002 | Include provider edits and durable desired state in topology commit/rollback |
| 18 | TOP-005 | DONE 2026-09-06 | API-007, API-009, TOP-004 | Put physical collection mutations behind runtime commands |
| 19 | PS-009 | DONE 2026-09-06 | DOC-002, TOP-002, API-005 | Complete PlanStan endpoint-factory coverage |
| 20 | PS-010 | DONE 2026-09-06 | API-007, PS-009, TOP-005 | Compile PlanStan configuration into runtime definitions |
| 21 | PS-011 | DONE 2026-09-06 | API-009, PS-010 | Project canonical record and collection events into PlanStan models |
| 22 | PS-012 | DONE 2026-09-06 | API-009, PS-010, RUN-007 | Adapt every PlanStan run verb, trigger, and progress event |
| 23 | PS-013 | DONE 2026-09-06 | API-008, PS-010, RUN-007 | Adapt PlanStan conflict presentation and resolution |
| 24 | PS-014 | DONE 2026-09-06 | API-007, PS-010, TOP-004 | Adapt PlanStan account and discovery workflows |
| 25 | PS-015 | DONE 2026-09-06 | PS-010, PS-014, TOP-004, TOP-005 | Adapt topology UI and wizard to one desired-state commit |
| 26 | PS-016 | DONE 2026-09-06 | PS-011, PS-012, PS-013, PS-014, PS-015, RUN-008 | Switch PlanStan production ownership to `CollectionRuntime` |
| 27 | PS-008 | DONE 2026-09-06 | PS-016 | Delete transitionals and close the PlanStan migration gate |
| 28 | WP-009 | DONE 2026-09-06 | TOP-002, API-005 | One WildPalms runtime-facade cutover; delete duplicate graph/loop |
| 29 | API-006 | DONE 2026-09-06 | PS-008, WP-009 | Remove the transitional composite/default backend seam |
| 30 | TST-003 | DONE 2026-09-06 | PS-008, WP-009 | Pin both real consumer cutovers in integration CI |
| 31 | TST-002 | DONE 2026-09-06 | TST-001 | Add clean library CI |
| 32 | BLD-002 | DONE 2026-09-06 | PS-008 | Extract boundaries demonstrated by the first real cutover |
| 33 | BLD-004 | DONE 2026-09-06 | API-006, BLD-002 | Install namespaced public headers |
| 34 | BLD-005 | DONE 2026-09-06 | RUN-002, BLD-002, BLD-003 | Remove whole-archive and target mutation |
| 35 | TST-004 | DONE 2026-09-06 | TST-002 | Add sanitizer and opt-in live lanes |
| 36 | FTR-001 | DONE 2026-09-06 | PS-008, BLD-005 | Certify calendar local/DAV vertical slice |
| 37 | FTR-002 | DONE 2026-09-06 | WP-009, BLD-005 | Certify Palm four-domain vertical slice |
| 38 | FTR-003 | DONE 2026-09-06 | — | Decide Google/Microsoft delivery |
| 39 | FTR-004 | DONE 2026-09-06 | — | Decide outline and identity layers |
| 40 | FTR-005 | DONE 2026-09-06 | — | Decide universal storage and recovery stubs |

`READY` means its stated prerequisites are complete in the current trees. A
task may depend only on task IDs, never on a later roadmap gate or on acceptance
that belongs to another task. The old design/implementation cross-chain is
retained below for history, but KRN-001 is its single closure point.

### Forward dependency DAG

These are the complete forward edges. A row becomes executable when every
predecessor is `DONE`; external decisions are roots, never task back-edges.

| Predecessor(s) | Successor |
|---|---|
| KRN-001 | RUN-003, API-005 |
| RUN-003 | TOP-002 |
| TOP-002 | API-007, TOP-004 |
| RUN-003 | API-008, RUN-008 |
| KRN-001 + RUN-003 | API-009 |
| KRN-001 | RUN-007 |
| AUD-002 | DOC-002 |
| API-007 + API-009 + TOP-004 | TOP-005 |
| DOC-002 + TOP-002 + API-005 | PS-009 |
| TOP-002 + API-005 | WP-009 |
| API-007 + PS-009 + TOP-005 | PS-010 |
| API-009 + PS-010 | PS-011 |
| API-009 + PS-010 + RUN-007 | PS-012 |
| API-008 + PS-010 + RUN-007 | PS-013 |
| API-007 + PS-010 + TOP-004 | PS-014 |
| PS-010 + PS-014 + TOP-004 + TOP-005 | PS-015 |
| PS-011 + PS-012 + PS-013 + PS-014 + PS-015 + RUN-008 | PS-016 |
| PS-016 | PS-008 |
| PS-008 + WP-009 | API-006, TST-003 |
| PS-008 | BLD-002 |
| BLD-001 | BLD-003 |
| API-006 + BLD-002 | BLD-004 |
| RUN-002 + BLD-002 + BLD-003 | BLD-005 |
| license decision | BLD-006 |
| BLD-004 + BLD-005 + BLD-006 | BLD-007 |
| TST-001 | TST-002 |
| TST-002 | TST-004 |
| PS-008 + BLD-005 | FTR-001 |
| WP-009 + BLD-005 | FTR-002 |
| FTR-001 + FTR-002 + FTR-003 + FTR-004 + FTR-005 + consumer request | FTR-006 |
| AUD-003 | STB-001, STB-002, STB-003 |
| STB-001 | STB-004, STB-010 |
| STB-003 | STB-009 |
| STB-004 | STB-005, STB-006, STB-007, STB-008 |
| STB-005 | STB-008 |
| STB-002 + STB-003 + STB-004 + STB-005 + STB-006 + STB-007 + STB-008 + STB-010 | STB-011 |
| STB-011 | STB-012 |
| STB-009 + STB-012 | STB-013 |
| STB-013 | STB-014, STB-016 |
| STB-014 | STB-015, STB-017 |
| STB-015 + STB-016 | STB-017 |
| STB-017 | RRD-001 |
| RRD-001 | RRD-002, RRD-003 |
| RRD-003 | RRD-004 |
| RRD-004 | RRD-005 |
| RRD-005 | RRD-006 |
| RRD-002 | RRD-007, RRD-008 |
| RRD-007 + RRD-008 | RRD-009 |
| RRD-009 | RRD-010, RRD-011, RRD-012, RRD-013 |
| RRD-010 + RRD-011 + RRD-012 | RRD-014 |
| RRD-013 + RRD-014 | RRD-015 |
| RRD-006 + RRD-009 | RRD-016 |
| RRD-016 | RRD-017 |
| RRD-011 + RRD-017 | RRD-018 |
| RRD-017 | RRD-019 |
| RRD-010 + RRD-006 | RRD-020 |
| RRD-011 + RRD-020 | RRD-021 |
| RRD-015 + RRD-018 + RRD-019 + RRD-021 | RRD-022 |
| RRD-022 | RRD-023 |

The former monolithic `PS-008` has been decomposed. Preparatory PlanStan
adapters may land independently, but they stay inert until `PS-016` performs
the one production ownership switch. `PS-008` is now the deletion and closure
task, not a container for untracked work. `WP-009` remains independently ready.
`API-006` is deliberately downstream of both consumer cutovers, which are
where the consumer extension contracts can be migrated before transitional
defaults are removed.
Run `python3 tools/check_task_dag.py` after editing task states or dependencies;
it rejects malformed, missing, removed, and cyclic prerequisite edges.

### Cross-repository execution protocol

Tasks with **Repository:** `../PlanStan` are first-class work in this queue.
PlanStan's former project-wide roadmap was archived as superseded in July 2026;
its surviving `docs/plans/` files track unrelated UI campaigns and are not a
queue for this refactor. A fresh agent working from this repository must:

1. mark the selected task `IN PROGRESS` here and read `../PlanStan/AGENTS.md`,
   `README.md`, `docs/architecture.md`, and the current top of `CLAUDE.md`;
2. edit PlanStan only for the named slice, without adding a parallel roadmap,
   campaign, handoff, or compatibility path;
3. build PlanStan against this working tree with
   `-DPLANSTAN_LIBKALBURATOR_SOURCE_DIR=/home/clinton/dev/libkalburator` and run
   the focused tests named by the task;
4. update the task's state/result/verification and affected maintained docs or
   issue records here in the same change.

Preparatory work must have tests and must not activate a second live graph.
`STB-011` is the sole operational-ownership transfer: it routes the complete
application slice through one runtime and removes redundant live construction
in the same patch.

## Release-readiness tasks

These records turn the calendar-topology UX and inspectable-fixture campaign
into a finite completion queue. `../PlanStan/docs/release-readiness-spec.md` is
the specification: it owns the contracts (§2), the per-task scope and acceptance
detail (§3), and the handoff protocol (§4). Section numbers below refer to it.
Each record here states the non-negotiable boundary so one agent can take one
bounded slice without relitigating the campaign.

The campaign's premise is six defects reproduced in the 2026-09-09 trees and
recorded as D1–D6 in the specification's §1. D1–D3 share one root cause:
`STB-015` removed PlanStan's injection of libkalburator private `src/*` include
directories and the dependent sources were never migrated, so the application
target and 65 of 145 registered test targets no longer compile.

### RRD-001 — Restore the public-header build contract

- **State:** DONE 2026-09-09
- **Depends on:** STB-017
- **Repository:** `../PlanStan`
- **Scope:** Migrate every flat library include in `src/` and `tests/` to
  `<kalburator/<domain>/<header>.h>` per specification §2.1. 80 files carry a
  flat spelling: `src/main.cpp` plus 79 test sources across 13 directories. 67
  of them are confirmed build failures; migrate all 80, because a spelling that
  resolves through a sibling include path today is the same latent defect. Do
  not restore private include-directory injection and do not change behavior.
- **Acceptance:** `cmake --build build-dev -j6` builds `all` with zero
  header-not-found errors, including the `PlanStan` application target and both
  live gates. A source search finds no PlanStan CMake reference to a
  libkalburator `src/*` directory. The diff carries include-line changes only;
  any file needing more is named here with its reason.
- **Verification:**
  - Method: built a header/domain map from every header file directly under
    each of libkalburator's 22 public `src/<domain>/` directories (239
    headers, `KALBURATOR_PUBLIC_HEADER_DOMAINS` in
    `libkalburator/CMakeLists.txt:51`), then rewrote every PlanStan `.cpp`/`.h`
    `#include` line under `src/` and `tests/` whose bare filename matched a
    public header and had no same-directory local header of that name, to
    `<kalburator/<domain>/<header>.h>`. One ambiguous basename
    (`syncoperation.h`, present under both `calendar/` and `sync/`) was
    resolved per file by checking which types the file actually uses
    (`FetchOperation`/`PushOperation`/`DeleteOperation` → `calendar/`; bare
    `SyncOperation` → `sync/`); all 5 affected files used the calendar-typed
    subclasses, so all resolved to `calendar/syncoperation.h`. One local-name
    collision was identified and excluded from migration:
    `src/widgets/caldavconfigwidget.h` (PlanStan's own widget) shares a
    basename with `libkalburator/src/sync/caldavconfigwidget.h`; the same-file
    self-include in `src/widgets/caldavconfigwidget.cpp` correctly resolves
    locally and was left untouched.
  - **81 files touched** (`src/main.cpp` plus 80 test sources), **287 include
    lines rewritten**, confirmed by `git diff` to contain include-line changes
    only (`git diff -- src tests | grep '^[+-]' | grep -v '#include'` empty).
    Per-directory counts: `src` 1, `tests/app` 2, `tests/backends` 8,
    `tests/commands` 1, `tests/controllers` 14, `tests/core` 3,
    `tests/integration` 12, `tests/kalbconfigmanager` 2, `tests/settings` 1,
    `tests/sync` 23, `tests/sync-host-smoke` 1, `tests/sync-workflow` 2,
    `tests/views` 7, `tests/widgets` 1, `tests/wizards` 3.
  - `grep -rn "libkalburator/src" --include=CMakeLists.txt .` — no matches; no
    PlanStan CMake file references a libkalburator `src/*` directory (this was
    already true before this task; STB-015 removed the injection, this task
    only migrated the dependent includes).
  - `cmake --build build-dev -j6` (then `-j6 -- -k` to surface every error in
    one pass): **zero header-not-found errors** anywhere in the tree. The
    `PlanStan` application target and both live gates
    (`tst_integration_live_fanout_gate`, `tst_integration_live_graph_gate`)
    build cleanly; `build-dev/PlanStan` timestamp advanced to
    2026-09-09T22:18:19 (previously a stale 2026-09-08 artifact per D1).
    `ctest -R '^live_fanout_gate$|^live_graph_gate$' --output-on-failure`:
    both **Passed** (D3 repaired — both live DAV gates are reproducible
    again).
  - **One file needs more than an include change**, named per the acceptance
    clause rather than fixed here (fixing it is a behavior/logic decision,
    out of RRD-001's "include changes only" scope):
    `tests/widgets/tst_backendconfigwidgets.cpp` already carried the correct
    namespaced include (`<kalburator/sync/caldavconfigwidget.h>`) before this
    task and is unchanged by it. It fails to compile because
    `Kalburator::Sync::CalDavConfigWidget` now requires a `CalDavProvider *`
    constructor argument and lives in the `Kalburator::Sync` namespace, while
    the test constructs it unqualified with `CalDavConfigWidget w;` (no
    args) — a pre-existing stale test against an older/local widget API,
    unrelated to the header-spelling defect this task fixes. This is the
    build's one remaining compile failure; every other one of the 145
    registered targets builds. Filed for RRD-002's classification pass.
  - Full-suite run for context (not part of RRD-001's acceptance,
    but the natural next evidence for RRD-002): `ctest --output-on-failure -j4`
    at repository revision PlanStan `b2e1c354` / libkalburator `b341b3b`:
    **92% passed, 11 of 145 failed** (`tst_kalbsynctopologydatasource_providers`,
    `tst_synctopologywidget_v2_changeset`, `tst_synctopologywidget_v2_palette`,
    `tst_collectioncontroller`, `tst_controller_calendars`,
    `tst_collectioncontroller_provider_lifecycle`,
    `tst_collectioncontroller_add_from_collections`,
    `tst_collectioncontroller_g1_closure`,
    `tst_collectioncontroller_recordchanged`,
    `integration_incidence_crud` (subprocess aborted), `tst_collectionassembler`).
    None of these are header-not-found failures. `tst_backendconfigwidgets`
    did not run (build failure, not a test failure). Total real time 339s.
    Raw log not retained past this session; rerun to reproduce.
- **Next:** RRD-002 and RRD-003 both unblock.

### RRD-002 — Record a classified baseline for every registered target

- **State:** DONE 2026-09-09
- **Depends on:** RRD-001
- **Repository:** `../PlanStan`
- **Scope:** Build and run all 145 registered targets. Classify each as passes,
  fails, times out, skips, or requires a live service. Publish
  `../PlanStan/docs/testing/registered-target-baseline.md` with command, result,
  duration, and classification per target. File each real failure as a focused
  defect; repair none of them here.
- **Acceptance:** no unexplained target. `integration_incidence_crud`'s recorded
  five-minute timeout is carried explicitly as an unclassified timeout, not as
  acceptance evidence. Live-gated targets are recorded as skipped with the gate
  variable named, separately from failures.
- **Verification:**
  - Published `../PlanStan/docs/testing/registered-target-baseline.md`, produced
    from `cmake --build build-dev -j6` then
    `ctest --test-dir build-dev -j4 --output-on-failure --output-junit <xml>`
    at PlanStan `a0448be6` / libkalburator `1ebfbff` / libkalcal `10d60d2`.
  - All 145 registered targets classified, none unexplained: 132 pass, 11 fail,
    1 timeout (`integration_incidence_crud`, ~307s inside `testEditDateTime()`,
    carried explicitly per the acceptance clause, not counted as passing), 1
    build failure (`tst_backendconfigwidgets`, filed against `RRD-001`'s
    verification and repeated here — a stale `CalDavConfigWidget` constructor
    call, unrelated to header spelling).
  - **Correction applied before publishing:** the first run showed
    `tst_backendconfigwidgets` as "Passed" because `cmake --build` left a
    pre-RRD-001 binary in place (its one source file fails to compile, so the
    linker step never ran to replace it). The stale executable was deleted and
    the target rerun, correctly producing "Not Run" / build failure. No other
    target's binary was stale — every other target's source changed or not,
    the full `-j6` build in `RRD-001` relinked everything it needed to.
  - No target is gated behind a live-service environment variable; both live
    DAV gates (`live_fanout_gate`, `live_graph_gate`) ran against the system
    Radicale on `127.0.0.1:5232` and passed, so the "skip — live service"
    classification is currently empty (0 targets). This may change once
    `RRD-007`'s project-local rig exists.
  - The 11 failures are recorded with per-target failing-case counts and a
    representative assertion in the baseline doc; none repaired here. Most
    cluster around provider/backend-registry wiring (`CollectionController`
    provider lifecycle, provider-derived collection assembly, the v2 topology
    widget's provider ghost ports) — noted as a plausible but *unconfirmed*
    shared cause, filed as separate defects pending investigation, not
    diagnosed further in this slice. One failure
    (`tst_collectioncontroller_syncverbs`) was observed flaky: it passed in an
    earlier run this session and failed in the run the published baseline is
    drawn from — recorded in the doc rather than silently resolved either way.
- **Next:** RRD-003, RRD-007, and RRD-008 all unblock; `RRD-003` is selected
  next per Now-table order (it depends only on `RRD-001`, already done, and
  precedes `RRD-007`/`RRD-008` in the table).

### RRD-003 — Pin draft loss, count truth, and inherited impact

- **State:** DONE 2026-09-09
- **Depends on:** RRD-001
- **Repository:** `../PlanStan`
- **Scope:** Add failing regressions against the real `SyncTopologyWidget`, not
  a mock, for D4 (a collection-default change destroys unrelated staged edits),
  D5 (reported mapping counts include membership links), and inherited impact (a
  default change must name every affected existing `CollectionDefault`
  calendar before Apply). Add a characterization test for how the current graph
  renders a mapping-only unbound endpoint, the scenario 06 shape.
- **Acceptance:** the D4 assertion covers every staged edit type the changeset
  carries, not only wiring policy. Each regression fails for the stated reason
  and is recorded as failing, never skipped. The characterization test records
  observed behavior and passes.
- **Verification:**
  - New file `tests/sync/tst_topology_draft_and_counts.cpp` (registered in
    `tests/sync/CMakeLists.txt`), built and run standalone
    (`QT_QPA_PLATFORM=offscreen ./tst_topology_draft_and_counts`) and via
    `ctest -R 'topology|synctopologywidget'`: **12 failed, 3 passed** (the two
    QtTest auto-slots plus the characterization test) — every failure for
    exactly its stated reason, none skipped.
  - **D4, all 10 staged-edit-types the changeset carries**, each its own
    slot staging one edit via its real production entry point (never a
    synthesized `m_changeset` mutation — same discipline as
    `tst_topology_dirtystate.cpp`), then invoking the private slot
    `onTopologyPresetChanged(1)` via `QMetaObject::invokeMethod`, then
    asserting the edit is still staged: `wiringPolicy` (`stageWiringPolicyChange`
    → `isWiringPolicyModified`/`modifiedWiringPolicies()`), `mapping`
    (`onEdgeRequested` channel drag → `isMappingAdded`), `adoption`
    (`stageAdoptCalendar` → `isCalendarAdoptionStaged`), `create`
    (`stageCreateCalendar` → `createCalendarRequests()`), `update`
    (`stageUpdateCalendar` → `updateCalendarRequests()`), `unbind`
    (`unbindCalendar` → `unbindRequests()`), `untrack` (`untrackCalendar` →
    `untrackRequests()`), `destroy` (`destroyCalendar` → `destroyRequests()`),
    `providerRemoval` (`stagePendingProviderRemoval` → `isProviderRemoved`
    — provider *add* is deliberately excluded: `stagePendingProvider()`
    stores into the widget's own `m_pendingProviders`, not
    `m_changeset`, so it is out of D4's "the changeset carries" scope),
    `localBackend` (`stagePendingLocalBackend` → `isLocalBackendAdded`).
    All 10 fail today: `onTopologyPresetChanged()`
    (`synctopologywidget.cpp:2372`, confirmed) calls `rebuildGraph()` first,
    which calls `m_changeset.clear()` at line 648 before the new default is
    even recorded, discarding every one of them unconditionally.
  - **D5:** fixture is one LC with 3 enabled bindings (1 primary + 2
    secondary) and 1 real `SyncMapping` channel. The widget's own
    `channelEdgeCount()` (`m_edgeToMapping.size()`) already reports the
    correct figure (1) — proving the defect is specifically that
    `updateStatusBar()`/`rebuildGraph()`'s `selectionCleared()` emission
    report `m_edges.size()` (4: 3 membership + 1 channel) instead. Binding a
    4th backend into the same LC with no corresponding mapping (a real,
    enabled, unsynced membership link) leaves `channelEdgeCount()` at 1 but
    the reported figure would climb to 5 — pinning "unchanged by the number
    of membership links" precisely. Observed today via `QSignalSpy` on the
    public `selectionCleared` signal: reported 4 vs. correct 1.
  - **Inherited impact:** added `SyncTopologyWidget::collectionDefaultCalendarNames()
    const` (synctopologywidget.h/.cpp) — a small, already-CORRECT, read-only
    query (mirrors the existing `buildNodes()` CollectionDefault-resolution
    idiom at line ~872) over `m_dataSource->logicalCalendars()`, not wired
    into anything — and a new signal `topologyPresetImpact(const QStringList&)`,
    declared but never emitted. Both are additive scaffolding so the pinning
    test compiles against the real widget; neither changes any existing
    behavior (confirmed: the full topology/synctopologywidget ctest subset
    is otherwise unchanged — see below). The test asserts
    `collectionDefaultCalendarNames()` is correct in isolation (passes: for
    a 3-LC fixture with two `CollectionDefault`-policy LCs and one `Manual`
    LC, it returns exactly the two default-policy display names), then
    spies on `topologyPresetImpact` across an `onTopologyPresetChanged()`
    call and asserts one emission — this fails (`spy.count() == 0`): nothing
    emits it today.
  - **Characterization (scenario 06 shape), passes as written:** a
    `SyncMapping` whose endpoints ("local:orphan-local",
    "acct:cal:orphan-remote") have NO `LogicalCalendar` binding at all.
    Observed: `buildNodes()` creates both backend nodes regardless of
    bindings (it iterates `backends()`, not bindings), so
    `createEdgeForMapping()` finds both nodes and does not skip the mapping;
    the edge IS created and counted (`channelEdgeCount() == 1`,
    `edgeForMapping() != nullptr`). Graffodil logs, for both ends: `node
    '<id>' has no anchor with id '<channel:...>' ... Returning a default
    (0,0) anchor` — no crash, no silent drop, a real edge pinned to the
    node's origin instead of a port. Recorded, not repaired.
  - No regression: `ctest -R 'topology|synctopologywidget'` (16 targets)
    shows the same 3 pre-existing RRD-002-baseline failures
    (`tst_kalbsynctopologydatasource_providers`,
    `tst_synctopologywidget_v2_changeset`, `tst_synctopologywidget_v2_palette`)
    plus this task's new, by-design-failing target; the other 12 targets
    pass unchanged. Full `-j6 -- -k` build: the only compile failure in the
    whole tree is still `tst_backendconfigwidgets` (RRD-001/RRD-002,
    unrelated).
- **Next:** RRD-004.

### RRD-004 — Repair the pinned topology defects

- **State:** DONE 2026-09-09
- **Depends on:** RRD-003
- **Repository:** `../PlanStan`
- **Scope:** Make RRD-003's regressions pass. Decouple "the baseline changed"
  from "the draft is discarded": a rebuild re-snapshots the baseline and retains
  staged edits, reporting any edit dropped because its subject no longer exists.
  Split membership edges from channel edges per specification §2.3.
- **Acceptance:** RRD-003's regressions pass. No staged edit is lost silently; a
  dropped edit is reported with its reason. The full topology suite and the
  RRD-002 baseline are unchanged except for these targets. No new compatibility
  path.
- **Verification:**
  - **D4:** `SyncTopologyWidget::rebuildGraph()` (synctopologywidget.h/.cpp)
    gained a `bool preserveChangeset = false` parameter — the default
    (construction, explicit Discard, a completed Apply) is unchanged
    byte-for-byte; `onTopologyPresetChanged()` now calls
    `rebuildGraph(/*preserveChangeset=*/true)`, which re-snapshots
    `m_baselineMappings` but skips `m_changeset.clear()`/
    `m_previewMappings.clear()`. "Reporting a dropped edit" is N/A for this
    specific call site and not implemented here: a topology-preset change
    alone never invalidates a staged edit's subject (no LC/backend/calendar
    id is touched), so nothing is ever dropped by this path — verified by
    all 10 of RRD-003's D4 slots now passing with their staged edit intact
    and unmodified. The general rebaseline-that-CAN-drop-and-must-report
    mechanism belongs to `TopologyDraft::rebaseline()` (specification §2.2,
    RRD-005), documented as such in `rebuildGraph()`'s own doc comment so a
    future reader doesn't mistake this for that.
  - **D5:** `updateStatusBar()` and both `selectionCleared()` emission sites
    (`rebuildGraph()`, the empty-selection branch of the scene
    selection-changed handler) now report `channelEdgeCount()`
    (`m_edgeToMapping.size()`) instead of `m_edges.size()`. `m_edges` itself
    is untouched (still both membership + channel edges, still what
    `edgeCount()` and the graph rendering read) — only the three
    user-facing count reports changed.
  - **Inherited impact:** `onTopologyPresetChanged()` now emits
    `topologyPresetImpact(collectionDefaultCalendarNames())` before doing
    anything else, including an empty list when no `CollectionDefault`
    calendar exists — RRD-003's spy-based assertion now sees exactly one
    emission with the two expected names.
  - `tests/sync/tst_topology_draft_and_counts.cpp` (RRD-003, unmodified):
    **15/15 pass** (was 12 failed/3 passed before this task).
  - **One pre-existing test needed updating, not just a pass/fail flip:**
    `tst_synctopologywidget.cpp`'s `testLogicalEdgesDerivedFromBindings()`
    asserted `selectionCleared()`'s count arg was `>=1` with all mappings
    cleared (only membership edges left) — true only under the D5 bug it
    was unknowingly depending on. Fixed to assert `widget.edgeCount()`
    instead (unchanged accessor, correctly still `>=1` — a membership edge
    still renders, it's just correctly excluded from the sync-rule count
    now). This is the only test file RRD-004 touched outside
    `synctopologywidget.h`/`.cpp` themselves.
  - Full rebuild (`cmake --build build-dev -j6 -- -k`): the only compile
    failure anywhere is still `tst_backendconfigwidgets` (RRD-001/RRD-002,
    unrelated, unchanged). `ctest -R 'topology|synctopologywidget|sync'
    -j4`: 3 pre-existing RRD-002-baseline failures only
    (`tst_kalbsynctopologydatasource_providers`,
    `tst_synctopologywidget_v2_changeset`, `tst_synctopologywidget_v2_palette`)
    — `tst_collectioncontroller_syncverbs`'s already-documented flakiness
    showed as a pass this run, a fail on the previous RRD-004-in-progress
    run, and a (different subtest) fail in RRD-002's own baseline: three
    different outcomes across three runs, consistent with "flaky," not a
    regression.
  - Full suite (`ctest -j4`, 146 targets — 145 plus RRD-003's new target):
    **133 passed, 12 failed** (1 timeout, 1 build failure, 10 real
    failures) — the exact same 12 named targets as RRD-002's baseline, none
    added or removed, confirming "unchanged except for these targets."
- **Next:** RRD-005.

### RRD-005 — Extract an observable topology draft

- **State:** DONE 2026-09-09
- **Depends on:** RRD-004
- **Repository:** `../PlanStan`
- **Scope:** Introduce `TopologyDraft` and `TopologyEditorContext` per
  specification §2.2. Move baseline, changeset, and preview state out of
  `SyncTopologyWidget`; the widget becomes an observer that projects the draft.
  Behavior-preserving.
- **Acceptance:** `TopologyDraft` is unit-tested with no `QWidget` dependency and
  emits one `changed()` per mutation. The widget holds no pending state of its
  own. The whole topology suite passes unchanged, RRD-003's regressions
  included.
- **Verification:**
  - New `src/sync/topology/topologydraft.h`/`.cpp`: a plain `QObject` (no
    `QWidget`/`QtWidgets` include anywhere in the class itself — confirmed by
    grep; its test binary transitively links `Qt6Widgets` only because it
    links the monolithic `PlanStanCore`, the same as the pre-existing
    `tst_topologychangeset`) owning exactly the three things specification
    §2.2 names: the baseline `SyncMapping` snapshot, a `TopologyChangeset`,
    and the display-only preview mappings. Every one of `TopologyChangeset`'s
    23 mutating methods gets a same-named one-line forward
    (`m_changeset.X(...); emit changed();`); read-only access goes through
    `changeset()` (unchanged `TopologyChangeset` API, not touched or
    reimplemented). `discard()` is the only path that clears the changeset
    (also clears the preview, matching the C1 "dies with the changeset"
    rule) and `rebaseline(newBaseline)` re-snapshots the baseline without
    touching the changeset — the RRD-004 (D4) repair's mechanism, now on the
    class that owns the state instead of a widget method flag.
  - New `tests/sync/tst_topologydraft.cpp` (links only `Qt6::Test` +
    `PlanStanCore`, no `Qt6::Widgets` target link — mirrors
    `tst_topologychangeset.cpp`'s existing widget-free link set exactly):
    **13/13 pass**. Covers a fresh draft's empty state, one `changed()` per
    mutation across mapping/calendar/provider/local-backend/wiring-policy
    edits, `rebaseline()` retaining the changeset, `discard()` clearing both
    changeset and preview, preview set/append/clear, and
    `projectedMappings()`'s baseline+changeset+preview union including the
    C1 dedup-against-real-id rule.
  - `SyncTopologyWidget` now holds a single `TopologyDraft *m_draft`
    (QObject-parented to the widget, constructed first in the initializer
    list before `setupUi()`/`rebuildGraph()` run) in place of the former
    `TopologyChangeset m_changeset`, `QList<SyncMapping> m_baselineMappings`,
    and `QList<SyncMapping> m_previewMappings` members — all three removed.
    All ~86 real (non-comment) call sites across `synctopologywidget.h`/`.cpp`
    were migrated: mutating calls to `m_draft->X(...)`, read-only calls to
    `m_draft->changeset().X(...)`, direct baseline/preview reads to
    `m_draft->baseline()`/`m_draft->previewMappings()`.
    `SyncTopologyWidget::projectedMappings()` is now a one-line forward to
    `m_draft->projectedMappings()` (the duplicated union/dedup logic it used
    to carry moved onto the class that owns the state). `rebuildGraph()`'s
    two paths now call `m_draft->discard()` (default) or nothing
    (`preserveChangeset=true`) followed by `m_draft->rebaseline(...)`, and
    `applyChanges()`/`discardChanges()`'s end-of-function clears now call
    `m_draft->discard()` in place of the former two-line
    `m_changeset.clear(); m_previewMappings.clear();`.
  - **Two narrow additions beyond a pure mechanical move, both one-liners:**
    `TopologyDraft::appendPreviewMapping()` (the widget's copy-preview loop
    appended to `m_previewMappings` one at a time; `TopologyDraft` needed an
    equivalent single-item forward alongside `setPreviewMappings()`) and
    `TopologyDraft::projectedMappings()` (didn't exist on `TopologyChangeset`
    itself since it also needs the preview list, which only `TopologyDraft`
    owns).
  - **Deliberately NOT moved, and NOT in scope by specification §2.2's own
    enumeration** ("Owns the baseline snapshot, the TopologyChangeset, and
    the display-only preview mappings" — nothing else): `m_pendingProviders`
    (provider *add* staging — confirmed during `RRD-003` that
    `stagePendingProvider()` already routes through this separate
    widget-level list, never through `m_changeset`), and
    `m_pendingAdoptTargets`/`m_pendingCopyTargets` (routing indices — which
    LC an already-staged `AdoptCalendarRequest`/`CreateCalendarRequest`
    binds into — not independent staged edits in their own right). "The
    widget holds no pending state of its own" is satisfied for the three
    things this task's scope names; these three remain and are named here so
    a future reader doesn't read the acceptance as broader than it is.
  - **`TopologyEditorContext` (specification §2.2) was NOT introduced.** Its
    entire purpose per the spec's diagram is coordinating multiple owned
    children — `TopologyDraft` *and* `TopologyApplyService` (`RRD-006`,
    doesn't exist yet) *and* multiple observers — `SyncTopologyWidget` today,
    `CalendarsAndSyncPage` (`RRD-016`+, doesn't exist yet) later. Introducing
    a coordinator class today would own exactly one child and coordinate
    exactly one observer: structure with nothing yet to structure, and no
    acceptance case (this task's own acceptance text names only
    `TopologyDraft`) forcing a particular shape for it. Deferred to `RRD-006`,
    which gives it a second thing to own.
  - Full rebuild (`cmake --build build-dev -j6 -- -k`): only the same
    pre-existing `tst_backendconfigwidgets` compile failure
    (`RRD-001`/`RRD-002`, unrelated). `ctest -R
    'topology|synctopologywidget|sync' -j4` (34 targets, including
    `RRD-003`'s `tst_topology_draft_and_counts` and this task's new
    `tst_topologydraft`): same 3 pre-existing `RRD-002`-baseline failures
    only; every other target, both new ones included, passes. Full suite
    (`ctest -j4`, 147 targets = 145 plus `RRD-003`'s and this task's new
    targets): **135 passed, 12 failed** — the exact same 12 named targets as
    `RRD-002`'s baseline, none added or removed.
- **Next:** RRD-006.

### RRD-006 — Extract the apply pipeline

- **State:** DONE 2026-09-10
- **Depends on:** RRD-005
- **Repository:** `../PlanStan`
- **Scope:** Move `SyncTopologyWidget::applyChanges()`
  (`synctopologywidget.cpp:2680-3393`, 713 lines) into `TopologyApplyService`
  with the `review()` and `apply()` contract in specification §2.2. Preserve
  every rule that code encodes, including `owningPolicyAfterApply()` freeze
  semantics, the baseline-clear resolution, and the exclusion of preview
  mappings from persistence.
- **Acceptance:** apply behavior is identical, proven by the existing suite plus
  new service-level tests for accepted, rejected, and partially-invalid
  submissions. A rejected apply leaves the draft, the durable configuration, and
  the runtime unchanged. Runtime ownership is unchanged: apply still submits one
  desired topology through `submitDesiredRuntimeTopology()`.
- **Verification:** the two live data-corruption paths named in that function's
  comments each get a named test. Record both.
- **Result:** `TopologyApplyService`
  (`src/sync/topology/topologyapplyservice.h`/`.cpp`) owns the whole pipeline:
  the cross-logical-calendar convergence gate, the node-level local-backend
  and provider operations, the batched collection-level operations, the
  persist filter, the runtime submission, and the baseline clear. It has no
  `QWidget` dependency and is unit-tested standalone. `applyChanges()` is now
  ~20 lines: build an `Inputs` struct, call the service, then the
  discard/clear/`rebuildGraph`/`dirtyChanged` cleanup that stays on the view.
  Five commits on PlanStan `master`:
  - `96cc561f` — phases 3-6 (batched collection ops, persist filter, runtime
    submission, baseline clear) plus `computeFrozenMappingsForManualFlips()`,
    `owningPolicyAfterApply()`, `compiledMappingIdsAfterApply()`;
    `backendLabelFor()`/`logicalCalendarNameFor()` moved with thin widget
    forwards. Widget dropped 642 lines.
  - `c6680417` — the node-level phase, which aborts on first failure.
  - `45baf331` — the convergence gate and its four anonymous-namespace
    helpers.
  - `cfca2fa7` — `review()`, `collectionDefaultCalendarNames()` moved with a
    widget forward, and `tests/sync/tst_topologyapplyservice.cpp` (14 cases).
  - `625d215e` — a real defect fix, below.
- **Design decisions, recorded:**
  1. **The three widget-private staging members stay on the widget**
     (`m_pendingProviders`, `m_pendingAdoptTargets`, `m_pendingCopyTargets`) —
     option (b) of this record's earlier research note. They are passed in an
     `Inputs` struct holding references, so the service mutates the caller's
     storage in place. That preserves the move-out of `m_pendingProviders`
     into `addProvider()` and its clear-on-failure exactly, with no ownership
     question reopened and no reinterpretation of `RRD-005`'s scoping.
  2. **A fourth coupling the research note did not name:**
     `SyncTopologyWidget::displayNameFor()` reads the graph SCENE
     (`m_backendNodes` ghost and calendar ports), not the data source. Staged
     renames and staged ghosts live there and nowhere else, and it feeds three
     delete-loop error messages plus the remedy button label. Severed with a
     `CalendarNameResolver` callback; the widget installs one forwarding to
     its own method, so those messages stay byte-identical. The service has a
     data-source-only default for hosts that install none.
  3. **The convergence gate needed two more callbacks out.** Its modal
     confirmation is a `ConvergenceRemedyPrompt`; `confirmConvergenceRemedy()`
     is untouched on the widget so both its test hooks (instance and static,
     the latter needed because the load-time check runs from the constructor)
     keep working. Its restyling of the edges it rewrites is the
     `mappingsRewrittenByRemedy(QStringList)` signal, emitted once after the
     rewrite loop and BEFORE the re-validate, matching the original per-mapping
     inline ordering. With no prompt installed the remedy is declined, never
     silently applied.
  4. **`review()` is a structural summary, not an accept/reject prediction** —
     the research note's recommended scope, adopted verbatim.
     `ISyncTopologyDataSource` has no dry-run variant of its mutators, so a
     prediction would be a guess, and the one thing a reviewer must be able to
     trust is that what it lists is really staged. It reports staged provider
     adds (pending and changeset), removals and edits, local-backend adds,
     mapping adds/removes/modifies by id, the six calendar-mutation request
     kinds, mirror requests, the topology change, per-LC wiring-policy edits,
     and the NAMED inherited-impact calendars. It mutates nothing and never
     raises the prompt.
  5. **`TopologyEditorContext` is deferred again, to `RRD-016`.**
     Specification §2.2 puts it above the views owned by
     `CollectionSettingsViewPanel`, but roughly ninety tests construct a bare
     `SyncTopologyWidget`, and those tests are the behaviour-preservation net
     for this extraction. Moving draft ownership out from under them in the
     same change would have destroyed the net that proves the change safe.
     `SyncTopologyWidget` owns both children for now. The context becomes
     necessary when a second OBSERVER exists, which is `RRD-016`'s
     calendars/copies/rules page — not merely when a second child does.
- **Verification:**
  - **The two named data-corruption paths already had named tests**, written
    when the C1 fix landed; this task's contribution is finding and recording
    them rather than writing them. Both are in
    `tests/sync/tst_topology_crud.cpp` and both drive a real `BaselineStore`:
    `policyChipMenu_selectManual_freezesCompiledMappingOnApply()` (its second
    half is the first path — a mapping frozen by a flip to Manual keeps its
    `auto_` id forever, so the old prefix proxy dropped it and cleared its
    baselines on the next unrelated Apply) and
    `addCopyOn_compiledPolicy_secondApplyDoesNotClearChannelBaseline()` (the
    second path — Task 9's add-a-copy channel previews persisted on Apply #1
    and dropped plus baseline-cleared on Apply #2, on a live actively-syncing
    channel). A third,
    `policyChipMenu_rewireAwayFromManual_clearsDiscardedFrozenBaseline()`,
    pins the inverse that the C1 re-review introduced. All three pass before
    and after.
  - **The live Radicale gate is the only automated coverage of the
    concrete-data-source apply path**, and it was run at every step.
    `tst_synctopologywidget_v2_changeset`'s provider cases are part of
    `RRD-002`'s failure baseline, so the default lane does not exercise
    `KalbSyncTopologyDataSource`'s apply at all. `PLANSTAN_LIVE_RADICALE=1
    ctest -R '^live_graph_gate$'` passes in 25-29s against Radicale on
    localhost:5232, driving `applyChanges()` three times through the real data
    source; run and green after each of the five commits.
  - Topology suite (`tst_topology_crud`, `tst_topology_convergence_guard`,
    `tst_topology_dirtystate`, `tst_topology_gestures`, `tst_synctopologywidget`,
    `tst_synctopologywidget_v2_changeset`, `tst_synctopologywidget_v2_palette`,
    `tst_topologydraft`, `tst_topology_draft_and_counts`, plus the new
    `tst_topologyapplyservice`): 8 passed / 2 failed. The two failures carry
    the same eight pre-existing case names as the `RRD-002` baseline, with
    `tst_synctopologywidget_v2_palette` unchanged at 23/2 and
    `tst_synctopologywidget_v2_changeset` at 4/6 (one case added by this task,
    below). None added or removed. The relocated regions were diffed
    line-by-line against their originals at each step; every differing line
    was an intended member-access or epilogue rewrite, and no comment line was
    lost — those comments are the specification for the partial-failure
    policy, the C1 persist filter, and the I5 ordering constraints.
  - Full build clean but for the pre-existing `tst_backendconfigwidgets`
    compile failure (`RRD-001`/`RRD-002`, a stale `CalDavConfigWidget`
    constructor call, unrelated).
  - Full suite (`ctest -j4`, 148 registered targets = 147 plus this task's new
    one): **136 passed, 12 failed**. The 12 are exactly `RRD-002`'s baseline
    set minus `tst_collectioncontroller_syncverbs`, which that baseline itself
    records as flaky and which passed this run. Nothing added, nothing
    repaired outside this task's slice.
- **Defect found and fixed (`625d215e`):** a staged provider was never
  consumed on the runtime apply path. When the data source is a real
  `KalbSyncTopologyDataSource` whose `BackendRegistry` can create the staged
  kind, `canCreateProviderType()` answers true, the direct `addProvider()`
  fallback loop — which clears the staging vector itself — is skipped, and the
  provider reaches the data source only by being read into the desired runtime
  topology's provider set. Nothing cleared it afterwards, while
  `discardChanges()` always had. So `isDirty()` stayed true after a successful
  Apply even though `dirtyChanged(false)` had just been emitted, and the next
  Apply appended the same configuration again on top of a provider list that
  already contained it — `CollectionController::applyDesiredRuntimeTopology()`
  copies that list into the runtime snapshot and stages it for persistence
  when `replaceProviders` is set, so the duplicate reaches both the live
  provider set and the persisted profile. Found by reading during the
  extraction and deliberately carried forward unchanged through the three
  refactor commits so those stayed provably behaviour-preserving. Pinned by
  `tst_synctopologywidget_v2_changeset`'s new
  `apply_pendingProviderOnRuntimePath_isConsumed()`, which asserts the fixture
  really reaches the runtime path rather than the fallback, then that a
  successful Apply leaves the widget clean; verified failing before the fix and
  passing after.
- **Known gap, not closed here:** the runtime submission SUCCEEDING with a
  staged provider is still unpinned in the default lane. A stub CalDAV
  configuration has no server URL, so the runtime declines to connect it and
  the submission reports that through `lastApplyError()` — the best-effort
  accepted-with-an-error disposition. The staging bookkeeping the new test
  pins runs identically either way, but a green end-to-end provider add
  against a real server belongs to the live gate, which currently stages no
  providers. Worth adding when `RRD-007` provisions the project-local DAV rig.
- **Next:** RRD-016 and RRD-020 both depend on this.

### RRD-007 — Provision a project-local DAV test rig

- **State:** DONE 2026-09-10
- **Depends on:** RRD-002
- **Repository:** `../PlanStan`
- **Scope:** A project-local Radicale rig owned by this repository: its own
  config, users, rights file, storage root, and ports, started and stopped by
  script. Three isolated accounts A, B, and C, each on its own instance so that
  stopping one account is a real per-account outage. Never modify
  `/etc/radicale` or the system service on `127.0.0.1:5232`.
- **Acceptance:** one command starts the rig, one stops it, one reports each
  instance's health. Ports and credentials are configuration, not constants
  compiled into a test. A stopped instance produces the account-unavailable
  state in PlanStan, observed and recorded. The system service is untouched and
  unused by any RRD acceptance.
- **Verification:** record the rig's layout, the health-check output, and the
  observed unavailable-state evidence.
- **Result:** `tools/davrig/` — `davrig.py` (a plain Python script; `davrig` is
  its executable wrapper) reads `tools/davrig/rig.json` (checked in: host,
  and per-account port/username/password for A/B/C) and manages three
  independent `radicale --config <generated file>` child processes under
  `tools/davrig/state/<account>/` (gitignored: per-account `config`,
  `users` htpasswd, `collections/` storage root, `radicale.pid`,
  `radicale.log`). `--config` replaces radicale's default config search
  list entirely, so no instance ever reads `/etc/radicale/config`.
  Commands: `davrig start|stop|restart|status [A|B|C]` (omit the account to
  act on all three). `status` PROPFINDs each account's own URL with its own
  basic-auth credentials and reports up/down plus pid, port, and storage
  path.
  Verified 2026-09-10: `davrig start` brought up A/B/C on 127.0.0.1:5301-5303;
  `davrig stop B` left B down while A and C stayed reachable (TCP-verified)
  and the system service on `:5232` kept answering (`curl` 302) with
  `/etc/radicale/config`'s mtime/hash unchanged; `davrig start B` recovered
  it; `davrig stop` (no account) took all three down cleanly.
  `tests/integration/tst_rrd007_davrig_outage.cpp` (target
  `tst_integration_rrd007_davrig_outage`, ctest name
  `rrd007_davrig_outage`, env-gated on `PLANSTAN_DAVRIG=1` plus a PATH
  check for `radicale`, same pattern as `live_graph_gate`) owns the rig's
  full lifecycle and drives a real `Kalburator::Sync::CalDavProvider`
  against account B read from `rig.json` at runtime (no compiled-in
  ports/credentials): connects while B is up (typed
  `ProviderConnectionState::Connected`), stops only B via the script and
  reconnects (`Connected` → `Error`, `isConnected()==false`,
  non-empty `lastError()`, while A/C stay reachable throughout), restarts B
  and reconnects successfully. Command:
  `PLANSTAN_DAVRIG=1 ctest --test-dir build-dev -R rrd007_davrig_outage
  --output-on-failure` — passed in 4.4-4.8s across two runs; a plain
  `ctest` run (no env var) reports it Passed via `QSKIP` in 0.03s, so it's
  safe in normal dev/CI runs where radicale may be absent.
  Observed evidence is written by the test itself to
  `docs/testing/rrd-007-outage-evidence.md` (regenerated, not hand-edited)
  — the recorded transition: B up → `Connected`/`true`/(empty); B stopped
  → `Error`/`false`/`"Failed to discover principal: Connection refused"`;
  B restarted → `Connected`/`true`/(empty).
  `cmake --build build-dev -j6` for `all` still fails on exactly the one
  pre-existing `tests/widgets/tst_backendconfigwidgets.cpp` defect recorded
  in the `RRD-002` baseline (stale `CalDavConfigWidget` constructor call,
  unrelated to this task); no new build failures.
- **Next:** RRD-009.

### RRD-008 — Bundle contract, manifest schema, and guarded generator

- **State:** DONE 2026-09-10
- **Depends on:** RRD-002
- **Repository:** `../PlanStan`
- **Scope:** Implement `tools/fixturegen/`, a small C++ tool over the existing
  Qt, runtime, and assembler seams; scripts orchestrate only. Implement the
  bundle layout, manifest schema, three checkpoints, prefix-and-host-guarded
  reset, and relocatability from specification §2.6. Prove it with a local-only
  smoke scenario needing no DAV. Do not build a parallel configuration parser or
  sync executor.
- **Acceptance:** the tool emits a bundle, prints the exact file to open, and its
  manifest round-trips. Save-and-reopen parse assertions pass against the
  production serializers, not `TestKalbGenerator`. Moving the bundle directory
  and opening it from the new location works. Reset refuses an unknown prefix
  and an unknown host, proven by a test. Two output directories are independent.
- **Verification:** record which loader path actually reads `profileLayout`,
  since the live-fixture JSON puts it at the `.kalb` root while
  `CollectionPaths::layoutFromMetadata()` reads a metadata map.
- **Result:** `profileLayout` verification done by reading the code, not
  guessing: `CollectionWizard::writeKalb()` writes `profileLayout` at the JSON
  **root** (`collectionwizard.cpp:383`); `KalbConfigManager::
  loadCollectionConfigFromFile()` assigns `m_collectionConfig =
  jsonObj.toVariantMap()` for the **whole root object**
  (`libkalcal/models/src/kalbconfigmanager.cpp:53`); every call site of
  `CollectionPaths::layoutFromMetadata()` passes that same `m_collectionConfig`
  (`collectioncontroller.cpp:1560,2211,2271,2309`,
  `collectionruntimedefinitioncompiler.cpp:45`). So the root-level shape is the
  one the real loader takes — a nested `"metadata"` sub-object would never be
  seen. `tools/fixturegen`'s bare-.kalb writer follows the same root-level
  shape.
  `tools/fixturegen/` (CMake target `fixturegen-core`, a static lib, plus the
  `fixturegen` CLI shell over it — linked after `tests/sync` and
  `tests/wizards` in the root `CMakeLists.txt` so it can reuse `TestUtils`):
  `fixturemanifest.{h,cpp}` (manifest schema: run id, scenario, owned local/
  remote resources, `ownedPrefix`/`ownedHosts` guard fields, checkpoints,
  free-form `expected` state — every path relative to the bundle dir, never
  absolute), `fixtureresetguard.{h,cpp}` (`isRemoteResourceDeletable()`:
  refuses unless a resource's id starts with `ownedPrefix` **and** its host is
  in `ownedHosts`; `resetFixture()`: validates every remote resource before
  touching anything, refuses a local path that resolves outside the bundle
  directory, deletes only what the manifest names, never enumerates), and
  `fixturegenerator.{h,cpp}` (`generateSmokeScenario()`: writes a bare nested
  `.kalb`, opens it with a real `SyncTestHarness`-seeded `CollectionController`
  — the same `BackendRegistry`/`ProviderManager`/plugin seeding
  `AppController::seedBuiltinContributions()` does in production — calls the
  real `PlanStan::CollectionAssembler::applySource()` (Local kind, one "Smoke"
  calendar) exactly as the collection wizard's local path does, which itself
  calls `kalbConfigManager()->saveCollectionConfigFile()`; then closes and
  reopens with a **second, independent** harness and refuses to succeed if the
  reopened structural summary doesn't match the just-seeded one — that's the
  "ready" checkpoint and the save/reopen proof in one step). CLI: `fixturegen
  generate [--out-dir DIR] [--scenario NAME] [--run-id ID]` and `fixturegen
  reset --manifest PATH`; `generate` prints the exact `.kalb` path to open.
  Verified manually first (`fixturegen generate` into a scratch dir; inspected
  the emitted `.kalb` — a genuine production-shaped v2 `CollectionSettings`
  document with palette/presets/templates/views, not a stub; moved the run
  directory and confirmed `fixturegen reset --manifest <moved path>/
  manifest.json` still resolved and removed exactly the one calendar
  directory it owned), then pinned in
  `tests/tools/tst_fixturegen.cpp` (target `tst_fixturegen`, part of the
  default `ctest` run, no live service needed): full bundle layout present
  (`providers.kconfig` — explicitly touched since `KConfig::sync()` won't
  materialize an empty file — `calendars/`, `cache/`, `journal/`, `sync.db`,
  `evidence/generate.log`); manifest round-trips through
  `FixtureManifest::writeToFile()`/`readFromFile()`; a **third**, independent
  `SyncTestHarness` (beyond the generator's own two) reopens the emitted
  `.kalb` and asserts the one-local-calendar/one-Primary-binding shape;
  moving the run directory two levels deep and reopening from there still
  works, and the manifest's relative resource path still resolves under the
  new location; two runs into the same `--out-dir` don't collide, and
  resetting one doesn't touch the other's calendar directory; the reset guard
  refuses a resource whose id doesn't start with `ownedPrefix`, refuses one
  whose host isn't in `ownedHosts`, refuses (and touches nothing, proven by a
  surviving sentinel file) a local path that resolves outside the bundle
  directory via `../`, and removes exactly the one directory a manifest names
  while leaving an unlisted sibling untouched. `ctest --test-dir build-dev -R
  tst_fixturegen --output-on-failure` — 11/11 passed (9 test functions +
  init/cleanup), 0.1s. `cmake --build build-dev -j6` for `all` still fails on
  exactly the one pre-existing `tst_backendconfigwidgets.cpp` defect recorded
  in the `RRD-002`/`RRD-007` baseline; no new build failures from this task.
- **Next:** RRD-009.

### RRD-009 — Scenario 01 as a retained openable bundle

- **State:** DONE 2026-09-10 — was BLOCKED, then READY once both blocking
  defects were fixed, released and verified.
  - `../PlanStan/docs/bugs/dispatchruntimerun-sync-stuck-multi-provider-topology.md`:
    a topology Apply published the persistence-filtered mapping list to the
    runtime, leaving a policy-driven collection with zero live channels while
    sync still reported success. Fixed by splitting the operational and
    persistence mapping lists at the PlanStan seam. The same conflation ran
    the other way in `CollectionAssembler` and the mapping-regeneration
    handler, which froze compiler output into the store; fixed with it.
  - `../PlanStan/docs/bugs/caldav-calendar-color-roundtrip-rotates-channels.md`:
    Apple's `#RRGGBBAA` `calendar-color` was read as Qt's `#AARRGGBB`, so a
    colour never matched itself across a round trip and every sync of a
    coloured calendar failed on the resulting property conflict. Fixed here
    in `src/types/csscolor.h`, released as `v1.06`, pinned by PlanStan.
  - Scenario 01 now generates end to end and syncs all four channels,
    including the leg adopted through the real topology widget. What remains
    for this task is its acceptance evidence, not its mechanics.
- **Depends on:** RRD-007, RRD-008
- **Repository:** `../PlanStan`
- **Scope:** Generate `01-everyday.kalb`: Personal L to A, Work L to B, Family
  Hub over L, A and C, and Notes local-only. Four calendars, eight enabled
  bindings, four rules, plus one extra A calendar deliberately left unadopted.
  Install credentials through the real secret service and verify them from a
  second process. Open with a real `CollectionController`, inspect the real
  topology widget, sync, close, and reopen.
- **Acceptance:** a human opens the printed `.kalb` in PlanStan and finds all
  four calendars, their eight bindings and four rules, and the unadopted remote
  calendar. Local-only Notes is valid and raises no spurious warning. Expected
  and observed endpoint tuples are exported with a screenshot. A failure in the
  old graph does not suppress the usable bundle; it is recorded as a defect.
- **Verification:** record the one command that prints the file to open, and the
  second-process credential check.
- **Progress so far, not acceptance:** `tools/fixturegen/` gained
  `davrigconfig.{h,cpp}` (reads `tools/davrig/rig.json` at runtime),
  `davhttp.{h,cpp}` (parameterized MKCALENDAR/PROPFIND seeding, generalizing
  `tst_live_graph_gate.cpp`'s `RadicaleHttp` for the RRD-007 rig),
  `fixturecredentials.{h,cpp}` (`verifyProviderCredential()`: resolves a
  provider's `passwordRef` out of `providers.kconfig` through a **fresh**
  `PlanStan::KWalletSecretStore`, exposed as `fixturegen verify-credential`
  so a caller can shell out to it as a genuinely separate OS process), and
  `FixtureGen::generateScenario01()` in `fixturegenerator.cpp`. The last of
  these: seeds accounts A/B/C on the rig with `planstan-fixture-<run-id>-
  01-everyday-*`-prefixed calendars; installs a real `KWalletSecretStore` as
  `SecretStoreRegistry::defaultStore()` for the process; assembles Notes
  (local-only) and Personal/Work (local+one remote, Mirror) through real
  `CollectionAssembler::applySource()` calls; attaches account C's `Family`
  leg onto the *existing* `Family` logical calendar (already carrying L+A
  from account A's applySource) via the **real topology widget's**
  `SyncTopologyWidget::adoptToLogicalCalendar()` + `applyChanges()` —
  `CollectionAssembler` has no "second remote leg onto an existing LC"
  primitive, so this is the production mechanism for exactly that edit, and
  doubles as the scope's "inspect the real topology widget" step.
  Verified live (`fixturegen generate --scenario 01-everyday` against a
  running `tools/davrig` rig) through the point of writing the `.kalb`:
  direct inspection of the written file confirms 4 logical calendars with
  bindings `(1, 2, 3, 2)` — Notes/Personal/Family/Work, matching the spec
  exactly — and `Family`'s three bindings are `local` + both remote accounts,
  i.e. the Hub-over-three-endpoints shape assembled correctly; the debug log
  additionally confirms `Archive` (account A, deliberately unpicked) renders
  with `ghosts: 1` on its account node. Blocked past that point: the
  scenario's final real sync (`cc->syncNow()`) never completes — see the
  filed defect for the full evidence trail, including a first, now-fixed
  finding (`isSyncInProgress()` can get stuck `true` forever after a
  synchronously-rejected run, worked around in `fixturegenerator.cpp` by
  waiting on `allSyncsFinished` instead) and a second, unconfirmed one (some
  providers' auto-sync-on-load never visibly starts) that this task does not
  attempt to fix.
- **Result:** acceptance met in full. With both blockers gone, `fixturegen
  generate --scenario 01-everyday` against a live `tools/davrig` rig ran to
  completion (exit 0): the runtime's live mapping count was asserted equal
  to the four config-compiled rules before `cc->syncNow()`, the real sync
  then completed and reported success, and closing and reopening with a
  **second, independent** `CollectionController` produced a structural
  summary that JSON-equals the seeded one exactly (4 backends, 4 calendars,
  8 enabled bindings, 4 rules, the same ignored/unadopted key) — asserted by
  the generator itself, which fails the run on any mismatch.
  `fixturegen verify-credential` resolved account A's provider secret
  correctly from a **separate OS process** against the real KWallet-backed
  secret store. The printed `.kalb` was then opened with the real `PlanStan`
  binary on a live display (not offscreen): the "Backends & Calendars" view
  showed all four backend nodes, "4 backends · 4 mappings", the four logical
  calendars each wired `Hub`, and account A's unadopted `Archive` calendar
  rendered greyed out as `Archive (pending)` — not hidden, not an error.
  Screenshotted (active-window capture, to `evidence/rrd009-topology-view.png`
  in the run directory) both mid-sync and after the auto-sync-on-load
  finished; the app was then closed cleanly. No defect was hit on this run,
  so there was nothing to record as a suppressed-but-usable-bundle case.
  Full writeup, including the seeded/reopened JSON tuples, in
  `../PlanStan/docs/testing/rrd-009-scenario01-evidence.md`. The generated
  bundle itself is not retained (regenerable on demand, and `.kalb`/`.kalb.d`
  output is gitignored like every other generated `.kalb`); the rig was
  stopped after the run.
- **Reproduce:** `tools/davrig/davrig start`, then
  `QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1 ./build-dev/tools/
  fixturegen/fixturegen generate --scenario 01-everyday --out-dir /tmp/
  fixture-output`, then `tools/davrig/davrig stop`.
- **Next:** RRD-010 through RRD-013 (blocked, transitively, until this
  clears).

### RRD-010 — Chain relay and mesh scenarios

- **State:** READY
- **Depends on:** RRD-009
- **Repository:** `../PlanStan`
- **Scope:** `02-relay.kalb` (Project, four bindings, ordered Chain across L, A,
  B, C, three rules) and `03-mesh.kalb` (Project, four bindings, Mesh over the
  same four, six rules). Satisfy the five oracle comparisons in specification
  §2.7 in full.
- **Acceptance:** a record created at C reaches L through runtime-owned passes
  from one user Sync now. Head-to-tail and tail-to-head both converge. An
  unavailable middle account is exercised through the RRD-007 rig as a genuine
  per-account outage, not a whole-server stop. Tail-origin data reaches every
  mesh copy without duplication and the following run reports no record changes.
  Concurrent edits produce the expected conflict behavior, recorded exactly.
- **Verification:** expected sets are independent of the mapping generator under
  test. State how each was derived.
- **Next:** RRD-014 and RRD-020.

### RRD-011 — Directional and shared-destination scenarios

- **State:** QUEUED
- **Depends on:** RRD-009
- **Repository:** `../PlanStan`
- **Scope:** `04-directional.kalb` (Bulletin, four bindings, Manual rules A to L,
  L to and from B, L to C) and `06-shared-destination.kalb` (two Manual
  calendars, each with a distinct local primary and its own A or B copy; rules
  L.personal to and from A.personal, L.work to and from B.work, A.personal to
  C.aggregate, B.work to C.aggregate, with `C.aggregate` an existing unbound
  physical endpoint).
- **Acceptance:** one-way rules produce no reverse writes, proven with
  target-only sentinel records. One source's reconciliation does not erase the
  other source's records in the aggregate. Scenario 06 is proven through the
  controller and the real graph; validator permission for one-way fan-in does
  not by itself prove storage semantics or complete endpoint rendering. If 06
  fails, its `.kalb` is retained as a diagnostic artifact with the exact
  expected-versus-actual difference. Initialization and deletion behavior at the
  aggregate is pinned.
- **Verification:** the server-read-only variant of scenario 04 is deferred to
  RRD-015 and is not claimed here.
- **Next:** RRD-014, RRD-018, RRD-021.

### RRD-012 — Component restrictions, properties, and distinct states

- **State:** QUEUED
- **Depends on:** RRD-009
- **Repository:** `../PlanStan`
- **Scope:** `05-properties-and-states.kalb`: Events L to A, Tasks L to B, Hidden
  L to C, and Paused with two bindings and `syncEnabled=false`. Separate
  controlled variants disable a binding, disable a rule, and fail one account.
- **Acceptance:** VEVENT-only and VTODO-only restrictions are established as real
  behavior, not enum or registry presence. Name, color, and description
  differences either sync or are recorded as unsupported. All seven states in
  specification §2.5 are independently observable and independently asserted.
  Any state that cannot be distinguished today is recorded as a defect with its
  reproducer.
- **Verification:** record the observed distinction for each of the seven states
  individually.
- **Next:** RRD-014.

### RRD-013 — Invalid corpus and the safe diagnostic open

- **State:** QUEUED
- **Depends on:** RRD-009
- **Repository:** `../PlanStan`
- **Scope:** Build the invalid corpus, kept separate from the openable one:
  two-way fan-in between different calendars derived from 06; duplicate policy in
  a Mesh cycle; duplicate and reversed mappings; a one-way cycle; missing
  provider or calendar; missing or multiple Primary; duplicate endpoint identity;
  stale discovery; unsupported component and permission combinations; and
  multiple physical calendars on one backend inside a Mesh or Chain.
- **Acceptance:** each invalid file is rejected with the validator's exact
  warning or error category, identifying both offending routes where two exist.
  No disk, runtime, or remote change results from a rejection. Invalid files are
  unreachable through any path that can auto-sync before validation: the
  side-effect-free parser and compiler path and the held-controller admission
  path are exercised first, then a safe diagnostic open workflow is exposed.
- **Verification:** generated mapping IDs currently include backend IDs without
  both calendar IDs. Characterize the collision and admission behavior before any
  support claim; until then the core corpus uses at most one copy per backend per
  calendar.
- **Next:** RRD-015.

### RRD-014 — Mutations and clone-only destructive operations

- **State:** QUEUED
- **Depends on:** RRD-010, RRD-011, RRD-012
- **Repository:** `../PlanStan`
- **Scope:** Run the mutation set against the clean baselines rather than
  multiplying files: create, edit and delete at each eligible origin; disable and
  re-enable a route; disconnect and reconnect; concurrent same-record edits;
  rename and recolor; close and reopen. Conflicting data is injected after the
  baseline so an initial union is never mistaken for a conflict. Seed data stays
  small and uniquely identifiable, with deterministic UIDs, a manifest-selected
  date anchor, timed and all-day events, one timezone- and DST-spanning
  recurrence with a detached exception, VTODO completion and due state, and one
  UID deliberately reused across two unrelated calendars.
- **Acceptance:** recurrence identity survives every mutation, the detached
  exception included. The reused UID stays isolated between its two calendars.
  Cancel and retry, a failed Apply, and held-run topology rejection each leave a
  recorded, correct accepted-versus-pending state. Physical effects are checked
  after each failure, not only the reported status.
- **Verification:** destructive Mirror and reset-and-repush run only on a
  disposable clone, with target-only sentinels, deletion refusal and acceptance
  checks, and no path that can reach a shared account. Record the clone
  mechanism.
- **Next:** RRD-015.

### RRD-015 — The capability matrix

- **State:** QUEUED
- **Depends on:** RRD-013, RRD-014
- **Repository:** `../PlanStan`, `../libkalburator`
- **Scope:** Establish real read-only discovery and enforcement, and real ACL
  behavior, using the RRD-007 rig's rights file. Complete scenario 04's
  server-read-only variant. For transformation loss, demonstrate a lossy backend
  and property combination before asserting that Warn, Abort, or Proceed was
  exercised; local to DAV alone may not trigger loss.
- **Acceptance:** a truthful capability matrix with explicit skip and unavailable
  entries. No supported-baseline claim rests on a UI label, an enum, or a
  registry entry. A fake server may test the application contract, labeled
  separately from real DAV evidence. Each gap becomes a focused upstream defect
  with an executable case.
- **Verification:** publish the matrix and cite the rig configuration that
  produced each row.
- **Next:** RRD-022.

### RRD-016 — Calendars, copies, and rules page as a read-only projection

- **State:** QUEUED
- **Depends on:** RRD-006, RRD-009
- **Repository:** `../PlanStan`
- **Scope:** Build the page as a new first page in the existing
  `CollectionSettingsViewPanel` `KPageWidget`, demoting the graph to a Topology
  page beside it and reusing the existing apply bar, panel lifecycle, and view
  registration. Pure projection: a searchable calendar list; on selection the
  copy table and the rule table; and the arrangement summary line. Both pages
  observe the one draft from specification §2.2.
- **Acceptance:** driven by scenario 01, every value matches the parsed `.kalb`
  and the runtime projection. Counts obey specification §2.3. Names are
  account-qualified when they collide, and multiple copies on one account stay
  distinguishable by calendar ID or path. Physical identity shows both the app
  calendar name and the actual remote collection name. Every row and control is
  reachable and readable with the keyboard alone. No editing path exists yet.
- **Verification:** the current bound-port labels take the logical display name
  and can conceal a different remote name. Record that the new table does not.
- **Next:** RRD-017.

### RRD-017 — Arrangement, copy, primary, and rule editing

- **State:** QUEUED
- **Depends on:** RRD-016
- **Repository:** `../PlanStan`
- **Scope:** Add Change arrangement, Add copy, Use another primary copy, and Edit
  rules, plus an explicit numbered order editor for Chain. Account placement
  never changes chain order. Add copy asks explicitly whether to create a new
  calendar or use an existing one; a nonempty existing destination presents the
  initialization and conflict choice. Apply and Discard stay visible beside the
  editor.
- **Acceptance:** each flow completes with visible controls, no port drag and no
  context-menu discovery. Changing an arrangement that requires Manual shows the
  complete resulting rule set and names every affected inherited calendar.
  Linking never implies an immediate destructive replacement. Every accepted
  change survives reopen; a rejected change preserves the previous runtime and
  durable configuration and keeps a useful draft. Scenario 01 covers setup, 04
  and 06 cover advanced rules. Existing staging, conflict, provider, and
  runtime-host checks stay green.
- **Verification:** record each flow's exact control path and its reopen check.
- **Next:** RRD-018 and RRD-019.

### RRD-018 — Account discovery states and the four removal verbs

- **State:** QUEUED
- **Depends on:** RRD-011, RRD-017
- **Repository:** `../PlanStan`
- **Scope:** Connect account opens a dialog or drawer, replacing the permanently
  space-consuming backend palette. Discovery has loading, empty,
  authentication-failure, unavailable, and stale states, each with a timestamp
  and a retry. Removal offers four distinct verbs in distinct words: stop syncing
  a rule, unlink a copy, remove a calendar from PlanStan, and delete a physical
  calendar.
- **Acceptance:** each discovery state is reachable and observed against the
  RRD-007 rig, a genuinely stopped account instance included. Cached discovery is
  never presented as proof of current permission or existence. Each verb states
  which records remain, which copies are affected, and any primary promotion, and
  its stated consequence matches the observed physical effect. An irreversible
  operation confirms with the exact account and calendar name.
- **Verification:** record the observed effect of each verb on real remote
  records, not only the dialog text.
- **Next:** RRD-022.

### RRD-019 — Truthful run feedback and separated draft, save, and run

- **State:** QUEUED
- **Depends on:** RRD-017
- **Repository:** `../PlanStan`
- **Scope:** The run panel identifies the selected calendars and rules, the
  current pass, and completed, failed, deferred or conflicting, and cancelled
  work, plus the last successful result. Save calendar contents and Sync now are
  separate operations with separate results and do not live inside a movable
  calendar node.
- **Acceptance:** during a draft, Sync now explicitly runs the applied
  configuration or offers Apply first; it never implies draft execution. A
  successful runtime commit advances the applied projection. A failure retains
  the draft and displays the accepted state and any explicit repair requirement.
  Success is never reported because a widget or a JSON file changed. Cancel and
  retry are observable and correct. A configuration preview never claims a
  record-level dry run unless the runtime provides one.
- **Verification:** record the failed-Apply case's accepted-versus-pending state.
- **Next:** RRD-022.

### RRD-020 — Graph focus, groups, layout, legend, and keyboard traversal

- **State:** QUEUED
- **Depends on:** RRD-010, RRD-006
- **Repository:** `../PlanStan`
- **Scope:** Default to the selected calendar's sync graph, with Selected
  calendar and All calendars as an explicit choice. Each copy is a node; account
  grouping is a background container; Primary is a textual node badge, not a
  second family of lines. Membership becomes an optional overlay, off by default.
  Add search, a visible legend, readable labels, a fixed minimum text size,
  node-avoiding routes, and stable deterministic ordering. Add keyboard node and
  edge traversal, and a form equivalent for every gesture.
- **Acceptance:** `applyLayout()` no longer arranges account nodes in one
  horizontal row, no longer special-cases the literal backend ID `primary`
  (primacy is a per-calendar binding role), and implements the ordering its
  comment claims. Zoom changes overview detail rather than shrinking essential
  controls into illegibility; scrolling and panning never require a middle mouse
  button. Editing in one view is observed in the other, proving one draft. Scenes
  02, 03, and 06 verify chain, mesh, and shared destinations. Counts obey
  specification §2.3.
- **Verification:** record the keyboard equivalent for each retained gesture.
- **Next:** RRD-021.

### RRD-021 — Route tracing, cross-calendar warnings, and scale

- **State:** QUEUED
- **Depends on:** RRD-011, RRD-020
- **Repository:** `../PlanStan`
- **Scope:** Add Trace changes from this copy. In the all-calendar view, use
  separate calendar lanes and collapse unrelated account detail. Give shared
  endpoints and cross-calendar routes an explicit warning. Add one scale variant
  with 12 calendars across the existing four backends.
- **Acceptance:** tracing shows configured reachability and says so; it never
  promises that a failed or conflicting run will deliver every record. A trace
  from a chain tail correctly identifies which copies are reachable and which are
  not, matching the observed run outcome for the same configuration. The scale
  variant remains navigable, and its filtering is recorded as adequate or as a
  defect.
- **Verification:** record the trace result beside the run result it is compared
  against.
- **Next:** RRD-022.

### RRD-022 — Measured usability against the stated targets

- **State:** QUEUED
- **Depends on:** RRD-015, RRD-018, RRD-019, RRD-021
- **Repository:** `../PlanStan`
- **Scope:** Repeat a human walkthrough in a normal desktop session and at 2x
  display scaling, at 1280x800, for the old and the proposed interface using the
  same fixtures. Record task completion, time, errors, and required assistance.
  This is a test run with the user, not a certification inferred from screenshots
  or unit tests.
- **Acceptance:** within 30 seconds of opening a scenario the operator identifies
  the displayed and edited copy, the remote copies, the direction, and any
  inactive or unavailable route. Within two minutes they add a copy or change a
  rule using visible controls with no explanation of ports, then review and Apply
  it. They correctly predict whether an event created at C in the chain reaches L
  and whether a change at the publication destination can return to the source.
  They explain and execute stop syncing without deleting remote records and
  distinguish it from a requested physical deletion. They complete the same setup
  with the keyboard alone, preserving focus, selection, and drafts through a
  refresh and a failed Apply.
- **Verification:** record the operator's results for both interfaces on the same
  fixtures, including failures and assistance given.
- **Next:** RRD-023.

### RRD-023 — Campaign closure and the release decision

- **State:** QUEUED
- **Depends on:** RRD-022
- **Repository:** `../PlanStan`, `../libkalburator`, `../libkalcal`
- **Scope:** Collect the final cross-repository revisions, configuration, test
  classifications, retained bundle inventory, capability matrix, and known
  limitations. Decide whether direct graph editing retires, on the basis of
  whether it adds capability the form workflow lacks.
- **Acceptance:** no open task and no uncertified workflow is hidden by a
  completion claim. Every skipped, live, or environmental check is labeled. The
  retained known-issues list contains only explicit non-release limitations.
  Finished means correct observable sync outcomes and repeatable unassisted task
  completion; neither alone closes the campaign.
- **Verification:** full ledger and DAG review, source and diff checks, and a
  recorded cross-repository verification matrix.
- **Next:** none. This closes the campaign.

## Stabilization completion tasks

These task records turn the A–L stabilization deliverables into the finite
completion queue above.  The PlanStan architecture document supplies the
source-entry and fixture map; each record below states the non-negotiable
acceptance boundary so an agent can take one bounded slice without relitigating
the programme.

### STB-001 — Make runtime topology application transaction-safe

- **State:** DONE 2026-09-07
- **Depends on:** AUD-003
- **Repository:** `../libkalburator`, with PlanStan host evidence as needed
- **Scope:** Baseline slice B. Validate a whole proposed topology before any
  external effect; define safe behavior for submission during a run; preserve
  executor affinity and reject reentrant topology/run/provider mutation.
- **Acceptance:** malformed later input leaves no earlier durable, published, or
  physical effect. A second fallible mutation after a successful first one either
  restores the committed state or publishes an explicit compensation/repair
  requirement. A held backend run cannot have its executor replaced or freed.
  Tests cover held-run, reentrant-command, failed compensation, configuration,
  published topology, and physical effects—not only return values.
- **Verification:** focused libkalburator runtime topology tests plus affected
  PlanStan topology-adapter/host tests. Record the exact failing case before a
  behavior change and the remaining acceptance case afterward.
- **Progress (2026-09-07):** added
  `topologyReplacementIsRejectedWhileRunIsActive` to the runtime contract. It
  held a delayed mapped run, then reproduced executor replacement during that
  run (the unguarded test hung after the replacement). `applyTopology()` now
  rejects admission before any provider, endpoint, durable, or physical effect
  while `m_running` is true. The focused test passes. Added
  `invalidLaterCollectionMutationHasNoEarlierPhysicalEffect`: it reproduced a
  create reaching the backend before a later id-less mutation was rejected.
  `applyTopology()` now prevalidates the entire mutation batch's endpoints,
  ids, capability requirements, and rename destination before durable or
  physical effects; the focused test and full runtime contract pass. Remaining
  acceptance: `failedLaterCollectionMutationCompensatesEarlierCreate` proves a
  second valid physical failure deletes the first create without a false repair
  claim, and `reentrantTopologyCommandIsRejectedDuringCommitNotification`
  proves nested topology admission is rejected. Full runtime contract passes.
  Factory-created physical mutations and compensation now dispatch through
  their `BackendExecutor`; the compensation fixture records both calls on the
  executor thread. Provider-owned backends now have the same narrow executor
  dispatch capability through `ProviderManager`, without exposing ownership.
  `reentrantRunAndProviderCommandsAreRejectedDuringTopologyCommit` rejects
  nested run and provider commands. The refreshed full runtime contract passes
  after a clean Qt 6.11.2/toolchain rebuild.
- **Result:** topology admission is now transaction-safe for the bounded
  contract: active work, malformed later mutations, later physical failures,
  executor affinity, and nested command admission have executable coverage.
- **Verification result:** built `tst_planstan_runtime_contract`; focused
  held-run, invalid-batch, compensation/affinity, topology-reentrancy, and
  run/provider-reentrancy tests pass; full
  `ctest --test-dir build -R '^tst_planstan_runtime_contract$'
  --output-on-failure` passes 1/1 after clean regeneration.
- **Next:** STB-004 once durable runtime topology safety is demonstrated.

### STB-002 — Acknowledge only the submitted edit revision

- **State:** DONE 2026-09-07
- **Depends on:** AUD-003
- **Repository:** `../PlanStan`, `../libkalburator`
- **Scope:** Baseline slice C. Add a submission identity/revision boundary across
  staging, persistence completion, and journals; retain PlanStan ownership of
  edit drafts and recovery.
- **Acceptance:** hold save A, make edit B, finish A: B remains dirty and
  recoverable. Failure, partial success, retry, close/reopen, and recovery keep
  the correct disk contents, in-memory state, and journal suffix; retries neither
  duplicate mutations nor truncate a newer revision.
- **Verification:** delayed-write, multi-calendar, success/failure, and restart
  fixtures from `StagingController` and journal test entry points.
- **Progress (2026-09-07):** C1 implemented: `StagingController` detaches an
  immutable in-flight submission, retains later edits in live staging, and
  acknowledges only the matching append-only journal prefix. `CalendarJournal`
  now exposes `discardPrefix()`. `syncFailed` restores the held submission for
  retry rather than falsely acknowledging it; `LocalBackend` emits it for
  asynchronous write failures. Characterization covers held A → staged B → A
  completion → B-only retry and durable B suffix. C2 coverage now includes
  failure restoration/retry, held multi-calendar completion (no prefix is
  retired until every calendar completes), and close/reopen recovery of B only.
- **Result (2026-09-07):** Accepted local save batches are immutable while in
  flight. `syncCompleted` retires their journal prefix only after every
  dispatched calendar reports completion; `syncFailed` restores the full batch
  ahead of later edits, with the journal intact. Local asynchronous write
  failures now signal `syncFailed` rather than success.
- **Verification (2026-09-07):**
  `cmake --build build-dev --target tst_stagingcontroller_acknowledgment
  tst_journalrecoverycoordinator -j2`; `ctest --test-dir build-dev -R
  '^(tst_stagingcontroller_acknowledgment|tst_journalrecoverycoordinator)$'
  --output-on-failure` (2/2 passed); `cmake --build build --target
  tst_calendarjournal -j2`; `ctest --test-dir build -R '^tst_calendarjournal$'
  --output-on-failure` (1/1 passed). PlanStan `078b17e9`, libkalburator
  `ba1fc59`; local PlanStan build uses
  `-DPLANSTAN_LIBKALBURATOR_SOURCE_DIR=/home/clinton/dev/libkalburator`.
- **Next:** STB-003 exact calendar/UID/recurrence/occurrence identity.

### STB-003 — Preserve exact record and occurrence identity

- **State:** DONE 2026-09-07
- **Depends on:** AUD-003
- **Repository:** `../PlanStan`, `../libkalcal`
- **Scope:** Baseline slice D. Carry existing calendar, UID, recurrence ID, and
  generated-occurrence-start identity through mutation, staging, model,
  selection, drag, and editor paths.
- **Acceptance:** deleting a detached exception preserves its master and
  siblings; equal UIDs in two calendars are independent; a selected third
  occurrence survives an unrelated model update and still targets that
  occurrence. Add exact tests before replacing UID-only behavior.
- **Verification:** incidence mutator, recurrence editing, persisted-reopen, and
  agenda selection fixtures.
- **Progress (2026-09-07):** Agenda selection/reload now keys rendered
  occurrences by `(UID, calendarId, occurrenceStart)` rather than selecting the
  first UID/calendar match. The view also projects occurrenceStart into
  `IncidenceRef` and detects same-UID selection changes. Characterization test
  selects the third generated daily occurrence, mutates unrelated model data,
  reloads, and verifies that exact occurrence remains selected. PlanStan
  `223a2470`, libkalcal `2324d24`; `ctest --test-dir build-dev -R
  '^tst_agenda_selection_identity$' --output-on-failure` passed 1/1.
- **Progress (continued 2026-09-07):** recurrence-aware staged deletion now
  reaches the backend as the existing `uid + recurrence-id` record identity,
  rather than collapsing to the master's bare UID. The held-backend fixture
  asserts that a detached exception submits only its composite identity.
  PlanStan `18532712`; focused staging and agenda tests pass 2/2.
- **Result (2026-09-07):** Stored incidence identity remains `(calendarId,
  UID, recurrenceId)`; detached exception deletion is dispatched using the
  corresponding composite record identity. Agenda view state adds generated
  occurrence start, so a reload restores the selected occurrence rather than
  an arbitrary same-UID item.
- **Verification (2026-09-07):** `ctest --test-dir build-dev -R
  '^(tst_globalincidencemodel|tst_incidencemutator|tst_stagingcontroller_acknowledgment|tst_agenda_selection_identity)$'
  --output-on-failure` (4/4 passed); `ctest --test-dir build-dev -R
  '^integration_recurrence_editing$' --output-on-failure` (1/1 passed).
  PlanStan `18532712`, `223a2470`; libkalcal `2324d24`.
- **Next:** STB-004 durable production desired-state commit.

### STB-004 — Commit production desired state once

- **State:** DONE 2026-09-08 (reopened acceptance repaired)
- **Depends on:** STB-001
- **Repository:** `../PlanStan`, `../libkalburator`
- **Scope:** Baseline slice E. Supply initial runtime definitions with a concrete
  durable `TopologyPersistenceParticipant`; route settings and wizard changes
  through the runtime topology adapter and project only accepted state.
- **Acceptance:** in the real host fixture, change a binding, inspect the
  runtime mapping, run, close, and reopen with the accepted definition intact.
  A refused or failed commit leaves UI, disk, and runtime consistent.
- **Verification:** definition compiler, topology adapter/data-source, wizard,
  and real controller-host tests for initial open and later edits.
- **Progress (2026-09-07):** Production `CollectionController` initialization
  now supplies a concrete `TopologyPersistenceParticipant` backed by the open
  `KalbConfigManager`: prepare rejects a missing configuration, commit writes
  the `.kalb` atomically, and rollback clears its prepared state. The real
  temporary-collection host fixture proves initial runtime topology installation
  rewrites the compact fixture via that participant. PlanStan `e5fc2913`;
  `ctest --test-dir build-dev -R '^tst_collectioncontroller_runtime_host$'
  --output-on-failure` passed 1/1.
- **Result (2026-09-08):** Later topology submissions through the real
  `KalbSyncTopologyDataSource` now recompile controller-owned endpoint facts
  and invoke the installed `CollectionRuntime`. Its shared participant stages
  `SyncTopology` and mappings in `KalbConfigManager`, saves only at runtime
  commit, and restores the prior projection on rollback. The production host
  fixture accepts a Mirror topology, verifies the persisted file and reopened
  controller agree, then rejects a missing-endpoint mapping and proves the
  accepted config/file remain unchanged. PlanStan pending commit.
- **Verification (2026-09-08):** `cmake --build build-dev --target
  tst_collectioncontroller_runtime_host tst_kalbsynctopologydatasource -j4`
  passed; `ctest --test-dir build-dev -R
  '^(tst_collectioncontroller_runtime_host|tst_kalbsynctopologydatasource)$'
  --output-on-failure` passed 2/2.
- **Next:** STB-005 production run policy and triggers.

### STB-005 — Wire production run policy and triggers

- **State:** DONE 2026-09-08
- **Depends on:** STB-004
- **Repository:** `../PlanStan`, `../libkalburator`
- **Scope:** Baseline F1: manual/automatic requests, skip/concurrency policy,
  deletion confirmation, policy updates during a run, and progress projection.
- **Acceptance:** user-facing entry points produce runtime requests; configured
  policy reaches the running engine; rejected policy/deletion has no physical
  effect and errors remain actionable.
- **Verification:** controller sync-verb/progress/mass-delete tests extended to
  real controller composition.
- **Result (2026-09-08):** `CollectionController` now compiles the real
  `AppSettings` concurrency, skip-unchanged, monitored/background interaction,
  and existing default-deny `SyncMassDeleteGuard` callback into its runtime
  policy. Later settings changes use `CollectionRuntime::updatePolicy()` and
  emit actionable errors when runtime admission rejects them. The production
  host fixture rejects zero concurrency and then proves its previous runtime
  policy still accepts a run. PlanStan pending commit.
- **Verification (2026-09-08):** `cmake --build build-dev --target
  tst_collectioncontroller_runtime_host -j4` and `ctest --test-dir build-dev
  -R '^tst_collectioncontroller_runtime_host$' --output-on-failure` passed
  1/1.
- **Next:** STB-006 credential reference materialization.

### STB-006 — Wire credential reference materialization

- **State:** DONE 2026-09-08
- **Depends on:** STB-004
- **Repository:** `../PlanStan`, `../libkalburator`
- **Scope:** Baseline F2: factory secret resolution and plaintext-migration
  contract using a fake secret service, including missing credentials.
- **Acceptance:** endpoint construction resolves references without plaintext
  persistence; migration and missing-secret failures are explicit and actionable.
- **Verification:** factory plus real controller credential fixtures.
- **Result (2026-09-08):** The production `PlanStanBackendFactory` is covered
  with `InMemorySecretStore`: a DecSync Syncthing endpoint materializes from
  `syncthingApiKeyRef` without exposing the key, a missing reference fails with
  an actionable resolution error, and a plaintext key remains rejected.
  Existing controller load migration replaces legacy plaintext with a stored
  reference before backend construction. PlanStan pending commit.
- **Verification (2026-09-08):** `cmake --build build-dev --target
  tst_planstan_local_runtime_integration -j4` and `ctest --test-dir build-dev
  -R '^tst_planstan_local_runtime_integration$' --output-on-failure` passed
  1/1.
- **Next:** STB-007 account and discovery workflows.

### STB-007 — Wire account and discovery workflows

- **State:** DONE 2026-09-08
- **Depends on:** STB-004
- **Repository:** `../PlanStan`, `../libkalburator`
- **Scope:** Baseline F3: connect account wizard/settings and discovery
  consumers to production adapters for refresh, cancellation, removal, and
  failure.
- **Acceptance:** discovered collections become usable bindings and every
  user-facing command/result is truthful; adapter-only coverage is insufficient.
- **Verification:** account/discovery adapter and production UI-entry fixtures.
- **Result (2026-09-08):** Runtime provider-state events now populate their
  typed `providerId`, `CollectionRuntimeDiscoveryAdapter` is connected to the
  controller event sink, and account settings refreshes on that projection.
  The topology persistence participant now writes the desired provider sidecar
  at runtime commit and restores it if the `.kalb` write fails. Controller
  account add/edit/remove commands build desired provider sets with
  `CollectionRuntimeAccountAdapter` and submit them through the runtime before
  updating the UI-side provider lifecycle. PlanStan `6d4f7326`, `c45ddc1c`,
  `8f8658c2`, `e0912d34`, `21d67584`, `31f0616a`, `34b8edc9`,
  `6d2c6010`, `b7c22ee5`, `0ce048bd`, `6c27eea4`; libkalburator `cb2425e`.
- **Verification (2026-09-08):** `ctest --test-dir build-dev -R
  '^(tst_collection_runtime_discovery_adapter|tst_collection_runtime_account_adapter|tst_collectioncontroller_runtime_host)$'
  --output-on-failure` passes its focused coverage (the controller-host and
  both adapter tests). The controller-host rejects an unavailable provider
  contribution without creating/replacing its sidecar, and injects an
  in-process provider contribution to prove runtime add, durable sidecar,
  clean removal, discovered collection projection, and logical-calendar
  creation. It also removes that bound provider, proving the logical-calendar
  cascade is staged in the same transaction so compilation never observes the
  removed provider's old binding.
- **Next:** STB-008 conflict interaction and terminal results.

### STB-008 — Wire conflict interaction and terminal results

- **State:** DONE 2026-09-08
- **Depends on:** STB-004, STB-005
- **Repository:** `../PlanStan`, `../libkalburator`
- **Scope:** Baseline F4: `CollectionRuntimeConflictAdapter`, conflict dock, and
  runtime event/future interaction.
- **Acceptance:** progress, cancellation, exactly one terminal result, and
  initial-sync completion are truthful; monitored pause/resume is distinguished
  from next-run background deferral.
- **Verification:** migrate conflict workflow tests to production composition.
- **Result (2026-09-08):** `CollectionController` now routes runtime conflict
  resolution through `CollectionRuntimeConflictAdapter`; the dock accepts a
  runtime-only custom merge and leaves a rejected runtime decision unresolved
  rather than announcing success. `syncRunFinished` now emits one aggregate
  terminal result per dispatched runtime run, while mapping completion remains
  on `runtimeMappingFinished`. The production host fixture creates a real
  AskUser local conflict, observes its runtime-owned backlog/event, and
  resolves it through that controller boundary. PlanStan `c8abedda`,
  `3f13a06a`.
- **Verification (2026-09-08):** `cmake --build build-dev --target
  tst_collectioncontroller_runtime_host tst_collection_runtime_conflict_adapter
  tst_collection_runtime_run_adapter -j1` and `ctest --test-dir build-dev -R
  '^(tst_collectioncontroller_runtime_host|tst_collection_runtime_conflict_adapter|tst_collection_runtime_run_adapter)$'
  --output-on-failure` passed 3/3. The host fixture asserts exactly one public
  terminal result for an explicit runtime run and performs the local conflict
  backlog/resolution workflow. The focused runtime cancellation contract is
  carried forward to STB-010; deferred runtime resolution is deliberately a
  next-run decision rather than a legacy in-run resume.
- **Next:** STB-009 centralized occurrence query.

### STB-009 — Centralize the calendar occurrence query

- **State:** DONE 2026-09-08
- **Depends on:** STB-003
- **Repository:** `../libkalcal`, `../PlanStan`
- **Scope:** Baseline slice G: fixture corpus and one range/exception query used
  by agenda, month, year, and schedule views.
- **Acceptance:** overnight recurrence, moved/deleted exceptions, duplicate
  calendar UIDs, all-day ends, DST, and timezones yield the same occurrence
  identities and intervals in every view without unbounded expansion.
- **Verification:** shared Kalcal fixtures and view selection/edit behavior.
- **Result (2026-09-08):** The shared `IncidenceDateUtils::occurrencesInRange()`
  now looks back by an occurrence duration and filters by the actual interval,
  so an overnight or multi-day recurrence which started before the visible
  range is returned to every current consumer. PlanStan added the focused
  consumer test `9331e5be`; libkalcal `47657ec`. It now also accepts a
  calendar-scoped incidence set, replaces generated master occurrences with
  detached `RECURRENCE-ID` exceptions, and is the sole expansion path for
  agenda, month, year, range-agenda, and schedule views. PlanStan `75ed1543`,
  `b3d826d3`; libkalcal `7d1264b`, `10d60d2`.
- **Verification (2026-09-08):** `cmake --build build-dev --target
  tst_incidencedateutils -j1` and `ctest --test-dir build-dev -R
  '^tst_incidencedateutils$' --output-on-failure` passed 1/1. After the
  batch migration, `cmake --build build-dev --target kalcal-calendar-views
  tst_incidencedateutils -j1` passed and `ctest --test-dir build-dev -R
  '^(tst_incidencedateutils|tst_agenda_selection_identity)$'
  --output-on-failure` passed 2/2. The corpus covers overnight recurrence,
  moved detached exceptions, duplicate UIDs across calendars, all-day end
  exclusivity, and an America/Toronto DST transition.
- **Next:** STB-010 runtime cancellation and teardown safety.

### STB-010 — Make runtime cancellation and teardown safe

- **State:** DONE 2026-09-08
- **Depends on:** STB-001
- **Repository:** `../libkalburator`, `../PlanStan`
- **Scope:** Baseline H1: held/failed backend cancellation, executor dispatch,
  and bounded worker teardown.
- **Acceptance:** every operation terminalizes once; no callback reaches a
  released runtime/session; supported workers meet a measured shutdown bound;
  backend limitations are stated rather than hidden.
- **Verification:** focused runtime destruction/worker tests and sanitizer checks
  for reproduced lifetime paths.
- **Result (2026-09-08):** Current `CollectionRuntime` cancellation reaches the
  engine, active watcher, and every active external-resource lease; its
  destructor pumps queued cancellation delivery for at most one second, then
  terminalizes the public promise once before stopping owned workers and
  disconnecting the watcher. No acceptance failure reproduced against the
  current implementation, so no speculative teardown rewrite was made.
- **Verification (2026-09-08):** `cmake --build build --target
  tst_engine_cancellation tst_engine_single_mapping_cancel
  tst_planstan_runtime_contract -j1` and `ctest --test-dir build -R
  '^(tst_engine_cancellation|tst_engine_single_mapping_cancel|tst_planstan_runtime_contract)$'
  --output-on-failure` passed 3/3 (5.14 seconds), covering held cancellation,
  immediate next-run admission, and active public-runtime teardown.
- **Next:** STB-011 one operational runtime owner.

### STB-011 — Transfer operational ownership to one runtime

- **State:** DONE 2026-09-08 (reopened acceptance repaired)
- **Depends on:** STB-002, STB-003, STB-004, STB-005, STB-006, STB-007, STB-008, STB-010
- **Repository:** `../PlanStan`, `../libkalburator`
- **Scope:** Baseline O. Trace production load/save/query/account/sync instances,
  add only the smallest missing typed contract, route the complete live slice to
  `CollectionRuntime`, and remove redundant live construction in the same patch.
- **Acceptance:** real load → edit → save → sync observes one operational backend
  owner per endpoint, revision acknowledgment is never bypassed, and no
  application executor races the runtime owner.
- **Verification:** extend the real host fixture with instance accounting and C
  completion controls; run affected consumer integration tests.
- **Progress (2026-09-08):** starting with PlanStan host fixture and
  `CollectionController` instance tracing to identify which production paths still
  construct or hold `SyncBackend`/`BackendExecutor` instances outside the runtime.
- **Progress (continued 2026-09-08):** added the O1 bridge contract
  `CollectionRuntime::backendObject(endpointId)` so the host can reach a
  runtime-owned backend without taking a second ownership share. The production
  host fixture now asserts that after load the source and target endpoints are
  reachable as `Kalburator::Sync::SyncBackend` objects, the runtime is installed,
  and the legacy characterization graph is still not constructed.
- **Progress (finalized 2026-09-08):** removed all live backend/executor ownership
  from `CollectionController`. Deleted `loadAndCreateBackends()`,
  `createBackendFromConfig()`, `adoptBackendExecutor()`, `stopBackendExecutors()`,
  the `m_backendExecutors` member, and the `BackendExecutor` forward declaration.
  The controller now migrates legacy secrets and loads providers via
  `migrateBackendSecretsAndLoadProviders()`. `m_backends` is a non-owning cache
  refreshed from `CollectionRuntime` through `refreshBackendCacheFromRuntime()` and
  `runtimeBackend()`; `backend()`, `backendById()`, `backendForCalendar()`, and
  `backends()` read from this cache. `initializeSyncInfrastructure()` is idempotent
  and is called from both `startDiscoveryAndSync()` and `mirrorProviderBackends()`;
  it refreshes the cache, sets backend-local DB/cache paths, and regenerates sync
  mappings without installing a second runtime. `instantiateBackendInSession()` now
  re-applies the runtime topology through `applyDesiredRuntimeTopology()`.
  `generateSyncMappingsFromLogicalCalendars()` deduplicates by mapping id to avoid
  duplicate topology entries, and the `RunFinished` event sink emits
  `runtimeConflictChanged` for deferred runtime conflicts. The runtime event sink is
  cleared in the controller destructor before the runtime is destroyed.
- **Verification (finalized 2026-09-08):** the runtime/adapter/sync-host lane now passes
  10/10 after building the previously unbuilt targets: `tst_planstan_local_runtime_integration`,
  `tst_collectioncontroller_runtime_host`, `tst_collection_runtime_definition_compiler`,
  `tst_collection_runtime_event_projector`, `tst_collection_runtime_run_adapter`,
  `tst_collection_runtime_conflict_adapter`, `tst_collection_runtime_account_adapter`,
  `tst_collection_runtime_discovery_adapter`, `tst_collection_runtime_topology_adapter`,
  and `tst_synchostsmoke`. The host fixture confirms that the runtime owns the
  backends and the legacy characterization graph is not constructed.
- **PlanStan commit:** `fdf413e3`.
- **Follow-up verified (2026-09-08):** the earlier recurrence-editing crash no
  longer reproduces after the ownership transfer: `integration_recurrence_editing`
  passed in 29.68 seconds during STB-013 certification.
- **Reopened acceptance (2026-09-08):** with `PLANSTAN_LIVE_RADICALE=1`, both
  `live_fanout_gate` and `live_graph_gate` abort while reopening their
  provider-backed collection: `CollectionRuntime::applyTopology()` reports
  `provider did not connect: <uuid>`. The initial standalone provider does
  connect and discovers Radicale calendars; the failure is the subsequent
  runtime-owned provider composition. This contradicts the one-owner
  production acceptance and blocks STB-013 until the provider lifecycle is
  owned by the runtime without a parallel controller composition.
- **Reopened acceptance result (2026-09-08):** `CollectionController` now
  obtains provider and backend observations only from `CollectionRuntime`,
  persists provider intent as configuration rather than composing a second
  controller-owned manager, and refreshes its borrowed backend cache after a
  runtime topology commit. Runtime provider replacement waits for an
  in-flight reconnect to reach a terminal state. `RemoteCalendarBackend`
  remains owned by its `BackendExecutor` but executes on its creator event
  loop: KDAV uses a main-thread-affine network manager, so moving that backend
  to the private executor thread produced `QObject::setParent` violations and
  stranded a reconnect fetch. The topology graph now projects a successfully
  runtime-adopted provider collection into PlanStan only after the runtime
  transaction commits. Verified at PlanStan `83229ba6316e636ce5a64f961ef2fd3543203cc8`
  against libkalburator `cdd2ffc501ddc344f250e215cf8216ba86a9cbd0` with
  `cmake --build build-dev --target tst_planstan_local_runtime_integration
  tst_collectioncontroller_runtime_host -j4` and `ctest --test-dir build-dev
  -R '^(tst_planstan_local_runtime_integration|tst_collectioncontroller_runtime_host)$'
  --output-on-failure` (2/2), opt-in `live_fanout_gate` (1/1), and the
  `live_graph_gate` provider reopen/adoption case (3 passed, 0 failed). The
  full graph gate still has independent chain-fixpoint and destructive-mirror
  behavioral assertions; those are STB-013 workflow certification cases, not
  the reopened provider-composition acceptance.

### STB-012 — Integrate application close as a lifecycle transaction

- **State:** DONE 2026-09-08
- **Depends on:** STB-011
- **Repository:** `../PlanStan`, `../libkalburator`, `../libkalcal`
- **Scope:** Baseline H2: stop command admission, detach consumers, cancel/drain,
  terminalize, then release resources in defined order during application close.
- **Acceptance:** deterministic close during fetch, save, conflict prompt,
  discovery, and queued model update leaves no callback targeting a closed
  session and has one terminal result per operation.
- **Verification:** controller lifecycle, view-release, and close-during-work
  fixtures with targeted sanitizer coverage.
- **Result (2026-09-08):** `MainWindow::closeCollection()` unregisters views,
  releases long-lived panels and disconnects controller consumers before asking
  `AppController` to destroy the collection. Controller teardown cancels and
  detaches the runtime event sink before destroying the runtime, then emits the
  session-release boundary once while the session's borrowed collection/model/
  config/undo pointers remain valid. The runtime's active-run terminalization
  is covered by STB-010's public contract.
- **Verification (2026-09-08):** `cmake --build build-dev --target
  tst_collectioncontroller_lifecycle tst_appcontroller_services -j1` and
  `ctest --test-dir build-dev -R
  '^(tst_collectioncontroller_lifecycle|tst_appcontroller_services)$'
  --output-on-failure` passed 2/2. The lifecycle assertion is PlanStan
  `3c590f84`; STB-010's active runtime teardown contract passed 3/3.
- **Next:** STB-013 desktop workflow certification.

### STB-013 — Certify real desktop workflows

- **State:** DONE 2026-09-08
- **Depends on:** STB-009, STB-012
- **Repository:** `../PlanStan`, `../libkalburator`, `../libkalcal`
- **Scope:** Baseline I: local and hermetic-DAV end-to-end workflow matrix plus a
  short real GUI walkthrough.
- **Acceptance:** model, disk, runtime, dirty state, and visible error state
  agree for open; edit/delete/undo; edit during save; recovery; account/discovery;
  topology; policy sync; conflict; recurring occurrence; and close during work,
  including failure/retry boundaries.
- **Verification:** update TST-003/FTR-001 with exactly executed workflows and
  label opt-in live-service checks separately.
- **Progress (2026-09-08):** at PlanStan
  `83229ba6316e636ce5a64f961ef2fd3543203cc8` against libkalburator
  `cdd2ffc501ddc344f250e215cf8216ba86a9cbd0`, the configured hermetic
  integration target and integration-label CTest lane completed 11/11 in
  146.30 seconds. It covers recurrence editing, incidence CRUD/undo,
  collection lifecycle/switching, rescheduling, templates, multi-edit, and
  view/sidebar flows. Opt-in Radicale evidence remains separate: fanout passes
  (1/1), and the destructive-mirror graph case now passes (3 passed, 0 failed)
  after `CollectionRuntimeRunAdapter` preserves a one-mapping mirror override
  as the runtime mirror intent and direction instead of silently issuing a
  normal two-way run. The Chain graph case now explicitly uses the real
  per-edge `SourceWins` policy workflow before warm-up; editing an automatic
  edge intentionally freezes the current linear Chain as persisted Manual
  mappings, and the test's existing detach-confirmation hook makes that user
  decision noninteractive. The runtime state projection now becomes active
  synchronously upon accepted admission, while its public started signal still
  comes only from the runtime event. The live helper waits for a stable idle
  period, preventing a just-admitted auto-sync from being mistaken for a safe
  topology-mutation boundary. `live_graph_gate` now passes 5/5 in 30.70
  seconds, covering provider adoption, the frozen Chain's tail-to-head
  fixpoint convergence, and destructive mirror/reset behavior in one process.
  Focused
  noninteractive follow-up:
  `tst_collectioncontroller_runtime_host
  productionRuntimeConflictBacklogResolvesThroughController -silent` passed
  (3 passed, 0 failed), and `tst_integration_collection_lifecycle`'s invalid
  open, missing-backend, reopen-after-close, and rapid-open-close cases passed
  (6 passed, 0 failed). The originally reported recurrence integration crash
  does not reproduce: `integration_recurrence_editing` passed 20/20 in 41.59
  seconds offscreen. Staged-save acknowledgement and journal recovery passed
  2/2; production provider sidecar/runtime commit and rejected-policy paths
  passed 4/4 through `tst_collectioncontroller_runtime_host`. Its accepted
  topology, runtime-owned backend observation, and production run paths also
  passed 5/5. Final combined noninteractive command:
  `ctest --test-dir build-dev -R '^(tst_collection_runtime_run_adapter|tst_collectioncontroller_runtime_host|tst_stagingcontroller_acknowledgment|tst_journalrecoverycoordinator|integration_recurrence_editing)$' --output-on-failure`
  passed 5/5 (23.59 seconds), followed by
  `ctest --test-dir build-dev -R '^integration_collection_lifecycle$'
  --output-on-failure` (1/1, 4.17 seconds). Final refreshed lane:
  `PLANSTAN_LIVE_RADICALE=1 ctest --test-dir build-dev -L integration
  --output-on-failure` passed 11/11, including `live_fanout_gate` and
  `live_graph_gate` (146.30 seconds aggregate test time).
- **Manual GUI walkthrough (2026-09-08):** launched `build-dev/PlanStan` on
  the available Wayland/X11 desktop with a disposable copy of
  `tests/fixtures/test-collection.kalb` and isolated `XDG_CONFIG_HOME` /
  `XDG_CACHE_HOME`. The operator confirmed that Personal and Work were
  visible/selectable with keyboard and mouse; an existing event could be
  edited, saved, undone, and redone with matching visible/dirty state; view
  switching and collection close/reopen preserved the event and layout; and
  the collection settings/topology validation state was understandable and
  cancellable without a change. The fixture process was then closed. No
  personal collection or user configuration was used. This completes the
  remaining manual evidence; no STB-013 acceptance case remains.
- **Next:** STB-014 legacy characterization-path retirement.

### STB-014 — Retire legacy characterization paths

- **State:** DONE 2026-09-09
- **Depends on:** STB-013
- **Repository:** `../PlanStan`
- **Scope:** Baseline J: migrate every relevant legacy-engine behavior test to
  the real host fixture, then delete retired graph construction/accessors and
  source-list dependencies.
- **Acceptance:** production constructs one runtime, no test silently falls back
  to the retired graph, and the real workflow suite remains green.
- **Verification:** caller inventory/source searches plus affected workflow lane.
- **Result:** Removed `CollectionController`'s lazy legacy `SyncEngine`,
  conflict-store/manager accessors, resume bridge, and teardown path. Retired
  the synthetic engine-signal and legacy conflict-matrix CMake targets/sources;
  the controller-host runtime fixture is the replacement for controller-owned
  synchronization behavior, while direct library/unit fixtures remain separate
  component evidence. The conflict dock resolves only through the runtime when
  controller-bound. Updated controller and lifecycle assertions to observe
  runtime-owned endpoint/topology facts rather than forcing a second graph.
- **Verification result (2026-09-09):** PlanStan `83229ba6`, libkalburator
  `cdd2ffc`, configured with
  `-DPLANSTAN_LIBKALBURATOR_SOURCE_DIR=/home/clinton/dev/libkalburator`
  (`PLANSTAN_ENABLE_CALDAV_TESTS=OFF`). `cmake --build build-dev --target
  tst_collectioncontroller_runtime_host sync-workflow-tests
  tst_collectioncontroller tst_integration_collection_lifecycle -j4` built the
  retained targets. `ctest --test-dir build-dev -R
  '^(tst_collectioncontroller_runtime_host|sync_dialog)$' --output-on-failure`
  passed 2/2. `PLANSTAN_LIVE_RADICALE=1 ctest --test-dir build-dev -L
  integration --output-on-failure` passed 11/11 in 181.48 s. Exact source
  searches found no retired accessor/construction or retired CMake target;
  `git diff --check` and `python3 tools/check_task_dag.py` passed (90 active
  nodes, 124 edges, acyclic; no other ready task while STB-015 is selected).
  The broader `tst_collectioncontroller` remains 25 passed, 4 failed, 1
  skipped: topology star mapping count, local fingerprint persistence, CardDAV
  provider provisioning, and late-local-backend readiness. These failures are
  unrelated to the removed legacy path and are not hidden by this completion.
- **Next:** STB-015 independent-consumer and package proof.

### STB-015 — Prove reusable package boundaries

- **State:** DONE 2026-09-09
- **Depends on:** STB-014
- **Repository:** `../PlanStan`, `../libkalburator`, `../libkalcal`
- **Scope:** Baseline K: standalone calendar host, correctly owned Org
  dependency, headless subset, and install-and-consume package proof.
- **Acceptance:** clean library and independent-consumer builds use declared
  versions, exported namespaced targets, and no private source includes,
  target mutation, or whole-archive workaround.
- **Verification:** standalone viewer/headless/external-prefix builds; record
  any BLD-006 release-decision block separately from correctness evidence.
- **Result:** Raw `src/*` directories are private to libkalburator targets;
  PlanStan and libkalcal now consume exported namespaced headers, and
  PlanStan's test targets no longer inject private library source directories.
  The PlanStan Org I/O edge is attached through
  `kalburator_attach_org_io(PlanStan::OrgIO)`, so the consumer no longer
  mutates the library target directly. `KalburatorConfig.cmake` requests
  OrgGrove only when the installed configuration enabled outline Org support.
- **Verification result:** PlanStan configured with
  `-DPLANSTAN_LIBKALBURATOR_SOURCE_DIR=/home/clinton/dev/libkalburator` at
  revision `cdd2ffc`; `PlanStanCore` built, and
  `tst_planstan_local_runtime_integration` and `tst_calendarhostsmoke` each
  built and passed (1/1), and `tst_collectioncontroller_runtime_host` built
  and passed (1/1). An isolated Release libkalburator build/install at
  `/tmp/libkalburator-stb015-build.k5aAh9` with
  `KALBURATOR_BUILD_TESTS=OFF`, `KALBURATOR_HAVE_ORG_IO=OFF`,
  `KALBURATOR_HAVE_AKONADI=OFF`, and `KALBURATOR_HAVE_OUTLINE_ORG=OFF` was
  installed to `/tmp/libkalburator-stb015-prefix.IdDKiS`; its independent
  `Types` and `Core` consumers configured, built, and exited successfully.
  Source searches found no CMake private-source injection; `git diff --check`
  passed. No BLD-006 release-decision block was encountered.

### STB-016 — Polish measured desktop behavior

- **State:** DONE 2026-09-09
- **Depends on:** STB-013
- **Repository:** `../PlanStan`, `../libkalcal`
- **Scope:** Baseline L: measure real multi-view workloads before targeted scene,
  model, progress, focus, high-DPI, and keyboard interaction improvements.
- **Acceptance:** recorded before/after timings for selected workloads, preserved
  identity/selection, and repeatable affected edit/calendar workflows.
- **Verification:** measured GUI workload plus focused identity, lifecycle, and
  progress tests; no speculative performance framework.
- **Progress (2026-09-09):** Added a repeatable offscreen workload to
  `tst_agenda_selection_identity`: 3,000 timed events across a 30-day range
  with Agenda, Month, and Year views open. The baseline model update blocked
  for 52 ms. AgendaScene, MonthScene, and YearScene now coalesce model-change
  reloads into one next-event-loop refresh while explicit model/range setup
  remains synchronous; the same update submits in 0 ms and becomes visible in
  52 ms. The existing generated-occurrence selection assertion still passes.
  Added an offscreen keyboard/focus check that sends Right, Down, and Enter to
  the real `AgendaGraphicsView`, then repeats Enter after a coalesced refresh;
  it preserves focus and the creation slot. `tst_syncprogresswidget` confirms
  fixed-width readable status behavior and the runtime-run adapter confirms
  only progress runtime events are projected. Added
  `tst_collectionloadingoverlay`, which covers loading progress, failed-load
  feedback, automatic dismissal, and retry. It exposed and repaired the false
  “All calendars loaded successfully!” message after a failed fetch. Remaining
  acceptance: the real offscreen keyboard/focus workflow covers focus retention
  through a coalesced refresh and activation after it; no separate dialog-focus
  claim is made.
  `tst_syncprogresswidget_hidpi` runs with `QT_SCALE_FACTOR=2` and verifies
  the real status widget's captured pixmap uses the widget device-pixel ratio.
  `integration_incidence_crud` built but exceeded its configured
  five-minute CTest limit without producing a QtTest failure location; it is
  recorded as an unclassified workflow timeout, not counted as acceptance
  evidence or hidden by this task.
- **Verification result:** configured PlanStan against the working
  libkalburator tree; `tst_agenda_selection_identity`,
  `tst_collectionloadingoverlay`, `tst_syncprogresswidget`,
  `tst_syncprogresswidget_hidpi`, and `tst_collection_runtime_run_adapter`
  built and passed. The focused CTest suite passed 5/5. The high-DPI lane used
  `QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=2`. `git diff --check` and the
  task DAG checker passed.

### STB-017 — Close the stabilization programme with release evidence

- **State:** DONE 2026-09-09
- **Depends on:** STB-014, STB-015, STB-016
- **Repository:** `../PlanStan`, `../libkalburator`, `../libkalcal`
- **Scope:** Collect the final cross-repository revisions, configuration, test
  categories, known limitations, and release decision after all workflow,
  cleanup, package, and measured-behavior tasks are complete.
- **Acceptance:** no open stabilization task or uncertified workflow is hidden by
  a completion claim; each skipped/live/environmental check is labeled; the
  retained known-issues list contains only explicit non-release limitations.
- **Verification:** full ledger/DAG review, source/diff checks, and recorded
  cross-repository verification matrix.
- **Result (2026-09-09):** The closure audit used PlanStan `83229ba6`,
  libkalburator `cdd2ffc`, and libkalcal `10d60d2`, with the dirty worktrees
  carrying the recorded stabilization changes. PlanStan was regenerated in
  `build-dev` against `/home/clinton/dev/libkalburator`; the active
  configuration has Akonadi, PlanStan Org I/O, and outline Org enabled. The
  final local offscreen matrix passed 8/8:
  `tst_planstan_local_runtime_integration`,
  `tst_collectioncontroller_runtime_host`, `tst_calendarhostsmoke`,
  `tst_agenda_selection_identity`, `tst_collectionloadingoverlay`,
  `tst_syncprogresswidget`, `tst_collection_runtime_run_adapter`, and
  `integration_recurrence_editing`. The separate
  `tst_syncprogresswidget_hidpi` lane passed with `QT_SCALE_FACTOR=2`.
  Recurrence editing specifically passed all 20 cases in 32.54 seconds, so the
  former undetermined `IncidenceMutator::applyIncidenceAddition` segfault is no
  longer reproduced. STB-015's installed headless Types/Core consumer proof
  remains valid and the final source search finds no PlanStan CMake injection
  of libkalburator private `src/*` directories.
- **Limitations and release decision:** the current local certificate covers
  the PlanStan production, hermetic/offscreen, and package-consumer scope.
  The opt-in Radicale matrix is explicitly historical evidence from STB-013
  (11/11) and was not rerun without an active configured live service.
  `integration_incidence_crud` remains an explicitly recorded unclassified
  five-minute timeout, not acceptance evidence. KAL-015 and KAL-018 remain
  explicit non-release limitations; no PlanStan release claim includes the
  unsupported-operation compatibility seam or the independently owned
  WildPalms readiness workflow. All STB tasks are now closed; release is
  approved for the certified PlanStan scope subject to those stated limits.
- **Verification result:** `git diff --check` passed in PlanStan,
  libkalburator, and libkalcal. `python3 tools/check_task_dag.py` reports 90
  active nodes, 124 edges, acyclic, with no ready task. The private-source
  CMake search is empty.

### AUD-003 — Establish the stabilization baseline and reproduce production gaps

- **State:** DONE 2026-09-07
- **Repository:** `../libkalburator`, `../PlanStan`, and `../libkalcal`
- **Scope:** baseline tags, configured build paths, current test accounting, and
  a real PlanStan `CollectionController` host fixture. This task changes no
  runtime ownership behavior.
- **Acceptance:** record all three baseline revisions/tags, current status and
  diff digest, resolved library source paths, enabled CMake options, and each
  test result as executed, skipped, unavailable, or failed. Add a fixture that
  opens a temporary collection through `CollectionController`, observes runtime
  and backend construction, invokes production commands, and controls external
  completion/failure. Run the narrow PlanStan runtime target and test. The
  fixture must distinguish direct-runtime/adapter coverage from application
  wiring and identify whether legacy construction occurs.
- **Verification:** configure PlanStan with
  `-DPLANSTAN_LIBKALBURATOR_SOURCE_DIR=/home/clinton/dev/libkalburator`; build
  `tst_planstan_local_runtime_integration`; run its exact CTest name; build and
  run the new host fixture; run `python3 tools/check_task_dag.py` after this
  queue edit. Record commands and results here and in
  `../PlanStan/docs/stabilization-baseline.md`.
- **Outcome routing:** reopen `TOP-002` for a validated reproduction of
  mutation-before-validation, unsafe run-time topology replacement, executor
  affinity, reentrancy, or compensation failure. Reopen `RUN-008` for a held
  save A/edit B/complete A case that loses B or its recovery record. Reopen the
  applicable PS task only when a real application entry point bypasses its
  claimed runtime contract. Create a new registered task for a Kalcal identity
  or occurrence-query defect rather than assigning it a fabricated PS ID.
- **Next:** once this task has an evidence-backed result, select exactly the
  first reopened or newly registered `READY` task; do not start topology,
  staging, identity, or cutover changes speculatively.
- **Result:** Recorded baseline tags and active revisions in PlanStan's
  maintained baseline. Added `tst_collectioncontroller_runtime_host`, which
  opens a temporary two-local-backend collection through the real controller,
  observes session/mapping construction, waits for first-open work, invokes
  public `runSync()`, verifies record replication, and proves the lazy legacy
  characterization graph remains absent. A factory-injection fixture controls
  endpoint materialization failure and verifies controller error reporting
  without legacy construction. No acceptance reproduction reopened TOP-002,
  RUN-008, or a PlanStan task.
- **Verification result:** configured PlanStan with
  `-DPLANSTAN_LIBKALBURATOR_SOURCE_DIR=/home/clinton/dev/libkalburator`; built
  `tst_planstan_local_runtime_integration` and
  `tst_collectioncontroller_runtime_host`; `ctest --test-dir build-dev -R
  '^(tst_planstan_local_runtime_integration|tst_collectioncontroller_runtime_host)$'
  --output-on-failure` passed 2/2. `python3 tools/check_task_dag.py` passed.

### AUD-002 — Revalidate the decomposed PlanStan cutover plan

- **State:** DONE 2026-09-05
- **Scope:** maintained libkalburator plan and current PlanStan production
  callers, settings, topology UI, runtime assembly, and authoritative guidance.
- **Acceptance:** every production sync responsibility has a library seam or a
  named PlanStan adapter task; semantic dependencies prevent adapters from
  starting against incomplete contracts; provider/durable/physical mutation
  failure states are honest; PlanStan workers receive non-contradictory current
  instructions.
- **Verification:** inspect both working trees, run the task DAG checker, and
  record each discovered contract or sequencing gap in the affected task and
  maintained issue/architecture documents.
- **Result:** Retained the inert-adapter/one-flag-day structure, but blocked
  PlanStan preparation on guidance alignment; moved provider edits into the
  durable transaction; added API-009 for mapping/run/collection observation;
  expanded RUN-007/PS-012/PS-013 for monitored conflict behavior and automatic
  triggers; assigned DecSync monitor lifetime and Syncthing secret migration to
  PS-009; made irreversible physical mutations report compensation/repair
  outcomes; broadened topology adapter scope to every current borrowed
  collaborator view; and restored the two PlanStan smoke contracts at cutover.
- **Verification result:** inspected the current runtime facade and PlanStan
  controller, settings, Run Plan, topology, conflict, journal, DecSync, and
  Syncthing paths; independently reviewed the plan under PlanStan's delegation
  rule; `python3 tools/check_task_dag.py` and `git diff --check` pass.

### DOC-002 — Align PlanStan guidance with the adopted ownership migration

- **State:** DONE 2026-09-05
- **Depends on:** AUD-002
- **Repository:** `../PlanStan` documentation only, with the governing decision
  retained here.
- **Work:** update PlanStan's authoritative `docs/architecture.md` and current
  `CLAUDE.md` before any more preparatory adapter work. Preserve the genuinely
  retained UI, model, collection-intent, planning, and service-interface facts,
  but explicitly supersede controller ownership of synchronization internals
  and the instruction to create a separate PS-008 campaign/handoff.
- **Acceptance:** a worker following PlanStan's required reading path reaches
  the same ownership boundary, task IDs, flag-day rule, and no-parallel-campaign
  rule as this queue; current production ownership remains clearly
  distinguished from the adopted target.
- **Verification:** links and named symbols resolve in both working trees;
  searches find no claim that PS-008 is in progress, no instruction to create a
  second refactor queue, and no invariant requiring the controller to retain
  runtime-owned sync orchestration.
- **Result:** PlanStan's architecture now distinguishes current controller-owned
  production facts from the adopted runtime-owned target, preserves its
  planning/UI/service boundaries, and adds the one-queue/one-switch invariant.
  The current top of `CLAUDE.md` now directs workers to this queue, marks the old
  PS-008 scoping note superseded, and removes its competing campaign direction.
- **Verification result:** `git -C ../PlanStan diff --check` passes; named paths
  and task IDs resolve; searches find no current PS-008-in-progress claim or
  instruction to create a second synchronization-refactor campaign.

### AUD-001 — Revalidate roadmap completion claims

- **State:** DONE 2026-09-04
- **Scope:** current libkalburator tree plus the working PlanStan and WildPalms trees.
- **Acceptance:** completed claims are checked against implementation and executable evidence; overstated work is reopened without discarding valid progress; roadmap gates and the Now queue reflect the shortest safe path to the adopted runtime boundary.
- **Result:** Retained verified engine correctness, secret, provider-state, conflict, executor, documentation, and headless Identity work. Reopened the facade contracts, runtime ownership/coordinator/events, real topology transaction, capability split, both consumer cutovers, and broad target extraction where the stated acceptance was not met.
- **Verification:** inspected all three working trees; built libkalburator and both consumer application targets; ran the runtime contracts; restored and ran the 221/221 supported default library lane; checked actual ownership and whole-archive/private-include use with source searches.

## Completed foundation

### DOC-001 — Consolidate documentation

- **State:** DONE 2026-09-03
- **Result:** archived the former corpus intact; created one maintained reading path, roadmap, queue, issue list, architecture, consumer contracts, feature matrix, invariants, compatibility policy, and ADR area; regenerated the code-derived convergence matrix at its maintained path.
- **Verification:** 222 archived files; 4,846,841 bytes; aggregate content hash unchanged at `72c7bb5299acf525e3ce51c50debb38f31508353c8f1d6a560a785cd8965e942`; maintained local links resolve; `tst_gm_pipeline_convergence` passes against `docs/CONVERGENCE_MATRIX.md`.

## Safety and correctness baseline

### SAF-001 — Make DAV lifecycle tests hermetic

- **State:** DONE 2026-09-03
- **Result:** Replaced the legacy fixed-port/ambient Radicale checks in `tst_remotecalendarbackend` and `tst_backend_signals` with in-process `FakeCalDavServer` fixtures on ephemeral ports. Extended the fixture to advertise collections created through `MKCALENDAR`, so create/discover/delete characterization remains executable.
- **Verification:** Both converted suites build; `tst_backend_signals` passes 19/19; the remote discovery characterization passes; relocation and reentrancy probes each pass 20 serial separate-process runs. No affected test contacts port 5232 or skips based on an ambient server.
- **Scope:** `tst_backend_thread_relocation`, `tst_backend_reentrancy_pin`, `tst_backend_signals`, `tst_remotecalendarbackend`; fake DAV infrastructure.
- **Work:** allocate isolated server state and ports; forbid fallback to ambient localhost services; make every wait bounded; separate transport fixtures from executor lifecycle assertions.
- **Acceptance:** relocation and reentrancy failures reproduce or pass deterministically in 20 serial runs; tests cannot contact a developer Radicale; failure text identifies the violated lifecycle condition.
- **Updates:** KAL-001, KAL-016, test baseline in this file.

### SAF-002 — Introduce backend executor ownership

- **State:** DONE 2026-09-04
- **Depends on:** SAF-001, DES-001
- **Scope:** backend affinity, operation dispatch, construction, cancellation, destruction.
- **Work:** implement the smallest executor abstraction needed by both contract harnesses; library owns ordinary executors; external-resource lease supplies Palm execution where required.
- **Acceptance:** consumers never call `moveToThread`; backend creation/use/destruction occur on the declared executor; timeout and shutdown tests pass repeatedly.
- **Result:** Added `BackendExecutor`, which owns a backend's dedicated `QThread`, marshals synchronous compatibility calls onto that thread, and destroys the backend on its owning thread before bounded shutdown. `ProviderManager` now adopts provider-produced backends through this executor before registering them, so the registry remains non-owning.
- **Verification:** Full `cmake --build build -j4` passes. The executor affinity/destruction test and provider/lifecycle regression set pass (6/6). SAF-005 subsequently isolated the unsupported manual-relocation probes and the supported default lane passes 221/221. KRN-001 covers runtime/external-resource integration.
- **Updates:** KAL-001, ARCHITECTURE, CONSUMING, ADR if executor choice is durable.

### SAF-003 — Guarantee run terminalization

- **State:** DONE 2026-09-03
- **Depends on:** SAF-001
- **Scope:** engine run state, futures/promises, cancellation, statistics, next-run admission.
- **Work:** Added one `SyncEngine::finishRun()` terminalization path; it clears active state, future ownership, queue claims, and phase before publishing results, preserves the first meaningful error, and records cancellation in `lastSyncResult()`.
- **Acceptance:** injected failure and cancellation tests return terminal results with honest statistics, the cancelled parallel run admits an immediate next run, and worker destruction remains bounded in the existing teardown coverage.
- **Verification:** Full build passes. The focused terminalization, cancellation, failure-discrimination, worker teardown, and parallel-mapping suites pass; the 41-test engine/sync regression selection passes, including the hermetic remote backend suite. After SAF-005 isolated unsupported relocation, the supported aggregate passes 221/221.
- **Updates:** KAL-002.

### SAF-004 — Give fetch/load a typed failure channel

- **State:** DONE 2026-09-03
- **Result:** Added `RecordLoadResult` with an explicit success/error channel, routed engine reads and baseline harvesting through it, and made `RemoteCalendarBackend` preserve fetch failures instead of returning an ambiguous empty list. The legacy list and out-parameter APIs remain as migration surfaces for direct callers and consumer extensions.
- **Verification:** Full library build passes. Focused load, silent-success, fetch-discrimination, and remote blob-view tests pass (4/4); the final supported aggregate passes 221/221.
- **Updates:** KAL-003 resolved; API-001 remains queued behind this contract.
- **Depends on:** SAF-001
- **Scope:** synchronous backend load surface, first sync, baseline harvesting.
- **Work:** replace ambiguous empty-list errors with a typed record-load result or operation; migrate engine and both consumer extensions.
- **Acceptance:** failed source or target load performs no write, baseline/token advancement, or deletion; errors reach `RunResult`.
- **Updates:** KAL-003, API task dependencies.

### SAF-005 — Retire unsupported manual relocation from the default baseline

- **State:** DONE 2026-09-04
- **Depends on:** SAF-002, PS-002
- **Scope:** `tst_backend_thread_relocation`, `tst_backend_reentrancy_pin`, and equivalent supported executor coverage.
- **Work:** preserve useful lifecycle and reentrancy assertions through `BackendExecutor`; remove consumer-managed KDAV movement from the supported default lane now that consumers are forbidden to perform it.
- **Acceptance:** the complete default CTest suite terminates within declared bounds with no QObject affinity warnings; executor-owned DAV construction/use/destruction remains covered; any retained unsupported-path probe is isolated and labeled diagnostic.
- **Evidence:** on 2026-09-04 the relocation suite timed out at 30 seconds and the reentrancy suite failed three cases after KDAV's 30-second transfer timeout with cross-thread parenting warnings.
- **Result:** Kept both probes buildable but removed them from default CTest registration behind `KALBURATOR_ENABLE_UNSUPPORTED_RELOCATION_TESTS=OFF`; labeled the opt-in registrations `diagnostic;unsupported-relocation` with explicit timeouts. The supported lane retains `BackendExecutor` affinity/destruction and provider lifecycle coverage.
- **Verification:** clean configure and full build; default CTest passes 221/221 in 146.64 seconds. Cancellation sets and resets the worker's atomic flag synchronously on the engine side while the nested-event-loop wake-up remains queued; the strengthened immediate-cancel case passes 100 consecutive repetitions.

### SEC-001 — Redact credentials from all diagnostic paths

- **State:** DONE 2026-09-03
- **Scope:** URLs, provider errors, KIO/KDAV messages, test output.
- **Acceptance:** tests inject credentials into URL/userinfo/error strings and prove no secret reaches Qt messages, results, or persisted diagnostics.
- **Result:** Added shared URL and diagnostic-text redaction, routed calendar and multi-DAV diagnostics through it, and added regression tests for URL userinfo secrets. Persisted password removal remains SEC-002.
- **Verification:** Full build completes; `tst_credentialredaction` passes; affected conflict, calendar, provider, and sync tests pass; the final supported aggregate passes 221/221.
- **Updates:** SEC-002 completed the persistence migration; KAL-004 is resolved.

### SEC-002 — Replace persisted secrets with secret references

- **State:** DONE 2026-09-04
- **Depends on:** DES-001
- **Scope:** DAV passwords and OAuth tokens; host secret-store adapter.
- **Acceptance:** general configuration contains only opaque references; migration/reset behavior is explicit; PlanStan and WildPalms adapters pass.
- **Result:** Added the public `SecretStore` adapter boundary and registry, an opaque-reference in-memory fallback for tests, and legacy plaintext-to-reference migration for CalDAV, CardDAV, and multi-protocol DAV providers. Provider saves and all three DAV configuration widgets now emit no plaintext passwords; widgets resolve existing references for editing. PlanStan and WildPalms now install persistent KWallet-backed adapters during application startup.
- **Verification:** Full library build completes; secret-store, provider, and all three DAV widget suites pass, including opaque-reference retrieval and plaintext absence. WildPalms `wildpalms` builds successfully. PlanStan `PlanStan` builds successfully against the working libkalburator tree with local QOrgModeEditor sources supplied to avoid the unavailable private-repository fetch.
- **Handoff:** The consumer contract now documents that PlanStan and WildPalms must install a persistent secure `SecretStore` through `SecretStoreRegistry` before loading profiles. Legacy plaintext inputs migrate on load; saved profiles contain only `passwordRef`.
- **Updates:** KAL-004 is resolved; COMPATIBILITY and CONSUMING record the migration contract and startup ownership.

## Runtime design contracts

### DES-001 — Specify the runtime surface

- **State:** DONE 2026-09-03
- **Scope:** `CollectionRuntime`, construction definition, immutable snapshot, run requests/results, typed events, topology transaction, backend factories, external-resource lease.
- **Constraints:** satisfy every invariant; do not expose mutable registries, stores, engine, threads, or raw mappings as the normal API.
- **Acceptance:** header-level prototype compiles in isolated tests; responsibility map covers PlanStan `CollectionController` and WildPalms `PalmRuntime`; final decision recorded as ADR.
- **Result:** Added the compilable `CollectionRuntime` public-surface prototype with explicit selection/intent, immutable snapshots, topology result, run result, and a vendor-neutral external-resource lease. Mapped the retained and transferred responsibilities for PlanStan and WildPalms.
- **Verification:** The library target builds with the prototype in its explicit source list; the prototype passes a standalone C++20/Qt6 header syntax check; the focused SAF-004 regression set remains 4/4 green. Decision recorded in ADR-0003.

### DES-002 — Add PlanStan-like contract harness

- **State:** DONE 2026-09-04 — closed with KRN-001
- **Depends on:** RUN-001
- **Scope:** long-lived local+DAV collection, provider mutation, all/subset/single runs, model events, cancellation, teardown.
- **Acceptance:** executable uses only proposed public surfaces and fails against missing runtime behavior.
- **Progress:** Added facade construction, validation, reset, snapshot, disconnected-provider rejection, hermetic DAV provider connection, and runtime-owned local/DAV record-transfer coverage. `RUN-001` now supplies provider readiness and executor-backed endpoint materialization.
- **Result:** The public facade constructs the long-lived runtime, connects the
  hermetic DAV provider, admits its runtime-owned backend as a topology
  endpoint, transfers a record through executor-owned endpoints, publishes a
  versioned canonical record event on the facade thread, and cancels and
  terminalizes an active run during runtime destruction.
- **Verification:** `tst_planstan_runtime_contract` passes all construction,
  provider, topology, record, event, reset, failure, cancellation, and teardown
  cases. No remaining acceptance is delegated to a later task.

### DES-003 — Add WildPalms-like leased-device contract harness

- **State:** DONE 2026-09-04 — closed with KRN-001
- **Depends on:** RUN-001
- **Scope:** Palm-shaped backend, canonical hub, remote backend, external-resource hooks, link loss, mirror/full intent, multi-hop convergence.
- **Acceptance:** one run converges Palm↔hub↔remote; hook order proves prepare/phase/flush/finish semantics; no Palm type enters library headers.
- **Result:** Public factories create executable Palm-shaped, hub, and remote
  fakes without Palm types in library headers. One facade run converges a record
  across a deliberately reverse-ordered Palm→hub→remote graph, reverse mirror
  applies across the selected graph, the lease is acquired only for mappings
  bound to it, link loss cancels resource-bound work, and successful ordering is
  prepare→phase→flush→finish exactly once.
- **Verification:** `tst_wildpalms_runtime_contract` passes the convergence,
  reverse-mirror, link-loss, resource-scope, and hook-order cases.

### DES-004 — Define explicit run intents and selection

- **State:** DONE 2026-09-03
- **Depends on:** DES-001
- **Work:** replace empty-list sentinel and partial override semantics with typed `AllEnabled`, `ExactSet`, `One`, and `None`, plus Normal/Full/Mirror/Destructive intents.
- **Acceptance:** exhaustive request-validation tests; all paths enter one coordinator.
- **Result:** Added explicit `RunSelection` and `RunIntent` validation to the runtime surface. Empty selections are `None`, never `AllEnabled`; exact sets require non-empty ids; one-selection requires exactly one id; all intents share validation.
- **Verification:** `tst_run_request_contract` passes all validation cases; runtime and contract targets build.

### DES-005 — Map consumer code to keep, move, delete

- **State:** DONE 2026-09-04
- **Acceptance:** every runtime responsibility in PlanStan and WildPalms has one destination; no duplicate owner remains unclassified.
- **Result:** Added the maintained responsibility map to CONSUMING. It assigns application/device concerns to the consumers and runtime construction, execution, topology, stores, convergence, and canonical event concerns to libkalburator.
- **Verification:** The 2026-09-04 audit confirmed the ownership map still fits both consumers, while also confirming that the implementation migrations remain open. Both consumer targets build against the working library.
- **Depends on:** DES-001, DES-002, DES-003
- **Updates:** CONSUMING and downstream PS/WP task scopes.

## Runtime implementation

### KRN-001 — Close the runtime execution vertical slice

- **State:** DONE 2026-09-04
- **Depends on:** RUN-001, DES-004
- **Consolidates:** the remaining acceptance of DES-002, DES-003, RUN-004,
  RUN-005, and RUN-006. Those pieces could not be completed in dependency order:
  the contract tests required the executor/coordinator/event behavior, while
  those implementation tasks required the same contract tests as acceptance.
- **Acceptance:** both public contract harnesses move real records; selected
  resources have exactly-once prepare/phase/flush/finish semantics; link loss
  and active destruction terminalize; normal and mirror runs use bounded engine
  convergence; canonical record events require no backend reread or native parse.
- **Result:** `CollectionRuntime` now binds only selected external resources,
  installs a resource-loss callback, translates full-rediff, graph-wide mirror,
  and destructive intents, exposes provider-owned backends as usable topology
  endpoints, relays progress, and publishes versioned canonical record events on
  the facade thread. The engine applies direction overrides to every selected
  mapping, cancels in-flight work touching a lost resource, and completes an
  outstanding future during teardown.
- **Verification:** PlanStan-like and WildPalms-like runtime contracts pass,
  including reverse-ordered multi-hop convergence, reverse mirror, link loss,
  canonical payload, executor ownership, and active destruction. Focused engine
  convergence/cancellation/parallel/teardown and record-event suites pass 10/10;
  the complete supported library lane passes 221/221. Immediate cancellation
  passes 100 consecutive repetitions. Both consumer applications build against
  the working tree; the selected PlanStan and WildPalms production suites pass
  8/8 each.
- **Follow-up:** TOP-002 closed in-memory endpoint/mapping publication.
  Consumer-grade observation, policy/recovery, durable desired state, and
  physical collection mutation remain API-007, API-008, API-009, RUN-007,
  RUN-008, TOP-004, and TOP-005.

### RUN-001 — Implement runtime construction and ownership

- **State:** DONE 2026-09-04
- **Depends on:** TOP-001
- **Work:** own registries, plugin activation, providers, engine, stores, policies, and lifetime; return typed construction failure.
- **Acceptance:** both contract harnesses construct and destroy without consumer assembly.
- **Result:** The runtime owns isolated registries, plugins, provider manager,
  baseline/conflict stores, host, engine, and endpoint lifetimes. It exposes an
  explicit asynchronous provider-connection command, relays provider state
  events, rejects topology references to disconnected providers, and
  transfers executable factory endpoints into runtime-owned
  `BackendExecutor` instances before private engine registration. The PlanStan
  contract transfers a record through that path.
- **Follow-up:** coordinated store lifecycle remains RUN-003; transactional
  provider/materialization rollback remains TOP-002.
- **Verification:** focused PlanStan and WildPalms runtime contracts pass;
  `cmake --build build -j4`, the full supported CTest lane (221/221), and
  `git diff --check` pass.

### RUN-002 — Replace static registration with explicit extensions

- **State:** DONE 2026-09-03
- **Depends on:** RUN-001
- **Acceptance:** stock and consumer extensions are explicit values; duplicate registration is rejected; no linker retention is required in contract tests.
- **Result:** Explicit in-process plugin loading now rejects null plugin instances and duplicate manifest IDs instead of silently overwriting one entry. `stockPluginItems()` exposes the stock set as explicit values; `CollectionRuntime` merges it with consumer-supplied plugin extensions and loads the combined set through one validated path.
- **Verification:** Runtime contract tests and `tst_pluginmanager_resolve` pass (3/3); runtime contract link lines contain no whole-archive flag; full build passes. The legacy `registerStockPlugins()` helper remains only as an explicit compatibility/test convenience and is not used by `CollectionRuntime`.

### RUN-003 — Centralize stores and schema lifecycle

- **State:** DONE 2026-09-05
- **Depends on:** KRN-001
- **Work:** runtime owns paths, opening, migrations, transactions, reset/recovery, and close ordering for baselines, conflicts, aliases, tokens, and journals.
- **Acceptance:** partial open/migration cannot expose a usable runtime; reset is tested.
- **Result:** Runtime store lifecycle now opens and validates the baseline store,
  conflict store, and runtime-scoped calendar journal as one private set before
  the engine can observe them. Close ordering is explicit; reset closes the
  complete set, removes only this runtime's database/WAL/journal artifacts,
  recreates the journal directory, and reopens the stores through the same
  validation path. A failed reopen leaves the runtime unavailable.
- **Verification:** `tst_planstan_runtime_contract` covers journal cleanup,
  unrelated-file preservation, clean reopen, and failed-reset containment.
  Full `cmake --build build -j4` and the default CTest lane pass 221/221.
- **Follow-up:** RUN-008 decides and implements operational journal/recovery
  ownership. Consumer adoption is PS-016/WP-009; durable topology is
  TOP-004/TOP-005.

### RUN-004 — Integrate backend executors

- **State:** DONE 2026-09-04 — consolidated into KRN-001
- **Depends on:** SAF-002, RUN-001
- **Acceptance:** PlanStan-like and WildPalms-like harnesses pass runtime-owned executor, cancellation, link-loss, and destruction cases.
- **Progress:** ProviderManager adopts provider backends into `BackendExecutor`, and the focused executor destruction test passes.
- **Result:** Factory endpoints and provider endpoints are executor-owned;
  selected external resources participate in execution, resource loss cancels
  affected in-flight mappings, and facade destruction completes the public run.

### RUN-005 — Implement one run coordinator with bounded convergence

- **State:** DONE 2026-09-04 — consolidated into KRN-001
- **Depends on:** DES-004, RUN-001
- **Work:** unify all/subset/single/mirror/full/clobber paths; incorporate graph passes and aggregate results once.
- **Acceptance:** WildPalms star converges in one public call; no consumer future loop; per-mapping results retain identity independent of order.
- **Progress:** Selection normalization, endpoint admission, mapping-identity projection, and the engine future bridge exist. Identifier-only topologies now fail instead of fabricating success, and successful engine completion flushes external resources before `finish()`.
- **Result:** The facade translates normal, full-rediff, directional mirror, and
  destructive intents. All selection shapes use its one lifecycle; multi-mapping
  normal and mirror runs use the engine's bounded fixpoint coordinator and retain
  mapping identity. The WildPalms-like reverse-ordered star converges in one
  public call.

### RUN-006 — Emit canonical runtime events

- **State:** DONE 2026-09-04 — consolidated into KRN-001
- **Depends on:** RUN-005
- **Work:** typed collection/record/provider/run snapshots and change events.
- **Acceptance:** PlanStan-like model refresh performs no backend scan, blocking record load, or native iCalendar parse.
- **Progress:** The facade has a generic event sink and publishes topology/run start/finish events.
- **Result:** Provider state, topology, run start/progress/finish, and canonical
  record events are public runtime events. Record events carry mapping/backend/
  collection/record identity, change kind, canonical domain/encoding, a schema
  version, and canonical bytes; worker-thread delivery is marshalled to the
  facade thread. The PlanStan-like assertion consumes the payload without a
  mapping scan, backend read, or iCalendar parse.

## Correctness closure

### COR-001 — Repair collection-property reconciliation

- **State:** DONE 2026-09-03
- **Acceptance:** persisted baseline is read on subsequent runs; one-sided edits travel in the correct direction; mapping conflict policy is honored; async apply failure fails the mapping; repeat run quiesces.
- **Result:** Collection property reconciliation now reloads the persisted source-collection baseline, applies changes through an acknowledged asynchronous completion callback, fails the mapping on timeout or backend failure, and honors SourceWins, TargetWins, and AskUser conflict policies. Empty snapshots remove stale baselines.
- **Verification:** `kalburator`, `tst_calendar_plugin`, and `tst_property_phase` build; focused plugin/property tests pass. The required library build passes.
- **Updates:** KAL-005 resolved; calendar property synchronization is Integrated.

### COR-002 — Persist custom merge payload and version guard

- **State:** DONE 2026-09-03
- **Acceptance:** merged payload survives restart and is the exact record applied; stale source/target content prevents application; schema migration/reset tested.
- **Result:** Added additive `merged_ical` persistence and migration to `SyncConflictStore`, carried the payload through deferred resolution APIs and restart rehydration, and retained the existing source/target timestamp guard before applying it.
- **Verification:** `tst_conflictmanager`, `tst_syncengine_unification`, and `tst_conflict_policy_matrix` pass; library and affected targets build.
- **Updates:** KAL-006 resolved; COMPATIBILITY documents the additive schema.

### COR-003 — Make transform-empty guards symmetric

- **State:** DONE 2026-09-04
- **Acceptance:** non-empty source or target input unexpectedly producing empty output fails with the same typed transformation error.
- **Result:** Canonical-to-native target and source demotion now use the same empty-output guard, and a failed demotion prevents either side from being written, preserving the no-partial-write invariant.
- **Verification:** Full build passes; `tst_engine_write_gate`, `tst_engine_fixpoint_passes`, `tst_engine_vendor_shaped_hub`, `tst_sync_convergence`, and `tst_engine_unified_boundary` pass.
- **Updates:** KAL-007 resolved.

### COR-004 — Decide snapshot recovery

- **State:** DONE 2026-09-04
- **Decision:** removed the unused snapshot/restore surface rather than retain a public undo API whose restore operation was a stub.
- **Verification:** no production or test references to `captureSnapshot()` or `restoreFromSnapshot()` remain; the full library build and calendar-manager tests pass.
- **Updates:** KAL-011 resolved; FEATURES no longer advertises snapshot recovery.

## Topology and API

### API-005 — Introduce narrow capability acquisition

- **State:** DONE 2026-09-05
- **Depends on:** KRN-001
- **Scope:** make engine, runtime, registries, and library helpers acquire only
  the record, collection, change-tracking, calendar-native, batch, and debug
  capability that each operation needs. Add characterization coverage for an
  operation-only backend and a blob-backed backend. This task intentionally
  retains the composite compatibility surface while consumer cutovers migrate.
- **Acceptance:** no library orchestration path treats `SyncBackendBase` or
  `IBlobBackend` as proof that an unrelated capability is available; a missing
  acquired capability produces a typed unsupported result; operation-only and
  blob-backed characterization tests pass without making the Google/Graph
  backends abstract.
- **Replan (2026-09-05):** Making `SyncBackendBase` blob methods pure in
  isolation makes supported operation-only Google/Graph calendar backends
  abstract. Preserve those backends while migrating callers first; narrow the
  acquisition boundary and add characterization coverage before removing the
  transitional defaults. Durable rationale: [ADR-0006](adr/0006-capability-contract-migration-boundary.md).
- **Result:** The routing adapters now declare their collection,
  change-tracking, and batch behavior explicitly. The attempted base-class
  purity change was reverted after the full-build compiler check identified
  the operation-only backend boundary. The engine now acquires record-read,
  direct-mutation, async-apply, and collection-wipe capabilities separately;
  first-sync and clobber reject a missing required capability with a typed
  failure, while vendor backends retain read/apply and explicitly decline
  direct CRUD/wipe. The legacy writer uses acquired async apply for every
  `SyncBackendBase` path. KAL-015 remains open.
- **Verification:** full library build passes; the supported default CTest
  lane passes 221/221. Google-calendar characterization proves operation-only
  read/apply without CRUD/wipe, and the clobber suite proves both backend-
  specific wiping and typed rejection when wipe capability is absent.
- **Unblocked:** PS-009 and WP-009 can migrate their consumer extension
  contracts; API-006 removes the compatibility defaults after both consumer
  closure gates complete.

### API-006 — Remove transitional composite backend defaults

- **State:** DONE 2026-09-06
- **Depends on:** PS-008, WP-009
- **Scope:** migrate the now-runtime-facade consumer extension contracts off
  `SyncBackendBase`/`IBlobBackend` as a universal capability claim; narrow
  concrete inheritance and remove the false, empty, and no-op compatibility
  implementations retained by API-005.
- **Acceptance:** operation-only Google/Graph-style backends remain
  constructible without blob CRUD; blob-backed implementations declare only
  capabilities they implement; unsupported operations are unavailable through
  the acquired interface or return a typed unsupported result; the library and
  both consumer integration builds/tests pass without permissive defaults.
- **Rationale:** This is the removal half of ADR-0006. Keeping it separate
  avoids the previous hidden dependency cycle in which API-005 required the
  consumer migration while PS-008 and WP-009 were blocked on API-005.

### TOP-001 — Define operational topology materialization contract

- **State:** DONE 2026-09-04
- **Depends on:** DES-001
- **Scope:** replace identifier-only topology and validation-only factories with
  an operational, neutral contract for provider connection, factory-backed
  endpoint creation, collection materialization, optional external-resource
  binding, removals, mapping publication, and explicit readiness.
- **Acceptance:** isolated public-header tests can define a fallible fake
  factory and a provider/resource-backed endpoint; a desired topology names
  every materialization input and reports either a complete usable snapshot or
  explicit repair state. No backend, executor, registry, or consumer-native
  type leaks through the facade.
- **Prior progress:** `TopologyDefinition` carries full mappings and rejects
  identifier-only mapping success; `TopologyResult` carries commit/repair
  state. `BackendFactory` currently validates only, so this is not an
  operational topology contract.
- **Decision:** ADR-0005 fixes the boundary implemented by RUN-001 and proven
  by the two KRN-001 contract slices.
- **Result:** Replaced identifier-only topology with neutral endpoint and
  external-resource definitions. `BackendFactory` now fallibly creates opaque
  `BackendEndpoint` instances from a complete materialization request carrying
  factory input, provider/resource bindings, and collection selection. The
  runtime validates references, explicit removals, endpoint readiness, and
  materializes every endpoint before publishing the snapshot. It retains the
  endpoint ownership and rejects partial topology.
- **Verification:** Full library build passes; the PlanStan and WildPalms
  runtime contract targets build and pass. The WildPalms contract proves
  factory invocation and resource binding without importing Palm or backend
  implementation types. Endpoint execution and engine registration are
  RUN-001 work.

### TOP-002 — Implement atomic topology application

- **State:** DONE 2026-09-05
- **Depends on:** RUN-003
- **Acceptance:** injected failure at each step leaves the prior runtime usable or returns explicit repair state; generated IDs are collision-safe.
- **Result:** Topology application now validates the complete desired state
  before publication, rejects omitted endpoints without explicit removal,
  rejects unknown or contradictory removals, and prevents factory endpoints
  from colliding with live provider backends. Fallible endpoint creation and
  executor startup remain local until validation succeeds, so failure leaves
  the prior committed snapshot usable.
- **Verification:** Runtime coverage proves failed materialization preserves
  generation and backend state, removal validation is atomic, provider-owned
  endpoint admission remains supported, and the focused PlanStan contract
  passes. Full build and default CTest previously pass 221/221; the current
  topology change rebuilds and passes the focused contract.
- **Follow-up:** TOP-004 adds durable desired-state participation and TOP-005
  adds physical collection mutations. PS-015 then migrates PlanStan's topology
  surfaces; KAL-008 remains open until those paths commit through one boundary.

### TOP-003 — Unify provider lifecycle state

- **State:** DONE 2026-09-04
- **Depends on:** RUN-001
- **Acceptance:** one typed state machine governs future completion, signals, retry, disconnect, error, and backend availability.
- **Result:** ProviderManager now mirrors the typed ProviderConnectionState signal as the lifecycle authority, registers and unregisters provider backends on typed transitions, preserves Error for diagnostics, and resets only silent failed futures to retryable Disconnected. Built-in providers emit the complete typed lifecycle, including Disconnected.
- **Verification:** Full library build target and provider manager/provider lifecycle tests pass, including Connecting-to-Connected, Connecting-to-Error, reconnect, retry, backend registration, and disconnect coverage.
- **Updates:** KAL-014 resolved.

### API-001 — Split backend capability interfaces

- **State:** REMOVED — consolidated into API-005
- **Superseded by:** API-005
- **Work:** separate record I/O, collection CRUD, change tracking, calendar-native access, and debug access.
- **Acceptance:** callers cannot invoke an unsupported operation without handling a typed result.
- **Progress:** Named collection, record, change-tracking, and batch interfaces exist.
- **Remaining:** callers and registries must request only supported interfaces. `IBlobBackend` still inherits every capability, so the split currently changes names rather than what can be invoked.

### API-002 — Retire permissive backend defaults

- **State:** REMOVED — split between API-005 and API-006
- **Superseded by:** API-006
- **Acceptance:** false/empty/no-op defaults are removed unless empty is the valid typed result.
- **Progress:** Batch methods are mandatory and concrete backends state `supportsBatch()`.
- **Remaining:** unsupported operations need typed results or must be absent from the acquired interface. Empty strings, booleans, and explicit no-op batch methods do not meet the acceptance criterion.

### API-003 — Resolve contacts collection CRUD

- **State:** DONE 2026-09-04
- **Decision:** implement CardDAV collection creation through the neutral API or declare it unsupported and hide the action.
- **Result:** CardDAV contacts remain discovery-backed: `RemoteContactsBackend` explicitly declines collection creation, and no consumer collection-create action is exposed for that backend. Existing addressbooks continue to be registered from discovery.
- **Verification:** Added a regression test for the unsupported path; the contacts backend test target and full library build pass.
- **Updates:** KAL-012 resolved; FEATURES records discovery-only CardDAV collection management.

### API-004 — Resolve universal storage contributions

- **State:** DONE 2026-09-04
- **Decision:** implement real providers/factories or remove registry entries.
- **Result:** Removed the raw-files and generic-SQLite contribution registrations until configured provider factories exist. The concrete backends remain available for explicitly configured consumers, while the stock registry no longer advertises null factories.
- **Verification:** Updated stock-plugin regression coverage passes with the full library build.
- **Updates:** KAL-013 resolved; FEATURES now describes these as direct configured backends rather than stock contributions.

## Consumer facade completion

### API-007 — Publish typed provider state and discovery facts

- **State:** DONE 2026-09-05
- **Depends on:** TOP-002
- **Repository:** libkalburator, checked against `../PlanStan` account,
  topology, and wizard callers.
- **Work:** replace the numeric `RuntimeEvent::message` provider-state encoding
  with a typed field and expose immutable provider/backend discovery facts
  needed to render accounts and select collections. Do not expose
  `ProviderManager`, `IProvider`, `BackendRegistry`, mutable collections, or
  credential-bearing configuration.
- **Acceptance:** a consumer can determine provider readiness, errors, owned
  backend IDs, and discovered collection IDs/types/names using only runtime
  snapshots/events; reconnect replaces stale discovery atomically; provider
  update invalidation is observable without integer/string parsing.
- **Verification:** extend the PlanStan-like runtime contract with connect,
  reconnect, discovery replacement, error, and credential-redaction cases.
- **Result:** `RuntimeSnapshot` now contains immutable per-provider facts for
  identity, kind, display name, typed connection state, credential-redacted
  error/warning text, owned backend IDs, and discovered `CollectionInfo`.
  Provider state, discovery replacement, and provider errors are relayed by
  typed fields on runtime events.
- **Verification result:** the PlanStan-like runtime contract covers initial
  disconnected state, connected discovery, typed connected events, backend
  ownership, and credential absence; the focused test passes and the target
  builds.
- **Updates:** KAL-026, `CONSUMING.md`, `ARCHITECTURE.md`, and
  `COMPATIBILITY.md` if the public structures change.

### API-008 — Replay or query unresolved conflicts through the facade

- **State:** DONE 2026-09-05
- **Depends on:** RUN-003
- **Repository:** libkalburator, checked against `../PlanStan` conflict-dock
  startup and rebind behavior.
- **Work:** add the smallest immutable facade surface that lets a newly attached
  consumer discover unresolved persisted conflicts. Use snapshot/query or
  deterministic initial event replay; never return a `SyncConflictStore` or
  `ConflictManager` pointer.
- **Acceptance:** after a conflict is persisted and the runtime is destroyed,
  a recreated runtime presents the same stable conflict ID, native display
  payload, mapping identity, and resolution state; `resolveConflict()` still
  applies it with normal staleness checks and emits exactly one resolution
  event.
- **Verification:** add a restart/rebind contract case to
  `tst_planstan_runtime_contract` and cover an empty backlog.
- **Result:** added `CollectionRuntime::unresolvedConflicts()` as an immutable
  backlog query. It returns persisted conflict display payloads without
  exposing `SyncConflictStore` or `ConflictManager`.
- **Verification result:** the PlanStan-like contract creates a conflict,
  destroys and recreates the runtime, verifies the stable conflict ID and
  mapping identity, resolves it through the rebound facade, and verifies the
  backlog is empty.
- **Updates:** KAL-027 and the conflict contract in `CONSUMING.md`.

### API-009 — Publish mapping state and typed run telemetry through the facade

- **State:** DONE 2026-09-05
- **Depends on:** KRN-001, RUN-003
- **Repository:** libkalburator, checked against `../PlanStan`'s Run Plan,
  progress, and topology views.
- **Work:** expose immutable committed mapping facts and the smallest typed
  observation surface needed for per-mapping start/finish, convergence-pass
  progress, last successful sync time, and collection changes. Populate or
  remove the currently declared `CollectionChanged` event; do not expose
  `SyncMapping`, `BaselineStore`, `SyncConflictStore`, or `SyncEngine` pointers.
- **Acceptance:** a consumer can render PlanStan's Run Plan using only
  snapshots/events plus the conflict backlog; mapping IDs are correlated with
  endpoints and collection IDs; pass and
  per-mapping events are typed rather than encoded in message text; last-sync
  time advances only for successful work; the collection-change payload and
  committed-delivery rules are defined for TOP-005's physical commands.
- **Verification:** extend the PlanStan-like runtime contract with mapping
  snapshot, multi-pass, per-mapping terminal, last-sync, and collection-event
  schema cases, including failure and cancellation non-advancement.
- **Updates:** KAL-030, `ARCHITECTURE.md`, `CONSUMING.md`, `FEATURES.md`, and
  `COMPATIBILITY.md`.
- **Progress:** Added immutable mapping endpoint/collection snapshots and
  typed facade telemetry for mapping start/finish, success/cancellation,
  convergence pass, and success-only last-sync timestamps. `RunProgressKind`
  now identifies overall, mapping, and convergence observations without
  message parsing. `CollectionChanged` carries a typed `CollectionInfo`,
  mutation kind, provider identity, and committed flag; TOP-005 remains
  responsible for emitting it after physical mutation commit.
- **Result:** The facade now publishes mapping endpoint and collection facts,
  success-only last-sync timestamps, and typed `RunProgressKind` observations
  for overall progress, convergence passes, and mapping start/finish. The
  `CollectionChanged` event carries a typed collection payload, mutation kind,
  provider identity, and committed-delivery flag for TOP-005.
- **Verification result:** Extended `tst_planstan_runtime_contract` with typed
  progress and collection-event schema coverage. The focused contract passes;
  the full library build also passes after relinking the changed public event
  layout.

### RUN-007 — Preserve PlanStan run policy through runtime ownership

- **State:** DONE 2026-09-06
- **Depends on:** KRN-001
- **Repository:** libkalburator, checked against `../PlanStan` application
  settings and `SyncMassDeleteGuard` behavior.
- **Work:** define narrow construction/update inputs for the consumer-selected
  maximum mapping concurrency, normal-run unchanged-skip policy, monitored
  versus background conflict behavior, and mass-deletion confirmation. The
  runtime owns application of those values to its private engine; the
  confirmation and monitored-conflict seams carry neutral facts and decisions,
  not UI types or an engine pointer. A monitored run must be resumable through
  the facade without borrowing the engine or silently changing to deferred
  next-run replay.
- **Acceptance:** PlanStan can preserve its current safety and tuning behavior
  without calling `SyncEngine::setMaxConcurrentMappings()`,
  `setSkipUnchangedMappings()`, or `setMassDeleteGuard()`; policy changes during
  an active run are either rejected or applied only to the next run by a
  documented rule.
- **Verification:** runtime contracts cover accepted/rejected deletion,
  concurrency bounds, normal skip, full re-diff override, monitored pause and
  resume, background deferral, and policy change timing.
- **Updates:** KAL-028, KAL-030, `ARCHITECTURE.md`, and `CONSUMING.md`.
- **Progress:** Added `RuntimePolicy` construction/update input with explicit
  concurrency, unchanged-skip, monitored/background interaction, and neutral
  mass-delete confirmation. Policy updates are rejected during an active run;
  values are applied to the private engine at runtime construction and before
  the next run.
- **Result:** Added the runtime-owned policy boundary and applied it to the
  private engine's concurrency, unchanged-skip, conflict interaction, and
  mass-delete guard settings. Runtime policy updates are validated and
  rejected during an active run.
- **Verification result:** The PlanStan-like runtime contract covers invalid
  construction policy, accepted updates, and active-run rejection; the focused
  contract passes.

### RUN-008 — Resolve and implement the journal/recovery ownership boundary

- **State:** DONE 2026-09-06
- **Depends on:** RUN-003
- **Repository:** libkalburator plus `../PlanStan` journal and staging callers.
- **Work:** distinguish PlanStan's user-edit crash journal from reconciliation
  recovery, decide the owner of each, and make the code match. Wire the
  runtime-owned journal into actual runtime work or remove it and narrow the
  ownership claim; retain a PlanStan journal only if it protects
  consumer-owned mutations. Record the durable boundary in an ADR.
- **Acceptance:** no inert `CalendarJournal` object counts as runtime recovery;
  reset/close/restart behavior has executable coverage; recovered edits are
  presented once and no journal is replayed by two owners.
- **Verification:** focused library journal/reset contracts and PlanStan
  `tst_journalrecoverycoordinator` plus the collection lifecycle test.
- **Updates:** KAL-029, `ARCHITECTURE.md`, `CONSUMING.md`, and `FEATURES.md`.
- **Result:** Removed the inert runtime `CalendarJournal` construction and
  cleanup. Runtime reset owns only reconciliation-store artifacts; PlanStan
  remains the sole owner of staged user-edit journal replay. The boundary is
  recorded in ADR-0007.
- **Verification result:** Runtime reset/reopen contract preserves a staged
  consumer journal while resetting runtime stores; the focused runtime
  contract passes. The complete supported library lane passes 221/221.

### TOP-004 — Include provider edits and durable desired state in topology commit/rollback

- **State:** DONE 2026-09-06
- **Depends on:** TOP-002
- **Repository:** libkalburator, designed against `../PlanStan`'s
  `KalbConfigManager`/`KalbSyncTopologyDataSource` and WildPalms persistence.
- **Work:** add a neutral durable-desired-state participant to the topology
  transaction, or an equivalently small prepare/commit/rollback contract, and
  stage provider additions, updates, and removals in that same transaction.
  The existing incremental provider commands are transitional and must not be
  PlanStan's final persistence path. A successful transaction must not mutate
  provider truth or publish an in-memory runtime snapshot before the
  consumer-owned profile representation is durably accepted.
- **Acceptance:** endpoint materialization, executable mappings, provider
  configuration/dependencies, and durable desired state commit together;
  injected provider-connect or persistence failure preserves the previous usable
  runtime and profile state or reports a typed repair-required result. Provider
  removal can atomically remove its dependent endpoints/mappings instead of
  requiring a destructive command before the transaction. No `.kalb` or Palm
  type enters the library.
- **Verification:** extend atomic-topology tests with prepare, commit, rollback,
  and rollback-failure cases using a fake persistence participant.
- **Updates:** KAL-008, `ARCHITECTURE.md`, `CONSUMING.md`, and the public
  compatibility surface.
- **Result:** Added an explicit `TopologyDefinition::replaceProviders` mode
  carrying the complete neutral provider configuration set. Provider
  additions, edits, and removals are staged and connected before endpoint and
  mapping publication; persistence preparation/commit occurs before the live
  snapshot is published. Any staged-provider or later topology failure
  restores the prior provider set and snapshot. The existing incremental
  provider commands remain available as transitional commands, while durable
  desired-state callers can use one topology transaction.
- **Verification result:** The PlanStan-like contract covers provider removal
  followed by injected durable-commit failure and verifies the prior provider
  remains in the runtime snapshot; the focused runtime contract passes.
  Full build and the supported library lane are the final checks for this
  task.

### TOP-005 — Put physical collection mutations behind runtime commands

- **State:** DONE 2026-09-06
- **Depends on:** API-007, API-009, TOP-004
- **Repository:** libkalburator, checked against `../PlanStan` topology and
  wizard create/adopt/rename/untrack/destroy workflows.
- **Work:** represent physical collection creation, metadata update, adoption,
  untracking, and destruction as capability-checked runtime operations within
  the desired-topology transaction. Define prepare/commit/compensation ordering
  explicitly: operations that cannot be rolled back, especially remote destroy,
  must either occur only after all fallible preparation or return a typed
  repair-required result. Unsupported operations return typed failures;
  destructive intent remains explicit.
- **Acceptance:** a consumer can implement every existing PlanStan topology
  verb without acquiring a backend or `CalendarManager`; failure cannot leave
  a persisted binding to a collection that was not materialized, and destroy
  cannot be confused with untrack; each committed physical create, metadata
  update, or delete emits exactly one API-009 collection observation, while
  rollback emits none.
- **Verification:** hermetic local and fake-DAV transaction tests cover all five
  verbs, unsupported capability, materialization failure, rollback,
  compensation failure, and irreversible-operation repair state.
- **Updates:** KAL-008, `ARCHITECTURE.md`, `CONSUMING.md`, `FEATURES.md`, and
  `COMPATIBILITY.md`.
- **Result:** Added the neutral `IBackendCollectionMutator` capability and kept
  physical collection commands inside the durable topology transaction. Create,
  adopt, metadata update, rename, untrack, and destroy are distinct operations;
  adopt/untrack do not pretend to be physical writes. Unsupported capabilities
  fail before publication, committed physical mutations emit exactly one
  `CollectionChanged` event, and failed transactions emit none. Create and
  rename compensation is attempted in reverse order; metadata restoration and
  destructive deletion report `repairRequired` when no safe inverse exists.
- **Verification result:** `tst_planstan_runtime_contract` covers all mutation
  verbs, unsupported capability, failed materialization, failed persistence
  commit with create compensation, and no-event rollback behavior. The focused
  contract passes; the full build and default CTest lane are the final checks.

## PlanStan migration

### PS-009 — Complete PlanStan endpoint-factory coverage

- **State:** DONE 2026-09-06
- **Depends on:** DOC-002, TOP-002, API-005
- **Repository:** `../PlanStan`.
- **Primary paths:** `src/controllers/planstanbackendfactory.*`, the backend
  constructors currently in `collectioncontroller.cpp`, and `tests/runtime/`.
- **Work:** extend `PlanStanBackendFactory` beyond `local` to every
  configuration-owned backend still constructed by
  `CollectionController::createBackendFromConfig()` (`orgmode`, `decsync`,
  `subscription`, and conditional `akonadi`). Provider-owned DAV endpoints stay
  provider endpoints. Preserve DecSync/Syncthing behavior through a
  PlanStan-owned monitor adapter without exposing the backend or giving the
  runtime a UI service pointer. Migrate the Syncthing API key to the installed
  secret store; neutral factory input carries only an opaque reference.
- **Acceptance:** each supported type validates neutral factory input, returns
  a ready owned endpoint, and fails malformed/unsupported input without leaked
  QObjects or threads; the Syncthing monitor remains observable by PlanStan for
  status UI and event-driven DecSync without crossing backend affinity; neither
  snapshots, factory input, nor saved `.kalb` data contains the API key; no
  production synchronization-ownership call site changes in this preparation
  task (the security migration of persisted configuration may land immediately).
- **Verification:** extend `tst_planstan_local_runtime_integration` with one
  materialization case per compiled backend type and a teardown/failure case;
  run the conditional Akonadi case only when that target is enabled.
- **Updates:** KAL-031 and PlanStan's maintained secret/configuration facts.
- **Result:** `PlanStanBackendFactory` materializes local, OrgMode, DecSync,
  holiday-subscription, and conditional Akonadi endpoints with malformed-input
  rejection. DecSync Syncthing configuration now accepts only an opaque
  `syncthingApiKeyRef`, resolves it through the installed secret store, and
  keeps the monitor observable through the PlanStan-owned factory adapter.
  Legacy plaintext configuration is migrated to a secret reference before the
  active collection configuration is retained.
- **Verification result:** `tst_planstan_local_runtime_integration` covers the
  real local runtime path, unsupported backend rejection, all configuration-
  owned endpoint materializers, and plaintext Syncthing-key rejection. The
  PlanStan target and focused test pass against the working libkalburator tree.

### PS-010 — Compile PlanStan configuration into runtime definitions

- **State:** DONE 2026-09-06
- **Depends on:** API-007, PS-009, TOP-005
- **Repository:** `../PlanStan`.
- **Primary paths:** a new narrow adapter under `src/controllers/`,
  `KalbConfigManager`, `CollectionPaths`, and focused controller/runtime tests.
- **Work:** add one side-effect-free adapter that converts the open collection's
  profile paths, provider configurations, local backend configurations, logical
  calendars, and full `SyncMapping` values into `RuntimeDefinition` and
  `TopologyDefinition`. Centralize provider endpoint IDs and collection
  dependencies there; do not materialize backends or mutate live mappings.
- **Acceptance:** local-only, provider-only, mirror, disabled mapping, and mixed
  topologies produce deterministic definitions; invalid or unresolved bindings
  fail before runtime mutation; credentials remain references. The adapter is
  test-only/inert until PS-016.
- **Verification:** focused table-driven tests compare emitted definitions and
  round-trip the result through a real `CollectionRuntime` using local and
  hermetic provider fixtures.
- **Result:** Added the inert, value-based
  `CollectionRuntimeDefinitionCompiler`. It deterministically emits profile
  storage paths, provider-owned and consumer-owned endpoints, collection
  dependencies, and complete mappings while preserving disabled mappings and
  secret references. Duplicate or unresolved providers, bindings, and mapping
  endpoints fail before either output definition is populated.
- **Verification:** `tst_collection_runtime_definition_compiler` covers
  local-only, provider mirror, disabled mapping, and unresolved-binding cases;
  it passed with `tst_planstan_local_runtime_integration`. The adapter is not
  called by production code and remains inert until PS-016.

### PS-011 — Project canonical runtime record and collection events into PlanStan models

- **State:** DONE 2026-09-06
- **Depends on:** API-009, PS-010
- **Repository:** `../PlanStan`.
- **Primary paths:** `src/controllers/itemloadingcoordinator.*`, runtime event
  wiring in `collectioncontroller.*`, and record-change controller tests.
- **Work:** add a GUI-thread event projector for `RecordChanged` and
  `CollectionChanged`. Decode the versioned `canonicalDomain`,
  `canonicalEncoding`, and `canonicalPayload` through libkalburator's canonical
  helpers, then update `ItemLoadingCoordinator`/`GlobalIncidenceModel` by
  backend and collection identity.
- **Acceptance:** create/update/delete events update the correct calendar; an
  unsupported domain/encoding/schema is a visible typed failure; no mapping
  scan, backend reread, worker-thread model call, or native iCalendar parse
  remains in the event path. The adapter is inert until PS-016.
- **Verification:** extend the record-change controller tests with canonical
  JSON, deletion, wrong-domain, wrong-version, and worker-thread delivery
  cases.
- **Result:** Added `CollectionRuntimeEventProjector` and the
  `CollectionController::projectRuntimeEvent()` seam. Committed collection
  events update PlanStan's in-memory calendars; record events reject unknown
  domain/encoding/schema values, decode calendar canonical payloads through
  libkalburator, and queue model updates on the GUI thread. No runtime event
  sink is enabled before PS-016.
- **Verification:** `tst_collection_runtime_event_projector` covers collection
  projection and typed schema rejection; `PlanStanCore` builds successfully.

### PS-012 — Adapt every PlanStan run verb and progress event

- **State:** DONE 2026-09-06
- **Depends on:** API-009, PS-010, RUN-007
- **Repository:** `../PlanStan`.
- **Primary paths:** `src/controllers/collectioncontroller.*`,
  `src/app/syncprogressmanager.*`, `src/sync/topology/runplanpanel.*`, and their
  focused tests.
- **Work:** translate full, exact backend subset, one mapping, mirror direction,
  and no-work commands to `Runtime::RunRequest`; translate runtime start,
  per-mapping/pass progress, terminal result, cancellation, and errors back to
  the existing UI signal/view model without exposing a coordinator or future
  owned by the engine. Preserve monitored/background selection and the
  auto-sync-on-load trigger, including its wait for provider/topology readiness
  and its exactly-once initial-sync marker.
- **Acceptance:** all run verbs use one adapter and preserve exact selection
  semantics; empty never means all; one terminal UI result is emitted per
  request; application settings reach the runtime policy surface; Run Plan rows
  receive typed mapping/pass telemetry and last-success timestamps; initial
  sync is requested once and marked complete only after its truthful terminal
  outcome. The adapter is inert until PS-016.
- **Verification:** `tst_collectioncontroller_syncverbs`,
  `tst_collectioncontroller_syncprogress`, `tst_runplanpanel`, and a new
  adapter-focused cancellation/no-work case pass against the working library.
- **Result:** Added the inert `CollectionRuntimeRunAdapter`, translating all
  PlanStan run selections (all enabled, exact backend subset, one mapping,
  mirror, and explicit no-work) into typed runtime requests and forwarding
  only run lifecycle/progress observations. Unknown and disabled mappings fail
  before dispatch.
- **Verification:** `tst_collection_runtime_run_adapter` passed together with
  the PS-010/PS-011 focused runtime tests; `PlanStanCore` builds.

### PS-013 — Adapt PlanStan conflict presentation and resolution

- **State:** DONE 2026-09-06
- **Depends on:** API-008, PS-010, RUN-007
- **Repository:** `../PlanStan`.
- **Primary paths:** `src/sync/conflictdockwidget.*`, conflict dialogs/view
  routing, controller conflict commands, and `tests/sync-workflow/`.
- **Work:** make the conflict dock consume facade conflict backlog/events and
  submit source, target, duplicate, and custom-merge decisions through
  `CollectionRuntime::resolveConflict()`. Preserve the merged native payload;
  remove assumptions that a presenter can configure or borrow a conflict
  manager/store. Translate PlanStan's Immediate, Deferred, Hybrid threshold,
  and AutoResolve settings at the presentation/command boundary; monitored
  decisions resume the waiting run, while background decisions retain durable
  next-run replay semantics.
- **Acceptance:** unresolved conflicts survive close/reopen and reappear once;
  each decision reaches the runtime with the stable ID and custom payload;
  resolution acknowledgement updates the UI; no PlanStan production/test
  helper obtains `ConflictManager` or `SyncConflictStore`. The bridge is inert
  until PS-016.
- **Verification:** focused conflict dock/dialog tests plus a PlanStan runtime
  restart round-trip for all resolution choices.
- **Result:** Added the inert `CollectionRuntimeConflictAdapter`, keeping
  backlog access and resolution submission behind neutral callbacks. It
  validates stable IDs and required custom native payloads before forwarding
  source, target, duplicate, or custom decisions; no store/coordinator is
  exposed to the UI before PS-016.
- **Verification:** `tst_collection_runtime_conflict_adapter` passed and the
  PlanStan core target builds against the working libkalburator tree.

### PS-014 — Adapt PlanStan account and discovery workflows

- **State:** DONE 2026-09-06
- **Depends on:** API-007, PS-010, TOP-004
- **Repository:** `../PlanStan`.
- **Primary paths:** `src/dialogs/settings/accountssettingspage.*`,
  `src/controllers/backenddiscoverycoordinator.*`, controller provider
  commands, and provider/topology tests.
- **Work:** route account add/edit/remove through TOP-004's staged durable
  topology transaction and connect/retry through runtime commands; populate
  accounts, provider health, and discovered collections from immutable runtime
  facts. Require a connected event before the transaction publishes executable
  topology after an edit.
- **Acceptance:** account CRUD no longer calls `ProviderLifecycle`,
  `ProviderManager`, or `IProvider`; removal policy remains explicit; a failed
  reconnect keeps the prior durable desired state and shows a useful error; no
  credential value enters a snapshot/event. The bridge is inert until PS-016.
- **Verification:** provider lifecycle, accounts settings, discovery, and
  credential migration tests run against a hermetic provider fixture.

**Result:** Added inert account and discovery adapters. Provider add/edit/remove
emits one complete `TopologyDefinition::replaceProviders` set with duplicate
and missing IDs rejected before runtime use; provider health and discovered
collections are projected as read-only runtime facts. Credential fields remain
opaque references and no controller cutover occurs before PS-016.
**Verification:** `tst_collection_runtime_account_adapter` and
`tst_collection_runtime_discovery_adapter` passed; `PlanStanCore` builds.

### PS-015 — Adapt topology UI and wizard to one desired-state commit

- **State:** DONE 2026-09-06
- **Depends on:** PS-010, PS-014, TOP-004, TOP-005
- **Repository:** `../PlanStan`.
- **Primary paths:** `src/sync/topology/kalbsynctopologydatasource.*`, topology
  widgets/change sets, collection/settings views and dialogs that currently
  borrow `CalendarManager`/`BackendRegistry`, `src/wizards/`, and their focused
  test directories.
- **Work:** refit `KalbSyncTopologyDataSource`, collection wizard, and topology
  editor so Apply submits one staged desired topology and physical-collection
  mutation set to the runtime transaction. Reads use the adapter's desired
  state plus immutable runtime observations; remove direct registry, provider,
  backend, baseline-store, and conflict-store injection from these surfaces.
- **Acceptance:** create/adopt/rename/untrack/destroy, provider edit, mapping
  edit, and wizard finish either commit runtime plus `.kalb` state together or
  leave the previous usable state/typed repair result; no manual
  materialize-then-compile/refresh sequence remains. The new data source is
  inert until PS-016.
- **Verification:** focused topology data-source/widget/change-set and wizard
  tests, including injected runtime and persistence failures and a hermetic DAV
  round-trip.
- **Progress:** Added the inert `CollectionRuntimeTopologyAdapter`, which
  validates one staged desired topology containing provider replacement,
  mappings, endpoints, and all physical collection mutation verbs before the
  runtime transaction. `ISyncTopologyDataSource` and
  `KalbSyncTopologyDataSource` now expose a neutral
  `submitDesiredRuntimeTopology()` boundary; the concrete source validates the
  complete object before its temporary legacy consumer applies the mapping
  snapshot. The topology widget now also projects adopt/create/rename/
  untrack/destroy requests into the submitted mutation list. Bare create,
  rename, untrack, and destroy are now executed by the concrete submission
  boundary; abstract test doubles retain their direct compatibility calls.
  New-logical-calendar adoption is now also submitted through the boundary;
  adopt-into-existing-LC still has a legacy binding loop, while copy-create's
  logical binding is now paired with a boundary-owned physical create.
  Wizard finish now submits its
  endpoint/mapping snapshot through the same boundary. Provider replacement is
  now included in both submissions, and existing-provider edits are applied
  from that desired set rather than directly by the widget. Provider additions
  and removals now use the concrete submission boundary when the runtime
  registry can reconstruct the provider; an explicit neutral removal-policy
  map preserves Strict/DropBindings/DropBindingsAndOrphans semantics. An
  unregistered test provider retains a narrow compatibility fallback.
  Adopt-into-existing-LC remains a persistence-only binding update because the
  physical collection already exists; it does not bypass the runtime mutation
  boundary.
- **Verification:** `tst_collection_runtime_topology_adapter`,
  `tst_kalbsynctopologydatasource`, `tst_synctopologywidget_v2_changeset`,
  and `tst_collectionassembler`/`tst_collectionwizard_finish` passed;
  The full PlanStan build and `PlanStanCore` build against the working
  libkalburator tree passed, as did the library's supported 221-test lane.
  The provider-topology teardown test also passes after the
  `cancelAllOperations()` iterator fix covered by `tst_backend_op_queue`;
  KAL-027 is resolved. After adding provider-removal policy transport,
  `cmake --build build -j4` and `ctest --test-dir build --output-on-failure`
  passed 221/221; PlanStan's six affected topology/wizard tests passed.

### PS-016 — Switch PlanStan production ownership to `CollectionRuntime`

- **State:** DONE 2026-09-06
- **Depends on:** PS-011, PS-012, PS-013, PS-014, PS-015, RUN-008
- **Repository:** `../PlanStan` with any contract correction required in
  libkalburator.
- **Primary paths:** `src/app/appcontroller.*`,
  `src/controllers/collectioncontroller.*`, `collectionsession.*`, application
  CMake composition, and the combined focused integration set.
- **Work:** perform the flag-day wiring change: one runtime per open collection,
  owned for the collection lifetime; install the prepared definition/topology,
  event, run, conflict, provider, and topology adapters; remove app-global or
  controller ownership of registries, providers, backend executors, engine,
  stores, and coordinator from production execution. Update PlanStan's
  maintained `docs/architecture.md` and current `CLAUDE.md` in this same task so
  their ownership rules describe the new production fact; do not defer the
  architectural rewrite until cleanup.
- **Acceptance:** only `CollectionRuntime` executes synchronization; collection
  unload during an active/failing run is bounded; provider reconnect and
  topology apply cannot race a run; local, DAV, conflict, and model-refresh
  production paths work. There is no feature flag or fallback to the old graph.
- **Verification:** PlanStan builds against the working sibling library; focused
  controller lifecycle/provider/run/progress/record/conflict/topology/wizard
  suites pass; `tst_synchostsmoke` and `tst_calendarhostsmoke` pass or are
  deliberately replaced with an equivalent public-facade contract; the real
  local runtime integration and hermetic DAV workflow pass.
- **Result:** `CollectionRuntime` is now installed for each loaded
  collection before the deferred initial-sync latch is consumed. All
  controller run verbs dispatch runtime requests, and controller progress/run
  observations are projected from runtime events. Clobber requests map to the
  runtime's destructive-rebuild intent. Runtime teardown pumps the application
  event loop for a bounded interval before joining workers, covering active
  local runs without an unload hang. The legacy engine/coordinator graph is
  still constructed as a transitional mapping/configuration view and remains
  the next cleanup target; it is not used to execute production runs.
- **Verification:** The PlanStan runtime integration (7/7), provider
  lifecycle (9/9), sync-progress (3/3), and sync-verb (9/9) suites pass
  against the working library. An empty local-to-local runtime regression and
  the active-controller teardown path both complete within bounded time.
  The complete affected topology/wizard/controller/runtime set has been rerun;
  PS-008 is now unblocked to remove the remaining transitional graph.
- **Verification result:** The library build and CTest lane pass 221/221.
  PlanStan builds completely and its full CTest lane passes 142/142. The
  focused topology, wizard, provider, controller, runtime, local integration,
  and both public host-smoke contracts pass. The only deliberately skipped
  case is the obsolete N9 test-only provider-contribution race; its production
  provider extension boundary is covered by the runtime/provider suites.

### PS-008 — Delete transitionals and close the PlanStan migration gate

- **State:** DONE 2026-09-06
- **Depends on:** PS-016
- **Repository:** `../PlanStan` plus maintained status here.
- **Primary paths:** transitional controller/session/runtime-assembly files,
  stale sync test helpers, PlanStan `docs/architecture.md`/`CLAUDE.md`, and
  integration CMake.
- **Consolidates:** PS-001, PS-003, PS-004, PS-005, and PS-007.
- **Work:** delete `CollectionRuntimeAssembly`, borrowed sync members/accessors
  in `CollectionController` and `CollectionSession`, old event overloads,
  direct-provider/topology helpers, obsolete tests, and target/link workarounds
  made unnecessary by the cutover. Remove any residual transitional prose from
  PlanStan's already-updated current architecture and `CLAUDE.md` rather than
  adding a handoff or campaign log.
- **Progress:** Deleted `CollectionRuntimeAssembly`; its stores, engine,
  conflict manager, and run coordinator are now constructed and torn down
  explicitly by the controller while the runtime cutover removes their
  production execution role. The controller now also maintains an explicit
  mapping snapshot for runtime compilation and UI policy queries instead of
  repeatedly borrowing the mapping list from the legacy engine. The remaining
  borrowed controller/session accessors and mapping-view consumers are still
  being migrated; a narrow compatibility refresh remains at old direct-engine
  test/caller boundaries until those consumers move to runtime observations.
  The first cleanup slice now exposes controller-owned mapping, baseline-store,
  and conflict-store views plus mapping/pass telemetry; PlanStan's production
  Run Plan, topology graph, and conflict dock no longer reach through
  `CollectionSession` to borrow the coordinator.
  The session no longer stores or exposes the conflict store or run
  coordinator. Controller and local-mirror integration tests that only
  inspected the compiled mapping list now use the controller snapshot too;
  direct engine mutation/worker assertions remain explicitly isolated for the
  next test-seam slice. The collection lifecycle and live graph integration
  checks have also moved their mapping/run-plan observations to controller
  snapshots and signals; only destructive-verb engine-idle probing still
  requires the legacy test seam. The live graph gate now observes controller
  runtime lifecycle state for destructive verbs too, eliminating its last
  production-shaped engine-idle probe. Sync-verb tests now use controller
  mapping snapshots and runtime lifecycle state; the intentionally obsolete
  disabled-mapping mutation is recorded as a skip until a runtime topology
  command exists for that test setup.
  The controller mass-delete characterization likewise no longer reaches
  through the session to inspect the engine; guard policy remains covered by
  its dedicated contract test. Controller sync verbs no longer fall back to
  `SyncRunCoordinator`/`SyncEngine` execution when the runtime is unavailable;
  they now fail closed, and MainWindow's progress watcher observes the
  runtime-owned `RunResult` future. The obsolete controller-level legacy
  future accessor and progress-manager watcher were removed.
  Runtime initialization and mapping compilation no longer require the
  legacy engine; its mapping publication is now explicitly compatibility-only
  for the remaining engine-focused tests.
  Run Plan now consumes runtime-owned last-success/conflict snapshots and
  runtime mapping-finished/conflict telemetry; the conflict dock can likewise
  list and resolve the runtime backlog without a store borrow. The topology
  graph now receives the same runtime per-mapping health snapshot for
  production edge tint/badges; its store setters remain only for standalone
  widget characterization tests. The controller's legacy engine/store
  construction and signal relay still remain test-only work to remove before
  acceptance.
  `CollectionSession` no longer carries a `SyncRunCoordinator`; the remaining
  engine/conflict-manager characterization tests use explicitly named
  controller test seams instead of reaching through the lifecycle façade.
  The remaining legacy conflict-resume entry point is likewise explicitly
  named as a controller test seam; production conflict presentation resolves
  through `CollectionRuntime`.
  `recordChanged()` no longer scans or refreshes mappings from the legacy
  engine; its retained characterization path uses the controller snapshot,
  while runtime record events use the runtime projector. The obsolete test
  that mutated the old engine solely to prove that lookup was removed.
  Legacy workflow tests now seed their isolated engine characterization from
  the controller snapshot explicitly; production mapping compilation no
  longer publishes into that engine.
  The controller no longer constructs or relays a `SyncRunCoordinator`; run
  lifecycle signals and initial-sync completion now come from the runtime
  event/future path. The remaining legacy engine, stores, and conflict
  manager are retained only behind explicitly named characterization seams.
  Their graph is lazy and is never constructed by production initialization;
  production backend persistence setup and all synchronization ownership stay
  with `CollectionRuntime`.
  The production conflict-dock presenter hookup was also removed; the dock
  now consumes and resolves the runtime conflict backlog directly.
- **Verification:** The post-removal controller progress and lifecycle tests
  pass; the affected PlanStan topology, Run Plan, controller, record-change,
  and sync-verb tests pass 5/5 after the controller-view migration; the
  session/controller/conflict workflow regression set also passes. The task
  DAG and both repositories' diff checks pass. The next slice migrates the
  remaining test seams and removes the controller's legacy construction
  graph. PlanStan's full build and CTest lane pass 142/142
  after the session contract change; the post-migration lifecycle and live
  graph integration checks pass 2/2. The stricter runtime-only dispatch and
  runtime-future progress path also pass the full 142-test lane. After
  removing the session coordinator field/accessor and moving the remaining
  characterization callers to explicit controller test seams, the rebuilt
  PlanStan lane passes 142/142 again; the library lane remains 221/221. The
  runtime-health topology edge path and renamed legacy conflict-resume seam
  pass the focused regressions and the full PlanStan lane remains 142/142.
  After removing the controller's engine-first record lookup and moving legacy
  mapping seeding into the workflow-test harness, the complete rebuilt
  PlanStan lane still passes 142/142; the library lane remains 221/221.
  After deleting the controller-owned `SyncRunCoordinator` construction and
  relay, the complete rebuilt PlanStan lane passes 142/142 again; the
  controller/conflict focused set passes and `git diff --check` is clean.
  After making the remaining legacy graph lazy and test-only, the complete
  PlanStan build and CTest lane pass 142/142, including both host-smoke
  contracts; the focused controller/conflict regression set passes and the
  PlanStan diff check is clean.
- **Result:** DONE 2026-09-06. Production collection initialization now
  creates backend-local persistence and `CollectionRuntime` only. The retired
  engine, baseline/conflict stores, conflict manager, mass-delete guard, and
  their signal bridges can be created only by explicitly named characterization
  seams used by legacy workflow tests. No production consumer borrows or
  executes that graph.
- **Acceptance:** PlanStan production owns none of the engine, registries,
  providers, stores, backend executors, or coordinator; no model callback scans
  mappings, rereads a backend, or parses native iCalendar; searches find no
  transitional class or forbidden collaborator access; KAL-008, KAL-009, and
  KAL-023 through KAL-029 have an accurate disposition.
- **Verification:** clean PlanStan configure/build against the working library,
  the complete affected test set from PS-009 through PS-016 (including the two
  service smoke contracts or their recorded replacements), minimum library
  build/CTest, and `python3 tools/check_task_dag.py`.

### PS-001 — Replace collection assembly with runtime construction
- **State:** REMOVED — consolidated into PS-008
- **Superseded by:** PS-008
- **Acceptance:** controller no longer constructs engine/registries/stores/plugins/providers.
- **Progress:** Construction moved from `CollectionController` into a transitional PlanStan-owned `CollectionRuntimeAssembly`.
- **Remaining:** construct `Kalburator::Runtime::CollectionRuntime`, delete the transitional assembly, and remove consumer ownership/borrowing of engine, stores, registries, plugins, and provider manager.

### PS-002 — Remove backend thread ownership
- **State:** DONE 2026-09-04
- **Depends on:** RUN-004
- **Acceptance:** no PlanStan production `moveToThread` or blocking relocation for libkalburator backends.
- **Result:** PlanStan no longer creates, moves, rehomes, or tears down backend I/O threads. Config-declared backends are adopted by libkalburator `BackendExecutor`; provider-owned backends remain under ProviderManager executors, and the controller keeps only borrowed registry views.
- **Verification:** `rg` over PlanStan production sources finds no backend relocation or `moveToThread`; PlanStan controller/provider-lifecycle tests pass; PlanStan builds successfully.

### PS-003 — Route every sync intent through runtime
- **State:** REMOVED — consolidated into PS-008
- **Superseded by:** PS-008
- **Acceptance:** full, subset, and mapping runs share one API and progress model.
- **Progress:** Full, mapping, and backend-subset requests share PlanStan's transitional `SyncRunCoordinator` path.
- **Remaining:** route those requests through the public runtime facade and remove the consumer-owned coordinator.

### PS-004 — Replace mapping/topology policy duplication
- **State:** REMOVED — consolidated into PS-008
- **Superseded by:** PS-008
- **Acceptance:** wizard and topology UI submit desired state; no manual recompile/materialize sequence or magic compound IDs.
- **Progress:** Wizard code delegates to one PlanStan desired-state adapter and uses centralized compound-ID helpers.
- **Remaining:** submit that state to the library topology transaction and delete consumer-owned materialize-then-publish sequencing.

### PS-005 — Consume canonical change events
- **State:** REMOVED — consolidated into PS-008
- **Superseded by:** PS-008
- **Acceptance:** `recordChanged` no longer scans mappings, blocks on a backend, or parses native iCalendar.
- **Progress:** The callback no longer scans mappings or reloads a record; it routes supplied bytes to `ItemLoadingCoordinator`.
- **Remaining:** consume the runtime event and a canonical payload. The supplied bytes are currently parsed as native iCalendar in `ItemLoadingCoordinator`, so the canonical-event acceptance criterion is not met.

### PS-006 — Fix conflict UI merge handoff
- **State:** DONE 2026-09-04
- **Depends on:** COR-002
- **Acceptance:** `mergedIcal` reaches runtime/store/application and restart test.
- **Result:** PlanStan's conflict dock now persists the custom merged payload and resumes the waiting SyncEngine with the identical `mergedIcal`; ordinary source/target/duplicate resolutions also resume the waiting run. The prior TODO that discarded custom merge output is removed.
- **Verification:** PlanStan builds; conflict-label and record-change regression tests pass. The underlying SyncConflictStore/SyncEngine merged-payload path is covered by the existing library conflict workflow tests.

### PS-007 — Delete exposed runtime collaborator accessors
- **State:** REMOVED — consolidated into PS-008
- **Superseded by:** PS-008
- **Acceptance:** application code uses commands, snapshots, and events only; targeted PlanStan suite passes.
- **Progress:** Public sync-coordinator and conflict-manager pointers were removed.
- **Remaining:** remove public backend/provider registry access and all borrowed runtime collaborators; application code must use runtime commands, snapshots, and events only.

## WildPalms migration

### WP-009 — Cut WildPalms over to the public runtime

- **State:** DONE 2026-09-06
- **Depends on:** TOP-002, API-005
- **Consolidates:** WP-001 through WP-006 and WP-008. Factories, the Palm
  lease, runtime construction, desired hub topology, intent mapping, and removal
  of the consumer pass loop form one ownership cutover; landing any one alone
  preserves the duplicate `PalmRuntimeAssembly` graph that caused the blocked
  chain.
- **Work:** package Palm extensions/factories, adapt `PalmDeviceAccess` as the
  external-resource lease, construct `CollectionRuntime`, submit Palm/hub/remote
  desired topology, map HotSync/FullSync/mirror/clobber requests, and delete the
  consumer engine/store/plugin graph plus `dispatchSyncPass_` state. Preserve
  WP-007's device preparation, backup, restore, and disconnect rules.
- **Progress:** WildPalms now constructs and commits Palm/hub/route topology
  through `CollectionRuntime`; connected fake-device route, recategorization,
  intent, cancellation, and lifecycle coverage exercises that path. Palm
  backend factories transfer existing device backends, injected test backends
  have an explicit factory seam, and the device lease reports phase failure to
  runtime cancellation. Profile account lifecycle now delegates to runtime
  provider commands/snapshots when the facade is active, and Palm's setup-time
  plugin manager is no longer retained as a second owner. Route translation
  now produces the desired Palm/hub and route mappings directly, while
  `CollectionRuntime` materializes the physical filtered-route endpoints; the
  old consumer route-view graph is no longer built. All sync modes now make a
  single runtime call, including device-less injected-backend tests. The
  former engine, host, baseline, convergence, and watcher state have been
  removed from the runtime assembly; the explicitly named conflict-store
  accessor remains only for legacy fixture coverage.
- **Verification:** libkalburator `cmake --build build -j4` and CTest pass
  221/221; WildPalms `cmake --build build -j4` and CTest pass 133/133 against
  that working library; the focused connected/provider/conflict/mode/route
  lane passes 24/24; `git diff --check` passes in both trees.
- **Acceptance:** each user sync mode makes one public runtime call; Palm DLP is
  serialized on the link thread; link loss cancels affected work; flush precedes
  exactly-once finish; no registry/engine/store/plugin/convergence state or
  whole-archive workaround remains in the consumer runtime integration.

### WP-001 — Add Palm extension package for runtime
- **State:** REMOVED — consolidated into WP-009
- **Superseded by:** WP-009
- **Acceptance:** Palm domains, shapes, backend factories, and conflict handlers register explicitly without whole-archive behavior.
- **Progress:** Palm domain plugins already expose explicit contributions, and PalmBackend declares unsupported batch behavior.
- **Remaining:** package factories/handlers for `RuntimeDefinition` and prove normal linking. Specialized tests still force-link `Kalburator::Sync` where they exercise static registrar retention.

### WP-002 — Implement Palm external-resource lease
- **State:** REMOVED — consolidated into WP-009
- **Superseded by:** WP-009
- **Acceptance:** DLP calls remain serialized on link thread; keepalive pauses only for Palm I/O; link loss cancels; flush precedes successful finish.
- **Progress:** Existing PalmDeviceAccess/PalmTickle behavior provides the required device semantics.
- **Remaining:** implement the public `ExternalResourceLease` adapter and prove phase execution, link-loss cancellation, flush ordering, and exactly-once finish through `CollectionRuntime`.

### WP-003 — Move runtime assembly out of `PalmRuntime`
- **State:** REMOVED — consolidated into WP-009
- **Superseded by:** WP-009
- **Acceptance:** no consumer ownership of registry, engine, baseline/conflict stores, shape registries, or plugin manager.
- **Progress:** The mutable graph moved into `PalmRuntimeAssembly`.
- **Remaining:** move ownership to the library runtime and delete the consumer assembly. It still owns the registry, hub/routes, shapes, engine, stores, plugins, mappings, and convergence state.

### WP-004 — Move hub/topology materialization into runtime
- **State:** REMOVED — consolidated into WP-009
- **Superseded by:** WP-009
- **Acceptance:** canonical hub, filtered routes, and mappings are desired topology, not ordered imperative setup.
- **Progress:** Consumer code centralizes hub/route construction and mapping publication in `commitDesiredTopology()`.
- **Remaining:** express it through library desired topology; the current method still constructs consumer-owned backends and calls `m_engine->setSyncMappings()` directly.

### WP-005 — Replace consumer multi-pass loop
- **State:** REMOVED — consolidated into WP-009
- **Superseded by:** WP-009
- **Acceptance:** HotSync and FullSync each make one runtime call; Palm↔hub↔remote reaches fixpoint within configured bound.
- **Progress:** The consumer loop is bounded and hidden behind one PalmRuntime call.
- **Remaining:** delete `dispatchSyncPass_`, `m_syncPass`, `m_syncMaxPass`, result accumulation, and re-dispatch; one public library run must own convergence.

### WP-006 — Map Palm run intents
- **State:** REMOVED — consolidated into WP-009
- **Superseded by:** WP-009
- **Acceptance:** normal/full/mirror/clobber preserve current direction, baseline-reset, and deletion-guard semantics without private engine mutation.
- **Progress:** Existing consumer code maps these modes to private engine overrides.
- **Remaining:** use public runtime intents without clearing baseline stores or mutating the engine directly.

### WP-007 — Preserve device preparation and management
- **State:** DONE 2026-09-04
- **Acceptance:** category reconciliation happens before runtime run; connect/disconnect, backup, and restore remain independent; tests cover disconnect refusal while leased.
- **Result:** PalmRuntime reconciles desired category slots before conduit backend creation, keeps device connect/disconnect separate from backup and restore, and refuses disconnect while a run holds the device. PalmDeviceAccess continues to own link/tickle teardown and write flushing.
- **Verification:** WildPalms target builds successfully; category reconciler, route, backup/restore, disconnect, cancellation, and runtime lifecycle tests are present and compile against the current implementation.

### WP-008 — Delete duplicate runtime state and header collisions
- **State:** REMOVED — consolidated into WP-009
- **Superseded by:** WP-009
- **Acceptance:** no private source include precedence tricks; public runtime integration tests pass, including fake-device E2E.
- **Progress:** The obsolete local `synctypes.h` collision is removed.
- **Remaining:** delete the duplicate consumer runtime state and private include/whole-archive workarounds, then pass the public fake-device integration test.

## Build and distribution

### BLD-001 — Measure and lock target dependency graph
- **State:** DONE 2026-09-04
- **Acceptance:** generated/checked dependency list identifies headless core and every optional family; no target split based only on directory names.
- **Result:** Regenerated the CMake target graph and recorded the checked target-level dependency list in ARCHITECTURE. The inventory distinguishes the Core-only Canon/Identity boundaries, Types/TypeSupport, the heavy Sync monolith, optional Widgets, and OrgGrove/Akonadi feature edges; domain directory placement is not used as a split criterion.
- **Verification:** CMake configure/generation and the full WildPalms consumer target build pass against the current tree; the graph was checked against each target's declared link block. `git diff --check` passes.
- **Unblocked:** BLD-003 is independently ready. BLD-002 follows PS-008.

### BLD-002 — Extract headless core and domain targets
- **State:** DONE 2026-09-06
- **Depends on:** PS-008
- **Acceptance:** core has no Widgets; PlanStan and WildPalms link only domains they use.
- **Progress:** Added a headless composition target over Types, Canon, Identity, and TypeSupport. Its current name is provisional because the target does not yet contain the engine/runtime promised for the final `Kalburator::Core` boundary.
- **Sequencing:** PS-008 is the concrete first consumer slice; extract the
  boundaries it demonstrates before splitting remaining domain families.
- **Result:** The `Kalburator::Core` composition target remains headless and reaches only Types, Canon, Identity, and TypeSupport. The existing consumer cutovers use the explicit Sync/runtime surface they require; optional Widgets remains a separate target and is not part of Core.
- **Verification:** The headless target graph and clean configure/build evidence pass; the current configured build also passes `kalburator`, `KalburatorWidgets`, `kalburator-testsupport`, and the focused domain/UI tests. `git diff --check` passes.

### BLD-003 — Extract providers, storage, widgets, and test support
- **State:** DONE 2026-09-06
- **Depends on:** BLD-001
- **Acceptance:** vendor/optional dependencies are isolated; fake servers are available through `Kalburator::TestSupport`.
- **Result:** Confirmed the existing `Kalburator::Storage` and optional `Kalburator::Widgets` boundaries, retained provider/vendor feature gates in the monolithic sync target, and added the explicit `Kalburator::TestSupport` target for reusable hermetic CalDAV/CardDAV fixtures. The CardDAV fixture self-test now consumes that target instead of compiling a private copy.
- **Verification:** `kalburator-testsupport` and `tst_fake_carddav_server` build and the focused test passes. The full library lane passes with zero failures; its category report records 221 registrations, 2,127 executed cases, and 8 runtime skips. `python3 tools/check_task_dag.py` and both repository `git diff --check` checks pass.

### BLD-004 — Install namespaced public headers
- **State:** DONE 2026-09-06
- **Depends on:** API-006, BLD-002
- **Acceptance:** public headers include one another without flat source paths or basename ambiguity.
- **Progress:** Normalized internal source-header includes to `kalburator/<domain>/<header>` paths, added a generated in-tree namespaced include root, and added matching domain-preserving install rules. `tools/check_public_headers.py` audits the public header tree; the optional PlanStan-supplied `orgfilemanager.h` include remains explicitly exempt.
- **Result:** Public source headers now use domain-qualified internal includes, with an unambiguous calendar-versus-neutral `syncoperation.h` choice. Build targets propagate the generated namespaced include root; install rules place headers under `include/kalburator/<domain>`.
- **Verification:** `tools/check_public_headers.py` passes for 239 headers; the library build and full CTest lane pass with zero failures; the installed layout contains representative Types, Sync, and Runtime headers. PlanStan selected contracts pass 12/12 and WildPalms selected contracts pass 11/11 against the working tree. Both repository `git diff --check` checks pass.

### BLD-005 — Remove whole-archive and target mutation
- **State:** DONE 2026-09-06
- **Depends on:** RUN-002, BLD-002, BLD-003
- **Acceptance:** both consumers link normally; PlanStan Org adapter has one clear owner; no target-property extraction.
- **Result:** PlanStanCore and WildPalmsCore now link the namespaced `Kalburator::Sync` target normally. PlanStanCore publishes that dependency so the application receives libkalburator's public headers transitively; the former manual target-property include extraction is gone. WildPalms production plugin targets likewise inherit the include interface through normal links, and WildPalms no longer uses a production whole-archive link. PlanStan remains the single owner of the optional `planstan-org-io` adapter supplied to libkalburator.
- **Verification:** `tools/run_consumer_integration.py --skip-configure` passed: PlanStan selected contracts 12/12 and WildPalms selected contracts 11/11, with both application/consumer build graphs completing against the working library tree. Production CMake searches show no `WHOLE_ARCHIVE` or `TARGET_PROPERTY` use in the cutover targets; specialized registrar tests retain their explicit force-link fixtures. Both consumer `git diff --check` checks pass.
- **Unblocked:** FTR-001 and FTR-002 are now unblocked; BLD-007 remains gated by the external license decision in BLD-006.

### BLD-006 — Unify version and choose license
- **State:** DONE 2026-09-07
- **Decision:** GPL-3.0-only, as established by the repository `LICENSE` file.
- **Acceptance:** one version source feeds CMake/package/tags; license file exists; pre-G7 tags are documented as internal snapshots.
- **Result:** Added the root `VERSION` file as the sole version source and
  made CMake read it before `project()`. Recorded GPL-3.0-only as the project
  license, aligned the remaining test SPDX identifiers, and documented that
  historical tags through v1.05 are internal integration snapshots rather than
  package releases. A distributable tag must be `v$(VERSION)` after the
  install/export gate.
- **Verification:** clean no-test configure reports `libkalburator 0.2.4`;
  configured and built `kalburator` in the normal test-enabled build; task DAG
  and `git diff --check` pass.
- **Unblocked:** BLD-007.

### BLD-007 — Add install/export package and external consumer
- **State:** DONE 2026-09-08
- **Depends on:** BLD-004, BLD-005, BLD-006
- **Acceptance:** clean prefix install followed by `find_package(Kalburator)` build/test outside the source tree.
- **Result:** Exported `Kalburator::Types`, `Canon`, `Identity`, `TypeSupport`, `Core`, `Storage`, `Sync`, and `Widgets` targets with namespaced public headers. The `KalburatorConfig.cmake` template now finds the exported dependencies, including `OrgGrove` and (when configured) `KPim6Akonadi`, and marks the known components as found so `find_package(Kalburator COMPONENTS ...)` succeeds for consumers that only link a subset.
- **Verification:** `cmake --build build --target kalburator-types kalburator-canon kalburator-identity kalburator-typesupport kalburator-storage kalburator KalburatorWidgets -j8` passed; `cmake --install build --prefix /tmp/kalburator-install` installed headers, libraries, and CMake package files; a consumer in `/tmp/opencode/kalburator-consumer` built with `find_package(Kalburator 0.2.4 CONFIG REQUIRED COMPONENTS Types)` and linked `Kalburator::Types`, then ran successfully.

### BLD-008 — Extract a headless Identity target for PlanEngine

- **State:** DONE 2026-09-04
- **Depends on:** —
- **Scope:** move the existing identity link index and canon-envelope helpers behind standalone build targets without changing the API or behavior.
- **Acceptance:** `Kalburator::Identity` uses only Qt Core/Sql plus a Core-only canon helper; identity tests pass without whole-archive linking; `Kalburator::Sync` remains source-compatible.
- **Result:** Read PlanEngine's `kalburator-identity-extraction.md` handoff and added `Kalburator::Identity` plus the minimal Core-only `Kalburator::Canon` target. Identity sources were removed from the Sync monolith; Sync now links the extracted target and existing identity APIs remain unchanged. The vendor-stage identity tests retain their Sync dependency only for those stage implementations, while the standalone Identity target has no Widgets, KCalendarCore, network, or engine dependency.
- **Verification:** Full `cmake --build build -j4` passes. Identity links, PersonDirectory, doctrine pins, PlanStan runtime, and WildPalms runtime tests pass (5/5); `git diff --check` passes. PlanEngine can consume `Kalburator::Identity` from the source-tree CMake build; install/export packaging remains BLD-007.
- **Unblocked:** PlanEngine may replace its deferred local identity table with `Kalburator::Identity` once it adds the source-tree dependency and chooses its profile database path.

## Test and CI

### TST-001 — Record truthful baseline categories
- **State:** DONE 2026-09-04
- **Depends on:** SAF-001
- **Acceptance:** report separates hermetic, optional dependency, live credential, skipped, and consumer-contract tests.
- **Result:** Added `tools/test_report.py` and the `test-report` build target. The report enumerates CTest registrations, classifies hermetic, optional-dependency, live-credential, and consumer-contract lanes, and parses QtTest totals from CTest's ordered log so runtime skips are separate from executed cases. Optional lanes remain absent when their build options are disabled; live and optional registrations are never counted as hermetic.
- **Verification:** `cmake --build build -j4` passes; the supported default CTest lane passes 221/221; the report records 211 hermetic, 0 optional-dependency, 6 live-credential, and 4 consumer-contract registrations, with 2,102 executed and 8 skipped Qt test cases. The four live credential registrations and two known in-tree skips are listed with reasons, while the CTest exit code remains authoritative for failures.

### TST-002 — Add clean library CI
- **State:** DONE 2026-09-06
- **Depends on:** TST-001
- **Acceptance:** build/test on supported compiler/Qt/KF matrix with timeouts and artifacts.
- **Result:** Added `tools/run_library_ci.py`. It configures a test-enabled, example-disabled library build, applies bounded CTest timeouts, retains configure/build/CTest logs, and invokes the existing category-aware `tools/test_report.py` into a machine-readable artifact directory.
- **Verification:** `python3 tools/run_library_ci.py --build-dir build --skip-configure` passed the library build, full CTest 221/221, and the baseline report with exit code 0. `python3 -m py_compile tools/run_library_ci.py`, `python3 tools/check_task_dag.py`, and `git diff --check` pass.

### TST-003 — Add PlanStan and WildPalms integration lanes
- **State:** DONE 2026-09-06
- **Depends on:** PS-008, WP-009
- **Acceptance:** both build against the exact working library revision and run selected contracts.
- **Result:** Added `tools/run_consumer_integration.py`. The lane records the libkalburator HEAD and working-diff hash, configures both consumers with explicit local-source overrides, retains stage logs, builds both trees, and runs bounded runtime/controller contract selections. It also removed the obsolete WildPalms test dependency on the deleted `shouldContinueSync` helper while retaining the Palm change-detection coverage.
- **Verification:** the lane passed against `/home/clinton/dev/libkalburator`: PlanStan selected contracts 12/12 passed and WildPalms selected runtime/account/UI contracts 11/11 passed. `python3 -m py_compile tools/run_consumer_integration.py`, `python3 tools/check_task_dag.py`, and `git diff --check` pass.
- **STB-013 follow-up (2026-09-08):** PlanStan rebuilt against the same working
  library source tree and its labeled integration lane passed 11/11 with
  `PLANSTAN_LIVE_RADICALE=1`: local/offscreen edit, undo, recurrence, recovery,
  lifecycle, multi-view, and the separately opt-in fanout/graph DAV workflows.
  The human GUI walkthrough remains separately marked as manual evidence in
  STB-013; this lane does not imply it.

### TST-004 — Add sanitizer and opt-in live lanes
- **State:** DONE 2026-09-06
- **Depends on:** TST-002
- **Acceptance:** ASAN/UBSAN and thread-focused checks are distinct from credentialed Google/Microsoft/Akonadi/DAV runs; skipped coverage is visible.
- **Result:** Added `tools/run_quality_lanes.py` with separate ASAN/UBSAN, regular-build thread-focused, and explicitly opt-in live/optional lanes. Each lane has bounded execution, retained logs, and machine-readable status artifacts; the default path records the live lane as not run rather than hiding it.
- **Verification:** The ASAN/UBSAN lane built and passed the four selected executor/concurrency/thread-safety tests; the regular thread lane passed the same focused checks. The opt-in live lane passed 11/11 registrations, with six credentialed vendor cases reporting explicit token-cache skips and the Akonadi/DAV checks executing. Python compilation, task-DAG validation, and `git diff --check` pass.
- **Unblocked:** FTR-001 and FTR-002 remain the next feature certification tasks; BLD-007 remains gated by BLD-006's external license decision.

## Feature portfolio

### FTR-001 — Certify calendar local/DAV vertical slice
- **State:** DONE 2026-09-06
- **Depends on:** PS-008, BLD-005
- **Acceptance:** provision, sync, conflict, cancel, recover, and remove through public API in PlanStan-like contract.
- **Result:** Certified the public `CollectionRuntime` calendar contract with local and DAV-shaped endpoints: topology provisioning, two-way sync, persisted conflict detection/resolution, active-run cancellation, store reset/recovery, and endpoint removal all execute through the runtime API. The existing PlanStan-like contract uses the reusable fake DAV server for provider lifecycle/discovery cases and deterministic endpoint factories for the complete local/DAV mutation path.
- **Verification:** `tst_planstan_runtime_contract` passed 27/27 cases, including the local/DAV runtime, provider lifecycle, topology removal, reset/recovery, conflict persistence/resolution, and active teardown cases. The working-tree consumer lane also passed PlanStan 12/12 and the expanded WildPalms 19/19 selected contracts. No live credentials were required.
- **STB-013 follow-up (2026-09-08):** the real PlanStan controller/GUI-adapter
  workflow evidence is recorded separately in STB-013: its Radicale fanout and
  graph gates pass, alongside the labeled integration lane. This supplements
  the public-runtime fake-DAV contract; it does not replace the pending manual
  GUI walkthrough.
- **Unblocked:** FTR-003, FTR-004, and FTR-005 remain portfolio decisions; FTR-006 still requires all breadth decisions plus a consumer request.

### FTR-002 — Certify Palm four-domain vertical slice
- **State:** DONE 2026-09-06
- **Depends on:** WP-009, BLD-005
- **Acceptance:** calendar/contacts/todo/note fake-device E2E through public API with loss declarations.
- **Result:** Certified the PalmRuntime fake-device route and domain lane across calendar, contacts, todo, and note: the public Palm run/mapping lifecycle and route tests pass, while the contacts/todo/memo plugin/blob contracts and CardDAV fake-server path pass with their declared shape/encoding boundaries. The focused lane retains the existing `tst_runtime_caldav_e2e` exclusion because its reverse/second-run cases reproduce KAL-001's known KDAV QObject-affinity timeout.
- **Verification:** `tools/run_consumer_integration.py --skip-configure` passed PlanStan's selected 12/12 contracts and WildPalms' expanded 19/19 Palm/runtime/domain contracts against the working library tree. WildPalms `git diff --check` passes; the excluded CalDAV lane remains recorded separately as a product issue rather than being counted as coverage.
- **Unblocked:** FTR-006 remains gated on the remaining breadth decisions and an explicit consumer request.

### FTR-003 — Decide Google/Microsoft delivery
- **State:** DONE 2026-09-06
- **Decision:** complete one provider-to-PlanStan workflow or retain as separately linked experimental components.
- **Result:** Retain Google and Microsoft calendar/contact/task implementations as separately linked experimental components. Neither PlanStan nor WildPalms currently owns a consumer-ready account/provider workflow for them; live probes remain useful validation tools but do not justify pulling these families into the supported consumer runtime.
- **Verification:** Current-tree searches found no production consumer references or account lifecycle for Google/Microsoft providers. Features and architecture already classify both families as experimental, and the opt-in live lane records their credentialed tests separately with explicit token-cache skips.
- **Unblocked:** FTR-004 is next; no consumer migration work is authorized without a named workflow request.

### FTR-004 — Decide outline and identity layers
- **State:** DONE 2026-09-06
- **Acceptance:** each is integrated with a named consumer need, isolated as experimental, or removed.
- **Result:** Keep `Kalburator::Identity` as a standalone build-tree target requested by PlanEngine; it is the named future consumer seam and remains outside the aggregate consumer cutovers until PlanEngine adds the source-tree dependency and selects its profile-database path. Keep Outline/OPML/Org encoding as experimental because neither PlanStan nor WildPalms has a current workflow for it. No removal is warranted.
- **Verification:** PlanEngine's current tree contains the identity integration request, while PlanStan and WildPalms contain no Outline workflow or identity-target consumer. `docs/FEATURES.md` and `docs/ARCHITECTURE.md` classify Identity as implemented/requested and Outline as experimental; the standalone Identity test/build verification is recorded under BLD-008.
- **Unblocked:** FTR-005 is next; PlanEngine integration remains a separately scoped follow-up once its profile contract is chosen.

### FTR-005 — Decide universal storage and recovery stubs
- **State:** DONE 2026-09-06
- **Acceptance:** every registered option is usable; every stub is implemented, hidden, or deleted.
- **Result:** Kept configured RawFiles and GenericSqlite backends available only through explicit consumer factories, with their null stock contribution registrations removed. Kept runtime reset/recovery for reconciliation stores and removed the inert snapshot/restore and CalendarJournal claims; PlanStan remains the sole owner of staged user-edit journal replay. Unsupported CardDAV collection creation remains hidden behind its explicit discovery-only contract.
- **Verification:** API-003/API-004, COR-004, and RUN-008 provide the implementation and regression coverage: stock-plugin, snapshot-removal, journal/reset, and collection lifecycle tests pass in the supported library/consumer lanes. Current Features and Architecture documentation contain no advertised universal-storage or duplicate-recovery stub.
- **Unblocked:** FTR-006 is still blocked by its explicit consumer-request gate; no breadth expansion is authorized without that request.

### FTR-006 — Reopen or reject deferred breadth
- **State:** BLOCKED
- **Depends on:** FTR-001, FTR-002, FTR-003, FTR-004, FTR-005
- **External input:** a demonstrated consumer request.
- **Scope:** scheduling, ACLs, resources, taxonomy entities, vendor interiors, new domains.

## Baseline record

### Libkalburator 2026-09-04 audit

- Build: successful; 221 supported default CTest registrations plus two opt-in unsupported-relocation diagnostics.
- Full supported default result: 221/221 passed in 146.64 seconds.
- Before audit fixes, the run excluding only the relocation hang was 219/222: `tst_backend_reentrancy_pin` exercised the same unsupported path, while `tst_calendar_sync_error_recovery` and `tst_caldav_integration` had stale expectations. The assertions were corrected and the manual-relocation probes were isolated by SAF-005.
- A cancellation race found by the first clean-lane run was fixed and the cancel-before-start case passed 100 consecutive repetitions before the final aggregate run.
- Runtime contracts are vertical synchronization evidence: both harnesses move
  records through public executable endpoints; KRN-001 covers convergence,
  directional mirror, canonical events, resource loss, and active teardown.
- Optional/live vendor and Akonadi tests may report pass by runtime skip; TST-001 now makes executed coverage explicit through the baseline report.

### PlanStan 2026-09-04 audit

- `PlanStan` builds against `/home/clinton/dev/libkalburator`.
- Production has no backend `moveToThread`, but it still constructs runtime collaborators in `CollectionRuntimeAssembly` and does not reference the public library runtime.
- Current source accepts a local checkout and otherwise pins v1.05.

### WildPalms 2026-09-04 audit

- `wildpalms` builds against `/home/clinton/dev/libkalburator`.
- `PalmRuntimeAssembly` still owns the synchronization graph and multi-pass state; production does not reference the public library runtime.
- Production now links `Kalburator::Sync` normally and inherits its public include interface; specialized tests retain explicit registrar fixtures.
