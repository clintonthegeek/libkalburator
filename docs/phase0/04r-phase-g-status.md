# Phase G — Status

**Tag target #1:** `v0.15-phase-g-foundations` (after G.4)
**Tag target #2:** `v0.15.5-phase-g-engine-unified` (after G.8)
**Tag target #3:** `v0.16-phase-g-shape-pipeline` (after G.10)

**Sub-phase status:**

- G.1 Foundations — ✅ landed 2026-04-30
- G.2 Calendar plugin — ✅ landed 2026-04-30
- G.3 Backend interface migration — ✅ landed 2026-04-30
- G.4 Mapping-keyed baselines — ✅ landed 2026-05-01
- G.5 New domain plugins — ✅ landed 2026-05-01
- G.6 BlobDomainAdapter dispatch + MappingScheduler — ✅ landed 2026-05-01
- G.7 WildPalms transformation — ⏳ partial (Tasks 48-54 landed; Task 55 SyncRunner deletion deferred — deep integration with DeviceSession/DeviceWorker makes it a separate migration step)
- G.8 F1 facade deletion + universal sinks — ⏳ partial (Tasks 59-61 landed: RawFilesBackend, GenericSqliteBackend, 29 tests; Tasks 55/58 deferred — 40+ WildPalms callers of runBlobTwoWay/runBlobMirror use IBlobBackend* not SyncBackend*)
- G.9 ISyncHost narrowing + sync I/O retirement — ✅ landed 2026-05-01
- G.10 Loss profile UX + new stock backends — ✅ landed 2026-05-01

**Last task completed:** Task 90 (G.10 — retire 2-arg pushItems virtual; verify-all green)
**Next task:** User to authorize tag `v0.16-phase-g-shape-pipeline`
**Deferred:** Tasks 55/58 (SyncRunner_wp + F1 facade deletion); Task 84-87 (Akonadi/CardDAV
  stock backends — kf6pim not installed); Task 89 (WildPalms mapping UI — no topology editor
  in WildPalms).

## What landed in G.1

Foundational shape-pipeline vocabulary, all in
`src/shape/`:

- `shape.h/.cpp` — `Shape`, `DomainId`, `EncodingId`, `Shape::Any()`
  sentinel, qHash overloads.
- `propertycatalogue.h/.cpp` — `PropertyId`, `PropertyKind`,
  `PropertyDescriptor`, `PropertyCatalogue` (addProperty, find,
  hasProperty, sqlColumnDdl).
- `lossprofile.h/.cpp` — `LossLevel` enum, `LossProfile` with
  `compose()` (max level + union of dropped sets) and `summary()`.
- `transformationedge.h/.cpp` — `TransformationStage` abstract base,
  `IdentityStage`, `TransformationEdge` struct.
- `pipeline.h/.cpp` — `Pipeline` (identity / composed),
  `composedLoss()`, `apply()`, chain validation throwing
  `std::logic_error` on mismatch.
- `transformationregistry.h/.cpp` — singleton with `registerShape`,
  `declareCanonical`, `registerEdge`, `compile()` (hub-and-spoke
  algorithm), `inspect()`, `clear()` for tests.
- `canonicalrecord.h` — `CanonicalRecord` struct.
- `irecorddiffer.h` — `IRecordDiffer` interface (diff, equal).
- `irecordmerger.h` — `IRecordMerger` interface (3-way merge with
  ConflictPolicy parameter).
- `domainplugin.h` — `DomainPlugin` abstract interface.
- `domainregistry.h/.cpp` — `DomainRegistry` singleton with
  idempotent `initialize()`.

Test counts: libkalburator 26 → 32 (six new test executables under
`tests/shape/`). PlanStan and WildPalms unchanged.

## Tasks completed this phase

- Task 1 (preflight)
- Task 2 (status doc)
- Task 3 (clangd config)
- Task 4 (Shape value type)
- Task 5 (tst_shape pin)
- Task 6 (PropertyCatalogue)
- Task 7 (LossProfile)
- Task 8 (TransformationEdge + Pipeline)
- Task 9 (TransformationRegistry)
- Tasks 10+11 (DomainPlugin + DomainRegistry + IRecord interfaces)
  — landed in one commit because DomainPlugin's
  `unique_ptr<IRecord*>` returns require complete IRecord types
  wherever a plugin is instantiated; forward-decls alone don't work.
- Task 12 (G.1 verify-all gate; baseline refreshed)
- Task 13 (makeICalCatalogue — 18-property PropertyCatalogue)
- Task 14 (IRecordDifferICal — wraps IncidenceDiff::compare; tst_ical_record_differ)
- Task 15 (IRecordMergerICal — 3-way merge via IncidenceDiff; tst_ical_record_merger)
- Task 16 (KalburatorDomainCalendar plugin + static-init registrar; tst_calendar_plugin)
- Task 17 (CalendarDomainAdapter wired: CustomMerge delegates to
  registry IRecordMergerICal; no behavior change to existing policies)
