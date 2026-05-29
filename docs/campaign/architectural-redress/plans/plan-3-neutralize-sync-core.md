# Neutralize the Calendar-Typed Sync Core — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `sync/` (the orchestration layer) traffic in domain-neutral types instead of the
calendar-typed `SyncBackend`, so KCalendarCore no longer leaks through `BackendRegistry`,
`ProviderManager`, `SyncEngine`, or the non-calendar backends. Resolves AUDIT CRITICALs #1–#3 +
the `engine/→syncoperation`, `contacts/→calendar`, and `universal/→calendar` include MAJORs.

**Architecture (Approach A, approved):** Extract the already-neutral `SyncOperation` base into
`sync/`; neutralize `SyncBackendBase`'s operation-return types and add `discoveredWritable()` to
it; store `SyncBackendBase*` (not calendar `SyncBackend*`) in `BackendRegistry`; reparent the
four non-calendar backends onto `SyncBackendBase`. The calendar-typed `SyncBackend` +
`FetchOperation`/`PushOperation`/`DeleteOperation` remain as `calendar/` subclasses for calendar
backends, `CalendarManager`, and UI. The engine already operates on the neutral record/blob API
(verified) — its only calendar-typed call, `discoveredWritable()`, moves to the base.

**Tech Stack:** C++/Qt6, KCalendarCore (being pushed OUT of `sync/`), CMake (single `kalburator`
static lib; legacy `build/` workflow).

**Scope (locked):** Core neutralization ONLY. The `sync/` *provider* files
(`akonadiprovider.cpp`, `caldavprovider.cpp`, `carddavprovider.cpp`,
`multiprotocoldavprovider.cpp`) construct concrete domain backends and so still `#include
"../calendar/…"`/`"../contacts/…"`. **That is a separate plan** — this plan does NOT touch the
providers. The acceptance greps below are scoped accordingly.

**Branch:** `feature/redress-3-neutralize-sync-core` (off `main`). Worktree per
`superpowers:using-git-worktrees`.

**Nature of change:** This is a **behavior-preserving structural refactor**. There is no
red-green-refactor; the discipline is: **the full existing suite (132 tests incl. Plan 2's
CalendarManager tests) stays green after every task**, the tree compiles after every task, plus
one new proof-of-neutrality test. If any existing test fails, STOP — a behavior-preserving
refactor that changes behavior has a bug; do not adapt the test.

---

## Reference facts (verified against source at HEAD)

- `src/calendar/syncoperation.h` defines `SyncOperation` (base, lines 40–193, **zero
  KCalendarCore** in its members/methods — only the file's `#include <KCalendarCore/Incidence>`
  at line 7 serves the *subclasses*) + `FetchOperation`/`PushOperation`/`DeleteOperation`
  (carry `KCalendarCore::Incidence::Ptr`). Base members: `m_operationId`, `m_calendarId`,
  `std::atomic<State> m_state`, `std::atomic<bool> m_cancelRequested`, `int m_progress`,
  `QString m_errorString`, `static int s_nextOperationId`. Base public API:
  `operationId/calendarId/state/progress/errorString/isFinished/cancel(virtual)/fail/setState/setProgress/complete`;
  protected `setErrorString/setError/cancelRequested/start`; signals
  `started/stateChanged/progressChanged/finished`.
- `src/sync/syncbackendbase.h` (`SyncBackendBase : public QObject, public IBlobBackend`,
  KCalendarCore-free today via forward-decls): currently forward-declares `SyncOperation`,
  `FetchOperation`, `PushOperation`, `DeleteOperation`; declares
  `virtual FetchOperation* fetchItems(const QString &calendarId);` and
  `virtual DeleteOperation* deleteItems(const QString &calendarId, const QStringList &uids);`
  and `QList<SyncOperation*> pendingOperations*()`. Includes `"iblobbackend.h"`, `"shape.h"`.
- `src/calendar/syncbackend.h` (`class SyncBackend : public SyncBackendBase`, line 120; includes
  `<KCalendarCore/...>` at lines 40–41 and `"syncoperation.h"` (calendar)): declares
  `virtual bool discoveredWritable(const QString &calendarId) const { Q_UNUSED(calendarId); return true; }`
  (calendar/syncbackend.h:191–194), overrides `fetchItems`/`deleteItems` returning
  `FetchOperation*`/`DeleteOperation*`, and adds `pushItems(QList<Incidence::Ptr>)→PushOperation*`
  plus the calendar surface/signals.
- `discoveredWritable` overrides exist in: `LocalBackend`, `OrgBackend`, `SubscriptionBackend`,
  `AkonadiBackend`, `RemoteCalendarBackend`, `FilteredCollectionBackend`. Default is `return true`.
