# Libkalburator architecture audit

**Audit date:** 2026-09-03  
**Primary consumer considered:** PlanStan  
**Repository revision audited:** `2ba0dee`

## Purpose and method

This report deliberately has two independently ordered parts.

Part I was completed before reading any project documentation, roadmap, design note, README, or code comment. Its sources were executable code, build definitions, tests, repository history, and the code of the primary consumer, PlanStan. Names necessarily communicate some intent, but comments and prose were not treated as evidence. This ordering is meant to prevent an intended architecture from being mistaken for the one the code actually implements.

Part II evaluates the documentation only after the code-first conclusions below were recorded. It asks whether the prose helps a new maintainer understand the implemented system, accurately distinguishes working features from experiments, and guides the project toward the needs visible in PlanStan.

## Part I — Code-first audit

### Executive verdict

Libkalburator contains a serious and unusually capable synchronization engine surrounded by a much less mature library boundary. Its strongest work is below the application boundary: three-way record comparison, per-side baselines, identity reconciliation, shape conversion, convergence passes, concurrency limits, cancellation, conflict handling, persistence, and a wide set of backend implementations. The tests show sustained attention to subtle correctness cases.

The project is nevertheless not yet a stable, independently consumable library. It is currently an embedded synchronization framework—or more bluntly, a large static subsystem—which PlanStan must finish assembling. PlanStan owns the effective runtime object, backend threading and destruction protocol, plugin bootstrap, provider lifecycle, topology mutation workflow, and several policy decisions. It also reaches through nominal public boundaries to concrete classes, private include directories, raw target properties, and static-linker behavior.

The central problem is not a lack of abstractions. It is that the project has built many abstractions and feature families before consolidating one supported end-to-end product path. Several layers overlap, several persistence components are islands, and some advertised capabilities terminate in defaults, null factories, or stubs. The result is broad conceptual reach with a narrow area that can currently be trusted as a consumer contract.

In short:

- **The good:** the synchronization algorithms, data model, correctness checks, backend breadth, and regression suite are substantial.
- **The bad:** the public surface is broad, concrete, stateful, and difficult to compose correctly; build and packaging boundaries are weak.
- **The ugly:** PlanStan and libkalburator form a circular composition unit, PlanStan whole-archives the library and owns backend thread affinity, and that exact relocation path currently hangs or fails in library tests.

The next milestone should not add another domain, protocol, provider, or abstraction layer. It should make the PlanStan path a first-class, library-owned, tested product path.

### Confidence and limitations

This is a structural and implementation audit, not a formal proof of every algorithm. The entire default build compiled successfully. The test suite was exercised, including isolated reproduction of failures described below. Optional online services cannot be certified without credentials and live servers, and numerous tests correctly skip in their absence. The full CTest run could not reach a clean aggregate result because a backend-relocation test hung; it was stopped after the failure was reproduced separately with a 180-second timeout.

### Scale and rate of change

The present tree contains about 156,000 lines across production and test code, 181 production `.cpp` files, 249 test `.cpp` files, and 217 registered CTest executables. There are roughly 419 class, struct, or enum declarations and 64 `Q_OBJECT` classes. The main `syncengine.cpp` alone is about 5,000 lines / 239 KB.

The repository accumulated 1,137 commits between 2026-04-20 and 2026-09-03. In that short period, `src` saw approximately 144,000 added or removed lines and tests approximately 110,000. `syncengine.cpp` changed in 139 commits and its header in 71. Frequent tags progressed from `v0.73` in June to `v1.05` in September, while the CMake project version remains `0.2.4` (`CMakeLists.txt:2`).

This is not evidence that the code is bad. It is evidence that the architecture and contracts are still moving too quickly to treat incidental public surfaces as stable. Versioning currently communicates less stability than the tag names may imply.

### Architecture the code actually implements

The build graph has three static libraries:

| Target | Actual responsibility | Boundary quality |
|---|---|---|
| `Kalburator::Types` | Core record/configuration/value types and the base shape type | Relatively cohesive, but still exposed through source-directory includes |
| `kalburator-typesupport` | JSON support, crash journal, lock registry, logical-calendar serialization | A small support island rather than an installed public package |
| `Kalburator::Sync` / `kalburator` | Engine, calendar, contacts, todos, notes, outlines, storage, providers, plugins, vendor clients, persistence, discovery, identity, and UI | A monolithic subsystem, not a narrow synchronization library |

`Kalburator::Sync` publicly pulls in Qt SQL, Network, XML, Widgets, and Concurrent plus multiple KDE Frameworks. UI source is compiled directly into this target, and Qt Widgets is unconditionally required by the root build. Twenty-one source directories are exported as flat public include directories (`CMakeLists.txt:795-834`). There is no install/export/package configuration and no install interface. Consequently, source layout, header basenames, and an in-tree CMake build are part of the effective API.

At runtime, the lower backend contract is `IBlobBackend`, which provides synchronous opaque-record operations. `SyncBackendBase` combines that contract with `QObject`, shapes, resources, operations, a queue, and asynchronous application of records. Calendar `SyncBackend` then adds a second, much larger calendar-specific surface: calendar CRUD, raw iCalendar paths, push/sync controls, capabilities, and signals. Numerous methods have permissive no-op or false-returning defaults (`src/calendar/syncbackend.h`). This makes interface conformance an unreliable indication that an operation is supported.

