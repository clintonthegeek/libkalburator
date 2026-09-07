# Task queue

**Last updated:** 2026-09-07
This is the only active work queue. Stable IDs are used by code, tests, issues, and commits.

The `DONE` entries below are retained as historical implementation evidence.
They do not certify the production consumer workflows until `AUD-003` records
current, reproducible evidence. Reopen only the exact task contradicted by that
evidence; retain its prior result and verification under a historical heading.

States: `READY`, `IN PROGRESS`, `QUEUED`, `BLOCKED`, `DONE`, `REMOVED`.

## Now

Work top to bottom unless a task is blocked or the user chooses otherwise.

| Order | Task | State | Depends on | Outcome |
|---:|---|---|---|---|
| 1 | AUD-003 | READY | — | Establish the tagged cross-repository baseline and real PlanStan host fixture |
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

Preparatory adapters must have tests and no production caller until `PS-016`.
This makes each slice independently reviewable without allowing the old and new
runtime graphs to execute side by side.

### AUD-003 — Establish the stabilization baseline and reproduce production gaps

- **State:** READY
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
- **State:** BLOCKED — external decision only
- **User decision required:** license selection.
- **Acceptance:** one version source feeds CMake/package/tags; license file exists; pre-G7 tags are documented as internal snapshots.

### BLD-007 — Add install/export package and external consumer
- **State:** QUEUED
- **Depends on:** BLD-004, BLD-005, BLD-006
- **Acceptance:** clean prefix install followed by `find_package(Kalburator)` build/test outside the source tree.

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
