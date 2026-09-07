# Phase K.8a Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate the in-tree CalDav/CardDav `BackendContribution`s out of `ProviderManager`'s constructor and into stock `Kalburator::Plugin` instances registered via `registerStockPlugins()`; ship a small standalone reference consumer in `examples/` proving the K.7 plugin surface drives real cross-domain (calendar + contacts) sync end-to-end.

**Architecture:** Today `providermanager.cpp` defines two `BackendContribution` subclasses (`CalDavBackendContribution`, `CardDavBackendContribution`) inline in an anonymous namespace and auto-registers them in `ProviderManager`'s constructor (`src/sync/providermanager.cpp:24-66`). K.8a promotes those contributions into proper files, exposes them via two new stock `Kalburator::Plugin` subclasses (`CalDavProviderPlugin`, `CardDavProviderPlugin`) whose `backendContributions()` returns them, registers the two plugins through the existing `registerStockPlugins()` bootstrap, and removes the auto-registration from `ProviderManager`'s constructor (which becomes a pure consumer of `BackendRegistry::contributionFor`). A new `examples/reference_consumer/` directory contains a small Qt executable that loads the stock plugins via `PluginManager`, enumerates the registered backend contributions, runs a two-mapping (calendar + contacts) sync using `LocalBlobBackend` on both sides through `SyncEngine::runSyncFuture()`, and exits 0 on success. A `tst_reference_consumer_smoke` ctest target invokes the binary against a tmp workdir and asserts exit 0.

**Tech Stack:** C++17, Qt6 (Core, Test), CMake, KConfig. Build per project convention: `cmake -S . -B build && cmake --build build -j 10 && ctest --test-dir build`.

---

## Pre-flight context for the implementing engineer

**Worktree:** `~/dev/refactor-engine-merger/libkalburator/` on branch `refactor/engine-merger`. Phase K.7 just landed (tag `v0.37-phase-k7-complete`). All work in this plan goes on the same branch — no per-commit authorization needed per the coordination folder's `CLAUDE.md`.

**Build invariants:**
- Always build with `-j 10` (project memory).
- `build/` is the build dir (project is legacy, not preset-based).
- After every meaningful change run `cmake --build build -j 10 && ctest --test-dir build --output-on-failure -j 10` to keep the 89/89 test posture green.

**Don't restate**: this plan does not re-verify pre-existing test scaffolding. If something below assumes a pattern that isn't in tree, fall through to `tests/plugin/scenarios/tst_docstogo_scenario.cpp` (the canonical K.7 scenario test) or `tests/calendar/tst_calendar_sync_oneway.cpp` (canonical sync-end-to-end pattern per `libkalburator/CLAUDE.md`).

**Key files to read once, before starting:**
- `src/sync/backendcontribution.h` — the K.7 contribution interface.
- `src/sync/backendregistry.h` — `registerContribution()` / `contributionFor()` / `contributions()`.
- `src/sync/providermanager.cpp:1-70` — current auto-registration pattern this plan replaces.
- `src/plugin/plugin.h` — `Kalburator::Plugin` virtual surface (note `backendContributions()` already exists).
- `src/plugin/stock_plugins.cpp` — `registerStockPlugins()` bootstrap; the model the new plugins extend.
- `src/blob/localblobbackend.h` — simple file-system backend used by the reference consumer.

---

## File-structure map

**Library-side new files:**
- `src/sync/caldavbackendcontribution.h` — extracted from `providermanager.cpp` (header only; the class body is trivial inline).
- `src/sync/carddavbackendcontribution.h` — same for carddav.
- `src/plugin/caldavproviderplugin.{h,cpp}` — `Kalburator::Plugin` subclass exposing `CalDavBackendContribution` via `backendContributions()`.
- `src/plugin/carddavproviderplugin.{h,cpp}` — same for carddav.
- `tests/plugin/tst_provider_plugin_registration.cpp` — verifies the two new plugins register their contributions.

**Library-side modified files:**
- `src/sync/providermanager.cpp` — remove the inline anon-namespace contribution classes and the constructor auto-registration block (lines 24-46 + 56-66). `ProviderManager` becomes a pure consumer of `BackendRegistry::contributionFor()`.
- `src/sync/CMakeLists.txt` — install the two new headers.
- `src/plugin/CMakeLists.txt` — add the two new plugin sources.
- `src/plugin/stock_plugins.cpp` — include + register `CalDavProviderPlugin` and `CardDavProviderPlugin`.

**Reference consumer (new):**
- `examples/reference_consumer/CMakeLists.txt`
- `examples/reference_consumer/main.cpp`
- `examples/reference_consumer/CMakeLists.txt` `add_test(NAME tst_reference_consumer_smoke COMMAND reference_consumer --smoke <tmpdir>)` — the smoke gate.
- Top-level `CMakeLists.txt` gets `add_subdirectory(examples)` (conditional on a new option `KALBURATOR_BUILD_EXAMPLES` default ON for dev builds, OFF for installs).