`SyncEngine` is the main coordinator. It compiles shape paths, obtains records from both sides, promotes them to a canonical representation, performs baseline-aware comparison, resolves conflicts, demotes changes, writes both sides, updates aliases and tokens, and repeats when necessary to reach a fixpoint. Its public header also exposes considerable orchestration and test-oriented state, while its implementation contains a large worker state machine, nested event loops, and blocking cross-thread invocations.

Above the engine are registries, plugins, providers, stores, a run coordinator, conflict management, journals, calendar management, and several topology-related concepts. No single library object owns and validates their lifecycle as one collection runtime. PlanStan therefore implements that missing layer itself.

### Implemented feature inventory

The following capabilities have real implementation and test evidence, although not all have an equally usable consumer path.

#### Synchronization core

- Two-way and directional upload/download/mirror synchronization.
- Per-mapping isolation, cancellation, read-only handling, and deletion protection.
- Parallel mappings with endpoint and resource concurrency limits.
- Revision/fingerprint-based unchanged-run skipping.
- Per-side native content hashes and persistent three-way baselines.
- Record-ID aliasing and healing, including recurring-exception identity cases.
- Shape graph compilation, canonical promotion, demotion, and loss reporting.
- Conflict policies including last-writer-wins, hybrid handling, deferred/monitored paths, and custom-resolution plumbing.
- Multiple convergence/fixpoint passes for backends whose write response changes identity or representation.
- Persistent mappings, tokens, collection properties, sync metadata, conflicts, and selected identity data.

#### Domains and representations

- Generic blobs and whole-record comparison.
- Calendar records with canonical JSON plus iCalendar, Org/iCalendar, Google Event JSON, and Microsoft Event JSON edges.
- Todo records with canonical form plus iCalendar VTODO, todo.txt, Google Tasks, and Microsoft To Do edges.
- Contacts with canonical form plus vCard 3/4, Google Person, and Microsoft Contact edges.
- Canonical/Markdown notes.
- Canonical/OPML/Org outlines.

#### Backends and services

- Local, mock, CalDAV, DecSync, subscription/holiday, and optional Org/Akonadi calendar backends.
- Google Calendar and Microsoft Graph calendar implementations.
- CardDAV, optional Akonadi, Google People, and Graph contacts implementations.
- Google Tasks and Graph To Do implementations.
- Raw-files, Markdown-files, generic-SQLite, filtered-view, and kind-demultiplexing storage machinery.
- DAV, CardDAV, multiprotocol DAV, and optional Akonadi provider contributions.

Provider registration is materially narrower than backend implementation. There are no equivalent stock Google or Microsoft provider contributions that turn those clients/backends into an end-user account lifecycle. Those integrations should therefore be described as implementation components or experimental paths, not complete consumer-facing provider features.

### What PlanStan actually consumes

PlanStan is not a small client using a stable facade. It is the missing application-integration layer.

Its root CMake either fetches a version pin or adds a neighboring checkout, then mutates the `kalburator` target to link PlanStan-owned KalCal and OrgIO targets (`../PlanStan/CMakeLists.txt:62-89,126-141`). PlanStanCore whole-archives the static library and manually propagates target include properties (`../PlanStan/src/CMakeLists.txt:167-227`). This reverses ownership: the library needs host targets, while the host must know the library's static-registration and include implementation details.

`AppController` constructs registries and the plugin manager, manually instantiates static plugins and manifests, loads them in-process, and separately registers provider contributions (`../PlanStan/src/app/appcontroller.cpp:29-117`). This overlaps `registerStockPlugins()` in the library. It then creates and injects a provider manager for each collection (`appcontroller.cpp:119-169`).

`CollectionController` is the effective per-collection runtime and facade, but it lives in PlanStan. It implements `ISyncHost` and owns or borrows the backend registry, shape registries, provider lifecycle, engine, stores, conflict manager, deletion guard, run coordinator, calendar journal, calendar manager, and Syncthing monitor. Its public API exposes many of these concrete objects (`../PlanStan/src/controllers/collectioncontroller.h:61-73,281-409,925-999`). `CollectionSession` is largely another pointer bundle over the same collaborators. A conservative search found Kalburator types or common Kalburator headers referenced by 98 PlanStan production files and 102 test files.

The PlanStan production path exercises the most credible part of the library: local/Org/DecSync/holiday/Akonadi calendars, DAV/CardDAV/multiprotocol/Akonadi providers, mappings, stores, conflicts, recurrence/identity, journals, locks, the sync engine/coordinator, and selected widgets. By contrast, PlanStan has no direct source/test references to the note, outline, todo, or contacts plugins; Google/Graph task/contact backends; identity directory/resolvers; or raw-files/Markdown-files/generic-SQLite/kind-demux backends. These still enter its process because the aggregate static target is whole-archived.

PlanStan also duplicates policies and protocols that should be atomic library operations:

