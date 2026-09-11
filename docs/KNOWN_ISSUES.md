# Known issues

**Last reviewed:** 2026-09-11
This is the only active defect and risk list. Historical finding numbers are not reused.

States: `OPEN`, `INVESTIGATING`, `BLOCKED`, `RESOLVED`. Resolved entries remain only until the next release, then leave this file.

## Critical

### KAL-032 — Production synchronization ownership is not certified

- **State:** RESOLVED 2026-09-08
- **Affects:** PlanStan production collection load, edit, save, topology,
  account/discovery, conflict, and close workflows; dependent library claims.
- **Evidence:** STB-001 through STB-010 are complete. The real
  `CollectionController` host fixture passes for runtime-owned topology,
  policy, credentials, account/discovery, and conflict paths. The remaining
  gap was the application-side duplicate ownership of live backend/executor
  instances (`m_backends`, `m_backendExecutors`) that should be owned solely by
  `CollectionRuntime`.
- **Resolution:** STB-011 removed the duplicate ownership. `CollectionController`
  no longer constructs or holds live `BackendExecutor` instances; it only keeps a
  non-owning `m_backends` cache refreshed from `CollectionRuntime`. The runtime/
  adapter/sync-host verification lane passes 10/10, including the real host
  fixture that asserts runtime-owned backends and no legacy characterization
  graph.
- **Follow-up verification:** the formerly undetermined recurrence-editing
  observation was rebuilt against the public package boundary and rerun during
  STB-017: `integration_recurrence_editing` passed all 20 cases in 32.54 seconds
  offscreen. No segfault reproduced.
- **Required outcome:** route the complete live slice through one runtime and
  remove redundant live construction in the same patch; retain staging, undo,
  journals, and configuration intent in PlanStan.
- **Tasks:** STB-011 through STB-017. Historical TOP/RUN/PS/TST/FTR tasks are
  implementation evidence and are reopened only for a directly reproduced
  regression in their stated contract.

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

- **State:** RESOLVED 2026-09-09
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
- **Resolution:** STB-004 and STB-007 route the production settings, wizard,
  account, and physical collection paths through the runtime transaction and
  its durable participant. The real controller-host fixture proves accepted
  commit/reopen and rejected commit rollback; STB-011 removed the parallel
  controller composition.

### KAL-009 — Consumers duplicate runtime assembly and run policy

- **State:** RESOLVED 2026-09-09
- **Affects:** maintainability and behavioral drift in both consumers
- **Evidence:** the runtime run path and adapter tests exist, but AUD-003 must
  establish whether PlanStan production has one operational `CollectionRuntime`
  per open collection. The controller still constructs a transitional engine,
  stores, and mapping view for compatibility. WildPalms still owns
  `PalmRuntimeAssembly`, the engine, stores, plugins, mappings, and convergence
  state; `dispatchSyncPass_()` still re-dispatches passes.
- **Required outcome:** library-owned collection/profile runtime with consumer adapters.
- **Tasks:** KRN-001, RUN-003, TOP-002, PS-009 through PS-016, PS-008, WP-009
- **Resolution:** STB-011 transferred the PlanStan operational slice to one
  `CollectionRuntime`; STB-014 removed the remaining legacy graph and accessor
  paths. The retained `m_backends` view is non-owning observation only.

### KAL-010 — Build boundary depends on source layout and linker side effects

- **State:** RESOLVED 2026-09-08
- **Affects:** both consumers and external reuse
- **Evidence:** BLD-007 installs namespaced public headers, exports `Kalburator::Types`, `Canon`, `Identity`, `TypeSupport`, `Core`, `Storage`, `Sync`, and `Widgets` targets, and a consumer outside the source tree builds with `find_package(Kalburator 0.2.4 CONFIG REQUIRED COMPONENTS Types)` and links `Kalburator::Types`. The test-only registrar force-linking remains isolated in test fixtures.
- **Required outcome:** modular exported targets, namespaced headers, explicit registration, clean external consumer.
- **Resolution:** install/export package is available; stable API compatibility remains deferred to the first release after the consolidation.
- **Tasks:** BLD-001 through BLD-007 completed.

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

