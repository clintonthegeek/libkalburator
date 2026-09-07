# Plan 2 — Per-Engine Registries Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `::instance()` Service-Locator access to the three `Shape::` registries with a dependency-injected, per-engine `ShapeRegistries` bundle shared by reference between `PluginManager` (writer) and `SyncEngine` (reader), ending the per-test `clear()` rituals.

**Architecture:** A composition-root-owned `ShapeRegistries` value type bundles `TransformationRegistry` + `DomainRegistry` + `DomainOperationsRegistry`. It is injected by reference into both `PluginManager` and `SyncEngine` (threaded onward to `SyncEngineWorker`, which holds the read sites). A process-global *default* bundle is retained as **documented Ambient-Context scaffolding**: the three `::instance()` accessors and the existing ctors keep working by binding to it, so PlanStan/WildPalms compile unchanged (invariant 10). This mirrors the dependency injection `BackendRegistry` already uses in this codebase (the OSGi `BundleContext` topology — design §8).

**Tech Stack:** C++17 / Qt6, CMake, QtTest. Build/test commands per `docs/campaign/STATUS.md`.

**Source of truth:** design §8 (`docs/2026-05-23-canon-upgrade-and-convergence-design.md`), INVARIANTS (esp. 1, 5, 6, 8, 10), STATUS locked-decision 5, FINDINGS O6/O7.

---

## Why each task stays green (read before starting)

- After **Task 1**, `TransformationRegistry::instance()` returns `defaultShapeRegistries().transformation` (etc.). Behavior is byte-identical to today — every existing test and downstream consumer is unaffected. This is the seam.
- **Tasks 3–4** add *new* injecting ctor overloads and keep the old ctors as delegations to the default bundle. No call site is forced to change; downstream stays green per commit (invariant 10).
- **Tasks 6–11** convert tests to inject their own stack-/fixture-owned bundle and *delete* their `clear()` calls. Each batch is independently green. Until a file is converted it keeps using the default bundle and its `clear()` — both still valid.
- The Ambient-Context default is **not removed** in this plan (that needs the downstream port — FINDINGS O7).

## File structure

| File | Responsibility | Change |
|------|----------------|--------|
| `src/shape/shaperegistries.h` | The `ShapeRegistries` bundle struct + `defaultShapeRegistries()` accessor | **Create** |
| `src/shape/shaperegistries.cpp` | `defaultShapeRegistries()` definition | **Create** |
| `src/shape/transformationregistry.{h,cpp}` | Public ctor; `instance()` delegates to default bundle | Modify |
| `src/shape/domainregistry.{h,cpp}` | Public ctor; `instance()` delegates to default bundle | Modify |
| `src/shape/domainoperationsregistry.{h,cpp}` | Public ctor; `instance()` delegates to default bundle | Modify |
| `src/engine/syncengine.{h,cpp}` | Injecting ctor + `m_shape` ref; thread to worker; 7 read sites | Modify |
| `src/plugin/pluginmanager.{h,cpp}` | Injecting ctor + `m_shape` ref; `applyPlugin` write/unwind sites | Modify |
| `src/shape/CMakeLists.txt` (or equivalent) | Add `shaperegistries.cpp` to the shape sources | Modify |
| `tests/engine/tst_engine_registry_injection.cpp` | New falsifiable per-engine-isolation test (invariant 5) | **Create** |
| 31 existing test files | Inject a fixture/local bundle; delete `clear()` | Modify (Tasks 6–11) |

---

## Task 1: The `ShapeRegistries` bundle + Ambient-Context default

**Files:**
- Create: `src/shape/shaperegistries.h`, `src/shape/shaperegistries.cpp`
- Modify: `src/shape/transformationregistry.h:107`, `src/shape/transformationregistry.cpp:7`
- Modify: `src/shape/domainregistry.h:38`, `src/shape/domainregistry.cpp:7`
- Modify: `src/shape/domainoperationsregistry.h:40`, `src/shape/domainoperationsregistry.cpp:5`
- Modify: the shape library `CMakeLists.txt`

- [ ] **Step 1: Make the three registry default ctors public**

In `src/shape/transformationregistry.h`, move the ctor out of the `private:` block to a `public:` block. Replace:

```cpp
private:
    TransformationRegistry() = default;
```
with:
```cpp
public:
    /// Default-constructible so it can be a member of ShapeRegistries (DI).
    /// Use ShapeRegistries / dependency injection, not instance(), in new code.
    TransformationRegistry() = default;

private:
```

In `src/shape/domainregistry.h`, replace:
```cpp
private:
    DomainRegistry() = default;
```
with:
```cpp
public:
    DomainRegistry() = default;

private:
```

In `src/shape/domainoperationsregistry.h`, replace:
```cpp
private:
    DomainOperationsRegistry() = default;
```
with:
```cpp
public:
    DomainOperationsRegistry() = default;

private:
```

- [ ] **Step 2: Create the bundle header**

Create `src/shape/shaperegistries.h`:

```cpp
#pragma once

#include "domainoperationsregistry.h"
#include "domainregistry.h"
#include "transformationregistry.h"

namespace Kalburator::Shape {

/// The composition-root product for shape state: the three registries a
/// SyncEngine reads and a PluginManager writes, bundled so they can be
/// owned by value and injected by reference into both. This is the shape
/// equivalent of an OSGi BundleContext (design §8). One bundle per engine
/// gives that engine its own versioned spine (in `transformation`) and its
/// own domain/operations tables.
///
/// New code MUST inject a ShapeRegistries& rather than call
/// TransformationRegistry::instance() etc. The instance() accessors and
/// defaultShapeRegistries() are transitional Ambient-Context scaffolding
/// (Seemann) kept only so downstream consumers compile during migration;
/// see FINDINGS O7 for their scheduled removal.
struct ShapeRegistries {
    TransformationRegistry   transformation;
    DomainRegistry           domain;
    DomainOperationsRegistry operations;
};

/// The process-global default bundle. TransformationRegistry::instance()
/// and friends delegate to its members so existing callers and the
/// no-bundle ctor overloads keep working. ANTI-PATTERN (Ambient Context):
/// do not reach for this in new code — inject a ShapeRegistries& instead.
ShapeRegistries &defaultShapeRegistries();

}  // namespace Kalburator::Shape
```

- [ ] **Step 3: Create the bundle source**

Create `src/shape/shaperegistries.cpp`:

```cpp
#include "shaperegistries.h"

namespace Kalburator::Shape {

ShapeRegistries &defaultShapeRegistries()
{
    static ShapeRegistries s;
    return s;
}

}  // namespace Kalburator::Shape
```

- [ ] **Step 4: Delegate the three `instance()` accessors to the default bundle**

In `src/shape/transformationregistry.cpp`, replace the body of `instance()` (currently `static TransformationRegistry s; return s;` around line 7):

```cpp
#include "shaperegistries.h"
// ... existing includes ...

TransformationRegistry& TransformationRegistry::instance() {
    return defaultShapeRegistries().transformation;
}
```

In `src/shape/domainregistry.cpp`:
```cpp
#include "shaperegistries.h"
// ... existing includes ...

DomainRegistry& DomainRegistry::instance() {
    return defaultShapeRegistries().domain;
}
```

In `src/shape/domainoperationsregistry.cpp`:
```cpp
#include "shaperegistries.h"
// ... existing includes ...

DomainOperationsRegistry &DomainOperationsRegistry::instance()
{
    return defaultShapeRegistries().operations;
}
```

- [ ] **Step 5: Add `shaperegistries.cpp` to the build**