- It moves each backend to its own `QThread`, removes parents, and blocking-marshals the backend home before teardown (`collectioncontroller.cpp:1378-1431`).
- A backend `recordChanged` callback scans mappings, blocking-loads a `BackendRecord`, parses iCalendar, then queued-marshals a model mutation (`collectioncontroller.cpp:2533-2642`). Thread semantics and native encoding leak across the boundary.
- It reimplements mapping eligibility rules involving wiring policy, enablement, and writable/primary bindings (`collectioncontroller.cpp:2438-2462`).
- Full synchronization uses `SyncRunCoordinator`, while subset runs construct `SyncRequest` and invoke `SyncEngine` directly (`collectioncontroller.cpp:2735-2832`). The consumer must guard the surprising rule that an empty mapping-ID list means “all enabled mappings.”
- The topology datasource separately merges/recompiles mappings, knows a magic local identifier, materializes deferred calendars, parses provider/domain compound IDs, blocks on callback APIs, and requires explicit live recompilation (`../PlanStan/src/sync/topology/kalbsynctopologydatasource.cpp:27-205,265-299,344-520`).
- The setup wizard provisions providers, mirrors configuration, records magic `:cal` IDs, instantiates backends, materializes calendars, and recompiles mappings through separate calls (`../PlanStan/src/wizards/collectionassembler.cpp:11-182`). Local IDs derived from display names can collide.

These are not merely aesthetic leaks. They make correct behavior dependent on call ordering, consumer knowledge, thread affinity, magic identifiers, and partial rollback behavior.

### The good

#### The engine tackles real synchronization problems

This is not a thin CRUD copier. The per-side hash baseline model avoids treating semantically equivalent but natively different records as perpetual changes. Alias healing and recurrence identity work address problems that routinely break calendar synchronizers. Fixpoint passes recognize that graph-style remote writes can return transformed records. Shape compilation and fail-loud guards reject missing definitions, paths, and cross-domain mappings instead of blindly moving bytes.

#### Correctness has received meaningful test investment

The suite covers convergence, conflict decisions, cancellation, migration, loss profiles, recurrence, backend behavior, thread movement, concurrency caps, deletion guards, journals, and many edge cases. Tests are often fine-grained enough to expose genuine lifecycle failures. The failing relocation tests are a sign of product risk, but also proof that the project has tests capable of finding it.

#### There is a viable narrow seam

PlanStan's sync-host smoke test constructs `SyncEngine` using small `ISyncHost` and `ISyncConfigStore` implementations plus shape registries (`../PlanStan/tests/sync-host-smoke/tst_synchostsmoke.cpp:33-154`). That demonstrates that a smaller supported contract is possible. The architecture should consolidate around that kind of seam rather than expose every collaborator.

#### The implementation often prefers explicit failure to silent corruption

Missing shapes and transformations, invalid mappings, unsupported cross-domain paths, identity conflicts, and several loss conditions are checked. That instinct is correct and should guide the unfinished areas too.

### The bad

#### The library boundary is an in-tree build accident

There is no installable CMake package, export set, or stable include tree. Public include directories are internal source folders. Some PlanStan tests add libkalburator private source directories directly (`../PlanStan/tests/backends/CMakeLists.txt:154-209`, `tests/integration/CMakeLists.txt:294-312`). This means reorganizing files is potentially a consumer-breaking API change.

#### The aggregate target defeats modularity

All domains, vendor integrations, storage variants, providers, plugin machinery, and widgets compile into one static target. PlanStan then whole-archives it to retain registration side effects. Optional-looking abstractions therefore do not translate into optional binaries, dependency isolation, or a smaller supported surface.

#### Multiple overlapping execution surfaces invite semantic drift

Blob APIs, `SyncBackendBase`, the calendar-specific `SyncBackend`, direct engine requests, `SyncRunCoordinator`, provider callbacks, plugin contributions, registry lookups, and consumer-owned topology operations all overlap. The result is more ways to initiate or mutate synchronization than there are consistently enforced lifecycle rules.

The empty-`mappingIds` meaning “all enabled” is a particularly dangerous example. PlanStan contains explicit guards to avoid accidentally turning an empty subset into a full run. An empty selection should mean empty, while “all” should be an explicit request variant.

#### Capability detection is too permissive

Large base classes return false, empty values, or do nothing for unsupported operations. A caller can compile against calendar creation, raw access, push, or synchronization methods without a reliable typed indication that the concrete backend implements them. Capabilities and operations exist, but are not consistently the only legal route to invocation.

#### Persistence is fragmented

Baselines, conflicts, aliases, mappings, identity, journals, locks, and provider configuration are represented by multiple managers and stores whose transactions and lifecycle are not unified. PlanStan is forced to decide construction order and coordinate mutations. Multi-step provider/topology changes are not atomic across these stores and live objects.

#### Security ownership is unclear

DAV credentials are placed in `BackendConfiguration` connection parameters and persisted through provider configuration. OAuth token caches attempt restrictive file modes, but there is no unified credential-store contract. During the test run, a KIO error message printed a URL containing `testuser:password1`, despite surrounding code attempting safer logging. Secrets can therefore escape through dependency-generated error strings. Credentials should be opaque references backed by an OS secret store, and all third-party error text should be treated as potentially sensitive.

### The ugly

#### The consumer owns the hardest lifecycle problem—and the tested path is broken

PlanStan moves QObject backends between threads and later performs a blocking relocation for teardown. The library's relocation tests exercise the same scenario. In the current build:

- `tst_backend_reentrancy_pin` failed three relocated-backend cases after roughly 93 seconds, with repeated cross-thread-parent warnings and 30-second DAV timeouts.
- `tst_backend_thread_relocation` timed out after 180 seconds. It emitted repeated `QObject::setParent` thread-affinity warnings, failed a remote single-fetch setup, left a cancelled write future unfinished, reported dishonest zero error statistics, rejected the next run as still synchronizing, and could not stop a worker within its 30-second teardown allowance.

