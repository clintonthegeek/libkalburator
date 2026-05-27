# O15 — Calendar write-path convergence — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Retire `CalendarPluginWriter` and the `SyncTransaction`/`*IncidenceItem`
machinery so the calendar domain writes through the uniform `DefaultBlobWriter`
record path (invariant §1), without losing any *real* behavioral contract.

**Architecture:** The engine already demotes canon → target native encoding before
calling a `RecordWriter`. Calendar's writer forks between a host-`MemoryCalendar`
`SyncTransaction` path (path 1) and a `DefaultBlobWriter`-equivalent "fallback"
(path 2). Path 1's only live differentiator — transactional rollback — was shown
to be a MockBackend artifact (design §1), so we route calendar through
`DefaultBlobWriter` and rewrite the rollback-pinning tests to the retry-safe
contract that actually holds.

**Tech Stack:** C++17, Qt6, CMake, QtTest, KCalendarCore. Build dir: `build/`.

**Design:** `docs/2026-05-26-o15-calendar-write-path-convergence-design.md`.
**Finding:** `docs/campaign/FINDINGS.md` O15.

**Key ordering trick:** The rewritten tests assert *failure-propagates*,
*retry-converges*, and *pre-existing-data-preserved* — all true under BOTH the old
(rollback) and new (best-effort) behavior, because they never assert the unordered
partial-write count. So Phase 1 lands the test changes FIRST (green under old
behavior), then the routing change (green under new behavior). Every commit green.

**Build / test commands:**
```bash
cmake -S /home/clinton/dev/libkalburator -B /home/clinton/dev/libkalburator/build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build /home/clinton/dev/libkalburator/build --target tst_calendar_sync_error_recovery
ctest --test-dir /home/clinton/dev/libkalburator/build -R tst_calendar_sync_error_recovery -VV
ctest --test-dir /home/clinton/dev/libkalburator/build   # full suite
```
The full suite has one known-unrelated failure: `tst_providerlifecycle` (FINDINGS
O9). Treat "all green except O9" as success.

---

## File structure

**Phase 1 (sever + delete writer):**
- Modify: `tests/calendar/tst_calendar_sync_error_recovery.cpp` (rewrite/delete/trim slots)
- Modify: `src/calendar/calendardomainoperations.cpp` (`createWriter` → `nullptr`)
- Modify: `src/engine/syncengine.cpp` (null-writer → `DefaultBlobWriter`; drop `ctx.calendarCollection`)
- Delete: `src/calendar/calendarplugin_writer.{h,cpp}`, `tests/calendar/tst_calendar_plugin_writer.cpp`
- Modify: `CMakeLists.txt`, `tests/calendar/CMakeLists.txt` (drop writer entries)

**Phase 2 (delete dead transaction classes):**
- Delete: `src/calendar/synctransaction.{h,cpp}`, `synctransactionitem.{h,cpp}`,
  `createincidenceitem.{h,cpp}`, `updateincidenceitem.{h,cpp}`,
  `deleteincidenceitem.{h,cpp}`, `synctesthooks.h`,
  `tests/calendar/tst_synctransaction.cpp`
- Modify: `CMakeLists.txt`, `tests/calendar/CMakeLists.txt`, `src/calendar/localbackend.{h,cpp}` (comments), `src/engine/syncengine.{h,cpp}` (comments)
- Modify: `docs/campaign/FINDINGS.md` (close O15)

---

## PHASE 1 — Sever + delete the writer

### Task 1: Rewrite the rollback-asserting tests to the retry-safe contract

These edits keep the suite green under the *current* (rollback) behavior — they
assert only properties true under both behaviors.

**Files:**
- Modify: `tests/calendar/tst_calendar_sync_error_recovery.cpp`

- [ ] **Step 1: Delete the 3 redundant rollback-duplicate slot declarations**

In the `private slots:` block (around lines 111, 113, 119), delete these lines:
```cpp
    void partialWriteRollback_targetClean();
```
```cpp
    void crashRecoveryReplay_targetClean();
```
```cpp
    void pendingLogContentFidelity_targetRolledBack();
```

