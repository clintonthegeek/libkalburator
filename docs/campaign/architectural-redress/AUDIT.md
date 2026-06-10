# Architectural-redress audit — VERIFIED REBUILD (2026-05-29)

> **Source of truth for the architectural-redress campaign.** This audit **supersedes** the
> 2026-05-28 fresh-eyes audit (archived at `archive/AUDIT-2026-05-28-original.md`), which was
> found to contain material factual errors that survived its own four-agent cross-validation
> (e.g. it located `syncoperation.h` in `sync/` when it lives in `calendar/`, and called a
> one-way include a "circular dependency").
>
> **Anchor-drift warning (2026-06-10):** The 2026-05-29 line-number anchors below have drifted.
> Notable: `syncengine.h` ctor :390→:164; G5 :1940 is no longer present; several resolved-finding
> anchors now show the **fixed** code, not the smell. Do NOT rewrite the audit; treat the quoted
> code as the original evidence, not the current state. Re-verify against HEAD before citing.
>
> **Method.** Every finding in the prior audit (G1–G8, B1–B9, U1–U5) was re-checked against
> the actual source at HEAD, alongside an independent 13-dimension gap sweep. Then **every
> surviving candidate — old and new — was put through an adversarial verifier whose job was to
> *refute* it against the cited source.** Only findings that held up survive here; 23
> candidates were rejected (see "Refuted / non-issues"). Run: 139 agents, code-only, no
> comments or docs trusted. Each finding carries `file:line` + a verbatim quote.
>
> If a plan and this audit disagree, the audit wins; update the plan. If reality and this
> audit disagree, write a FINDING and update the audit in the same commit.

**Tally:** GOOD 23 · CRITICAL 4 · MAJOR 20 · MODERATE 24 · MINOR 9 · refuted/rejected 23.
Raw verified payload: `archive/audit-v2-findings.json`.

## TL;DR

- The canon-upgrade convergence held: `src/transcoding/` is gone and the shape graph is the
  sole transformation mechanism (G5). DI/no-singleton discipline, the
  `IBlobBackend`/`SyncBackendBase` split, and per-domain plugin symmetry are all
  confirmed-good.
- The deepest structural rot is a **calendar-typed sync core**: `BackendRegistry` stores
  `SyncBackend*` and `ProviderManager` `dynamic_cast`s every provider backend to
  calendar-typed `SyncBackend`, so the orchestration layer (`sync/`) hard-depends on
  `calendar/` and drags KCalendarCore through it (two CRITICALs).
- The same root cause makes non-calendar backends (`RawFilesBackend`, `GenericSqliteBackend`,
  `RemoteContactsBackend`) inherit calendar-typed `SyncBackend` against the explicit Phase K.4
  design — a CRITICAL plus several MAJOR cross-domain include violations. **Fix the core and a
  cluster of MAJORs fall with it.**
- `SyncEngine` (2780/840 LOC) and the remote backends remain god classes; `types/` carries
  behavior (JSON, atomic I/O, lock registry) it should not; `shape/recordmerger.h` still pulls
  `conflict/` upward.
- Ownership hazards are real: raw `bool*`/`QFutureInterface*`/`QPromise*` lifetimes managed by
  hand, plus a thread-unsafe `RawFilesBackend`.
- Test coverage is strong for the engine/shape spine but **`CalendarManager` (destructive,
  immediate CRUD across all backends) is entirely untested** — a CRITICAL gap.
- The prior audit's headline B4 "circular dependency" was **wrong**: the real defect is a
  strictly one-way `sync/ → calendar/` layering violation, not a cycle.

## THE GOOD