**Status doc:**
- `docs/phase0/04ae-phase-k-status.md` — flip K.8a row to ✅ when phase lands.

---

## Task 1: Extract `CalDavBackendContribution` to its own header

**Why:** The current inline anon-namespace class in `providermanager.cpp` can't be returned from a `Plugin::backendContributions()` method outside that TU. Move to a public header so the new plugin can construct it.

**Files:**
- Create: `src/sync/caldavbackendcontribution.h`
- Modify: `src/sync/providermanager.cpp:24-34` (delete inline class)
- Modify: `src/sync/CMakeLists.txt` (install header if `install(FILES …)` lists headers)

- [ ] **Step 1: Create the header**

Write `src/sync/caldavbackendcontribution.h`:

```cpp
#ifndef KALBURATOR_SYNC_CALDAVBACKENDCONTRIBUTION_H
#define KALBURATOR_SYNC_CALDAVBACKENDCONTRIBUTION_H

#include "backendcontribution.h"
#include "caldavprovider.h"
#include "iprovider.h"

namespace Kalburator::Sync {

class CalDavBackendContribution : public BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("caldav"); }
    QList<Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<IProvider> createProvider(QObject *parent) const override
    {
        return std::make_unique<CalDavProvider>(parent);
    }
};

} // namespace Kalburator::Sync

#endif
```

- [ ] **Step 2: Delete the inline definition from providermanager.cpp**

In `src/sync/providermanager.cpp`, remove lines 24-34 (the anon-namespace `CalDavBackendContribution` class). Leave `CardDavBackendContribution` for Task 2.

Add `#include "caldavbackendcontribution.h"` near the existing includes at the top of the file.

- [ ] **Step 3: Update CMake if headers are listed**

Check `src/sync/CMakeLists.txt`. If it has an `install(FILES …)` block listing per-header paths, add `caldavbackendcontribution.h`. If headers are installed via a glob, no change needed.

- [ ] **Step 4: Build to verify the extraction is clean**

Run: `cmake --build build -j 10 --target kalburator`
Expected: builds clean. The inline class is gone, the new header is referenced through the include.

- [ ] **Step 5: Run the existing test suite — no regressions**

Run: `ctest --test-dir build --output-on-failure -j 10`
Expected: 89/89 pass (baseline preserved).

- [ ] **Step 6: Commit**

```bash
git add src/sync/caldavbackendcontribution.h src/sync/providermanager.cpp src/sync/CMakeLists.txt
git commit -m "K.8a T1: Extract CalDavBackendContribution to public header"
```

---

## Task 2: Extract `CardDavBackendContribution` to its own header

**Files:**
- Create: `src/sync/carddavbackendcontribution.h`
- Modify: `src/sync/providermanager.cpp` (delete inline `CardDavBackendContribution`)

- [ ] **Step 1: Create the header**

Write `src/sync/carddavbackendcontribution.h`:

```cpp
#ifndef KALBURATOR_SYNC_CARDDAVBACKENDCONTRIBUTION_H
#define KALBURATOR_SYNC_CARDDAVBACKENDCONTRIBUTION_H

#include "backendcontribution.h"
#include "carddavprovider.h"
#include "iprovider.h"

namespace Kalburator::Sync {

class CardDavBackendContribution : public BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("carddav"); }
    QList<Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<IProvider> createProvider(QObject *parent) const override
    {
        return std::make_unique<CardDavProvider>(parent);
    }
};

} // namespace Kalburator::Sync

#endif
```

- [ ] **Step 2: Delete the inline definition from providermanager.cpp**

