# Phase E — Transcoding into backends Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move `TranscodingRegistry` invocation off `SyncWorker::applyChanges` into each calendar backend's write path, via a per-engine `TranscodingRouter` that produces a `TranscodingPlan` consumed by the backend. After this phase, `SyncWorker.cpp` contains zero `TranscodingRegistry` references and the calendar engine is capability-blind on the normal write path.

**Architecture:** Hybrid — router decides, backend coerces. `TranscodingRouter` owns the routing decision (returns an empty `TranscodingPlan` when source/target backend types match, otherwise a populated plan). The plan threads through `SyncBackend::storeItems`/`updateItem`/`startSync` via a defaulted parameter; each backend invokes a shared `executeTranscodingPlan` helper inside its write method, emits the `transcodingWarning` signal on lossy results, and writes the transcoded clone. `SyncWorker` connects each backend's `transcodingWarning` to its own existing same-named signal, preserving the public `SyncCoordinator::transcodingWarning` contract that D.0 pinned.

**Tech Stack:** Qt6, KCalendarCore (KF6), QTest, CMake. C++20.

**Working tree:** `~/dev/refactor-engine-merger/libkalburator/` (worktree on `refactor/engine-merger`). PlanStan and WildPalms worktrees in sibling directories under the same coordination folder.

**Build:** legacy preset-less project — build dir is `build/`. Use `-j 12`, never `--parallel`.

**Reference design:** `docs/phase0/04n-phase-e-transcoding-design.md`.

**Verify after each task that touches code:** `cmake --build build -j 12` and `cd build && ctest --output-on-failure`. After Tasks 11–13: `bash ~/dev/refactor-engine-merger/scripts/verify-all.sh`.

---

## Pre-flight: confirm production interfaces

Before Task 1, briefly read these to confirm shapes haven't drifted from what the design assumes:

- `src/transcoding/transcodingregistry.{h,cpp}` — confirm `findTranscoders`, `transcodeIncidence` shape; note the warning-list construction on lines 67–93 of `.cpp` (used as model for `executeTranscodingPlan`).
- `src/transcoding/propertytranscoder.h` — confirm `PropertyTranscoder::transcode(Incidence::Ptr&) → bool` and `description()`/`fidelity()` accessors. Namespace is `Kalburator::Sync`.
- `src/calendar/syncbackend.h:151–167` — confirm pure-virtual `storeItems`/`updateItem`/`startSync`/`removeItem` signatures.
- `src/calendar/syncworker.{h,cpp}` — note `transcodingWarning` signal at `syncworker.h:151`; the two `TranscodingRegistry::instance().transcodeIncidence(...)` call sites at `syncworker.cpp:1166–1199`.
- `src/calendar/synccoordinator.{h,cpp}` — note constructor and how it constructs `SyncWorker` (for router injection).
- `tests/calendar/tst_calendar_transcoding_warning.cpp` — the D.0 contract that must keep passing.
- Root `CMakeLists.txt:67–75` — the foreach-glob loop being converted in Task 3.

If any production shape has drifted from the design, **stop** and fix `04n-phase-e-transcoding-design.md` first.

---

## Task 1: `TranscodingPlan` + `executeTranscodingPlan` helper

Pure addition. No callers yet. Compiles into the library.

**Files:**
- Create: `src/transcoding/transcodingplan.h`
- Create: `src/transcoding/transcodingplan.cpp`

- [ ] **Step 1: Write `transcodingplan.h`**

```cpp
// src/transcoding/transcodingplan.h
#ifndef KALBURATOR_TRANSCODINGPLAN_H
#define KALBURATOR_TRANSCODINGPLAN_H

#include <QList>
#include <QString>
#include <QStringList>
#include <KCalendarCore/Incidence>

namespace Kalburator::Sync {

class PropertyTranscoder;

/// Decision produced by TranscodingRouter and consumed by SyncBackend
/// write methods. Borrowed (non-owning) pointers — the transcoders are
/// owned by TranscodingRegistry. A plan must not outlive the registry
/// it was sourced from. In normal use, plans are built at the start of
/// an applyChanges() invocation and discarded at its end.
struct TranscodingPlan {
    QList<PropertyTranscoder*> transcoders;
    QString routingDecision;  // diagnostic only ("source=X target=Y, N transcoders")

    bool isEmpty() const { return transcoders.isEmpty(); }
};

/// Result of executing a plan against an incidence.
struct TranscodingResult {
    KCalendarCore::Incidence::Ptr incidence;  // the (possibly cloned and transcoded) incidence
    QStringList warnings;                     // empty if lossless or plan was empty
};

/// Execute the plan against `original`. If the plan is empty, returns
/// `{original, {}}` with no clone. Otherwise clones the incidence,
/// runs each transcoder in order, accumulates warnings from lossy
/// transcoders, and returns the transcoded clone.
TranscodingResult executeTranscodingPlan(
    const TranscodingPlan& plan,
    const KCalendarCore::Incidence::Ptr& original);

} // namespace Kalburator::Sync

#endif // KALBURATOR_TRANSCODINGPLAN_H
```

- [ ] **Step 2: Write `transcodingplan.cpp`**

```cpp
// src/transcoding/transcodingplan.cpp
#include "transcodingplan.h"
#include "propertytranscoder.h"

namespace Kalburator::Sync {

TranscodingResult executeTranscodingPlan(
    const TranscodingPlan& plan,
    const KCalendarCore::Incidence::Ptr& original)
{
    if (plan.isEmpty() || !original) {
        return TranscodingResult{original, {}};
    }

    auto transcoded = KCalendarCore::Incidence::Ptr(original->clone());
    QStringList warnings;
    for (auto* transcoder : plan.transcoders) {
        if (!transcoder) {
            continue;
        }
        if (transcoder->transcode(transcoded)) {
            if (transcoder->fidelity() != TranscodingFidelity::Lossless) {
                warnings.append(transcoder->description());
            }
        }
    }
    return TranscodingResult{transcoded, warnings};
}

} // namespace Kalburator::Sync
```

- [ ] **Step 3: Build to confirm it compiles standalone**

Note: the root `CMakeLists.txt` globs `src/transcoding/*.h` and `*.cpp`, so the new files are picked up automatically. After Task 3 they'll be in an explicit list — but until then, the glob handles it.

```bash
cmake --build build -j 12
```

Expected: succeeds. The new files compile as part of `kalburator` (no callers yet). If `kalburator_autogen/timestamp` causes a vtable error (per FINDINGS), wipe it and retry: `rm build/kalburator_autogen/timestamp && cmake --build build -j 12`.

- [ ] **Step 4: Commit**

```bash
git add src/transcoding/transcodingplan.h src/transcoding/transcodingplan.cpp
git commit -m "feat(transcoding): TranscodingPlan + executeTranscodingPlan helper (Task 1)

Phase E foundation. Pure addition; no callers yet."
```

