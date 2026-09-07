# Known issues

**Last reviewed:** 2026-09-07
This is the only active defect and risk list. Historical finding numbers are not reused.

States: `OPEN`, `INVESTIGATING`, `BLOCKED`, `RESOLVED`. Resolved entries remain only until the next release, then leave this file.

## Critical

### KAL-032 — Production synchronization ownership is not certified

- **State:** INVESTIGATING
- **Affects:** PlanStan production collection load, edit, save, topology,
  account/discovery, conflict, and close workflows; dependent library claims.
- **Evidence:** historical `DONE` adapter and direct-runtime tests do not
  exercise the real `CollectionController` host path. The 2026-09-07 baseline
  review found unverified topology commits, policy/secret input wiring, save
  acknowledgment boundaries, and legacy construction paths.
- **Required outcome:** `AUD-003` supplies a controllable production host
  fixture and reopens only the contradicted task IDs with repeatable acceptance
  failures. No release or migration-completion claim is valid before the
  workflow certification gate.
- **Tasks:** AUD-003; then evidence-selected TOP-002/TOP-004/TOP-005,
  RUN-008, PS-010 through PS-016, PS-008, TST-003, and FTR-001.

### KAL-001 — Backend relocation can hang and violate QObject affinity

- **State:** RESOLVED 2026-09-04
- **Affects:** unsupported callers that manually move DAV backends; optional diagnostics
- **Evidence:** Provider-created backends use `BackendExecutor` and PlanStan production no longer moves them. The unsupported legacy relocation suite still times out, and the reentrancy suite produces cross-thread QObject parenting warnings followed by three KDAV transfer timeouts.
- **Product impact:** no current production consumer path was found. Manual relocation is outside the supported contract; consumers use library-owned executors or an external-resource lease.
- **Required outcome:** preserve lifecycle coverage through executors and remove or isolate consumer-managed relocation probes.
- **Resolution:** SAF-005 removed the two consumer-managed relocation probes from default registration while retaining them as explicitly labeled opt-in diagnostics. Executor-owned lifecycle coverage remains in the supported lane; KRN-001 proves the same ownership through `CollectionRuntime`.
- **Tasks:** SAF-005, KRN-001

### KAL-002 — Failure/cancellation can leave a future unfinished and engine marked active

- **State:** RESOLVED 2026-09-03
- **Affects:** both consumers
- **Evidence:** `SyncEngine::finishRun()` now owns all normal run completion paths and clears active state before future publication. Cancellation preserves `cancelled=true` in both the future result and `lastSyncResult()`, and the parallel cancellation regression admits a subsequent run. Worker cancellation sets the atomic stop flag synchronously before posting its nested-event-loop wake-up. The per-run reset is likewise synchronous before dispatch, so it cannot arrive after and erase an immediate cancellation.
- **Resolution:** SAF-003 plus the SAF-005/KRN-001 audit follow-ups; failure, cancellation, next-run admission, active-runtime destruction, and bounded worker-teardown coverage pass. The strengthened immediate-cancel case passes 100 consecutive repetitions.

### KAL-003 — Remote load failure may be interpreted as an empty successful first sync

- **State:** RESOLVED 2026-09-03
- **Affects:** first sync against remote backends; potential data loss/false success
- **Evidence:** failing relocation run logged remote load errors, harvested zero baselines, and continued orchestration; the synchronous load API has no strong error channel.
- **Resolution:** SAF-004 added `RecordLoadResult`; the engine now rejects failed source/target reads before writes or baseline/token harvesting, and CalDAV reports failed fetch operations through the typed result. Legacy direct list callers remain available during consumer migration.
- **Tasks:** SAF-004

### KAL-004 — Credentials can be persisted or emitted in error logs

- **State:** RESOLVED 2026-09-04
- **Affects:** DAV users and live tests
- **Evidence:** provider saves emit opaque `passwordRef` values and diagnostics redact credential-bearing URLs. PlanStan and WildPalms install persistent KWallet-backed `SecretStore` adapters before profile loading.
- **Required outcome:** secret references plus central redaction of library and dependency error text.
- **Resolution:** SEC-001 and SEC-002; the library retains only the reference contract while each host owns KWallet persistence and unlock policy.

## High

### KAL-005 — Collection-property three-way baseline is never reloaded

- **State:** RESOLVED 2026-09-03
- **Affects:** calendar color/description reconciliation
- **Evidence:** `dispatchSync` now reloads the persisted collection baseline before the property phase; property applies are acknowledged and failures terminate the mapping.
- **Required outcome:** real baseline read, mapping conflict policy, acknowledged writes, asymmetric-edit regression — implemented in COR-001.
- **Tasks:** COR-001

