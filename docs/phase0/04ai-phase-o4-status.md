Status: landed 2026-05-18

Phase O.4 — Legacy cleanup

Pure deletion phase. Removes superseded widgets, dialogs, methods, and
deprecated APIs.

What landed:
- Deleted LogicalCalendarWidget (deprecated, no callers)
- Removed BackendsSettingsPage + CalendarsSettingsPage from
  CollectionSettingsDialog + CollectionSettingsViewPanel
- Deleted BackendsSettingsPage + CalendarsSettingsPage files
- Deleted SyncTopologyDialog (superseded by SyncTopologyViewPanel)
- Rewired File → New Collection: prompt for kalb path + name → create
  empty collection → switch to 'collection' layout (which shows
  sync_topology view per O.3.10)
- Deleted NewCollectionWizard + AdditionalBackendsPage
- Deleted CalendarListWidget (both consumers removed)
- MainWindow cleanup: removed m_actionAddAccount, account_add KXmlGui
  binding, onAddAccountTriggered slot, provisionProvidersAndRewriteBindings;
  planstanui.rc.in <gui version> 29 → 30
- Deleted CollectionController::provisionCalDavProvider (last caller gone)
- Deleted libkalburator ProviderConfigDialog hardcoded-kinds ctor

Carveouts vs. design §7:
- ProviderManager::providerConnectionStateChanged(QString, bool) was NOT
  deleted. WildPalms still uses it; per design §3 WildPalms stays untouched
  through O.4. Migration + deletion deferred to a follow-up phase.
- Manual smoke checklist (§7.1) was NOT performed — agentic execution has
  no UI driver. Verify-all + per-deletion gating served as the substitute.
  User should walk through §7.1 before declaring O.4 fully shipped.
- Full WizardChromeOverlay integration in SyncTopologyViewPanel was NOT
  delivered. The new-collection rewire (O.4.5) uses a minimal path-prompt
  + layout switch. Full wizard-chrome mode is a follow-up.

Tag: v0.48-phase-o4-legacy-cleanup on libkalburator HEAD
Phase O complete — G1, G2, G3, G4 (mostly), G5, G6, G7, G8, G9, G10 closed.
G8's deprecated boolean signal preserved; full G8 closure depends on
WildPalms migration follow-up.