---

## Task 2: `TranscodingRouter` + `tst_transcoding_router`

Add the router and its unit test. Each test owns its own `TranscodingRegistry` to avoid the singleton-cleanup hazard documented in FINDINGS.

**Files:**
- Create: `src/transcoding/transcodingrouter.h`
- Create: `src/transcoding/transcodingrouter.cpp`
- Create: `tests/transcoding/CMakeLists.txt` (new sub-directory)
- Create: `tests/transcoding/tst_transcoding_router.cpp`
- Modify: `tests/CMakeLists.txt` (add `add_subdirectory(transcoding)`)

- [ ] **Step 1: Write `transcodingrouter.h`**

```cpp
// src/transcoding/transcodingrouter.h
#ifndef KALBURATOR_TRANSCODINGROUTER_H
#define KALBURATOR_TRANSCODINGROUTER_H

#include "transcodingplan.h"
#include <QString>

namespace Kalburator::Sync {

class TranscodingRegistry;

/// Routes transcoding decisions for a per-engine. Owns no state of
/// its own beyond a reference to the registry it queries. The
/// registry must outlive the router. Production code constructs the
/// router with TranscodingRegistry::instance(); tests can construct
/// a fresh registry per test for isolation.
///
/// Phase E semantics: gate is `sourceType != targetType`; capability
/// objects are not consulted. Capability-aware routing is deferred
/// to Phase F (see 04n-phase-e-transcoding-design.md §7).
class TranscodingRouter
{
public:
    explicit TranscodingRouter(TranscodingRegistry& registry);

    TranscodingPlan plan(const QString& sourceType,
                         const QString& targetType) const;

private:
    TranscodingRegistry& m_registry;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_TRANSCODINGROUTER_H
```

- [ ] **Step 2: Write `transcodingrouter.cpp`**

```cpp
// src/transcoding/transcodingrouter.cpp
#include "transcodingrouter.h"
#include "transcodingregistry.h"

namespace Kalburator::Sync {

TranscodingRouter::TranscodingRouter(TranscodingRegistry& registry)
    : m_registry(registry)
{}

TranscodingPlan TranscodingRouter::plan(const QString& sourceType,
                                        const QString& targetType) const
{
    if (sourceType.isEmpty() || targetType.isEmpty()
        || sourceType == targetType) {
        return TranscodingPlan{};
    }
    auto transcoders = m_registry.findTranscoders(sourceType, targetType);
    if (transcoders.isEmpty()) {
        return TranscodingPlan{};
    }
    TranscodingPlan plan;
    plan.transcoders = transcoders;
    plan.routingDecision = QStringLiteral("source=%1 target=%2, %3 transcoders")
        .arg(sourceType, targetType)
        .arg(transcoders.size());
    return plan;
}

} // namespace Kalburator::Sync
```

- [ ] **Step 3: Write the failing test**

```cpp
// tests/transcoding/tst_transcoding_router.cpp
#include <QtTest>
#include <KCalendarCore/Event>

#include "transcodingrouter.h"
#include "transcodingregistry.h"
#include "propertytranscoder.h"

using namespace Kalburator::Sync;

namespace {

class FakeTranscoder : public PropertyTranscoder
{
public:
    FakeTranscoder(QString src, QString tgt, TranscodingFidelity fid)
        : m_source(std::move(src)), m_target(std::move(tgt)), m_fidelity(fid) {}

    QString propertyName() const override { return QStringLiteral("FAKE"); }
    QString sourceBackendType() const override { return m_source; }
    QString targetBackendType() const override { return m_target; }
    TranscodingFidelity fidelity() const override { return m_fidelity; }
    bool transcode(KCalendarCore::Incidence::Ptr&) const override { return true; }
    QString description() const override { return QStringLiteral("fake transcoder"); }

private:
    QString m_source;
    QString m_target;
    TranscodingFidelity m_fidelity;
};

} // namespace

class TestTranscodingRouter : public QObject
{
    Q_OBJECT

private slots:
    void cleanup()
    {
        // Singleton hygiene per FINDINGS 2026-04-28; Phase E new code uses
        // a stack registry where possible, but tests of the router with
        // the singleton still need this.
        TranscodingRegistry::instance().clear();
    }

    void emptySourceType_returnsEmptyPlan()
    {
        TranscodingRegistry registryUnused;
        TranscodingRouter router(TranscodingRegistry::instance());
        const auto plan = router.plan(QString(), QStringLiteral("orgmode"));
        QVERIFY(plan.isEmpty());
    }

    void emptyTargetType_returnsEmptyPlan()
    {
        TranscodingRouter router(TranscodingRegistry::instance());
        const auto plan = router.plan(QStringLiteral("local"), QString());
        QVERIFY(plan.isEmpty());
    }

    void equalTypes_returnsEmptyPlan()
    {
        TranscodingRegistry::instance().registerTranscoder(
            std::make_unique<FakeTranscoder>(QStringLiteral("*"),
                                             QStringLiteral("*"),
                                             TranscodingFidelity::Lossy));
        TranscodingRouter router(TranscodingRegistry::instance());
        const auto plan = router.plan(QStringLiteral("local"),
                                      QStringLiteral("local"));
        QVERIFY(plan.isEmpty());
    }

    void differingTypes_noMatchingTranscoder_returnsEmptyPlan()
    {
        TranscodingRegistry::instance().registerTranscoder(
            std::make_unique<FakeTranscoder>(QStringLiteral("orgmode"),
                                             QStringLiteral("orgmode"),
                                             TranscodingFidelity::Lossy));
        TranscodingRouter router(TranscodingRegistry::instance());
        const auto plan = router.plan(QStringLiteral("local"),
                                      QStringLiteral("caldav"));
        QVERIFY(plan.isEmpty());
    }

    void differingTypes_matchingTranscoder_returnsPopulatedPlan()
    {
        TranscodingRegistry::instance().registerTranscoder(
            std::make_unique<FakeTranscoder>(QStringLiteral("*"),
                                             QStringLiteral("orgmode"),
                                             TranscodingFidelity::Lossy));
        TranscodingRouter router(TranscodingRegistry::instance());
        const auto plan = router.plan(QStringLiteral("local"),
                                      QStringLiteral("orgmode"));
        QVERIFY(!plan.isEmpty());
        QCOMPARE(plan.transcoders.size(), 1);
        QVERIFY(plan.routingDecision.contains(QStringLiteral("source=local")));
        QVERIFY(plan.routingDecision.contains(QStringLiteral("target=orgmode")));
    }
};

QTEST_GUILESS_MAIN(TestTranscodingRouter)
#include "tst_transcoding_router.moc"
```

- [ ] **Step 4: Write `tests/transcoding/CMakeLists.txt`**

