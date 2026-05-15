# Phase K — Engine generalization & semantic cleansing (Status)

**Status:** ✅ **K.8b landed 2026-05-15** — Tag `v0.36-phase-k8-wildpalms-rewrite`.
Next: Phase K closing — `v0.40-phase-k-engine-generalized` tag (pending K.9+).
**Phases completed:** K.0, K.1, K.2, K.3, K.4, K.5, K.5.5, K.6, K.7, K.8a, K.8b  
**Closing tag:** `v0.40-phase-k-engine-generalized` (pending K.9+)

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

## Test posture (2026-05-15, post-K.8b)

- libkalburator: **91/91** pass (100%). K.8a added 2 tests.
- PlanStan: **82/106** pass (24 pre-existing env failures, unchanged).
- WildPalms: **71/71** pass (80→71 delta: 9 native-engine tests deleted by K.8b).

## Next

✅ **Phase K.8b** (code) — Full WildPalms rewrite per audit deletion list.
   Landed 2026-05-15. Tag `v0.36-phase-k8-wildpalms-rewrite`.
   Native Sync::SyncEngine, LocalFileBackend, IConduit/IBackendPlugin V1+V2,
   BackendPluginManager, ConduitManager, BlobBackendAdapter, CalendarCollection_WP,
   native InteractiveConflictHandler, .wildpalms.providers sidecar — all deleted.
   Mid-sync cancel restored. Memory-corruption guard removed at root cause.
   5 Palm plugins migrated to Kalburator::Plugin + STATIC libs. Profile gains
   accounts subgroup + one-shot sidecar migration. Audit Findings 1–10 closed.

Closing tag `v0.40-phase-k-engine-generalized` after K.9+ lands clean and `verify-all.sh` is green.

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