- **Engine** (`src/engine/syncengine.cpp`): obtains backends via
  `SyncBackend *base = m_registry->backendInstance(id)` (5 sites: ~738/751/776/1080/1187) and the
  worker dispatch path (~1080, 1365, 1703, 1753, 1774–1802, 1857, 2055, 2087, 2129, 2157, 2593).
  All calls are NEUTRAL (`backendType`, `loadRecordsOrError`, `createRecord`, `updateRecord`,
  `deleteRecord`, `fetchItems`, `ChangeDetection` mixin via `dynamic_cast`) **except
  `discoveredWritable()`** (~1753, 2593). At the `fetchItems` sites (~2055, 2129) the engine
  stores the result in a `FetchOperation*` / `QPointer<FetchOperation>` local but uses ONLY
  `state()` and the `finished` signal — never `fetchedItems()`. The engine never calls
  `pushItems()`.
- `src/sync/backendregistry.h`: `void registerBackendInstance(const QString&, SyncBackend*)`
  (line 34), `SyncBackend* backendInstance(const QString&) const` (line 44),
  `QMap<QString, SyncBackend*> m_instances` (line 105); `backendregistry.cpp:3` `#include
  "syncbackend.h"`.
- `src/sync/providermanager.cpp:242`: `auto *asSync = dynamic_cast<SyncBackend*>(backend.get());`
  then `m_registry->registerBackendInstance(backendId, asSync)` (line 249);
  `providermanager.cpp:7` `#include "syncbackend.h"`. `IProvider::createBackend` returns
  `std::unique_ptr<IBlobBackend>`.
- The four reparent targets (all verified to override ONLY neutral methods, no KCalendarCore in
  their `.cpp`): `src/universal/rawfilesbackend.h:27`, `src/universal/genericsqlitebackend.h:33`,
  `src/contacts/remotecontactsbackend.h:37` (`: public SyncBackend, public ...ChangeDetection`),
  `src/universal/filteredcollectionbackend.h:32` (holds `SyncBackend* m_parent` at :95, calls
  only `shapeFor/resourceId/collectionInfo/discoveredWritable/loadRecords/loadRecord/createRecord/updateRecord/deleteRecord` on it).
- **CMake:** single `kalburator` STATIC lib. `src/calendar/syncoperation.cpp` is in
  `KALBURATOR_CALENDAR_SOURCES`; `src/sync/*.cpp` (e.g. `backendregistry.cpp`) likewise. Adding
  `src/sync/syncoperation.cpp` just means adding it to the sources list — no layering concern.

---

## File Structure

- **Create:** `src/sync/syncoperation.h`, `src/sync/syncoperation.cpp` — the neutral
  `SyncOperation` base (moved out of `calendar/`).
- **Modify:** `src/calendar/syncoperation.{h,cpp}` (keep subclasses, include the neutral base),
  `src/sync/syncbackendbase.h` (neutral return types + `discoveredWritable`),
  `src/calendar/syncbackend.h` (drop the now-inherited `discoveredWritable` decl),
  `src/engine/syncengine.{h,cpp}` (neutral operation locals + includes + `SyncBackendBase*`),
  `src/sync/backendregistry.{h,cpp}`, `src/sync/providermanager.cpp`,
  `src/universal/{rawfilesbackend,genericsqlitebackend,filteredcollectionbackend}.h`,
  `src/contacts/remotecontactsbackend.h`, `CMakeLists.txt`.
- **Create (test):** `tests/sinks/tst_neutral_sync_core.cpp` (proof of neutrality) + its CMake
  registration.

---

### Task 1: Extract the neutral `SyncOperation` base into `sync/`

Pure relocation — behavior identical. After this, `calendar/syncoperation.h` re-exposes the same
symbols (it includes the new base), so all existing callers are unaffected.

**Files:** Create `src/sync/syncoperation.h`, `src/sync/syncoperation.cpp`; modify
`src/calendar/syncoperation.{h,cpp}`, `CMakeLists.txt`.

- [ ] **Step 1: Create `src/sync/syncoperation.h`** with the neutral base (copy the
  `SyncOperation` class body verbatim from `calendar/syncoperation.h` lines 40–193, drop the
  `#include <KCalendarCore/Incidence>`):

