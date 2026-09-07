# Phase Ic — WildPalms accounts UX — status

**Status:** ✅ landed 2026-05-09 (tag `v0.29-phase-ic-wildpalms-contacts-ux`).
**Spec:** `~/dev/refactor-engine-merger/2026-05-09-phase-ic-wildpalms-accounts-ux-design.md`
**Plan:** `~/dev/refactor-engine-merger/2026-05-09-phase-ic-wildpalms-accounts-ux-plan.md`

## What landed

- `WildPalms::Runtime::AccountController` (profile-scoped sibling to PalmRuntime).
- `<syncFolderPath>/.wildpalms.providers` sidecar persistence (matches PlanStan H.5 shape).
- SettingsDialog gains an "Accounts" KPageWidget item with provider list,
  per-provider createConfigWidget, Add/Remove buttons.
- `AddAccountDialog` (kind picker + provider config widget).
- `MappingPromptDialog` (post-add convenience accelerator; CardDAV gets Palm-slot picker,
  CalDAV shows "Bound (Phase J wires this)").
- `MappingRowDialog` target-combo extension: replaced hardcoded `"rawfiles-cal"` with a
  real backend picker. Closes data-loss bug from design §4.5a.
- `MappingEditorDialog` seeds both source and target combos from BackendRegistry via
  `setKnownBackends()`.

## Test posture

- libkalburator: 73/75 unchanged (2 pre-existing load-flaky).
- PlanStan: 82/106 unchanged.
- WildPalms: 80/80 (78 + 2 new executables: `tst_account_controller`, `tst_accounts_page`).
  Sub-test growth: +10 in `tst_account_controller`, +3 in `tst_accounts_page`,
  +4 in `tst_mapping_row_dialog`, +1 QSKIP in `tst_mapping_editor_dialog`.

## Closed deferred-work items

- §D.2 — WildPalms accounts settings dialog
- §D.3 — ProviderManager wiring in PalmRuntime (AccountController owns it)
- §D.4 — Default-mapping logic (always-prompt via MappingPromptDialog)

## Notable implementation detail

`CardDavProvider::createConfigWidget` returns `nullptr` (Phase Ib placeholder).
`AccountsPage::refreshList` guards against this by substituting an empty QWidget —
without the guard, `QStackedWidget::addWidget(nullptr)` crashes. Future sessions that
implement CardDAV config widget should remove this guard.

## What's next

Phase J — WildPalms migrates other domains (calendar/memo/todo) to providers.
Phase Ic delivered a working MappingEditor, so Phase J's UX delta is minimal:
activate the CalDAV row's slot picker in MappingPromptDialog and add per-domain
BlobBackendAdapter wiring. Real-device verification gate (§E.1) still pending.
