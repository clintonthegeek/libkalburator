# Sync-Topology Promotion (libkalburator Phase 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Promote the topology→`SyncMapping` generator into libkalburator as a pure headless function, make `LogicalCalendar` domain-agnostic, and close the `discoveredWritable()` authority gap.

**Architecture:** Five independent changes, each TDD. A new pure `generateMappings()` (lifted from PlanStan's `CollectionController`), a `domain` field + `LogicalCollection` alias + a neutral demotion fact on `LogicalCalendar`, a ReadOnly role seed in `LogicalCalendarBuilder`, and a writability guard at the engine's three first-sync write sites. The engine's `SyncMapping` loop and the `SyncMapping` struct are untouched. Lands on `main`, tagged v0.57.

**Tech Stack:** C++17, Qt6 (Core/Test), CMake, QtTest. Namespace `Kalburator::Sync`.

**Spec:** `docs/2026-05-27-sync-topology-promotion-design.md`. **Coordination spec:** `../PlanStan/docs/superpowers/specs/2026-05-27-sync-topology-promotion-coordination-design.md`.

**Conventions used throughout:**
- Library headers are included by **bare filename** (e.g. `#include "logicalcalendar.h"`), matching existing code — all `src/*` subdirs are on the include path.
- Build dir is `build/`. Configure once: `cmake -S . -B build -DKALBURATOR_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug`. Build a target: `cmake --build build --target <tst> -j8`. Run: `ctest --test-dir build -R <tst> --output-on-failure`. (Do **not** pass `--parallel` to cmake.)
- Each test is a single-file `QTEST_MAIN` with `#include "tst_<name>.moc"` at the bottom (AUTOMOC is on).

---

## File Structure

**Create:**
- `src/sync/syncmappinggenerator.h` — declares the two `generateMappings` overloads.
- `src/sync/syncmappinggenerator.cpp` — implements them (logic lifted from PlanStan).
- `tests/sync/tst_syncmappinggenerator.cpp` — generator canaries (Task 3).
- `tests/calendar/tst_logicalcalendar_domain.cpp` — domain + alias + demotion fact (Tasks 1-2).
- `tests/calendar/tst_logicalcalendarbuilder_readonly_seed.cpp` — ReadOnly seed (Task 4).
- `tests/engine/tst_engine_write_gate.cpp` — write-gate (Task 5).

**Modify:**
- `src/types/logicalcalendar.h` — `domain` field, `collectionId()`, `hasWritableRemoteSyncTarget()`, `using LogicalCollection`, serialize/deserialize `domain` (Tasks 1-2).
- `src/calendar/logicalcalendarbuilder.cpp` — ReadOnly seed in `createBindingFromDiscovery` (Task 4).
- `src/engine/syncengine.cpp` — writability guard in `dispatchFirstSync` (Task 5).
- `CMakeLists.txt` — add the new `src/sync/syncmappinggenerator.{h,cpp}` (Task 3).
- `tests/sync/CMakeLists.txt`, `tests/calendar/CMakeLists.txt`, `tests/engine/CMakeLists.txt` — register the new tests.

---

## Task 1: `LogicalCalendar` domain field, `collectionId()`, and `LogicalCollection` alias

**Files:**
- Modify: `src/types/logicalcalendar.h`
- Test: `tests/calendar/tst_logicalcalendar_domain.cpp` (new)

- [ ] **Step 1: Write the failing test**

Create `tests/calendar/tst_logicalcalendar_domain.cpp`:

```cpp
#include <QtTest>
#include "logicalcalendar.h"

using namespace Kalburator::Sync;

class TstLogicalCalendarDomain : public QObject
{
    Q_OBJECT
private slots:
    void defaultDomainIsCalendar()
    {
        LogicalCalendar lc;
        QCOMPARE(lc.domain.toString(), QStringLiteral("calendar"));
    }

    void collectionIdAliasesId()
    {
        LogicalCalendar lc;
        lc.id = QStringLiteral("work");
        QCOMPARE(lc.collectionId(), QStringLiteral("work"));
    }

    void logicalCollectionAliasCompiles()
    {
        LogicalCollection lc;            // alias of LogicalCalendar
        lc.id = QStringLiteral("x");
        QCOMPARE(lc.collectionId(), QStringLiteral("x"));
    }

    void calendarDomainOmitsKey()
    {
        // A default (calendar) logical calendar must NOT write a "domain" key,
        // so existing .kalb files round-trip byte-for-byte.
        LogicalCalendar lc;
        lc.id = QStringLiteral("c");
        lc.displayName = QStringLiteral("C");
        const QJsonObject obj = logicalCalendarToJson(lc);
        QVERIFY(!obj.contains(QStringLiteral("domain")));
    }

    void nonCalendarDomainRoundTrips()
    {
        LogicalCalendar lc;
        lc.id = QStringLiteral("c");
        lc.displayName = QStringLiteral("C");
        lc.domain = Shape::DomainId(QStringLiteral("contacts"));
        const QJsonObject obj = logicalCalendarToJson(lc);
        QCOMPARE(obj.value(QStringLiteral("domain")).toString(), QStringLiteral("contacts"));

        const LogicalCalendar back = logicalCalendarFromJson(obj);
        QCOMPARE(back.domain.toString(), QStringLiteral("contacts"));
    }

    void absentDomainKeyDefaultsToCalendar()
    {
        QJsonObject obj;
        obj[QStringLiteral("id")] = QStringLiteral("c");
        obj[QStringLiteral("displayName")] = QStringLiteral("C");
        const LogicalCalendar back = logicalCalendarFromJson(obj);
        QCOMPARE(back.domain.toString(), QStringLiteral("calendar"));
    }
};

QTEST_MAIN(TstLogicalCalendarDomain)
#include "tst_logicalcalendar_domain.moc"
```

- [ ] **Step 2: Register the test and run it to verify it fails to compile**

Add to `tests/calendar/CMakeLists.txt` (follow the sibling `kalburator_add_*_test(...)` entries already in that file — use the same helper the other calendar tests use):

```cmake
kalburator_add_calendar_test(tst_logicalcalendar_domain)
```

Run: `cmake --build build --target tst_logicalcalendar_domain -j8`
Expected: FAIL — `LogicalCalendar` has no `domain` / `collectionId()`, `LogicalCollection` undeclared.

- [ ] **Step 3: Implement the type changes**

In `src/types/logicalcalendar.h`, add the include near the top (with the other includes):

```cpp
#include "shape.h"   // Shape::DomainId
```

Add the `domain` member to the `struct LogicalCalendar` field block (right after `CalendarType type = CalendarType::Hybrid;`):

```cpp
    Shape::DomainId domain = Shape::DomainId(QStringLiteral("calendar")); ///< Data domain (calendar/contacts/todo/...)
```

Add the accessor among the helper methods (near `isValid()`):

```cpp
    /// Domain-agnostic alias for `id` (collection identity).
    QString collectionId() const { return id; }
```

Add the alias immediately after the `struct LogicalCalendar { ... };` closing brace:

```cpp
using LogicalCollection = LogicalCalendar;
```

- [ ] **Step 4: Implement serialization**

In `logicalCalendarToJson` (writes), after the `obj["type"] = ...` line add:

```cpp
    if (cal.domain != Shape::DomainId(QStringLiteral("calendar"))) {
        obj[QStringLiteral("domain")] = cal.domain.toString();  // omit for calendar (back-compat)
    }
```

In `logicalCalendarFromJson` (reads), after the `cal.type = ...` line add:

```cpp
    cal.domain = obj.contains(QStringLiteral("domain"))
        ? Shape::DomainId(obj.value(QStringLiteral("domain")).toString())
        : Shape::DomainId(QStringLiteral("calendar"));
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build build --target tst_logicalcalendar_domain -j8 && ctest --test-dir build -R tst_logicalcalendar_domain --output-on-failure`
Expected: PASS (all 6 slots).

- [ ] **Step 6: Commit**

```bash
git add src/types/logicalcalendar.h tests/calendar/tst_logicalcalendar_domain.cpp tests/calendar/CMakeLists.txt
git commit -m "feat(types): domain-agnostic LogicalCalendar (domain field + LogicalCollection alias)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 2: Neutral demotion fact `hasWritableRemoteSyncTarget()`

**Files:**
- Modify: `src/types/logicalcalendar.h`
- Test: `tests/calendar/tst_logicalcalendar_domain.cpp` (extend)

- [ ] **Step 1: Write the failing test**

Add these slots to the `TstLogicalCalendarDomain` class from Task 1 (before `QTEST_MAIN`):

```cpp
    void noWritableRemoteWhenOnlyPrimary()
    {
        LogicalCalendar lc;
        CalendarBackendBinding p;
        p.backendId = QStringLiteral("local");
        p.calendarId = QStringLiteral("c");
        p.role = BackendRole::Primary;
        lc.bindings.append(p);
        QVERIFY(!lc.hasWritableRemoteSyncTarget());
    }

    void readOnlyRemoteIsNotWritableTarget()
    {
        LogicalCalendar lc;
        CalendarBackendBinding p; p.backendId = "local"; p.calendarId = "c"; p.role = BackendRole::Primary;
        CalendarBackendBinding r; r.backendId = "caldav"; r.calendarId = "c"; r.role = BackendRole::ReadOnly;
        lc.bindings = { p, r };
        QVERIFY(!lc.hasWritableRemoteSyncTarget());
    }

    void syncBindingIsWritableTarget()
    {
        LogicalCalendar lc;
        CalendarBackendBinding p; p.backendId = "local"; p.calendarId = "c"; p.role = BackendRole::Primary;
        CalendarBackendBinding s; s.backendId = "caldav"; s.calendarId = "c"; s.role = BackendRole::Sync1;
        lc.bindings = { p, s };
        QVERIFY(lc.hasWritableRemoteSyncTarget());
    }

    void disabledSyncBindingDoesNotCount()
    {
        LogicalCalendar lc;
        CalendarBackendBinding p; p.backendId = "local"; p.calendarId = "c"; p.role = BackendRole::Primary;
        CalendarBackendBinding s; s.backendId = "caldav"; s.calendarId = "c"; s.role = BackendRole::Sync1; s.enabled = false;
        lc.bindings = { p, s };
        QVERIFY(!lc.hasWritableRemoteSyncTarget());
    }
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build build --target tst_logicalcalendar_domain -j8`
Expected: FAIL — `hasWritableRemoteSyncTarget` not declared.

- [ ] **Step 3: Implement the method**

In `src/types/logicalcalendar.h`, add to the helper methods of `LogicalCalendar`:

```cpp
    /**
     * @brief Neutral fact: does an enabled Sync* (writable remote spoke) binding exist?
     *
     * Pure config-level query, no backend access. Consumers apply their own policy:
     * a conduit (WildPalms) demotes user editing when true; a two-way editor (PlanStan)
     * may ignore it. ReadOnly and Primary bindings do not count.
     */
    bool hasWritableRemoteSyncTarget() const {
        for (const auto &b : bindings) {
            if (b.enabled && static_cast<int>(b.role) >= static_cast<int>(BackendRole::Sync1)) {
                return true;
            }
        }
        return false;
    }
```

(Note: `Sync1..N` are positive ints ≥ 1; `Primary` is 0; `ReadOnly` is -1 — so `>= Sync1` selects exactly the writable remote spokes.)

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --target tst_logicalcalendar_domain -j8 && ctest --test-dir build -R tst_logicalcalendar_domain --output-on-failure`
Expected: PASS (now 10 slots).

- [ ] **Step 5: Commit**

```bash
git add src/types/logicalcalendar.h tests/calendar/tst_logicalcalendar_domain.cpp
git commit -m "feat(types): neutral hasWritableRemoteSyncTarget() demotion fact

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 3: The `generateMappings()` generator

**Files:**
- Create: `src/sync/syncmappinggenerator.h`, `src/sync/syncmappinggenerator.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/sync/tst_syncmappinggenerator.cpp` (new)

- [ ] **Step 1: Write the failing test**

Create `tests/sync/tst_syncmappinggenerator.cpp`:

```cpp
#include <QtTest>
#include "syncmappinggenerator.h"
#include "logicalcalendar.h"
#include "synctypes.h"

using namespace Kalburator::Sync;

static CalendarBackendBinding bind(const QString &backend, BackendRole role)
{
    CalendarBackendBinding b;
    b.backendId = backend;
    b.calendarId = QStringLiteral("cal");
    b.role = role;
    b.enabled = true;
    return b;
}

// Primary(local) + Sync1(caldav) + Sync2(akonadi), syncEnabled.
static LogicalCalendar threeNode()
{
    LogicalCalendar lc;
    lc.id = QStringLiteral("work");
    lc.displayName = QStringLiteral("Work");
    lc.syncEnabled = true;
    lc.bindings = { bind("local", BackendRole::Primary),
                    bind("caldav", BackendRole::Sync1),
                    bind("akonadi", BackendRole::Sync2) };
    return lc;
}

class TstSyncMappingGenerator : public QObject
{
    Q_OBJECT
private slots:
    void starIsHubAndSpoke()
    {
        const auto m = generateMappings(threeNode(), SyncTopology::Star);
        QCOMPARE(m.size(), 2);   // local<->caldav, local<->akonadi
        for (const auto &mapping : m) {
            QCOMPARE(mapping.sourceBackend, QStringLiteral("local"));
            QCOMPARE(mapping.mode, SyncMode::TwoWay);
        }
    }

    void mirrorIsFullMesh()
    {
        const auto m = generateMappings(threeNode(), SyncTopology::Mirror);
        QCOMPARE(m.size(), 3);   // local-caldav, local-akonadi, caldav-akonadi
    }

    void chainIsSequential()
    {
        const auto m = generateMappings(threeNode(), SyncTopology::Chain);
        QCOMPARE(m.size(), 2);   // local-caldav, caldav-akonadi
        QCOMPARE(m[0].sourceBackend, QStringLiteral("local"));
        QCOMPARE(m[0].targetBackend, QStringLiteral("caldav"));
        QCOMPARE(m[1].sourceBackend, QStringLiteral("caldav"));
        QCOMPARE(m[1].targetBackend, QStringLiteral("akonadi"));
    }

    void readOnlyBindingExcluded()
    {
        LogicalCalendar lc = threeNode();
        lc.bindings[2].role = BackendRole::ReadOnly;   // akonadi read-only
        const auto m = generateMappings(lc, SyncTopology::Star);
        QCOMPARE(m.size(), 1);   // only local<->caldav
        QCOMPARE(m[0].targetBackend, QStringLiteral("caldav"));
    }

    void syncDisabledYieldsNothing()
    {
        LogicalCalendar lc = threeNode();
        lc.syncEnabled = false;
        QVERIFY(generateMappings(lc, SyncTopology::Star).isEmpty());
    }

    void noSyncBindingsYieldsNothing()
    {
        LogicalCalendar lc;
        lc.id = "x"; lc.displayName = "X"; lc.syncEnabled = true;
        lc.bindings = { bind("local", BackendRole::Primary) };
        QVERIFY(generateMappings(lc, SyncTopology::Star).isEmpty());
    }

    void deterministicIds()
    {
        const auto a = generateMappings(threeNode(), SyncTopology::Star);
        const auto b = generateMappings(threeNode(), SyncTopology::Star);
        QCOMPARE(a.size(), b.size());
        for (int i = 0; i < a.size(); ++i) QCOMPARE(a[i].id, b[i].id);
        QVERIFY(!a[0].id.isEmpty());
    }

    void listOverloadConcatenatesEnabled()
    {
        LogicalCalendar other = threeNode();
        other.id = "personal";
        const auto m = generateMappings(QList<LogicalCalendar>{ threeNode(), other },
                                        SyncTopology::Star);
        QCOMPARE(m.size(), 4);   // 2 + 2
    }
};

QTEST_MAIN(TstSyncMappingGenerator)
#include "tst_syncmappinggenerator.moc"
```

- [ ] **Step 2: Run it to verify it fails to compile**

Add to `tests/sync/CMakeLists.txt`:

```cmake
kalburator_add_sync_test(tst_syncmappinggenerator)
```

Run: `cmake --build build --target tst_syncmappinggenerator -j8`
Expected: FAIL — `syncmappinggenerator.h` not found.

- [ ] **Step 3: Create the header**

Create `src/sync/syncmappinggenerator.h`:

```cpp
#ifndef KALBURATOR_SYNCMAPPINGGENERATOR_H
#define KALBURATOR_SYNCMAPPINGGENERATOR_H

#include <QList>
#include "synctypes.h"   // SyncMapping, SyncTopology

namespace Kalburator::Sync {

struct LogicalCalendar;

/**
 * @brief Translate a logical calendar's bindings + a topology into the flat
 *        SyncMapping list the SyncEngine consumes. Pure; no engine/store state.
 *
 * Star   = Primary <-> each enabled non-ReadOnly Sync* (hub-and-spoke).
 * Mirror = full mesh (every backend <-> every other).
 * Chain  = sequential (Primary <-> Sync1 <-> Sync2 ...).
 * Returns empty when !syncEnabled, no valid primary, or no sync bindings.
 */
QList<SyncMapping> generateMappings(const LogicalCalendar &lc, SyncTopology topology);

/// Convenience: concatenates the single-lc overload over a list.
QList<SyncMapping> generateMappings(const QList<LogicalCalendar> &lcs, SyncTopology topology);

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNCMAPPINGGENERATOR_H
```

- [ ] **Step 4: Create the implementation**

Create `src/sync/syncmappinggenerator.cpp`:

```cpp
#include "syncmappinggenerator.h"
#include "logicalcalendar.h"

namespace Kalburator::Sync {

QList<SyncMapping> generateMappings(const LogicalCalendar &lc, SyncTopology topology)
{
    QList<SyncMapping> out;
    if (!lc.syncEnabled)
        return out;

    const CalendarBackendBinding primary = lc.primaryBinding();
    if (!primary.isValid())
        return out;

    // Writable sync bindings only (exclude the primary itself and ReadOnly).
    QList<CalendarBackendBinding> syncBindings;
    for (const auto &b : lc.orderedSyncBindings()) {
        if (b.backendId == primary.backendId && b.calendarId == primary.calendarId)
            continue;
        if (b.role == BackendRole::ReadOnly)
            continue;
        syncBindings.append(b);
    }
    if (syncBindings.isEmpty())
        return out;

    auto make = [](const QString &id,
                   const CalendarBackendBinding &s,
                   const CalendarBackendBinding &t) {
        SyncMapping m;
        m.id = id;
        m.enabled = true;
        m.mode = SyncMode::TwoWay;
        m.conflictPolicy = ConflictResolution::AskUser;
        m.sourceBackend = s.backendId;
        m.sourceCalendar = s.calendarId;
        m.targetBackend = t.backendId;
        m.targetCalendar = t.calendarId;
        return m;
    };

    switch (topology) {
    case SyncTopology::Star:
        for (const auto &s : syncBindings)
            out.append(make(QStringLiteral("auto_%1_%2").arg(lc.id, backendRoleToString(s.role)),
                            primary, s));
        break;
    case SyncTopology::Mirror: {
        QList<CalendarBackendBinding> all;
        all.append(primary);
        all.append(syncBindings);
        for (int i = 0; i < all.size(); ++i)
            for (int j = i + 1; j < all.size(); ++j)
                out.append(make(QStringLiteral("auto_%1_mirror_%2_%3")
                                    .arg(lc.id, all[i].backendId, all[j].backendId),
                                all[i], all[j]));
        break;
    }
    case SyncTopology::Chain: {
        QList<CalendarBackendBinding> chain;
        chain.append(primary);
        chain.append(syncBindings);
        for (int i = 0; i + 1 < chain.size(); ++i)
            out.append(make(QStringLiteral("auto_%1_chain_%2_%3")
                                .arg(lc.id, chain[i].backendId, chain[i + 1].backendId),
                            chain[i], chain[i + 1]));
        break;
    }
    }
    return out;
}

QList<SyncMapping> generateMappings(const QList<LogicalCalendar> &lcs, SyncTopology topology)
{
    QList<SyncMapping> out;
    for (const auto &lc : lcs)
        out.append(generateMappings(lc, topology));
    return out;
}

} // namespace Kalburator::Sync
```

- [ ] **Step 5: Register the sources in CMake**

In the top-level `CMakeLists.txt`, add to the library's source lists alongside the other `src/sync/` entries (header near the other `src/sync/*.h`, source near the other `src/sync/*.cpp`):

```cmake
    src/sync/syncmappinggenerator.h
```
```cmake
    src/sync/syncmappinggenerator.cpp
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `cmake -S . -B build -DKALBURATOR_BUILD_TESTS=ON && cmake --build build --target tst_syncmappinggenerator -j8 && ctest --test-dir build -R tst_syncmappinggenerator --output-on-failure`
Expected: PASS (all 8 slots). (Re-run cmake configure because a new source was added to the library.)

- [ ] **Step 7: Commit**

```bash
git add src/sync/syncmappinggenerator.h src/sync/syncmappinggenerator.cpp CMakeLists.txt tests/sync/tst_syncmappinggenerator.cpp tests/sync/CMakeLists.txt
git commit -m "feat(sync): pure generateMappings(lc, topology) promoted from PlanStan

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 4: ReadOnly role seed in `LogicalCalendarBuilder`

**Files:**
- Modify: `src/calendar/logicalcalendarbuilder.cpp`
- Test: `tests/calendar/tst_logicalcalendarbuilder_readonly_seed.cpp` (new)

- [ ] **Step 1: Write the failing test**

Create `tests/calendar/tst_logicalcalendarbuilder_readonly_seed.cpp`:

```cpp
#include <QtTest>
#include "logicalcalendarbuilder.h"
#include "discoveredcalendar.h"
#include "logicalcalendar.h"

using namespace Kalburator::Sync;

static DiscoveredCalendar disc(const QString &backend, const QString &calId,
                               const QString &name, bool writable)
{
    DiscoveredCalendar d;
    d.backendId = backend;
    d.calendarId = calId;
    d.name = name;
    d.writable = writable;
    d.supportsVEvent = true;
    d.supportsVTodo = false;
    return d;
}

class TstReadOnlySeed : public QObject
{
    Q_OBJECT
private slots:
    void nonWritableSyncBindingSeededReadOnly()
    {
        LogicalCalendarBuilder builder;
        builder.setPrimaryBackendId(QStringLiteral("local"));
        builder.setSyncBackendOrder({ QStringLiteral("caldav") });
        builder.addDiscoveredCalendars("local",  { disc("local",  "work", "Work", /*writable*/true) });
        builder.addDiscoveredCalendars("caldav", { disc("caldav", "work", "Work", /*writable*/false) });

        const QList<LogicalCalendar> matched = builder.autoMatch();
        QCOMPARE(matched.size(), 1);

        // Find the caldav binding; it must be ReadOnly (not Sync1) because it's not writable.
        bool found = false;
        for (const auto &b : matched[0].bindings) {
            if (b.backendId == QStringLiteral("caldav")) {
                QCOMPARE(b.role, BackendRole::ReadOnly);
                found = true;
            }
        }
        QVERIFY(found);
    }

    void writableSyncBindingStaysSync1()
    {
        LogicalCalendarBuilder builder;
        builder.setPrimaryBackendId(QStringLiteral("local"));
        builder.setSyncBackendOrder({ QStringLiteral("caldav") });
        builder.addDiscoveredCalendars("local",  { disc("local",  "work", "Work", true) });
        builder.addDiscoveredCalendars("caldav", { disc("caldav", "work", "Work", true) });

        const QList<LogicalCalendar> matched = builder.autoMatch();
        QCOMPARE(matched.size(), 1);
        for (const auto &b : matched[0].bindings)
            if (b.backendId == QStringLiteral("caldav"))
                QCOMPARE(b.role, BackendRole::Sync1);
    }
};