```cpp
#ifndef KALBURATOR_SYNC_SYNCOPERATION_H
#define KALBURATOR_SYNC_SYNCOPERATION_H

#include <QObject>
#include <QString>
#include <atomic>

namespace Kalburator::Sync {

/**
 * @brief Domain-neutral base for trackable async sync operations.
 *
 * Lifted out of calendar/syncoperation.h (architectural-redress Plan 3) so the
 * sync/ orchestration layer and engine depend on a base with ZERO KCalendarCore.
 * Calendar-typed subclasses (FetchOperation/PushOperation/DeleteOperation) live
 * in calendar/syncoperation.h and inherit this base.
 */
class SyncOperation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)

public:
    enum State { Pending, Running, Succeeded, Failed, Cancelled };
    Q_ENUM(State)

    explicit SyncOperation(const QString &calendarId, QObject *parent = nullptr);
    ~SyncOperation() override;

    QString operationId() const { return m_operationId; }
    QString calendarId() const { return m_calendarId; }
    State state() const noexcept { return m_state.load(std::memory_order_acquire); }
    int progress() const { return m_progress; }
    QString errorString() const { return m_errorString; }
    bool isFinished() const noexcept;

    virtual void cancel();
    void fail(const QString &errorString);
    void setState(State newState);
    void setProgress(int percent);
    void complete();

signals:
    void started();
    void stateChanged(SyncOperation::State newState);
    void progressChanged(int percent);
    void finished();

protected:
    void setErrorString(const QString &error);
    void setError(const QString &message);
    bool cancelRequested() const noexcept;
    void start();

private:
    QString m_operationId;
    QString m_calendarId;
    std::atomic<State> m_state{Pending};
    std::atomic<bool> m_cancelRequested{false};
    int m_progress = -1;
    QString m_errorString;

    static int s_nextOperationId;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNC_SYNCOPERATION_H
```

- [ ] **Step 2: Create `src/sync/syncoperation.cpp`** by moving the `SyncOperation` base method
  implementations out of `src/calendar/syncoperation.cpp`. Open `calendar/syncoperation.cpp`,
  cut every `SyncOperation::` member definition (ctor, dtor, `isFinished`, `cancel`, `fail`,
  `setState`, `setProgress`, `complete`, `setErrorString`, `setError`, `cancelRequested`,
  `start`, and the `int SyncOperation::s_nextOperationId = …` definition) into the new file:

```cpp
#include "syncoperation.h"
// (paste the cut SyncOperation:: method bodies here verbatim, unchanged)
```

  Add any `#include`s those bodies need (e.g. `<QUuid>` if used for `m_operationId`) — copy
  whatever the original `calendar/syncoperation.cpp` had for the base methods.

- [ ] **Step 3: Edit `src/calendar/syncoperation.h`** — remove the `SyncOperation` base class
  body (lines ~40–193) and the `#include <KCalendarCore/Incidence>` stays (subclasses need it);
  add `#include "../sync/syncoperation.h"` near the top. The file now contains only
  `FetchOperation`, `PushOperation`, `DeleteOperation` (unchanged), which already inherit
  `SyncOperation` (now from the included base). Keep the include guard.

- [ ] **Step 4: Edit `src/calendar/syncoperation.cpp`** — it now contains only the subclass
  method bodies (`FetchOperation::setFetchedItems`, `PushOperation` ctor + uid mutators,
  `DeleteOperation` ctor + uid mutators). Ensure it still `#include "syncoperation.h"` (calendar)
  which transitively pulls the neutral base.

- [ ] **Step 5: Add the new source to CMake.** In `CMakeLists.txt`, find the line listing
  `src/calendar/syncoperation.cpp` (in `KALBURATOR_CALENDAR_SOURCES`) and add alongside the sync
  sources a new entry `src/sync/syncoperation.cpp`. Concretely, locate the `set(KALBURATOR_CALENDAR_SOURCES …)`
  block and add `src/sync/syncoperation.cpp` to it (simplest — same library target). Leave
  `src/calendar/syncoperation.cpp` in the list.

- [ ] **Step 6: Build + full test (regression gate).**

Run: `cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build -j$(($(nproc)-1))`
Expected: clean build (pure relocation).
Run: `ctest --test-dir build -j$(($(nproc)-1))`
Expected: 132/132 pass (the `tst_engine_cancellation` flake may segfault under `-jN` — rerun it
isolated to confirm; it is pre-existing per FINDINGS, not caused here).

- [ ] **Step 7: Commit**

```bash
git add src/sync/syncoperation.h src/sync/syncoperation.cpp src/calendar/syncoperation.h src/calendar/syncoperation.cpp CMakeLists.txt
git commit -m "refactor(sync): extract neutral SyncOperation base into sync/ (P3.T1)"
```

---

### Task 2: Move `discoveredWritable()` down to `SyncBackendBase`

**Files:** Modify `src/sync/syncbackendbase.h`, `src/calendar/syncbackend.h`.

- [ ] **Step 1: Add to `src/sync/syncbackendbase.h`** — in the public section (near the identity
  methods like `shapeFor`), add:

```cpp
    /// Whether the given collection is writable. Default: true (most backends are
    /// writable). Calendar/remote backends override with discovered writability.
    virtual bool discoveredWritable(const QString &collectionId) const {
        Q_UNUSED(collectionId);
        return true;
    }
```

- [ ] **Step 2: Remove the duplicate from `src/calendar/syncbackend.h`** — delete the
  `discoveredWritable` declaration at lines ~191–194 (the calendar backends'
  `discoveredWritable` overrides now override the inherited base method; their signatures are
  identical, so no other change is needed).

