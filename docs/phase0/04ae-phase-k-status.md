# Phase K — Engine generalization & semantic cleansing (Status)

**Status:** ✅ **K.9 landed 2026-05-15** — universal-sink shape contract fix
(`SyncBackend::shapeFor` wired into `dispatchSync`; per-collection shape on
`RawFilesBackend` / `GenericSqliteBackend`; `Shape::Any` no longer a
backend-declarable shape). Closing tag `v0.40-phase-k-engine-generalized`
drops after verify-all + manual HotSync regression test.
**Phases completed:** K.0, K.1, K.2, K.3, K.4, K.5, K.5.5, K.6, K.7, K.8a, K.8b, K.9
**Closing tag:** `v0.40-phase-k-engine-generalized` (pending tag drop)

## What exists now

**Structural generalization (K.0–K.5):**
- Unified `SyncEngine` consuming domain-generic `ISyncBackend` (née `IBlobBackend`)
- Capability interfaces (`ChangeDetection`, `ResourceLinearization`, `RecordRevision`)
- Baseline unification: single `Storage::BaselineStore` replaces calendar + blob variants
- Storage reorganization: `src/storage/` replaces `src/journal/` (mutable key-value stores, not journals)
- All three consumer repos migrated (libkalburator, PlanStan, WildPalms)

**Plugin extensibility surface (K.7 + K.8a):**
- **K.7 (landed 2026-05-11, tag `v0.37-phase-k7-complete`):** Four contribution
  interfaces; `Kalburator::Plugin` Qt interface + JSON manifest; `PluginManager`
  with DAG resolve, in-process and .so load paths. Stock plugins migrated;
  `DomainPlugin` deleted; `ProviderManager` rewired. 89/89 pass.
- **K.8a (landed 2026-05-14, tag `v0.38-phase-k8a-reference`):** Extracted inline
  `CalDavBackendContribution` / `CardDavBackendContribution` from `ProviderManager`'s
  constructor into public headers. Wrapped each in a `Kalburator::Plugin` subclass
  (`CalDavProviderPlugin`, `CardDavProviderPlugin`) registered via `registerStockPlugins()`.
  Removed `ProviderManager` auto-registration; application layer now seeds its local
  `BackendRegistry` (WildPalms `PalmRuntime`, PlanStan `CollectionController`).
  `examples/reference_consumer/` binary proves end-to-end calendar+contacts sync
  via the K.7 plugin surface. `tst_provider_plugin_registration` + `tst_reference_consumer_smoke`
  added as ctest gates. 91/91 pass.

**Semantic cleansing (K.5.5–K.6):**
- Complete vocabulary realignment: 12 coherent namespaces (`Engine`, `Backend`, `Conflict`, `Storage`, `Provider`, `Host`, etc.)
- Interface convention unified: no `I` prefix (Qt-idiomatic); `Abstract*` for QObject abstract bases
- Spec-name capitalization: `VCard*`, `ICal*`, `CalDAV*`, `CardDAV*`
- Consumer-specific docstring references removed; all comments and docs now backend-agnostic
- 30+ class renames, 12 namespace moves, 2 directory renames (complete in K.5.5)
- Audit reports (K.6.1) moved into phase doc tree for archival

**Verification gates passed:**
- ✅ Consumer-reference grep returns empty (K.6 gate)
- ✅ `IBlobBackend` / `SyncBackend` names removed from active code
- ✅ `QSyncCore::` sub-namespace collapsed into `Conflict::`
- ✅ All class renames applied (class-name grep gates)
- ✅ Directory moves complete (`journal/` → `storage/`, `sinks/` → `universal/`)
- ✅ `verify-all.sh` green: libkalburator 73/80 pass (known parallel flakes), PlanStan 82/106, WildPalms 81/81

## Test posture (2026-05-15, post-K.9)

- libkalburator: **92/92** pass (100%). K.9 added `tst_engine_universal_sink_dispatch`.
- PlanStan: **82/105** pass (env-gated tests Not-Run; `tst_inboxmanager` pre-existing fail).
- WildPalms: **71/71** pass.

## Next

✅ **Phase K.9** — universal-sink shape contract fix. Landed 2026-05-15.
   Discovered when the first real HotSync against a Palm device after K.8b
   failed with "cross-domain mappings not supported (src=calendar tgt=__any__)".
   Root cause: `SyncEngine::dispatchSync` resolved shape via
   `nativeShapes().first()` and rejected mismatched domains, but
   `RawFilesBackend` declared `Shape::Any()`, so every typed-source → universal-sink
   mapping bailed. K.8b's test suite never exercised a typed-source → universal-sink
   sync end-to-end, so the regression slipped through.
   Fix: shape moved from per-backend to per-collection.
   - `SyncBackend::shapeFor(collectionId)` (declared in K.4, previously unused)
     is now consulted by `dispatchSync` and `unifiedContinueAfterConflicts`.
   - `RawFilesBackend::createCollection` and `GenericSqliteBackend::createCollection`
     now take a required `Shape` arg and store it in a per-collection map.
   - `nativeShapes()` on universal sinks returns the dedup'd union of collection
     shapes; pre-K.9 it returned `{ Shape::Any() }` which was an attractive nuisance.
   - `Shape::Any` survives as a library primitive (TransformationRegistry sentinel,
     Pipeline default) but is no longer a *backend-declarable* shape.
   - `PalmRuntime::finishConnect` passes the source backend's
     `shapeFor(palmCol.id)` through to the RawFiles mirror's createCollection.
   - Side-fix: `PalmRuntime::runAllMappings().then()` and `runMirror.then()`
     propagate `SyncResult::errorMessage` to `PalmRunResult::errorMessage`
     so the UI shows the real engine error instead of an empty string.
   - New gate: `libkalburator/tests/engine/tst_engine_universal_sink_dispatch.cpp`
     pins typed-source → RawFiles round-trip through the unified engine.

Closing tag `v0.40-phase-k-engine-generalized` after `verify-all.sh` green +
manual HotSync against the user's Palm device confirms K.9 fixed the regression.

## Key commits

- `01e5a36`: K.5.5 complete — full convention sweep
- `fd3263e`: K.6 — consumer-specific docstring references removed
- `5a21fce`: K.6.1 — audit reports moved to phase doc tree
- `e0a805f`: K.6 gate verification complete
- `85f78ed`: K.7.3 bootstrap — `registerStockPlugins()` + `UniversalStoragePlugin`
- `07d562f`: K.7.3 complete — `CalendarDomainPlugin` → `CalendarPlugin` + decomposed
  contributions; delete `DomainPlugin` API; strip `DomainRegistry`; fix race in
  `SyncEngineWorker` fetch loops; 80 → 88 tests (all pass)
- K.7.4: Remove legacy `BackendFactory` API; rewire `ProviderManager` to
  `BackendRegistry::contributionFor`; 88 → 89 tests (DocsToGo scenario test)

---

## Lessons & findings

See `libkalburator/docs/phase0/04ab-phase-k-audits/` for audit reports and K.0 notes documenting the architectural reasoning and cross-consumer compatibility analysis that informed K's design.

**Architectural stability:** The unified engine is now backend-agnostic and domain-generic. Calendar, contacts, and future domains all route through `SyncEngine` + domain plugin architecture. Naming aligns with this reality.

**Consumer compatibility:** Both PlanStan and WildPalms compile + pass tests against the unified engine. The two-phase consumer rewrite (Phase Ic + Phase J for PlanStan, Phase Ic + Phase J + Phase K.8 for WildPalms) validates the abstraction.