- [ ] **Step 2: Delete the 3 corresponding slot bodies**

Delete the entire function bodies of `partialWriteRollback_targetClean`,
`crashRecoveryReplay_targetClean`, and `pendingLogContentFidelity_targetRolledBack`
(they assert `targetUids() == 0` via the same `OnStoreItems,2` setup as
`storeFailsPartial`, so they are pure duplicates of rollback behavior).

- [ ] **Step 3: Rename + rewrite `storeFailsPartial_rolledBack`**

Declaration (line ~106) → rename:
```cpp
    void storeFailsPartial_failsAndRetryConverges();
```
Body → replace with:
```cpp
void TestCalendarSyncErrorRecovery::storeFailsPartial_failsAndRetryConverges()
{
    // Target fails after 2 of 5 stores. The converged engine is best-effort
    // (no transactional rollback): the sync reports failure and does NOT save
    // baselines, so a retry re-attempts the writes and converges. We do not
    // assert the partial count — it is unordered and not contractual.
    addSourceEvent(QStringLiteral("event-1"), QStringLiteral("Event One"));
    addSourceEvent(QStringLiteral("event-2"), QStringLiteral("Event Two"));
    addSourceEvent(QStringLiteral("event-3"), QStringLiteral("Event Three"));
    addSourceEvent(QStringLiteral("event-4"), QStringLiteral("Event Four"));
    addSourceEvent(QStringLiteral("event-5"), QStringLiteral("Event Five"));

    m_target->setFailurePoint(MockBackend::FailurePoint::OnStoreItems, 2);
    QVERIFY(runOneSync());
    QVERIFY2(!m_lastResult.success, "Partial store failure must propagate");
    QCOMPARE(sourceUids().size(), 5);

    // Retry-safe: clearing the failure and re-syncing converges the target.
    m_target->clearFailurePoint();
    QVERIFY(runOneSync());
    QVERIFY2(m_lastResult.success, "Retry after cleared failure must succeed");
    QCOMPARE(targetUids().size(), 5);
    QCOMPARE(sourceUids().size(), 5);
}
```

- [ ] **Step 4: Rename + rewrite `pushFailsPartial_rolledBack`**

Declaration (line ~108) → rename:
```cpp
    void pushFailsPartial_failsAndRetryConverges();
```
Body → replace with:
```cpp
void TestCalendarSyncErrorRecovery::pushFailsPartial_failsAndRetryConverges()
{
    // OnPush variant of storeFailsPartial: best-effort + retry-safe.
    addSourceEvent(QStringLiteral("event-1"), QStringLiteral("Event One"));
    addSourceEvent(QStringLiteral("event-2"), QStringLiteral("Event Two"));
    addSourceEvent(QStringLiteral("event-3"), QStringLiteral("Event Three"));
    addSourceEvent(QStringLiteral("event-4"), QStringLiteral("Event Four"));
    addSourceEvent(QStringLiteral("event-5"), QStringLiteral("Event Five"));

    m_target->setFailurePoint(MockBackend::FailurePoint::OnPush, 2);
    QVERIFY(runOneSync());
    QVERIFY2(!m_lastResult.success, "Partial push failure must propagate");
    QCOMPARE(sourceUids().size(), 5);

    m_target->clearFailurePoint();
    QVERIFY(runOneSync());
    QVERIFY2(m_lastResult.success, "Retry after cleared failure must succeed");
    QCOMPARE(targetUids().size(), 5);
    QCOMPARE(sourceUids().size(), 5);
}
```

- [ ] **Step 5: Rename + rewrite `deleteFailsPartial_rolledBack`**