```cmake
# tests/transcoding/CMakeLists.txt

add_executable(tst_transcoding_router tst_transcoding_router.cpp)
target_link_libraries(tst_transcoding_router
    PRIVATE
        Qt6::Test
        Qt6::Core
        KF6::CalendarCore
        Kalburator::Sync
)
add_test(NAME tst_transcoding_router COMMAND tst_transcoding_router)
```

- [ ] **Step 5: Wire into `tests/CMakeLists.txt`**

Add `add_subdirectory(transcoding)` next to the other `add_subdirectory` lines (calendar, blob, journal). Verify with:

```bash
grep -n "add_subdirectory" tests/CMakeLists.txt
```

- [ ] **Step 6: Configure and build**

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j 12 --target tst_transcoding_router
```

If vtable error, `rm build/kalburator_autogen/timestamp && cmake --build build -j 12`.

- [ ] **Step 7: Run the test**

```bash
cd build && ctest --output-on-failure -R tst_transcoding_router
```

Expected: PASS, 5 test cases.

- [ ] **Step 8: Commit**

```bash
git add src/transcoding/transcodingrouter.h src/transcoding/transcodingrouter.cpp \
        tests/transcoding/CMakeLists.txt tests/transcoding/tst_transcoding_router.cpp \
        tests/CMakeLists.txt
git commit -m "feat(transcoding): TranscodingRouter + tst_transcoding_router (Task 2)