| ID | Affirmation | Evidence |
|----|-------------|----------|
| G1 | No singletons; each session/test owns its registry | `src/shape/shaperegistries.h:18` ("no process-global default and no `::instance()` accessor"); `src/sync/backendregistry.h:52` ("instance() singleton removed; each session/test/embedder owns its own"); ctor `:25` |
| G2 | Consistent DI on `SyncEngine` ctor; no service-locator lookups | `src/engine/syncengine.h:390` (ctor takes `BackendRegistry*`, `ISyncHost*`, `ShapeRegistries&`); members `:738`; zero `qApp`/`::instance()` in the 2780-line impl |
| G3 | No `friend class` declarations in audited surface | grep across 182 src headers + 162 test files → zero matches |
| G4 | Clean POD/control-class split | value structs `src/types/synctypes.h:69/111/151/224`, `backendrecord.h:14`, `logicalcalendar.h:141`; control classes `calendarmetadatamanager.h:24`+`:63 private`, `providermanager.h:44` |
| G5 | Single transformation mechanism (`transcoding/` deleted) | `src/shape/transformationregistry.h:80`; used at `src/engine/syncengine.cpp:1940`; commit 88122b8 |
| G8 | Deprecated APIs marked `[[deprecated]]` with migration targets | `src/storage/baselinestore.h:123/129/134/139/143` (all cite the v3 mapping-keyed API, G.4) |
| — | `IProvider` / `BackendContribution` cleanly separated | `src/sync/iprovider.h:47`; `src/sync/backendcontribution.h:15` includes only `shape.h` + fwd-decl `IProvider` |
| — | `IBlobBackend` / `SyncBackendBase` split avoids diamond inheritance | `src/blob/iblobbackend.h:36` (no QObject); `src/sync/syncbackendbase.h:53` (QObject + IBlobBackend) |
| — | Blob signal contracts are test-only, never polymorphic in production | `tests/blob/tst_mockblobbackend.cpp:137/160`; declared per concrete class per `iblobbackend.h:29` |
| — | `FilteredCollectionBackend` inheriting `SyncBackend` is correct (wraps a parent) | `src/universal/filteredcollectionbackend.h:32/35`; required by `backendregistry.h:34` |
| — | `MarkdownFilesBackend` clean template-method specialization | `markdownfilesbackend.h:12`; overrides `suffixFor`/`recordStem` only (`markdownfilesbackend.cpp:49`) |
| — | `storage/` cleanly isolated (only `shape/canonicalrecord.h` + Qt) | `src/storage/baselinestore.h:35`; `idmappingstore.h:24` |
| — | `backend/` capability interfaces depend only on Qt | `src/backend/changedetection.h:4`; `resourcelinearization.h:4` |
| — | Domain differs polymorphic via `RecordDiffer`, no CRTP | `src/shape/recorddiffer.h:14`; `vcarddiffer.h:9`; `canonjsondiffer.h:16` |
| — | Each domain differ owns its SDK includes; no central super-utility | `vcarddiffer.cpp:3` (KContacts); `icalvtododiffer.cpp:3` (KCalendarCore); `textdiffer.cpp:3` (JSON only) |
| — | Other backends (Akonadi/DecSync/Org/Subscription) are legit interface impls, not god classes | `akonadibackend.h:37`; `decsyncbackend.h:36`; counts 23–45 reflect multi-interface contracts |
| — | `AsyncFileWriter` parentless worker is the correct thread-move pattern | `asyncfilewriter.cpp:171` + dtor `:190`, documented |
| — | `DecSyncBackend` raw members deterministically freed | `decsyncbackend.cpp:37/43` + dtor `:50` (`qDeleteAll`/`delete`) |
| — | Plugin two-method symmetry is principled, not copy-paste | `plugin/plugin.h:17`; `todoplugin.cpp:7`; `calendarplugin.h:6` (richer: 4 overrides) |
| — | JSON ser/deser individually tailored, no wholesale copy-paste | `conflictrecord.cpp:10/112`; `backendconfiguration.cpp:31`; `journal/baselinestore.cpp:52` |
| — | SyncEngine API + cancellation contract thoroughly tested end-to-end | `tst_engine_cancellation.cpp:84` (C1–C7); `tst_calendar_sync_full.cpp:79`; `tst_calendar_sync_error_recovery.cpp:96`; `tst_engine_write_gate.cpp:231` |
| — | Shape transform + diff/merge infra comprehensively unit-tested | 11 files in `tests/shape/`; `tst_incidencediff.cpp:243`; `tst_syncdiff.cpp:50`; `tst_property_phase.cpp:105` |

> Note: prior G6 ("per-domain consistency, all five domains carry the same quartet") and G7
> ("nine modules healthy") were **corrected** — see below. G6's PropertyIds companion exists
> in only 3/5 domains; G7's "healthy" overstated testedness for `diff/`/`discovery/`.

## THE BAD — Critical / Major

_Ordered by severity, then blast radius._

### CRITICAL — `BackendRegistry` stores a calendar-specific type from a lower layer

`BackendRegistry` (in `sync/`) stores `SyncBackend*`, where `SyncBackend` is defined in
`calendar/` — the orchestration layer hard-depends on a domain layer's concrete type, and its
`.cpp` `#include`s the calendar header (pulling KCalendarCore into `sync/`).

- `src/sync/backendregistry.h:34` — `void registerBackendInstance(const QString &backendId, SyncBackend *backend);`
- `src/sync/backendregistry.h:105` — `QMap<QString, SyncBackend*> m_instances;`
- `src/sync/backendregistry.cpp:3` — `#include "syncbackend.h"`
- `src/calendar/syncbackend.h:120` — `class SyncBackend : public SyncBackendBase`; `:40` `#include <KCalendarCore/MemoryCalendar>`

**Fix direction:** store `IBlobBackend*` (the neutral interface) in the registry; delegate any
up-casting to callers.

### CRITICAL — `ProviderManager` `dynamic_cast`s to the calendar-specific type

`ProviderManager::registerProviderBackends` casts the `IBlobBackend` from
`IProvider::createBackend()` to calendar-typed `SyncBackend`, enforcing that every provider
backend be calendar-derived and coupling `sync/` to `calendar/`.