Declaration (line ~110) → rename:
```cpp
    void deleteFailsPartial_failsAndRetryConverges();
```
Body → replace with:
```cpp
void TestCalendarSyncErrorRecovery::deleteFailsPartial_failsAndRetryConverges()
{
    // Both start with 3 identical events (baseline sync). Delete all 3 from
    // source; target fails after 1 delete. Best-effort leaves a partial state,
    // but the sync fails and baselines are not advanced, so a retry converges
    // the target to empty.
    addEventToBoth(QStringLiteral("event-1"), QStringLiteral("Event One"));
    addEventToBoth(QStringLiteral("event-2"), QStringLiteral("Event Two"));
    addEventToBoth(QStringLiteral("event-3"), QStringLiteral("Event Three"));

    QVERIFY(runOneSync());                 // establish baselines
    QCOMPARE(sourceUids().size(), 3);
    QCOMPARE(targetUids().size(), 3);

    m_source->removeItem(QString::fromLatin1(kCalendarId), QStringLiteral("event-1"));
    m_source->removeItem(QString::fromLatin1(kCalendarId), QStringLiteral("event-2"));
    m_source->removeItem(QString::fromLatin1(kCalendarId), QStringLiteral("event-3"));
    QCOMPARE(sourceUids().size(), 0);

    m_target->setFailurePoint(MockBackend::FailurePoint::OnDelete, 1);
    QVERIFY(runOneSync());
    QVERIFY2(!m_lastResult.success, "Partial delete failure must propagate");
    QCOMPARE(sourceUids().size(), 0);

    m_target->clearFailurePoint();
    QVERIFY(runOneSync());
    QVERIFY2(m_lastResult.success, "Retry after cleared failure must succeed");
    QCOMPARE(targetUids().size(), 0);
}
```

- [ ] **Step 6: Rename + rewrite `rollbackPreservesPreExistingData`**

Declaration (line ~114) → rename:
```cpp
    void preExistingDataPreservedOnFailure();
```
Body → replace with:
```cpp
void TestCalendarSyncErrorRecovery::preExistingDataPreservedOnFailure()
{
    // Target pre-populated with 3 events; source has 5 new events. Target fails
    // mid-store. The failed target-direction write must not destroy pre-existing
    // target data; the (successful) target->source direction still propagates
    // the 3 pre-existing items to source.
    addTargetEvent(QStringLiteral("existing-1"), QStringLiteral("Existing One"));
    addTargetEvent(QStringLiteral("existing-2"), QStringLiteral("Existing Two"));
    addTargetEvent(QStringLiteral("existing-3"), QStringLiteral("Existing Three"));
    QCOMPARE(targetUids().size(), 3);

    addSourceEvent(QStringLiteral("new-1"), QStringLiteral("New One"));
    addSourceEvent(QStringLiteral("new-2"), QStringLiteral("New Two"));
    addSourceEvent(QStringLiteral("new-3"), QStringLiteral("New Three"));
    addSourceEvent(QStringLiteral("new-4"), QStringLiteral("New Four"));
    addSourceEvent(QStringLiteral("new-5"), QStringLiteral("New Five"));

    m_target->setFailurePoint(MockBackend::FailurePoint::OnStoreItems, 2);
    QVERIFY(runOneSync());
    QVERIFY2(!m_lastResult.success, "Partial store failure must propagate");

    // Pre-existing target data is untouched by the failed sync.
    QVERIFY(targetUids().contains(QStringLiteral("existing-1")));
    QVERIFY(targetUids().contains(QStringLiteral("existing-2")));
    QVERIFY(targetUids().contains(QStringLiteral("existing-3")));

    // target->source direction succeeded: source gained the 3 pre-existing items.
    QCOMPARE(sourceUids().size(), 8);
}
```

- [ ] **Step 7: Rename + rewrite `mixedOperationRollback_targetRestored`**

