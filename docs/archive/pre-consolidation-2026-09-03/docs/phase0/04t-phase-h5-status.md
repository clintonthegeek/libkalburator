# Phase H.5 — Status

**Tag:** `v0.25-phase-h5-planstan-providers` (landed 2026-05-07)

**Status:** ✅ Complete.

## Summary

Phase H.5 migrates PlanStan off the `davUrl`-on-bindings persistence
pattern and onto Phase H's `ProviderManager`-registered backends.
This validates the `IProvider` abstraction against PlanStan as the
first real consumer and closes Phase H Task 9 by deleting the two
`davUrl`-construction blocks in `libkalburator/src/calendar/
calendarmanager.cpp` (lines `:110-128` and `:422-440`) that PlanStan
was previously consuming.

β plumbing-only scope: PlanStan's wizard UX is unchanged; only the
finish-time persistence rewires to provision providers and write
composite-id bindings. No Accounts page (deferred to Phase J's
WildPalms work or PlanStan's planned wizard rewrite). No CardDAV
(Phase I). No "create-new-calendar-via-wizard for CalDAV" UX (the
legacy `BDC :298` `needsCreation` block stays as deferred-creation
legacy code; new wizards route through provider-enumerated
collections). Hard-break migration: existing dev-profile bindings
became unresolvable post-upgrade, the user re-adds via the wizard
(no migration code, single-developer user base).

## Tasks

| # | Task | Commit | Status |
|---|---|---|---|
| 1 | Pre-flight audit (read-only) | (audit doc only) | ✅ |
| 2 | Confirm KF6Config + headers reachable | `88019ce6` (PlanStan, no-op) | ✅ |
| 3 | ProviderManager scaffold in CollectionController | `5cd7abbf` (PlanStan) | ✅ |
| 4 | provisionCalDavProvider helper | `4f729e5c` (PlanStan, bundled) | ✅ |
| 5 | Startup loadFromProfile + connectAll wiring | `4f729e5c` (PlanStan, bundled) | ✅ |
| 6 | m_backends mirror loop | `4f729e5c` (PlanStan, bundled) | ✅ |
| 7 | NewCollectionWizard finish-time rewrite | `bab97316` (PlanStan) | ✅ |
| 7b | Sweep widget setDavUrl calls (~17 sites) | `99b2b957` (PlanStan) | ✅ |
| 8 | CalendarCreationWizard finish-time rewrite | `8b6baba2` (PlanStan) | ✅ |
| 9 | Delete BDC `registerCalendarUrlsFromBindings` | `2986e2da` (PlanStan) | ✅ |
| 9b | Delete second davUrl-replay in CC | `1bf782a3` (PlanStan) | ✅ |
| 9c | Simplify BDC remaining davUrl reads | `9e077fb9` (PlanStan) | ✅ |
| 10 | libkalburator: delete davUrl construction blocks | `31ad3db` (libkalburator) | ✅ |
| 11 | Verify no test regressions | `9f0cb811` (PlanStan, empty) | ✅ |
| 12 | Provider-roundtrip integration test | `91dfe192` (PlanStan) | ✅ |
| 12b | Fix: re-evaluate multi-backend gate post-mirror | `02808ea8` (PlanStan) | ✅ |
| 13 | verify-all + tag | (annotated tag, no commit) | ✅ |
| 14 | Update persistence docs | (this doc + coord-folder) | ✅ |

## Test posture (post-Phase-H.5)

- **libkalburator:** 58/58 pass (unchanged from Phase H baseline)
- **PlanStan:** 90/114 pass (24 baseline failures unchanged; the
  one new test is a Qt-slot inside the existing
  `tst_collectioncontroller` executable, so the ctest count stays
  114; Qt-slot count went 18 → 19)
- **WildPalms:** 75/75 pass (unchanged)
- **`verify-all.sh`:** exit 0 — all green, no flips

## What landed (the surface)

### libkalburator

- `src/calendar/calendarmanager.cpp` — two `davUrl`-construction
  blocks deleted (~42 LOC); `<QUrl>` and `remotebackend.h` includes
  dropped along with them.

### PlanStan

- `src/controllers/collectioncontroller.{h,cpp}` —
  `m_providerManager` member; new public surface
  `provisionCalDavProvider(url, user, pass) → uuid`,
  `providerManager()` accessor,
  `setKalbFilePathForProvisioning(path)` for transient-CC pattern,
  `rewriteWizardBindingsToProviderComposite(QList<...>&)` for the
  CalendarCreationWizard finish-time rewrite,
  `mirrorProviderBackends()` (private), and
  `maybeInitSyncInfrastructure()` (private, idempotent gate).
- `src/controllers/backenddiscoverycoordinator.{h,cpp}` —
  `registerCalendarUrlsFromBindings()` deleted;
  `onCalendarDiscovered` rename-detection block deleted;
  `detectMissingCalendars` URL fallback deleted; `:298`
  `needsCreation`-write-path stays as legacy.
- `src/app/mainwindow.{h,cpp}` —
  `provisionProvidersAndRewriteBindings(...)` helper; called from
  both `onActionNewCollection` and `onActionDebugNewCollection`
  finish paths.
- `src/dialogs/calendarcreationwizard.cpp` — two `setDavUrl(...)`
  call sites deleted.
- `src/dialogs/newcollectionwizard.{h,cpp}` — per-slot CalDAV
  config accessors exposed.
- `src/widgets/calendarlistwidget.{h,cpp}` — 6 `setDavUrl(...)`
  sites deleted; `setCollectionController()` setter; finish-time
  rewrite hook in `onAddCalendarClicked`.
- `src/widgets/logicalcalendarwidget.cpp` — 8 `setDavUrl(...)`
  sites deleted (audit said 8; sweep found a 9th at `:836`).
- `src/widgets/backendbindingrow.{h,cpp}` — 2 `setDavUrl(...)`
  sites deleted; `selectedDavUrl()` accessor deleted (zero
  callers).
- `src/views/dialogcoordinator.cpp` — wired finish-time rewrite
  into `onOrphanCalendarDialogResult` (adoption-mode wizard).
- `src/widgets/collectionexplorer.{h,cpp}` — wired into
  `onContextMenuRequested` (creation-from-tree wizard); takes a
  `CollectionController*` reference.
- `src/dialogs/settings/calendarssettingspage.{h,cpp}` /
  `src/views/collectionsettingsviewpanel.cpp` /
  `src/dialogs/collectionsettingsdialog.cpp` — plumbing for
  `CollectionController` to reach `CalendarListWidget` from the
  settings page.
- `tests/controllers/tst_collectioncontroller.cpp` — three new
  test slots: `testProvisionCalDavProvider` (Task 4),
  `testProviderConnectAtStartup` (Tasks 5+6), and
  `testProviderRoundtripFullSync` (Task 12, the capstone). The
  roundtrip test exercises the realistic one-local-plus-provider
  config (post-Task-12b fix).

### Architectural choices locked in

1. **Finish-time slot→composite-id rewrite.** Widgets keep their
   slot-id binding-write logic; the wizard caller seam walks
   bindings post-finish and rewrites slot ids to
   `<provider-uuid>:<calendarId>` in one pass. Avoids threading
   provider awareness through 16+ widget sites.
2. **Transient-CC pattern for sidecar provisioning.**
   `NewCollectionWizard` runs before the app's main CC for the new
   kalb-doc exists; a minimal transient CC writes the sidecar
   `<kalbFilePath>.providers` KConfig file via
   `setKalbFilePathForProvisioning(path)`, then is discarded. Main
   CC at load reads the sidecar via the standard startup path.
3. **Sidecar `<kalbFilePath>.providers` KConfig file.** PlanStan's
   `KalbConfigManager` is JSON-only, but
   `ProviderManager::loadFromProfile` requires a `KConfigGroup`.
   Sidecar file is the pragmatic bridge for β scope; multi-file
   kalb-doc representation is an architectural wart to reconcile
   in the wizard rewrite or follow-up unification pass.
4. **Idempotent multi-backend infra gate.**
   `maybeInitSyncInfrastructure()` extracted into a helper called
   from both `startDiscoveryAndSync()` (sync startup) AND the tail
   of `mirrorProviderBackends()` (post-async provider arrival),
   ensuring a one-local-plus-provider config correctly initializes
   syncCoordinator after the async mirror completes.
5. **Composite-id-only bindings.** No slot indirection layer.
   Bindings store `backendId = "<provider-uuid>:<calendarId>"` and
   `metadata.davUrl` is unset.
6. **Hard-break migration.** Existing dev-profile bindings with
   embedded `davUrl` strings become unresolvable post-upgrade; the
   user re-adds via the wizard. No migration code, no
   compatibility shim.

## What did NOT land (and why)

### `IProvider::createCollection()` / "create-via-wizard" UX

PlanStan's pre-H.5 wizards supported "type a new calendar name and
we'll auto-create it on the CalDAV server" via BDC's
`processDeferredCalendarCreations` and a davUrl-stamp on the
binding. Phase H.5 disables this UX for CalDAV: the new wizard
finish-paths route only through `provider->collections()`
(already-enumerated existing calendars). Adding
`IProvider::createCollection()` is a meaningful new abstraction
extension that pressure-tests `IProvider` in a different way than
CardDAV/Akonadi (Phase I). Belongs in Phase I or a dedicated
mini-phase, not bolted into H.5. The `BDC :298` legacy block stays
for old-data resilience.

### Eager-vs-lazy backend creation in ProviderManager

`ProviderManager::registerProviderBackends` eagerly calls
`provider->createBackend(collectionId)` for every collection on
every connect. For CalDAV accounts with many calendars, this is
multi-second work and pure waste for the K << N usage pattern.
Out-of-scope perf hazard; FINDINGS-tracked.

### `KalbConfigManager` JSON↔KConfig unification

The sidecar pattern is pragmatic but not pretty. Reconciling the
two persistence formats belongs in the wizard rewrite or a
dedicated unified-config pass.

### Legacy `findLogicalCalendarByBindingUrl` helpers

`KalbConfigManager::findLogicalCalendarByBindingUrl` and
`CollectionSettings::findLogicalCalendarByBindingUrl` became
unused when Task 9c deleted the BDC rename-detection block. Left
for a follow-up cleanup pass.

### `CalendarBackendBinding::davUrl()` / `setDavUrl()` accessors

Marked `@deprecated` in libkalburator's `logicalcalendar.h`. Zero
remaining callers in production code. Deleting them touches both
consumers' test fixtures across libkalburator and PlanStan;
deferred to a future cleanup pass.

## Next phase — Phase I (CardDAV + Akonadi)

Phase H.5's validation of `IProvider` against PlanStan as a real
consumer was successful. Phase I now adds the second protocol
(CardDAV inside `CalDavProvider`) and the second provider class
(`AkonadiProvider`), pressure-testing the abstraction along
different axes:

- CardDAV: same provider class, second collection type
  (contacts), exercises multi-collection-type fan-out from one
  account.
- Akonadi: separate provider class, exercises the abstraction's
  "swap-in another implementation" extension axis.

Together they declare the IProvider abstraction stable enough for
Phase J (WildPalms migration to providers).

## References

- **Design:** `~/dev/refactor-engine-merger/2026-05-07-phase-h5-planstan-providers-design.md`
- **Plan:** `~/dev/refactor-engine-merger/2026-05-07-phase-h5-planstan-providers-plan.md`
- **Audit:** `~/dev/refactor-engine-merger/2026-05-07-phase-h5-task1-audit.md`
- **FINDINGS:** `~/dev/refactor-engine-merger/FINDINGS.md` (Phase H.5
  entries appended 2026-05-07; ~9 entries total, including the
  audit-scope-deeper-than-design surprise, the finish-time rewrite
  pattern, the JSON↔KConfig sidecar bridge, the QObject
  insertion-order delete trap, the async-mirror gate fix, the slot-id
  asymmetry, the eager-backend-creation perf hazard, the legacy
  `BDC :298` block, and the transient-CC pattern).
- **CURRENT-STATUS:** `~/dev/refactor-engine-merger/CURRENT-STATUS.md`
- **ROADMAP:** `~/dev/refactor-engine-merger/ROADMAP.md`
- **Phase H predecessor:** `04s-phase-h-status.md`