- `src/sync/providermanager.cpp:7` — `#include "syncbackend.h"`
- `src/sync/providermanager.cpp:242` — `auto *asSync = dynamic_cast<SyncBackend*>(backend.get());`
- `src/sync/providermanager.cpp:249` — `m_registry->registerBackendInstance(backendId, asSync);`
- contract is neutral: `src/sync/iprovider.h:142` — `virtual std::unique_ptr<IBlobBackend> createBackend(...)`

**Fix direction:** accept `IBlobBackend` as the registration contract; remove the
calendar-typed cast.

### CRITICAL — Non-calendar backends inherit calendar-typed `SyncBackend` instead of neutral `SyncBackendBase`

`RawFilesBackend`, `GenericSqliteBackend`, and `RemoteContactsBackend` inherit `SyncBackend`,
directly contradicting the Phase K.4 design comment that names these very classes as ones that
must inherit `SyncBackendBase`. This drags KCalendarCore into `universal/` and `contacts/`.

- `src/universal/rawfilesbackend.h:27` — `class RawFilesBackend : public Kalburator::Sync::SyncBackend {`
- `src/universal/genericsqlitebackend.h:33` — `class GenericSqliteBackend : public Kalburator::Sync::SyncBackend {`
- `src/contacts/remotecontactsbackend.h:37` — `class RemoteContactsBackend : public SyncBackend,`
- `src/sync/syncbackendbase.h:5` — "non-calendar backends (RawFilesBackend, GenericSqliteBackend, RemoteContactsBackend, blob-only adapters) can inherit a base that does NOT pull in KCalendarCore"

**Fix direction:** reparent all three onto `SyncBackendBase`, importing only the interfaces
they use (depends on the BackendRegistry/ProviderManager fix above).

### CRITICAL — `CalendarManager` is entirely untested despite destructive, immediate CRUD

`CalendarManager` exposes immediate mutation methods that execute destructively across ALL
bindings synchronously (via `QEventLoop`), with no transactional rollback and a stub
`restoreFromSnapshot` returning `false`. No `tst_calendarmanager*.cpp` exists.

- `src/calendar/calendarmanager.h:101/145/213/249` (class + `deleteCalendar`/`deleteIncidence`/`restoreFromSnapshot`)
- `src/calendar/calendarmanager.cpp:776` — `// For now, this is a placeholder...`; `:782` — `return false;`

**Fix direction:** add unit + integration tests covering success, partial-failure recovery,
batch semantics, and snapshot/restore **before** any refactor.

### MAJOR — `SyncEngine` god class + incomplete worker unification (B1, corrected)

`engine/syncengine.cpp` is 2780 LOC, `.h` 840 LOC; `SyncEngineWorker` is a publicly-declared
QObject (lines 119–335) with an `m_engine` back-pointer; four overlapping `runSyncFuture()`
overloads; implicit `m_pendingOverride` state; dual `Mode`/`SyncBehavior` enums.

- `src/engine/syncengine.h:119` worker; `:331` `SyncEngine *m_engine = nullptr;`; `:759` `ExecutionOverride m_pendingOverride;`; enums `:127` and `:381`
- **Corrected:** completion is wired correctly — `src/engine/syncengine.cpp:128`
  `connect(m_worker, &SyncEngineWorker::syncCompleted, this, &SyncEngine::onWorkerSyncCompleted, Qt::QueuedConnection)`; the `invokeMethod(m_engine, …)` calls (e.g. `:1668`) marshal
  baseline-store access only, not completion.

**Fix direction:** extract `SyncEngineWorker` to its own TU; replace the back-pointer +
`invokeMethod` baseline marshaling with a dedicated signal/slot or a thread-safe `BaselineStore`
proxy. _(Note: prior Plan 1 work on branch `feature/redress-1-syncengine` already addresses
much of this; reconcile when re-sequencing.)_

### MAJOR — `types/` carries behavior (JSON, atomic I/O, lock registry) (B2, corrected)

The layer CMake advertises as a "minimal type vocabulary" embeds JSON ser/deser
(`logicalcalendar.h:449`), full JSON round-trip (`backendconfiguration.cpp:173`), JSON-lines
crash I/O (`crashjournal.cpp:24`), atomic `QSaveFile` writes (`calendarmetadatamanager.cpp:189`),
and a QObject lock registry (`incidencelock_registry.cpp:33`).

- evidence above + `CMakeLists.txt:7` ("minimal type vocabulary")
- **Corrected:** the `shape.h` include is **not** a violation — `CMakeLists.txt:50` co-bundles
  `shape` into `kalburator-types` intentionally.

**Fix direction:** split `types/` into a pure vocabulary sub-target and a separate helpers
target for codecs/I/O/lock machinery.

### MAJOR — Remote/Local backend god classes (B3, corrected)

`RemoteCalendarBackend` is 2649 LOC / 427-LOC header with ~60 public methods over 7 concerns;
`LocalBackend` ~1300 LOC / 224-LOC header with ~55 methods.