Per-engine router with injectable registry, white-box unit tested
against five scenarios (empty source/target, equal types, no
matching transcoder, matching transcoder)."
```

---

## Task 3: Convert root `CMakeLists.txt` glob loop to explicit source lists

Removes the AUTOMOC-timestamp footgun documented in FINDINGS. After this task, adding a new `Q_OBJECT` class to a SYNC_SUBDIR no longer requires `rm build/kalburator_autogen/timestamp`.

**Files:**
- Modify: `CMakeLists.txt` (lines 67–75 region)

- [ ] **Step 1: Read the current loop**

Confirm the current shape at `CMakeLists.txt:67–75`:

```cmake
set(KALBURATOR_SYNC_SUBDIRS calendar conflict transcoding journal discovery blob)
set(KALBURATOR_SYNC_HEADERS "")
set(KALBURATOR_SYNC_SOURCES "")
foreach(_subdir IN LISTS KALBURATOR_SYNC_SUBDIRS)
    file(GLOB _subdir_headers CONFIGURE_DEPENDS src/${_subdir}/*.h)
    file(GLOB _subdir_sources CONFIGURE_DEPENDS src/${_subdir}/*.cpp)
    list(APPEND KALBURATOR_SYNC_HEADERS ${_subdir_headers})
    list(APPEND KALBURATOR_SYNC_SOURCES ${_subdir_sources})
endforeach()
```

- [ ] **Step 2: Enumerate actual files in each subdir**

```bash
for d in calendar conflict transcoding journal discovery blob; do
    echo "=== $d ==="
    ls src/$d/*.h src/$d/*.cpp 2>/dev/null
done
```

Capture the output — these are the source lists that go into the explicit version.

- [ ] **Step 3: Replace the foreach with explicit lists**

Replace lines 67–75 of `CMakeLists.txt` with the following shape (substitute the actual file lists from Step 2 — do not omit any header or cpp file):

```cmake
set(KALBURATOR_CALENDAR_HEADERS
    src/calendar/akonadibackend.h
    src/calendar/backendcapabilities.h
    src/calendar/backendregistry.h
    src/calendar/calendarbaselinestore.h
    src/calendar/calendarmanager.h
    src/calendar/decsyncbackend.h
    src/calendar/holidaysubscriptionbackend.h
    src/calendar/icalendarcollection.h
    src/calendar/iincidenceregistry.h
    src/calendar/isynchost.h
    src/calendar/localbackend.h
    src/calendar/mockbackend.h
    src/calendar/orgbackend.h
    src/calendar/remotebackend.h
    src/calendar/subscriptionbackend.h
    src/calendar/syncbackend.h
    src/calendar/syncconfigstore.h
    src/calendar/syncconflictstore.h
    src/calendar/synccoordinator.h
    src/calendar/syncworker.h
)
set(KALBURATOR_CALENDAR_SOURCES
    src/calendar/akonadibackend.cpp
    src/calendar/backendcapabilities.cpp
    src/calendar/backendregistry.cpp
    src/calendar/calendarbaselinestore.cpp
    src/calendar/calendarmanager.cpp
    src/calendar/decsyncbackend.cpp
    src/calendar/holidaysubscriptionbackend.cpp
    src/calendar/localbackend.cpp
    src/calendar/mockbackend.cpp
    src/calendar/orgbackend.cpp
    src/calendar/remotebackend.cpp
    src/calendar/subscriptionbackend.cpp
    src/calendar/syncbackend.cpp
    src/calendar/syncconfigstore.cpp
    src/calendar/syncconflictstore.cpp
    src/calendar/synccoordinator.cpp
    src/calendar/syncworker.cpp
)

# Repeat per subdir: KALBURATOR_CONFLICT_*, KALBURATOR_TRANSCODING_*,
# KALBURATOR_JOURNAL_*, KALBURATOR_DISCOVERY_*, KALBURATOR_BLOB_*.
# Use the actual file list from Step 2 — do not skip any file.

# Aggregate
set(KALBURATOR_SYNC_HEADERS
    ${KALBURATOR_CALENDAR_HEADERS}
    ${KALBURATOR_CONFLICT_HEADERS}
    ${KALBURATOR_TRANSCODING_HEADERS}
    ${KALBURATOR_JOURNAL_HEADERS}
    ${KALBURATOR_DISCOVERY_HEADERS}
    ${KALBURATOR_BLOB_HEADERS}
)
set(KALBURATOR_SYNC_SOURCES
    ${KALBURATOR_CALENDAR_SOURCES}
    ${KALBURATOR_CONFLICT_SOURCES}
    ${KALBURATOR_TRANSCODING_SOURCES}
    ${KALBURATOR_JOURNAL_SOURCES}
    ${KALBURATOR_DISCOVERY_SOURCES}
    ${KALBURATOR_BLOB_SOURCES}
)
```

The transcoding subdir specifically must include the new files from Tasks 1 and 2:

```cmake
set(KALBURATOR_TRANSCODING_HEADERS
    src/transcoding/incidencediff.h
    src/transcoding/propertytranscoder.h
    src/transcoding/rruletranscoder.h
    src/transcoding/syncdiff.h
    src/transcoding/transcodingplan.h          # new in Task 1
    src/transcoding/transcodingregistry.h
    src/transcoding/transcodingrouter.h        # new in Task 2
)
set(KALBURATOR_TRANSCODING_SOURCES
    src/transcoding/incidencediff.cpp
    src/transcoding/propertytranscoder.cpp
    src/transcoding/rruletranscoder.cpp
    src/transcoding/syncdiff.cpp
    src/transcoding/transcodingplan.cpp        # new in Task 1
    src/transcoding/transcodingregistry.cpp
    src/transcoding/transcodingrouter.cpp      # new in Task 2
)
```

The downstream filtering (`list(FILTER ...)` for `KALBURATOR_HAVE_ORG_IO` and `KALBURATOR_HAVE_AKONADI`, lines 77–85) stays exactly as-is — it operates on `KALBURATOR_SYNC_HEADERS` / `KALBURATOR_SYNC_SOURCES` and does not need to change.

- [ ] **Step 4: Wipe build and reconfigure clean**

```bash
rm -rf build
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j 12
```

Expected: clean build succeeds with the same target shape as before.

- [ ] **Step 5: Run all tests to confirm no regression**

```bash
cd build && ctest --output-on-failure
```

Expected: 20/20 pass (19 from Phase D baseline + 1 new `tst_transcoding_router` from Task 2).

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: convert SYNC_SUBDIRS glob loop to explicit source lists (Task 3)

Removes the AUTOMOC-timestamp footgun documented in FINDINGS
(2026-04-28). Adding a new Q_OBJECT class no longer requires
manual rm of build/kalburator_autogen/timestamp."
```

---

## Task 4: `SyncBackend` interface — signal + defaulted plan parameters

Pure interface change. The base class declarations grow the new signal and the new defaulted parameter on three methods. Concrete backends are not updated yet — the default argument keeps existing call sites compiling.

**Files:**
- Modify: `src/calendar/syncbackend.h`

- [ ] **Step 1: Add include + forward declaration**

In `syncbackend.h`, near the top of the file (just after the existing `#include` block but inside the `Kalburator::Sync` namespace if applicable), add:

```cpp
#include "transcodingplan.h"
```

Confirm `Kalburator::Sync::TranscodingPlan` is now visible in this header.

- [ ] **Step 2: Add the signal**

In the `signals:` section of the `SyncBackend` class (Q_OBJECT-bearing — it's already a QObject subclass), add:

```cpp
signals:
    // ... existing signals ...

    /// Emitted when a write operation invokes a non-lossless transcoder.
    /// Carries calendar id, incidence uid, and the warning descriptions
    /// from each transcoder that contributed to the loss.
    void transcodingWarning(const QString& calendarId,
                            const QString& uid,
                            const QStringList& warnings);
```

- [ ] **Step 3: Add the defaulted parameter to three pure-virtual methods**

Modify `syncbackend.h:151–164`:

```cpp
virtual void storeItems(KCalendarCore::MemoryCalendar* cal,
                        const QList<KCalendarCore::Incidence::Ptr>& items,
                        const TranscodingPlan& plan = TranscodingPlan{}) = 0;

virtual void updateItem(KCalendarCore::MemoryCalendar* cal,
                        const KCalendarCore::Incidence::Ptr& item,
                        const QString& icalData,
                        const TranscodingPlan& plan = TranscodingPlan{}) = 0;

virtual void startSync(const QString& collectionId,
                       KCalendarCore::MemoryCalendar* calendar,
                       const QList<KCalendarCore::Incidence::Ptr>& stagedCreations,
                       const QList<KCalendarCore::Incidence::Ptr>& stagedUpdates,
                       const QList<KCalendarCore::Incidence::Ptr>& stagedDeletions,
                       const TranscodingPlan& plan = TranscodingPlan{}) = 0;
```

`removeItem` is **not** modified — deletes do not transcode.

- [ ] **Step 4: Add Phase F flag comment near the operation API**

Locate the comment line near the operation-based API (`pushItems`/`PushOperation` etc., around line 169 of `syncbackend.h`):

```cpp
// ========== Operation-Based API (Preferred) ==========
// These methods return trackable SyncOperation handles and work with
// ...
```

Insert after the existing comment block:

```cpp
// Phase E note (2026-04-29): the operation-based API does not yet
// carry a TranscodingPlan parameter. If this API survives Phase F's
// threading-API redesign, it inherits the same plan-passing pattern
// used by storeItems/updateItem/startSync.
```

- [ ] **Step 5: Build (expected to FAIL)**

```bash
cmake --build build -j 12
```

Expected: compilation **fails** — every concrete backend's override of `storeItems`, `updateItem`, `startSync` no longer matches the base signature. This is intentional. The next tasks fix each backend.

- [ ] **Step 6: Note the expected failure**

The build will report eight backends with mismatched override signatures: `MockBackend`, `LocalBackend`, `RemoteBackend`, `OrgBackend` (if KALBURATOR_HAVE_ORG_IO), `AkonadiBackend` (if KALBURATOR_HAVE_AKONADI), `DecSyncBackend`, `SubscriptionBackend`, `HolidaySubscriptionBackend`. Tasks 5–7 fix them.

**Do not commit yet.** Tasks 5–7 land together with this signature change as one logical "interface widened" group, or each backend can be its own commit; plan-author choice. The recommended pattern below is one commit for Tasks 4–5 (interface + MockBackend so the test compiles), then one commit for Task 6 (other writable backends), then one commit for Task 7 (read-only backends). Adjust per your taste; the tests don't pass until all eight are wired.

---

## Task 5: `MockBackend` honors plan + emits signal

`MockBackend` is the canonical reference implementation and is what `tst_calendar_transcoding_warning.cpp` uses. Wiring it first lets the D.0 contract test pass before the other backends are touched.

**Files:**
- Modify: `src/calendar/mockbackend.h`
- Modify: `src/calendar/mockbackend.cpp`

- [ ] **Step 1: Update `mockbackend.h`**

Match the new base-class signatures exactly. In the override declarations, **do not** redeclare the default value (the base class owns it).

```cpp
void storeItems(KCalendarCore::MemoryCalendar* cal,
                const QList<KCalendarCore::Incidence::Ptr>& items,
                const TranscodingPlan& plan) override;

void updateItem(KCalendarCore::MemoryCalendar* cal,
                const KCalendarCore::Incidence::Ptr& item,
                const QString& icalData,
                const TranscodingPlan& plan) override;

void startSync(const QString& collectionId,
               KCalendarCore::MemoryCalendar* calendar,
               const QList<KCalendarCore::Incidence::Ptr>& stagedCreations,
               const QList<KCalendarCore::Incidence::Ptr>& stagedUpdates,
               const QList<KCalendarCore::Incidence::Ptr>& stagedDeletions,
               const TranscodingPlan& plan) override;
```

- [ ] **Step 2: Add `#include "transcodingplan.h"` to `mockbackend.cpp`**

- [ ] **Step 3: Update each method to honor the plan**

Pattern for `storeItems` (apply the same shape to `updateItem` and the per-item processing inside `startSync`):

```cpp
void MockBackend::storeItems(KCalendarCore::MemoryCalendar* cal,
                             const QList<KCalendarCore::Incidence::Ptr>& items,
                             const TranscodingPlan& plan)
{
    const QString calId = cal ? cal->id() : QString();
    QList<KCalendarCore::Incidence::Ptr> finalItems;
    finalItems.reserve(items.size());

    for (const auto& original : items) {
        auto result = executeTranscodingPlan(plan, original);
        if (!result.warnings.isEmpty() && original) {
            emit transcodingWarning(calId, original->uid(), result.warnings);
        }
        finalItems.append(result.incidence);
    }

    // ... existing storage logic, but using finalItems instead of items ...
}
```

For `updateItem`, the singular form:

```cpp
void MockBackend::updateItem(KCalendarCore::MemoryCalendar* cal,
                             const KCalendarCore::Incidence::Ptr& item,
                             const QString& icalData,
                             const TranscodingPlan& plan)
{
    auto result = executeTranscodingPlan(plan, item);
    if (!result.warnings.isEmpty() && item) {
        emit transcodingWarning(cal ? cal->id() : QString(),
                                item->uid(), result.warnings);
    }
    // ... existing update logic, using result.incidence and possibly the
    // rewritten icalData. If the existing logic uses icalData verbatim,
    // the transcoded incidence may need re-serialization; mirror what
    // SyncWorker::applyChangesToBackend used to do (it cloned + re-serialized
    // before this phase) ...
}
```

For `startSync`, apply the plan to each incidence in `stagedCreations` and `stagedUpdates`; do not transcode `stagedDeletions`:

```cpp
void MockBackend::startSync(const QString& collectionId,
                            KCalendarCore::MemoryCalendar* calendar,
                            const QList<KCalendarCore::Incidence::Ptr>& stagedCreations,
                            const QList<KCalendarCore::Incidence::Ptr>& stagedUpdates,
                            const QList<KCalendarCore::Incidence::Ptr>& stagedDeletions,
                            const TranscodingPlan& plan)
{
    const QString calId = calendar ? calendar->id() : QString();

    QList<KCalendarCore::Incidence::Ptr> finalCreations;
    finalCreations.reserve(stagedCreations.size());
    for (const auto& original : stagedCreations) {
        auto result = executeTranscodingPlan(plan, original);
        if (!result.warnings.isEmpty() && original) {
            emit transcodingWarning(calId, original->uid(), result.warnings);
        }
        finalCreations.append(result.incidence);
    }

    QList<KCalendarCore::Incidence::Ptr> finalUpdates;
    finalUpdates.reserve(stagedUpdates.size());
    for (const auto& original : stagedUpdates) {
        auto result = executeTranscodingPlan(plan, original);
        if (!result.warnings.isEmpty() && original) {
            emit transcodingWarning(calId, original->uid(), result.warnings);
        }
        finalUpdates.append(result.incidence);
    }

    // ... existing logic, using finalCreations / finalUpdates / stagedDeletions ...
}
```

- [ ] **Step 4: Build**

```bash
cmake --build build -j 12 --target tst_calendar_transcoding_warning
```

The full library still won't compile (Tasks 6–7 unfixed). The specific test target may build if MockBackend is the only backend it links against; otherwise wait for Task 7.

- [ ] **Step 5: Hold off on commit**

Bundle this commit with Task 4 once Task 7 is green. See the note at the bottom of Task 4.

---

## Task 6: Wire writable concrete backends

Apply the same plan-honoring pattern from Task 5 to the five remaining writable backends: `LocalBackend`, `RemoteBackend`, `OrgBackend`, `AkonadiBackend`, `DecSyncBackend`. Each backend's `storeItems`/`updateItem`/`startSync` follows the MockBackend template:

1. For each incoming `Incidence::Ptr`, call `executeTranscodingPlan(plan, original)`.
2. If `result.warnings` is non-empty, emit `transcodingWarning(calId, original->uid(), result.warnings)`.
3. Use `result.incidence` instead of the original in the existing write logic.
4. For `startSync`: apply only to `stagedCreations` and `stagedUpdates`, never to `stagedDeletions`.

**Files (per backend):**
- Modify: `src/calendar/localbackend.{h,cpp}`
- Modify: `src/calendar/remotebackend.{h,cpp}`
- Modify: `src/calendar/orgbackend.{h,cpp}` (gated on `KALBURATOR_HAVE_ORG_IO`)
- Modify: `src/calendar/akonadibackend.{h,cpp}` (gated on `KALBURATOR_HAVE_AKONADI`)
- Modify: `src/calendar/decsyncbackend.{h,cpp}`

For each backend:

- [ ] **Step 1: Update header signatures** to match the base-class shape (same as Task 5 Step 1, but for this backend).
- [ ] **Step 2: `#include "transcodingplan.h"` in the `.cpp`**.
- [ ] **Step 3: Apply the plan-honoring pattern** to `storeItems`, `updateItem`, `startSync` (same as Task 5 Steps 3, but adapted to whatever existing logic this backend has — preserve all existing behavior, just substitute `result.incidence` for the original and emit on warnings).

**Wrinkles per backend:**

- **`LocalBackend`** — calls `MemoryCalendar::addIncidence` and writes ICS to disk. After transcoding, the in-memory incidence is the transcoded clone; the on-disk write picks up the clone's serialized form automatically because it serializes from the calendar.
- **`RemoteBackend`** — calls KDAV. The clone re-serializes naturally; no special handling needed beyond using `result.incidence`.
- **`OrgBackend`** — writes via OrgModeParser. Use `result.incidence` directly; warnings emit before the write so the user sees warnings even if the write later fails.
- **`AkonadiBackend`** — writes via `Akonadi::ItemCreateJob` etc. Same treatment.
- **`DecSyncBackend`** — DecSync stores by full content hash; the transcoded clone produces a new hash, which is the desired behavior.

- [ ] **Step 4: Build + sanity-check the full library**

```bash
cmake --build build -j 12
```

Expected: all backends compile. If a backend breaks because some private helper's signature relied on the un-transcoded item, fix it minimally — preserve the existing behavior for everything except "use the transcoded version of the item."

---

## Task 7: Read-only backends (accept-and-ignore)

`SubscriptionBackend` and `HolidaySubscriptionBackend` have `BackendCapabilities::canCreate == false`. They cannot be write targets. Their existing `storeItems`/`updateItem`/`startSync` implementations either no-op, throw, or return early. They must still match the new base-class signature.

**Files:**
- Modify: `src/calendar/subscriptionbackend.{h,cpp}`
- Modify: `src/calendar/holidaysubscriptionbackend.{h,cpp}`

- [ ] **Step 1: Update header signatures** to match the base (same as Task 5 Step 1).

- [ ] **Step 2: In the `.cpp`, accept the parameter and ignore it**

The plan parameter is unused in read-only backends. Suppress the unused-parameter warning explicitly:

```cpp
void SubscriptionBackend::storeItems(KCalendarCore::MemoryCalendar* /*cal*/,
                                     const QList<KCalendarCore::Incidence::Ptr>& /*items*/,
                                     const TranscodingPlan& /*plan*/)
{
    // Read-only backend; no-op (existing behavior).
}
```

If the existing implementation logs a warning or throws, preserve that — do not silently change semantics.

- [ ] **Step 3: Build**

```bash
cmake --build build -j 12
```

Expected: full library compiles.

- [ ] **Step 4: Commit Tasks 4–7 as one or more logical commits**

Suggested split (plan-author may bundle differently):

```bash
# Bundle 1: interface + Mock so the existing transcoding test target builds
git add src/calendar/syncbackend.h \
        src/calendar/mockbackend.h src/calendar/mockbackend.cpp
