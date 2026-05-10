# WildPalms integrity audit — 2026-05-09

WildPalms entered the engine-merger refactor as a coherent Palm OS sync
application: a `DeviceSession`/`DeviceWorker`/`TickleWorker` async
runtime over `KPilotLink`, a `Sync::SyncEngine` orchestrating
per-data-type `Conduit` plugins, a profile-based config layer, and a
`SyncBackend` abstraction owned by the application. Pristine
`docs/ARCHITECTURE_2026.md` is unambiguous about that paradigm — the
2026-01-08 architecture document was written as if the application were
done.

It exited Phases D through J with **two complete sync stacks coexisting
inside the same KF6MainWindow object**, two plugin discovery loops
(one of them running over a now-empty plugin set), a settings dialog
that requires a runtime-coupled controller to construct, and a Phase J
test suite that fails because the calendar plugin's "native" interface
to the engine is a 47-line shim that lies about being calendar-shaped.

The merge worked at the level of "code compiles and 80/80 unit tests
pass." It has not worked at the level of WildPalms-the-application
remaining a coherent thing. The seams along which the library was
forced into the application are exactly the places where Phase J Task 9
is now stuck — and they are where future bugs and future stalled plans
will keep coming from until the seams are confronted, not papered over.

---

## Top findings, ranked

### Finding 1: Two SyncEngines coexist in KF6MainWindow; the native one is dead scaffolding kept alive for its property bag

**Severity:** critical

**Where:**
- `WildPalms/src/kf6/kf6mainwindow.h:34-37` (forward-declares `Sync::SyncEngine`)
- `WildPalms/src/kf6/kf6mainwindow.h:212` (`Sync::SyncEngine *m_syncEngine`)
- `WildPalms/src/kf6/kf6mainwindow.h:199` (`std::unique_ptr<…PalmRuntime> m_palmRuntime` — which itself owns a `Kalburator::Sync::SyncEngine`, see `palmruntime.h:159`)
- `WildPalms/src/kf6/kf6mainwindow.cpp:496-515` (`initializeSyncEngine()` — wires native engine signals to the log widget)
- `WildPalms/src/kf6/kf6mainwindow.cpp:1761-1782` (`onHotSync()` / `onFullSync()` — only touch `m_palmRuntime`, never `m_syncEngine`)

**What it is:** `KF6MainWindow` constructs two engines on every profile load. `m_syncEngine` is the native `Sync::SyncEngine` from `src/sync/syncengine.h` — registers conduits, holds the device link via `setDeviceLink()`, gets every conflict policy setting from the Profile (`m_syncEngine->setConflictAutoResolve(...)` etc., `kf6mainwindow.cpp:794-798`), holds a backend (`setBackend(new Sync::LocalFileBackend(...))`, line 828), receives conduit registrations from `ConduitManager` (line 581), holds enable/disable state per conduit (lines 831-848), and is *never asked to do any work*. Every sync action in the UI routes to `m_palmRuntime->hotSync()` / `fullSync()` / `copyPalmToPC()` etc. A grep of the entire WildPalms tree for `m_syncEngine->syncAll`, `m_syncEngine->sync(`, or any equivalent driver call returns zero hits.

```cpp
// kf6mainwindow.cpp:1761
void KF6MainWindow::onHotSync() {
    if (!m_palmRuntime || !m_palmRuntime->isDeviceConnected()) { ... }
    auto *watcher = new QFutureWatcher<…PalmRunResult>(this);
    …
    watcher->setFuture(m_palmRuntime->hotSync());   // ← all routes go here
}
```

Meanwhile every plugin and every profile setting still flows into the unused `m_syncEngine`. That engine is configured, has a backend, has a device link, has conflict policy, has registered conduits — all of which is also true (separately) for the `Kalburator::Sync::SyncEngine` inside `m_palmRuntime`, which is the one actually doing the work.

**Why it's a violation:** In pristine WildPalms the `Sync::SyncEngine` *was* the orchestrator. ARCHITECTURE_2026.md positions it at the centre of layer 3 of the architecture diagram. The refactor introduced a parallel orchestrator (`PalmRuntime` wrapping the library's engine), routed all user actions to it, but did not retire the original. The result is that the application has two answers to "what runs a sync?", and neither is fully responsible: the native engine has all the configuration knobs the UI thinks matter; the library engine has the only code path that actually runs. Conduit ordering hints (`m_syncEngine->setConduitOrdering`, line 600), conflict timeout (`setConflictTimeoutSeconds`, line 798), the `ConduitManager::activeDatabasesForConduit` resolver (line 852-854) — all of these go into a dead engine.

**What it costs WildPalms:**
- Profile settings the user adjusts in `SettingsDialog` (conflict policy timeouts, conduit ordering, conduit enable/disable for non-V2 conduits) silently no-op against the actual sync, because they configure `m_syncEngine` and the work is done by the library engine which doesn't know about them.
- Every future bugfix in the conflict / ordering / enablement subsystems has to be applied to two engines or risks behaving differently in the UI vs. in reality.
- The `InteractiveConflictHandler` (native, set on `m_syncEngine` at line 807) is dead. The actual conflict handler is `m_palmConflictHandler` (`KalburatorInteractiveConflictHandler`, set on the library engine, line 784). M5a's "fix the conflict UI" required *building a second conflict handler* because the first one was unreachable.
- Profile is now ambiguous about which knobs matter: the answer is "the ones touched by `palmruntime.cpp`" but the SettingsDialog presents them as if they all do.

