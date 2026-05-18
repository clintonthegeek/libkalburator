# Phase O.1 — libkalburator UI foundations

**Status:** landed 2026-05-18
**Tag:** `v0.45-phase-o1-libkalburator-ui-foundations`
**Design:** `~/dev/refactor-engine-merger/2026-05-17-phase-o-unified-provider-backend-ux-design.md` §4
**Plan:** `~/dev/refactor-engine-merger/2026-05-17-phase-o1-libkalburator-ui-foundations-plan.md`
**Closes:** G7 (provider kinds hardcoded), G8 part 1 (per-provider state introspection), G10 part 1 (capability metadata in CollectionInfo).

## What landed

- O.1.1 — `BackendRegistry::contributionRegistered/Unregistered` signals.
- O.1.2 — `ProviderManager::providerState(id)` + `providerStateChanged(id, ProviderConnectionState)`. Old `providerConnectionStateChanged(id, bool)` preserved for deprecation overlap; removed in Phase O.4.
- O.1.3 — `IProvider::applyConfig(BackendConfiguration)` default impl (disconnect → load → reconnect when was-connected).
- O.1.4 — `BackendContribution::displayName()` pure virtual. `ProviderConfigDialog` registry-aware constructor; live updates from O.1.1 signals. Hardcoded-kinds ctor preserved for one release.
- O.1.5 — `CollectionInfo` gains `readOnly`, `contentTypes`, `estimatedSizeBytes` fields; `CollectionPickerWidget` renders capability chips (content-type labels + read-only chip + disabled checkbox).

## Test posture

- libkalburator: 103/103 (was 101/101; +2 ctest entries, +11 test slots total).
- PlanStan + WildPalms: baselines unchanged.

## What remains in Phase O

O.2 — PlanStan provider lifecycle + Accounts page + G1 closure.
O.3 — Topology canvas v2.
O.4 — Legacy cleanup + deprecation removals (providerConnectionStateChanged bool overload, hardcoded-kinds ProviderConfigDialog ctor).