### KAL-006 — Custom merge payload is not durable and is dropped by PlanStan UI

- **State:** RESOLVED 2026-09-03
- **Affects:** conflict resolution
- **Evidence:** `SyncConflictStore` persists `merged_ical`; deferred resolution APIs accept it; restart rehydration supplies it to the existing canonical promotion/write path.
- **Required outcome:** payload flows UI → runtime → store → restart → apply with content-version protection — implemented in COR-002.
- **Tasks:** COR-002, PS-006

### KAL-007 — Target promotion lacks the non-empty-to-empty transform guard

- **State:** RESOLVED 2026-09-04
- **Affects:** malformed or unsupported target representations
- **Evidence:** source and target demotion now reject an unexpectedly empty transform before either side is written.
- **Resolution:** COR-003 applies the same guard to source and target demotion and blocks writes after a transform-empty failure.
- **Tasks:** COR-003

### KAL-008 — Provider/topology/mapping changes are non-atomic

- **State:** OPEN
- **Affects:** PlanStan setup/topology UI; WildPalms route setup
- **Evidence:** historical TOP-002 made endpoint/mapping replacement atomic inside the
  runtime, TOP-004 adds a consumer-owned durable desired-state participant,
  and TOP-005 now routes capability-checked physical collection commands
  through the same transaction. `applyTopology()` still excludes some
  provider configuration
  mutation and `updateProvider()` currently mutates the live provider
  and invalidates executable topology before PlanStan's `.kalb` state can
  participate. PlanStan also separately invokes backend/calendar operations.
- **Required outcome:** runtime transaction with durable desired-state
  participation and capability-checked physical collection mutations, with
  commit/rollback or explicit repair state.
- **Tasks:** RUN-003, TOP-002, TOP-004, PS-014, PS-015, WP-009

### KAL-009 — Consumers duplicate runtime assembly and run policy

- **State:** OPEN
- **Affects:** maintainability and behavioral drift in both consumers
- **Evidence:** the runtime run path and adapter tests exist, but AUD-003 must
  establish whether PlanStan production has one operational `CollectionRuntime`
  per open collection. The controller still constructs a transitional engine,
  stores, and mapping view for compatibility. WildPalms still owns
  `PalmRuntimeAssembly`, the engine, stores, plugins, mappings, and convergence
  state; `dispatchSyncPass_()` still re-dispatches passes.
- **Required outcome:** library-owned collection/profile runtime with consumer adapters.
- **Tasks:** KRN-001, RUN-003, TOP-002, PS-009 through PS-016, PS-008, WP-009

### KAL-010 — Build boundary depends on source layout and linker side effects

- **State:** OPEN
- **Affects:** both consumers and external reuse
- **Evidence:** package export is still absent; production target-property extraction, whole-archive links, and PlanStan target mutation were removed by BLD-005, while specialized test fixtures retain explicit registrar force-linking where required.
- **Required outcome:** modular exported targets, namespaced headers, explicit registration, clean external consumer.
- **Tasks:** BLD-001 through BLD-007

### KAL-027 — Provider-backed PlanStan fixture could double-free during teardown

- **State:** RESOLVED 2026-09-06
- **Affects:** PlanStan provider/topology test fixture teardown; provider-backed
  collection unload paths require confirmation before the production cutover.
- **Evidence:** `tst_kalbsynctopologydatasource_providers` reproduces a
  `SIGSEGV` in `BackendExecutor::shutdown()` while a `LocalBackend` is being
  destroyed on the executor thread. The provider changeset test likewise
  completes its assertions and then reports `double free or corruption` during
  fixture teardown. The failures reproduce with the current inert PS-015
  bridge, so they are not accepted as test flakiness.
- **Resolution:** `SyncBackendBase::cancelAllOperations()` now snapshots
  pending operations before cancellation, so synchronous `finished()` cleanup
  cannot invalidate the `QHash` iterator. The regression is covered by
  `tst_backend_op_queue`; the PlanStan provider-topology teardown test now
  passes against the working library.
- **Tasks:** PS-016

### KAL-018 — WildPalms readiness and persisted remote topology can diverge

- **State:** OPEN
- **Affects:** WildPalms Palm↔hub↔remote runs
- **Evidence:** `PalmRuntime::finishConnect()` loads persisted mappings, then replaces its active mapping list with generated logical-calendar mappings and emits `readyForSync`; provider-supplied remote backends are registered through a separate asynchronous lifecycle.
- **Required outcome:** one desired topology is retained and a run cannot start until every required backend or explicit unavailable state is present.
- **Tasks:** TOP-002, WP-009