- `remotecalendarbackend.cpp` 2649 lines; 7 `discoveredX` getters (`:102 … :205`);
  `localbackend.h:124–133` (4 metadata setters)
- **Corrected:** `collectionRevision()` is a batched network PROPFIND via `fetchAllCtags`
  (`:158`), not a delegate — `cachedCollectionRevision()` is the `ctag()` delegate (`:168`);
  there are 7 (not 6) `discoveredX` getters; method counts higher than the prior audit stated.
- **Updated 2026-06-06:** the out-of-campaign v0.63 release (CalDAV discovery primer +
  content-cache determinism + `setCacheDir`) grew this target to **2718 LOC / 472-LOC header**
  and expanded the discovery-state surface the MODERATEs below catalogue. Line-number evidence
  above is from the 2026-05-29 tree; **re-derive locations when Plan 7 is written.**

**Fix direction:** extract a `DiscoveredCalendarInfo` DTO for the getters; split IBlobBackend /
ChangeDetection / calendar-CRUD into collaborators.

### MAJOR — `sync/` includes domain backends, but there is NO cycle (B4, corrected)

`sync/akonadiprovider.cpp` includes both `../calendar/akonadibackend.h` and
`../contacts/akonadicontactsbackend.h`; `sync/akonadibackendcontribution.cpp` includes the
contacts header. One-way `sync/ → domain` violation.

- `akonadiprovider.cpp:8/9`; `akonadibackendcontribution.cpp:5`
- **Corrected:** `syncoperation.h` lives in `src/calendar/` and is a same-directory include
  (`akonadibackend.h:7`), so the prior audit's "cycle closure" does not exist; and
  `akonadiprovider.cpp` (not just the contribution) directly pulls contacts.

**Fix direction:** invert via provider-facing interfaces (e.g. an `IBlobBackend` factory) in
`sync/`. _(Largely subsumed by the two registry/provider CRITICALs.)_

### MAJOR — `shape/recordmerger.h` pulls `conflict/conflictpolicy.h` upward (B6, corrected)

`shape/recordmerger.h:4` includes `conflictpolicy.h` and uses
`Kalburator::Conflict::ConflictPolicy` in its pure-virtual signature (`:30`) — an upward
dependency from the abstract transformation layer into the engine-orbit `conflict/` layer.

- `recordmerger.h:4/30`; `conflictpolicy.h:20`
- **Corrected:** `recordwriter.h:7` → `types/backendrecord.h` is a valid **downward**
  dependency; `canonjsonmerger.h` only references `ConflictPolicy` transitively (it includes
  `recordmerger.h`, not `conflictpolicy.h` directly).

**Fix direction:** move `ConflictPolicy` into `types/` (or `shape/`) so `shape/` references it
without depending on `conflict/`.
- **RESOLVED 2026-06-06 (Plan 6) — by narrowing, not moving.** Code verification showed all
  9 merger impls read only `policy.autoResolve` and production passes constant `deferAll()`;
  `merge()` now takes `Shape::AutoResolveStrategy` (enum extracted to
  `shape/autoresolvestrategy.h`; `Conflict::` alias preserved). `ConflictPolicy` stays in
  `conflict/` — the audit's "move ConflictPolicy" direction would have violated the Plan 5
  purity gate and dragged `ConflictRecord` down. shape/→conflict/ edge count: 0.

### MAJOR — `engine/syncengine.h` includes calendar-specific `syncoperation.h`

`syncengine.h:13` includes `syncoperation.h`, which lives in `src/calendar/` and defines both
the neutral `SyncOperation` base and calendar-specific `FetchOperation`/`PushOperation`/
`DeleteOperation` (with `KCalendarCore` members). The `await<Op>` template needs the base.

- `syncengine.h:13/246`; `calendar/syncoperation.h:40/200/7`

**Fix direction:** extract the neutral `SyncOperation` base to `sync/` (or `types/`); have
`engine/` depend only on that.

### MAJOR — `contacts/` includes calendar-specific `syncbackend.h` + `syncoperation.h`

`contacts/akonadicontactsbackend.h:6/7` and `remotecontactsbackend.h:4` include calendar-domain
headers and inherit calendar-typed `SyncBackend`.

- above + `akonadicontactsbackend.h:44` `class AkonadiContactsBackend : public SyncBackend`

**Fix direction:** extract neutral base + operation types to `sync/`; reparent contacts
backends onto `SyncBackendBase`.

### MAJOR — `universal/` backends include calendar-specific `syncbackend.h`

`filteredcollectionbackend.h:5`, `rawfilesbackend.h:6`, `genericsqlitebackend.h:9` include
`syncbackend.h`, pulling KCalendarCore into domain-agnostic code; none override calendar-typed
methods.

- above + `syncbackend.h:25` (design says non-calendar backends inherit `SyncBackendBase`)