QTEST_MAIN(TstReadOnlySeed)
#include "tst_logicalcalendarbuilder_readonly_seed.moc"
```

- [ ] **Step 2: Register and run to verify it fails**

Add to `tests/calendar/CMakeLists.txt`:

```cmake
kalburator_add_calendar_test(tst_logicalcalendarbuilder_readonly_seed)
```

Run: `cmake --build build --target tst_logicalcalendarbuilder_readonly_seed -j8 && ctest --test-dir build -R tst_logicalcalendarbuilder_readonly_seed --output-on-failure`
Expected: `nonWritableSyncBindingSeededReadOnly` FAILS (caldav binding role is `Sync1`, not `ReadOnly`). `writableSyncBindingStaysSync1` passes.

- [ ] **Step 3: Implement the seed**

In `src/calendar/logicalcalendarbuilder.cpp`, in `createBindingFromDiscovery(...)`, immediately after the line `binding.role = role;`, insert:

```cpp
    // Authority: a discovered non-writable calendar must not become a writable
    // sync target. Seed ReadOnly for sync bindings; a non-writable PRIMARY is a
    // warning (silently demoting it would break exactly-one-Primary).
    if (!discovered.writable) {
        if (role == BackendRole::Primary) {
            m_warnings.append(
                QStringLiteral("Primary backend '%1' calendar '%2' reports read-only")
                    .arg(discovered.backendId, discovered.calendarId));
        } else {
            binding.role = BackendRole::ReadOnly;
        }
    }
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --target tst_logicalcalendarbuilder_readonly_seed -j8 && ctest --test-dir build -R tst_logicalcalendarbuilder_readonly_seed --output-on-failure`
Expected: PASS (both slots).

- [ ] **Step 5: Commit**

```bash
git add src/calendar/logicalcalendarbuilder.cpp tests/calendar/tst_logicalcalendarbuilder_readonly_seed.cpp tests/calendar/CMakeLists.txt
git commit -m "feat(calendar): seed ReadOnly role from discoveredWritable in builder

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 5: Writability guard at the engine's first-sync write sites