### KAL-019 — Unserviceable committed mappings can leave an engine run waiting

- **State:** RESOLVED 2026-09-04
- **Affects:** runtime topology/run boundary; mappings whose backend instances are unavailable
- **Evidence:** an engine-backed runtime run with valid mapping fields but no registered backend instances previously did not produce a bounded future during contract probing.
- **Resolution:** `CollectionRuntime::run()` checks both mapping endpoints before dispatch. The 2026-09-04 audit additionally removed the ID-only topology path that fabricated successful per-mapping results without definitions or backends.
- **Tasks:** KRN-001, TOP-002

### KAL-020 — Runtime teardown is not proven safe during an active facade run

- **State:** RESOLVED 2026-09-04
- **Affects:** `CollectionRuntime` cancellation and destruction
- **Evidence:** the engine future watcher callback captures the runtime implementation, while the destructor neither owns the watcher nor cancels and joins an active run. Existing contract tests destroy only idle runtimes.
- **Resolution:** KRN-001 makes runtime destruction cancel selected resources,
  finish each lease once, complete the public future with a cancelled result,
  detach its watcher, and then destroy the engine. The engine also terminalizes
  its outstanding interface after worker shutdown. The active-run contract case
  completes below its five-second bound.
- **Tasks:** KRN-001

### KAL-021 — “Canonical” record notifications carry backend-native bytes

- **State:** RESOLVED 2026-09-04
- **Affects:** PlanStan model projection; public runtime event contract
- **Evidence:** `SyncEngineWorker` assigns `WriteBatch` record data to `ISyncHost::RecordChange::data`; PlanStan then parses it with `KCalendarCore::ICalFormat`. `CollectionRuntime` does not publish record change events at all.
- **Resolution:** the engine retains canonical records beside demoted write
  batches and emits the canonical payload and shape for successful source-side
  materialization. `CollectionRuntime` publishes it with canonical domain,
  encoding, schema version, and complete identity on the facade thread. The
  PlanStan-like contract parses canonical JSON directly without a backend read
  or native iCalendar parser.
- **Tasks:** KRN-001, PS-011, PS-016, PS-008

### KAL-022 — Runtime facade cannot express usable backend construction

- **State:** RESOLVED 2026-09-04
- **Affects:** DES-002/DES-003 contract slices and the runtime implementation order
- **Evidence:** `BackendFactory` exposes only an identifier and validation; it cannot create a backend or bind one to a topology endpoint. `TopologyDefinition` carries backend IDs and mappings but no factory/materialization binding, and `CollectionRuntime` loads providers without connecting them or materializing their backend specs. The current runtime contract tests therefore prove construction and rejection only.
- **Required outcome:** replan the runtime surface so a consumer can supply operational backend factories, provider/resource bindings, and a fallible topology materialization boundary before the PlanStan and WildPalms vertical slices are completed.
- **Resolution:** TOP-001 added neutral endpoint materialization requests,
  opaque runtime-owned endpoints, explicit provider/resource bindings, and a
  fallible `BackendFactory::createEndpoint()` boundary. RUN-001 now retains
  endpoint-owned backend QObjects and registers executable endpoints with its
  private engine registry after complete topology validation. The PlanStan
  contract proves a record transfer. KRN-001 additionally permits connected
  provider-owned backends to be named directly as topology endpoints and proves
  executor ownership and the leased-resource vertical slice.
- **Tasks:** TOP-001, RUN-001, KRN-001

### KAL-023 — PlanStan session collaborator snapshot can be stale after lazy sync initialization

- **State:** OPEN
- **Affects:** PlanStan sessions that gain their second backend after the session object is published.
- **Evidence:** `CollectionController` can emit `syncInfrastructureReady` after
  creating its engine/coordinator, while the already-published
  `CollectionSession::syncCoordinator()` still reflects the null constructor
  value. The production application builds, but direct late-init collaborator
  inspection is not a reliable readiness contract.
- **Product impact:** UI code that borrows the session coordinator can miss
  late-created conflict/run infrastructure; command and signal paths continue
  to operate through the controller.
- **Required outcome:** PS-008 replaces the transitional borrowed-collaborator
  graph with runtime commands, snapshots, and events; do not add another
  mutable accessor or a second session refresh protocol.
- **Tasks:** PS-016, PS-008