This is a release-blocking defect for the primary consumer architecture. It can cause hangs, stuck run state, unreliable cancellation, and shutdown problems. The library should own backend executors/affinity and expose async operations that do not require consumers to move QObjects.

#### Build ownership is circular

The host mutates the library target to supply PlanStan-specific targets, while the host also extracts the library's target properties and whole-archives it. This prevents independent packaging, obscures which project owns Org integration, and makes linker side effects part of correctness.

#### Advertised registrations can lead nowhere

`UniversalStoragePlugin` registers raw-files and generic-SQLite backend contributions whose `createProvider()` functions return null (`src/universal/universalstorageplugin.cpp`). A UI or registry can discover an option that cannot be provisioned. Registration must mean usable, or carry an explicit experimental/unavailable state with a reason.

### Concrete unfinished, disconnected, or suspect behavior

The following items are distinguishable from general architectural preference because the code path is incomplete or internally inconsistent.

1. **Backend relocation and cancellation are currently unstable.** Reproduced by the two failing tests described above. This is the highest-priority defect.

2. **Collection-property baselines appear write-only.** The engine persists a collection baseline, but production search found no read of `BaselineStore::collectionBaseline()`. Calls into the property phase pass an empty baseline, so subsequent property comparison cannot perform the intended three-way logic. Property conflicts are resolved as source-wins rather than using the mapping's conflict policy, and property application is fire-and-forget through a void/queued API. Metadata convergence and failure reporting are incomplete.

3. **Target promotion lacks the source-side empty-transform guard.** Source promotion rejects a transform that turns non-empty input into empty output. Target promotion does not apply the equivalent check. A malformed or unsupported target representation can therefore be treated asymmetrically and may produce a false diff or data loss.

4. **Calendar snapshot restoration is a stub.** `CalendarManager::restoreFromSnapshot()` returns false (`src/calendar/calendarmanager.cpp`). Snapshot creation should not be presented as a recovery feature until restore works and is tested end to end.

5. **Remote contacts collection creation is unimplemented.** `RemoteContactsBackend::createCollection()` returns an empty result, and it does not provide the calendar-style creation override expected by a generic topology call (`src/contacts/remotecontactsbackend.h`).

6. **Universal storage contributions cannot create providers.** The registered raw-files and generic-SQLite entries return null factories, making them registry-visible but not provisionable.

7. **PlanStan custom merge discards its payload.** The conflict UI obtains `mergedIcal` but persists only the resolution enum (`../PlanStan/src/sync/conflictdockwidget.cpp:301-325`). A user-visible custom merge therefore does not pass the merged record into resolution.

8. **First-sync error honesty needs hardening.** The failing relocation run showed a remote `loadRecords` failure followed by zero harvested baselines and continued orchestration. The relevant API lacks a strong typed error channel. This should be treated as a suspected false-success path and covered by a focused regression test before release.

9. **Provider state has parallel sources of truth.** `ProviderManager` wires a legacy boolean connection signal while providers also expose richer typed states. Failed futures and emitted state changes can diverge. One typed lifecycle state machine should own connection state.

10. **Optional live integrations remain largely unverified by the default suite.** Google, Microsoft, Akonadi, and live DAV tests skip or are conditional without services/credentials. Passing default CTest does not validate those transports.

### Stability assessment

The project is **algorithmically promising, integration-unstable**.

The default configuration built successfully from the present tree. Most of the 217 test executables completed successfully in the exercised run, including broad engine and model coverage. However, the run does not support a clean “all tests pass” claim:

- Two test executables exposed genuine backend-thread relocation defects, one by hanging beyond 180 seconds.
- One backend-signal test opportunistically connected to a developer Radicale instance at the default localhost port, collided with persistent fixed IDs, and failed on HTTP 412. This test is non-hermetic rather than evidence of a core algorithm failure.
- The initial sandboxed run produced many localhost-bind failures; these were environment failures and were rerun with normal network permissions.
- Several live-vendor tests skip without credentials, and CardDAV/Akonadi coverage is conditional.

There is no repository CI configuration to demonstrate repeatability across clean machines, compilers, Qt/KF versions, or sanitizers. A prior PlanStan test log records 134/134 tests passing on this date, but that was not a fresh run in this audit and its default configuration excludes some live integrations.

Stability by area:

| Area | Assessment |
|---|---|
| Canonical models, diffs, baseline algorithms | Strongest area; broad unit coverage |
| Mock/local synchronization | Generally strong, with substantial edge-case tests |
| Persistence and migration | Good breadth, but stores are fragmented and collection-property use is incomplete |
| Remote DAV transport | Implemented and tested, but environment-coupled tests and relocation failures reduce confidence |
| Backend threading/cancellation/shutdown | Release-blocking instability on a primary-consumer path |
| Google/Microsoft/Akonadi live behavior | Experimental or conditionally verified |
| Provider/topology lifecycle | Functional in PlanStan through multi-step consumer orchestration; not a stable library contract |
| Packaging and compatibility | Not yet a consumable/installable library boundary |

### Maintainability assessment

Maintainability risk is high despite good local engineering in many components.

