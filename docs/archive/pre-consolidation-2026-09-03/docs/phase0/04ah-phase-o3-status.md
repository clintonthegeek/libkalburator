Status: landed 2026-05-18

Phase O.3 — Topology Canvas V2

Promotes the SyncTopologyWidget into a main view with new node types
(ProviderNode + LocalBackendNode + LogicalCalendarsBlock), a backend
palette, wizard-chrome overlay, GraphValidationResult validator,
TopologyChangeset node coverage, and ISyncTopologyDataSource provider
CRUD. libkalburator untouched.

What landed:
- ISyncTopologyDataSource provider CRUD + KalbSyncTopologyDataSource impl
- TopologyChangeset gained provider/local-backend tracking
- SyncTopologyValidator gained GraphValidationResult with errors+warnings
- ProviderNode + LocalBackendNode (PortNode-style with state chip + chips)
- LogicalCalendarsBlock (central multi-row composite)
- SyncTopologyWidget atomic apply across provider+backend+mapping changes
- BackendPaletteWidget left-dock palette tracking BackendRegistry
- "Show backends" toolbar toggle (stub — nested rendering deferred)
- WizardChromeOverlay (sidebar checklist + Finish gated by validator)
- SyncTopologyViewPanel registered as 'sync_topology' main view
- PlanStan tests: 87/111 → 94/118

Deferred (followups):
- Nested-rendering when show-backends is ON (Task 8 stub only)
- Right-click context menu (O.4 candidate)
- NewCollectionWizard rewire to topology view in wizard-chrome mode (O.4)
- Unconfigured-node inspector with embedded config widget (O.4)

Tag: v0.47-phase-o3-topology-canvas-v2 on libkalburator HEAD
Next: O.4 — legacy cleanup
