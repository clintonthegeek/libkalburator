Status: landed 2026-05-18

Phase O.2 — PlanStan provider lifecycle

Implements provider lifecycle APIs in PlanStan's CollectionController
and closes G1 (Add Account → Save drops selection) and G2 (no Settings page
for accounts). libkalburator untouched.

What landed:
- CascadePolicy enum (Strict / DropBindings / DropBindingsAndOrphans)
- CollectionController::listProviders() / updateProvider() / removeProvider()
- CollectionController::addLogicalCalendarsFromCollections()
- MainWindow::onAddAccountTriggered() switched to registry-aware
  ProviderConfigDialog, calls addLogicalCalendarsFromCollections (G1 closed)
- AccountsSettingsPage registered in SettingsDialog between Behaviours and Sync (G2 closed)
- PlanStan tests: 83/107 → 87/111

Tag: v0.46-phase-o2-planstan-provider-lifecycle on libkalburator
Next: O.3 — PlanStan topology canvas overhaul (§6 of phase-o design doc)
