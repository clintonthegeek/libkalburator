# Phase G — Status

**Tag target #1:** `v0.15-phase-g-foundations` (after G.4)
**Tag target #2:** `v0.15.5-phase-g-engine-unified` (after G.8)
**Tag target #3:** `v0.16-phase-g-shape-pipeline` (after G.10)

**Sub-phase status:**

- G.1 Foundations — ✅ landed 2026-04-30
- G.2 Calendar plugin — ✅ landed 2026-04-30
- G.3 Backend interface migration — ⬜ not started
- G.4 Mapping-keyed baselines — ⬜ not started
- G.5 New domain plugins — ⬜ not started
- G.6 BlobDomainAdapter dispatch + MappingScheduler — ⬜ not started
- G.7 WildPalms transformation — ⬜ not started
- G.8 F1 facade deletion + universal sinks — ⬜ not started
- G.9 ISyncHost narrowing + sync I/O retirement — ⬜ not started
- G.10 Loss profile UX + new stock backends — ⬜ not started

**Last task completed:** Task 18 (G.2 verify-all gate)
**Next task:** Task 19 (add nativeShapes()/resourceId() to SyncBackend base, G.3)

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
