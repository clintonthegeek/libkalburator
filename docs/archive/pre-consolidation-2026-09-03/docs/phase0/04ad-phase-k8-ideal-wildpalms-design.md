# Phase K.8 — Ideal-WildPalms Design

**Status:** spec drafted 2026-05-14; awaiting user ratification before plan writing.

**Tags:** `v0.38-phase-k8a-reference` (K.8a), `v0.36-phase-k8-wildpalms-rewrite` (K.8b), `v0.40-phase-k-engine-generalized` (closing).

**Supersedes:** the deleted `04ac-phase-k7-ideal-wildpalms-design.md` placeholder (commit "K.7: Remove ideal WildPalms design — redoing fresh"). Phase K.7 in `04ab-phase-k-engine-generalization-design.md` lines 702–728 sketched the ideal architecture in passing; this document is the canonical version.

**Audit inputs:** `04ab-phase-k-audits/2026-05-09-audit-wildpalms-integrity.md` (the 10 findings). All findings are addressed by tasks in either K.8a or K.8b below.

---

## 1. Architectural goal

WildPalms-the-application becomes a coherent host for libkalburator: exactly one sync engine, exactly one plugin contract, exactly one persistence file. The application's job is device session management, Palm-specific data access (`PalmDeviceAccess`), UI, and orchestration. Everything else — sync, conflict resolution, transformation, baseline storage, provider abstraction — belongs to libkalburator and reaches WildPalms through `Kalburator::Plugin` contributions and `SyncEngine` API.

The K.7 plugin extensibility surface (contributions decomposed into `DomainDefinition` + `ShapeContribution` + `DomainOperations` + `BackendContribution`, loaded by `Kalburator::PluginManager`) is the lever. WildPalms's five existing plugins (calendar, contacts, memo, todo, webcalendar) become `Kalburator::Plugin` instances, registered in-process during `PalmRuntime` initialization using the same closure-capture pattern as `NeutralProvider` and `registerStockPlugins()`. WildPalms's parallel scaffolding (native `Sync::SyncEngine`, `IConduit`, `IBackendPlugin` V1/V2, `BackendPluginManager`, `ConduitManager`, `BlobBackendAdapter`, `CalendarCollection_WP`, `.wildpalms.providers` sidecar) is deleted.

## 2. Component disposition

### Delete (from WildPalms)

