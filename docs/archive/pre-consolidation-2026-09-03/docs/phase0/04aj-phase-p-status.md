# Phase P — Merge Readiness — Status

**Status:** ✅ landed 2026-05-21 (`v0.52-phase-p-merge-ready`)
**Branch:** `refactor/engine-merger`
**Design:** `~/dev/refactor-engine-merger/2026-05-20-phase-p-merge-readiness-design.md`
**Plan:** `~/dev/refactor-engine-merger/2026-05-20-phase-p-merge-readiness-plan.md`

## What exists now

### P.1 — events render
- `CollectionController::connectBackendSignals(id, backend)` private helper called from both `loadAndCreateBackends` and `mirrorProviderBackends`. Guarded by `QSet<QString> m_signalsConnectedBackends` against the mirror's double-call.
- `CollectionController::recordChanged(mappingId, recordId, ChangeKind)` implemented (was a Phase G stub pointing at a non-existent "Task 67" through 8 phases — see 04w §A.7).
- `ItemLoadingCoordinator::onItemDeleted(calendarId, recordId)` added for the Deleted change kind.

### P.2 — topology editor renders 4th data-model layer
- `SyncTopologyWidget::m_logicalBlock` instantiates the existing `LogicalCalendarsBlock` (created in O.3.5, never instantiated until now). Rows populated from `m_dataSource->logicalCalendars()`.
- `createEdgeForMapping` resolves `mapping.targetBackend == "logical"` against the block's per-row `in:<lcId>` anchors. The silent-skip from before is gone.
- `SyncTopologyValidator` warns when a logical calendar has zero enabled bindings.
- Lifecycle note: `LogicalCalendarsBlock` is a `QGraphicsObject` (not `QGraphicsRectItem` like `BackendNode`); explicit `delete m_logicalBlock` required in dtor + rebuildGraph to prevent stack-smashing crashes in some Qt styles.

### P.3 — multi-backend wizard
- `LogicalCalendarGenerator::fromMultipleBackends(sels, primaryUuid)` delegates merge-by-name to libkalburator's `LogicalCalendarBuilder::autoMatch`.
- `CalendarDiscoveryStep` supports N providers via QTabWidget; "Add another account" opens `ProviderConfigDialog` in `AddNew` mode (full reuse of existing dialog).
- `SyncTopologyViewPanel::enterNewCollectionWizard` connects both `discoverySelected` (single) and `multiDiscoverySelected` (multi) to a shared `applyGeneratedLayer` helper.
- Semantic: initial load is Primary-binding-only by design (loadItemsForCalendar routes through Primary). Secondary bindings populate via the sync engine running the `SyncMapping` — that's separate, not a Phase P concern.

### P.4 — e2e gate
- `PlanStan/tests/e2e-wizard/` directory created. Three tests:
  - `tst_wizard_single_caldav_events_visible` — wizard-finish pins events-visible
  - `tst_wizard_multi_provider_merge` — N=2 providers → merged logical with 2 bindings + events from Primary visible
  - `tst_wizard_reopen_events_visible` — save → reopen → events still visible (exercises the O.7.2 fix)
- `WizardE2EHarness` drives the wizard's internal API (no GUI events).

## Test posture

- libkalburator: 103/103
- PlanStan: 105/129 (24 fail = pre-existing noise per FINDINGS — tst_inboxmanager + sync_workflow_caldav + 22 env-gated Not-Run)
- WildPalms: 77/77
- `verify-all.sh` exits 0

## What remains (out of Phase P scope — see 04w-deferred-work.md)

- **Phase Q** — full session refactor (remove `BackendRegistry::instance()` singleton)
- **Signal storm** (5–8 `calendarDiscovered` per cal during initial render) — Phase R perf pass
- **SyncTransaction integration** (`PlanStan/docs/bugs/sync-transaction-integration.md`)
- **`useQuickSyncForFirstSync`** inert setting (either delete or wire up)
- **First-class libkalburator promotion** ("Phase 3b" per libkalburator/CLAUDE.md) — FetchContent cutover after the refactor branch merges to master/main

## Manual smoke checklist (user-driven, post-tag)

See design doc §"Manual smoke checklist". Must pass before the cross-repo merges to master/main. The Radicale dev server at `localhost:5232` (per PlanStan/CLAUDE.md) is the recommended target.