**Fix direction:** reparent the universal backends onto `SyncBackendBase`.
(`FilteredCollectionBackend` is the exception — it wraps a `SyncBackend*` parent and is
correctly typed; only its KCalendarCore pull-through is incidental.)

### MAJOR — `RawFilesBackend` has no thread synchronization for shared collections

`RawFilesBackend` accesses `m_collections`/`m_shapeByCollection` with no mutex while the worker
thread reads them via `shapeFor()`. (`GenericSqliteBackend`'s `m_connMutex` guards only
`m_openConnections`, so it shares the same race on its collection hashes.)

- `rawfilesbackend.h:89`; `genericsqlitebackend.h:88`; `engine/syncengine.cpp:1439` (worker
  calls `shapeFor`), `:1394` (worker thread)

**Fix direction:** guard both backends' collection hashes with a `QMutex`, or document
non-thread-safety and serialize at the caller.

### MAJOR — `RemoteCalendarBackend`/`RemoteContactsBackend` duplicate `QEventLoop` network-wait boilerplate

11+ identical connect-exec-cleanup blocks in `RemoteCalendarBackend` and 4+ in
`RemoteContactsBackend`.

- `remotecalendarbackend.cpp:626/1278/1502`; `remotecontactsbackend.cpp:284/350`

**Fix direction:** extract a private `sendCustomRequestSync` helper encapsulating the reply wait
+ error handling.

### MAJOR — `MockBlobBackend` injects `OnLoadRecords` failure but doesn't override `loadRecordsOrError`

Failure is swallowed by the base `loadRecordsOrError()` default, which returns `true` with
cleared error.

- `mockblobbackend.h:25`; `mockblobbackend.cpp:48`; `iblobbackend.h:55`

**Fix direction:** override `loadRecordsOrError()` in the mock to report `OnLoadRecords`
failures. _(Test-harness correctness bug — silent false-greens.)_

### MAJOR — `GenericSqliteBackend::clearCollection` (void) silently ignores DELETE failure

`clearCollection`/`deleteCollection` exec DELETE/DROP without checking, unlike
`deleteRecord`/`createRecord` in the same class.

- `genericsqlitebackend.cpp:121/108` vs checked `:236/:198`

**Fix direction:** return `bool` (or signal) and log/early-return on `exec()` failure.

### MAJOR — Raw `bool*` captured in `CardDavProvider` lambda (use-after-free risk)

`bool *errorSeen = new bool(false)` captured by two lambdas, one of which `delete`s it; relies
on undocumented Qt signal-firing order.

- `carddavprovider.cpp:79/93`; ordering at `carddavcapabilitydiscovery.cpp:469`

**Fix direction:** use `std::make_shared<bool>` or a member with proper lifetime.

### MAJOR — Raw `QFutureInterface*` in `SyncEngine` without lifecycle management

`m_currentSingleIface`/`m_currentMultiIface` allocated raw, deleted only in conditional
completion paths; the destructor (`stopWorkerThread()` only) doesn't clean them up — leak/dangle
if destroyed mid-sync.

- `syncengine.cpp:477/530/565`; `syncengine.h:772`; dtor `syncengine.cpp:105`

**Fix direction:** wrap in `std::unique_ptr` or use stack-allocated move semantics.

### MAJOR — `backend/` is a capability bin with no clear principle (B5-adjacent)

`backend/changedetection.h` and `resourcelinearization.h` are mixin capability interfaces, not
backends; the directory has no stated principle.

- `changedetection.h:40/12`; `resourcelinearization.h:27`; `remotecalendarbackend.h:27` inherits
  the mixin

**Fix direction:** rename/move to `capabilities/` (or `sync/capabilities/`); the directory
should hold either backend base classes or nothing.

### MAJOR — Store/Manager naming collisions (U3, corrected)

Two unrelated `BaselineStore` classes (SQLite `storage/baselinestore.h:39` vs in-memory
`journal/baselinestore.h:35`), two conflict stores (`conflict/conflictstore.h:31` in-memory vs
`calendar/syncconflictstore.h:24` SQLite), five `Manager` classes across five namespaces, and
`CalendarMetadataManager` doing file I/O from `types/` in `Kalburator::Sync`.

- cited above + `calendarmetadatamanager.h:8` namespace
- **Corrected:** the prior audit missed the second (journal) `BaselineStore` and the second
  (`SyncConflictStore`) conflict store.

**Fix direction:** encode persistence/responsibility in names (Db/Persistent vs Cache/Index),
reserve `Manager` for one role, relocate `CalendarMetadataManager` to `calendar/`.

### MAJOR — Test gaps behind the "nine healthy modules" affirmation (G7, downgraded)

Nine modules are structurally clean, but `diff/` has no dedicated `tests/diff/` (tested only via
`tests/calendar/`) and `discovery/` has zero coverage.

- `tests/calendar/tst_incidencediff.cpp:9`, `tst_syncdiff.cpp:4`

**Fix direction:** add a dedicated `tests/diff/` suite and a `tests/discovery/` smoke test.