- **State:** OPEN — explicit non-release limitation
- **Affects:** WildPalms Palm↔hub↔remote runs
- **Evidence:** `PalmRuntime::finishConnect()` loads persisted mappings, then replaces its active mapping list with generated logical-calendar mappings and emits `readyForSync`; provider-supplied remote backends are registered through a separate asynchronous lifecycle.
- **Required outcome:** one desired topology is retained and a run cannot start until every required backend or explicit unavailable state is present.
- **Tasks:** TOP-002, WP-009
- **Release disposition:** WildPalms is not a PlanStan release component and
  its independently maintained remote readiness workflow is excluded from this
  PlanStan/libkalcal stabilization certification. It remains visible here for
  the WildPalms owner; it is not evidence for a second PlanStan runtime.

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

- **State:** RESOLVED 2026-09-09
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
- **Resolution:** STB-014 removed the transitional borrowed coordinator graph.
  Runtime commands, snapshots, and events are the production readiness surface.

### KAL-024 — PlanStan conflict UI is not yet consuming runtime conflict commands

- **State:** RESOLVED 2026-09-09
- **Affects:** PlanStan production conflict presentation during its runtime migration
- **Evidence:** `CollectionRuntime` now publishes populated `ConflictDetected`
  and `ConflictResolved` events and accepts `resolveConflict()` commands;
  PlanStan's production conflict adapter is installed, while the borrowed
  compatibility manager path remains to be deleted by PS-008.
- **Required outcome:** bind PlanStan's conflict dock to runtime conflict
  events, submit source/target/duplicate/custom-merge decisions through
  `resolveConflict()`, and remove the transitional conflict-manager access.
- **Tasks:** API-008, PS-013, PS-016, PS-008
- **Resolution:** STB-008 and STB-014 bind the controller-bound dock to runtime
  backlog/events and resolution commands; the compatibility manager path was removed.

### KAL-025 — PlanStan account UI is not yet consuming runtime provider mutations

- **State:** RESOLVED 2026-09-09
- **Affects:** PlanStan live account add/edit/remove during its runtime migration
- **Evidence:** `CollectionRuntime` now exposes incremental provider lifecycle
  commands, but PlanStan production still routes account changes through its
  transitional `ProviderLifecycle` and `ProviderManager` graph.
- **Required outcome:** route account add/edit/remove through the runtime;
  await provider state/readiness, resubmit desired topology after edits, and
  remove the consumer-owned provider lifecycle path.
- **Tasks:** API-007, PS-014, PS-016, PS-008
- **Resolution:** STB-007 exercises controller account add/edit/remove through
  runtime provider transactions, durable provider state, and discovery projection.

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

- **State:** RESOLVED 2026-09-09
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
- **Resolution:** STB-005 and STB-008 certify runtime-owned run policy,
  interaction, progress, and one aggregate terminal result through the
  production controller.

### KAL-031 — PlanStan DecSync/Syncthing ownership and secret migration are unspecified

- **State:** RESOLVED 2026-09-09
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
- **Resolution:** STB-006 verifies opaque Syncthing API-key materialization and
  plaintext migration; STB-011 places the resulting endpoint under runtime ownership.

## Medium

### KAL-033 — A write operation reports success for a half-applied batch

- **State:** OPEN 2026-09-11
- **Affects:** every `IBackendRecordApplier`; observed on `RemoteCalendarBackend`
- **Evidence:** `applyRecords()`'s `settleIfDone()` fails an operation only when
  `succeededUids()` is empty, so a batch with one write accepted and one refused
  settles `Succeeded` with a non-empty `failedUids()`. The same shape is in
  `SyncBackendBase::applyRecords()`, whose failure text says "all N record(s)
  failed to apply". Reproduced by
  `tst_remotecalendarbackend_blob_view::detachedException_dualWrite_masterEdit
  AndExceptionCreate_refusedByConformingServer()`, which pins the current
  behaviour so it trips when fixed.