git commit -m "feat(calendar): SyncBackend transcodingWarning + plan param; Mock wired (Tasks 4–5)

Phase E. Interface widened with TranscodingPlan defaulted parameter
on storeItems/updateItem/startSync and a transcodingWarning signal.
MockBackend wires the plan execution; remaining concrete backends
follow in subsequent commits (compile-broken in interim)."

# Bundle 2: writable backends
git add src/calendar/localbackend.h src/calendar/localbackend.cpp \
        src/calendar/remotebackend.h src/calendar/remotebackend.cpp \
        src/calendar/orgbackend.h src/calendar/orgbackend.cpp \
        src/calendar/akonadibackend.h src/calendar/akonadibackend.cpp \
        src/calendar/decsyncbackend.h src/calendar/decsyncbackend.cpp
git commit -m "feat(calendar): five writable backends honor TranscodingPlan (Task 6)

LocalBackend, RemoteBackend, OrgBackend, AkonadiBackend, DecSyncBackend
each invoke executeTranscodingPlan in storeItems/updateItem/startSync
and emit transcodingWarning on lossy results."

# Bundle 3: read-only backends
git add src/calendar/subscriptionbackend.h src/calendar/subscriptionbackend.cpp \
        src/calendar/holidaysubscriptionbackend.h src/calendar/holidaysubscriptionbackend.cpp