The largest driver is cognitive load: an engineer working on a sync run may need to reason about engine workers, QObject affinity, nested event loops, backend queues, resource caps, shape graphs, multiple stores, plugin registration, provider state, PlanStan collection state, and static linking. A 5,000-line engine implementation and frequent cross-thread blocking calls concentrate risk. The number of separate abstractions has not reduced the amount a consumer must know.

Rapid historical churn further means tests may validate behavior without defining a durable compatibility contract. Because private directories and concrete types are exposed, safe refactoring is harder than the apparent target aliases imply. Conversely, the large focused test suite is a real asset: once the public path is narrowed, it can support aggressive internal simplification.

### Inferred project goal versus delivered product

From code alone, the apparent goal is a domain-general, backend-agnostic synchronization framework that can map multiple native representations through canonical forms, persist reconciliation state, discover and provision providers, handle conflicts, and support several personal-information domains.

The delivered product today is narrower: a sophisticated calendar-centered synchronization subsystem embedded by PlanStan, plus multiple partially integrated domain and vendor experiments. Calendar is the only domain whose plugin registers domain operations, and PlanStan's actual integration is overwhelmingly calendar-centric. The generalized architecture is plausible, but its breadth has outrun evidence of consumer need and end-to-end completion.

That distinction should drive scope. “The model can represent it” and “some backend code exists” are not the same as “the library supports it.” A supported feature should have a provisionable provider path, explicit capabilities, a library-owned lifecycle, hermetic tests, at least one consumer workflow, and documented compatibility expectations.

### Recommended architecture

Build one library-owned `CollectionRuntime` (name illustrative) around the narrow host/config seam already proven in the smoke test. It should be the only supported owner of per-collection synchronization collaborators.

Its responsibilities should include:

- registries and stock plugin activation;
- backend construction, executor/thread ownership, and deterministic async teardown;
- provider lifecycle and typed connection state;
- engine, coordinator, conflict manager, guards, and persistence stores;
- atomic mapping/topology/provider mutations with rollback;
- materialization of live calendars/collections;
- one synchronization entry point for all, single, and subset runs;
- native-record change translation into a typed canonical event;
- recovery/journal lifecycle.

PlanStan should receive a small facade of commands, immutable snapshots, progress/events, and explicit capability results. It should not move backend QObjects, parse native records in callbacks, know compound provider IDs, mutate registries, decide mapping eligibility, or stitch together stores.

At build time, split the aggregate target along real optionality boundaries:

1. `Kalburator::Core` — types, shape graph, diff, engine contracts, no Widgets.
2. `Kalburator::Calendar` — calendar model/operations and supported local/DAV paths.
3. `Kalburator::ProvidersDav` — DAV provider lifecycle and transport.
4. Separate experimental targets for contacts, todo, notes, outlines, Google, Microsoft, universal storage, and widgets until each meets support criteria.

Replace static-constructor/whole-archive discovery with explicit registration functions or generated registries. Give every public target a namespaced installed include tree and CMake package export. Remove PlanStan-owned targets from libkalburator's build graph; Org integration should be an adapter target owned on one side of the boundary.

### Prioritized action plan

#### P0 — Make the existing PlanStan path safe

1. Fix and continuously run `tst_backend_thread_relocation` and `tst_backend_reentrancy_pin`. Require bounded cancellation and teardown; never fall back to an unbounded wait.
2. Move backend affinity/executor ownership into the library runtime. Until then, document and assert one legal affinity protocol in code.
3. Make run state exception/error safe so every exit completes the future, reports honest statistics, and clears “syncing.”
4. Make remote first-sync/load failures typed and fatal to the run rather than indistinguishable from an empty collection.
5. Stop logging dependency error strings without credential redaction; migrate persisted credentials to opaque secret references.
6. Fix custom-merge payload persistence before exposing that action.

#### P1 — Establish one supported vertical slice

1. Introduce the library-owned per-collection runtime/facade.
2. Route all/all-subset/single mapping runs through the coordinator; replace empty-list sentinel semantics with explicit request types.
3. Make provider/topology/mapping changes atomic and return typed results.
4. Complete or disable collection-property synchronization until baselines, policies, application acknowledgement, and failures are correct.
5. Convert backend optional operations from permissive defaults to explicit capability interfaces or typed unsupported errors.
6. Add a clean consumer contract test that builds and runs without private includes, target mutation, or whole-archive flags.

#### P2 — Create an actual library distribution

1. Split headless core/calendar/DAV from widgets and experimental domains.
2. Add install rules, export sets, namespaced headers, version compatibility, and a package-consumer test.
3. Replace static registration side effects with explicit composition.
4. Align tag/release versioning with the CMake package version and state compatibility guarantees.
5. Add clean-machine CI with hermetic DAV fixtures, timeouts, sanitizers, and a PlanStan integration lane.

#### P3 — Earn back breadth

For each additional domain or vendor, require a provider/account path, concrete CRUD capabilities, lifecycle ownership, live-contract tests where feasible, a consumer workflow, and removal of null/stub registrations. Until then, label the target experimental and keep it out of PlanStan's production link.

### Release gate proposed for the next version

A release intended for PlanStan should require all of the following:

