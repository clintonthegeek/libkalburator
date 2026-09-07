# Roadmap

**Adopted:** 2026-09-03
**Goal:** a safe, headless, installable synchronization library with thin PlanStan and WildPalms adapters.

This is the only roadmap. It orders outcomes rather than sessions or releases. Detailed work lives in [TASKS.md](TASKS.md); current defects live in [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

## Current position

The project has completed the two-consumer runtime proof and the first G3/G4
library-side slices. A direct comparison with PlanStan's production account,
topology, conflict, run-policy, journal, and model-projection paths found that
the facade is not yet sufficient for a safe production cutover. Work therefore
proceeds through the small API completion tasks `API-007`, `API-008`,
`API-009`, `RUN-007`, `RUN-008`, `TOP-004`, and `TOP-005`, followed by
testable PlanStan adapters and one flag-day ownership switch. `DOC-002` first
aligns PlanStan's authoritative guidance with this queue so adapter workers do
not receive contradictory ownership instructions.

The engine safety/correctness work is substantial and a useful runtime facade
skeleton exists. The 2026-09-04 completion audit restored a clean supported
test baseline and truthful coverage accounting. KRN-001 then closed the
inseparable contract/executor/coordinator/event slice: both public harnesses now
move records, the Palm-shaped graph converges in one call, resource loss and
active teardown terminalize, and canonical events cross the facade. Neither
real consumer has cut over yet. PlanStan's former project-wide roadmap was
archived as superseded; the cross-repository `PS-*` slices in `TASKS.md` are
the active queue for this refactor and name `../PlanStan` explicitly.

## Governing decisions

- Consumers own user intent and external-resource sessions.
- Libkalburator owns synchronization execution, state, convergence, cancellation, and teardown.
- PlanStan and WildPalms may be changed together; compatibility is not a constraint during consolidation.
- Both consumer vertical slices must stay represented throughout the refactor.
- No new feature breadth until the runtime and packaging gates are complete.

## Revalidated assumptions

- The ownership decision remains sound: consumers own intent and scarce
  external sessions; the library owns reconciliation and its lifecycle.
- Coordinated breaking changes remain cheaper than preserving the current
  source-tree and runtime seams.
- A contract is proven only when it creates usable backends and synchronizes
  records. Identifier-only topology and hook-count tests are not vertical
  slices and cannot report successful work.
- Moving old collaborators into a differently named consumer-owned assembly is
  a useful preparatory refactor, but it is not migration to library ownership.
- Atomic topology is part of the runtime kernel because provider connection,
  backend creation, collection materialization, and mapping publication are
  the fallible steps that need the transaction.
- A canonical change event must carry a canonical representation or typed
  canonical value. Relabeling backend-native bytes does not satisfy that gate.
- Target modularization should follow one real consumer runtime cutover so the
  public seams are extracted from use rather than predicted from directories.
- Preparatory consumer adapters may land with focused tests, but stay inert
  until one production wiring task replaces the old owner. Incremental planning
  must not create two active synchronization runtimes.

## Gates

### G0 — Documentation reset

**State:** COMPLETE — revalidated 2026-09-04

Exit conditions:

- one current reading path;
- one roadmap, task queue, and issue list;
- former corpus archived without content changes;
- PlanStan and WildPalms requirements represented.

### G1 — Trustworthy safety baseline

**State:** COMPLETE — revalidated 2026-09-04

Outcome: failures on supported paths are reproducible without ambient developer services, and every run terminates safely.

Exit conditions:

- DAV tests use isolated ephemeral fixtures and cannot reach a developer server accidentally;
- backend relocation/reentrancy failures are classified and pinned with deterministic tests;
- every failure and cancellation completes its future, clears run state, reports honest statistics, and tears down within a bound;
- remote load failure cannot become empty success;
- credentials are redacted from logs;
- library and affected consumer baselines are recorded with executed/skipped coverage.

Primary tasks: SAF-001 through SAF-005, SEC-001, TST-001.

Progress evidence: the supported default lane passes 221/221. The two
consumer-managed KDAV relocation probes remain buildable behind
`KALBURATOR_ENABLE_UNSUPPORTED_RELOCATION_TESTS`, but are explicitly outside
the supported lifecycle; executor-owned affinity and destruction stay in the
default lane. Cancellation now sets each worker's atomic stop flag before its
queued event-loop wake-up, closing the release-before-cancel race found by the
audit.

The supported default baseline now reports executed and runtime-skipped
coverage separately through `tools/test_report.py` (or the `test-report` build
target). Optional-dependency registrations appear when their build options are
enabled; credentialed live registrations remain a distinct lane.

### G2 — Runtime contract proven against both consumers

**State:** COMPLETE — revalidated 2026-09-04

Outcome: a concrete public runtime contract covers PlanStan's long-lived collections and WildPalms' leased Palm sessions before implementation replaces either consumer runtime.

Exit conditions:

- two executable contract harnesses model the PlanStan and WildPalms vertical slices;
- runtime ownership, external-resource lease, events, topology transaction, and run intents are expressed as small interfaces;
- all/subset/single/no-work semantics are explicit;
- Palm connection, tickle, category preparation, flush, backup, and restore ownership is preserved;
- a migration map names every consumer responsibility to keep, move, or delete.

Primary tasks: DES-001 through DES-005.

Completion evidence: the PlanStan-like harness connects a hermetic DAV provider,
admits its owned backend as a topology endpoint, transfers a record through
factory endpoints, consumes a canonical record event, and destroys an active
runtime safely. The WildPalms-like harness creates executable Palm/hub/remote
fakes, proves bounded multi-hop and reverse-mirror convergence, selected lease
hook order, and resource-loss cancellation. DES-002, DES-003, and KRN-001 carry
the executable evidence.

### G3 — Library-owned runtime kernel

**State:** ACTIVE — execution kernel proven; consumer policy and recovery seams remain

Outcome: libkalburator can construct, operate, and destroy a complete collection/profile runtime without consumer hand-assembly.

Exit conditions:

- runtime owns registries, plugins, providers, backends, stores, engine, and coordinator;
- backend execution and teardown satisfy G1 under the runtime;
- every run intent uses one coordinator and one terminal result path;
- bounded graph convergence replaces consumer-owned repeat loops;
- canonical typed events replace native-record callbacks;
- stores are opened, migrated, and closed through one runtime lifecycle.

Primary tasks: KRN-001, RUN-001 through RUN-008, and API-009.

Current gap: neither production consumer has completed the ownership cutover to
the library runtime. Execution, intent translation, convergence, canonical
record events, resource loss, store lifecycle, and active-run destruction are
proven by the library contracts. `RUN-007` must preserve PlanStan's
mass-deletion guard, tuning, and monitored/background conflict behavior without
exposing its engine, while `RUN-008` must replace the facade's currently inert
journal ownership with an operational and accurately documented recovery
boundary. `API-009` must expose the mapping/pass/last-success observations used
by PlanStan's Run Plan and define the currently inert collection-change event.

### G4 — Atomic topology and honest capabilities

**State:** ACTIVE — in-memory endpoint boundary proven; durable/consumer surface remains

Outcome: provider, collection, and mapping changes cannot leave partially wired runtimes, and discovery never advertises unusable operations.

Exit conditions:

- desired topology applies through one commit/rollback operation;
- mapping eligibility is compiled in one place;
- provider lifecycle has one typed state model;
- optional backend operations are capability-scoped or return typed unsupported errors;
- null provider contributions and generic calendar/contacts CRUD mismatches are removed;
- collection-property baseline and custom-merge persistence defects are closed.

Primary tasks: TOP-001 through TOP-005, API-003 through API-008, and COR-001
through COR-004.

Current gap: the runtime topology boundary is atomic for provider-owned and
factory endpoints in memory, but durable desired state and physical collection
mutations are outside its transaction. Provider state is encoded as a numeric
message and discovery facts are not exposed; unresolved persisted conflicts
cannot repopulate a newly attached consumer. `API-007`, `API-008`, `TOP-004`,
and `TOP-005` close those concrete PlanStan blockers. Provider edits must join
the TOP-004 durable transaction rather than invalidate live topology before the
profile commit. Irreversible physical operations use explicit compensation or
repair-required results rather than claiming rollback that a remote service
cannot provide. API-006 removes the transitional defaults only after both
consumer cutovers migrate their extension contracts.

### G5 — Consumers reduced to adapters

**State:** ACTIVE — preparation is decomposed; production ownership has not switched

Outcome: both applications use the library runtime and no longer own synchronization internals.

PlanStan exit conditions:

- `CollectionController` does not own/move backends, stores, engine, registries, or provider manager;
- full, subset, and single runs use the runtime facade;
- topology wizard/UI submits desired changes atomically;
- model refresh consumes canonical runtime events;
- custom merge round-trips its payload.

WildPalms exit conditions:

- `PalmRuntime` retains device-session and UI intent but not engine/store/plugin assembly;
- Palm extensions and backend factories register through public extension APIs;
- Palm↔hub↔remote convergence is one library run;
- resource hooks preserve tickle pause/resume, DLP serialization, link-loss cancellation, and flush-before-finish;
- backup/restore remain independent device operations.

Primary tasks: PS-008 and WP-009. PlanStan's factory, definition compiler,
event, run, conflict, provider, and topology adapters are now in production
execution after PS-016's single ownership switch. PS-008 deletes the remaining
transitionals and closes the gate. The PlanStan and WildPalms migrations may proceed in parallel,
but G5 closes only when both pass. Their completion then unblocks API-006, the
final removal of the transitional capability defaults.

PlanStan preparation begins only after DOC-002 makes its authoritative current
guidance distinguish today's controller-owned implementation from the adopted
runtime-owned target. The DecSync factory slice also owns the Syncthing monitor
lifetime and API-key reference migration; these cannot be deferred to the
flag-day wiring task.

Current gap: PlanStan still constructs a transitional engine/mapping view for
configuration and compatibility; WildPalms owns `PalmRuntimeAssembly`, the
engine, stores, plugins, mappings, and the multi-pass loop. PlanStan run
execution now references `Kalburator::Runtime::CollectionRuntime`.

### G6 — Modular public API and build

**State:** QUEUED — small headless extractions retained; broad split paused

Outcome: the code structure reflects feature boundaries and consumers stop depending on source layout or linker tricks.

Exit conditions:

- headless core has no Widgets dependency;
- domains, providers, storage, widgets, and test support are independently linkable;
- static registration side effects and whole-archive links are gone;
- public headers are namespaced and private implementation headers are not exported;
- PlanStan does not mutate library targets;
- both consumers build using public target properties only.

Primary tasks: BLD-001 through BLD-005.

### G7 — Installable library and release contract

**State:** QUEUED

Outcome: a third project can consume an installed libkalburator package without repository knowledge.

Exit conditions:

- install/export/package configuration exists;
- an external consumer test builds from the installed package;
- CMake and tag versions share one source;
- dependency ranges and schema policy are declared;
- a license is selected and included;
- CI runs library, contract, PlanStan, and WildPalms gates with clear skipped/live coverage.

Primary tasks: BLD-006, BLD-007, TST-002 through TST-004.

### G8 — Feature portfolio decision

**State:** QUEUED

Outcome: every retained feature is either supported through a consumer workflow or intentionally experimental and isolated.

Exit conditions:

- calendar/local/DAV and Palm domain paths meet the supported-feature definition;
- Google/Microsoft providers remain separate experimental targets until a named consumer workflow is requested;
- outline, identity, universal storage, snapshot recovery, and optional backends each receive keep/finish/remove decisions;
- unsupported breadth is not linked into consumers;
- only after these decisions may new domains or vendor interiors be scheduled.

Primary tasks: FTR-001 through FTR-006.

## Sequencing and flexibility

G1 precedes structural migration because unsafe failure behavior must be observable. G2 precedes the runtime implementation because both consumer shapes must constrain the contract. G3 and G4 may overlap where tests establish a stable seam. PlanStan and WildPalms migrations may run in parallel once the relevant runtime slice exists. Build modularization may start early only when it does not create compatibility scaffolding or distract from G1–G4.

When work reveals a surprise:

- add it to `KNOWN_ISSUES.md` if it is a defect or risk;
- add or split a task in `TASKS.md` if work is required;
- change this roadmap only if gate ordering or an exit condition changes;
- record a durable architectural choice in an ADR;
- do not create a separate campaign.

## Deferred until G8

- scheduling/free-busy and iTIP;
- ACL and sharing models;
- resource calendars;
- taxonomy/category entities beyond current consumer needs;
- new vendor API interiors;
- new domains;
- compatibility guarantees for pre-G7 APIs.