git commit -m "feat(calendar): subscription backends accept TranscodingPlan (Task 7)

SubscriptionBackend and HolidaySubscriptionBackend are read-only
(canCreate=false); plan parameter is accepted and ignored."
```

---

## Task 8: `SyncCoordinator` owns `TranscodingRouter`; injects into `SyncWorker`

The router is a per-engine instance (per design decision R2). `SyncCoordinator` is the natural owner — it already constructs `SyncWorker` and passes it dependencies.

**Files:**
- Modify: `src/calendar/synccoordinator.h`
- Modify: `src/calendar/synccoordinator.cpp`
- Modify: `src/calendar/syncworker.h`
- Modify: `src/calendar/syncworker.cpp`

- [ ] **Step 1: Add `TranscodingRouter` member to `SyncCoordinator`**

In `synccoordinator.h`, near the top:

```cpp
#include "transcodingrouter.h"
```

In the private member section:

```cpp
private:
    // ... existing members ...
    TranscodingRouter m_transcodingRouter;
```

Constructor initialiser list (in `synccoordinator.cpp`):

```cpp
SyncCoordinator::SyncCoordinator(/* ... existing args ... */)
    : /* ... existing initialisers ... */
    , m_transcodingRouter(TranscodingRegistry::instance())
{
    // ... existing body ...
}
```

`#include "transcodingregistry.h"` near the top of `synccoordinator.cpp` if not already present.

- [ ] **Step 2: Pass router into `SyncWorker` constructor**

In `syncworker.h`, add:

```cpp
#include "transcodingrouter.h"
```

Add a parameter to the constructor (typically the last parameter, before any default-valued tail):

```cpp
SyncWorker(/* existing parameters */,
           const TranscodingRouter& router);
```

Add a member:

```cpp
private:
    // ... existing members ...
    const TranscodingRouter& m_router;
```

In `syncworker.cpp`, initialiser:

```cpp
SyncWorker::SyncWorker(/* ... existing args ... */,
                       const TranscodingRouter& router)
    : /* ... existing inits ... */
    , m_router(router)
{
    // ... existing body ...
}
```

- [ ] **Step 3: Update `SyncCoordinator`'s `SyncWorker` construction site**

Find where `SyncCoordinator` constructs the `SyncWorker` (search `new SyncWorker` or `make_unique<SyncWorker>` in `synccoordinator.cpp`). Add `m_transcodingRouter` as the new last argument.

- [ ] **Step 4: Build**

```bash
cmake --build build -j 12
```

Expected: succeeds. No behavior change yet — the router is owned but unused.

- [ ] **Step 5: Commit**

```bash
git add src/calendar/synccoordinator.h src/calendar/synccoordinator.cpp \
        src/calendar/syncworker.h src/calendar/syncworker.cpp
git commit -m "refactor(calendar): SyncCoordinator owns TranscodingRouter (Task 8)

Per-engine router constructed with TranscodingRegistry::instance(),
passed by const-ref into SyncWorker. Not yet consulted; Task 9
wires the call sites."
```

---

## Task 9: `SyncWorker::applyChangesToBackend` — replace `TranscodingRegistry` with router; connect backend signals

The structural payoff. After this task, `SyncWorker.cpp` has zero `TranscodingRegistry` references; the `transcodingWarning` chain runs through each backend's signal forwarded by the worker.

**Files:**
- Modify: `src/calendar/syncworker.cpp`

- [ ] **Step 1: Connect each backend's `transcodingWarning` signal**

Find where `SyncWorker` connects to backend signals (search for existing `connect(.*backend.*Signal` patterns; this is typically in a method like `attachBackend`, `connectBackendSignals`, or inside the backend-iteration block of `processSync` / setup code). Add:

```cpp
connect(backend, &SyncBackend::transcodingWarning,
        this, &SyncWorker::transcodingWarning,
        Qt::DirectConnection);
```

`Qt::DirectConnection` because backend write methods run on the worker thread; both signal and slot are co-located. If the surrounding code uses a different connection type for similar signals, match it.

- [ ] **Step 2: Replace the two `TranscodingRegistry` call sites in `applyChangesToBackend`**

Locate `syncworker.cpp:1108–1210` (the body of `applyChangesToBackend`).

**At the top of the method** (after `sourceType` and `targetType` are derived around lines 1131–1145), build the plan once per direction:

```cpp
const TranscodingPlan plan = m_router.plan(sourceType, targetType);
```

**Delete the two `TranscodingRegistry::instance().transcodeIncidence(...)` blocks** (around lines 1166–1199 — both the Created and Modified branches). Specifically, delete:

```cpp
// DELETE:
if (needsTranscoding) {
    auto transcoded = KCalendarCore::Incidence::Ptr(inc->clone());
    QStringList warnings = TranscodingRegistry::instance()
        .transcodeIncidence(sourceType, targetType, transcoded);
    if (!warnings.isEmpty()) {
        emit transcodingWarning(calendarId, inc->uid(), warnings);
    }
    // ... whatever the existing code did with `transcoded` ...
}
```

**Pass the plan into the backend write call.** The existing `applyChangesToBackend` builds `CreateIncidenceItem`/`UpdateIncidenceItem` wrappers and applies them. Find the actual `backend->storeItems(...)` / `backend->updateItem(...)` invocations downstream (these may live in the `apply()` method of those wrapper classes, or inline). Add `plan` as the new last argument:

```cpp
backend->storeItems(cal, items, plan);
backend->updateItem(cal, item, icalData, plan);
```

If the wrapper's `apply()` method does not currently have access to the plan, plumb it through — either as a constructor argument to the wrapper, or as a parameter to `apply()`. Choose the path that mirrors how other per-direction context (like `targetType` itself) flows today.

- [ ] **Step 3: Remove the `#include "transcodingregistry.h"` line from `syncworker.cpp`** if it was included only for the deleted call sites.

- [ ] **Step 4: Confirm via grep**

```bash
git grep -n "TranscodingRegistry" src/calendar/
```