- `src/core/ibackendplugin.h` (V1) and `ibackendplugin_v2.h` (V2)
- `src/runtime/backendpluginmanager.{h,cpp}` + all corresponding tests
- `src/core/iconduit.h` (if extant), `src/runtime/conduitmanager.{h,cpp}`
- `src/sync/syncengine.{h,cpp}` (native), `src/sync/syncconduitbase.{h,cpp}`, `src/sync/localfilebackend.{h,cpp}`, native `src/sync/interactiveconflicthandler.{h,cpp}`
- Dead conflict-handler installation path in `kf6mainwindow.cpp:540–810`
- `src/runtime/palmruntime.cpp:60–153` (`BlobBackendAdapter`) and the `dynamic_cast<SyncBackend*>` routing branch
- `src/runtime/calendarcollection_wp.{h,cpp}` and the eager-preload loop at `palmruntime.cpp:313–324`
- `tests/test_syncengine.cpp` (tests dead native engine), `tests/test_libkalburator_smoke.cpp` (library doesn't need a survival witness in the consumer)

### Keep (essentially unchanged)

- `src/profile.{h,cpp}` (schema extended; base preserved)
- `src/runtime/palmruntime.{h,cpp}` shell (PalmDeviceAccess facade + library SyncEngine wrapper)
- `src/palm/*/Palm{Calendar,Contacts,Memo,Todo}Backend.{h,cpp}` — sound per-domain encapsulations
- `src/app/conflict/conflictdialogbridge.{h,cpp}` (clean V2 bridge)
- `src/app/accounts/*` UI shell (rewired to render generically off `ProviderManager`)
- Phase Ic dialogs (`MappingEditorDialog`, `MappingRowDialog`, `MappingPromptDialog`) — solid work per audit

### Rewrite

- `src/plugins/{calendar,contacts,memo,todo,webcalendar}/*` — replace `IBackendPluginV2` implementations with `Kalburator::Plugin` subclasses exposing `BackendContribution`s. CMake target type flips from `MODULE` (.so) to `STATIC` lib linked into the WildPalms binary. Drop `Q_PLUGIN_METADATA` macros and `.json` manifests.
- `kf6mainwindow.{h,cpp}` — drop `m_syncEngine` member, `initializeSyncEngine()`, and the V1 plugin-discovery loop in `initializeConduits()`; route all sync UI actions through `m_palmRuntime`.
- `settingsdialog.{h,cpp}` — drop dual-engine knobs; remap profile-scoped settings to `PalmRuntime::engine()` directly; Accounts page becomes a generic `ProviderManager` view.
- `palmruntime.{h,cpp}` — drop cached `m_link` member + `setLinkForTest` seam; add `registerPalmPlugins()` (in-process plugin registration); add `cancelSync()` slot.
- `palmdeviceaccess.{h,cpp}` — add `pauseTickle()` / `resumeTickle()` (auto-marshalling); these become the only tickle path.
- `accountcontroller.{h,cpp}` — reduce to a thin runtime view over `Profile`; delete sidecar I/O.

### New

- **Libkalburator-side (K.8a):**
  - > **Amendment 2026-05-15:** `ProviderContribution` was NOT introduced. K.8a execution
    > determined it would be redundant with `BackendContribution` (see FINDINGS 2026-05-14).
    > The items below that reference `ProviderContribution` reflect pre-execution intent;
    > read `BackendContribution` everywhere `ProviderContribution` appears.
  - ~~`src/sync/providercontribution.h`~~ — not created; `BackendContribution` serves this role.
  - Stock provider plugins `Kalburator.Provider.CalDav` and `Kalburator.Provider.CardDav` migrating the existing `CalDavProvider` / `CardDavProvider` via `BackendContribution`. Registered through `registerStockPlugins()`.
  - `ProviderManager` enumerates `BackendContribution`s from `BackendRegistry` instead of being hand-coded.
  - `examples/reference_consumer/` — small executable that uses `PluginManager` to perform cross-domain (Calendar + Contacts) sync end-to-end between two stock-plugin backends. Backed by a `tst_reference_consumer_smoke` ctest target.
- **WildPalms-side (K.8b):**
  - `Profile` schema extension: `accounts/<uuid>/{kind,displayName,config/...}` keys.
  - One-shot migration from `.wildpalms.providers` → `Profile`'s accounts subgroup on first load post-K.8b; sidecar renamed to `.wildpalms.providers.migrated.<ISO-8601>` as recovery anchor.

## 3. Plugin-registration model

### Precedent

`BackendContribution::createProvider(QObject *parent)` (libkalburator/src/sync/backendcontribution.h:20) has no host-context channel — it takes only a parent. The K.7 model for backends needing application state is `NeutralProvider`'s **closure capture**: a `BackendFactory = std::function<std::unique_ptr<IBlobBackend>()>` lambda is passed at provider construction time, capturing whatever the caller has in scope. Stock plugins use **in-process registration** via `registerStockPlugins()`, not `.so` discovery via `PluginManager::discover()` (which exists but is reserved for genuinely external plugins).

### Application to WildPalms

Palm plugins register in-process during `PalmRuntime` initialization. Each Palm plugin is constructed by `PalmRuntime::registerPalmPlugins(PluginManager&)` (or a `registerPalmPlugins()` free function called from the ctor) with `PalmDeviceAccess*` and `Profile*` captured in factory closures inside each `BackendContribution`. Multi-profile scoping is automatic: each `PalmRuntime` instance is profile-scoped and registers its own plugin instances; teardown drops them.

This means:

- **No `PalmHostContext` struct, no `PluginHost` interface, no `setHost()` method on `Kalburator::Plugin`** — the K.7 surface stays unchanged.
- **The `wildpalms_*_v2.so` shared-library plugin targets are dissolved** into static libs linked into the WildPalms binary.
- **`BackendPluginManager` (V1 and V2) and `${WP_PLUGIN_DIR}` discovery are deleted entirely.**
- **The `.so` plugin path remains available** in libkalburator for genuinely third-party additions (future Akonadi, JMAP, etc.).

### Plugin shape per domain

> **Amendment 2026-05-15 (post-K.8b execution):** Palm plugins do NOT use
> `BackendContribution::createProvider()` or `NeutralProvider`. Instead, each
> plugin exposes a non-virtual `createPalmBackend(PalmDeviceAccess*) → SyncBackend`
> accessor that `PalmRuntime` calls directly after `loadInProcess()`. Rationale:
> the K.7 `BackendContribution` surface is cloud-provider-shaped (CalDAV/CardDAV);
> Palm backends are per-session, device-bound, and don't have a "connect to cloud"
> model. The K.7 surface owns plugin lifecycle; the direct accessor owns device binding.

For each of `calendar`, `contacts`, `memo`, `todo`, `webcalendar`:

```cpp
// e.g., WildPalms/src/plugins/calendar/palmcalendarplugin.h
class PalmCalendarPlugin : public Kalburator::Plugin {
public:
    PalmCalendarPlugin(PalmDeviceAccess *device, Profile *profile);

    QList<std::shared_ptr<Sync::BackendContribution>>
        backendContributions() const override;
    // DomainDefinition + ShapeContributions for "calendar" come from the
    // stock kalburator.calendar plugin; Palm plugin does not redefine them.
};
```

~~Each `BackendContribution::createProvider()` returns a `NeutralProvider` (or per-domain provider) whose `BackendFactory` captures `device` + `profile`.~~ (Superseded by amendment above.)
`BackendContribution::backendType()` returns a stable identifier (`palm.calendar`, `palm.contacts`, …) so per-mapping configuration can target it.

## 4. Account / provider model

### Library-side change

The hand-coded `CalDavProvider` / `CardDavProvider` selection inside `ProviderManager` is replaced by enumeration of registered `ProviderContribution`s. Each provider becomes a stock plugin registered through `registerStockPlugins()`:

- `Kalburator.Provider.CalDav` — registers a `ProviderContribution` whose `createProvider()` constructs the existing `CalDavProvider`.
- `Kalburator.Provider.CardDav` — same for `CardDavProvider`.

Existing call sites that take `IProvider` references continue to work; the type hierarchy is unchanged. Only the construction path moves from hard-coded `switch` to contribution-driven lookup.

### WildPalms UI

`AccountsPage` (and `AddAccountDialog`) becomes generic:

- "Add account" combo populated from `ProviderManager::registeredProviderTypes()` (returns `QList<QString>` of `ProviderContribution::id()`s with display names).
- Per-provider editor: invoke `ProviderContribution::createConfigWidget(this)` to get a full editor widget; providers retain complete control of their UI.
- Account list: iterate `Profile::accounts()` (see Section 5) and render via `ProviderManager::providerById(id).displayName()`.

`AccountController` is reduced to:

```cpp
class AccountController : public QObject {
public:
    QList<BackendConfiguration> accounts() const { return m_profile->accounts(); }
    void addAccount(const BackendConfiguration &cfg);   // forwards to Profile
    void removeAccount(const QString &id);              // forwards to Profile
signals:
    void accountsChanged();
};
```

No sidecar I/O. No background `ProviderManager` mutation outside the Profile flow. Persistence is `Profile`'s job exclusively.

## 5. Persistence & migration

### Profile schema extension

`Profile`'s KConfig file gains an `accounts` subgroup:

```
[accounts]
ids=<comma-separated UUIDs>

[accounts/<uuid>]
kind=caldav|carddav|...
displayName=Work Nextcloud
config/url=https://...
config/user=...
config/passwordKey=<reference into existing credential store>
```

`Profile::accounts()` returns `QList<BackendConfiguration>`. `Profile::saveAccount(BackendConfiguration)` / `Profile::removeAccount(QString id)` mutate it. Credentials continue to use whatever store Phase Ic chose (KWallet placeholder / plaintext fallback per `04w-deferred-work.md` B.4); the schema references it rather than reimplementing it.

### Sidecar migration

On `Profile::load()`, if `<syncDir>/.wildpalms.providers` exists:

1. Parse each entry into a `BackendConfiguration`.
2. Write to `Profile`'s `accounts/<uuid>` keys, preserving UUIDs so existing `SyncMapping`s still reference the same account ids.
3. Rename the sidecar to `.wildpalms.providers.migrated.<ISO-8601 timestamp>`. Do not delete — recovery anchor.
4. Log one info-level message.

Forward-compat note: new WildPalms reads old sidecar-only profiles; old WildPalms cannot read new accounts-in-Profile profiles. Acceptable per the "WildPalms can be sacrificed" stance.

## 6. PalmRuntime, device facade, mid-sync cancel

### `PalmDeviceAccess` facade completion

```cpp
class PalmDeviceAccess : public QObject {
    // ... existing self-marshalling API ...
    Q_INVOKABLE void pauseTickle();
    Q_INVOKABLE void resumeTickle();
};
```

Both auto-marshal to `m_linkThread` (same pattern as the existing API) and forward to the held `KPilotLink`. With these, `PalmRuntime` no longer needs the cached `m_link` member or `setLinkForTest`. All sync paths (`hotSync`, `fullSync`, `mirror`, `backup`, `restore`) call `m_device->pauseTickle()` / `m_device->resumeTickle()`.

### Mid-sync cancel restoration

```cpp
class PalmRuntime : public QObject {
public:
    QFuture<PalmRunResult> hotSync();
    // ... and friends
public slots:
    void cancelSync();   // NEW
};
```

`PalmRuntime` holds a `QFutureWatcher<PalmRunResult>*` for the active sync. `cancelSync()` invokes `watcher->cancel()`, which propagates through `m_engine`'s `runSyncFuture` cancel channel (already present in libkalburator since F2). `KF6MainWindow::cancelConnectionAction` is renamed `cancelSyncAction` and wired to the new slot; the "mid-sync cancel currently unsupported" TODO comment is deleted.

### Teardown / memory-corruption guard

Pick exactly one ownership model for `m_currentProfile` — likely `std::unique_ptr<Profile> m_currentProfile` with explicit `reset()` in `loadProfile()` and `~KF6MainWindow`, no Qt parent. Delete the "memory corruption detected, skipping delete" guard at root cause. If the guard's trigger pattern can't be reproduced with the new ownership model within a reasonable investigation budget, downgrade to a `Q_ASSERT` rather than a silent leak, and open a follow-up ticket.

## 7. K.8a — Reference + Provider plugin contributions (libkalburator-side)

**Scope:** the prerequisites that K.8b needs and a falsifying witness that the K.7 surface is consumer-ready.

**Files (new):**
- `libkalburator/src/sync/providercontribution.h`
- `libkalburator/src/plugin/plugin.h` (extension: `providerContributions()` virtual)
- `libkalburator/src/plugins/provider_caldav/caldavproviderplugin.{h,cpp}`
- `libkalburator/src/plugins/provider_carddav/carddavproviderplugin.{h,cpp}`
- `libkalburator/examples/reference_consumer/main.cpp` + `CMakeLists.txt`
- `libkalburator/examples/reference_consumer/tst_reference_consumer_smoke.cpp` (or a `add_test` wrapper that runs the binary against a tmp workdir and asserts exit 0 + expected log lines)

**Files (modified):**
- `libkalburator/src/sync/providermanager.{h,cpp}` — enumerate `ProviderContribution`s; remove hard-coded provider switch
- `libkalburator/src/plugin/registerstockplugins.cpp` — register the two new provider plugins

**Gate:**
- `verify-all.sh` green.
- `tst_reference_consumer_smoke` passes.
- Existing `ProviderManager` consumers (PlanStan, WildPalms) build unchanged — the migration is internal to libkalburator.

**Tag:** `v0.38-phase-k8a-reference`.

## 8. K.8b — Full WildPalms rewrite (WildPalms-side)

**Scope:** executes the audit's deletion list and restores feature parity with pristine.

**Task groups:**

1. **Engine consolidation** — delete native `Sync::SyncEngine`, `SyncConduitBase`, `LocalFileBackend`, native `InteractiveConflictHandler`, `m_syncEngine` member, `initializeSyncEngine()`, dead conflict-handler installation paths.
2. **Plugin-contract consolidation** — delete `IBackendPlugin` V1+V2, `BackendPluginManager`, `IConduit`, `ConduitManager`.
3. **Plugin rewrites (×5)** — calendar, contacts, memo, todo, webcalendar each: change CMake `MODULE` → `STATIC`, replace `IBackendPluginV2` impl with `Kalburator::Plugin`, register via `PalmRuntime::registerPalmPlugins()`, capture `PalmDeviceAccess*`/`Profile*` in factory closures, delete `Q_PLUGIN_METADATA` and `.json` manifests.
4. **`BlobBackendAdapter` deletion** — drop adapter + `dynamic_cast<SyncBackend*>` routing.
5. **`CalendarCollection_WP` deletion** — drop class + eager-preload loop. *Confirm during execution that K.7's writer rewiring removed the `MemoryCalendar*` requirement; if not, library-side fix becomes K.8b task 0.*
6. **`PalmDeviceAccess` facade completion** — add `pauseTickle`/`resumeTickle`; drop `m_link` + `setLinkForTest`.
7. **Mid-sync cancel restoration** — `PalmRuntime::cancelSync()`, wire `QFutureWatcher::cancel` → engine cancel channel, rename UI action, delete TODO.
8. **Account persistence migration** — `Profile` schema extension, sidecar migration, `AccountController` reduction, `AccountsPage` generic rendering.
9. **Memory-corruption guard removal** — single ownership model, delete guard.
10. **Test culling** — delete `test_syncengine.cpp`, `test_libkalburator_smoke.cpp`; audit + simplify `tst_palm_runtime_*` where it tests library-survival rather than WildPalms behavior.

**Gate:**
- `verify-all.sh` green.
- Phase J Task 9 `tst_runtime_caldav_e2e` passes **both** directions (palm→caldav and caldav→palm).
- Phase J Task 10 (stress) and Task 11 (CardDAV E2E) pass.
- All settings-dialog knobs observably affect sync behavior (audit Finding 1 closed).
- Memory-corruption guard removed without regression.

**Tag:** `v0.36-phase-k8-wildpalms-rewrite`. Closing tag `v0.40-phase-k-engine-generalized` follows.

## 9. Testing strategy

**K.8a additions:**
- `tst_reference_consumer_smoke` — cross-domain end-to-end sync via `PluginManager`. Gates the K.7 surface from outside library internals.
- Small unit tests for `ProviderContribution` registration + `BackendRegistry` enumeration.

**K.8b expectations:**
- Per-task `verify-all.sh` between major task groups; baseline refresh at phase end.
- Phase J Tasks 9–11 become gating, not aspirational. Task 9's caldav→palm direction is the canonical witness that `CalendarCollection_WP` deletion is safe.
- `tst_main_window_plugin_pages_populated` adapted to the new in-process registration path.
- Library-survival tests deleted; their absence is the gate.

## 10. Risks & open issues

1. **Calendar writer null-guard fix may not actually be in place.** Section 8 task 5 assumes K.7's plugin rewire fixed `CalendarPluginWriter::apply()`'s `MemoryCalendar*` requirement. **Mitigation:** the K.8a reference consumer is a fast falsifier — its calendar test will fail if the writer still requires a registered calendar. If so, K.8b gains a task 0 (library-side writer fix) before `CalendarCollection_WP` can be deleted.

2. **`AccountsPage` generic rendering may push UX regressions.** Today the page is hand-coded against CalDAV/CardDAV-specific fields. **Mitigation:** the per-provider `createConfigWidget()` returns a full widget; providers retain complete UI control. Only listing/selection chrome becomes generic.

3. **In-process plugin registration removes WildPalms-side `.so` plugin-extensibility-by-end-user.** **Mitigation:** acceptable per audit Finding 3's analysis — WildPalms's plugin ABI was never a real external surface; it was library types in a WildPalms namespace. The libkalburator-level `.so` discovery path (used for genuinely external backends) remains available.

4. **Memory-corruption guard fix is speculative.** The audit hypothesizes a teardown-order bug; root cause needs investigation during K.8b task 9. **Mitigation:** if root cause isn't found within budget, downgrade guard to `Q_ASSERT` and open a follow-up ticket rather than silently leaking.

**Open issues (resolve during execution, not now):**
- Exact CMake idiom for dissolving plugin `MODULE` targets into `STATIC` libs.
- Whether `ProviderContribution` needs a `connectionParams` schema descriptor for generic UI, or whether `createConfigWidget()` is sufficient (likely sufficient).