Declaration (line ~116) → rename:
```cpp
    void mixedOperationFailure_sharedDataPreservedAndRetryConverges();
```
Body → replace with:
```cpp
void TestCalendarSyncErrorRecovery::mixedOperationFailure_sharedDataPreservedAndRetryConverges()
{
    // Both start with 3 identical events (baseline). Add 3 new to source; target
    // fails mid-push. Shared (baseline) data must remain; retry converges to 6.
    addEventToBoth(QStringLiteral("shared-1"), QStringLiteral("Shared One"));
    addEventToBoth(QStringLiteral("shared-2"), QStringLiteral("Shared Two"));
    addEventToBoth(QStringLiteral("shared-3"), QStringLiteral("Shared Three"));

    QVERIFY(runOneSync());                 // establish baselines
    QCOMPARE(sourceUids().size(), 3);
    QCOMPARE(targetUids().size(), 3);

    addSourceEvent(QStringLiteral("new-1"), QStringLiteral("New One"));
    addSourceEvent(QStringLiteral("new-2"), QStringLiteral("New Two"));
    addSourceEvent(QStringLiteral("new-3"), QStringLiteral("New Three"));
    QCOMPARE(sourceUids().size(), 6);

    m_target->setFailurePoint(MockBackend::FailurePoint::OnPush, 2);
    QVERIFY(runOneSync());
    QVERIFY2(!m_lastResult.success, "Partial push failure must propagate");

    QVERIFY(m_target->incidence(QString::fromLatin1(kCalendarId), QStringLiteral("shared-1")));
    QVERIFY(m_target->incidence(QString::fromLatin1(kCalendarId), QStringLiteral("shared-2")));
    QVERIFY(m_target->incidence(QString::fromLatin1(kCalendarId), QStringLiteral("shared-3")));

    m_target->clearFailurePoint();
    QVERIFY(runOneSync());
    QVERIFY2(m_lastResult.success, "Retry after cleared failure must succeed");
    QCOMPARE(targetUids().size(), 6);
}
```

- [ ] **Step 8: Trim `rollbackFailureResilience_errorReported`**

Keep the slot name and setup; delete only the final rollback assertion line:
```cpp
    QCOMPARE(targetUids().size(), 0); // 1 committed item rolled back
```
(Leave the `!m_lastResult.success` and `!m_lastResult.errorMessage.isEmpty()`
assertions — they hold under best-effort.)

- [ ] **Step 9: Trim `retryAfterFailure_recoversCorrectly`**

Delete only the mid-test rollback assertion line:
```cpp
    QCOMPARE(targetUids().size(), 0); // rolled back
```
(The rest of this slot already asserts the retry-safe contract — keep it.)

- [ ] **Step 10: Build + run the test under current behavior**

Run: `cmake --build /home/clinton/dev/libkalburator/build --target tst_calendar_sync_error_recovery && ctest --test-dir /home/clinton/dev/libkalburator/build -R tst_calendar_sync_error_recovery -VV`
Expected: PASS (all slots green — these assertions hold under the existing
rollback behavior because none assert the partial count).

- [ ] **Step 11: Commit**

```bash
git add tests/calendar/tst_calendar_sync_error_recovery.cpp
git commit -m "test(calendar): assert retry-safe contract, not rollback artifact (O15)"
```

---

### Task 2: Route calendar through DefaultBlobWriter

**Files:**
- Modify: `src/calendar/calendardomainoperations.cpp`
- Modify: `src/engine/syncengine.cpp:2624-2626`, `:2654-2656`, `:2529-2533`

- [ ] **Step 1: Make `CalendarDomainOperations::createWriter` return nullptr**

In `src/calendar/calendardomainoperations.cpp`, replace the body:
```cpp
std::unique_ptr<Kalburator::Shape::RecordWriter>
CalendarDomainOperations::createWriter(Kalburator::Sync::SyncBackendBase *backend) const
{
    auto *syncBackend = qobject_cast<Kalburator::Sync::SyncBackend *>(backend);
    if (!syncBackend) return nullptr;
    return std::make_unique<Kalburator::Calendar::CalendarPluginWriter>(syncBackend);
}
```
with:
```cpp
std::unique_ptr<Kalburator::Shape::RecordWriter>
CalendarDomainOperations::createWriter(Kalburator::Sync::SyncBackendBase *backend) const
{
    // Calendar uses the uniform DefaultBlobWriter record path. Returning nullptr
    // signals the engine to build it (O15 convergence). Type-aware work (typed
    // diff, loss model, conflict detection) is upstream in the canon/diff/merge
    // layer, not the writer.
    Q_UNUSED(backend);
    return nullptr;
}
```
Then remove the now-unused include near the top of the file:
```cpp
#include "calendarplugin_writer.h"
```

- [ ] **Step 2: Make the engine treat a null writer as DefaultBlobWriter**

