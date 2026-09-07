# Phase G — Shape pipeline architecture — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans` to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land Phase G of the engine-merger refactor — scoped
shape-pipeline architecture per `04r-phase-g-design.md` — through
three checkpoint tags: `v0.15-phase-g-foundations`,
`v0.15.5-phase-g-engine-unified`, `v0.16-phase-g-shape-pipeline`.
Estimated 15-17 weeks with parallelism.

**Architecture:** The engine deals in records-with-shape via a
`TransformationRegistry`. Domain plugins (calendar, contacts,
memo, todo) own canonical-shape edges hub-and-spoke. Backends
declare native shapes and a `resourceId()` for exclusivity
scheduling. A `MappingScheduler` queues mappings by resource
graph (capacity-1 per resource v1). `SyncEngineFuture` wraps
`QFuture<QList<SyncResult>>` with `cancelWithReason()`. WildPalms
transforms into a peer multi-PIM consumer; `SyncRunner_wp`
dissolves into a ~50-line `HotSyncCoordinator`. Cross-domain edges
NOT registered in v1 stock library (architecture supports them).

**Tech stack:** C++20, Qt 6, KCalendarCore, KContacts (new), CMake,
QTest. Three worktrees: libkalburator (target), PlanStan
(consumer #1), WildPalms (consumer #2). All on branch
`refactor/engine-merger`.

**Worktree paths:**
- libkalburator: `~/dev/refactor-engine-merger/libkalburator/`
- PlanStan: `~/dev/refactor-engine-merger/PlanStan/`
- WildPalms: `~/dev/refactor-engine-merger/WildPalms/`

**Build dirs:**
- libkalburator: `build/` (legacy, no presets)
- PlanStan: `build-dev/` (uses `cmake --preset dev`)
- WildPalms: `build/` (legacy, with `-DWILDPALMS_*_PLUGIN_V2=ON`)

**Canonical verification:**
`~/dev/refactor-engine-merger/scripts/verify-all.sh` exits 0 at
every group boundary. Builds + tests all three worktrees;
compares to `~/dev/refactor-engine-merger/baselines/`.

**Per-commit policy:** Commits to `refactor/engine-merger` do NOT
require per-commit user authorisation. Tags DO require user
authorisation. `git push`, `git reset --hard`, `git branch -D`,
and any operation on pristine `~/dev/{libkalburator,PlanStan,
WildPalms}` checkouts also require user authorisation.

**Reading order before starting:**

1. `04r-phase-g-design.md` (canonical spec; design references
   throughout this plan)
2. `04r-phase-g-walkthrough.md`, `…-wildpalms.md`, `…-migration.md`
   (background; not strictly required but provides intuition)
3. `~/dev/refactor-engine-merger/CLAUDE.md` (refactor-folder
   ground rules)
4. `~/dev/refactor-engine-merger/OPERATIONS.md` (deprecation-with-
   overlap pattern; per-phase rhythm)
5. `~/dev/refactor-engine-merger/FINDINGS.md` (cross-cutting
   lessons; especially the F1 SQLite migration footgun and the
   F2 Qt6 QFuture quirks)

---

## Group 0 — Pre-flight

### Task 1: Design doc review and worktree state confirmation

**Files:** read-only.

**Background:** Before any code changes, confirm the worktrees are
in the expected state and that the design doc is internally
consistent.

- [ ] **Step 1: Confirm worktree branch and clean state**

```bash
cd ~/dev/refactor-engine-merger/
for repo in libkalburator PlanStan WildPalms; do
    echo "=== $repo ==="
    git -C $repo rev-parse --abbrev-ref HEAD
    git -C $repo status -s
done
```

Expected: each repo on `refactor/engine-merger`; status clean
(or only locally-staged plan-doc changes).

- [ ] **Step 2: Confirm F2 tag is applied**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator tag -l v0.14-phase-f2-threading
```

Expected: tag exists. If missing, halt — Phase G assumes F2
is fully landed.

- [ ] **Step 3: Confirm verify-all green at start**

```bash
~/dev/refactor-engine-merger/scripts/verify-all.sh
```

Expected: exit 0. If non-zero, halt and triage; do not start
Phase G work on a red baseline.

- [ ] **Step 4: Read design doc end-to-end**

Open `~/dev/refactor-engine-merger/libkalburator/docs/phase0/04r-phase-g-design.md`.
Verify you understand:

- The hub-and-spoke topology
- That cross-domain edges are NOT in v1 stock
- The 5-stage worker flow (fetch → promote → diff → merge → push)
- The `Shape::Any` semantics
- The mapping-keyed baseline schema (v3)
- The MappingScheduler's resource graph model
- The `SyncEngineFuture` + `CancellationReason` shape

**Verification:**

- [ ] verify-all green
- [ ] design doc read; agent can paraphrase the architecture

### Task 2: Establish Phase G status doc

**Files:**
- Create: `~/dev/refactor-engine-merger/libkalburator/docs/phase0/04r-phase-g-status.md`

**Background:** Per libkalburator's CLAUDE.md, "Phase-status docs
are living documents." Create the file now so each task can update
it as work lands.

- [ ] **Step 1: Create the status file**

Initial content (template):

```markdown
# Phase G — Status

**Tag target #1:** `v0.15-phase-g-foundations` (after G.4)
**Tag target #2:** `v0.15.5-phase-g-engine-unified` (after G.8)
**Tag target #3:** `v0.16-phase-g-shape-pipeline` (after G.10)

**Sub-phase status:**

- G.1 Foundations — ⬜ not started
- G.2 Calendar plugin — ⬜ not started
- G.3 Backend interface migration — ⬜ not started
- G.4 Mapping-keyed baselines — ⬜ not started
- G.5 New domain plugins — ⬜ not started
- G.6 BlobDomainAdapter dispatch + MappingScheduler — ⬜ not started
- G.7 WildPalms transformation — ⬜ not started
- G.8 F1 facade deletion + universal sinks — ⬜ not started
- G.9 ISyncHost narrowing + sync I/O retirement — ⬜ not started
- G.10 Loss profile UX + new stock backends — ⬜ not started

**Last task completed:** Task 2 (status doc creation)
**Next task:** Task 3 (build dir and clangd setup)

## Tasks completed this phase

- Task 1 (preflight)
- Task 2 (status doc)
```

- [ ] **Step 2: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator add docs/phase0/04r-phase-g-status.md
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "docs(phase0): Phase G status doc skeleton (Task 2)"
```

**Verification:**

- [ ] File exists; commit landed; verify-all unchanged

### Task 3: Confirm build dirs and clangd config

**Files:**
- Verify: `~/dev/refactor-engine-merger/libkalburator/build/compile_commands.json`
- Verify: `~/dev/refactor-engine-merger/libkalburator/.clangd`

**Background:** Per the user-level CLAUDE.md ("Qt6/C++ clangd
Setup" section), every project needs a clean clangd config for
agent-friendly editing. libkalburator uses `build/` (legacy, no
presets).

- [ ] **Step 1: Verify build dir exists**

```bash
ls -la ~/dev/refactor-engine-merger/libkalburator/build/CMakeCache.txt
ls -la ~/dev/refactor-engine-merger/libkalburator/build/compile_commands.json
```

If missing or stale, regenerate:

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

- [ ] **Step 2: Verify .clangd**

```bash
cat ~/dev/refactor-engine-merger/libkalburator/.clangd
```

Expected: contains `CompilationDatabase: build`. If missing,
create or fix per the `setup-clangd` skill's guidance.

- [ ] **Step 3: Verify the symlink**

```bash
ls -la ~/dev/refactor-engine-merger/libkalburator/compile_commands.json
```

Expected: symlink to `build/compile_commands.json`. If missing:

```bash
cd ~/dev/refactor-engine-merger/libkalburator
ln -sf build/compile_commands.json compile_commands.json
```

**Verification:**

- [ ] Clangd can resolve includes when probing a libkalburator source file

---

## Group 1 — G.1 Foundations (~2 weeks)

Lands the architectural primitives: `Shape`, `PropertyId`,
`PropertyCatalogue`, `LossProfile`, `TransformationEdge`,
`Pipeline`, `TransformationRegistry`. No edges registered yet;
no behavioural change. Existing engine continues to work via
`IDomainAdapter`.

### Task 4: Add `Shape` value type

**Files:**
- Create: `~/dev/refactor-engine-merger/libkalburator/src/shape/shape.h`
- Create: `~/dev/refactor-engine-merger/libkalburator/src/shape/shape.cpp`
- Create: `~/dev/refactor-engine-merger/libkalburator/src/shape/CMakeLists.txt`
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/CMakeLists.txt` (add `add_subdirectory(shape)`)

**Background:** `Shape` is the foundational `(domain, encoding)`
value type. `Shape::Any()` is a real sentinel value (not a
wildcard) for universal sinks. See design doc § "Shape" for the
full contract.

- [ ] **Step 1: Create shape.h with the `Kalburator::Shape` namespace**

Implement per design doc § "Shape" verbatim:

```cpp
#pragma once

#include <QHash>
#include <QString>

namespace Kalburator::Shape {

class DomainId {
    QString m_id;
public:
    DomainId() = default;
    explicit DomainId(QString id) : m_id(std::move(id)) {}
    QString toString() const { return m_id; }
    bool operator==(const DomainId&) const = default;
    bool operator!=(const DomainId&) const = default;
};

class EncodingId {
    QString m_id;
public:
    EncodingId() = default;
    explicit EncodingId(QString id) : m_id(std::move(id)) {}
    QString toString() const { return m_id; }
    bool operator==(const EncodingId&) const = default;
    bool operator!=(const EncodingId&) const = default;
};

struct Shape {
    DomainId domain;
    EncodingId encoding;

    static Shape Any();
    bool isAny() const noexcept;

    bool operator==(const Shape&) const = default;
    bool operator!=(const Shape&) const = default;
    QString toString() const;
};

uint qHash(const DomainId&, uint seed = 0) noexcept;
uint qHash(const EncodingId&, uint seed = 0) noexcept;
uint qHash(const Shape&, uint seed = 0) noexcept;

}  // namespace Kalburator::Shape
```

- [ ] **Step 2: Implement shape.cpp**

```cpp
#include "shape.h"

namespace Kalburator::Shape {

namespace {
constexpr const char* kAnySentinel = "__any__";
}

Shape Shape::Any() {
    return Shape{ DomainId{QStringLiteral(kAnySentinel)},
                  EncodingId{QStringLiteral(kAnySentinel)} };
}

bool Shape::isAny() const noexcept {
    return domain.toString() == QLatin1String(kAnySentinel) &&
           encoding.toString() == QLatin1String(kAnySentinel);
}

QString Shape::toString() const {
    if (isAny()) return QStringLiteral("any");
    return domain.toString() + QLatin1Char('+') + encoding.toString();
}

uint qHash(const DomainId& d, uint seed) noexcept {
    return qHash(d.toString(), seed);
}
uint qHash(const EncodingId& e, uint seed) noexcept {
    return qHash(e.toString(), seed);
}
uint qHash(const Shape& s, uint seed) noexcept {
    return qHash(s.domain, seed) ^ qHash(s.encoding, seed << 1);
}

}  // namespace
```

- [ ] **Step 3: CMakeLists for the new src/shape/ subdir**

```cmake
target_sources(kalburator PRIVATE
    shape.cpp
)
target_include_directories(kalburator PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
```

- [ ] **Step 4: Add `add_subdirectory(shape)` to libkalburator/src/CMakeLists.txt**

Place near other `add_subdirectory(...)` lines for src
sub-modules; keep alphabetical or insertion-order per existing
convention.

- [ ] **Step 5: Build**

```bash
cmake --build ~/dev/refactor-engine-merger/libkalburator/build
```

Expected: clean build.

- [ ] **Step 6: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator add src/shape/
git -C ~/dev/refactor-engine-merger/libkalburator add src/CMakeLists.txt
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "feat(shape): add Shape value type (G.1 Task 4)"
```

**Verification:**

- [ ] Build clean
- [ ] verify-all unchanged (no test impact yet)

### Task 5: Add `tst_shape.cpp` smoke tests

**Files:**
- Create: `~/dev/refactor-engine-merger/libkalburator/tests/shape/tst_shape.cpp`
- Create: `~/dev/refactor-engine-merger/libkalburator/tests/shape/CMakeLists.txt`
- Modify: `~/dev/refactor-engine-merger/libkalburator/tests/CMakeLists.txt`

**Background:** Pin `Shape`'s contract: equality, hashability,
`Shape::Any()` semantics, `toString` round-trip.

- [ ] **Step 1: Write the test**

```cpp
#include <QTest>
#include <QHash>
#include "shape.h"

using namespace Kalburator::Shape;

class TestShape : public QObject {
    Q_OBJECT
private slots:
    void equality() {
        Shape a{ DomainId{"calendar"}, EncodingId{"ical"} };
        Shape b{ DomainId{"calendar"}, EncodingId{"ical"} };
        Shape c{ DomainId{"contacts"}, EncodingId{"vcard"} };
        QCOMPARE(a, b);
        QVERIFY(a != c);
    }

    void anySentinel() {
        Shape a = Shape::Any();
        Shape b = Shape::Any();
        QCOMPARE(a, b);
        QVERIFY(a.isAny());
        Shape c{ DomainId{"calendar"}, EncodingId{"ical"} };
        QVERIFY(!c.isAny());
    }

    void toStringFormat() {
        Shape s{ DomainId{"calendar"}, EncodingId{"ical"} };
        QCOMPARE(s.toString(), QStringLiteral("calendar+ical"));
        QCOMPARE(Shape::Any().toString(), QStringLiteral("any"));
    }

    void hashable() {
        QHash<Shape, int> h;
        h.insert(Shape{ DomainId{"calendar"}, EncodingId{"ical"} }, 1);
        h.insert(Shape::Any(), 2);
        QCOMPARE(h.value(Shape{ DomainId{"calendar"}, EncodingId{"ical"} }), 1);
        QCOMPARE(h.value(Shape::Any()), 2);
        QCOMPARE(h.size(), 2);
    }
};

QTEST_GUILESS_MAIN(TestShape)
#include "tst_shape.moc"
```

- [ ] **Step 2: tests/shape/CMakeLists.txt**

Define a helper macro `kalburator_add_shape_test(name)` (mirroring
existing `kalburator_add_calendar_test` etc. patterns):

```cmake
function(kalburator_add_shape_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE kalburator Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

kalburator_add_shape_test(tst_shape)
```

- [ ] **Step 3: tests/CMakeLists.txt — add the subdir**

```cmake
add_subdirectory(shape)
```

- [ ] **Step 4: Build and run**

```bash
cmake --build ~/dev/refactor-engine-merger/libkalburator/build
ctest --test-dir ~/dev/refactor-engine-merger/libkalburator/build -R tst_shape -V
```

Expected: 4/4 tests pass.

- [ ] **Step 5: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator add tests/shape tests/CMakeLists.txt
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "test(shape): pin Shape contract (G.1 Task 5)"
```

**Verification:**

- [ ] tst_shape: 4/4 pass
- [ ] verify-all: libkalburator now 27/27 (was 26/26)

### Task 6: Add `PropertyId`, `PropertyKind`, `PropertyDescriptor`, `PropertyCatalogue`

**Files:**
- Create: `~/dev/refactor-engine-merger/libkalburator/src/shape/propertycatalogue.h`
- Create: `~/dev/refactor-engine-merger/libkalburator/src/shape/propertycatalogue.cpp`
- Create: `~/dev/refactor-engine-merger/libkalburator/tests/shape/tst_property_catalogue.cpp`
- Modify: `src/shape/CMakeLists.txt`, `tests/shape/CMakeLists.txt`

**Background:** Per design doc § "PropertyId and PropertyCatalogue".
Each shape registered with the engine has exactly one catalogue.

- [ ] **Step 1: Implement propertycatalogue.h**

Per the design doc spec. Includes `PropertyId` (string-id wrapper),
`PropertyKind` enum, `PropertyDescriptor` struct, `PropertyCatalogue`
class with `properties()`, `hasProperty()`, `find()`,
`sqlColumnDdl()`.

- [ ] **Step 2: Implement propertycatalogue.cpp**

`sqlColumnDdl()` returns one DDL fragment per property, mapping
`PropertyKind` to SQLite types:
- `String`/`StringList`/`Json` → `TEXT`
- `Integer`/`Boolean` → `INTEGER`
- `DateTime`/`Duration` → `TEXT` (ISO 8601)
- `Bytes` → `BLOB`

Format: `<id> <type> [NOT NULL]` per descriptor.

- [ ] **Step 3: Write tst_property_catalogue.cpp**

Test cases:
- Empty catalogue
- Add/find/has property
- Multiple properties; `properties()` order preserved
- `sqlColumnDdl()` produces expected DDL for each `PropertyKind`
- Optional vs required reflected in DDL (`NOT NULL` only for required)

- [ ] **Step 4: Build, run, commit**

```bash
cmake --build ~/dev/refactor-engine-merger/libkalburator/build
ctest --test-dir ~/dev/refactor-engine-merger/libkalburator/build -R tst_property_catalogue -V

git -C ~/dev/refactor-engine-merger/libkalburator add src/shape tests/shape
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "feat(shape): add PropertyId, PropertyCatalogue (G.1 Task 6)"
```

**Verification:**

- [ ] tst_property_catalogue passes (target ~6 cases)
- [ ] libkalburator now 28/28

### Task 7: Add `LossProfile`

**Files:**
- Create: `~/dev/refactor-engine-merger/libkalburator/src/shape/lossprofile.h`
- Create: `~/dev/refactor-engine-merger/libkalburator/src/shape/lossprofile.cpp`
- Create: `~/dev/refactor-engine-merger/libkalburator/tests/shape/tst_loss_profile.cpp`

**Background:** Per design doc § "LossProfile".

- [ ] **Step 1: Implement per design doc spec**

Includes `LossLevel` enum, `LossProfile` struct,
`LossProfile::compose()` method (max level + union of dropped sets),
`LossProfile::summary()` (human-readable string).

- [ ] **Step 2: Test cases**

- Default-constructed is `Lossless` with empty `dropped`
- `compose(Lossless, Lossless) → Lossless`
- `compose(Lossless, IntraDomainLossy)` → `IntraDomainLossy` with
  the lossy edge's dropped properties
- `compose(IntraDomainLossy, Degenerate)` → `Degenerate` (max wins)
- Composition is associative — verify by composing 3 edges in
  different orders
- `summary()` format checks

- [ ] **Step 3: Build, run, commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator add src/shape/lossprofile.{h,cpp} tests/shape/tst_loss_profile.cpp src/shape/CMakeLists.txt tests/shape/CMakeLists.txt
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "feat(shape): add LossProfile (G.1 Task 7)"
```

**Verification:**

- [ ] tst_loss_profile passes
- [ ] libkalburator now 29/29

### Task 8: Add `TransformationStage`, `TransformationEdge`, `Pipeline`

**Files:**
- Create: `~/dev/refactor-engine-merger/libkalburator/src/shape/transformationedge.h`
- Create: `~/dev/refactor-engine-merger/libkalburator/src/shape/transformationedge.cpp`
- Create: `~/dev/refactor-engine-merger/libkalburator/src/shape/pipeline.h`
- Create: `~/dev/refactor-engine-merger/libkalburator/src/shape/pipeline.cpp`
- Create: `~/dev/refactor-engine-merger/libkalburator/tests/shape/tst_pipeline.cpp`

**Background:** Per design doc § "TransformationEdge and Pipeline".

- [ ] **Step 1: TransformationStage abstract base**

```cpp
class TransformationStage {
public:
    virtual ~TransformationStage() = default;
    virtual QByteArray transform(const QByteArray& sourceBytes) const = 0;
};
```

- [ ] **Step 2: TransformationEdge struct**

Has `from`, `to`, `loss`, `stage` (shared_ptr). `toString()`
formats as `"<from-shape> → <to-shape> [<loss-summary>]"`.

- [ ] **Step 3: Pipeline class**

- `m_inputShape`, `m_outputShape`, `m_edges`
- `composedLoss()` — folds `LossProfile::compose` over `m_edges`
- `apply(QByteArray)` — chains `stage->transform(...)` over edges
- `isIdentity()` — true iff `m_edges.isEmpty()`

Constructors:
- For identity: `Pipeline(Shape s)` sets input==output==s with
  empty edges
- For composed: `Pipeline(QList<TransformationEdge>)` validates
  that consecutive edges' `to`/`from` chain matches

- [ ] **Step 4: Test cases for tst_pipeline.cpp**

- Identity pipeline: `apply(bytes) == bytes`; loss is `Lossless`
- Single-edge pipeline: invokes stage's transform once
- Two-edge pipeline: chains transforms; loss profile composes
- Non-matching edge chain (from/to mismatch): assertion or
  exception
- A custom test stage that prefixes bytes; compose two; verify
  output is double-prefixed

- [ ] **Step 5: Build, run, commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator add src/shape/transformationedge.{h,cpp} src/shape/pipeline.{h,cpp} tests/shape/tst_pipeline.cpp
# ... CMakeLists updates ...
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "feat(shape): add TransformationEdge and Pipeline (G.1 Task 8)"
```

**Verification:**

- [ ] tst_pipeline passes
- [ ] libkalburator now 30/30

### Task 9: Add `TransformationRegistry`

**Files:**
- Create: `~/dev/refactor-engine-merger/libkalburator/src/shape/transformationregistry.h`
- Create: `~/dev/refactor-engine-merger/libkalburator/src/shape/transformationregistry.cpp`
- Create: `~/dev/refactor-engine-merger/libkalburator/tests/shape/tst_transformation_registry.cpp`

**Background:** Per design doc § "TransformationRegistry".
Static-singleton; populated at static-init by domain plugins.

- [ ] **Step 1: Implement registry header per design**

Includes `instance()`, `registerEdge`, `registerShape`,
`catalogueFor`, `compile`, `inspect`, `registeredShapes`,
`edgesFrom`.

- [ ] **Step 2: Implement registry body**

- Internal storage: `QHash<Shape, PropertyCatalogue> m_catalogues`,
  `QMultiHash<Shape, TransformationEdge> m_edgesFrom`
- `registerEdge` — assert: both endpoints are registered shapes;
  idempotent on identical re-registration; assertion failure on
  conflict
- `registerShape` — store catalogue; idempotent on identical
- `compile(from, to)`:
  - If `to.isAny()` → return identity Pipeline (universal sink)
  - If `from.isAny()` → return `nullopt` (can't compile from
    unknown)
  - If `from == to` → return identity
  - If shapes not in same domain → return `nullopt` (no
    cross-domain in v1)
  - Look up canonical hub for the domain (the shape registered
    for `(domain, "canonical-hub")` — see Task 10 for how this
    is established)
  - Compose `from → hub → to` if both edges exist; else `nullopt`
- `inspect(from, to)` — same as compile but only returns the
  loss profile (cheaper, doesn't allocate Pipeline)

**Note on canonical hubs:** The registry needs a way to find the
canonical shape for a given domain. Options:

(a) Add a `registerCanonical(domain, shape)` method on the registry.
(b) Use a sentinel encoding `"canonical-hub"` and look up
    `(domain, "canonical-hub")` for the canonical mapping.
(c) Domain plugins call `registry.declareCanonical(domain, shape)`.

Choose (a). Add `void declareCanonical(DomainId, Shape canonical)`
and `Shape canonicalFor(DomainId) const`.

- [ ] **Step 3: Test cases for tst_transformation_registry.cpp**

- Register a shape; verify catalogueFor returns it
- Register canonical; verify canonicalFor returns it
- Register an edge between two registered shapes; verify
  edgesFrom returns it
- compile(from, to) for identity (same shape)
- compile(from, Any) returns identity
- compile(Any, to) returns nullopt
- compile(from, to) cross-domain returns nullopt (no edges in v1)
- compile(from, to) intra-domain via hub composes correctly
- compile when no path exists (e.g., domain not registered) returns
  nullopt
- Idempotent re-registration of same shape/edge succeeds
- Conflicting re-registration fails (with assert or thrown
  exception, depending on platform)

- [ ] **Step 4: Build, run, commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator add src/shape/transformationregistry.{h,cpp} tests/shape/tst_transformation_registry.cpp
# ... CMakeLists ...
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "feat(shape): add TransformationRegistry (G.1 Task 9)"
```

**Verification:**

- [ ] tst_transformation_registry passes (~10 cases)
- [ ] libkalburator now 31/31

### Task 10: Add `DomainPlugin` interface and `DomainRegistry`

**Files:**
- Create: `~/dev/refactor-engine-merger/libkalburator/src/shape/domainplugin.h`
- Create: `~/dev/refactor-engine-merger/libkalburator/src/shape/domainregistry.h`
- Create: `~/dev/refactor-engine-merger/libkalburator/src/shape/domainregistry.cpp`
- Create: `~/dev/refactor-engine-merger/libkalburator/tests/shape/tst_domain_registry.cpp`

**Background:** Per design doc § "DomainPlugin". Plugins are
registered with the `DomainRegistry` static singleton; the
registry calls `plugin->registerEdges(transformationRegistry)`.

- [ ] **Step 1: DomainPlugin abstract base per design**

Includes virtuals: `domain()`, `canonicalShape()`, `peerShapes()`,
`canonicalCatalogue()`, `catalogueFor()`,
`createCanonicalDiffer()`, `createCanonicalMerger()`,
`registerEdges(TransformationRegistry&)`, `richnessRank(Shape)`.

`IRecordDiffer` and `IRecordMerger` are forward-declared here;
their full interface lands in Task 11.

- [ ] **Step 2: DomainRegistry singleton**

```cpp
class DomainRegistry {
public:
    static DomainRegistry& instance();
    void registerDomain(std::shared_ptr<DomainPlugin>);
    DomainPlugin* findByDomain(const DomainId&) const;
    QList<DomainPlugin*> all() const;

    /// Initializes all registered plugins by calling
    /// registerEdges on each, populating the
    /// TransformationRegistry. Called from app startup.
    /// Idempotent — safe to call multiple times.
    void initialize(TransformationRegistry&);
};
```

- [ ] **Step 3: tst_domain_registry.cpp test cases**

- Stub plugin; register; findByDomain returns it
- Multiple plugins; `all()` returns all
- `initialize` calls each plugin's `registerEdges` exactly once
  (verify via mock plugin counter)
- Re-calling `initialize` is a no-op (idempotent)

- [ ] **Step 4: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator add src/shape/domainplugin.h src/shape/domainregistry.{h,cpp} tests/shape/tst_domain_registry.cpp
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "feat(shape): add DomainPlugin interface and DomainRegistry (G.1 Task 10)"
```

**Verification:**

- [ ] tst_domain_registry passes
- [ ] libkalburator now 32/32

### Task 11: Add `IRecordDiffer` and `IRecordMerger` interfaces

**Files:**
- Create: `~/dev/refactor-engine-merger/libkalburator/src/shape/irecorddiffer.h`
- Create: `~/dev/refactor-engine-merger/libkalburator/src/shape/irecordmerger.h`
- Create: `~/dev/refactor-engine-merger/libkalburator/src/shape/canonicalrecord.h`

**Background:** Per design doc § "IRecordDiffer and IRecordMerger"
and "CanonicalRecord". Pure interfaces; no implementation here.

- [ ] **Step 1: CanonicalRecord struct**

Per design doc § "CanonicalRecord" — `Shape`, `QByteArray data`,
`QString recordId`, `bool isDeleted`.

- [ ] **Step 2: IRecordDiffer abstract**

```cpp
class IRecordDiffer {
public:
    virtual ~IRecordDiffer() = default;
    virtual QSet<PropertyId> diff(const CanonicalRecord& source,
                                   const CanonicalRecord& baseline) const = 0;
    virtual bool equal(const CanonicalRecord& a,
                        const CanonicalRecord& b) const = 0;
};
```

- [ ] **Step 3: IRecordMerger abstract**

Per design doc spec, with `FieldChoice` enum and `merge` method.

- [ ] **Step 4: ConflictPolicy**

Reuse `Kalburator::Sync::ConflictPolicy` (already exists from F2).
Include the existing header.

- [ ] **Step 5: Commit**

No tests at this stage — these are pure interfaces. Concrete
implementations and their tests land in G.2 / G.5.

```bash
git -C ~/dev/refactor-engine-merger/libkalburator add src/shape/irecorddiffer.h src/shape/irecordmerger.h src/shape/canonicalrecord.h
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "feat(shape): add IRecordDiffer, IRecordMerger, CanonicalRecord interfaces (G.1 Task 11)"
```

**Verification:**

- [ ] Build clean (interfaces compile; no test impact)

### Task 12: G.1 verify-all gate

**Files:** none — verification only.

- [ ] **Step 1: Run verify-all**

```bash
~/dev/refactor-engine-merger/scripts/verify-all.sh
```

Expected: exit 0. libkalburator: 32/32. PlanStan, WildPalms
unchanged.

- [ ] **Step 2: Update status doc**

Mark G.1 complete in `04r-phase-g-status.md`.

- [ ] **Step 3: Commit status update**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator commit -am "docs(phase0): mark G.1 complete (Task 12)"
```

**Verification:**

- [ ] verify-all green

---

## Group 2 — G.2 Calendar plugin (~1.5 weeks)

Creates `KalburatorDomainCalendar` plugin, the `(calendar, ical)`
canonical differ/merger, and re-wires `CalendarDomainAdapter` to
delegate to the registry. Existing tests continue to pass — this
is a refactor under the existing contract.

### Task 13: Define calendar property catalogue

**Files:**
- Create: `~/dev/refactor-engine-merger/libkalburator/src/calendar/icalproperties.h`
- Create: `~/dev/refactor-engine-merger/libkalburator/src/calendar/icalproperties.cpp`

**Background:** The catalogue mirrors `KCalendarCore::Incidence`'s
property set. Hand-written for v1; auto-derivation deferred.

- [ ] **Step 1: Enumerate KCalendarCore::Incidence properties**

Reference: `KCalendarCore::Incidence` and `KCalendarCore::IncidenceBase`
header. Key properties:

- `uid` (String, required)
- `summary` (String)
- `description` (String)
- `location` (String)
- `dtStart` (DateTime)
- `duration` (Duration)
- `dtEnd` (DateTime)
- `priority` (Integer)
- `status` (String)
- `categories` (StringList)
- `attendees` (Json — list of attendee records)
- `organizer` (Json)
- `attachments` (Json — list)
- `recurrenceRule` (Json — RRULE structure)
- `alarms` (Json — list)
- `customProperties` (Json — map of X-* properties)
- `created` (DateTime)
- `lastModified` (DateTime)

- [ ] **Step 2: Build the catalogue**

Function: `Kalburator::Shape::PropertyCatalogue makeICalCatalogue()`.
Returns a catalogue populated with the descriptors.

- [ ] **Step 3: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator add src/calendar/icalproperties.{h,cpp}
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "feat(calendar): hand-written ical property catalogue (G.2 Task 13)"
```

### Task 14: Implement `IRecordDifferICal`

**Files:**
- Create: `~/dev/refactor-engine-merger/libkalburator/src/calendar/icalrecorddiffer.h`
- Create: `~/dev/refactor-engine-merger/libkalburator/src/calendar/icalrecorddiffer.cpp`
- Create: `~/dev/refactor-engine-merger/libkalburator/tests/calendar/differs/tst_ical_record_differ.cpp`

**Background:** Wraps existing `IncidenceDiff` logic in the
`IRecordDiffer` interface. The diff algorithm is unchanged from
F2; only the wrapper shape changes.

- [ ] **Step 1: Implement IRecordDifferICal**

Body:
- Parse both records' bytes via `KCalendarCore::ICalFormat::fromString`
- Call existing `IncidenceDiff` machinery
- Map the diff result back to `QSet<PropertyId>`

- [ ] **Step 2: Implement equal()**

Two records equal iff their parsed Incidences have identical
property values per the catalogue.

- [ ] **Step 3: Test cases**

Test cases mirror existing `IncidenceDiff` tests:
- Identical records → empty diff
- Different summary → `{summary}` in diff
- Different priority → `{priority}` in diff
- Multiple field changes → all changed fields
- Categories modified → `{categories}`

- [ ] **Step 4: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator add src/calendar/icalrecorddiffer.{h,cpp} tests/calendar/differs/tst_ical_record_differ.cpp
# ... CMakeLists ...
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "feat(calendar): IRecordDifferICal (G.2 Task 14)"
```

**Verification:**

- [ ] tst_ical_record_differ passes
- [ ] libkalburator now 33/33

### Task 15: Implement `IRecordMergerICal`

**Files:**
- Create: `~/dev/refactor-engine-merger/libkalburator/src/calendar/icalrecordmerger.h`
- Create: `~/dev/refactor-engine-merger/libkalburator/src/calendar/icalrecordmerger.cpp`
- Create: `~/dev/refactor-engine-merger/libkalburator/tests/calendar/differs/tst_ical_record_merger.cpp`

**Background:** Wraps existing 3-way merge logic. Per-property
choices per the supplied conflict policy.

- [ ] **Step 1: Implement merge()**

For each property in catalogue:
- If source unchanged from baseline AND target unchanged → take baseline
- If only source changed → take source
- If only target changed → take target
- If both changed:
  - `SourceWins` → take source
  - `TargetWins` → take target
  - `LastWriteWins` → take whichever has later `lastModified`
  - `AskUser` → fall through to host's `resolveConflict` callback

Serialize the resulting Incidence back to iCal bytes.

- [ ] **Step 2: Test cases**

- Both unchanged → baseline returned
- Source-only change → source value in result
- Target-only change → target value in result
- Both changed, SourceWins → source value
- Both changed, TargetWins → target value
- Both changed, LastWriteWins (source newer) → source value
- Both changed, LastWriteWins (target newer) → target value
- Cross-property merge: source changed summary, target changed
  description → both changes preserved in result

- [ ] **Step 3: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator add src/calendar/icalrecordmerger.{h,cpp} tests/calendar/differs/tst_ical_record_merger.cpp
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "feat(calendar): IRecordMergerICal (G.2 Task 15)"
```

**Verification:**

- [ ] tst_ical_record_merger passes (~8 cases)

### Task 16: Implement `KalburatorDomainCalendar` plugin

**Files:**
- Create: `~/dev/refactor-engine-merger/libkalburator/src/calendar/calendardomainplugin.h`
- Create: `~/dev/refactor-engine-merger/libkalburator/src/calendar/calendardomainplugin.cpp`
- Create: `~/dev/refactor-engine-merger/libkalburator/tests/calendar/plugin/tst_calendar_plugin.cpp`

**Background:** Implements `DomainPlugin` for the calendar
domain. Registers `(calendar, ical)` as canonical and the identity
edge. Other peer shapes (`palm-datebook`, `org`) are added in G.5.

- [ ] **Step 1: Implement plugin per design doc**

```cpp
class KalburatorDomainCalendar : public Kalburator::Shape::DomainPlugin {
public:
    DomainId domain() const override { return DomainId{"calendar"}; }
    Shape canonicalShape() const override
        { return Shape{ DomainId{"calendar"}, EncodingId{"ical"} }; }
    QList<Shape> peerShapes() const override { return {}; }  // populated in G.5
    PropertyCatalogue canonicalCatalogue() const override
        { return makeICalCatalogue(); }
    PropertyCatalogue catalogueFor(const Shape&) const override;
    std::unique_ptr<IRecordDiffer> createCanonicalDiffer() const override
        { return std::make_unique<IRecordDifferICal>(); }
    std::unique_ptr<IRecordMerger> createCanonicalMerger() const override
        { return std::make_unique<IRecordMergerICal>(); }
    void registerEdges(TransformationRegistry& r) override {
        r.registerShape(canonicalShape(), canonicalCatalogue());
        r.declareCanonical(domain(), canonicalShape());
        // identity edge always registered
        r.registerEdge(TransformationEdge{
            canonicalShape(), canonicalShape(),
            LossProfile{},  // lossless
            std::make_shared<IdentityStage>()
        });
    }
    int richnessRank(const Shape& s) const override {
        if (s == canonicalShape()) return 10;
        return 0;
    }
};
```

- [ ] **Step 2: Static-init registration**

```cpp
namespace {
struct CalendarPluginRegistrar {
    CalendarPluginRegistrar() {
        DomainRegistry::instance().registerDomain(
            std::make_shared<KalburatorDomainCalendar>());
    }
};
static CalendarPluginRegistrar s_registrar;
}
```

Note: static-init order across translation units is unspecified.
Use `Q_GLOBAL_STATIC` if order matters; for now this works
because backends aren't using the registry yet.

- [ ] **Step 3: Tests**

- Plugin registered after include
- canonicalShape returns `(calendar, ical)`
- canonicalCatalogue has expected property count
- registerEdges populates registry: identity edge present;
  canonical declared

- [ ] **Step 4: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator add src/calendar/calendardomainplugin.{h,cpp} tests/calendar/plugin/tst_calendar_plugin.cpp
# ... CMakeLists ...
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "feat(calendar): KalburatorDomainCalendar plugin (G.2 Task 16)"
```

**Verification:**

- [ ] tst_calendar_plugin passes
- [ ] libkalburator now 35/35

### Task 17: Wire `CalendarDomainAdapter` to delegate to registry

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/calendardomainadapter.cpp`

**Background:** Existing `CalendarDomainAdapter` (from F1) has
hardcoded calendar diff/merge logic. Re-wire it to delegate to
the registry's `IRecordDifferICal` / `IRecordMergerICal` for the
canonical shape. Backwards-compatible — same external behavior.

- [ ] **Step 1: Locate the adapter's diff and merge methods**

```bash
grep -n 'diff\|merge' ~/dev/refactor-engine-merger/libkalburator/src/calendar/calendardomainadapter.cpp
```

- [ ] **Step 2: Replace direct IncidenceDiff calls with registry-mediated ones**

```cpp
// Was: IncidenceDiff diff(...);
// Now:
auto* differ = DomainRegistry::instance()
    .findByDomain(DomainId{"calendar"})
    ->createCanonicalDiffer().get();
differ->diff(source, baseline);
```

- [ ] **Step 3: Verify all calendar tests still pass**

```bash
ctest --test-dir ~/dev/refactor-engine-merger/libkalburator/build -R 'tst_calendar' -V
```

Expected: all 23+ existing calendar tests pass under the new
wiring.

- [ ] **Step 4: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator add src/calendar/calendardomainadapter.cpp
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "refactor(calendar): adapter delegates to registry (G.2 Task 17)"
```

**Verification:**

- [ ] All existing calendar tests still pass

### Task 18: G.2 verify-all gate

- [ ] verify-all green; status doc updated; commit

---

## Group 3 — G.3 Backend interface migration (~2 weeks)

Adds `nativeShapes()`, `resourceId()`, `shapeFor()` to
`SyncBackend`. Migrates the 13 `dataDomain()` callsites and deletes
`DataDomain` enum.

### Task 19: Add `nativeShapes()` and `resourceId()` to SyncBackend base

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/syncbackend.h`
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/syncbackend.cpp`

**Background:** Per design doc § "SyncBackend (post-bend)".

- [ ] **Step 1: Add virtual methods to syncbackend.h**

```cpp
virtual QList<Kalburator::Shape::Shape> nativeShapes() const = 0;
virtual QString resourceId() const;  // default impl
virtual Kalburator::Shape::Shape shapeFor(const QString& collectionId) const;
    // default impl returns nativeShapes().first() or Shape::Any()
```

- [ ] **Step 2: Default `resourceId()` impl in syncbackend.cpp**

```cpp
QString SyncBackend::resourceId() const {
    return QStringLiteral("backend:") +
        QString::number(reinterpret_cast<quintptr>(this), 16);
}

Kalburator::Shape::Shape SyncBackend::shapeFor(const QString&) const {
    auto shapes = nativeShapes();
    if (shapes.isEmpty()) return Kalburator::Shape::Shape::Any();
    return shapes.first();
}
```

- [ ] **Step 3: Build — expect compile errors in concrete backends**

The pure-virtual `nativeShapes()` will trip every concrete
backend. The next 8 tasks (one per backend) supply overrides.

**Verification:** at this point the build fails; that's expected.
Don't commit yet; complete Tasks 20-27 first.

### Task 20: Override `nativeShapes()` in `LocalBackend`, `RemoteBackend`, `OrgBackend`, `MockBackend`, `DecSyncBackend`, `AkonadiBackend`, `HolidaySubscriptionBackend`, `SubscriptionBackend`

**Files:**
- Modify each backend's `.h` and `.cpp` in
  `libkalburator/src/calendar/`

**Background:** All eight backends serve the calendar domain
canonical shape `(calendar, ical)`. Mechanical change.

- [ ] **Step 1: Apply to each backend**

In each `.h`:
```cpp
QList<Kalburator::Shape::Shape> nativeShapes() const override;
```

In each `.cpp`:
```cpp
QList<Kalburator::Shape::Shape> X::nativeShapes() const {
    return { Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{"calendar"},
        Kalburator::Shape::EncodingId{"ical"} } };
}
```

- [ ] **Step 2: Build clean**

```bash
cmake --build ~/dev/refactor-engine-merger/libkalburator/build
```

- [ ] **Step 3: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator commit -am "feat(backend): add nativeShapes() to all calendar backends (G.3 Task 20)"
```

**Verification:** build clean.

### Task 21: Mark `dataDomain()` deprecated

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/syncbackend.h`

- [ ] **Step 1: Add `[[deprecated]]` attribute**

```cpp
[[deprecated("Use nativeShapes() instead. Will be removed in G.3.")]]
virtual DataDomain dataDomain() const = 0;
```

- [ ] **Step 2: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator commit -am "deprecate(backend): mark dataDomain() deprecated (G.3 Task 21)"
```

**Verification:** build emits deprecation warnings at the 13
callsites.

### Task 22: Migrate `dataDomain()` callsites — libkalburator side

**Files (per the grep audit in Task 1):**
- `libkalburator/src/types/iincidencesource.h`
- `libkalburator/src/types/iincidenceregistry.h`
- `libkalburator/src/calendar/syncbackend.h` (the declaration)
- `libkalburator/src/engine/syncengine.cpp`
- `libkalburator/tests/calendar/stubs/stubincidenceregistry.{h,cpp}`

**Background:** Each callsite asks "what kind of data does this
backend serve?" — replace with checking `nativeShapes()`.

- [ ] **Step 1: For each callsite, replace `dataDomain() == DataDomain::Calendar` with `nativeShapes().contains(calendar shape)` (or appropriate predicate)**

Detailed migration per callsite:

- **iincidencesource.h, iincidenceregistry.h:** the `DataDomain`
  parameter on these interfaces was a type-tag for incidence
  filtering. Replace with `Shape` parameter or remove if redundant.
- **syncengine.cpp:** existing routing using `dataDomain()`.
  Replace with shape-based routing. Specifically: where the engine
  decides "is this a calendar mapping or a blob mapping?", use
  `mapping.sourceBackend->shapeFor(mapping.sourceCalendarId)
  .domain == DomainId{"calendar"}`.
- **stubincidenceregistry.{h,cpp}:** stub for tests; update to
  match interface change.

- [ ] **Step 2: Build clean (no more deprecation warnings on
  libkalburator side)**

- [ ] **Step 3: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator commit -am "refactor(backend): migrate libkalburator dataDomain() callsites to nativeShapes() (G.3 Task 22)"
```

### Task 23: Migrate `dataDomain()` callsites — PlanStan side

**Files:**
- `PlanStan/src/controllers/collectioncontroller.cpp`
- `PlanStan/src/controllers/itemloadingcoordinator.cpp`

- [ ] **Step 1: Audit callsites**

```bash
git -C ~/dev/refactor-engine-merger/PlanStan grep -n 'dataDomain' -- 'src/'
```

- [ ] **Step 2: Migrate per the same pattern as libkalburator**

- [ ] **Step 3: Commit on PlanStan worktree**

```bash
git -C ~/dev/refactor-engine-merger/PlanStan commit -am "refactor(controllers): migrate dataDomain() callsites to nativeShapes() (Phase G G.3 Task 23)"
```

### Task 24: Migrate `dataDomain()` callsites — WildPalms side

**Files:**
- `WildPalms/src/palm/calendar/palmcalendarbackend.h`
- `WildPalms/src/palm/calendar/palmcalendarbackend.cpp`

- [ ] **Step 1: Override `nativeShapes()` to return Palm calendar shape**

```cpp
QList<Kalburator::Shape::Shape> nativeShapes() const override {
    return { Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{"calendar"},
        Kalburator::Shape::EncodingId{"palm-datebook"} } };
}
```

Note: WildPalms's PalmCalendarBackend's native shape is
`(calendar, palm-datebook)`, not `(calendar, ical)` like the
libkalburator-side calendar backends. The transformation edge
between them lands in G.5.

- [ ] **Step 2: Override `resourceId()` to return Palm device serial**

```cpp
QString resourceId() const override {
    return m_device ?
        QStringLiteral("palm-device:") + m_device->serial() :
        SyncBackend::resourceId();
}
```

- [ ] **Step 3: Remove the existing `dataDomain()` override**

- [ ] **Step 4: Commit on WildPalms worktree**

```bash
git -C ~/dev/refactor-engine-merger/WildPalms commit -am "refactor(palm-calendar): adopt nativeShapes/resourceId (Phase G G.3 Task 24)"
```

### Task 25: Delete `dataDomain()` and `DataDomain` enum

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/syncbackend.h`
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/types/datadomain.h` (delete)

- [ ] **Step 1: Verify zero callsites remain**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator grep 'dataDomain' -- 'src/' 'tests/'
git -C ~/dev/refactor-engine-merger/PlanStan grep 'dataDomain' -- 'src/' 'tests/'
git -C ~/dev/refactor-engine-merger/WildPalms grep 'dataDomain' -- 'src/' 'tests/'
```

Expected: zero hits across all three repos.

- [ ] **Step 2: Delete the virtual method from syncbackend.h**

- [ ] **Step 3: Delete `src/types/datadomain.h`**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator rm src/types/datadomain.h
```

- [ ] **Step 4: Build clean**

- [ ] **Step 5: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator commit -am "refactor(backend): delete dataDomain() and DataDomain enum (G.3 Task 25)"
```

### Task 26: G.3 verify-all gate

- [ ] verify-all green
- [ ] status doc updated
- [ ] commit status

---

## Group 4 — G.4 Mapping-keyed baselines (~1.5 weeks)

Migrates `BlobBaselineStore` from triple-keyed
`(backend_id, collection_id, record_id)` to mapping-keyed
`(mapping_id, record_id)` per design doc § "BlobBaselineStore
v3 schema". Stored user data migrates in-place via
`PRAGMA user_version` gate.

### Task 27: Add v3 schema and migration code

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/journal/blobbaselinestore.h`
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/journal/blobbaselinestore.cpp`

**Background:** Per design doc § "BlobBaselineStore v3 schema".
The migration creates `blob_baselines_v3` and copies rows from
`blob_baselines_triple` (the v2 table). Each v2 row maps to N
v3 rows where N is the number of mappings referencing that
`(backend_id, collection_id)` pair.

- [ ] **Step 1: Add `ensureSchemaV3()` method**

Reads `PRAGMA user_version`; if < 3, runs migration:
1. Begin transaction
2. CREATE TABLE blob_baselines_v3 (...)
3. SELECT all from blob_baselines_triple
4. For each row, look up which mappings reference (backend_id,
   collection_id). The mapping registry must be reachable;
   inject via `setMappingResolver(std::function<...>)` on the
   store; the engine plumbs it during init.
5. INSERT v3 rows for each matching mapping
6. PRAGMA user_version = 3
7. Commit transaction
8. (Don't drop blob_baselines_triple yet; defer to a future
   release for rollback safety)

- [ ] **Step 2: Add new mapping-keyed accessors**

```cpp
void setBaseline(MappingId mappingId, const QString& recordId,
                 const Shape::CanonicalRecord& canonical);
std::optional<Shape::CanonicalRecord> baseline(
    MappingId mappingId, const QString& recordId) const;
QList<Shape::CanonicalRecord> baselinesForMapping(MappingId) const;
void removeBaseline(MappingId mappingId, const QString& recordId);
```

- [ ] **Step 3: Mark old triple-keyed methods deprecated**

The post-F1 triple-keyed API is callable but warned:
```cpp
[[deprecated("Use mapping-keyed API.")]]
void setBaseline(const QString& backendId, const QString& collectionId,
                 const QString& recordId, const QByteArray& bytes);
```

- [ ] **Step 4: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator commit -am "feat(journal): BlobBaselineStore v3 schema + migration (G.4 Task 27)"
```

### Task 28: Tests for v3 schema and migration

**Files:**
- Create: `~/dev/refactor-engine-merger/libkalburator/tests/journal/tst_blob_baseline_store_v3.cpp`

- [ ] **Step 1: Test cases**

- Fresh DB: schema initialized to v3
- v2 DB with sample rows: migration produces v3 rows; user_version
  bumped
- **Idempotent migration**: open, close, reopen; second open is
  a no-op (per FINDINGS lesson)
- mapping-keyed read after write returns correct bytes
- multiple mappings sharing (backend, collection): v2 row
  appears in each
- orphan v2 row (no mapping) is logged + skipped

- [ ] **Step 2: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator commit -am "test(journal): pin BlobBaselineStore v3 (G.4 Task 28)"
```

**Verification:**

- [ ] all v3 tests pass

### Task 29: Migrate engine to use mapping-keyed baselines

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/engine/syncengine.cpp`
- Modify: any other engine sources reading/writing baselines

- [ ] **Step 1: Replace triple-keyed accessors with mapping-keyed**

Where the engine writes baselines, supply `mappingId` instead of
`backendId, collectionId`. The mapping_id is already known to the
engine — it's processing a specific mapping at the time.

- [ ] **Step 2: Verify all calendar/blob tests still pass**

- [ ] **Step 3: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator commit -am "refactor(engine): use mapping-keyed baselines (G.4 Task 29)"
```

### Task 30: G.4 verify-all gate + tag preparation

- [ ] verify-all green
- [ ] status doc updated; mark G.1-G.4 complete
- [ ] **Tag check:** confirm with user before tagging

### Task 31: TAG `v0.15-phase-g-foundations` (USER-AUTHORISED)

**Files:** none — tag operation only.

- [ ] **Step 1: Confirm verify-all green at HEAD**
- [ ] **Step 2: Get user authorisation for tag**
- [ ] **Step 3: Create the tag**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator tag -a v0.15-phase-g-foundations \
  -m "Phase G foundations: Shape, registry, calendar plugin, mapping-keyed baselines"
```

- [ ] **Step 4: Update phase-status doc and CURRENT-STATUS.md**

---

## Group 5 — G.5 New domain plugins (~2 weeks)

Adds `KalburatorDomainTodo`, `KalburatorDomainContacts`,
`KalburatorDomainMemo` plugins. Each has property catalogue,
canonical-shape differ/merger, and edges to native peer shapes
(populated as TransformationStage stubs initially; real bytes-
preserving stages land alongside concrete backends in G.7/G.10).

### Task 32: Todo domain — property catalogue

**Files:**
- Create: `libkalburator/src/todo/icalvtodoproperties.{h,cpp}`

**Background:** VTODO catalogue. Subset of the calendar catalogue
(no DTSTART required, plus `dtDue`, `percentComplete`, etc.).

- [ ] Implement; mostly mirrors calendar catalogue with
      VTODO-specific properties

### Task 33: Todo domain — IRecordDifferVTodo, IRecordMergerVTodo

**Files:**
- Create: `libkalburator/src/todo/icalvtododiffer.{h,cpp}`
- Create: `libkalburator/src/todo/icalvtodomerger.{h,cpp}`
- Create: `libkalburator/tests/todo/differs/tst_vtodo_differ.cpp`
- Create: `libkalburator/tests/todo/differs/tst_vtodo_merger.cpp`

- [ ] Implement per the calendar plugin pattern (Task 14, 15)

### Task 34: Todo domain — TodoTxtTransformationStage

**Files:**
- Create: `libkalburator/src/todo/todotxttransformation.{h,cpp}`
- Create: `libkalburator/tests/todo/plugin/tst_todotxt_stage.cpp`

**Background:** A `TransformationStage` for `(todo, todotxt) ↔
(todo, ical-vtodo)`. todo.txt is plain text per
`https://github.com/todotxt/todo.txt`; parse/format both ways.

- [ ] Parse todo.txt format (priority `(A)`, completion `x`,
      `+project`, `@context`, `due:YYYY-MM-DD`, etc.)
- [ ] Serialize VTODO to todo.txt
- [ ] Loss profile: drops description, attendees, RRULE,
      attachments, custom properties, multiple categories beyond
      first project/context

### Task 35: Todo domain — KalburatorDomainTodo plugin

**Files:**
- Create: `libkalburator/src/todo/tododomainplugin.{h,cpp}`

- [ ] Mirrors KalburatorDomainCalendar pattern
- [ ] Registers canonical `(todo, ical-vtodo)`, peer
      `(todo, todotxt)`, edge between them via
      `TodoTxtTransformationStage`
- [ ] Other peer shapes (org, palm-todo) registered without
      stages initially; real stages land in G.7/G.10

### Task 36: Contacts domain — property catalogue

**Files:**
- Create: `libkalburator/src/contacts/vcardproperties.{h,cpp}`

**Background:** vCard 3.0 properties (FN, N, EMAIL, TEL, ADR, ORG,
TITLE, NOTE, BDAY, PHOTO, URL, custom X-*).

### Task 37: Contacts domain — IRecordDifferVCard, IRecordMergerVCard

**Files:**
- Create: `libkalburator/src/contacts/vcarddiffer.{h,cpp}`
- Create: `libkalburator/src/contacts/vcardmerger.{h,cpp}`
- Create: `libkalburator/tests/contacts/differs/tst_vcard_differ.cpp`
- Create: `libkalburator/tests/contacts/differs/tst_vcard_merger.cpp`

- [ ] Use `KContacts::VCardConverter` for parsing
- [ ] Property-aware diff (compare FN, EMAIL list, TEL list, etc.)
- [ ] Multi-value properties (multiple emails, multiple addresses)
      diff per item

**Note:** Contacts gating depends on `KALBURATOR_HAVE_KCONTACTS` —
add CMake flag, gate the contacts subdir.

### Task 38: Contacts domain — KalburatorDomainContacts plugin

**Files:**
- Create: `libkalburator/src/contacts/contactsdomainplugin.{h,cpp}`

- [ ] Mirrors KalburatorDomainCalendar pattern
- [ ] Canonical: `(contacts, vcard)`. Peer: `(contacts, palm-address)`
      (stage stub for now)

### Task 39: Memo domain — property catalogue, differ, merger, plugin

**Files:**
- Create: `libkalburator/src/memo/memoproperties.{h,cpp}`
- Create: `libkalburator/src/memo/textdiffer.{h,cpp}`
- Create: `libkalburator/src/memo/textmerger.{h,cpp}`
- Create: `libkalburator/src/memo/memodomainplugin.{h,cpp}`
- Create: `libkalburator/tests/memo/...`

**Background:** Memo is the simplest domain. Catalogue: `body`
(string), `categories` (string-list), `lastModified` (datetime).
Differ: text equality + category set equality. Merger: 3-way
text merge by line; if line-merge produces conflicts, fall through
to conflict policy.

- [ ] Implement all four files in one task block
- [ ] Tests for differ, merger, plugin registration

### Task 40: G.5 verify-all gate

- [ ] verify-all green; status updated; commit

---

## Group 6 — G.6 BlobDomainAdapter dispatch + MappingScheduler (~2 weeks)

Registers `BlobDomainAdapter` for unified worker dispatch (closing
the F2 deferral); adds the `MappingScheduler`,
`runSyncFuture(QList<MappingId>)` overload, `SyncEngineFuture`
wrapper, and `CancellationReason`.

### Task 41: Wire `BlobDomainAdapter` for unified dispatch

**Files:**
- Modify: `libkalburator/src/engine/syncengine.cpp`
- Modify: `libkalburator/src/engine/syncengineworker.cpp`

**Background:** Today the worker hardcodes the calendar pipeline.
Generalize: read `mapping.sourceBackend->shapeFor(...)`'s domain;
look up the domain plugin; route through its differ/merger.

For the blob domain, register a stub `KalburatorDomainBlob` plugin
(if not already present) with canonical `(blob, opaque)`,
identity-only edges, hash-equality differ, whole-record-replace
merger.

- [ ] Implement `KalburatorDomainBlob` plugin
- [ ] Generalize worker's `processSync` to look up the domain
      plugin and use its differ/merger
- [ ] Existing blob tests (`tst_engine_blob_one_shot`) now run
      through unified dispatch

**Verification:** existing blob tests pass; calendar tests pass.

### Task 42: Mark F1 facade deprecated

**Files:**
- Modify: `libkalburator/src/engine/syncengine.h`

- [ ] Add `[[deprecated]]` to `runBlobTwoWay` and `runBlobMirror`
      with deletion-target comment pointing at G.8
- [ ] Build emits deprecation warnings at WildPalms callsites and
      libkalburator's `tst_engine_blob_one_shot` (which uses the
      facade per F2 deferral notes)

### Task 43: Add `runSyncFuture(QList<MappingId>)` overload

**Files:**
- Modify: `libkalburator/src/engine/syncengine.h`
- Modify: `libkalburator/src/engine/syncengine.cpp`
- Create: `libkalburator/tests/engine/tst_engine_subset_dispatch.cpp`

- [ ] Add public method `SyncEngineFuture runSyncFuture(QList<MappingId> ids, SyncBehavior = {})`
- [ ] Internally: enqueue all ids on the new MappingScheduler
      (Task 44)
- [ ] Test: 3 mappings registered; subset of 2 dispatched; only
      those 2 run

### Task 44: Implement `MappingScheduler`

**Files:**
- Create: `libkalburator/src/engine/mappingscheduler.h`
- Create: `libkalburator/src/engine/mappingscheduler.cpp`
- Create: `libkalburator/tests/engine/tst_mapping_scheduler.cpp`

- [ ] Implement per design doc § "MappingScheduler"
- [ ] Resource graph based on `resourceId()` of source+target
      backends per mapping
- [ ] Capacity-1 per resource; FIFO queue for waiters
- [ ] Tests:
  - Two disjoint-resource mappings: both queued; both eventually
    run (initial v1: still sequential since global cap is 1)
  - Two same-resource mappings: second waits for first
  - 5-mapping queue with mixed resources: order preserved within
    a resource group

### Task 45: Add `CancellationReason` and `SyncEngineFuture` wrapper

**Files:**
- Create: `libkalburator/src/engine/syncenginefuture.{h,cpp}`
- Modify: `libkalburator/src/engine/syncengine.h` (return type)

- [ ] Implement per design doc § "SyncEngineFuture and
      CancellationReason"
- [ ] Migrate existing `runSyncFuture` call sites to use the
      wrapper (return type change is mechanical; existing
      `QFuture<...>::cancel()` paths still work since the wrapper
      converts implicitly)

### Task 46: Cancellation channel: `ResourceLost` short-circuits queued mappings

**Files:**
- Modify: `libkalburator/src/engine/mappingscheduler.cpp`
- Modify: `libkalburator/src/engine/syncenginefuture.cpp`
- Create: `libkalburator/tests/engine/tst_cancellation_reason.cpp`

- [ ] Wire: `cancelWithReason(ResourceLost, resourceId)` →
      scheduler iterates queued mappings; cancels those whose
      resource set includes `resourceId`
- [ ] Tests:
  - Cancel UserRequested: only in-flight mapping cancelled
  - Cancel ResourceLost("palm-device:abc"): all queued mappings
    using that resource also cancelled

### Task 47: G.6 verify-all gate

- [ ] verify-all green; status updated

---

## Group 7 — G.7 WildPalms transformation (~3 weeks)

Replaces `SyncRunner_wp` with `HotSyncCoordinator`. Adds Palm
backends for contacts, memo, todo. Profile-side mapping registry.
F1 facade ready for deletion (lands in G.8).

### Task 48: Create `HotSyncCoordinator` skeleton

**Files:**
- Create: `WildPalms/src/runtime/hotsynccoordinator.{h,cpp}`

- [ ] Implement per design doc § "HotSyncCoordinator"
- [ ] Skeleton: subscribes to `PalmDeviceConnection::deviceConnected`,
      stores future, no actual mapping dispatch yet (Task 49 wires
      that)

### Task 49: Wire HotSyncCoordinator to engine

- [ ] On `deviceConnected`, query the registry for
      `mappingsTouchingResource("palm-device:" + serial)`
- [ ] Fire `engine->runSyncFuture(palmMappingIds)`
- [ ] Connect future's `onProgress`, `onFinished`, `onCanceled`
      to UI signals
- [ ] On `deviceDisconnected`, call
      `m_currentFuture->cancelWithReason(ResourceLost, resource)`

### Task 50: PalmContactsBackend

**Files:**
- Create: `WildPalms/src/palm/contacts/palmcontactsbackend.{h,cpp}`

- [ ] Implement SyncBackend with native shape
      `(contacts, palm-address)`; resourceId = device serial
- [ ] fetchItems / pushItems / deleteItems operating on the
      Palm AddressDB

### Task 51: PalmMemoBackend

**Files:**
- Create: `WildPalms/src/palm/memo/palmmemobackend.{h,cpp}`

- [ ] Mirrors PalmContactsBackend pattern; native shape
      `(memo, palm-memo)`

### Task 52: PalmToDoBackend

**Files:**
- Create: `WildPalms/src/palm/todo/palmtodobackend.{h,cpp}`

- [ ] Mirrors pattern; native shape `(todo, palm-todo)`

### Task 53: Palm transformation stages

**Files:**
- Create: `WildPalms/src/palm/transformations/palmaddresstovcard.{h,cpp}`
- Create: `WildPalms/src/palm/transformations/palmemotoplaintext.{h,cpp}`
- Create: `WildPalms/src/palm/transformations/palmtodotovtodo.{h,cpp}`

- [ ] Each implements `TransformationStage`
- [ ] Registered with TransformationRegistry by a WildPalms-side
      domain-plugin extension (so libkalburator's stock plugins
      don't need Palm-specific knowledge)

### Task 54: Profile-side mapping registry in WildPalms

**Files:**
- Modify: `WildPalms/src/config/...` (locate and modify)

- [ ] Per-profile config grows a list of `SyncMapping`s
- [ ] UI to add/edit/delete mappings in WildPalms's settings
- [ ] On profile load, mappings registered with the
      `BackendRegistry`

### Task 55: Delete `SyncRunner_wp`

**Files:**
- Delete: `WildPalms/src/runtime/syncrunner_wp.{h,cpp}`
- Modify: callers (kf6mainwindow.cpp, etc.) to use HotSyncCoordinator

- [ ] Confirm zero callers via grep
- [ ] Delete files
- [ ] Update CMakeLists

### Task 56: Tests for new Palm backends and HotSyncCoordinator

**Files:**
- Create: `WildPalms/tests/palmsync/tst_palm_contacts_backend.cpp`
- Create: `WildPalms/tests/palmsync/tst_palm_memo_backend.cpp`
- Create: `WildPalms/tests/palmsync/tst_palm_todo_backend.cpp`
- Create: `WildPalms/tests/runtime/tst_hotsync_coordinator.cpp`

- [ ] Per backend: round-trip test (write+read), failure cases
- [ ] HotSyncCoordinator: device-connect dispatches future;
      device-disconnect cancels with ResourceLost

### Task 57: G.7 verify-all gate

- [ ] verify-all green; WildPalms baseline holds (73/73 modulo
      flakes)

---

## Group 8 — G.8 F1 facade deletion + universal sinks (~1.5 weeks)

### Task 58: Delete F1 facade

**Files:**
- Modify: `libkalburator/src/engine/syncengine.h`
- Modify: `libkalburator/src/engine/syncengine.cpp`
- Delete: `libkalburator/src/blob/blobsyncresult.h` (if no callers)

- [ ] Verify zero callers across all repos
- [ ] Delete `runBlobTwoWay`, `runBlobMirror` declarations and
      bodies
- [ ] Migrate `tst_engine_blob_one_shot` to use the unified
      `runSyncFuture` (per F2 deferral note in CURRENT-STATUS)

### Task 59: RawFilesBackend

**Files:**
- Create: `libkalburator/src/sinks/rawfilesbackend.{h,cpp}`

- [ ] Implement per design doc § "RawFilesBackend"
- [ ] File-naming: `<rootPath>/<recordId>.<encoding-id>.<domain-id>`
- [ ] Manifest: `<rootPath>/_shapes.json` for fast re-discovery
- [ ] As source: parse filenames; recover shapes; yield records

### Task 60: GenericSqliteBackend

**Files:**
- Create: `libkalburator/src/sinks/genericsqlitebackend.{h,cpp}`

- [ ] Implement per design doc § "GenericSqliteBackend"
- [ ] On first push of a new shape, create
      `<domain>_<encoding>` table from `PropertyCatalogue`'s
      `sqlColumnDdl()`
- [ ] `_shapes` metadata table

### Task 61: Tests for universal sinks

**Files:**
- Create: `libkalburator/tests/sinks/tst_rawfiles_backend.cpp`
- Create: `libkalburator/tests/sinks/tst_generic_sqlite_backend.cpp`
- Create: `libkalburator/tests/sinks/tst_universal_sink_as_source.cpp`

- [ ] Round-trip per backend
- [ ] Multi-shape coexistence in one generic-db
- [ ] Sink-as-source: write+restore round-trip

### Task 62: G.8 verify-all gate + tag #2

- [ ] verify-all green
- [ ] User-authorised tag `v0.15.5-phase-g-engine-unified`

---

## Group 9 — G.9 ISyncHost narrowing + sync I/O retirement (~3 weeks)

The largest group. Two halves: G.9.a (ISyncHost narrowing) and
G.9.b (sync I/O retirement + test moves).

### Task 63: New ISyncHost interface (G.9.a)

**Files:**
- Modify: `libkalburator/src/calendar/isynchost.h`

- [ ] Add new generic methods: `syncStarted`, `syncFinished`,
      `recordChanged`, `resolveConflict`, `progressChanged`,
      `phaseChanged`, `errorOccurred`
- [ ] Mark old methods `[[deprecated]]`

### Task 64: PlanStan implements new ISyncHost

**Files:**
- Modify: `PlanStan/src/sync/planstansynchost.{h,cpp}`

- [ ] Implement new methods
- [ ] Old method bodies remain; mark them as fallthrough to
      new methods or no-op

### Task 65: WildPalms implements new ISyncHost

**Files:**
- Modify: `WildPalms/src/runtime/synchost_wp.{h,cpp}`

- [ ] Implement new methods

### Task 66: Engine migrates to new ISyncHost methods

**Files:**
- Modify: `libkalburator/src/engine/syncengine.cpp`
- Modify: `libkalburator/src/engine/syncengineworker.cpp`

- [ ] Where engine calls old per-incidence methods, switch to
      `recordChanged`
- [ ] Verify behaviour preserved: PlanStan and WildPalms tests
      pass with new method calls

### Task 67: Delete old calendar-typed ISyncHost methods

- [ ] Verify zero callsites
- [ ] Delete the deprecated methods from interface
- [ ] Consumer impls drop the old method overrides

### Task 68-78: Test file moves (G.9.b — bulk migration)

These tasks are mechanical and can be parallelized via subagents.
Each task is one test file:

- Task 68: Move `PlanStan/tests/backends/tst_orgbackend.cpp` to
  `libkalburator/tests/calendar/backends/`. Rewrite usages of
  `loadItems`/`storeItems`/`updateItem`/`writeFinished` to use
  the operation API.
- Task 69: Move `tst_orgbackend_external.cpp` similarly.
- Task 70: Move `tst_decsyncbackend.cpp` similarly.
- Task 71: Move `tst_localbackend.cpp` (PlanStan/tests/backends).
- Task 72: Move `tst_remotebackend.cpp`.
- Task 73: Move `tst_backend_signals.cpp` (rewrite —
  `&SyncBackend::writeFinished` → `&SyncOperation::finished`).
- Task 74: Move `syncbackend_test_framework.h` to
  `libkalburator/tests/shared/`; update operation-handle helpers.
- Task 75: Audit `tests/sync/tst_sync_directions.cpp`; either
  move or stay-in-PlanStan per fate map.
- Task 76: Audit `tests/integration/tst_calendarcrud.cpp`; stay
  in PlanStan per fate map.
- Task 77: Audit `tests/localbackend/tst_localbackend.cpp`;
  retire if redundant per fate map.
- Task 78: Migrate PlanStan production caller
  `convertCalendarToBackend` to operation API.

Each task: single commit on the appropriate worktree; verify-all
green after each.

### Task 79: Delete deprecated synchronous I/O API

**Files:**
- Modify: `libkalburator/src/calendar/syncbackend.h`
- Modify: `libkalburator/src/calendar/syncbackend.cpp`

- [ ] Verify zero callsites
- [ ] Delete `loadItems`, `storeItems`, `updateItem`, `writeFinished`
- [ ] Update `palmcalendarbackend` to remove the legacy stub
      overrides (now no longer required by the base)

### Task 80: G.9 verify-all gate

- [ ] verify-all green
- [ ] PlanStan baseline shifts (~5500 lines moved out); refresh
      baseline at this commit

---

## Group 10 — G.10 Loss profile UX + new stock backends (~2 weeks)

### Task 81: `WhenLossWouldOccur` field on SyncMapping

**Files:**
- Modify: `libkalburator/src/types/syncmapping.h`
- Modify: any serialization (config store)

### Task 82: LossProfile plumbed through ISyncHost::syncStarted

**Files:**
- Modify: engine to pass loss profile when emitting syncStarted

### Task 83: `libkalburator-qtwidgets` sibling library

**Files:**
- Create: `libkalburator-qtwidgets/` sibling library structure
- Create: `libkalburator-qtwidgets/lossprofiledetailview.{h,cpp}`
- Create: `libkalburator-qtwidgets/CMakeLists.txt`

- [ ] Decision per design doc § open question 1: sibling library
- [ ] Implement `LossProfileDetailView` Qt widget for rendering
      loss profiles in a sync-config UI

### Task 84: AkonadiContactsBackend

**Files:**
- Create: `libkalburator/src/contacts/akonadicontactsbackend.{h,cpp}`
- Create: `libkalburator/tests/contacts/backends/tst_akonadi_contacts_backend.cpp`

- [ ] Native shape `(contacts, vcard)`
- [ ] Gated by `KALBURATOR_HAVE_AKONADI` and `KALBURATOR_HAVE_KCONTACTS`

### Task 85: AkonadiNotesBackend

**Files:**
- Create: `libkalburator/src/memo/akonadinotesbackend.{h,cpp}`
- Create: tests

### Task 86: AkonadiTasksBackend

**Files:**
- Create: `libkalburator/src/todo/akonaditasksbackend.{h,cpp}`
- Create: tests

### Task 87: CardDAVRemoteBackend

**Files:**
- Create: `libkalburator/src/contacts/carddavremotebackend.{h,cpp}`
- Create: tests

- [ ] Use libkdav2 (gate by CMake flag)

### Task 88: PlanStan UI for new mapping fields

**Files:**
- Modify: `PlanStan/src/sync/syncconfigdialog.{h,cpp}` (or wherever
  mapping config UI lives)

- [ ] Add `WhenLossWouldOccur` selector
- [ ] Add LossProfileDetailView widget for visualising loss

### Task 89: WildPalms UI for new mapping fields

**Files:**
- Modify: `WildPalms/src/config/...`

- [ ] Same as PlanStan

### Task 90: Delete vestigial `pushItems(Incidence::Ptr)` overload

**Files:**
- Modify: `libkalburator/src/calendar/syncbackend.h`
- Migrate all calendar backends to `pushRecords(CanonicalRecord)`

- [ ] Mechanical migration; small per backend

### Task 91: G.10 verify-all gate + final tag

- [ ] verify-all green
- [ ] User-authorised tag `v0.16-phase-g-shape-pipeline`

---

## Group 11 — Wrap-up

### Task 92: Update FINDINGS.md

- [ ] Append findings discovered during Phase G implementation
- [ ] Mark resolved findings `[RESOLVED in v0.16]`

### Task 93: Update CURRENT-STATUS.md and ROADMAP.md

- [ ] Move Phase G from "In flight" to "Where we are"
- [ ] Update at-a-glance status table

### Task 94: Final phase-status doc

- [ ] Mark all G.1-G.10 complete in `04r-phase-g-status.md`
- [ ] Document any remaining deferrals

### Task 95: Refresh baselines

- [ ] Update `~/dev/refactor-engine-merger/baselines/*.txt` from
      the post-G test run
- [ ] Commit on the coordination folder (no git repo there;
      just a file edit)

---

## Verification summary

At each tag boundary:

1. `~/dev/refactor-engine-merger/scripts/verify-all.sh` exits 0
2. libkalburator test count grows monotonically per the
   estimates in this plan
3. PlanStan and WildPalms baselines hold (or shift expectedly
   per the test-move tasks)
4. Phase-status doc reflects current state
5. CURRENT-STATUS.md and ROADMAP.md updated
6. FINDINGS.md appended with anything new
7. User-authorised tag created

Total task count: 95 tasks across 10 sub-phases plus pre-flight
and wrap-up. Estimated 15-17 weeks calendar time with disciplined
execution and subagent parallelism for mechanical migrations.

## Cross-references

- `04r-phase-g-design.md` — canonical specification
- `04r-phase-g-shape-pipeline-ideation.md` — origin
- `04r-phase-g-walkthrough.md`, `…-wildpalms.md`, `…-migration.md` — exploration record
- `04q-phase-f2-threading-plan.md` — precedent for plan format
- `04p-phase-f1-unify-plan.md` — precedent for plan format
- `~/dev/refactor-engine-merger/CLAUDE.md`
- `~/dev/refactor-engine-merger/OPERATIONS.md`
- `~/dev/refactor-engine-merger/FINDINGS.md`

## What this plan deliberately does NOT specify

- **Exact line numbers in code citations** — F1/F2 plan docs cited
  specific line numbers, but the worktree state will drift before
  Phase G work begins. Reference *symbols* and *file paths*; let
  the agent grep for the current line.
- **Subagent dispatch detail per task** — the `superpowers:
  subagent-driven-development` skill handles dispatch. This plan
  identifies which tasks are parallelizable; the executing agent
  decides dispatch strategy.
- **Per-task time budgets** — sub-phase estimates are total; per-
  task estimates are misleading because many tasks gate on builds
  and test runs that vary by machine.
- **Exact code for differs/mergers** — the design doc specifies
  the *contract*; implementation details follow the patterns
  established by the existing `IncidenceDiff` and the F2
  operation-handle migrations.

A new agent picking this up should read the design doc first to
understand the architecture, then this plan to understand the
slicing, then start at Task 1 and proceed sequentially.