- backend relocation, cancellation, and teardown tests pass repeatedly under a global timeout;
- no future can remain unfinished and no run can remain stuck “syncing” after failure;
- DAV tests use isolated ephemeral state and never opportunistically connect to a developer service;
- no credentials appear in logs or plaintext general configuration;
- PlanStan can use a single library-owned collection runtime without moving backends;
- custom merge round-trips the merged payload;
- unsupported registrations are hidden or return an explicit reason;
- a minimal external consumer builds only through exported targets and public headers;
- all supported features are distinguished from experimental implementation components.

---

## Part II — Documentation, roadmap, and design-note audit

### Corpus and method

Only after Part I was written did this pass open the existing prose. The corpus is exceptionally large: 214 Markdown files under `docs/`, approximately 91,377 lines and 4.38 MiB, excluding this report. Sixty-six files live under `docs/phase0/`, 78 under `docs/campaign/`, and another 64 directly under `docs/`. The review inventoried the full corpus by filename, size, headings, status/supersession markers, and cross-references, then read the current entry points, active and closed campaign status files, roadmaps, architectural audits, proposals, consumer coordination material, current findings, and representative designs/receipts in depth.

The relevant distinction is not “documentation exists.” It plainly does. The question is whether a maintainer can efficiently derive current architectural truth and sound priorities from it.

### Documentation verdict

The documentation is simultaneously one of the project's strongest engineering assets and one of its largest sources of confusion.

It is strongest as a forensic laboratory notebook. The campaign files preserve reproductions, counterexamples, non-vacuity checks, protocol observations, consumer feedback, and why a particular fix was selected. The best entries are more rigorous than typical project documentation.

It is weakest as documentation of a product or library. There is no trustworthy front door, no current architecture overview, no supported-feature matrix, no public API guide, no packaging guide, no lifecycle contract, and no single current roadmap. Instead, the reader enters a 91,000-line event log written mainly to coordinate successive agents. Current facts, historical decisions, superseded plans, proposed work, completed work, and uncompleted work are interleaved across dozens of “source of truth” documents.

The net effect is that the prose helps an investigator who already knows what symbol or finding number to search for, but obscures the system for a consumer or new maintainer asking ordinary questions: What is supported? What owns a collection? How do I embed this safely? Which tests must pass? What is experimental? What work is next? What does version 1.05 promise?

### Where the documentation helps

#### It preserves high-quality defect evidence

The central findings log already contains the property-baseline defect found independently in Part I. O38 accurately states that the property phase is always passed an empty baseline and that persisted collection metadata is never read back (`docs/campaign/FINDINGS.md:1202-1242`). It explains the false-green test and gives a concrete one-sided-edit reproduction.

The same log records that rehydrated custom merge payloads are not persisted (O52, `FINDINGS.md:2185-2202`) and accurately identifies the missing schema field. This confirms that portions of the code-first audit are not surprising new discoveries; the project has previously diagnosed them correctly.

Other findings capture real protocol behavior, identity failures, convergence bugs, and external-library limitations with unusually useful detail. The vendor wire notes, loss-profile documents, and captured-fixture discipline are valuable long-term assets.

#### It values falsifiable tests

The campaign invariants explicitly require test-first work, production-path tests rather than synonyms, and demonstrations that a new test fails before the implementation changes. Numerous return receipts record those demonstrations. The incidence-parity preflight used separately runnable probes to distinguish upstream KCalendarCore behavior from libkalburator behavior. This is exemplary practice.

#### It often marks old material honestly

Several historical documents have prominent superseded banners. The April honest assessment now says it describes pre-F1 architecture. `docs/phase0/README.md` acknowledges that its six-directory snapshot is stale and points elsewhere. The May architectural audit warns that its anchors drifted and should be reverified. This is much better than silently rewriting history.

#### It records consumer and live-service learning

The synchronization-convergence roadmap begins from an actual PlanStan/Nextcloud failure rather than a hypothetical architecture. The B2C and EEE work preserves live Google/Microsoft API discoveries that mocks and vendor prose missed. The consumer coordination document attempts to track releases across PlanStan and WildPalms. That outward-looking discipline is correct.

#### Earlier architecture work anticipated an important remedy

The June architectural follow-up explicitly proposed an “Engine-assembly facade” because both hosts hand-wire registries, stores, and `SyncEngine`. It proposed a `SyncSessionBuilder` (`docs/campaign/architectural-redress/2026-06-10-audit-follow-up-specs.md`, “RFC proposals to draft for consumers”). That is close to Part I's recommended library-owned runtime. The problem is not that the project never recognized the need; it is that the proposal was stranded while later campaigns expanded feature breadth.

### Where the documentation obscures

#### The root README is wholly false as a current entry point

The README says “Phase 0,” “No source code yet,” “design-only,” and “not accepting contributions during Phase 0” (`README.md:3,22-26,56-59`). The repository actually contains roughly 76,000 lines of production code, 217 test executables, a primary production consumer, and tags through v1.05. It also says the license has not been chosen; that remains consistent with the absence of a license file, but is itself a distribution blocker that the later roadmaps do not resolve.

A reader should never have to discover that the root README is an April time capsule by navigating into a superseded Phase-0 index.

#### Agent instructions have become the de facto product manual

`CLAUDE.md` is the designated live entry point. It is a 31 KB operational briefing containing campaign state, vendor experiments, branch/remotes instructions, test baselines, historic release summaries, and tool traps. It is useful to an agent continuing a named campaign, but inappropriate as the sole current guide to a library. It does not explain the build/runtime architecture or a supported consumer path in a stable, reader-oriented way.