Find the shape sources list. Run: `grep -rn "domainregistry.cpp\|transformationregistry.cpp" --include=CMakeLists.txt .`
Add `shape/shaperegistries.cpp` (or `src/shape/shaperegistries.cpp`, matching the sibling entries' relative form) to the same target's source list, immediately after the `domainoperationsregistry.cpp` entry.

- [ ] **Step 6: Configure + build the library**

Run: `cmake -S /home/clinton/dev/libkalburator -B /home/clinton/dev/libkalburator/build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build /home/clinton/dev/libkalburator/build`
Expected: builds clean. (No behavior change yet — `instance()` returns the default bundle's members.)

- [ ] **Step 7: Run the full suite to confirm the seam is behavior-neutral**

Run: `ctest --test-dir /home/clinton/dev/libkalburator/build`
Expected: same pass/fail set as before this task (all currently-passing tests still pass; `clear()` now clears the default bundle's members, which is identical to the old singletons).

- [ ] **Step 8: Commit**

```bash
git add src/shape/shaperegistries.h src/shape/shaperegistries.cpp \
        src/shape/transformationregistry.h src/shape/transformationregistry.cpp \
        src/shape/domainregistry.h src/shape/domainregistry.cpp \
        src/shape/domainoperationsregistry.h src/shape/domainoperationsregistry.cpp
git add -u   # picks up the CMakeLists.txt edit
git commit -m "shape: add injectable ShapeRegistries bundle; instance() delegates to default (Plan 2 Task 1)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 2: Falsifiable per-engine-isolation test (invariant 5, fail-first)

**Files:**
- Create: `tests/engine/tst_engine_registry_injection.cpp`
- Modify: `tests/engine/CMakeLists.txt`

This test pins the new capability — two independently-configured bundles route independently — which was *impossible* under the process-global singleton. It is written against the injecting `PluginManager` ctor that Task 4 introduces, so it will not compile until Tasks 3–4 land: that compile failure is the required red (invariant 5). Engine-*path* coverage (invariant 6) is provided by the integration tests converted in Tasks 8–10, which construct `SyncEngine(&reg, &host, bundle)` and would fail to route if the worker didn't read the injected bundle.

- [ ] **Step 1: Write the failing test**

Create `tests/engine/tst_engine_registry_injection.cpp`:

```cpp
// Proves per-engine registry isolation: two ShapeRegistries bundles
// configured differently route independently. Impossible under the old
// process-global singleton (there was only one set of edges). See design §8.
#include <QtTest>

#include "plugin/pluginmanager.h"
#include "plugin/stock_plugins.h"
#include "sync/backendregistry.h"
#include "shape/shape.h"
#include "shape/shaperegistries.h"

using namespace Kalburator;
using namespace Kalburator::Shape;

class TestEngineRegistryInjection : public QObject
{
    Q_OBJECT

private slots:
    // Bundle A has the stock contacts vcard3<->vcard4 edge; bundle B is
    // empty. The same compile() the worker performs succeeds on A and
    // fails on B — the two bundles do not share state.
    void distinctBundlesRouteIndependently()
    {
        Sync::BackendRegistry pmRegA;
        ShapeRegistries shapeA;
        PluginManager pmA(&pmRegA, shapeA);   // injecting ctor (Task 4)
        registerStockPlugins(pmA);

        ShapeRegistries shapeB;               // never populated

        const Shape v3{ DomainId{"contacts"}, EncodingId{"vcard3"} };
        const Shape v4{ DomainId{"contacts"}, EncodingId{"vcard4"} };

        QVERIFY(shapeA.transformation.compile(v3, v4).has_value());
        QVERIFY(!shapeB.transformation.compile(v3, v4).has_value());
    }
};

QTEST_MAIN(TestEngineRegistryInjection)
#include "tst_engine_registry_injection.moc"
```

- [ ] **Step 2: Register the test in CMake**

In `tests/engine/CMakeLists.txt`, add a registration entry matching the sibling pattern used by `tst_engine_unified_routing` (find it: `grep -n "tst_engine_unified_routing" tests/engine/CMakeLists.txt`). Mirror that entry exactly, substituting the new file/target name `tst_engine_registry_injection`. It must link whatever target exposes `registerStockPlugins` and `PluginManager` (the same libraries `tst_engine_unified_routing` links).

- [ ] **Step 3: Run it; verify it FAILS to compile**

Run: `cmake --build /home/clinton/dev/libkalburator/build --target tst_engine_registry_injection`
Expected: FAIL — compile error, no matching constructor `PluginManager(BackendRegistry*, ShapeRegistries&)` (the injecting ctor doesn't exist until Task 4). This is the falsifiable red: the capability the test asserts is not yet present.

- [ ] **Step 4: Commit the failing test**

```bash
git add tests/engine/tst_engine_registry_injection.cpp tests/engine/CMakeLists.txt
git commit -m "test: pin per-engine registry isolation (fails until injection lands) (Plan 2 Task 2)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 3: Inject `ShapeRegistries` through `SyncEngine` and `SyncEngineWorker`

**Files:**
- Modify: `src/engine/syncengine.h` (worker ctor ~148, worker member ~326, engine ctor ~390, engine member ~737)
- Modify: `src/engine/syncengine.cpp` (engine ctor 49–60, worker ctor 1304–1319, read sites 1403/1861/1874/1885/2417/2423/2641)

- [ ] **Step 1: Add the `ShapeRegistries&` member + injecting ctor to `SyncEngine` (header)**

In `src/engine/syncengine.h`, add the include near the other shape includes:
```cpp
#include "shape/shaperegistries.h"
```
Replace the existing `SyncEngine` ctor declaration:
```cpp
    explicit SyncEngine(BackendRegistry *registry,
                              ISyncHost *host,
                              QObject *parent = nullptr);
```
with both overloads (new injecting one + retained default-bundle one):
```cpp
    /// Injecting ctor (preferred): the engine reads shape state from
    /// `shape`, which the caller must also have handed to the
    /// PluginManager that populated it. Per-engine isolation lives here.
    explicit SyncEngine(BackendRegistry *registry,
                              ISyncHost *host,
                              Kalburator::Shape::ShapeRegistries &shape,
                              QObject *parent = nullptr);

    /// Transitional overload: binds to the process-global default bundle
    /// (Ambient Context). Kept so existing consumers compile unchanged;
    /// scheduled for removal once they adopt the injecting ctor (FINDINGS O7).
    explicit SyncEngine(BackendRegistry *registry,
                              ISyncHost *host,
                              QObject *parent = nullptr);
```
Add the member alongside `m_registry` (after `BackendRegistry *m_registry;` near line 737):
```cpp
    Kalburator::Shape::ShapeRegistries &m_shape;
```

- [ ] **Step 2: Add the `const ShapeRegistries&` member + ctor arg to `SyncEngineWorker` (header)**

In `src/engine/syncengine.h`, replace the worker ctor declaration (~line 148):
```cpp
    explicit SyncEngineWorker(const TranscodingRouter &router, QObject *parent = nullptr);
```
with:
```cpp
    explicit SyncEngineWorker(const TranscodingRouter &router,
                              const Kalburator::Shape::ShapeRegistries &shape,
                              QObject *parent = nullptr);
```
Add the member next to `const TranscodingRouter &m_router;` (~line 326):
```cpp
    const Kalburator::Shape::ShapeRegistries &m_shape;
```

- [ ] **Step 3: Wire the ctors (cpp)**

In `src/engine/syncengine.cpp`, replace the engine ctor (lines 49–60) with the injecting definition plus a delegating default-bundle overload:
```cpp
SyncEngine::SyncEngine(BackendRegistry *registry,
                                   ISyncHost *host,
                                   Kalburator::Shape::ShapeRegistries &shape,
                                   QObject *parent)
    : QObject(parent)
    , m_registry(registry)
    , m_controller(host)
    , m_transcodingRouter(TranscodingRegistry::instance())
    , m_shape(shape)
{
    // Create worker but don't start thread yet
    m_worker = new SyncEngineWorker(m_transcodingRouter, m_shape);
    setupWorkerConnections();
}

SyncEngine::SyncEngine(BackendRegistry *registry,
                                   ISyncHost *host,
                                   QObject *parent)
    : SyncEngine(registry, host,
                 Kalburator::Shape::defaultShapeRegistries(), parent)
{
}
```
Add the include near the top of the file:
```cpp
#include "shape/shaperegistries.h"
```
Replace the worker ctor head (line 1304) and init list:
```cpp
SyncEngineWorker::SyncEngineWorker(const TranscodingRouter &router,
                                   const Kalburator::Shape::ShapeRegistries &shape,
                                   QObject *parent)
    : QObject(parent)
    , m_router(router)
    , m_shape(shape)
{
```
(leave the connect() body unchanged).

- [ ] **Step 4: Convert the seven worker read sites**

In `src/engine/syncengine.cpp`, make these exact substitutions (all inside `SyncEngineWorker` methods):

Line ~1403:
```cpp
                loss = m_shape.transformation.inspect(
                    srcShape, tgtShape);
```
Line ~1861:
```cpp
    auto *dd = m_shape.domain
                   .definitionFor(srcShape.domain);
```
Line ~1874:
```cpp
    auto *ops = m_shape.operations
                    .operationsFor(srcShape.domain);
```
Line ~1885:
```cpp
    const auto &reg = m_shape.transformation;
```
Line ~2417:
```cpp
    const auto &treg = m_shape.transformation;
```
Line ~2423:
```cpp
    auto *opsUCC = m_shape.operations
                       .operationsFor(srcShape.domain);
```
Line ~2641:
```cpp
    auto *ddUCC = m_shape.domain
                      .definitionFor(opsUCC->targetDomain());
```

- [ ] **Step 5: Build the library + the isolation test**

Run: `cmake --build /home/clinton/dev/libkalburator/build`
Expected: builds clean. (The Task 2 test still won't pass — it needs the PluginManager ctor from Task 4 — but the engine path now reads `m_shape`.)

- [ ] **Step 6: Run the full suite**

Run: `ctest --test-dir /home/clinton/dev/libkalburator/build`
Expected: same pass set as after Task 1 (default-bundle ctor preserves all behavior); `tst_engine_registry_injection` still fails to build/compile or is not yet green — acceptable, it lands in Task 4.

- [ ] **Step 7: Commit**

```bash
git add src/engine/syncengine.h src/engine/syncengine.cpp
git commit -m "engine: inject ShapeRegistries into SyncEngine + worker; convert 7 read sites (Plan 2 Task 3)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 4: Inject `ShapeRegistries` through `PluginManager`

**Files:**
- Modify: `src/plugin/pluginmanager.h:20` (ctor), `:55` (member)
- Modify: `src/plugin/pluginmanager.cpp:98` (ctor), `:127–131`, `:145–180`, `:187` (write/unwind sites)

- [ ] **Step 1: Add the injecting ctor + member (header)**

In `src/plugin/pluginmanager.h`, add the forward declaration / include. Near the existing `namespace Sync { class BackendRegistry; }`:
```cpp
namespace Shape { struct ShapeRegistries; }
```
Replace the ctor declaration (line 20):
```cpp
    explicit PluginManager(Sync::BackendRegistry *registry);
```
with:
```cpp
    /// Injecting ctor (preferred): contributions are registered into
    /// `shape`, which must be the same bundle the consuming SyncEngine reads.
    PluginManager(Sync::BackendRegistry *registry, Shape::ShapeRegistries &shape);

    /// Transitional overload binding to the process-global default bundle
    /// (Ambient Context; FINDINGS O7).
    explicit PluginManager(Sync::BackendRegistry *registry);
```
Add the member after `Sync::BackendRegistry *m_backendRegistry = nullptr;` (line 55):
```cpp
    Shape::ShapeRegistries &m_shape;
```

- [ ] **Step 2: Wire the ctors (cpp)**

In `src/plugin/pluginmanager.cpp`, add the include near the top:
```cpp
#include "shape/shaperegistries.h"
```
Replace the ctor (line 98):
```cpp
PluginManager::PluginManager(Sync::BackendRegistry *registry,
                             Shape::ShapeRegistries &shape)
    : m_backendRegistry(registry)
    , m_shape(shape)
{
    Q_ASSERT(registry);
}

PluginManager::PluginManager(Sync::BackendRegistry *registry)
    : PluginManager(registry, Shape::defaultShapeRegistries())
{
}
```

- [ ] **Step 3: Convert the write/unwind sites in `applyPlugin`**

In `src/plugin/pluginmanager.cpp`, substitute every `Shape::<Registry>::instance()` inside `applyPlugin` (lines ~127–199) with the injected members:

- `Shape::DomainOperationsRegistry::instance().unregister(d);` → `m_shape.operations.unregister(d);` (line ~127)
- `Shape::TransformationRegistry::instance().unregisterEdges(pending.edgesAdded);` → `m_shape.transformation.unregisterEdges(pending.edgesAdded);` (~128)
- `Shape::TransformationRegistry::instance().unregisterShapes(pending.shapesAdded);` → `m_shape.transformation.unregisterShapes(pending.shapesAdded);` (~129)
- `Shape::DomainRegistry::instance().unregisterDefinition(d);` → `m_shape.domain.unregisterDefinition(d);` (~131)
- `Shape::DomainRegistry::instance().registerDefinition(def)` → `m_shape.domain.registerDefinition(def)` (~145)
- `Shape::TransformationRegistry::instance().isFrozen(def->domain())` → `m_shape.transformation.isFrozen(def->domain())` (~149)
- `Shape::TransformationRegistry::instance().registerShape(def->canonicalShape(), def->canonicalCatalogue());` → `m_shape.transformation.registerShape(def->canonicalShape(), def->canonicalCatalogue());` (~152)
- `Shape::TransformationRegistry::instance().declareCanonical(def->domain(), def->canonicalShape());` → `m_shape.transformation.declareCanonical(def->domain(), def->canonicalShape());` (~153)
- `Shape::TransformationRegistry::instance().isFrozen(sc->targetDomain())` → `m_shape.transformation.isFrozen(sc->targetDomain())` (~163)
- `Shape::TransformationRegistry::instance().registerShape(pair.first, pair.second);` → `m_shape.transformation.registerShape(pair.first, pair.second);` (~168)
- `Shape::TransformationRegistry::instance().catalogueFor(s) != nullptr;` → `m_shape.transformation.catalogueFor(s) != nullptr;` (~175)
- `Shape::TransformationRegistry::instance().registerEdge(edge);` → `m_shape.transformation.registerEdge(edge);` (~180)
- `Shape::DomainOperationsRegistry::instance().registerOperations(ops)` → `m_shape.operations.registerOperations(ops)` (~187)

Verify none remain. Run: `grep -n "Shape::TransformationRegistry::instance()\|Shape::DomainRegistry::instance()\|Shape::DomainOperationsRegistry::instance()" src/plugin/pluginmanager.cpp`
Expected: no output.

- [ ] **Step 4: Build the library + the isolation test**

Run: `cmake --build /home/clinton/dev/libkalburator/build --target tst_engine_registry_injection`
Expected: now compiles (the injecting `PluginManager` ctor exists).

- [ ] **Step 5: Run the isolation test; verify it PASSES**

Run: `ctest --test-dir /home/clinton/dev/libkalburator/build -R tst_engine_registry_injection -VV`
Expected: PASS — bundle A routes `vcard3→vcard4`, bundle B does not. The capability shown red in Task 2 Step 3 is now green.

- [ ] **Step 6: Run the full suite**

Run: `ctest --test-dir /home/clinton/dev/libkalburator/build`
Expected: all previously-green tests still green (default-bundle overload preserves behavior for unconverted callers).

- [ ] **Step 7: Commit**

```bash
git add src/plugin/pluginmanager.h src/plugin/pluginmanager.cpp \
        tests/engine/tst_engine_registry_injection.cpp
git commit -m "plugin: inject ShapeRegistries into PluginManager; isolation test green (Plan 2 Task 4)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Test conversion (Tasks 6–11): two reference patterns

The remaining tasks convert the 31 `clear()`-ritual files to inject a fixture-owned bundle so isolation is lexical scope, not teardown. Every file follows one of two patterns. Apply the matching pattern; the per-file steps give exact files and the `clear()` lines to delete.

### Reference Pattern A — integration tests (build a `SyncEngine` and/or a `PluginManager`)

These have an `initTestCase()`/`init()` that loads plugins and per-test code that constructs `SyncEngine engine(&registry, &host)`. Convert as follows.

*Before* (representative, from `tst_engine_unified_routing.cpp`):
```cpp
void TestEngineUnifiedRouting::initTestCase()
{
    Kalburator::Sync::BackendRegistry pmRegistry;
    Kalburator::PluginManager pm(&pmRegistry);
    Kalburator::registerStockPlugins(pm);
}
void TestEngineUnifiedRouting::cleanupTestCase()
{
    TransformationRegistry::instance().clear();
    DomainRegistry::instance().clear();
    Kalburator::Shape::DomainOperationsRegistry::instance().clear();
}
// ... later, per test:
    SyncEngine engine(&registry, &host);
```

*After*:
```cpp
// add include:
#include "shape/shaperegistries.h"

// add a fixture member (private section of the test class):
    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry m_pmRegistry;  // owns plugin backend contributions for m_shape's lifetime

void TestEngineUnifiedRouting::initTestCase()
{
    Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
    Kalburator::registerStockPlugins(pm);
}
// DELETE cleanupTestCase()'s three clear() lines. If cleanupTestCase()
// becomes empty, delete the method and its declaration too.

// ... later, per test, inject the bundle:
    SyncEngine engine(&registry, &host, m_shape);
```

Notes:
- The bundle must be a **fixture member** (not a local in `initTestCase`) so the registrations survive into the test slots. The old code relied on the global persisting; the bundle replaces that role with member lifetime.
- `m_pmRegistry` must also become a fixture member if plugin **backend** contributions are read later (the old local `pmRegistry` was discarded; backend contributions went to a throwaway registry). If the test never uses backend contributions from stock plugins, a local `BackendRegistry` in `initTestCase` is fine — keep it local in that case.
- Every `SyncEngine(&x, &y)` construction in the file gains `, m_shape`.

### Reference Pattern B — registry/unit tests (use `::instance()` directly)

These grab `auto& r = TransformationRegistry::instance();` and register/query directly.

*Before* (from `tst_transformation_registry.cpp`):
```cpp
    void cleanup() { /* ... */ }
    void init() { TransformationRegistry::instance().clear(); }
    void someCase() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(/* ... */);
    }
```
(or `clear()` calls in `cleanup()`/`cleanupTestCase()`).

*After*:
```cpp
#include "shape/shaperegistries.h"

    // fixture member:
    Kalburator::Shape::ShapeRegistries m_shape;

    // DELETE every ::instance().clear() line in init()/cleanup()/cleanupTestCase().
    // If a method becomes empty, delete it.

    void someCase() {
        auto& r = m_shape.transformation;   // was TransformationRegistry::instance()
        r.registerShape(/* ... */);
    }
```
Substitute member-by-member: `TransformationRegistry::instance()` → `m_shape.transformation`, `DomainRegistry::instance()` → `m_shape.domain`, `DomainOperationsRegistry::instance()` → `m_shape.operations`. Because `m_shape` is a per-test-object member and QtTest constructs one test object per class, isolation between slots that previously relied on `init()`-time `clear()` must be re-checked: if multiple slots in the same class mutate `m_shape` and depend on a clean slate, reset by making `m_shape` a local in each slot instead of a member, OR keep a `void init(){ m_shape = {}; }` (assign a fresh bundle) — choose per file based on whether slots share setup.

---

## Task 6: Convert `tests/shape/` unit tests (Pattern B)

**Files (delete `clear()`; substitute `::instance()` → `m_shape.<member>`):**
- `tests/shape/tst_transformation_registry.cpp` — `clear()` at line 40; ~13 `instance()` query sites (44–179)
- `tests/shape/tst_domain_registry.cpp` — `clear()` at 36–37; query sites 41–71
- `tests/shape/tst_canonical_spine.cpp` — `clear()` in `cleanup()` at 63; query sites 66–187
- `tests/shape/tst_dynamic_domain_registration.cpp` — `clear()` at 43–44; `instance()` throughout (49–138), incl. `setupOfficeDomain(TransformationRegistry::instance())` → `setupOfficeDomain(m_shape.transformation)`
- `tests/shape/tst_domain_operations_registry.cpp` — `clear()` in `cleanup()` at 28 (`DomainOperationsRegistry::instance().clear()`); substitute its query sites to `m_shape.operations`

- [ ] **Step 1: Apply Reference Pattern B to each file above.** For `tst_dynamic_domain_registration.cpp` and `tst_canonical_spine.cpp`, slots mutate the registry and rely on a clean slate per slot — add `void init() { m_shape = {}; }` (replacing the old `clear()` call) so each slot starts fresh.

- [ ] **Step 2: Build the shape tests**

Run: `cmake --build /home/clinton/dev/libkalburator/build` (or target-by-target: `--target tst_transformation_registry` etc.)
Expected: clean build.

- [ ] **Step 3: Run the shape tests**

Run: `ctest --test-dir /home/clinton/dev/libkalburator/build -R "transformation_registry|domain_registry|canonical_spine|dynamic_domain_registration|domain_operations_registry"`
Expected: all PASS.

- [ ] **Step 4: Confirm no `clear()` ritual remains in `tests/shape/`**

Run: `grep -rn "Registry::instance().clear()" tests/shape/`
Expected: no output.

- [ ] **Step 5: Commit**

```bash
git add tests/shape/
git commit -m "test(shape): inject ShapeRegistries; drop clear() rituals (Plan 2 Task 6)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 7: Convert `tests/plugin/` tests (Pattern A/B mix)

**Files:**
- `tests/plugin/tst_pluginmanager_load.cpp` — `clear()` 28–30; constructs `PluginManager` → Pattern A (inject `m_shape`); assertions on `Shape::DomainRegistry::instance()` → `m_shape.domain`
- `tests/plugin/scenarios/tst_docstogo_scenario.cpp` — `clear()` 23–25; `PluginManager` + `Shape::TransformationRegistry::instance().compile(...)` at 44 → `m_shape.transformation.compile(...)`
- `tests/plugin/tst_stock_plugins.cpp` — `clear()` 18–21; assertions on `Kalburator::Shape::DomainRegistry::instance().definitionFor(...)` / `...TransformationRegistry::instance().edgesFrom(...)` → `m_shape.domain` / `m_shape.transformation`

- [ ] **Step 1: Apply the patterns.** Each constructs a `PluginManager` — give it `m_shape` (fixture member) and a fixture `BackendRegistry` member; route all post-load `::instance()` assertions to `m_shape.<member>`; delete the `clear()` lines.

- [ ] **Step 2: Build + run**

Run: `cmake --build /home/clinton/dev/libkalburator/build && ctest --test-dir /home/clinton/dev/libkalburator/build -R "pluginmanager_load|docstogo_scenario|stock_plugins"`
Expected: all PASS.

- [ ] **Step 3: Confirm + commit**

Run: `grep -rn "Registry::instance().clear()" tests/plugin/` → expect no output.
```bash
git add tests/plugin/
git commit -m "test(plugin): inject ShapeRegistries; drop clear() rituals (Plan 2 Task 7)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 8: Convert `tests/engine/` integration tests (Pattern A)

**Files (all Pattern A — inject `m_shape` into PluginManager in init + into every `SyncEngine(...)`; delete `clear()`):**
- `tests/engine/tst_engine_unified_routing.cpp` — `clear()` 311–313; engine ctor at 346 (+ any others)
- `tests/engine/tst_carddav_engine_integration.cpp` — `clear()` 319–321; engine ctors at 384, 515
- `tests/engine/tst_engine_universal_sink_dispatch.cpp` — `clear()` 235–237; engine ctor at 272
- `tests/engine/tst_engine_silent_success_guard.cpp` — `clear()` 95–97
- `tests/engine/tst_mass_delete_guard.cpp` — `clear()` 98–100
- `tests/engine/tst_cancellation_reason.cpp` — `clear()` 77–79

- [ ] **Step 1: Apply Reference Pattern A to each file.** Add `#include "shape/shaperegistries.h"`, a `Kalburator::Shape::ShapeRegistries m_shape;` fixture member, inject it into the `PluginManager` built in `initTestCase`/`init`, append `, m_shape` to every `SyncEngine(&registry, &host)` construction, and delete the three `clear()` lines (drop the method if it becomes empty). Find every engine construction per file with: `grep -n "SyncEngine .*(" tests/engine/<file>`.

- [ ] **Step 2: Build + run the engine tests**

Run: `cmake --build /home/clinton/dev/libkalburator/build && ctest --test-dir /home/clinton/dev/libkalburator/build -R "tst_engine_|tst_carddav_|tst_cancellation_reason|tst_mass_delete"`
Expected: all PASS.

- [ ] **Step 3: Confirm + commit**

Run: `grep -rn "Registry::instance().clear()" tests/engine/` → expect no output.
```bash
git add tests/engine/
git commit -m "test(engine): inject ShapeRegistries; drop clear() rituals (Plan 2 Task 8)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 9: Convert `tests/calendar/` integration tests (Pattern A)

**Files (Pattern A):**
- `tests/calendar/tst_calendar_sync_full.cpp` — `clear()` 111–113
- `tests/calendar/tst_calendar_conflict.cpp` — `clear()` 99–101
- `tests/calendar/tst_calendar_sync_error_recovery.cpp` — `clear()` 96–98
- `tests/calendar/tst_calendar_first_sync_via_blob_engine.cpp` — `clear()` 136–138
- `tests/calendar/tst_calendar_subsequent_sync_uses_blob_view.cpp` — `clear()` 158–160
- `tests/calendar/tst_calendar_transcoding_warning.cpp` — `clear()` 141–143 (note: also clears `TranscodingRegistry` at 213 — leave that one; it's Plan 4's)
- `tests/calendar/tst_engine_cancellation.cpp` — `clear()` 118–120
- `tests/calendar/tst_engine_subset_dispatch.cpp` — `clear()` 61–63
- `tests/calendar/tst_engine_unified_boundary.cpp` — `clear()` 90–92
- `tests/calendar/plugin/tst_calendar_plugin.cpp` — `clear()` 106–107

- [ ] **Step 1: Apply Reference Pattern A to each file.** Same procedure as Task 8. In `tst_calendar_transcoding_warning.cpp`, delete only the three `Shape::` `clear()` lines (141–143); **keep** `TranscodingRegistry::instance().clear()` at line 213 (out of scope until Plan 4).

- [ ] **Step 2: Build + run the calendar tests**

Run: `cmake --build /home/clinton/dev/libkalburator/build && ctest --test-dir /home/clinton/dev/libkalburator/build -R "calendar|tst_engine_cancellation|tst_engine_subset_dispatch|tst_engine_unified_boundary"`
Expected: all PASS.

- [ ] **Step 3: Confirm + commit**

Run: `grep -rn "TransformationRegistry::instance().clear()\|DomainRegistry::instance().clear()" tests/calendar/` → expect no output (the remaining hit is `TranscodingRegistry`, intentionally kept).
```bash
git add tests/calendar/
git commit -m "test(calendar): inject ShapeRegistries; drop clear() rituals (Plan 2 Task 9)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 10: Convert `tests/contacts/` integration tests (Pattern A)

**Files (Pattern A):**
- `tests/contacts/tst_contacts_engine_witness.cpp` — `clear()` 312–314
- `tests/contacts/tst_unified_askuser_pause.cpp` — `clear()` 276–278
- `tests/contacts/tst_unified_custom_merge.cpp` — `clear()` 280–282
- `tests/contacts/tst_vcard3_vcard4_edge.cpp` — `clear()` 38–39 (Pattern B-ish: also registers via `TransformationRegistry::instance()` at 84 → `m_shape.transformation`)
- `tests/contacts/tst_vcard_plugin.cpp` — `clear()` 20–21 (Pattern A: builds PluginManager)

- [ ] **Step 1: Apply the matching reference pattern to each file** (A for engine/plugin builders; B substitution for `tst_vcard3_vcard4_edge.cpp`'s direct `instance()` registration at line 84). Delete the `clear()` lines.

- [ ] **Step 2: Build + run**

Run: `cmake --build /home/clinton/dev/libkalburator/build && ctest --test-dir /home/clinton/dev/libkalburator/build -R "contacts|vcard"`
Expected: all PASS.

- [ ] **Step 3: Confirm + commit**

Run: `grep -rn "Registry::instance().clear()" tests/contacts/` → expect no output.
```bash
git add tests/contacts/
git commit -m "test(contacts): inject ShapeRegistries; drop clear() rituals (Plan 2 Task 10)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 11: Convert `tests/todo/`, `tests/memo/`, `tests/blob/` (Pattern A)

**Files (Pattern A — each builds a PluginManager and/or SyncEngine):**
- `tests/todo/tst_vtodo_plugin.cpp` — `clear()` 20–21
- `tests/memo/tst_memo_plugin.cpp` — `clear()` 19–20
- `tests/blob/tst_engine_mirror_direction.cpp` — `clear()` 276–278

- [ ] **Step 1: Apply Reference Pattern A to each file.** Delete the `clear()` lines.

- [ ] **Step 2: Build + run**

Run: `cmake --build /home/clinton/dev/libkalburator/build && ctest --test-dir /home/clinton/dev/libkalburator/build -R "vtodo_plugin|memo_plugin|mirror_direction"`
Expected: all PASS.

- [ ] **Step 3: Confirm + commit**

Run: `grep -rn "Registry::instance().clear()" tests/todo/ tests/memo/ tests/blob/` → expect no output.
```bash
git add tests/todo/ tests/memo/ tests/blob/
git commit -m "test(todo,memo,blob): inject ShapeRegistries; drop clear() rituals (Plan 2 Task 11)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 12: Final verification + status update

**Files:**
- Modify: `docs/campaign/STATUS.md`

- [ ] **Step 1: Prove the engine path holds no `::instance()` and no `clear()` ritual survives**

Run:
```bash
grep -rn "TransformationRegistry::instance()\|DomainRegistry::instance()\|DomainOperationsRegistry::instance()" src/engine/ src/plugin/
grep -rn "TransformationRegistry::instance().clear()\|DomainRegistry::instance().clear()\|DomainOperationsRegistry::instance().clear()" tests/
```
Expected: **first** command — no output (engine + plugin reach shape state only via `m_shape`). **Second** command — no output (all 31 `Shape::` `clear()` rituals removed; the lone surviving `TranscodingRegistry::instance().clear()` in `tst_calendar_transcoding_warning.cpp` is intentionally out of scope).

Confirm the Ambient-Context scaffold is the *only* remaining `instance()` surface:
```bash
grep -rn "::instance()" src/shape/transformationregistry.cpp src/shape/domainregistry.cpp src/shape/domainoperationsregistry.cpp
```
Expected: exactly the three delegating definitions — the documented scaffolding (FINDINGS O7).

- [ ] **Step 2: Full clean build + full suite**

Run:
```bash
rm -rf /home/clinton/dev/libkalburator/build
cmake -S /home/clinton/dev/libkalburator -B /home/clinton/dev/libkalburator/build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build /home/clinton/dev/libkalburator/build
ctest --test-dir /home/clinton/dev/libkalburator/build
```
Expected: clean build; full suite green (same pass set as Plan 1 baseline, plus the new `tst_engine_registry_injection`).

- [ ] **Step 3: Update STATUS**

In `docs/campaign/STATUS.md`: set the Plan 2 row status to **Complete**; update the Status line (top) and the "Next action" block to point at Plan 3; add a one-line note that FINDINGS O7 (remove the Ambient-Context default) remains open for the downstream port.

- [ ] **Step 4: Commit**

```bash
git add docs/campaign/STATUS.md
git commit -m "docs: Plan 2 complete — per-engine ShapeRegistries injection landed (Plan 2 Task 12)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Self-review notes (author)

- **Spec coverage (design §8):** ShapeRegistries bundle of all three registries (Task 1) ✓; injected into SyncEngine+worker (Task 3) and PluginManager (Task 4) ✓; Ambient-Context default + retained ctors for downstream (Tasks 1,3,4) ✓; `BackendRegistry` left separate ✓ (non-goal, untouched); `clear()` rituals removed (Tasks 6–11) ✓; DomainOperationsRegistry included ✓ (FINDINGS O6).
- **Invariant 5 (falsifiable test):** Task 2 writes the isolation test and shows it red (no-compile) before Tasks 3–4 make it green ✓.
- **Invariant 6 (production path):** engine-path coverage via the converted integration tests in Tasks 8–10, which drive `SyncEngine(&reg,&host,m_shape)` and would fail if the worker ignored `m_shape` ✓.
- **Invariant 10 (downstream green):** default-bundle ctor overloads keep PlanStan/WildPalms compiling; removal deferred to FINDINGS O7 ✓.
- **Bulk-edit structure:** Tasks 6–11 use two fully-worked reference patterns + explicit per-file line numbers rather than 31 verbatim diffs. This is a deliberate deviation from "repeat all code" justified by 31 mechanically-identical conversions; each file's exact target lines and pattern are named so the engineer has what they need.
- **Open risk:** per-file `init()` reset for Pattern-B slots that share `m_shape` — flagged inline in Task 6 Step 1; the executor must check whether slots depend on a clean slate and use `m_shape = {}` in `init()` where they do.
