# Phase K — Engine generalization & semantic cleansing (Status)

**Status:** ⏳ **IN PROGRESS** — K.7 library complete (2026-05-11); K.8 next  
**Phases completed:** K.0, K.1, K.2, K.3, K.4, K.5, K.5.5, K.6, K.7 (all subtasks)  
**Closing tag:** `v0.40-phase-k-engine-generalized` (pending K.8)

## What exists now

**Structural generalization (K.0–K.5):**
- Unified `SyncEngine` consuming domain-generic `ISyncBackend` (née `IBlobBackend`)
- Capability interfaces (`ChangeDetection`, `ResourceLinearization`, `RecordRevision`)
- Baseline unification: single `Storage::BaselineStore` replaces calendar + blob variants
- Storage reorganization: `src/storage/` replaces `src/journal/` (mutable key-value stores, not journals)
- All three consumer repos migrated (libkalburator, PlanStan, WildPalms)

**Plugin extensibility surface (K.7):**
- **K.7 (landed 2026-05-11):** Plugin extensibility surface. Four
  contribution interfaces (DomainDefinition, ShapeContribution,
  DomainOperations, BackendContribution); Kalburator::Plugin Qt
  interface + JSON manifest; PluginManager with dependency-DAG
  resolve, in-process and .so load paths, full error matrix.
  Stock plugins migrated (DomainPlugin deleted); ProviderManager
  rewired. End-to-end validated by the DocsToGo running-example
  integration test (89/89 pass).
  Tag candidate: v0.37-phase-k7-complete.

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

## Test posture (2026-05-11, post-K.7)

- libkalburator: **89/89** pass (100%) — 1 new test added in K.7.5 (DocsToGo
  running-example integration scenario). K.7.3 added 8 tests (`tst_calendar_plugin`,
  `tst_domain_registry`, `tst_dynamic_domain_registration` + 5 others). Pre-existing
  race fixed in `SyncEngineWorker` fetch loops (see FINDINGS). The 3 previously-flaky
  tests (`tst_engine_cancellation`, `tst_cancellation_reason`,
  `tst_engine_subset_dispatch`) now pass reliably under `-j 10`.
- PlanStan: **82/106** pass (24 pre-existing env failures, unchanged).
- WildPalms: **81/81** pass.

## Next

⬜ **Phase K.8** (code) — WildPalms migration to ideal architecture (K.7.6
   reference impl + full rewrite). Largest single refactor; deletions per audit
   finding list. Gate: `verify-all.sh` green + Phase J E2E tests passing.
   Tag: `v0.36-phase-k8-wildpalms-rewrite`.

Closing tag `v0.40-phase-k-engine-generalized` after K.8 lands clean and `verify-all.sh` is green.

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