It is also already internally stale. Its B2C section says P3 todo backends are next (`CLAUDE.md:87-89`), while the B2C status says P3 is done and P4–P6 are not started (`docs/campaign/b2c/STATUS.md:18-21`). Its generic phase-status instructions still send readers to `docs/phase0/` (`CLAUDE.md:408-423`) even though that index declares itself superseded and says per-campaign status is now authoritative.

#### There are multiple competing “sources of truth”

The corpus repeatedly designates different files as authoritative or mandatory: the root agent instructions, Phase-0 overview, campaign STATUS files, campaign plans, findings logs, consumer coordination page, individual receipts, and superseding amendments. This was partially mitigated with banners, but authority still depends on knowing the date and campaign lineage.

The architectural-redress status is a concrete contradiction. Its plan table leaves item 6.5 “in progress” and Plans 10/11 proposed (`docs/campaign/architectural-redress/STATUS.md:364-378`); its definition of done requires every plan plus a retrospective that does not exist (`STATUS.md:498-505`). `CLAUDE.md` nevertheless calls the 11-plan campaign closed (`CLAUDE.md:396`). There is no safe interpretation available to a new reader without re-auditing the code.

The consumer coordination page calls itself the single cross-repo status page but says PlanStan pins v1.01 (`docs/2026-07-19-consumer-coordination-status.md:13-22`); PlanStan's current CMake pins v1.05. The page even notes its own stale interval. A status index that requires checking another repository before believing its primary table is not functioning as an index.

#### “Pre-existing” is used as a substitute for risk classification

The current agent guide, EEE status, incidence status/receipts, consumer coordination page, and v1.05 consumer notice repeatedly classify four failing tests—including backend thread relocation and reentrancy—as “known environmental” because they predate the campaigns and show DAV timeout/local-Radicale symptoms (`CLAUDE.md:377-385`; `docs/campaign/eee/STATUS.md:83-92`; `docs/2026-09-03-incidence-parity-v1.05-consumer-notice.md:93-96`).

That establishes non-regression relative to a campaign baseline; it does not establish environmental causation or product acceptability. Isolated current execution of the relocation path produced cross-thread parenting violations, an unfinished cancellation future, a subsequent run rejected as still active, and teardown that could not stop the worker. Those are library lifecycle failures, even if an unreliable test server helps trigger them. More importantly, PlanStan uses this exact backend-relocation architecture.

The documentation therefore turns a useful attribution statement—“this campaign did not introduce the failure”—into a misleading stability statement—“the failure is environmental.” This is the most consequential way the current prose obscures reality.

#### Campaign completion is confused with feature completion

The B2C plan defines victory as normal consumer OAuth/configuration UX, provider-backed account mapping, and stock-engine operation. Its status shows P4 providers/config UX, P5 identity wiring, and P6 consumer handoff have not started. Yet other current summaries describe vendor backends as production accomplishments and direct attention toward more vendor interiors.

From the code and PlanStan, the more honest state is:

- vendor transports and backends have significant implementation and live-test evidence;
- stock Google/Microsoft provider/account contributions do not exist;
- PlanStan does not consume these vendor paths;
- the universal storage registrations cannot instantiate providers;
- the identity layer is not integrated into the consumer runtime.

Calling P0–P3 “done” is locally true for their campaign task lists, but easily read as end-user feature readiness. Every status needs separate columns for implementation, mock tests, live probe, provider provisioning, consumer integration, and supported/released status.

#### The roadmap rewards breadth over product consolidation

After Tier A, the EEE roadmap points toward scheduling/free-busy, ACLs, resource calendars, taxonomy entities, MAPI properties, and beta-horizon monitoring. Meanwhile B2C P4–P6 remain unfinished, the assembly facade proposal is unimplemented, the aggregate target is unsplit, packaging does not exist, the license is undecided, thread relocation is failing, and open correctness findings remain.

This is exactly the premature-complexity dynamic visible in the code. The roadmap treats a new representational interior or vendor edge as a well-shaped campaign, while consumer-boundary consolidation has no active owner. The documentation has become a mechanism for making expansion legible rather than questioning whether expansion is the right work.

#### Important open issues are buried in a 293 KB append-only ledger

`docs/campaign/FINDINGS.md` is nearly 4,600 lines / 293 KB. Open and resolved entries are not cleanly separated: the file reaches a `## Resolved` heading and later resumes newly numbered open findings. An issue may contain an original diagnosis, updates, corrections, and partial resolutions hundreds of lines apart. O38 and O52 are accurate and important, but neither appears in a concise current release-risk list.

The append-only record should remain available as history. It should not also serve as the active issue tracker.

#### Documentation volume is dominated by execution process

Thirty-nine documents explicitly present themselves as agent/session/handoff/return-receipt material, and the corpus contains more than 1,500 occurrences of workflow language such as “agent,” “session,” “handoff,” “binding,” “must,” and “do not.” Detailed receipts often restate plan, implementation, test commands, results, findings, and next steps already repeated in STATUS, FINDINGS, tag messages, and `CLAUDE.md`.

This repetition was likely useful while many LLM sessions were operating, but it raises the cost of finding architectural truth and increases the number of copies that can drift. It also incentivizes checklist closure over deletion and simplification.

#### There is almost no consumer-facing API documentation

The corpus explains internal campaigns in great depth but does not provide:

- a current component/dependency diagram;
- ownership and thread-affinity rules for a collection runtime;
- supported public targets and headers;
- install/package instructions;
- a minimal supported PlanStan-style integration;
- backend capability/error semantics;
- provider lifecycle/state semantics;
- database and credential ownership;
- compatibility/versioning policy tied to current releases;
- a matrix distinguishing supported, experimental, stubbed, and unused features.

The example consumer proves a narrow engine seam, but it is not documented as the supported contract and does not prove installed-package consumption.

### Roadmap assessment

There is no single current roadmap. There are at least three kinds of roadmap in play:

1. historical extraction/redress plans, some nominally closed with unfinished rows;
2. synchronization correctness campaigns that are mostly complete but retain open findings;
3. vendor/domain campaigns whose local task progress is current but whose consumer delivery remains unfinished.

The plans are often excellent at sequencing a known technical campaign. They are poor at deciding between campaigns or limiting total system ambition. There is no portfolio-level prioritization that compares “add ACL canon” against “make the only primary consumer stop owning unsafe backend threads,” or “add another backend” against “make the library installable.”

The current effective roadmap should be reset around consumer value and risk:

| Order | Outcome | Existing roadmap relationship |
|---|---|---|
| 1 | Stable PlanStan lifecycle: bounded cancellation/teardown and no stuck runs | Reopens the mislabeled relocation baseline as product work |
| 2 | Library-owned collection runtime / assembly facade | Revives the unimplemented June facade RFC and makes it the architectural center |
| 3 | Atomic topology/provider/mapping operations and one run path | Replaces PlanStan's duplicated orchestration |
| 4 | Honest supported-feature matrix and disabling of null/stub registrations | Clarifies what B2C/EEE work actually delivers |
| 5 | Headless modular targets, packaging, license, compatibility contract | Creates an actual reusable library |
| 6 | Finish one vendor provider through PlanStan UX, or defer all vendor production claims | Completes B2C vertically rather than starting another interior |
| 7 | Only then resume additional domains/vendor interiors | Requires demonstrated consumer demand and ownership |

### Recommended documentation structure

The existing corpus should be preserved, not deleted, but most of it should move conceptually into an archive. A small maintained layer should sit in front of it:

1. **`README.md` — truthful landing page.** What the project is today; stability level; supported and experimental scope; build/test quick start; primary consumer; links to the five maintained documents below.
2. **`docs/ARCHITECTURE.md` — implemented architecture.** Target/module graph, runtime ownership, threading, data flow, stores, extension points, and the intended dependency direction. It should describe current code, not a plan.
3. **`docs/CONSUMING.md` — supported integration.** For now, explicitly document the in-tree/PlanStan path and its limitations. Later replace it with installed-package instructions. Include one complete minimal example and lifecycle rules.
4. **`docs/FEATURES.md` — maturity matrix.** For every domain/backend/provider: implementation, unit/mock coverage, live verification date, provisionable provider, primary-consumer use, supported/experimental/disabled status, and known limitations.
5. **`docs/ROADMAP.md` — one outcome-level roadmap.** No session instructions. Show current top risks, ordered outcomes, acceptance evidence, and deferred breadth. Campaign plans may elaborate an active row but may not compete with it.
6. **`docs/KNOWN_ISSUES.md` — current open risks only.** Severity, affected supported paths, reproduction, workaround, owner/next action. The large FINDINGS file remains the immutable forensic log.
7. **`docs/COMPATIBILITY.md` — versions and policy.** Align CMake, tags, schema migrations, source/binary compatibility, consumer pins, and supported Qt/KF ranges.
8. **`docs/adr/` — compact architecture decisions.** Extract still-binding decisions from campaign prose; one decision per file, with status and supersession link.
9. **`docs/archive/` — historical campaigns and receipts.** Preserve dates and provenance, but remove them from default navigation and search expectations.

The root `CLAUDE.md` should shrink to repository-working instructions and point to the same maintained architecture/roadmap documents humans use. Agent-specific continuation state should never be the only source of current product truth.

### Documentation quality gates

Future releases should require:

- README status matches the code and release tag;
- exactly one current roadmap and one active-issues list;
- supported/experimental labels match registrable and provisionable code paths;
- test failures are classified independently along two axes: cause and product impact;
- consumer pins are read from consumer code or automatically checked, not manually copied;
- links and symbol anchors are checked automatically;
- stale status documents are archived or receive a superseded banner;
- campaign closure cannot occur while its own status table or definition-of-done remains incomplete;
- release notes include known failures that affect the primary consumer even when they predate the release;
- documentation changes are measured by removal/consolidation as well as addition.

### Final assessment of documentation impact

The documentation materially helps with deep defect archaeology, protocol truth, and reconstructing why individual changes were made. It also proves that the project has repeatedly recognized many of its own architectural risks.

It materially obscures present understanding by lacking a reliable front door, fragmenting authority, over-reporting campaign completion, under-reporting consumer incompleteness, and dismissing a primary-consumer lifecycle failure as environmental. Its sheer procedural volume makes intended architecture easier to find than delivered architecture.

The most telling fact is that this code-first audit independently rediscovered open findings already described well in the repository. The knowledge was present, but it was not governing priorities. The next documentation effort should not produce another campaign dossier. It should compress the existing knowledge into a small, current, consumer-centered contract—and the next code effort should make that contract true.