- **Why it is not Critical:** both consumers compensate independently.
  `SyncEngineWorker::applyBatch` and `DefaultBlobWriter::apply` each add
  `&& failedUids().isEmpty()`. Nothing is fooled today; the operation's own
  state is wrong and the compensation is duplicated rather than shared, which is
  how a third consumer would get it wrong. A partial write trusted as complete
  would persist baselines for records never written — the phantom-delete class
  the engine's own comment warns about.
- **Fix shape:** settle `Failed` whenever `failedUids()` is non-empty, matching
  what both consumers already compute, or add an explicit partial state. Both
  `settleIfDone` sites must change together. A repo-wide check found no caller
  relying on "some succeeded" being enough.
- **Full record:** `../PlanStan/docs/bugs/writeoperation-succeeds-when-a-batch-half-failed.md`
- **Tasks:** FAM-002

### KAL-034 — CalDAV records carry whole-family bytes under per-component ids

- **State:** OPEN 2026-09-11
- **Affects:** `RemoteCalendarBackend` record layer; canon transcode; per-override
  conflict granularity
- **Evidence:** all three sites populating `m_lastRawIcsByUid` store the entire
  calendar object resource, so a master and its detached override are emitted as
  two records with different ids, identical `data` and identical `contentHash`.
  `ICalToCanonStage::transform()` parses with `ICalFormat::fromString()`, which
  KDE documents as returning the first component only, so both transcode to the
  master.
- **Effect:** ADR 0008 decision 3's per-override conflict granularity is
  unreachable, not merely unimplemented — two components that hash identically
  cannot be distinguished by any diff. A detached override does not reach CalDAV
  at all, pinned as a `QEXPECT_FAIL` in PlanStan's `tst_rrd014_mutations`.
  `LocalBackend` emits one record per file, so the two calendar backends also
  disagree on granularity.
- **Decision:** ADR 0009. A calendar record becomes one component in payload as
  well as identity, both backends change together, and family assembly moves to
  the apply boundary.
- **Tasks:** FAM-003

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

- **State:** OPEN — explicit non-release limitation
- **Affects:** capability correctness across domains
- **Evidence:** capability interfaces were named, but `IBlobBackend` still inherits all of them. Unsupported creation/write operations still return empty strings or booleans, and unsupported batch implementations may be explicit no-ops guarded by `supportsBatch()`. A direct attempt to make the shared `SyncBackendBase` methods pure made supported operation-only Google/Graph calendar backends abstract; caller migration must precede removal of that compatibility seam.
- **Required outcome:** capability-scoped interfaces or typed unsupported results.
- **Tasks:** API-006; see [ADR-0006](adr/0006-capability-contract-migration-boundary.md)
- **Release disposition:** this compatibility seam is outside the certified
  PlanStan calendar workflow. It remains an explicit library API migration
  limitation and must not be represented as a supported unsupported-operation
  result by a future consumer.

### KAL-016 — Test suite is not hermetic

- **State:** RESOLVED 2026-09-03
- **Affects:** release confidence
- **Evidence:** The legacy `tst_remotecalendarbackend` and `tst_backend_signals` suites now use `FakeCalDavServer` on per-process ephemeral ports; no fixed-port availability probe remains in those suites. `tools/test_report.py` separately reports hermetic, optional-dependency, live-credential, consumer-contract, and runtime-skipped coverage. Live vendor tests remain separately opt-in and are not part of this resolution.
- **Required outcome:** isolated ephemeral DAV fixtures; separate opt-in live lanes; CI reports executed versus skipped coverage.
- **Tasks:** TST-001 through TST-004

### KAL-017 — Version and release identity are inconsistent

- **State:** RESOLVED 2026-09-07
- **Affects:** consumers and future packaging
- **Evidence:** `VERSION` now supplies CMake's project version. The
  compatibility policy defines GPL-3.0-only and explicitly classifies tags
  through v1.05 as internal snapshots rather than package releases.
- **Resolution:** release tags must match `v$(VERSION)` only after BLD-007's
  install/export gate; historical tags do not imply compatibility.
- **Tasks:** BLD-006 completed; BLD-007 owns the installed-package proof.
