# Phase M — Multi-protocol DAV provider + UI lift

**Status:** Landed 2026-05-16  
**Tag:** `v0.42-phase-m-multiprotocol-dav` (pending user `git tag` authorization)  
**Plan:** `~/dev/refactor-engine-merger/2026-05-16-phase-m-multi-protocol-dav-plan.md`  
**Design:** `~/dev/refactor-engine-merger/2026-05-16-phase-m-multi-protocol-dav-design.md`

## What landed

### Library (libkalburator, 99 tests passing)

- **M.1:** `IProvider` optional accessors — `lastWarning()` + `icon()` default virtuals.
- **M.2–M.5:** `MultiProtocolDavProvider` — full `IProvider` implementation.
  Parallel CalDAV + CardDAV capability discovery via `QFuture` composition.
  `collections()` returns union with domain-tagged ids.
  `createBackend()` dispatches by `"caldav:"` / `"carddav:"` prefix on collection id.
  `load()` / `save()` round-trip via `BackendConfiguration::connectionParams`.
- **M.6:** `MultiProtocolDavConfigWidget` — form widget with collapsed Advanced
  section (using `QGroupBox::setCheckable(true)` + explicit `setVisible`; see F-M3).
- **M.7:** `MultiProtocolDavBackendContribution` + `MultiProtocolDavProviderPlugin`.
- **M.8:** Registered in `stock_plugins.cpp`.
- **M.9:** `CollectionPickerWidget` — new `src/ui/` directory, `Kalburator::Ui`
  namespace. Lists available collections from a provider with checkbox selection.
- **M.10:** `AccountsListWidget` — displays configured accounts with add/remove
  actions; delegates to `ProviderConfigDialog` for account creation/editing.
- **M.11:** `ProviderConfigDialog` — library-level dialog for provider config
  (provider selection combo + dynamic config widget area). Provider creation in
  `rebuildProviderWidget()` is **stubbed** (deferred to M.5 — see below).

### WildPalms (77 tests passing)

- **M.12:** `test_addaccountdialog_baseline.cpp` — pins `AddAccountDialog` behavior
  before migration; baseline test to catch regressions.
- **M.13:** Added "Multi-protocol DAV (calendar + contacts)" label in
  `AddAccountDialog` provider selection.
- **M.14:** `AccountsPage` migrated to embed `AccountsListWidget` from library.
  Safe to migrate because `AccountsPage` was defined but not yet instantiated
  by any caller (see F-M6).
- **Incidental:** Fixed `.clangd` to point at `build-dev/` (was stale `build/`,
  causing false-positive clangd errors for Phase M's new `src/ui/` include path;
  see F-M7).

### PlanStan — gated to M.5

PlanStan's CalDAV add flow is wizard-based (`provisionCalDavProvider` via
`AdditionalBackendsPage`), not dialog-based. The plan's Tasks M.15/16 assumed a
`CalDavAddDialog` that does not exist in PlanStan. The plan's gate condition was
triggered; both tasks were deferred. See F-M5.

## Key findings from Phase M

- **F-M1:** `CalDavCapabilityDiscovery` and `CardDavCapabilityDiscovery` have
  **different APIs**. CalDav uses signal-based `finished(bool)` with constructor
  args. CardDav uses `setCredentials()` + `discover() → QFuture<>`. The plan
  incorrectly assumed both had the same API.
- **F-M2:** `BackendConfiguration` field is `.type` **NOT** `.kind`. Plan contained
  `.kind` references that were bugs.
- **F-M3:** Qt `QGroupBox::setCheckable(true)` only disables children — does NOT
  hide them. Must explicitly call `setVisible(false)` + connect `toggled(bool)`.
- **F-M4:** `ProviderConfigDialog::rebuildProviderWidget()` provider creation is
  stubbed. Both consumers receive `ProviderConfigDialog` via M.11 but provider
  wiring is M.5 work.
- **F-M5:** PlanStan has no `CalDavAddDialog` — CalDAV provisioning is wizard-based
  (`provisionCalDavProvider`). The plan's Tasks M.15/16 assumption was wrong;
  migration deferred to M.5.
- **F-M6:** WildPalms `AccountsPage` was not wired into any caller yet (defined but
  not instantiated). Migration was safe with no runtime risk.
- **F-M7:** WildPalms `.clangd` pointed at stale `build/` dir — updated to
  `build-dev/` to pick up Phase M's `src/ui/` include path.

## M.5 follow-up — landed 2026-05-17

- `ProviderManager::backendRegistry()` accessor.
- `ProviderConfigDialog::rebuildProviderWidget()` wired via
  `BackendRegistry::contributionFor(kind)->createProvider(nullptr)`.
- `ProviderConfigDialog::takeProvider()` exposes the constructed
  IProvider to callers (move semantics).
- PlanStan `MainWindow` "Add Account…" File-menu action launches
  `ProviderConfigDialog` and feeds the result to a new generic
  `CollectionController::provisionProvider()`. Wizard
  `AdditionalBackendsPage` + `provisionCalDavProvider` left untouched.
- Tag: `v0.43-phase-m5-runtime-add-account`.

## Deferred work status

- **B.5** (multi-protocol DAV provider): ✅ CLOSED — `MultiProtocolDavProvider`
  shipped; both consumers receive it via `stock_plugins`.
- **D.1** (PlanStan CalDAV dialog → library dialog): ✅ CLOSED —
  runtime "Add Account…" entry point in PlanStan MainWindow opens
  `ProviderConfigDialog`. Wizard cards untouched (deliberate
  non-goal; a future cleanup may dedupe against IProvider config
  widgets). WildPalms keeps its own AddAccountDialog (not migrated).