In `src/engine/syncengine.cpp`, the target-write site (~`:2624`):
```cpp
        auto tgtWriter = opsUCC
            ? opsUCC->createWriter(tgtBackend)
            : std::make_unique<Kalburator::Shape::DefaultBlobWriter>(tgtBackend);
```
→ replace with:
```cpp
        auto tgtWriter = opsUCC ? opsUCC->createWriter(tgtBackend) : nullptr;
        if (!tgtWriter)
            tgtWriter = std::make_unique<Kalburator::Shape::DefaultBlobWriter>(tgtBackend);
```
And the source-write site (~`:2654`):
```cpp
        auto srcWriter = opsUCC
            ? opsUCC->createWriter(srcBackend)
            : std::make_unique<Kalburator::Shape::DefaultBlobWriter>(srcBackend);
```
→ replace with:
```cpp
        auto srcWriter = opsUCC ? opsUCC->createWriter(srcBackend) : nullptr;
        if (!srcWriter)
            srcWriter = std::make_unique<Kalburator::Shape::DefaultBlobWriter>(srcBackend);
```
(`DefaultBlobWriter` takes an `IBlobBackend*`; `SyncBackend*` converts — the prior
`: DefaultBlobWriter(tgtBackend)` fallback already relied on this.)

- [ ] **Step 3: Stop feeding the host MemoryCalendar to writers**

In `applyBatch` (~`:2529`), replace:
```cpp
        Kalburator::Shape::RecordWriter::ApplyContext ctx;
        ctx.collectionId = colId;
        ctx.calendarCollection = m_collection
            ? m_collection->calendar(colId)
            : nullptr;
        writer->prepareForApply(ctx);
```
with:
```cpp
        // The converged writers (DefaultBlobWriter) ignore the host
        // MemoryCalendar; do not source it. prepareForApply remains a no-op hook
        // on the RecordWriter interface. (m_collection / setCollection stay for
        // CalendarManager's separate use.)
        Kalburator::Shape::RecordWriter::ApplyContext ctx;
        ctx.collectionId = colId;
        writer->prepareForApply(ctx);
```

- [ ] **Step 4: Build the full library + calendar tests**

Run: `cmake --build /home/clinton/dev/libkalburator/build`
Expected: builds clean. `CalendarPluginWriter` is now unreferenced (still
compiled; deleted in Task 3).

- [ ] **Step 5: Run the calendar integration suite**

Run: `ctest --test-dir /home/clinton/dev/libkalburator/build -R "tst_calendar|tst_engine" -VV`
Expected: PASS — including the rewritten `tst_calendar_sync_error_recovery`
(now exercising the best-effort path) and all other calendar/engine tests.

- [ ] **Step 6: Run the full suite**

Run: `ctest --test-dir /home/clinton/dev/libkalburator/build`
Expected: all green except `tst_providerlifecycle` (O9).

- [ ] **Step 7: Commit**

```bash
git add src/calendar/calendardomainoperations.cpp src/engine/syncengine.cpp
git commit -m "refactor(calendar): route calendar writes through DefaultBlobWriter (O15)"
```

---

### Task 3: Delete CalendarPluginWriter

**Files:**
- Delete: `src/calendar/calendarplugin_writer.{h,cpp}`, `tests/calendar/tst_calendar_plugin_writer.cpp`
- Modify: `CMakeLists.txt:134,176`; `tests/calendar/CMakeLists.txt:95`

- [ ] **Step 1: Delete the writer source + its test**

```bash
git rm src/calendar/calendarplugin_writer.h src/calendar/calendarplugin_writer.cpp \
       tests/calendar/tst_calendar_plugin_writer.cpp
```

- [ ] **Step 2: Remove from the library CMakeLists.txt**

In `CMakeLists.txt`, delete these two lines:
```cmake
    src/calendar/calendarplugin_writer.h
```
```cmake
    src/calendar/calendarplugin_writer.cpp
```

- [ ] **Step 3: Remove the test registration**

In `tests/calendar/CMakeLists.txt`, delete:
```cmake
kalburator_add_calendar_integration_test(tst_calendar_plugin_writer)
```