**Files:**
- Modify: `src/engine/syncengine.cpp` (`SyncEngineWorker::dispatchFirstSync`)
- Test: `tests/engine/tst_engine_write_gate.cpp` (new)

Context: `tgtBackend` is a `SyncBackend *` (syncengine.cpp ~1630) with `discoveredWritable(const QString&) const` (syncbackend.h:191). The three write calls are at lines 1709 (`createRecord`), 1714 (`updateRecord`), 1723 (`deleteRecord`), inside the `QMetaObject::invokeMethod` mirror lambda; `colId` is in scope.

- [ ] **Step 1: Write the failing test**

Model this on the existing first-sync integration test `tests/engine/tst_calendar_first_sync_via_blob_engine.cpp` (open it for the harness pattern — how it constructs source/target backends, registers them with the controller, builds a `SyncMapping`, and runs a first sync). Create `tests/engine/tst_engine_write_gate.cpp` that:

1. builds a **source** backend holding one record, and a **target** stub backend whose `discoveredWritable(...)` returns `false` and that counts `createRecord`/`updateRecord`/`deleteRecord` calls;
2. runs a first sync over a `local→target` mapping;
3. asserts the target's write-call count is **0** and the sync completes without error.

Concrete assertion shape (adapt construction to the harness in the sibling test):