Expected: zero hits (the design's success criterion).

```bash
git grep -n "TranscodingRegistry" src/
```

Expected: only `src/transcoding/transcodingregistry.{h,cpp}` (the registry's own definition) and `src/transcoding/transcodingrouter.cpp` (router's own use). No occurrences in `src/calendar/`.

- [ ] **Step 5: Build**

```bash
cmake --build build -j 12
```

Expected: succeeds.

- [ ] **Step 6: Run the calendar test suite**

```bash
cd build && ctest --output-on-failure -R "calendar|transcoding"
```

Expected: all calendar tests pass, including `tst_calendar_transcoding_warning` (D.0 contract). The test now exercises the full chain: `MockBackend` runs the plan, emits `SyncBackend::transcodingWarning`, `SyncWorker` forwards to its own `transcodingWarning`, `SyncCoordinator` forwards to its public signal, the test's `QSignalSpy` observes it.

- [ ] **Step 7: Run the full library test suite**

```bash
cd build && ctest --output-on-failure
```

Expected: 20/20 pass.

- [ ] **Step 8: Commit**

```bash
git add src/calendar/syncworker.cpp
git commit -m "refactor(calendar): SyncWorker uses TranscodingRouter; no TranscodingRegistry refs (Task 9)

Phase E payoff. applyChangesToBackend asks m_router for a
TranscodingPlan once per direction; the plan flows through to the
target backend's write methods. Each backend's transcodingWarning
signal forwards through SyncWorker::transcodingWarning, preserving
the public SyncCoordinator::transcodingWarning contract pinned by
the D.0 test (tst_calendar_transcoding_warning).

git grep TranscodingRegistry src/calendar/ now returns zero hits."
```

---

## Task 10: Verify D.0 contract + full calendar suite green

A standalone verification step before touching consumers.

- [ ] **Step 1: Run `tst_calendar_transcoding_warning` in isolation**

```bash
cd build && ctest --output-on-failure -R tst_calendar_transcoding_warning -V
```

Expected: PASS. The signal payload (calendarId, uid, warnings) and the warning content ("BYDAY") match D.0's assertions.

- [ ] **Step 2: Run all `tests/calendar/`**

```bash
cd build && ctest --output-on-failure -R "calendar|transcoding"
```

Expected: every test passes. If any test fails, stop and diagnose. A common failure mode: a backend's `updateItem` re-serialized from the original `icalData` instead of the transcoded incidence — the transcoded changes get overwritten on disk. Check the `updateItem` implementation in the failing backend's `.cpp`.

- [ ] **Step 3: Run the full suite**

```bash
cd build && ctest --output-on-failure
```

Expected: 20/20 pass.

No commit (verification only).

---

## Task 11: Migrate PlanStan

PlanStan calls libkalburator's `SyncBackend` through the same interface. Defaulted parameters mean its existing call sites should compile unchanged.

**Working tree:** `~/dev/refactor-engine-merger/PlanStan/`

- [ ] **Step 1: Configure and build PlanStan**

```bash
cd ~/dev/refactor-engine-merger/PlanStan
cmake --preset dev -DPLANSTAN_ENABLE_CALDAV_TESTS=ON
cmake --build build-dev -j 12
cmake --build build-dev -j 12 --target tst_sync_conflicts \
    --target tst_sync_caldav_conflicts \
    --target tst_sync_error_recovery \
    --target tst_sync_dialog
```

Expected: builds. No source changes required.

If it fails to build because PlanStan has its own subclass of `SyncBackend` somewhere that overrides the affected methods, update those overrides to match the new signature (add the trailing `const TranscodingPlan&` parameter, no default). Search:

```bash
grep -rn "void storeItems\|void updateItem\|void startSync" ~/dev/refactor-engine-merger/PlanStan/src ~/dev/refactor-engine-merger/PlanStan/tests
```

- [ ] **Step 2: Run PlanStan tests**

```bash
cd ~/dev/refactor-engine-merger/PlanStan
QT_QPA_PLATFORM=offscreen ctest --test-dir build-dev --output-on-failure
```

Expected: 96/120 pass (Phase D baseline). The 24 pre-existing failures should be the same set as before; if a new failure appears, investigate.

- [ ] **Step 3: Commit (only if PlanStan source had to change)**

If no source changes were needed, skip this step. If a subclass had to be updated:

```bash
cd ~/dev/refactor-engine-merger/PlanStan
git add <changed files>
git commit -m "fix(sync): adopt SyncBackend TranscodingPlan parameter (Phase E)"
```

---

## Task 12: Migrate WildPalms

Same pattern as PlanStan.

**Working tree:** `~/dev/refactor-engine-merger/WildPalms/`

- [ ] **Step 1: Configure and build WildPalms**

```bash
cd ~/dev/refactor-engine-merger/WildPalms
cmake -S . -B build \
    -DWILDPALMS_DATEBOOK_PLUGIN_V2=ON \
    -DWILDPALMS_ADDRESS_PLUGIN_V2=ON \
    -DWILDPALMS_TODO_PLUGIN_V2=ON \
    -DWILDPALMS_MEMO_PLUGIN_V2=ON \
    -DWILDPALMS_EXPENSE_PLUGIN_V2=ON \
    -DWILDPALMS_MAIL_PLUGIN_V2=ON
cmake --build build -j 12
```

WildPalms's plugin backends inherit from `IBlobBackend` (calendar backends are not used here, per Phase D Task 10). So the new `SyncBackend` interface changes don't affect WildPalms unless WildPalms has a `SyncBackend` subclass somewhere — verify:

```bash
grep -rn "public SyncBackend" ~/dev/refactor-engine-merger/WildPalms/src
```

If empty, WildPalms doesn't subclass `SyncBackend` and just needs to compile against the updated headers, which include only public-API additions.

- [ ] **Step 2: Run WildPalms tests**

```bash
cd ~/dev/refactor-engine-merger/WildPalms
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

Expected: 73/73 pass. (Two known order-dependent destructor flakes per FINDINGS — `tst_pluckerbackendplugin` and `tst_calendar_v2`. Re-run if they bite; both pass in isolation.)

- [ ] **Step 3: Commit (only if WildPalms source had to change)**

Same conditional commit pattern as Task 11 Step 3.

---

## Task 13: `verify-all.sh` + refresh baselines if needed

- [ ] **Step 1: Run `verify-all.sh`**

```bash
bash ~/dev/refactor-engine-merger/scripts/verify-all.sh
```

Expected exit codes:

- `0` — match baseline; proceed to Task 14.
- `1` — configure or build failure; investigate.
- `2` — test regression (pass→fail somewhere); investigate.
- `3` — test improvement (fail→pass somewhere); investigate before refreshing — could be flaky test. Phase E shouldn't make consumer tests pass that were failing before.

- [ ] **Step 2: Refresh baselines if Phase E added a test**

The new `tst_transcoding_router` adds one entry to libkalburator's baseline. Refresh:

```bash
cd ~/dev/refactor-engine-merger/libkalburator
ctest --test-dir build --output-on-failure 2>&1 | \
    tee ~/dev/refactor-engine-merger/baselines/libkalburator-worktree-ctest.txt
```

Verify the new file lists 20 tests and all pass.

- [ ] **Step 3: Commit baseline refresh**

```bash
cd ~/dev/refactor-engine-merger
git -C baselines add libkalburator-worktree-ctest.txt 2>/dev/null || \
    cd baselines && git add libkalburator-worktree-ctest.txt && cd ..
```

(`baselines/` may or may not be its own git repo; check `git status` from the coordination folder. If the coordination folder is not a git repo at all per CLAUDE.md, baselines are tracked some other way — in that case skip this commit step.)

- [ ] **Step 4: Re-run `verify-all.sh` after refresh**

```bash
bash ~/dev/refactor-engine-merger/scripts/verify-all.sh
```

Expected: exit 0.

---

## Task 14: Update phase docs, FINDINGS, CURRENT-STATUS, tag

- [ ] **Step 1: Update `04n-phase-e-transcoding-design.md` Status line**

Edit the `**Status:**` line near the top:

```
**Status:** Landed YYYY-MM-DD on tag `v0.11-phase-e-transcoding-backends` (libkalburator HEAD `<short-sha>`). Approved 2026-04-29 by user via brainstorming session.
```

- [ ] **Step 2: Update `04n-phase-e-transcoding-plan.md` Status line**

Add a Status section near the top mirroring `04m-phase-d-compose-plan.md`:

```
**Status:** Landed YYYY-MM-DD on tag `v0.11-phase-e-transcoding-backends` (libkalburator HEAD `<short-sha>`). All 14 tasks complete; libkalburator 20/20, PlanStan 96/120, WildPalms 73/73 at tag.
```

- [ ] **Step 3: Update `04k-engine-merger-roadmap.md` status table**

Change Phase E row from `⬜ not started` to `✅ landed YYYY-MM-DD`.

- [ ] **Step 4: Update `~/dev/refactor-engine-merger/CURRENT-STATUS.md`**

- Bump the date.
- Move "Phase E — Transcoding into backends" from "Next" to "Where we are" with the landed indicator.
- Replace "Next" with "Phase F — Unify".
- Append to "Recently committed (libkalburator)" the Phase E commits.
- Update test posture line if libkalburator went from 19 to 20.

- [ ] **Step 5: Update `~/dev/refactor-engine-merger/FINDINGS.md`**

Append any non-obvious lessons learned during Phase E. Candidates:

- Anything surprising about how a specific backend's existing write-path code interacted with the transcoded clone (e.g., re-serializing from `icalData` vs. from the incidence).
- Any deferred work that emerged but wasn't already captured in the design's §7.
- Behavior of `QStringLiteral` vs `QString` in test fixtures, if relevant.

If nothing surprising emerged, skip — FINDINGS is for non-obvious discoveries, not a forced log.

- [ ] **Step 6: Commit doc updates**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git add docs/phase0/04n-phase-e-transcoding-design.md \
        docs/phase0/04n-phase-e-transcoding-plan.md \
        docs/phase0/04k-engine-merger-roadmap.md
git commit -m "docs(phase0): mark Phase E landed on tag v0.11-phase-e-transcoding-backends"
```

```bash
cd ~/dev/refactor-engine-merger
# CURRENT-STATUS / FINDINGS are in the coordination folder which is NOT
# a git repo per CLAUDE.md. Just save and move on.
```

- [ ] **Step 7: Tag the libkalburator HEAD**

Per CLAUDE.md, the user runs destructive operations including `git tag` unless explicitly authorized. **Do not tag autonomously.** Instead, after committing the doc updates, report the head sha and recommended tag command to the user:

```
Phase E ready to tag. Recommended:
  cd ~/dev/refactor-engine-merger/libkalburator
  git tag v0.11-phase-e-transcoding-backends <head-sha>
```

The user runs the tag command and confirms. After the tag is in place, return to Step 1 of this task and update the Status lines with the actual sha.

---

## Self-review checklist (run by plan executor before declaring done)

- [ ] `git grep -n "TranscodingRegistry" src/calendar/` returns zero hits.
- [ ] libkalburator standalone ctest: 20/20 pass.
- [ ] `tst_calendar_transcoding_warning` passes in isolation and as part of full suite.
- [ ] `tst_transcoding_router` passes (5 cases).
- [ ] PlanStan: 96/120 pass (Phase D baseline).
- [ ] WildPalms: 73/73 pass.
- [ ] `verify-all.sh` exit 0 on a stable run.
- [ ] No new TODOs / FIXMEs left in code.
- [ ] All eight concrete `SyncBackend` subclasses (Mock, Local, Remote, Org, Akonadi, DecSync, Subscription, Holiday) have updated signatures.
- [ ] `SyncBackend::transcodingWarning` connect()s exist for every backend `SyncWorker` knows about.
- [ ] `04n-phase-e-transcoding-design.md` and `04n-phase-e-transcoding-plan.md` Status lines reflect the tag.
- [ ] `04k-engine-merger-roadmap.md` status table shows Phase E ✅.
- [ ] `CURRENT-STATUS.md` updated.
- [ ] `FINDINGS.md` appended if non-obvious learnings emerged.