- [ ] **Step 4: Reconfigure + build**

Run: `cmake -S /home/clinton/dev/libkalburator -B /home/clinton/dev/libkalburator/build && cmake --build /home/clinton/dev/libkalburator/build`
Expected: builds clean, no references to the deleted header.

- [ ] **Step 5: Run the full suite**

Run: `ctest --test-dir /home/clinton/dev/libkalburator/build`
Expected: all green except O9.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "refactor(calendar): delete CalendarPluginWriter, retired by convergence (O15)"
```

**Phase 1 done:** the second write *mechanism* is gone. Invariant §1's concern is
resolved. `SyncTransaction`/`*IncidenceItem` are now consumed only by their own
tests + `synctesthooks.h`.

---

## PHASE 2 — Delete the dead transaction classes

### Task 4: Confirm no remaining consumers, then delete the transaction classes

**Files:**
- Delete: `src/calendar/synctransaction.{h,cpp}`, `synctransactionitem.{h,cpp}`,
  `createincidenceitem.{h,cpp}`, `updateincidenceitem.{h,cpp}`,
  `deleteincidenceitem.{h,cpp}`, `synctesthooks.h`,
  `tests/calendar/tst_synctransaction.cpp`
- Modify: `CMakeLists.txt` (lines 93,99,117,118,119,120,142,148,160,161,162),
  `tests/calendar/CMakeLists.txt:109`

- [ ] **Step 1: Verify there are no non-test, non-self consumers**

Run:
```bash
cd /home/clinton/dev/libkalburator
grep -rn "SyncTransaction\|CreateIncidenceItem\|UpdateIncidenceItem\|DeleteIncidenceItem\|SyncTestHooks\|SYNC_HOOK" src/ tests/ \
  | grep -v "src/calendar/synctransaction\|src/calendar/synctransactionitem\|src/calendar/createincidenceitem\|src/calendar/updateincidenceitem\|src/calendar/deleteincidenceitem\|src/calendar/synctesthooks\|tests/calendar/tst_synctransaction"
```
Expected: only **comment** lines in `src/calendar/localbackend.{h,cpp}` and
`src/engine/syncengine.{h,cpp}`. If any *code* (include, type use) appears outside
the files being deleted, STOP and report — the consumer map (design §3) is wrong.

- [ ] **Step 2: Delete the class files + their test**

```bash
git rm src/calendar/synctransaction.h src/calendar/synctransaction.cpp \
       src/calendar/synctransactionitem.h src/calendar/synctransactionitem.cpp \
       src/calendar/createincidenceitem.h src/calendar/createincidenceitem.cpp \
       src/calendar/updateincidenceitem.h src/calendar/updateincidenceitem.cpp \
       src/calendar/deleteincidenceitem.h src/calendar/deleteincidenceitem.cpp \
       src/calendar/synctesthooks.h \
       tests/calendar/tst_synctransaction.cpp
```

- [ ] **Step 3: Remove from the library CMakeLists.txt**

In `CMakeLists.txt`, delete these lines:
```cmake
    src/calendar/createincidenceitem.h
    src/calendar/deleteincidenceitem.h
    src/calendar/synctesthooks.h
    src/calendar/synctransaction.h
    src/calendar/synctransactionitem.h
    src/calendar/updateincidenceitem.h
    src/calendar/createincidenceitem.cpp
    src/calendar/deleteincidenceitem.cpp
    src/calendar/synctransaction.cpp
    src/calendar/synctransactionitem.cpp
    src/calendar/updateincidenceitem.cpp
```
(They are not contiguous — remove each from the header list and the source list.
After editing, `grep -n "incidenceitem\|synctransaction\|synctesthooks" CMakeLists.txt`
must return nothing.)

- [ ] **Step 4: Remove the test registration**

In `tests/calendar/CMakeLists.txt`, delete:
```cmake
kalburator_add_calendar_test(tst_synctransaction)
```

- [ ] **Step 5: Fix dangling comment references**

In `src/calendar/localbackend.{h,cpp}`, update the two comments that read
`// Operation-based API for SyncTransaction support` to
`// Operation-based push/delete API (PushOperation / DeleteOperation)`.