### MAJOR — Critical `SyncEngine` config APIs untested

`loadSyncMappings`, `setMappingEnabled`, `registerActiveController`/`unregister…`,
`setSkipUnchangedMappings` have zero explicit coverage; `hasSyncWork()` only indirect coverage
via `SyncRunCoordinator`.

- `syncengine.h:457/499/515`; `syncengine.cpp:265`; `syncruncoordinator.cpp:37`;
  `tst_syncruncoordinator.cpp:70`

**Fix direction:** add unit tests verifying these enable/disable/queue work correctly.

## THE BAD — Moderate / Minor

### MODERATE
- **`sync/syncruncoordinator.h:20` includes `engine/syncengine.h`** for `SyncBehavior` — one-way (no cycle); forward-declare or invert. (`:15/19/83`, `syncengine.h:381`)
- **`calendar/syncbackend.h` lives in `calendar/` but is `Kalburator::Sync`** — physical/logical mismatch with calendar signals + KCalendarCore. (`:4/47/40/285`) Consider moving to `sync/`.
- **Implicit CMake include paths expose domain headers to `sync/`** — `caldavprovider.cpp:6/7`, `carddavprovider.cpp:5/7` use unqualified includes while `multiprotocoldavprovider.cpp:4` uses `../calendar/`; inconsistent.
- **`U1` "Backend" overloaded across 6 constructs in 4 dirs** (corrected: `SyncBackend` is calendar-typed, neutral one is `SyncBackendBase`; `ChangeDetection` excluded). `syncbackend.h:120`, `syncbackendbase.h:53`, `iblobbackend.h:36`, `backendcontribution.h:15`, `backendconfiguration.h:81`, `backendcapabilities.h:141`.
- **`B5` three backend-adjacent dirs lack documented layer position + namespace fragmentation** (Backend/Storage/Sinks). `changedetection.h:8`, `baselinestore.h:37`, `rawfilesbackend.h:9`; phases K.1/K.5/K.7.3.
- **`B7` `CalendarManager` mixes Calendar/Binding/Incidence CRUD + repeated skeleton** (corrected: 5 DeleteMode variants, 17 public methods; no routine "check caps"/"update baseline" steps). `calendarmanager.cpp:927`; `:73/125/128`.
- **`B8` `IncidenceDiff` is a namespace-as-class** (1160 LOC, all-static; corrected: `vcarddiffer`/`icalvtododiffer` are properly polymorphic, not equivalents). `incidencediff.h:71/91`.
- **`PropertyDiff`/`IncidenceDiff` are calendar-specific but in `Sync` namespace** — `incidencediff.h:8/10`; used only from `calendar/`; engine deliberately defines a separate `MapPropertyDiff` (`propertydiff.h:23`).
- **`IncidenceDiff` duplicates property-catalogue metadata** — display-name/category/priority maps in `incidencediff.cpp:20/72` duplicate `calendar/icalproperties.cpp:7`.
- **7 loose `discovered*` getters leak `RemoteCalendarBackend` discovery state** — `remotecalendarbackend.h:102…205`, private maps `:350/351`. Consolidate into a DTO.
- **`discoveredCapabilities()` bulk getter** exposes whole struct + nested map — `caldavcapabilitydiscovery.h:76`; both callers reach into `.perCalendarCapabilities`.
- **Discovery URL maps duplicated across discovery/provider/backend** — `caldavcapabilitydiscovery.h:158`, `caldavprovider.h:66`, `remotecalendarbackend.h:350`.
- **Asymmetric discovery placement** — `CalDavCapabilityDiscovery` in `calendar/` (`:14/41`), `CardDavCapabilityDiscovery` in `sync/`; `multiprotocoldavprovider.cpp:4` crosses upward into calendar.
- **`FilteredCollectionBackend` couples to `BackendRegistry` via `parentBackendId`** — `filteredcollectionbackend.h:29/36`, `.cpp:39`; `backendId()` defaults to `backendType()` (`syncbackendbase.cpp:60`).
- **`FilteredCollectionBackend` `const_cast` in const methods** — `filteredcollectionbackend.cpp:48/94` cast away const to call non-const `collectionInfo()` (`iblobbackend.h:47`).
- **Identical `nativeShapes()`/`shapeFor()` in `GenericSqliteBackend` + `RawFilesBackend`** — byte-for-byte (`genericsqlitebackend.cpp:86/97`, `rawfilesbackend.cpp:56/67`). Hoist to a shared base.
- **Repeated `QEventLoop` boilerplate in `CalendarManager` incidence ops** — `calendarmanager.cpp:582/627/674`; an `executeOnAllBindings` helper already exists (`:338`).
- **No `Q_DISABLE_COPY` on QObject subclasses** — `backendregistry.h:21`, `asyncfilewriter.h:74`, `conflictmanager.h:38`, `incidencelock_registry.h:37`.
- **Raw `QPromise*` fragmentation in `CardDavCapabilityDiscovery`** — `new` at `:74`, manual `delete` in 4 sites (`:62/471/485` + dtor).
- **`ConflictManager` namespace mismatch** — `conflict/conflictmanager.h:12` is `Kalburator::Sync` while peers are `Kalburator::Conflict` (`conflictrecord.h:27`, `conflictstore.h:23`).
- **Silent PRAGMA failures in `SyncConflictStore::initDatabase`** — `syncconflictstore.cpp:64/65` + ALTER TABLE `:105–108` unchecked while other methods check.
- **Silent PRAGMA failures in `IDMappingStore::open`** — `idmappingstore.cpp:97/98` unchecked while `db.open()`/schema/`PRAGMA table_info` are checked.
- **Inconsistent error channels** — `CalendarMetadataManager` returns `bool` (`calendarmetadatamanager.h:67/68`) vs `SyncConflictStore` void+logging (`syncconflictstore.h:64/74`).