```cpp
// after running first sync over the mapping:
QCOMPARE(target->createCalls, 0);
QCOMPARE(target->updateCalls, 0);
QCOMPARE(target->deleteCalls, 0);
QVERIFY(syncSucceededWithoutError);   // skip is not an error
```

The read-only stub overrides:

```cpp
bool discoveredWritable(const QString &) const override { return false; }
QString createRecord(const QString &c, const BackendRecord &r) override { ++createCalls; return SomeBaseOrStub::createRecord(c, r); }
bool    updateRecord(const BackendRecord &r) override { ++updateCalls; return SomeBaseOrStub::updateRecord(r); }
bool    deleteRecord(const QString &id) override { ++deleteCalls; return SomeBaseOrStub::deleteRecord(id); }
```

- [ ] **Step 2: Register and run to verify it fails**

Add to `tests/engine/CMakeLists.txt` (follow the sibling engine-test registration helper in that file):

```cmake
kalburator_add_engine_test(tst_engine_write_gate)
```

Run: `cmake --build build --target tst_engine_write_gate -j8 && ctest --test-dir build -R tst_engine_write_gate --output-on-failure`
Expected: FAIL — write counts are non-zero (the engine writes to the read-only target today).

- [ ] **Step 3: Implement the guard**

In `src/engine/syncengine.cpp`, in `dispatchFirstSync`, just before the `QMetaObject::invokeMethod(srcBackend, [src, tgt, colId, ...]` block (currently ~line 1693), compute the writability once:

```cpp
    const bool tgtWritable = tgtBackend->discoveredWritable(colId);
    if (!tgtWritable) {
        qWarning() << "SyncEngine: target backend" << request.mapping.targetBackend
                   << "reports read-only for collection" << colId
                   << "- skipping first-sync writes";
    }
```

Add `tgtWritable` to the lambda capture list, e.g. `[src, tgt, colId, tgtWritable, &mirrorErrors, &mirrorReadErr]()`.

Then guard each write inside the lambda:

```cpp
            // Copy source → target (create or update).
            for (const auto &sr : srcRecords) {
                const auto it = tgtById.constFind(sr.id);
                if (it == tgtById.constEnd()) {
                    if (tgtWritable && tgt->createRecord(colId, sr).isEmpty())
                        ++mirrorErrors;
                } else if (it.value().contentHash != sr.contentHash) {
                    BackendRecord out = sr;
                    out.id = it.value().id;
                    if (tgtWritable && !tgt->updateRecord(out))
                        ++mirrorErrors;
                }
            }

            // Delete target records not in source.
            const auto srcById = indexBlobById(srcRecords);
            for (const auto &tr : tgtRecords) {
                if (!srcById.contains(tr.id)) {
                    if (tgtWritable && !tgt->deleteRecord(tr.id))
                        ++mirrorErrors;
                }
            }
```