In `src/engine/syncengine.{h,cpp}`, update comments that name
`CalendarPluginWriter` / `SyncTransaction` to describe the current
`DefaultBlobWriter` path (touch only the lines that name the removed types; do not
rewrite unrelated prose). Find them with:
```bash
grep -rn "CalendarPluginWriter\|SyncTransaction" src/engine/syncengine.h src/engine/syncengine.cpp
```

- [ ] **Step 6: Reconfigure + build**

Run: `cmake -S /home/clinton/dev/libkalburator -B /home/clinton/dev/libkalburator/build && cmake --build /home/clinton/dev/libkalburator/build`
Expected: builds clean, no dangling references.

- [ ] **Step 7: Run the full suite**

Run: `ctest --test-dir /home/clinton/dev/libkalburator/build`
Expected: all green except O9.

- [ ] **Step 8: Build the optional profiles**

Run:
```bash
cmake -S /home/clinton/dev/libkalburator -B /home/clinton/dev/libkalburator/build-org -DKALBURATOR_HAVE_ORG_IO=ON
cmake --build /home/clinton/dev/libkalburator/build-org
```
Expected: builds clean (the org backend uses the `IBlobBackend` surface, not the
deleted transaction classes). If `HAVE_AKONADI=ON` is buildable in this
environment, repeat with that flag; otherwise note it as untested.

- [ ] **Step 9: Commit**

```bash
git add -A
git commit -m "refactor(calendar): delete SyncTransaction + *IncidenceItem machinery (O15)"
```

---

### Task 5: Close O15 in FINDINGS

**Files:**
- Modify: `docs/campaign/FINDINGS.md`

- [ ] **Step 1: Move O15 to Resolved**

Cut the `### O15 …` block from "Open issues / watch items" and add to the
"Resolved" section:
```markdown
### O15 — CalendarPluginWriter dual write-path (resolved 2026-05-26)
Converged the calendar domain onto the uniform `DefaultBlobWriter` record path and
deleted `CalendarPluginWriter` + the `SyncTransaction`/`*IncidenceItem` machinery.
Investigation found path (1) provided no live benefit it appeared to: the host
`MemoryCalendar` was never written, the `simulate()`-based collision/version
checks were never invoked (`commitAll` only), and its one live differentiator —
transactional rollback — was a MockBackend artifact (sticky per-op-type failure
let rollback ops of a different type succeed; under systemic failure all writes
fail and `rollbackCommitted` misreports success). The retry-safe property
(baselines not saved on failure) is preserved and domain-uniform; conflict
detection lives in the canon diff/merge + `ConflictManager` layer. The ~9
rollback-asserting slots in `tst_calendar_sync_error_recovery` were rewritten to
the retry-safe contract (3 redundant duplicates deleted). Downstream caveat:
PlanStan/WildPalms code constructing `CalendarPluginWriter` or the `*IncidenceItem`
classes directly will fail to compile until ported (consistent with O7/O12).
See `docs/2026-05-26-o15-calendar-write-path-convergence-design.md`.
```

- [ ] **Step 2: Commit**

```bash
git add docs/campaign/FINDINGS.md
git commit -m "docs(campaign): close O15 — calendar write-path converged"
```

---

## Self-review notes

- **Spec coverage:** Phase 1 §4 steps 1–7 → Tasks 1–3; Phase 2 §5 → Tasks 4–5;
  rollback evidence (design §1) → Task 5 closeout text; test rewrite (design §4
  step 6, ~9 slots) → Task 1 steps 1–9. Optional-profile build (design §5 exit)
  → Task 4 step 8.
- **Type/name consistency:** `createWriter` returns `nullptr`; engine guards with
  `if (!tgtWriter)` / `if (!srcWriter)`; `DefaultBlobWriter(tgtBackend)` matches
  the pre-existing fallback. Renamed test slots are updated in BOTH the
  declaration and the definition within the same task.
- **No placeholders:** every code step shows the exact before/after.