### MINOR
- **`U2` "Canon" vs "Canonical" abbreviation inconsistency** (corrected: one concept, not 3-4 meanings) — `canonicalrecord.h:13` full word vs `canonenvelope.h:15`/`*CanonStages`/`canonicalShape()` abbreviated.
- **`U4` `BackendRegistry` dual-responsibility** (corrected: method names ARE unambiguous — `…Instance` vs `…Contribution`) — `backendregistry.h:34/60/105`. Split into two registries.
- **Two `BaselineStore` classes** — `journal/baselinestore.h:35` (in-memory, stale/unused per `CMakeLists.txt:202`) vs `storage/baselinestore.h:39`.
- **`progressUpdated` declared but never emitted** — `iblobbackend.h:34`, `mockblobbackend.h:71`, `localblobbackend.h:62`; emitted only by the engine.
- **Ignored `CREATE INDEX` result** — `remotecalendarbackend.cpp:374`; `m_cacheInitialized=true` set unconditionally (`:376`).
- **`PerCalendarCapabilities`/`DiscoveredCapabilities` direct field access** — `backendconfiguration.h:20`; valid DTO pattern, smell only.
- **`addressbookUrls()` loose getter** — `carddavcapabilitydiscovery.h:79` returns full map by value (internal to `sync/`).
- **`IncidenceSyncAdapter` never instantiated** — `incidencesyncadapter.h:17`; scaffolding for future `qsynccore` (`isyncrecord.h:16`). Remove or retain explicitly.
- **`ResourceLinearization` never implemented** — `resourcelinearization.h:27`; intentional Phase K.1 scaffolding for a future Palm backend. Remove or retain explicitly.

## THE UGLY — Naming & Semantics

- **`Backend` is overloaded six ways** across `blob/`, `sync/`, `types/`, `calendar/` —
  `SyncBackend` (calendar abstract), `SyncBackendBase` (neutral abstract), `IBlobBackend` (blob
  iface), `BackendContribution` (factory), `BackendConfiguration` (config struct),
  `BackendCapabilities` (capability struct). The prior audit even mislabeled which one is the
  neutral interface. (U1)
- **Two classes named `BaselineStore`** with opposite persistence and namespaces; **two conflict
  stores** (`ConflictStore` in-memory, `SyncConflictStore` SQLite). (U3)
- **`Manager` means five unrelated things** — CRUD orchestration, conflict workflow, provider
  lifecycle, plugin loading, filesystem I/O — and `CalendarMetadataManager` is mislocated in
  `types/` under `Kalburator::Sync`. (U3)
- **`Canon` vs `Canonical`** — one concept, mixed abbreviation (`CanonicalRecord` vs every other
  `Canon*`). (U2)
- **`ConflictManager` sits in `conflict/` but is `Kalburator::Sync`**, breaking
  namespace-directory correspondence.
- **`IncidenceDiff` is a class that is really a namespace** (all-static) and is calendar-specific
  while living under `Sync`.

## Corrected from the prior (2026-05-28) audit

- **B4 (the headline):** the prior audit called this a **circular dependency `sync/ ↔ calendar/`**.
  It is **not a cycle** — `syncoperation.h` lives in `src/calendar/` and is a same-directory
  include from `akonadibackend.h:7`, so the alleged back-edge into `sync/` does not exist. The
  real defect is a strictly **one-way `sync/ → calendar/` (and `→ contacts/`) layering
  violation**, with the contacts include attributable to `akonadiprovider.cpp:9` too (not only
  the contribution file). `calendar/syncbackend.h` correctly *inherits* the neutral base — it is
  not a competing base.
- **B1:** completion signaling was claimed to use `invokeMethod(m_engine,"onWorkerSyncCompleted",…)`
  — false. Completion is a proper `Qt::QueuedConnection` (`syncengine.cpp:128`); the
  `invokeMethod` calls only marshal baseline-store access. Private-member count understated
  (~33–36, not ~20).