- Task 18 (G.2 verify-all gate; libkalburator 32→35/35; baseline refreshed)
- Task 19 (add nativeShapes(), resourceId(), shapeFor() to SyncBackend base)
- Task 20 (override nativeShapes() in all 8 concrete backends; returns `{calendar, ical}`)
- Task 21 (mark dataDomain() [[deprecated]])
- Task 22 (migrate dataDomain() callsites — libkalburator: syncengine.cpp,
  iincidencesource.h, iincidenceregistry.h, stubincidenceregistry.h/cpp)
- Task 23 (migrate dataDomain() callsites — PlanStan: globalincidencemodel.h/cpp,
  collectioncontroller.cpp, itemloadingcoordinator.cpp, forwardscheduler.cpp,
  virtualprojectionmatcher.cpp, containerregistry.cpp)
- Task 24 (migrate dataDomain() callsites — WildPalms: palmcalendarbackend.h/cpp,
  tst_palmcalendarbackend.cpp; nativeShapes() returns `{calendar, palm-datebook}`)
- Task 25 (delete dataDomain() virtual + DataDomain enum; delete datadomain.h;
  remove stale includes from palmcalendarbackend.cpp and tst_planningengine_blockgraph.cpp)
- Task 26 (G.3 verify-all gate; 35/35, 96/120, 73/73; no flips)
- Tasks 27-29 (G.4 mapping-keyed baselines; BlobBaselineStore v3 schema)
- Task 30 (G.4 verify-all gate; tag prep)
- Tasks 32-40 (G.5 domain plugins: todo, contacts, memo + catalogues/differs/mergers)
- Task 41 (G.5/G.6 verify-all gate; 46/46 baseline)
- Tasks 42-43 (G.6 MappingScheduler + subset dispatch runSyncFuture)
- Task 44 (tst_mapping_scheduler; 9/9)
- Task 45 (SyncEngineFuture + CancellationReason)
- Task 46 (cancelWithReason + m_lostResources; tst_cancellation_reason 3/3)
- Task 47 (G.6 verify-all gate; 46/46, 96/120, 73/73)
- Task 48+49 (HotSyncCoordinator skeleton + engine wiring in WildPalms)
- Task 50 (PalmContactsBackend)
- Task 51 (PalmMemoBackend)
- Task 52 (PalmToDoBackend)
- Task 53 (Palm transformation stage stubs)
- Task 54 (Profile-side mapping registry via syncMappingsJson)

## Tasks completed in G.9

- Task 63 (ISyncHost narrowing — delete calendar-typed virtuals, add lifecycle events)
- Task 64 (CalendarManager wired via signals; CollectionController adapted)
- Task 65 (WildPalms ISyncHost consumers adapted)
- Task 66-67 (G.9.a verify-all; ISyncHost narrowed)
- Task 68 (tst_localbackend + tst_mockbackend_failure_injection migrated from PlanStan to libkalburator)
- Task 69 (tst_orgbackend + tst_orgbackend_external migrated; gated KALBURATOR_HAVE_ORG_IO)
- Task 70 (tst_decsyncbackend migrated)
- Task 71 (tst_remotebackend migrated)
- Task 72 (tst_backend_signals migrated)
- Task 73 (tst_akonadibackend migrated; gated KALBURATOR_HAVE_AKONADI)
- Tasks 74-79 (delete deprecated loadItems/storeItems/updateItem/writeFinished from SyncBackend + all backends)
- Task 75 (PlanStan tst_sync_directions migrated)
- Task 76 (PlanStan tst_calendarcrud migrated)
- Task 78 (PlanStan CollectionController::convertCalendarToBackend → pushItems + QEventLoop)
- Task 80 (G.9.b verify-all; 53/53, 90/114, 73/73)

## Tasks completed in G.10

- Task 81 (WhenLossWouldOccur enum + JSON serialization on SyncMapping)
- Task 82 (SyncEngine computes LossProfile and passes to ISyncHost::syncStarted)
- Task 83 (Kalburator::Widgets CMake target; LossProfileDetailView widget)
- Task 84-87 (Akonadi/CardDAV backends — deferred; kf6pim not installed)
- Task 88 (PlanStan TopologyInspectorPanel loss-policy combo + SyncTopologyWidget wiring)
- Task 89 (WildPalms mapping UI — N/A; no topology editor in WildPalms)
- Task 90 (retire vestigial 2-arg pushItems virtual; non-virtual inline replaces it)
- Task 91 (verify-all green; 53/53, 90/114, 73/73; tag pending user authorization)