**Suggested direction:** Delete `Sync::SyncEngine`, `IConduit`, `ConduitManager`, `Sync::SyncConduitBase`, `Sync::LocalFileBackend`, and the dead conflict-handler installation paths in `kf6mainwindow.cpp`. WildPalms's UI should orchestrate exactly one engine (the library one, inside `PalmRuntime`). Settings should map to that engine. If a Profile setting has no library equivalent, decide whether to add it to the library or drop it from the UI, but stop pretending both engines are real.

---

### Finding 2: `BlobBackendAdapter` — the calendar pure-virtuals every memo and todo backend has to lie about

**Severity:** critical

**Where:**
- `WildPalms/src/runtime/palmruntime.cpp:60-153` (the adapter class)
- `WildPalms/src/runtime/palmruntime.cpp:303-308` (the `dynamic_cast<SyncBackend*>` routing branch)
- `libkalburator/src/calendar/syncbackend.h:117` (`class SyncBackend : public QObject, public IBlobBackend` — note inheritance)
- `libkalburator/src/calendar/syncbackend.h:142-162` (the calendar-shaped pure virtuals every backend must implement: `loadCalendars`, `storeCalendars`, `startSync`, `removeItem`, `pushItems`)

**What it is:** `BlobBackendAdapter` is a 94-line class inside `palmruntime.cpp`. It exists to take an `IBlobBackend` (the WildPalms-facing plugin contract) and present it as a `Kalburator::Sync::SyncBackend` (the library's abstract base). The adapter forwards the eleven `IBlobBackend` methods to the wrapped backend, declares a shape (default `{blob,raw}`, optional override per-test), and then must satisfy `SyncBackend`'s **calendar-shaped pure-virtuals** — `loadCalendars`, `storeCalendars`, `startSync`, `removeItem`, `pushItems`, all of which take `KCalendarCore::MemoryCalendar*` arguments — by writing empty stubs:

```cpp
// palmruntime.cpp:133-147
void loadCalendars(const QString &) override {}
void storeCalendars(const QString &,
                    const QList<KCalendarCore::MemoryCalendar *> &) override {}
void startSync(const QString &, KCalendarCore::MemoryCalendar *,
               const QList<KCalendarCore::Incidence::Ptr> &,
               const QList<KCalendarCore::Incidence::Ptr> &,
               const QMap<QString, QString> &,
               const Kalburator::Sync::TranscodingPlan &) override {}
void removeItem(const QString &, const QString &) override {}
PushOperation *pushItems(const QString &,
    const QList<KCalendarCore::Incidence::Ptr> &,
    const Kalburator::Sync::TranscodingPlan &) override { return nullptr; }
```

The header comment justifies this: "Calendar pure-virtuals are empty stubs — never invoked via dispatchBlobSync" (`palmruntime.cpp:58`). Which is true *today*, but the existence of a `loadCalendars(...)` no-op on a memo backend is the proof that the library's abstract base class is calendar-shaped and every other domain pays the inheritance tax.

**Why it's a violation:** This is the single clearest piece of evidence that the library was not designed to host WildPalms's plugin world; it was extracted from PlanStan (a calendar-and-contacts app) and the abstract base class still bears that lineage. `Kalburator::Sync::SyncBackend` inherits `IBlobBackend` (`syncbackend.h:117`), so every "blob" plugin that wants to participate in the engine must inherit `SyncBackend` and implement the calendar surface, even if the plugin has nothing to do with calendars. WildPalms's `MemoBackend`, `TodoBackend`, `WebcalendarBackend` either go through the `BlobBackendAdapter` shim (and silently no-op on the calendar surface) or they have to inherit `SyncBackend` directly and write the same no-op methods themselves.

The shim is also where the "two type hierarchies for the same concept" lives: a plugin returns an `IBlobBackend`, PalmRuntime then `dynamic_cast`s to `SyncBackend` (`palmruntime.cpp:303`) to detect "native" backends and skip the wrapping. The same physical object is sometimes treated as a blob (wrapped) and sometimes as a SyncBackend (unwrapped), depending on what subclass it happens to be. This is exactly the "adapter that should not need to exist" the brief asks about.

**What it costs WildPalms:**
- Phase J Task 9 (the failing test) is a direct consequence. The test wraps a `MockBlobBackend` in `BlobBackendAdapter` with an explicit `{calendar,ical}` shape (the test API at `palmruntime.h:104-106` *exists* to override the default `{blob,raw}` and pretend the adapter is calendar-shaped); the engine then routes through the calendar dispatch path instead of the blob path; the calendar path requires a `MemoryCalendar*` registered for the target collection id; the adapter has none, the calendar plugin writer's null-guard fires (`calendarplugin_writer.cpp:80-85`), the sync silently fails. The bug is reading "engine wants a calendar shape but the adapter can only wear it as a costume."
- FINDINGS.md's 2026-05-09 entry "Phase J aborted: CalendarBlobBackend is an architectural dead-end" diagnoses the same thing in different words: the previous solution was *another* shim (`CalendarBlobBackend` — now deleted) that wore an iCal shape over the blob protocol. Phase J Tasks 2-5 deleted that shim and replaced it with `PalmCalendarBackend : public SyncBackend` directly. But the new backend still inherits the calendar pure-virtuals. The shim moved one layer deeper; it did not go away.
- Every plugin author who writes a new domain (a hypothetical Notes domain, an Apps domain, a Mail domain) will hit this surface and either add the no-op stubs or wrap in `BlobBackendAdapter`. Neither is correct.

**Suggested direction:** This is libkalburator's homework, not WildPalms's, but its consequences live in WildPalms. The library's `SyncBackend` should not have calendar-shaped pure-virtuals. Either factor a `CalendarSyncBackend` subclass that has them, or convert the calendar surface to operation-based methods that exist only on calendar-aware backends. Then `BlobBackendAdapter` reduces to a one-line shape declaration and the `dynamic_cast<SyncBackend*>` routing branch in `finishConnect()` disappears — every plugin's backend is just a `SyncBackend`.

---

### Finding 3: Plugin contract is WildPalms-namespaced but library-typed; the V2 interface returns a libkalburator `IBlobBackend`

**Severity:** major

**Where:**
- `WildPalms/src/core/ibackendplugin_v2.h:39-89` (the interface)
- `WildPalms/src/core/ibackendplugin_v2.h:53-54` (return type is `Kalburator::Sync::IBlobBackend`)
- `WildPalms/src/core/ibackendplugin_v2.h:62-63` (`registerDomain(Kalburator::Shape::DomainRegistry &)`)
- `WildPalms/src/core/ibackendplugin_v2.h:70` (`createConflictHandler` returns `Kalburator::Sync::QSyncCore::ConflictHandler *`)
- `WildPalms/src/plugins/calendar/calendarbackendplugin.cpp:57-80` (`createPalmBackend` constructs a `PalmCalendarBackend` and returns it through the `IBlobBackend` slot — works only because `SyncBackend : IBlobBackend`)

**What it is:** The "WildPalms plugin contract" in `WildPalms::IBackendPluginV2` declares all of its return types in libkalburator namespaces. A plugin author writing for WildPalms is writing against `Kalburator::Sync::IBlobBackend`, `Kalburator::Sync::QSyncCore::ConflictHandler`, `Kalburator::Shape::DomainRegistry`. The interface lives in `WildPalms::` namespace but its semantic vocabulary is entirely the library's.

The class comment is candid about which way the dependency points: "Returns ONLY a Palm-side IBlobBackend. PC-side backend is configured per-mapping by the user, not chosen by the plugin."

**Why it's a violation:** Pristine WildPalms had `IConduit` — a plugin contract written in WildPalms's own vocabulary (`PilotRecord`, `SyncContext`, `SyncMode`, things owned by the application). A plugin author wrote against application types. With `IBackendPluginV2`, the author writes against library types, and the calling code (`palmruntime.cpp:262-330`) is responsible for translating between them.

This is what the brief described as "WildPalms learns to speak library" — and it's now the *contract*. Future plugins can't be written without knowing libkalburator's blob/shape/domain vocabulary. WildPalms's plugin ABI is, in effect, the library's plugin ABI with WildPalms-themed wrappers.

**What it costs WildPalms:**
- Plugin authors must understand two namespaces and two paradigms to add a backend.
- The plugin contract has no useful WildPalms-specific concepts (no `KPilotLink`, no `PalmRecord`, no `SyncContext` — only `PalmDeviceAccess` which is itself a thin facade over `IPalmDatabaseAccess`, also from the WildPalms-side but designed to satisfy the library's threading model).
- When the library's `IBlobBackend` shape changes (as it has, repeatedly, between Phases F1, Ia.5, Ib.5), every WildPalms plugin gets disturbed. Phase J Tasks 2-5 are exactly this: cascading edits to plugin code because the library said "no, that interface is wrong now."
- The `M4` series of commits ("update <X> submodule pointer + test for IBackendPluginV2") confirms how often the contract churn ripples out: every plugin submodule has been touched at least once per phase.

**Suggested direction:** The plugin contract should be in WildPalms's vocabulary, with adapters at the runtime boundary. `createPalmBackend` should return a WildPalms type (`PalmDataAccessor` or similar) that the runtime translates to the library shape. This isolates plugin authors from library churn and gives WildPalms a stable plugin ABI it can version on its own schedule.

---

### Finding 4: `KF6MainWindow` runs a second plugin discovery loop that loads zero plugins — dead V1 scaffolding tied into the device-connect flow

**Severity:** major

**Where:**
- `WildPalms/src/kf6/kf6mainwindow.cpp:544-557` (`m_backendPluginManager` discovery + load)
- `WildPalms/src/kf6/kf6mainwindow.h:195` (`WildPalms::BackendPluginManager *m_backendPluginManager`)
- `WildPalms/src/runtime/backendpluginmanager.cpp:104` (`dynamic_cast<IBackendPlugin *>(obj)` — the V1 interface)
- `WildPalms/src/core/ibackendplugin.h` (the V1 interface, no production implementations)
- `WildPalms/src/plugins/calendar/calendarbackendplugin.h:36-39` (the calendar plugin implements only `IBackendPluginV2`, not V1)

**What it is:** `KF6MainWindow` constructs *two* plugin managers in `initializeConduits()` — `ConduitManager` (loads `IConduit`) and `BackendPluginManager` (loads `IBackendPlugin` V1). The V2 plugin loader is in `PalmRuntime::finishConnect()` (`palmruntime.cpp:255-260`).

So three plugin discovery loops fire, scanning two plugin directories (`wildpalms/conduits` and `wildpalms/plugins`). Two of them — `ConduitManager` and `BackendPluginManager` — produce nothing in production: there are no `IConduit` plugins in the tree (all calendar/contacts/memo/todo conduits were retired in Phase E.16) and no `IBackendPlugin` V1 implementations exist (`grep -rn ": public IBackendPlugin\b" src/` returns only the forward declarations and the new V2 interface). The current calendar/contacts/memo/todo/webcalendar plugins all implement V2.

`BackendPluginManager` is constructed with three `nullptr`s for its host/device/coordinator (`kf6mainwindow.cpp:544-545`), which is documented as a "Phase E.9 / Phase E.15-E.17 follow-up." But the follow-up never landed — Phase E.16 was supposed to delete `ConduitManager` per the comment at `kf6mainwindow.h:26-27` ("Coexists with ConduitManager until E.16 retires the old surface"), and Phase G/H/Ia/Ib/Ic/J came and went without retiring it.

**Why it's a violation:** The original WildPalms plugin model was `IConduit`. The library import added `IBackendPlugin` to support library types. Mid-refactor, V1 was found inadequate (it took a `PalmDeviceConnection*` directly, which broke threading on real hardware) and `IBackendPluginV2` was introduced. The V1 path was never deleted; the V0 (`IConduit`) path was never deleted; both keep running on every profile load. The signal connections at `kf6mainwindow.cpp:548-551` wire `pluginLoaded` / `pluginUnloading` to slots that, in practice, are never invoked.

This is a graveyard of half-completed migrations sitting inside the application's startup path. A user who has never opened a profile doesn't see the cost; a user who opens a profile pays for two unused plugin scans plus signal wiring.

**What it costs WildPalms:**
- Every developer reading `KF6MainWindow::initializeConduits()` has to know which of three loops "actually runs sync." The comment at line 540-543 admits the wiring is incomplete and references future work that didn't happen.
- The destructor at `kf6mainwindow.cpp:200-209` has to defensively disconnect `m_conduitManager` signals "to fire harmlessly into the void when deleteChildren() eventually destroys the manager (after all base-class destructors have run cleanly)" — a known lifetime issue around the dead path.
- Refactoring the plugin model in libkalburator now means navigating three plugin contracts in WildPalms.

**Suggested direction:** Delete `ConduitManager`, `IConduit`, `BackendPluginManager`, `IBackendPlugin` (V1), and the corresponding wiring in `KF6MainWindow`. Phase J's failure is the right time to do this: there is no consumer of the dead paths, and `m_palmRuntime`'s V2 path is the only one that actually loads plugins. The old TODOs about "E.16 retires the old surface" describe the work that needs doing.

---

### Finding 5: `CalendarCollection_WP` — a WildPalms class defined to satisfy a library interface, then "wired into PalmRuntime" only to keep an unrelated test runnable

**Severity:** major

**Where:**
- `WildPalms/src/runtime/calendarcollection_wp.h:13` (`class CalendarCollection_WP : public Kalburator::Sync::ICalendarCollection`)
- `WildPalms/src/runtime/palmruntime.cpp:200-202` (constructed in PalmRuntime ctor; `m_engine->setCollection(...)`)
- `WildPalms/src/runtime/palmruntime.cpp:313-324` (eager-preload loop: `loadRecords()` → parse iCal → add `MemoryCalendar` to the collection)
- `libkalburator/src/calendar/calendarplugin_writer.cpp:80-85` (the null-guard that the eager preload exists to satisfy)
- FINDINGS 2026-05-09: "CalendarPluginWriter::apply() fails immediately when target collection has no registered MemoryCalendar"

**What it is:** `CalendarCollection_WP` (the `_WP` suffix is the giveaway — class names with project-suffix tags exist when two projects need the same name) is a WildPalms-side subclass of `Kalburator::Sync::ICalendarCollection`. Its sole purpose is to be passed to `m_engine->setCollection()` so that the library's `CalendarPluginWriter` can call `m_collection->calendar(collectionId)` and get back a `MemoryCalendar*`.

`PalmRuntime::finishConnect()` then has a Palm-specific eager-preload loop (`palmruntime.cpp:313-324`): for the calendar backend, iterate every Palm collection, `loadRecords()` from the device, parse each record's bytes as iCal, build `MemoryCalendar*` instances, and stuff them into `m_calendarCollection`. **All of this is so the library's writer guard finds a non-null calendar.** It is not because WildPalms wants a `MemoryCalendar` view of the device for its own purposes (the application's UI doesn't read from it).

The Phase J Task 9 failure makes this absolutely concrete (FINDINGS, 2026-05-09): when a CalDAV target backend is added with a CalDAV-side collection id like `"Personal"`, no `MemoryCalendar` is registered for `"Personal"` — only for the palm-device collection ids — so `CalendarPluginWriter::apply("Personal", ...)` hits the null-guard at `calendarplugin_writer.cpp:80-85` and the sync silently fails. The eager-preload only thinks of palm collection ids, because the runtime *can*. Anything else fails at runtime.

**Why it's a violation:** WildPalms didn't have a "calendar collection" concept in pristine. It had a `CalendarConduit` that read records from `DatebookDB` and wrote `.ics` files. The library wants a `ICalendarCollection` because that's how the library models calendar state; WildPalms now has to *be* one to participate in calendar sync. The class is defined for the library's sake, populated in a way that satisfies the library's null-guard for one direction (palm→pc) and not the other (pc→palm).

The eager-preload further means the application now implicitly *holds the entire calendar in memory at connect time*, parsed into `KCalendarCore::MemoryCalendar`, on top of the device records and on top of whatever the writer manipulates. This is a copy WildPalms's pristine architecture didn't need — it streamed records through conduits.

**What it costs WildPalms:**
- The Phase J Task 9 calendarplugin_writer null-guard blocker (FINDINGS, 2026-05-09) is unsolvable without either populating `CalendarCollection_WP` for *every* possible target collection id (impossible — providers can mint new collections at runtime) or fixing the library writer to not require `m_collection`.
- `disconnectDevice()` now has to call `m_calendarCollection->clear()` (`palmruntime.cpp:399-401`), which means `CalendarCollection_WP` carries the lifetime contract of the device session — not a coincidence, but a sign the device's session lifecycle is now tied to a library-imported data structure.
- Memory: large palm calendars are now duplicated in `MemoryCalendar` form on connect, even if the user never opens a calendar view.
- Phase F (per the comment in `calendarcollection_wp.h:28-29`, "Phase F will wire the model layer through these") promised to make `CalendarCollection_WP` useful to the application's view layer. That hasn't happened; the class exists for the library and only the library reads it.

**Suggested direction:** Either (a) push the null-guard fix into libkalburator — `CalendarPluginWriter::apply()` should be willing to write through the backend without a registered `MemoryCalendar` (the FINDINGS 2026-05-09 entry on `CreateIncidenceItem::commit()` notes that `m_calendar` isn't actually used post-guard); or (b) drop `CalendarCollection_WP` entirely and have the engine work with the backend directly. WildPalms doesn't want this object; it has it because the library demanded it.

---

### Finding 6: `AccountController` introduces a "providers / accounts" concept WildPalms's UX paradigm never had, and a parallel persistence layer

**Severity:** major

**Where:**
- `WildPalms/src/runtime/accountcontroller.h:1-107`
- `WildPalms/src/runtime/accountcontroller.cpp:8-14` (libkalburator includes: `providermanager.h`, `iprovider.h`, `caldavprovider.h`, `carddavprovider.h`)
- `WildPalms/src/runtime/accountcontroller.cpp:67-86` (sidecar persistence at `<sync>/.wildpalms.providers`)
- `WildPalms/src/profile.h` (the existing application persistence — `.wildpalms.conf`)
- `WildPalms/src/settingsdialog.cpp:31-66` (SettingsDialog now requires an `AccountController*` to construct an "Accounts" page; the page is conditional on it being non-null)
- `WildPalms/src/app/accounts/*` (entire new UI subtree: `AccountsPage`, `AddAccountDialog`, `MappingPromptDialog`)

**What it is:** Phase Ic added an `AccountController` whose entire vocabulary is library-provided: `IProvider`, `ProviderManager`, `BackendConfiguration`, `CalDavProvider`, `CardDavProvider`. It manages credentials and connection state for cloud sync targets, persists them in a sidecar file `<sync-folder>/.wildpalms.providers`, and exposes a UI surface (the Accounts page in SettingsDialog).

WildPalms had no such concept. ARCHITECTURE_2026.md describes a "file-based backend" (`LocalFileBackend`) that writes Markdown / vCard / iCalendar to a directory tree. There were no accounts; there were no providers; there was no concept of "connect to a remote service." The sync target was a folder.

**Why it's a violation:** The library brought CalDAV/CardDAV providers, and WildPalms had to grow a UX to expose them. That's not in itself wrong — adding cloud sync is a valuable feature. But the way it was added is:

- A new top-level controller (`AccountController`) with profile-coupled lifetime, sibling to `PalmRuntime`, doing things `Profile` and `SettingsDialog` already did half of.
- A second persistence file (`.wildpalms.providers`) parallel to `.wildpalms.conf`. Two settings stores in the same sync folder, persisted via two different KConfig paths. The class comment says "Same shape PlanStan adopted in Phase H.5" — i.e., we copied PlanStan's solution directly because it fit *that* application's existing model.
- A `SettingsDialog` constructor signature that requires a runtime-coupled `AccountController*`: `SettingsDialog(..., AccountController *accounts = nullptr, ...)`. The settings dialog is now conditional on whether the runtime is up. Pristine `SettingsDialog` was a pure UI over `QSettings`; it could be opened any time.
- A new `MappingPromptDialog` that pops up when an account is added, asking the user to choose which Palm slots map to which cloud collections — a new modal flow that didn't exist in pristine WildPalms's UX.
- The `m_palmRuntime->isRunning()` check inside `AccountController::addProvider` (`accountcontroller.cpp:90`) — adding an account is now disallowed during sync. This wasn't an invariant of the pristine app; it's a constraint imposed by the library's lifecycle.

**What it costs WildPalms:**
- Two persistence layers means migration between profile versions is harder, and a partial save / corruption can leave the two stores inconsistent.
- The Accounts UX feels grafted: it's in SettingsDialog (where global settings live) but is profile-scoped (where conduit settings live), because providers are scoped to a profile. The scoping mismatch between `SettingsDialog`'s global flavor and `AccountController`'s profile flavor is what the conditional construction at `settingsdialog.cpp:63-66` papers over.
- Future provider types (Akonadi, Nextcloud, Mail, ...) will need both a library-side provider class *and* a WildPalms-side UX hook in `AccountsPage` / `MappingPromptDialog` / `AddAccountDialog`. The UI surface is hard-coded against the current two providers (CalDAV, CardDAV); adding more means coordinated changes in two repos.

**Suggested direction:** Decide whether "accounts" is a first-class WildPalms UX concept or a library-imported one. If first-class, pull the persistence into `Profile`, retire `.wildpalms.providers`, and let `AccountController` be a thin runtime view over `Profile` data. If library-imported, the provider model should be a library plugin loaded by PalmRuntime alongside the calendar/contacts plugins, and the application's UI should be a generic "providers I have" view rather than hand-coded per-provider.

---

### Finding 7: `m_link` is held twice inside PalmRuntime to keep two sync paradigms working

**Severity:** minor

**Where:**
- `WildPalms/src/runtime/palmruntime.h:155-156` (both `m_link` and `m_device` as members)
- `WildPalms/src/runtime/palmruntime.cpp:250-252` (`finishConnect` caches `m_link = m_device->link()` with comment "so backup/restore (which read m_link directly) keep working")
- `WildPalms/src/runtime/palmruntime.cpp:521,548,589,613,648,680,703,728` (every sync path tickle-pauses via `m_link` directly, not through `m_device`)
- `WildPalms/src/runtime/palmruntime.cpp:466-468` (`setLinkForTest(KPilotLink *)` — second test seam to inject only `m_link`, parallel to `setDeviceAccessForTest`)

**What it is:** `PalmRuntime` holds the device link via two members: `m_device` (a `PalmDeviceAccess`, the library-friendly facade) and `m_link` (a raw `KPilotLink*`, the Palm-runtime native pointer). The comment is candid: "Cache the link from PalmDeviceAccess so backup/restore (which read m_link directly) keep working."

The HotSync, FullSync, Mirror, Backup, and Restore paths all reach for `m_link->pauseTickle()` / `m_link->resumeTickle()` directly, bypassing `m_device`. Because the tickle pause/resume API is a Palm-runtime concern that lives on `KPilotLink` — `PalmDeviceAccess` doesn't expose it cleanly.

**Why it's a violation:** `PalmDeviceAccess` was supposed to be the device facade. It exists exactly so plugins talk to a self-marshalling object instead of a raw link pointer. But the *runtime itself* — the layer that owns `PalmDeviceAccess` — can't use it for its own tickle management, because the abstraction doesn't cover the use case. So PalmRuntime keeps the raw pointer alongside the facade and uses each in different code paths.

The double-pointer also means there are two test seams: `setDeviceAccessForTest()` injects an `m_device`, `setLinkForTest()` injects an `m_link`. Tests that exercise sync paths must remember to set both, or one half of the runtime sees a real link and the other sees nullptr.

**What it costs WildPalms:**
- Lifecycle confusion: `disconnectDevice()` (`palmruntime.cpp:395-401`) sets `m_link = nullptr` and resets `m_device`, which is correct, but the duplication means a future bug could leave them out of sync.
- The pristine `DeviceSession` (`src/palm/devicesession.cpp` in the pristine WildPalms tree, deleted in M6b) had a single, coherent ownership of the link with explicit pause/resume API. Splitting that into `PalmDeviceAccess` (library-shaped) plus `m_link` (pristine-shaped raw pointer) gave the runtime two device representations because neither alone could do the job.

**Suggested direction:** Either lift `pauseTickle`/`resumeTickle` onto `PalmDeviceAccess` (so `m_link` can be private to it and the runtime never sees it), or accept that `PalmDeviceAccess` is incomplete as a device facade and have `PalmRuntime` always go through `m_device->link()` directly, dropping the cached `m_link`. The current state — both — is the worst of both worlds.

---

### Finding 8: Mid-sync cancel was a pristine WildPalms feature; it has been silently lost in the migration

**Severity:** major

**Where:**
- `WildPalms/docs/ARCHITECTURE_2026.md:128` ("Cancellation support at any point" — listed as a key feature of pristine `DeviceSession`)
- `WildPalms/src/kf6/kf6mainwindow.cpp:1009-1013` (the comment admitting cancel is gone):

```cpp
// Note: cancelConnectionAction triggers PalmRuntime::cancelConnect
// which only cancels the open handshake. Mid-sync cancel is currently
// unsupported (would require PalmRuntime::cancelSync via QFutureWatcher
// cancellation propagation — TODO for follow-up).
```

- `WildPalms/src/runtime/palmruntime.h:55-57` (only `cancelConnect()` exists, no `cancelSync()`)
- `WildPalms/src/runtime/palmruntime.cpp:242-245` (`cancelConnect` impl — handshake-only)

**What it is:** Pristine WildPalms's `DeviceSession` exposed cancellation at any point during a sync (per ARCHITECTURE_2026.md and per the deleted `src/palm/devicesession.h` API). The new `PalmRuntime` only cancels the open-handshake phase. Once a sync is running, the user has no way to abort it — they must wait, force-quit, or pull the device cable. The comment in `KF6MainWindow` acknowledges the regression as a "TODO for follow-up."

**Why it's a violation:** This is a feature regression caused by adopting the library's engine, which has its own cancellation model (`QFuture::cancel()` with the cooperative cancellation channel described in `libkalburator/CLAUDE.md`'s test-harness section). Wiring it through `PalmRuntime`'s `QFuture<PalmRunResult>` was non-trivial enough to be deferred. So the deferral became permanent.

**What it costs WildPalms:**
- Real-world UX regression: a user who realizes they have the wrong profile loaded mid-sync has no recovery path.
- The "TODO for follow-up" was added in M5b (commit `1d691d2`, 2026-05-02) and has not been picked up across Phase G, H, H.5, Ia, Ia.5, Ib, Ib.5, Ic, or J. It is now eight phases deep and still pending.

**Suggested direction:** Wire `QFutureWatcher::cancel()` through `PalmRuntime::hotSync()`'s returned future to the engine's cancellation channel, then expose `PalmRuntime::cancelSync()` and re-enable the cancel action mid-sync. The library has the plumbing; the runtime just needs to forward the request.

---

### Finding 9: `Profile` deletion guarded against memory corruption — symptom of cross-paradigm lifetime mismatch

**Severity:** minor

**Where:** `WildPalms/src/kf6/kf6mainwindow.cpp:234-244`:

```cpp
// Safety check: verify m_currentProfile is a valid heap pointer
// before deleting. This guards against memory corruption that could
// cause the pointer to hold an invalid value (like 'this').
if (m_currentProfile != nullptr) {
    if (reinterpret_cast<void*>(m_currentProfile) == reinterpret_cast<void*>(this)) {
        qWarning() << "[KF6MainWindow] BUG: m_currentProfile points to 'this' - "
                   << "memory corruption detected, skipping delete";
    } else {
        delete m_currentProfile;
    }
}
```

**What it is:** A defensive check in `~KF6MainWindow` that `m_currentProfile` is not pointing at `this` before deleting. The comment explicitly says "memory corruption detected, skipping delete." This is not normal defensive coding; it is a workaround for a specific observed crash pattern.

**Why it's a violation:** Pristine WildPalms had a clear ownership chain: `KF6MainWindow` owns `Profile`, `Profile` is destroyed in `~KF6MainWindow`. The fact that the destructor now needs a sanity check on whether the pointer collides with `this` strongly suggests something in the surrounding code corrupts the pointer — likely cross-paradigm vtable or layout disagreements during a destruction race. The destructor body (lines 200-246) does a lot: disconnects ConduitManager signals, stops monitors, disconnects PalmRuntime, nulls-out the dead `m_syncEngine`'s device link. With the recent additions of `m_palmRuntime`, `m_accountController`, `m_palmConflictHandler`, the order of teardown matters and the order isn't fully audited.

**What it costs WildPalms:**
- A memory-corruption guard is shipping in production. The fact that it triggers (warning printed) means the corruption has been observed.
- The teardown order in `loadProfile()` and `~KF6MainWindow` now has multiple cross-borrowed objects (AccountController borrows `&m_palmRuntime->backendRegistry()` and `m_currentProfile`, comment at `palmruntime.h:113-117`). These ordering constraints are documented in comments but not enforced by the type system.

**Suggested direction:** Treat the guard as a TODO. Find the path that puts a non-heap address into `m_currentProfile` — likely a teardown-order bug where Qt parent destruction races with explicit `delete`. Then remove the guard. A defensive check that says "if memory corruption detected, skip delete" is leaking memory by design rather than crashing — a Faustian bargain.

---

### Finding 10: Tests have grown to test that the library's architecture survives inside WildPalms, not WildPalms's architecture

**Severity:** minor

**Where:**
- `WildPalms/tests/runtime/tst_runtime_caldav_e2e.cpp` (Phase J Task 9 — the failing test)
- `WildPalms/tests/runtime/tst_palm_runtime_*.cpp` (many)
- `WildPalms/tests/test_libkalburator_smoke.cpp` (a smoke test whose entire purpose is "the library still links")
- 81 test files in refactor vs. 73 in pristine — most of the new tests are runtime / accounts / kalburator-shape

**What it is:** The Phase J Task 9 test (`tst_runtime_caldav_e2e.cpp`) is illustrative of the broader pattern. It constructs a `PalmRuntime`, registers a `MockBlobBackend` via `runtime.registerBlobBackendForTest(kPalmBkId, std::move(palmBlob), kCalShape)` — the *test seam exists to override the BlobBackendAdapter's default shape so the engine routes through the calendar dispatch path*. The test is largely a test of "does the library's calendar pipeline tolerate WildPalms's plugin shape", not a test of WildPalms's behavior.

`tst_palm_runtime_modes.cpp`, `tst_palm_runtime_default_mappings_only_when_empty.cpp`, `tst_palm_runtime_reload_mappings.cpp`, `tst_palm_runtime_is_running.cpp` — all assert library-engine behavior under PalmRuntime's wrapping. They're necessary tests, but their existence means the test suite is increasingly proving the library's contract from the outside rather than proving the application's contract.

The pristine `tests/test_syncengine.cpp` still exists and tests the now-dead `Sync::SyncEngine` (Finding 1). It passes, but what it tests is unreachable code.

**Why it's a violation:** A coherent application's test suite asks "does the application do what it claims?" A two-headed application's test suite asks "do both heads agree?" — and that's a much weaker invariant. WildPalms's tests increasingly read as the latter.

**What it costs WildPalms:**
- Test coverage of the dead native engine is wasted effort; passing those tests doesn't increase confidence in the live system.
- New tests like `tst_runtime_caldav_e2e` require setting up the library's threading model, the library's transformation registry, the library's shape vocabulary — making the test harness far more complex than pristine WildPalms's harness.
- The smoke test for libkalburator linkage at `tests/test_libkalburator_smoke.cpp` is a tell: a healthy embedded library doesn't need a "does it still link" test in the consumer.

**Suggested direction:** Audit which tests are actually testing WildPalms's user-visible behavior versus testing library survival. Promote the application-behavior tests to a tier that gates releases; demote the library-survival tests to a layer the library should own. Delete `test_syncengine.cpp` if Finding 1 is acted on.

---

## What I'd argue at the dialectic

If my counterpart claims the library's design is sound and WildPalms's pain is just integration debt, here's what I'd press hardest:

**1. The empty calendar pure-virtuals on every blob backend are not integration debt — they are the type system documenting that the library's `SyncBackend` was extracted from a calendar app.** Finding 2. There is no architectural defense for `loadCalendars(const QString&) override {}` on a memo backend other than "we couldn't be bothered to factor the abstract base." Until the library separates calendar-shaped backends from generic backends, every domain that joins the library pays the calendar tax. WildPalms feels this acutely because half of its plugins are not calendar-shaped (memo, todo, install, plucker historically) and they all have to wear the costume.

**2. Phase J Task 9 isn't stuck because of two bugs; it's stuck because the calendar write path was designed for a single-collection world.** FINDINGS 2026-05-09 lays this out: `CalendarPluginWriter::apply()` requires a `MemoryCalendar*` registered for the *target* collection id, but in any provider/multi-backend world the target collection ids are arbitrary strings minted by the provider. The "fix" is to not require the collection at all (the `CreateIncidenceItem::commit()` evidence confirms the calendar is not actually used after the guard). That's a library design problem manifesting as a WildPalms test failure. WildPalms is the canary.

**3. Two SyncEngines is an indictment, not a phase.** Finding 1. A merge that completed cleanly would have replaced the native engine, not run both in parallel. Eight phases (Ia through J) have landed since the V2 plugin path replaced the V1 sync path, and `Sync::SyncEngine` is still wired into every loadProfile. Either there is something the native engine still does that the library can't (in which case the library is incomplete for WildPalms's needs), or the cleanup just hasn't been done (in which case eight phases passed without anyone closing the loop). Both readings reflect badly on the merger.

---

## What I am NOT claiming

To make this audit credible: there are places I looked at expecting damage and didn't find it.

- **`PalmDeviceAccess` is genuinely well-designed.** Finding 7 dings it for not covering tickle pause/resume, but the self-marshalling thread-affinity model (parking the device access on its own `m_linkThread`, having all calls auto-marshal via `QMetaObject::invokeMethod`) is a real improvement on pristine `DeviceSession`'s explicit-thread-handoff API. M6 was a quality refactor that happens to have happened during the merger.

- **The WildPalms-side palm backends (`PalmCalendarBackend`, `PalmContactsBackend`, `PalmMemoBackend`, `PalmTodoBackend`) under `src/palm/<domain>/` are reasonable encapsulations.** They're pure WildPalms-owned code that wraps `IPalmDatabaseAccess`, exposes virtual sub-collections by category, and inherits `SyncBackend`. The inheritance is the cost paid for Finding 2, but the per-domain code itself is clean.

- **Phase Ic's mapping editor UI (`MappingEditorDialog`, `MappingRowDialog`, `MappingPromptDialog`) is solid work.** The user-facing flow for "configure which palm slot syncs where" is genuinely better than pristine WildPalms's configuration model. Finding 6 critiques the *coupling* of accounts to the new dialogs, not the dialogs themselves.

- **The conflict-handler v2 (`KalburatorInteractiveConflictHandler` + `ConflictDialogBridge`) is well-isolated.** The `ConflictDialogBridge` indirection (`src/app/conflict/conflictdialogbridge.{h,cpp}`) deliberately wraps the include-guard collision between WP-local and libkalburator QSyncCore headers — that's a forced workaround for header-namespace pollution upstream, but the bridge is clean and the lifetime contract is documented.

- **The `IBackendPluginV2` interface, despite Finding 3's complaints, is internally coherent.** It documents its threading model, has reasonable defaults for optional methods, and the migration cost in plugin submodules has been roughly proportional. The complaint is about *which side owns the vocabulary*, not about the contract's design.

- **The plugin-page UI integration (`onBackendPluginLoaded` / `m_backendPluginPages`) has tests.** `tst_main_window_plugin_pages_populated.cpp` exists and asserts the right thing. Finding 4's claim that V1 BackendPluginManager loads zero plugins doesn't impact the V2 path's view rendering, which is exercised separately.

- **The Profile class has been mostly left alone.** It still owns `.wildpalms.conf`. Finding 6 dings the addition of the parallel `.wildpalms.providers` sidecar, but Profile itself remains coherent.

These are the seams that are clean. The findings above are about the seams that aren't.