(Short-circuit `&&` means the write call is never invoked when `!tgtWritable`; `mirrorErrors` stays 0, so the existing success-completion path runs unchanged.)

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --target tst_engine_write_gate -j8 && ctest --test-dir build -R tst_engine_write_gate --output-on-failure`
Expected: PASS (0 writes, sync succeeds).

- [ ] **Step 5: Run the full engine suite (no regressions)**

Run: `cmake --build build -j8 && ctest --test-dir build -R "tst_.*engine|tst_calendar_first_sync" --output-on-failure`
Expected: PASS (the guard is a no-op when targets are writable).

- [ ] **Step 6: Commit**

```bash
git add src/engine/syncengine.cpp tests/engine/tst_engine_write_gate.cpp tests/engine/CMakeLists.txt
git commit -m "fix(engine): gate first-sync writes on discoveredWritable (read-only targets)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Final verification

- [ ] Run the whole suite: `cmake --build build -j8 && ctest --test-dir build --output-on-failure`. Expected: all pass.
- [ ] Confirm an existing calendar `.kalb` (single-backend, all `role: "primary"`) still loads and re-saves with no new `"domain"` key — round-trip unchanged.

## Self-review (completed against the spec)

- **§3.1 type generalization** → Task 1 (domain field, collectionId, alias, serialization). ✔
- **§3.2 serialization round-trip** → Task 1 (calendarDomainOmitsKey, absentDomainKeyDefaultsToCalendar). ✔
- **§3.3 generator (single + list, Star/Mirror/Chain, ReadOnly exclude, syncEnabled skip, deterministic ids)** → Task 3. ✔
- **§3.4(a) ReadOnly role seed + non-writable-primary warning** → Task 4. ✔
- **§3.4(b) write-gate at the 3 confirmed sites via tgtBackend->discoveredWritable** → Task 5. ✔
- **§3.4(c) neutral demotion fact (hasWritableRemoteSyncTarget, not isPrimaryUserWritable)** → Task 2. ✔
- **§4 guardrails** — no SyncMapping/loop change (Task 5 only short-circuits writes), no struct rename (Task 1 uses alias), no persist helper (dropped). ✔

Two tasks contain a bounded in-execution discovery (acknowledged in the spec): Task 4/5 reference sibling tests/headers for harness shape (test plumbing only — the production change is fully specified). No placeholders in production code steps.