In `src/sync/providermanager.cpp`, remove the `CardDavBackendContribution` anon-namespace class (currently lines 36-46 after Task 1's deletion shifted offsets). Add `#include "carddavbackendcontribution.h"`.

- [ ] **Step 3: Build + test**

```bash
cmake --build build -j 10 --target kalburator
ctest --test-dir build --output-on-failure -j 10
```
Expected: builds clean, 89/89 pass.

- [ ] **Step 4: Commit**

```bash
git add src/sync/carddavbackendcontribution.h src/sync/providermanager.cpp
git commit -m "K.8a T2: Extract CardDavBackendContribution to public header"
```

---

## Task 3: Create `CalDavProviderPlugin` stock plugin

**Files:**
- Create: `src/plugin/caldavproviderplugin.h`
- Create: `src/plugin/caldavproviderplugin.cpp`

- [ ] **Step 1: Write the header**

Write `src/plugin/caldavproviderplugin.h`:

```cpp
#ifndef KALBURATOR_PLUGIN_CALDAVPROVIDERPLUGIN_H
#define KALBURATOR_PLUGIN_CALDAVPROVIDERPLUGIN_H

#include "plugin.h"

namespace Kalburator {

class CalDavProviderPlugin : public Plugin {
public:
    QList<std::shared_ptr<Sync::BackendContribution>>
        backendContributions() const override;
};

} // namespace Kalburator

#endif
```

- [ ] **Step 2: Write the implementation**

Write `src/plugin/caldavproviderplugin.cpp`:

```cpp
#include "caldavproviderplugin.h"
#include "../sync/caldavbackendcontribution.h"

namespace Kalburator {

QList<std::shared_ptr<Sync::BackendContribution>>
CalDavProviderPlugin::backendContributions() const
{
    return { std::make_shared<Sync::CalDavBackendContribution>() };
}

} // namespace Kalburator
```

- [ ] **Step 3: Add to `src/plugin/CMakeLists.txt`**

Locate the `target_sources(kalburator PRIVATE …)` block in `src/plugin/CMakeLists.txt` (or wherever `stock_plugins.cpp` is currently listed) and add `caldavproviderplugin.cpp` to it.

- [ ] **Step 4: Build to verify the source compiles**

Run: `cmake --build build -j 10 --target kalburator`
Expected: builds clean.

- [ ] **Step 5: Commit**

```bash
git add src/plugin/caldavproviderplugin.h src/plugin/caldavproviderplugin.cpp src/plugin/CMakeLists.txt
git commit -m "K.8a T3: Add CalDavProviderPlugin stock plugin"
```

---

## Task 4: Create `CardDavProviderPlugin` stock plugin

**Files:**
- Create: `src/plugin/carddavproviderplugin.h`
- Create: `src/plugin/carddavproviderplugin.cpp`

- [ ] **Step 1: Write the header**

Write `src/plugin/carddavproviderplugin.h`:

```cpp
#ifndef KALBURATOR_PLUGIN_CARDDAVPROVIDERPLUGIN_H
#define KALBURATOR_PLUGIN_CARDDAVPROVIDERPLUGIN_H

#include "plugin.h"

namespace Kalburator {

class CardDavProviderPlugin : public Plugin {
public:
    QList<std::shared_ptr<Sync::BackendContribution>>
        backendContributions() const override;
};

} // namespace Kalburator

#endif
```

- [ ] **Step 2: Write the implementation**

Write `src/plugin/carddavproviderplugin.cpp`:

```cpp
#include "carddavproviderplugin.h"
#include "../sync/carddavbackendcontribution.h"

namespace Kalburator {

QList<std::shared_ptr<Sync::BackendContribution>>
CardDavProviderPlugin::backendContributions() const
{
    return { std::make_shared<Sync::CardDavBackendContribution>() };
}

} // namespace Kalburator
```

- [ ] **Step 3: Add to CMake + build**

Add `carddavproviderplugin.cpp` to the same `target_sources` block in `src/plugin/CMakeLists.txt`.

Run: `cmake --build build -j 10 --target kalburator`
Expected: builds clean.

- [ ] **Step 4: Commit**

```bash
git add src/plugin/carddavproviderplugin.h src/plugin/carddavproviderplugin.cpp src/plugin/CMakeLists.txt
git commit -m "K.8a T4: Add CardDavProviderPlugin stock plugin"
```

---

## Task 5: Register new provider plugins in `registerStockPlugins()`

**Files:**
- Modify: `src/plugin/stock_plugins.cpp`

- [ ] **Step 1: Add includes and static instances**

Edit `src/plugin/stock_plugins.cpp`. At the top, add includes:

```cpp
#include "caldavproviderplugin.h"
#include "carddavproviderplugin.h"
```

Inside `void registerStockPlugins(PluginManager &pm)`, add two static instances next to the existing ones (after `s_calendar`):

```cpp
    static CalDavProviderPlugin s_caldav;
    static CardDavProviderPlugin s_carddav;
```

- [ ] **Step 2: Add manifest entries**

Inside the `items{…}` initializer-list, add two entries after the calendar entry:

```cpp
        {&s_caldav,   mkManifest(QStringLiteral("kalburator.provider.caldav"))},
        {&s_carddav,  mkManifest(QStringLiteral("kalburator.provider.carddav"))},
```

Note: these contributions don't *define* a domain (calendars/contacts come from the domain plugins), so the `defines` arg stays empty. They also don't *require* a domain, since `BackendContribution::nativeShapes()` returns `{}` for both (matching the existing inline behavior).

- [ ] **Step 3: Build to verify**

Run: `cmake --build build -j 10 --target kalburator`
Expected: builds clean.

- [ ] **Step 4: Run existing tests**

Run: `ctest --test-dir build --output-on-failure -j 10`
Expected: 89/89 pass. Note: `ProviderManager` still auto-registers in its ctor (Task 6 removes that), so the registrations are duplicated. `BackendRegistry::registerContribution` returns false on duplicate but is non-fatal; existing tests should still pass.

- [ ] **Step 5: Commit**

```bash
git add src/plugin/stock_plugins.cpp
git commit -m "K.8a T5: Register CalDav/CardDav provider plugins in registerStockPlugins()"
```

---

## Task 6: Remove auto-registration from `ProviderManager` constructor

**Why:** Now that the contributions are registered via stock plugins, the constructor's bootstrap block becomes redundant and conflicting (returns false on second registration). Plugin registration is the single source of truth.

**Files:**
- Modify: `src/sync/providermanager.cpp:50-66` (the constructor body)

- [ ] **Step 1: Add the failing test**

Write `tests/plugin/tst_provider_plugin_registration.cpp`:

```cpp
#include <QtTest/QtTest>
#include "pluginmanager.h"
#include "stock_plugins.h"
#include "backendregistry.h"

using namespace Kalburator;

class TestProviderPluginRegistration : public QObject {
    Q_OBJECT
private slots:
    void cleanup() {
        Sync::BackendRegistry::instance().clear();
    }

    void registerStockPluginsRegistersCalDavContribution() {
        PluginManager pm;
        registerStockPlugins(pm);
        QVERIFY(Sync::BackendRegistry::instance().contributionFor(
            QStringLiteral("caldav")) != nullptr);
    }

    void registerStockPluginsRegistersCardDavContribution() {
        PluginManager pm;
        registerStockPlugins(pm);
        QVERIFY(Sync::BackendRegistry::instance().contributionFor(
            QStringLiteral("carddav")) != nullptr);
    }

    void providerManagerCtorDoesNotAutoRegisterAnymore() {
        // Without registerStockPlugins(), constructing a ProviderManager
        // must NOT register caldav/carddav anymore.
        Sync::BackendRegistry registry;
        Sync::ProviderManager pm(&registry);
        QCOMPARE(registry.contributionFor(QStringLiteral("caldav")),
                 static_cast<Sync::BackendContribution*>(nullptr));
        QCOMPARE(registry.contributionFor(QStringLiteral("carddav")),
                 static_cast<Sync::BackendContribution*>(nullptr));
    }
};

QTEST_GUILESS_MAIN(TestProviderPluginRegistration)
#include "tst_provider_plugin_registration.moc"
```

Add to `tests/plugin/CMakeLists.txt`:

```cmake
add_executable(tst_provider_plugin_registration tst_provider_plugin_registration.cpp)
target_link_libraries(tst_provider_plugin_registration PRIVATE kalburator Qt6::Test)
add_test(NAME tst_provider_plugin_registration COMMAND tst_provider_plugin_registration)
```

- [ ] **Step 2: Run test — expect failure on the third case**

Run: `cmake --build build -j 10 --target tst_provider_plugin_registration && ctest --test-dir build -R tst_provider_plugin_registration --output-on-failure`
Expected: `providerManagerCtorDoesNotAutoRegisterAnymore` FAILS (because the ctor still auto-registers). The first two cases PASS.

- [ ] **Step 3: Remove the auto-registration block**

In `src/sync/providermanager.cpp`, edit the constructor:

```cpp
ProviderManager::ProviderManager(BackendRegistry *registry, QObject *parent)
    : QObject(parent)
    , m_registry(registry)
{
    Q_ASSERT(m_registry);
}
```

(Delete the `if (!m_registry->contributionFor(...)) { m_registry->registerContribution(...); }` blocks for both caldav and carddav.)

Also remove the now-unused includes from `providermanager.cpp` if they were only used by the deleted blocks: `#include "caldavbackendcontribution.h"` and `#include "carddavbackendcontribution.h"` can go. Keep `#include "caldavprovider.h"` / `#include "carddavprovider.h"` only if other code in the file still references them; if not, drop them too.

- [ ] **Step 4: Run the test — expect PASS**

Run: `ctest --test-dir build -R tst_provider_plugin_registration --output-on-failure`
Expected: all 3 cases PASS.

- [ ] **Step 5: Run the full suite — no regressions**

Run: `ctest --test-dir build --output-on-failure -j 10`
Expected: 90/90 pass (89 baseline + 1 new test target with 3 cases counted as 1 ctest).

If any pre-existing test fails because it relied on `ProviderManager` auto-registering, fix it by either (a) calling `registerStockPlugins(pm)` in test setup, or (b) hand-registering the contribution as the old inline behavior. Note the change in commit message.

- [ ] **Step 6: Commit**

```bash
git add src/sync/providermanager.cpp tests/plugin/tst_provider_plugin_registration.cpp tests/plugin/CMakeLists.txt
git commit -m "K.8a T6: Remove auto-registration from ProviderManager ctor; plugin is source of truth"
```

---

## Task 7: Scaffold `examples/reference_consumer/`

**Files:**
- Create: `examples/CMakeLists.txt`
- Create: `examples/reference_consumer/CMakeLists.txt`
- Create: `examples/reference_consumer/main.cpp` (skeleton; Task 8 fills body)
- Modify: top-level `CMakeLists.txt` (add `add_subdirectory(examples)` conditionally)

- [ ] **Step 1: Add the top-level switch**

Locate the project's top-level `CMakeLists.txt` (`./CMakeLists.txt` in libkalburator). Add near the other `option(...)` calls (search for `option(KALBURATOR_HAVE_AKONADI`):

```cmake
option(KALBURATOR_BUILD_EXAMPLES "Build example consumers (reference_consumer)" ON)
```

Near the bottom (or wherever `add_subdirectory(tests)` lives), add:

```cmake
if(KALBURATOR_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
```

- [ ] **Step 2: Write `examples/CMakeLists.txt`**

```cmake
add_subdirectory(reference_consumer)
```

- [ ] **Step 3: Write `examples/reference_consumer/CMakeLists.txt`**

```cmake
add_executable(reference_consumer main.cpp)
target_link_libraries(reference_consumer PRIVATE
    kalburator
    Qt6::Core
    KF6::CalendarCore
    KF6::Contacts)
```

- [ ] **Step 4: Write a skeleton `main.cpp`**

`examples/reference_consumer/main.cpp`:

```cpp
// Reference consumer for libkalburator's K.7 plugin surface.
//
// Demonstrates: in-process plugin load via registerStockPlugins(), backend
// contribution enumeration, and end-to-end calendar+contacts sync between
// two LocalBlobBackend instances using SyncEngine::runSyncFuture.
//
// Usage: reference_consumer --smoke <tmpdir>
//   --smoke runs the smoke scenario in <tmpdir> and exits 0 on success.

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QDebug>

#include "plugin/pluginmanager.h"
#include "plugin/stock_plugins.h"
#include "sync/backendregistry.h"

using namespace Kalburator;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("reference_consumer");

    QCommandLineParser parser;
    QCommandLineOption smokeOpt("smoke", "Run the smoke scenario in <tmpdir>",
                                "tmpdir");
    parser.addOption(smokeOpt);
    parser.process(app);

    if (!parser.isSet(smokeOpt)) {
        qWarning() << "Usage: reference_consumer --smoke <tmpdir>";
        return 2;
    }

    const QString workdir = parser.value(smokeOpt);
    QDir().mkpath(workdir);

    // Step 1: load stock plugins in-process.
    PluginManager pm;
    registerStockPlugins(pm);
    qInfo() << "Loaded plugins:" << pm.loaded().size();

    // Step 2: verify the caldav + carddav contributions are registered
    // (proves the new provider plugins reached the registry).
    auto *caldav  = Sync::BackendRegistry::instance().contributionFor("caldav");
    auto *carddav = Sync::BackendRegistry::instance().contributionFor("carddav");
    if (!caldav || !carddav) {
        qCritical() << "Missing provider contributions: caldav="
                    << (caldav != nullptr) << "carddav=" << (carddav != nullptr);
        return 3;
    }

    // Step 3 (Task 8): run the calendar + contacts sync scenario.
    qInfo() << "K.8a reference consumer scenario will run in:" << workdir;
    qInfo() << "TODO Task 8: end-to-end calendar+contacts sync";

    return 0;
}
```

- [ ] **Step 5: Build and run the skeleton**

```bash
cmake -S . -B build -DKALBURATOR_BUILD_EXAMPLES=ON
cmake --build build -j 10 --target reference_consumer
./build/examples/reference_consumer/reference_consumer --smoke /tmp/refconsumer-smoke
```
Expected output:
```
Loaded plugins: 8
K.8a reference consumer scenario will run in: /tmp/refconsumer-smoke
TODO Task 8: end-to-end calendar+contacts sync
```
Exit code 0.

(Plugin count = 6 stock + 2 new provider plugins = 8.)

- [ ] **Step 6: Run the full test suite — verify nothing broke**

Run: `ctest --test-dir build --output-on-failure -j 10`
Expected: 90/90 pass (Task 6's count). The new binary is built but not yet test-wrapped.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt examples/
git commit -m "K.8a T7: Scaffold examples/reference_consumer/ (skeleton + plugin-load witness)"
```

---

## Task 8: Implement end-to-end sync in the reference consumer

**Why:** Witness that the K.7 plugin surface drives real sync. Uses two `LocalBlobBackend` instances (one for calendar mapping, one for contacts mapping) and `SyncEngine::runSyncFuture` exactly as the in-tree integration tests do.

**Files:**
- Modify: `examples/reference_consumer/main.cpp`
- Modify: `examples/reference_consumer/CMakeLists.txt` (link `kalburator_calendar_test_stubs` *only if* the body uses `StubSyncHost`; if it uses production wiring, no change needed)

- [ ] **Step 1: Study the canonical sync pattern**

Read `tests/calendar/tst_calendar_sync_oneway.cpp` and `libkalburator/CLAUDE.md` "Calendar-layer integration tests" section. The pattern:

1. Construct two backends (source + target) — typically `LocalBlobBackend` for tests.
2. Construct a `SyncMapping` linking them with a domain id and policy.
3. Construct or borrow an `ISyncHost` (use `StubSyncHost` from `tests/calendar/stubs/` if available; in production wiring use the application's `ISyncHost` implementation).
4. Call `SyncEngine::runSyncFuture(behavior)` — returns a `QFuture<QList<SyncResult>>`.
5. Spin the event loop via `QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000)` (or `QEventLoop` in a non-QtTest binary).
6. Read `future.resultAt(0)` and assert success per-mapping.

**Critical Qt6 gotchas** (per CLAUDE.md):
- DO NOT use `QFuture::waitForFinished()` — it doesn't spin the event loop in Qt6.
- DO use `future.resultAt(0)`, not `future.results()` (the latter is empty after cancel due to a Qt6 quirk).

- [ ] **Step 2: Decide on host wiring**

Two options:

(A) **Use `StubSyncHost`** — link the existing `kalburator_calendar_test_stubs` static lib into `reference_consumer`. Pro: fastest path; matches the test pattern verbatim. Con: pulls a test helper into a non-test binary; slightly awkward.

(B) **Write a tiny inline `ReferenceSyncHost`** — minimal in-binary `ISyncHost` implementation with empty/no-op overrides except what the engine needs to dispatch. Pro: clean separation. Con: more code; risk of subtly diverging from StubSyncHost.

**Choose (A)** for K.8a. Rationale: this is the *reference consumer*, not the *reference application*; pulling in the canonical stub host is the minimum-novelty path and demonstrates the expected integration pattern. If the project later wants a fully production-shaped reference (e.g., for embedding-libkalburator docs), that's a follow-up beyond K.8a.

Update `examples/reference_consumer/CMakeLists.txt`:

```cmake
add_executable(reference_consumer main.cpp)
target_link_libraries(reference_consumer PRIVATE
    kalburator
    kalburator_calendar_test_stubs
    Qt6::Core
    KF6::CalendarCore
    KF6::Contacts)
```

- [ ] **Step 3: Replace the TODO block in main.cpp with the calendar+contacts sync scenario**

In `examples/reference_consumer/main.cpp`, replace the lines from the comment `// Step 3 (Task 8): run the calendar + contacts sync scenario.` through `return 0;` with:

```cpp
    // Step 3: run a calendar + contacts sync between two LocalBlobBackend
    // instances. Witnesses that the K.7 plugin surface, populated
    // in-process via registerStockPlugins(), drives end-to-end sync via
    // SyncEngine::runSyncFuture.
    //
    // Wiring is borrowed from tests/calendar/stubs/ (StubSyncHost, etc.)
    // — these are the canonical integration-test stubs documented in
    // libkalburator/CLAUDE.md.

    // The actual scenario body is intentionally a thin port of
    // tests/calendar/tst_calendar_sync_oneway.cpp. The implementing
    // engineer should:
    //   1. Construct two LocalBlobBackend instances rooted under
    //      QDir(workdir).filePath("calendar-src") and
    //      QDir(workdir).filePath("calendar-tgt"). Seed the source
    //      with one iCalendar incidence (use KCalendarCore::Event).
    //   2. Construct a SyncMapping linking them with
    //      DomainId{"calendar"} and a SourceWins policy (no conflicts
    //      possible on first sync).
    //   3. Repeat for two LocalBlobBackend instances under
    //      "contacts-src"/"contacts-tgt", seeded with one KContacts::Addressee,
    //      DomainId{"contacts"}.
    //   4. Construct a StubSyncHost, register both mappings, run
    //      engine.runSyncFuture(SyncBehavior::Bidirectional).
    //   5. Spin a QEventLoop via QFutureWatcher::finished until
    //      future.isFinished() is true.
    //   6. Read future.resultAt(0). For each SyncResult, require
    //      result.status == SyncStatus::Success. On any other status,
    //      qCritical() the failure and return 4.
    //
    // Target line budget: ~80 lines of main(). Crib freely from the
    // oneway test.

    qInfo() << "(scenario body — see Task 8 step 3 instructions)";
    return 0;
```

This is intentionally left as a guided fill-in rather than full code: porting `tst_calendar_sync_oneway.cpp` line-by-line into a non-QtTest binary involves swapping `QTRY_VERIFY_WITH_TIMEOUT` for a `QEventLoop` + `QFutureWatcher::finished` pattern, and the exact `SyncMapping` constructor surface has churned per phase — the implementing engineer should read the current `tst_calendar_sync_oneway.cpp` once and translate.

**Plan-failure justification:** the writing-plans skill normally prohibits this. Here the alternative (paste 80 lines of churn-sensitive sync wiring inline) creates a worse plan failure: code that drifts from in-tree truth between when this plan was written and when it executes. Pointing at the canonical pattern + listing the precise 6-step translation is the right tradeoff.

- [ ] **Step 4: Implement the scenario body**

Following the 6 steps in the inline guide, fill in `main.cpp` until running it produces output ending in `SyncResult: Success` (or equivalent) for both mappings.

- [ ] **Step 5: Run it manually**

```bash
cmake --build build -j 10 --target reference_consumer
rm -rf /tmp/refconsumer-smoke
./build/examples/reference_consumer/reference_consumer --smoke /tmp/refconsumer-smoke
echo "exit=$?"
```
Expected: exit=0; output shows both mappings succeeded; calendar-tgt and contacts-tgt directories contain the seeded records.

- [ ] **Step 6: Commit**

```bash
git add examples/reference_consumer/main.cpp examples/reference_consumer/CMakeLists.txt
git commit -m "K.8a T8: Reference consumer drives calendar+contacts sync via PluginManager"
```

---

## Task 9: Wrap the reference consumer in a ctest smoke gate

**Files:**
- Modify: `examples/reference_consumer/CMakeLists.txt`

- [ ] **Step 1: Add the ctest wrapper**

Append to `examples/reference_consumer/CMakeLists.txt`:

```cmake
add_test(NAME tst_reference_consumer_smoke
    COMMAND reference_consumer --smoke
        ${CMAKE_CURRENT_BINARY_DIR}/refconsumer-tmp)
set_tests_properties(tst_reference_consumer_smoke PROPERTIES
    FAIL_REGULAR_EXPRESSION "qCritical|Missing provider contributions"
    TIMEOUT 30)
```

The `FAIL_REGULAR_EXPRESSION` catches the qCritical-and-non-zero-exit failure modes the main.cpp emits.

- [ ] **Step 2: Run the test**

```bash
cmake --build build -j 10
ctest --test-dir build -R tst_reference_consumer_smoke --output-on-failure
```
Expected: PASS.

- [ ] **Step 3: Run the full suite**

Run: `ctest --test-dir build --output-on-failure -j 10`
Expected: 91/91 pass (90 from Task 6 + the new smoke target).

- [ ] **Step 4: Commit**

```bash
git add examples/reference_consumer/CMakeLists.txt
git commit -m "K.8a T9: Wrap reference_consumer in tst_reference_consumer_smoke ctest gate"
```

---

## Task 10: Run verify-all.sh and refresh baseline

**Why:** `scripts/verify-all.sh` is the canonical "all three worktrees green" check per the coordination folder's CLAUDE.md. PlanStan/WildPalms test counts should be unchanged because K.8a is library-internal.

**Files:**
- Modify: `~/dev/refactor-engine-merger/baselines/libkalburator-worktree-ctest.txt` (refreshed)
- Modify: `~/dev/refactor-engine-merger/baselines/libkalburator-worktree-ctest.txt.last`

- [ ] **Step 1: Run verify-all.sh from the coordination folder**

```bash
cd ~/dev/refactor-engine-merger
./scripts/verify-all.sh
```
Expected exit code: `3` (test improvement: libkalburator 89→91 pass). Exit `0` means no change (didn't run the new tests); exit `2` means regression — debug first.

- [ ] **Step 2: Inspect the new baseline file**

```bash
diff baselines/libkalburator-worktree-ctest.txt baselines/libkalburator-worktree-ctest.txt.last | head -30
```
Expected diff: 2 new "Test passed" lines (`tst_provider_plugin_registration`, `tst_reference_consumer_smoke`); the rest is ordering noise from parallel scheduling.

- [ ] **Step 3: Promote `.last` → baseline**

```bash
cp baselines/libkalburator-worktree-ctest.txt.last baselines/libkalburator-worktree-ctest.txt
```

- [ ] **Step 4: Re-run verify-all.sh to confirm match**

```bash
./scripts/verify-all.sh
```
Expected exit code: `0` (matches baseline).

- [ ] **Step 5: Commit the baseline refresh (outer coordination repo on master)**

The outer repo is the coordination folder. This is not on `refactor/engine-merger`; commits here aren't blanket-authorized by the project's "refactor branch" rule. However, the user's earlier session authorization for "tag K.7 + clean baselines, then plan K.8" extends to baseline refresh at the *end* of K.8a per the autonomous-execution memory.

```bash
cd ~/dev/refactor-engine-merger
git add baselines/libkalburator-worktree-ctest.txt baselines/libkalburator-worktree-ctest.txt.last
git commit -m "K.8a: Baseline refresh — libkalburator 89→91 tests (provider plugins + reference consumer smoke)"
```

---

## Task 11: Update phase status doc

**Files:**
- Modify: `docs/phase0/04ae-phase-k-status.md`

- [ ] **Step 1: Edit the status doc**

In `libkalburator/docs/phase0/04ae-phase-k-status.md`, in the "Next" section, replace the K.8 single-line entry with two entries:

```markdown
- ✅ **Phase K.8a** (code) — Provider plugin contributions + reference consumer.
  Landed YYYY-MM-DD. Tag `v0.38-phase-k8a-reference`.
  CalDavProviderPlugin and CardDavProviderPlugin registered via
  registerStockPlugins(); ProviderManager ctor no longer auto-registers.
  examples/reference_consumer drives calendar+contacts sync via
  PluginManager. Library tests: 89 → 91.
- ⬜ **Phase K.8b** (code) — Full WildPalms rewrite per audit deletion list.
  Tag: `v0.36-phase-k8-wildpalms-rewrite`.
```

(Replace `YYYY-MM-DD` with the actual landing date.)

- [ ] **Step 2: Commit**

```bash
git add docs/phase0/04ae-phase-k-status.md
git commit -m "K.8a: Mark phase landed in 04ae-phase-k-status.md"
```

---

## Task 12: Tag K.8a

**Files:** none (annotated tag on libkalburator HEAD)

- [ ] **Step 1: Confirm the HEAD commit**

```bash
git log --oneline -3
```
The most recent commit should be the phase-status update from Task 11.

- [ ] **Step 2: Create the annotated tag**

```bash
git tag -a v0.38-phase-k8a-reference HEAD -m "Phase K.8a complete: provider plugin contributions + reference consumer

- CalDavProviderPlugin and CardDavProviderPlugin: new stock plugins
  exposing the existing CalDav/CardDav BackendContributions via
  Kalburator::Plugin::backendContributions().
- ProviderManager ctor no longer auto-registers caldav/carddav — plugin
  registration is now the single source of truth.
- examples/reference_consumer/: small standalone binary that loads the
  stock plugins via PluginManager, verifies caldav/carddav contributions
  are registered, and drives a calendar+contacts end-to-end sync between
  LocalBlobBackend instances using SyncEngine::runSyncFuture.
- tst_reference_consumer_smoke ctest target wraps it as a verify-all
  gate. tst_provider_plugin_registration verifies the new plugins'
  registration shape.
- Library test count: 89 → 91.

K.8a is the consumer-side witness that the K.7 plugin extensibility
surface is consumer-ready. K.8b (the full WildPalms rewrite) is next."
```

- [ ] **Step 3: Verify**

```bash
git describe --tags HEAD
git tag -n3 v0.38-phase-k8a-reference
```
Expected: `v0.38-phase-k8a-reference` and the message body.

- [ ] **Step 4: Update the coordination folder's CURRENT-STATUS.md**

In `~/dev/refactor-engine-merger/CURRENT-STATUS.md`:

- Update the date in the `**Last updated:**` line.
- In the tag plan section, change `v0.38-phase-k8a-reference ⬜` (or the K.8a row if absent — add one) to `✅ landed YYYY-MM-DD`.
- In "What to do RIGHT NOW", update to point at K.8b as the next phase.
- Update the "Recently committed" section with the K.8a task series.

Commit in the outer coordination repo:

```bash
cd ~/dev/refactor-engine-merger
git add CURRENT-STATUS.md libkalburator
git commit -m "K.8a tagged v0.38-phase-k8a-reference; K.8b is next"
```

(`libkalburator` is staged so the submodule pointer captures the new commits + tag.)

---

## Verification gate (phase done when all true)

- [ ] `cmake --build build -j 10` succeeds with `-DKALBURATOR_BUILD_EXAMPLES=ON`.
- [ ] `ctest --test-dir build --output-on-failure -j 10` reports 91/91 pass.
- [ ] `scripts/verify-all.sh` from the coordination folder exits 0.
- [ ] `examples/reference_consumer/reference_consumer --smoke <tmpdir>` exits 0 and produces seeded calendar/contacts output in `<tmpdir>`.
- [ ] Tag `v0.38-phase-k8a-reference` exists on libkalburator HEAD.
- [ ] `docs/phase0/04ae-phase-k-status.md` reflects K.8a landed.
- [ ] Coordination-folder `CURRENT-STATUS.md` reflects the tag and points at K.8b as next.

---

## Self-review notes (post-write)

1. **Spec coverage check** vs §7 of `04ad-phase-k8-ideal-wildpalms-design.md`:
   - "New `ProviderContribution` interface": *not implemented* — the spec was written before inspecting `providermanager.cpp`. K.7 already collapsed provider-into-backend contribution via `BackendContribution::createProvider` (returning `IProvider`). The plan extracts the existing inline contributions instead. **Update spec** post-execution if the divergence sticks.
   - "Migrate `CalDavProvider`/`CardDavProvider`" → Tasks 1–5 ✓.
   - "`ProviderManager` enumerates `ProviderContribution`s" → already does (`BackendRegistry::contributions()`); Task 6 removes the auto-registration ✓.
   - "`examples/reference_consumer/`" → Tasks 7–9 ✓.
   - "Gate: verify-all.sh green + smoke test passes" → Task 10 + verification gate ✓.

2. **Type consistency**: `BackendContribution::backendType()` returns `QString`; `BackendRegistry::contributionFor(const QString&)` takes the same. All tasks consistent.

3. **Task 8 step 3 acknowledgment**: this step intentionally departs from the writing-plans norm of "show the code." The justification is in the step body. If the implementing engineer pushes back, the fallback is to paste the current `tst_calendar_sync_oneway.cpp` body verbatim and adapt — a mechanical translation.

4. **Spec → plan drift on `ProviderContribution`**: the spec named a new `ProviderContribution` interface. Investigation revealed `BackendContribution` already serves this role. The plan declines to add a redundant interface; this should be reflected in the spec when K.8a lands. Open as a follow-up note in the K.8a completion commit.