### KAL-024 — PlanStan conflict UI is not yet consuming runtime conflict commands

- **State:** OPEN
- **Affects:** PlanStan production conflict presentation during its runtime migration
- **Evidence:** `CollectionRuntime` now publishes populated `ConflictDetected`
  and `ConflictResolved` events and accepts `resolveConflict()` commands;
  PlanStan's production conflict adapter is installed, while the borrowed
  compatibility manager path remains to be deleted by PS-008.
- **Required outcome:** bind PlanStan's conflict dock to runtime conflict
  events, submit source/target/duplicate/custom-merge decisions through
  `resolveConflict()`, and remove the transitional conflict-manager access.
- **Tasks:** API-008, PS-013, PS-016, PS-008

### KAL-025 — PlanStan account UI is not yet consuming runtime provider mutations

- **State:** OPEN
- **Affects:** PlanStan live account add/edit/remove during its runtime migration
- **Evidence:** `CollectionRuntime` now exposes incremental provider lifecycle
  commands, but PlanStan production still routes account changes through its
  transitional `ProviderLifecycle` and `ProviderManager` graph.
- **Required outcome:** route account add/edit/remove through the runtime;
  await provider state/readiness, resubmit desired topology after edits, and
  remove the consumer-owned provider lifecycle path.
- **Tasks:** API-007, PS-014, PS-016, PS-008

### KAL-026 — Runtime provider observations are not consumer-grade

- **State:** RESOLVED 2026-09-05
- **Affects:** PlanStan account health, discovery, topology, and wizard flows
- **Evidence:** the previous facade encoded provider state in `message` and
  exposed no immutable discovery facts.
- **Resolution:** API-007 adds typed `ProviderSnapshot` values and provider
  observation event fields for state, redacted status, backend ownership, and
  discovered collections. Provider collection replacement is relayed after
  the provider atomically updates its discovery result; disconnect clears the
  old list.
- **Tasks:** API-007, PS-014

### KAL-027 — Persisted unresolved conflicts are invisible after facade rebind

- **State:** RESOLVED 2026-09-05
- **Affects:** PlanStan conflict dock after collection reopen or UI rebind
- **Evidence:** `CollectionRuntime` emits `ConflictDetected` only when the
  current engine run detects a conflict. It persists unresolved conflicts but
  exposes neither a backlog query nor initial replay, so a new event sink cannot
  repopulate the dock without borrowing `SyncConflictStore`.
- **Resolution:** API-008 adds the immutable `unresolvedConflicts()` facade
  query. Rebind coverage verifies stable IDs and mapping identity across a
  runtime restart, followed by facade resolution and an empty backlog.
- **Tasks:** API-008, PS-013

### KAL-028 — Runtime facade cannot preserve PlanStan run safety policy

- **State:** RESOLVED 2026-09-06
- **Affects:** PlanStan mass-deletion confirmation, concurrency setting, and
  unchanged-mapping policy
- **Evidence:** the runtime now accepts and applies `RuntimePolicy` values for
  concurrency, unchanged skipping, conflict interaction, and mass-delete
  confirmation.
- **Required outcome:** runtime-owned policy with a neutral mass-deletion
  decision seam and documented update timing; no engine pointer crosses the
  facade.
- **Tasks:** RUN-007, PS-012, PS-016

### KAL-029 — Runtime journal ownership is lifecycle-only, not operational

- **State:** RESOLVED 2026-09-06
- **Affects:** recovery ownership claims and the PlanStan cutover
- **Evidence:** runtime reset/reopen leaves a staged consumer journal intact;
  runtime code no longer constructs or replays `CalendarJournal`.
- **Resolution:** RUN-008 and ADR-0007 assign staged user-edit journaling and
  replay to PlanStan, while runtime reconciliation durability remains in its
  baseline, conflict, and topology stores.
- **Tasks:** RUN-008

### KAL-030 — Runtime facade cannot preserve PlanStan run interaction and telemetry

- **State:** OPEN
- **Affects:** PlanStan monitored sync, Run Plan, progress UI, and collection
  refresh during the facade migration
- **Evidence:** PlanStan selects `SyncBehavior::Monitored` or `Unmonitored` for
  every run and its Run Plan still needs a runtime interaction policy;
  `RuntimeSnapshot` now exposes full mapping facts and last-success timestamps,
  and `RunProgress` now carries a typed progress kind. The declared
  `CollectionChanged` event now has a typed schema but is not emitted until
  TOP-005 owns physical collection commands.