- [ ] **Step 3: Build + full test.**

Run: `cmake --build build -j$(($(nproc)-1)) && ctest --test-dir build -j$(($(nproc)-1))`
Expected: clean build; 132/132 (behavior identical — same default, same overrides). If a
calendar backend's `discoveredWritable` override now fails to compile (e.g. it used `override`
and the base lacked the method — now it has it, so `override` is satisfied), that's expected to
pass; if it does NOT compile, STOP and report.

- [ ] **Step 4: Commit**

```bash
git add src/sync/syncbackendbase.h src/calendar/syncbackend.h
git commit -m "refactor(sync): move discoveredWritable() down to SyncBackendBase (P3.T2)"
```

---

### Task 3: Neutralize `SyncBackendBase`'s operation-return types

Make `SyncBackendBase` reference only the neutral `SyncOperation` (no calendar operation types).
Calendar backends keep returning the subclasses via covariant overrides.

**Files:** Modify `src/sync/syncbackendbase.h`, `src/sync/syncbackendbase.cpp`.

- [ ] **Step 1: Edit `src/sync/syncbackendbase.h`** —
  - Replace the operation forward-declarations. Remove `class FetchOperation; class
    PushOperation; class DeleteOperation;` (and the bare `class SyncOperation;` forward-decl) and
    instead `#include "syncoperation.h"` (the new neutral base in the same `sync/` dir).
  - Change the two declarations:
    - `virtual FetchOperation* fetchItems(const QString &calendarId);`
      → `virtual SyncOperation* fetchItems(const QString &calendarId);`
    - `virtual DeleteOperation* deleteItems(const QString &calendarId, const QStringList &uids);`
      → `virtual SyncOperation* deleteItems(const QString &calendarId, const QStringList &uids);`
  - `pendingOperations()` etc. already use `SyncOperation*` — unchanged.

- [ ] **Step 2: Edit `src/sync/syncbackendbase.cpp`** — update the default implementations of
  `fetchItems`/`deleteItems` to return `SyncOperation*` (they currently return
  `FetchOperation*`/`DeleteOperation*`; the default impls log-and-return-nullptr or similar).
  Change the return type in the definition to `SyncOperation*`. If a default impl constructs a
  `FetchOperation`, change it to return `nullptr` (the neutral base can't construct calendar
  subclasses) — verify the default was a stub; if it did real work, STOP and report.

- [ ] **Step 3: Confirm `src/calendar/syncbackend.h` overrides are covariant.** The calendar
  `SyncBackend` overrides `fetchItems`/`deleteItems` returning `FetchOperation*`/`DeleteOperation*`.
  C++ covariant returns require the override's return type to derive from the base's
  (`FetchOperation` IS-A `SyncOperation` — yes). No change needed; just verify it compiles.

- [ ] **Step 4: Build + full test.**