- **B3:** `collectionRevision()` is **not** a trivial `ctag()` delegate — it is a batched network
  PROPFIND (`fetchAllCtags`); the delegate is `cachedCollectionRevision()`. There are **7**
  `discoveredX` getters (not 6); method counts higher than stated.
- **B2:** the `shape.h` include in `types/` is **not** a layering violation — it is intentionally
  co-bundled (`CMakeLists.txt:50`).
- **B5:** `backend/` has **2** files (not 1); the `universal/` list omitted
  `universalstorageplugin`.
- **B6:** `canonjsonmerger.h` does **not** directly include `conflictpolicy.h`; only
  `recordmerger.h` does. `recordwriter.h → types/` is a valid downward dep, not a violation.
- **B7:** `DeleteMode` has **5** variants (not 4); **17** public methods (not ~25); the skeleton's
  "check caps"/"update baseline" steps don't exist as routine steps.
- **U2:** there are **not** 3-4 "Canon" meanings — one concept, with an abbreviation
  inconsistency.
- **U4:** `BackendRegistry` method names are **not** ambiguous (`…Instance`/`…Contribution`); the
  real issue is dual-responsibility.
- **G6:** outline does *not* have an `outlineCanonPropertyIds()` companion; only 3/5 domains carry
  the PropertyIds pair. Affirmation false.

## Refuted / non-issues

Candidates that failed the adversarial gate — recorded so they are not re-litigated, and so no
one "fixes" a non-problem:

- **B9 / "three unused interfaces"** — `IConflictPresenter` is used by `ConflictManager`;
  `ICommandDispatcher` is an intentional app-shell interface; `IIncidenceRegistry` has a full
  `StubIncidenceRegistry` test impl. Only `IIncidenceSource` may warrant review.
- **U5** — `collectionRevision`/`cachedCollectionRevision` swapped; `CalendarManager` has no
  `updateLogicalCalendar` (it's on the config manager); `isValid()` count is 18, not "20+".
- **"`RecordDifferICal`/`RecordMergerICal` are dead code"** — both are instantiated in
  `tests/calendar/differs/` and built into the library.
- **"`conflict/conflictmanager.h` includes `calendar/iconflictresolver.h` (violation)"** — the
  interface is neutral (`Kalburator::Sync`) and DI-injected; location ≠ layer.
- **"Dependency graph fundamentally healthy"** — refuted as an unqualified affirmation by the real
  `shape/ → conflict/` dependency it omits.
- **"mixed include styles in `multiprotocoldavprovider.cpp` are an error"** — same-dir unqualified
  + cross-dir relative is correct.
- **"IProvider/BackendRegistry contract mismatch (bug)"** — intentional, documented DI design with
  runtime validation.
- **"`SyncBackendBase` is the neutral base for `RemoteContactsBackend`"** — false; it inherits
  calendar-typed `SyncBackend` (captured as a CRITICAL instead).
- **"`SyncConflictStore` location is a cross-dir dependency problem"** — include is `.cpp`-only
  with a header forward declaration.
- **"B5 dirs lack ownership model entirely"** — too strong; separation exists, only
  documentation/namespace fragmentation is the (moderate) issue.
- **"`IncidenceDiff` exports internal helpers publicly"** — the helpers are `private`;
  header-cleanliness preference only.
- **"`QPointer` usage incorrect for NetworkReply"** — premise wrong (no `m_currentReply` in
  RemoteCalendarBackend; RemoteContactsBackend nulls before delete).
- **"`IncidenceRef::isOccurrence` non-atomic"** — const method, value type, unused; no race.
- **Five testing candidates** rejected on factual errors: `waitForFinished` anti-pattern (tests
  use custom watchers), "no real-backend e2e tests" (refuted by
  `tst_carddav_engine_integration.cpp`), DecSync temp-dir isolation, conflict-pause cancel
  "untested" (covered by `cancelDuringConflictPause`), and "comprehensive backend coverage"
  (overstated).

## What not to touch

Carried forward and re-confirmed; do **not** "fix" these unless a survivor above contradicts them:

- **DI / no-singleton ownership** (G1, G2) — registries are plain instantiable types injected via
  constructors. Keep it that way.
- **Per-domain plugin symmetry** — the two-method `Plugin` pattern (and CalendarPlugin's four) is
  principled, not copy-paste.
- **Shape graph as the sole transformation mechanism** (G5, invariant 1) — extend the shape graph;
  never fork a third conversion path. `src/transcoding/` is gone; keep it gone.
- **`[[deprecated]]` baseline v2 scaffolding** (G8) — keep the annotations and the v2 table until
  all callers migrate to v3, then remove.
- Additional confirmed-clean: the `IBlobBackend`/`SyncBackendBase` split, `storage/` and
  `backend/` minimal includes, polymorphic `RecordDiffer` differs with per-domain SDK ownership,
  the `AsyncFileWriter`/`DecSyncBackend` manual-ownership patterns, and the engine/shape test
  spine.