- **Required outcome:** a narrow runtime-owned run-policy seam plus immutable
  mapping state and typed run/collection observations, without exposing the
  engine or stores.
- **Tasks:** API-009, RUN-007, PS-011, PS-012, PS-013

### KAL-031 — PlanStan DecSync/Syncthing ownership and secret migration are unspecified

- **State:** OPEN
- **Affects:** PlanStan DecSync factory cutover and Syncthing status UI
- **Evidence:** the preparatory `PlanStanBackendFactory` now owns the
  DecSync monitor adapter, accepts only `syncthingApiKeyRef`, and resolves it
  through the installed secret store. CollectionController migrates legacy
  plaintext configuration before retaining the active collection config. The
  production runtime cutover still uses the controller-owned path until
  PS-016. PS-010 now compiles all current profile topology inputs, but the
  compiler remains inert until that cutover.
- **Required outcome:** keep the monitor observable and owned on the PlanStan
  side, bind it to the runtime-owned backend without affinity/lifetime hazards,
  and migrate the API key to an opaque reference resolved through the installed
  secret store.
- **Tasks:** PS-009, PS-010, PS-016

## Medium

### KAL-011 — Snapshot restoration is a stub

- **State:** RESOLVED 2026-09-04
- **Affects:** calendar recovery
- **Resolution:** COR-004 removed the unused snapshot/restore API and its claims; no public stub remains.
- **Tasks:** COR-004

### KAL-012 — Remote contacts collection creation is incomplete

- **State:** RESOLVED 2026-09-04
- **Affects:** CardDAV topology creation
- **Evidence:** CardDAV addressbooks are discovered and registered by the provider; `RemoteContactsBackend::createCollection()` explicitly declines provisioning and has regression coverage.
- **Resolution:** Consumer UI exposes no create action for discovery-only CardDAV collections; topology treats the empty result as unsupported rather than as a created collection.
- **Tasks:** API-003

### KAL-013 — Universal storage registrations advertise null provider factories

- **State:** RESOLVED 2026-09-04
- **Affects:** registry/UI truthfulness
- **Evidence:** raw-files and generic-SQLite are no longer returned by the stock universal-storage contribution list; configured consumers may still instantiate the concrete backends directly.
- **Resolution:** Removed the null-factory registry entries and added stock-plugin coverage for their absence.
- **Tasks:** API-004

### KAL-014 — Provider connection state has parallel representations

- **State:** RESOLVED 2026-09-04
- **Affects:** account UI and retry/error reporting
- **Evidence:** ProviderManager now consumes the typed provider lifecycle signal and keeps the legacy boolean signal only as a compatibility emission for direct observers.
- **Resolution:** TOP-003 makes ProviderConnectionState authoritative for manager state, backend registration, errors, retries, and disconnects.
- **Tasks:** TOP-003

### KAL-015 — Unsupported backend methods silently return defaults

- **State:** OPEN — reopened 2026-09-04
- **Affects:** capability correctness across domains
- **Evidence:** capability interfaces were named, but `IBlobBackend` still inherits all of them. Unsupported creation/write operations still return empty strings or booleans, and unsupported batch implementations may be explicit no-ops guarded by `supportsBatch()`. A direct attempt to make the shared `SyncBackendBase` methods pure made supported operation-only Google/Graph calendar backends abstract; caller migration must precede removal of that compatibility seam.
- **Required outcome:** capability-scoped interfaces or typed unsupported results.
- **Tasks:** API-006; see [ADR-0006](adr/0006-capability-contract-migration-boundary.md)

### KAL-016 — Test suite is not hermetic

- **State:** RESOLVED 2026-09-03
- **Affects:** release confidence
- **Evidence:** The legacy `tst_remotecalendarbackend` and `tst_backend_signals` suites now use `FakeCalDavServer` on per-process ephemeral ports; no fixed-port availability probe remains in those suites. `tools/test_report.py` separately reports hermetic, optional-dependency, live-credential, consumer-contract, and runtime-skipped coverage. Live vendor tests remain separately opt-in and are not part of this resolution.
- **Required outcome:** isolated ephemeral DAV fixtures; separate opt-in live lanes; CI reports executed versus skipped coverage.
- **Tasks:** TST-001 through TST-004

### KAL-017 — Version and release identity are inconsistent

- **State:** OPEN
- **Affects:** consumers and future packaging
- **Evidence:** repository tags reached v1.05 while CMake declares 0.2.4; consumers pin different tags.
- **Required outcome:** one version source and explicit compatibility policy.
- **Tasks:** BLD-006