Run: `cmake --build build -j$(($(nproc)-1)) && ctest --test-dir build -j$(($(nproc)-1))`
Expected: clean build (the engine still obtains `SyncBackend*` here, so it still sees
`FetchOperation*` covariantly — unaffected); 132/132. **`syncbackendbase.h` now has no reference
to any calendar type.** If covariant return fails to compile, STOP and report (it shouldn't).

- [ ] **Step 5: Commit**

```bash
git add src/sync/syncbackendbase.h src/sync/syncbackendbase.cpp
git commit -m "refactor(sync): SyncBackendBase fetch/delete return neutral SyncOperation* (P3.T3)"
```

---

### Task 4: Make `SyncEngine` depend only on the neutral operation type

**Files:** Modify `src/engine/syncengine.h`, `src/engine/syncengine.cpp`.

- [ ] **Step 1: Swap the engine's operation includes.** In `syncengine.h` (line ~13) and
  `syncengine.cpp`, replace `#include "../calendar/syncoperation.h"` (or `"syncoperation.h"`
  resolving to calendar) with `#include "../sync/syncoperation.h"`. (If the engine genuinely
  needs `FetchOperation`/`PushOperation`/`DeleteOperation` somewhere, see Step 2 first.)

- [ ] **Step 2: Neutralize the `fetchItems` result locals.** At the two fetch sites
  (`syncengine.cpp` ~2053–2072 and ~2127–2142), change the local types:
  - `FetchOperation *fetchOpRaw = nullptr;` → `SyncOperation *fetchOpRaw = nullptr;`
  - `QPointer<FetchOperation> fetchOp = fetchOpRaw;` → `QPointer<SyncOperation> fetchOp = fetchOpRaw;`
  The body only uses `state()` and the `finished` signal — both on the base — so no further
  change. (Assigning the covariant `FetchOperation*` returned by a `SyncBackend` override into a
  `SyncOperation*` is an upcast — valid.)

- [ ] **Step 3: Replace any `await<…Operation>` instantiations.** Grep the engine for
  `await<FetchOperation>`, `await<DeleteOperation>`, `await<PushOperation>`, and any other use of
  the calendar operation types:

  Run: `grep -nE 'FetchOperation|PushOperation|DeleteOperation|await<' src/engine/syncengine.cpp src/engine/syncengine.h src/engine/syncengine_p.h`

  For each hit that is just an operation *handle* (await/state/finished), retype it to
  `SyncOperation`. If any hit genuinely reads `fetchedItems()`/`requestedItems()` (calendar
  payload), STOP and report — that would contradict the investigation and require a different
  approach.

- [ ] **Step 4: Build + full test.**

Run: `cmake --build build -j$(($(nproc)-1)) && ctest --test-dir build -j$(($(nproc)-1))`
Expected: clean build; 132/132. The engine still obtains `SyncBackend*` from the registry (next
task) — fine, it upcasts fetch results to `SyncOperation*`. **`engine/` no longer includes
`calendar/syncoperation.h`.**

- [ ] **Step 5: Commit**

```bash
git add src/engine/syncengine.h src/engine/syncengine.cpp src/engine/syncengine_p.h
git commit -m "refactor(engine): depend on neutral SyncOperation, drop calendar/syncoperation include (P3.T4)"
```

---

### Task 5: Reparent the four non-calendar backends onto `SyncBackendBase`

All four were verified to use only the neutral surface. Each is a one-line base-class change +
an include swap (and one pointer-type change for the filtered backend).

**Files:** Modify `src/universal/rawfilesbackend.h`, `src/universal/genericsqlitebackend.h`,
`src/universal/filteredcollectionbackend.{h,cpp}`, `src/contacts/remotecontactsbackend.h`.

- [ ] **Step 1: `RawFilesBackend`** — `src/universal/rawfilesbackend.h:27`:
  `class RawFilesBackend : public Kalburator::Sync::SyncBackend {` →
  `class RawFilesBackend : public Kalburator::Sync::SyncBackendBase {`. Swap the include:
  `#include "syncbackend.h"` → `#include "../sync/syncbackendbase.h"`.

- [ ] **Step 2: `GenericSqliteBackend`** — `src/universal/genericsqlitebackend.h:33`:
  `: public Kalburator::Sync::SyncBackend` → `: public Kalburator::Sync::SyncBackendBase`. Swap
  the include `#include "syncbackend.h"` → `#include "../sync/syncbackendbase.h"`.

- [ ] **Step 3: `RemoteContactsBackend`** — `src/contacts/remotecontactsbackend.h:37`:
  `class RemoteContactsBackend : public SyncBackend, public Kalburator::Backend::ChangeDetection`
  → `class RemoteContactsBackend : public Kalburator::Sync::SyncBackendBase, public Kalburator::Backend::ChangeDetection`.
  Swap the include `#include "syncbackend.h"` → `#include "../sync/syncbackendbase.h"`. (Its `.cpp`
  has no calendar includes — verified.)

- [ ] **Step 4: `FilteredCollectionBackend`** —
  `src/universal/filteredcollectionbackend.h:32`: base `SyncBackend` → `SyncBackendBase`; swap
  include to `../sync/syncbackendbase.h`. Change the parent pointer type at `:95`:
  `Kalburator::Sync::SyncBackend* m_parent` → `Kalburator::Sync::SyncBackendBase* m_parent`, and
  update the ctor parameter / any setter accordingly (`.cpp` constructs it from a parent — change
  that parameter type too). All methods it calls on `m_parent`
  (`shapeFor/resourceId/collectionInfo/discoveredWritable/loadRecords/loadRecord/createRecord/updateRecord/deleteRecord`)
  now exist on `SyncBackendBase` (after T2). If the ctor is called anywhere with a `SyncBackend*`
  argument, that still binds to `SyncBackendBase*` (upcast) — fine.

- [ ] **Step 5: Build + full test.**

Run: `cmake --build build -j$(($(nproc)-1)) && ctest --test-dir build -j$(($(nproc)-1))`
Expected: clean build; 132/132. **`contacts/` and `universal/` no longer include
`calendar/syncbackend.h`.** If any backend fails to compile because it used a `SyncBackend`-only
method, STOP and report which (the investigation found none).

- [ ] **Step 6: Commit**

```bash
git add src/universal/rawfilesbackend.h src/universal/genericsqlitebackend.h src/universal/filteredcollectionbackend.h src/universal/filteredcollectionbackend.cpp src/contacts/remotecontactsbackend.h
git commit -m "refactor(backends): reparent non-calendar backends onto SyncBackendBase (P3.T5)"
```

---

### Task 6: Neutralize `BackendRegistry`, `ProviderManager`, and the engine's registry calls

The CRITICAL #1/#2 fix. After this, `sync/` core + `engine/` hold no calendar type.

**Files:** Modify `src/sync/backendregistry.h`, `src/sync/backendregistry.cpp`,
`src/sync/providermanager.cpp`, `src/engine/syncengine.cpp`.

- [ ] **Step 1: `src/sync/backendregistry.h`** — replace `SyncBackend` with `SyncBackendBase`
  throughout the instance API:
  - forward-decl: `class SyncBackend;` → `class SyncBackendBase;`
  - `void registerBackendInstance(const QString &backendId, SyncBackend *backend);` →
    `… SyncBackendBase *backend);`
  - `SyncBackend* backendInstance(const QString &backendId) const;` → `SyncBackendBase* …`
  - `QMap<QString, SyncBackend*> m_instances;` → `QMap<QString, SyncBackendBase*> m_instances;`

- [ ] **Step 2: `src/sync/backendregistry.cpp`** — change `#include "syncbackend.h"` →
  `#include "syncbackendbase.h"`; update the method signatures to match the header
  (`SyncBackendBase*`).

- [ ] **Step 3: `src/sync/providermanager.cpp`** — change `#include "syncbackend.h"` →
  `#include "syncbackendbase.h"`; at line ~242 change
  `auto *asSync = dynamic_cast<SyncBackend*>(backend.get());` →
  `auto *asSync = dynamic_cast<SyncBackendBase*>(backend.get());`. The `qWarning` text and the
  `registerBackendInstance(backendId, asSync)` call are unchanged (now passes `SyncBackendBase*`).
  Update the comment that referenced the "Phase H interim SyncBackend guard" to say the registry
  now stores the neutral `SyncBackendBase` (Plan 3).

- [ ] **Step 4: `src/engine/syncengine.cpp`** — at the 5 `backendInstance` sites
  (~738/751/776/1080/1187 and any in the worker path), change the local type
  `SyncBackend *base = m_registry->backendInstance(id);` →
  `SyncBackendBase *base = m_registry->backendInstance(id);`. The subsequent calls
  (`backendType`, `discoveredWritable`, `loadRecordsOrError`, `fetchItems`, `createRecord`, …,
  and the `dynamic_cast<Backend::ChangeDetection*>(base)`) all resolve on `SyncBackendBase`/
  `IBlobBackend` now. Remove `#include "../calendar/syncbackend.h"` from `syncengine.cpp`/`.h`
  if present; ensure `#include "../sync/syncbackendbase.h"` is present.

  Run first to find every site: `grep -nE 'backendInstance|SyncBackend[^B]' src/engine/syncengine.cpp`
  and retype each `SyncBackend*` holding a registry result to `SyncBackendBase*`.

- [ ] **Step 5: Build + full test.**

Run: `cmake --build build -j$(($(nproc)-1)) && ctest --test-dir build -j$(($(nproc)-1))`
Expected: clean build; 132/132. If the engine still references a `SyncBackend`-only method on a
registry-obtained pointer, the compile fails and names it — STOP and report (investigation says
only `discoveredWritable`, now on the base).

- [ ] **Step 6: Commit**

```bash
git add src/sync/backendregistry.h src/sync/backendregistry.cpp src/sync/providermanager.cpp src/engine/syncengine.cpp src/engine/syncengine.h
git commit -m "refactor(sync): BackendRegistry + ProviderManager + engine traffic in SyncBackendBase (P3.T6)"
```

---

### Task 7: Proof-of-neutrality test

Pin that the neutralized core works with a backend that is ONLY a `SyncBackendBase` (never a
calendar `SyncBackend`). `RawFilesBackend` is now exactly that.

**Files:** Create `tests/sinks/tst_neutral_sync_core.cpp`; modify `tests/sinks/CMakeLists.txt`.

- [ ] **Step 1: Inspect the existing `tests/sinks/` harness** to match its conventions (how it
  builds a `RawFilesBackend` in a `QTemporaryDir`, the include paths, the `kalburator_add_*`
  CMake helper). Run: `ls tests/sinks/ && sed -n '1,60p' tests/sinks/CMakeLists.txt`.

- [ ] **Step 2: Write the test** — register a `RawFilesBackend` through the registry as the
  neutral type and round-trip a record, plus assert the registry's API is neutral:

```cpp
#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "backendregistry.h"
#include "syncbackendbase.h"
#include "rawfilesbackend.h"
#include "backendrecord.h"

using namespace Kalburator::Sync;

class TestNeutralSyncCore : public QObject
{
    Q_OBJECT
private slots:
    void registry_storesAndReturnsNeutralBase_forNonCalendarBackend();
    void neutralBackend_roundTripsRecordThroughRegistry();
};

// Compile-time proof: the registry's instance API is the neutral base, not a calendar type.
static_assert(std::is_same_v<
    decltype(std::declval<BackendRegistry>().backendInstance(QString{})),
    SyncBackendBase*>,
    "BackendRegistry must traffic in the neutral SyncBackendBase, not calendar SyncBackend");

void TestNeutralSyncCore::registry_storesAndReturnsNeutralBase_forNonCalendarBackend()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BackendRegistry registry;
    auto backend = std::make_unique<RawFilesBackend>(dir.path());  // adapt ctor to actual signature

    // RawFilesBackend is a SyncBackendBase (NOT a calendar SyncBackend) — this registers
    // a purely-neutral backend, which only compiles because the registry took SyncBackendBase*.
    registry.registerBackendInstance(QStringLiteral("raw-1"), backend.get());

    SyncBackendBase *got = registry.backendInstance(QStringLiteral("raw-1"));
    QVERIFY(got == backend.get());
    QCOMPARE(got->backendType(), QStringLiteral("raw-files"));
    QVERIFY(got->discoveredWritable(QStringLiteral("any")));  // neutral default = true
}

void TestNeutralSyncCore::neutralBackend_roundTripsRecordThroughRegistry()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BackendRegistry registry;
    auto backend = std::make_unique<RawFilesBackend>(dir.path());  // adapt ctor
    registry.registerBackendInstance(QStringLiteral("raw-1"), backend.get());
    SyncBackendBase *b = registry.backendInstance(QStringLiteral("raw-1"));

    // Drive record CRUD purely through the neutral interface.
    const QString col = QStringLiteral("col-1");  // adapt: create collection per RawFilesBackend API
    BackendRecord rec;                            // adapt: populate the minimal valid record
    rec.id = QStringLiteral("rec-1");
    const QString createdId = b->createRecord(col, rec);
    QVERIFY(!createdId.isEmpty());

    QString err;
    QList<BackendRecord> out;
    QVERIFY(b->loadRecordsOrError(col, out, err));
    QVERIFY(err.isEmpty());
    QVERIFY(std::any_of(out.cbegin(), out.cend(),
                        [](const BackendRecord &r){ return r.id == QStringLiteral("rec-1"); }));
}

QTEST_MAIN(TestNeutralSyncCore)
#include "tst_neutral_sync_core.moc"
```

  **Adapt to real APIs:** confirm `RawFilesBackend`'s constructor signature, how a collection is
  created (`createCollection`/`availableCollections`), and the minimal valid `BackendRecord`
  shape (fields `id`, `payload`/`data`, `shape`?) by reading `rawfilesbackend.h` +
  `backendrecord.h`. If `createRecord` needs a pre-existing collection, create it first via the
  backend's collection API. The intent is fixed: **a non-calendar backend round-trips a record
  through the neutral registry**; make the calls match the real signatures. If the record shape
  is non-trivial, model it on an existing `tests/sinks/` test that exercises `RawFilesBackend`.

- [ ] **Step 3: Register in CMake.** Add to `tests/sinks/CMakeLists.txt` the same registration
  form the other sinks tests use (e.g. `kalburator_add_sinks_test(tst_neutral_sync_core)` or
  whatever helper exists there — match the existing pattern exactly).

- [ ] **Step 4: Build + run the new test.**

Run: `cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build -j$(($(nproc)-1)) --target tst_neutral_sync_core && ctest --test-dir build -R tst_neutral_sync_core --output-on-failure`
Expected: PASS. (The `static_assert` is the compile-time proof; the two tests are the runtime
proof.)

- [ ] **Step 5: Commit**

```bash
git add tests/sinks/tst_neutral_sync_core.cpp tests/sinks/CMakeLists.txt
git commit -m "test(sinks): prove the sync core is calendar-neutral (P3.T7)"
```

---

### Task 8: Acceptance gate + close the plan

**Files:** Modify `docs/campaign/architectural-redress/STATUS.md`,
`docs/campaign/architectural-redress/FINDINGS.md`.

- [ ] **Step 1: Layering acceptance greps.** Run and confirm the CORE is neutral (providers are
  out of scope and WILL still match — that is expected):

```
echo "=== core sync/ files must NOT include calendar (providers excluded) ==="
grep -nE '#include .*(calendar/syncbackend|calendar/syncoperation)' src/sync/backendregistry.* src/sync/providermanager.* src/sync/syncbackendbase.* ; echo "(empty = good)"
echo "=== engine must NOT include calendar backend/operation ==="
grep -nE '#include .*calendar/(syncbackend|syncoperation)' src/engine/*.{h,cpp} ; echo "(empty = good)"
echo "=== contacts/ + universal/ backends must NOT include calendar/syncbackend ==="
grep -rnE '#include .*calendar/syncbackend' src/contacts/ src/universal/ ; echo "(empty = good)"
echo "=== syncbackendbase.h must be KCalendarCore-free ==="
grep -nE 'KCalendarCore' src/sync/syncbackendbase.h ; echo "(empty = good)"
echo "=== NOTE: providers still include domain backends (deferred — separate plan): ==="
grep -rnE '#include .*\.\./(calendar|contacts)/' src/sync/*provider*.cpp
```
All four "(empty = good)" greps must be empty. The last grep (providers) is expected to show
hits — that is the deferred scope, not a failure.

- [ ] **Step 2: Full regression gate.**

Run: `cmake --build build -j$(($(nproc)-1)) && ctest --test-dir build --output-on-failure -j$(($(nproc)-1))`
Expected: 133/133 (132 prior + `tst_neutral_sync_core`). If `tst_engine_cancellation` segfaults
under `-jN`, rerun isolated (`ctest --test-dir build -R '^tst_engine_cancellation$'`) to confirm
it is the pre-existing flake.

- [ ] **Step 3: Verify PlanStan/WildPalms contract (INVARIANTS §10).** `BackendRegistry`'s
  public instance API changed type (`SyncBackend*` → `SyncBackendBase*`). Check whether PlanStan
  (`/home/clinton/dev/PlanStan`) or WildPalms (`/home/clinton/dev/WildPalms`) call
  `registerBackendInstance`/`backendInstance` with a calendar `SyncBackend*` and rely on the
  return type:

  Run: `grep -rn 'backendInstance\|registerBackendInstance' /home/clinton/dev/PlanStan /home/clinton/dev/WildPalms 2>/dev/null`

  If consumers exist and rely on the calendar return type, note it in FINDINGS as a downstream
  port item (they can up-cast or the call still binds via `SyncBackend* → SyncBackendBase*`). If
  none, note "no downstream consumers of the registry instance API."

- [ ] **Step 4: Update STATUS + FINDINGS.** In `STATUS.md`, set the Plan 3 table row to
  `**DONE — feature/redress-3-neutralize-sync-core**` and the "Next action" to Plan 4
  (Correctness/ownership sweep). Add a "Plan 3 outcome" note: CRITICALs #1–#3 resolved; the
  `engine/contacts/universal → calendar` include MAJORs resolved; provider→domain includes
  explicitly deferred. In `FINDINGS.md`, cross out (resolved) the audit's calendar-typed-core
  CRITICAL pointer in the Baseline summary, and add a one-line note that the provider-construction
  includes remain open (deferred plan).

- [ ] **Step 5: Commit**

```bash
git add docs/campaign/architectural-redress/STATUS.md docs/campaign/architectural-redress/FINDINGS.md
git commit -m "docs(campaign): close Plan 3 — calendar-typed sync core neutralized (P3.T8)"
```

Then use `superpowers:finishing-a-development-branch`.

---

## Self-Review

**1. Spec/design coverage:**
- Extract neutral `SyncOperation` → T1.
- `discoveredWritable` → base → T2.
- Neutralize `SyncBackendBase` operation returns → T3.
- Engine depends on neutral operation + drops calendar include → T4.
- Reparent the 4 backends (removes contacts/universal → calendar) → T5.
- Registry + ProviderManager + engine traffic in `SyncBackendBase*` (CRITICAL #1/#2) → T6.
- Proof of neutrality → T7.
- Acceptance + downstream contract + close → T8.
All design elements covered. CRITICAL #3 (non-calendar backends inheriting calendar type) is T5.

**2. Placeholder scan:** Concrete edits/signatures throughout. The two soft spots are flagged
with explicit "adapt to real API" instructions and a fixed intent: (a) T7's `RawFilesBackend`
ctor + `BackendRecord` shape (read the two headers — known to exist), and (b) T4 Step 3's
`await<>`/operation-type grep (mechanical retype, with a STOP rule if a calendar payload is read).
These are verification-and-adapt steps, not vague TODOs.

**3. Type consistency:** `SyncBackendBase*` is the single neutral type used consistently across
registry (T6), provider manager (T6), engine locals (T4/T6), and backend bases (T5). Operation
type is `SyncOperation*` consistently (T1/T3/T4). `discoveredWritable(const QString&) const`
signature identical in T2 (base) and the existing overrides. Covariant `fetchItems`/`deleteItems`
returns: base `SyncOperation*` (T3), calendar override `FetchOperation*`/`DeleteOperation*`
(unchanged) — consistent.

**4. Incrementality:** Each task leaves the tree compiling and 132 tests green (verified by the
ordering: neutral base exists before the base API uses it (T1<T3); base API neutral and engine
locals neutral before the registry returns the neutral type (T3,T4 < T6); backends reparented
independently (T5) since `SyncBackend` IS-A `SyncBackendBase`). No task depends on a later one to
compile.

**5. Scope:** Providers explicitly excluded; acceptance greps scoped to the core + flag the
deferred provider includes rather than failing on them.
