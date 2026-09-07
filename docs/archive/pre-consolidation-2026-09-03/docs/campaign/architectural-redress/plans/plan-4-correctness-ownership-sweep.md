# Correctness / Ownership Sweep — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the five MAJOR correctness/ownership latent bugs the verified AUDIT pins, plus the
one same-file MODERATE that is the direct collaborator of the raw-`bool*` fix: (1) `MockBlobBackend`
swallows injected `OnLoadRecords` failures → test false-greens; (2) `GenericSqliteBackend::clearCollection`/
`deleteCollection` silently ignore DELETE/DROP failures; (3) `RawFilesBackend` + `GenericSqliteBackend`
race on their collection hashes (worker-thread `shapeFor()` reads vs. main-thread writes); (4) a raw
`bool*` captured by two lambdas in `CardDavProvider` (use-after-free); (5) raw `QFutureInterface*`
members in `SyncEngine` leak/dangle if destroyed mid-sync; (5b, folded-in MODERATE) raw `QPromise*`
lifecycle fragmentation in `CardDavCapabilityDiscovery`.

**Architecture:** Each fix is local to one class. Test-harness and silent-failure bugs (T1, T2)
are red-green: a test pins the bug first. Ownership/safety bugs (T3–T6) are structural — convert
hand-managed raw pointers to `std::unique_ptr`/`std::shared_ptr` and add a `QMutex` for the shared
collection hashes; the regression guard is the existing suite staying green, plus a destroy-mid-sync
crash test (T6) and a sanitizer pass. No public interface signatures change except
`GenericSqliteBackend::{clearCollection,deleteCollection}` returning `bool` instead of `void` — a
concrete, non-virtual, non-interface method with **no libkalburator-internal override and no
downstream caller** (verified: PlanStan's `clearCollection` is on `BaselineStore`, WildPalms' on
`TodoBlobBackend` — unrelated classes).

**Tech Stack:** C++/Qt6 (`QMutex`/`QMutexLocker`, `QFutureInterface`, `QPromise`, `QFutureWatcher`,
`std::unique_ptr`/`std::make_shared`), QtSql (SQLite), QtTest, CMake (single `kalburator` static
lib; legacy `build/` workflow).

**Branch:** `feature/redress-4-correctness-ownership-sweep` (off `main`). Worktree per
`superpowers:using-git-worktrees`.

**Scope decision (locked 2026-05-29, deviates from INVARIANTS §8 — documented per §"Scope and
exceptions"):** Plan 4 covers the five named MAJOR findings **plus** the MODERATE raw-`QPromise*`
fragmentation in `CardDavCapabilityDiscovery` (AUDIT MODERATE list). Rationale: it is the direct
collaborator of the CardDavProvider `bool*` fix (T4) — fixing the provider's raw lifetime while
leaving its discovery object's raw `QPromise*` is a half-fix of one file family's ownership story.
The other same-class MODERATEs (silent PRAGMA failures in `SyncConflictStore`/`IDMappingStore`)
stay catalogued in AUDIT for a later plan; this plan does **not** touch them. Cited: INVARIANTS §8
(deviation documented, rule cited).

**Nature of change (mixed — read this before starting):**
- **T1, T2 are behavior-changing bug fixes** (red-green TDD). A failure that was silently
  swallowed is now reported. The "red" for a `void`→`bool` signature is a *compile failure* of the
  new test until the signature lands — this matches the repo's existing red-test idiom
  (`tst_engine_registry_injection` "deliberately fails to compile until Task 4").
- **T3–T6 are structural safety/ownership fixes.** T3 (locking) is behavior-preserving for
  single-threaded use and closes a data race; T4/T5/T6 convert hand-managed raw pointers to RAII
  with identical observable behavior. The discipline: **the full existing suite stays green after
  every task** (132 tests + Plan 3's `tst_neutral_sync_core` = 133), the tree compiles after every
  task, plus the new tests below. If an *existing* test fails, STOP — investigate, do not adapt it.

---

## Reference facts (verified against source at HEAD, 2026-05-29)

- **MockBlobBackend** lives at `src/blob/mockblobbackend.{h,cpp}`, namespace `Kalburator::Sync`,
  built into the `kalburator` lib. It overrides `loadRecords()` (consuming `FailurePoint::OnLoadRecords`
  via `consumeFailure()`), but does **not** override `loadRecordsOrError()`. The base default
  (`src/blob/iblobbackend.h:55-61`) calls `loadRecords()`, `error.clear()`, `return true` — so an
  injected `OnLoadRecords` failure surfaces as success-with-empty-list. `consumeFailure()`
  (`mockblobbackend.cpp:12-24`) **decrements** the count and emits `errorOccurred`. The sibling
  `MockBackend::loadRecordsOrError` (`src/calendar/mockbackend.cpp:607-620`) is the mirror pattern:
  check the failure point first, set `error` + `return false`, else delegate to `loadRecords()`.
  Existing test `tst_mockblobbackend.cpp::failureInjectionOnLoadRecords` calls bare `loadRecords()`
  (not the OrError form), so it is unaffected by the new override.
- **GenericSqliteBackend** (`src/universal/genericsqlitebackend.{h,cpp}`, namespace
  `Kalburator::Sinks`, base `SyncBackendBase` after Plan 3): `clearCollection`/`deleteCollection`
  declared `void` at `genericsqlitebackend.h:61-62`; impls at `.cpp:115-122`/`:102-113` call
  `q.exec(...)` without checking, unlike `createRecord` (`:198 if(!q.exec()) return {};`),
  `deleteRecord` (`:236 return q.exec();`), `updateRecord` (`:220 return q.exec() && ...`).
  `<QSqlError>` already `#include`d (`.cpp:4`); `qWarning()` streaming needs `<QDebug>` (NOT yet
  included). Collection hashes: `m_collections`/`m_shapeByCollection` (`genericsqlitebackend.h:86-87`);
  existing `mutable QMutex m_connMutex` (`:88`) guards **only** `m_openConnections`. Accessors of the
  collection hashes: `availableCollections` (`.cpp:54` R), `collectionInfo` (`:59` R),
  `createCollection` (`:69,80,82` R/W — calls `ensureOpen()`+`ensureTableFor()` *before* touching
  the hashes), `deleteCollection` (`:112` W), `nativeShapes` (`:88-93` R), `shapeFor` (`:99` R),
  `ensureOpen` (`:296` W). `createRecord` does **not** read the hashes. `<QMutexLocker>` already
  included (`.cpp:3`).
- **RawFilesBackend** (`src/universal/rawfilesbackend.{h,cpp}`, namespace `Kalburator::Sinks`,
  base `SyncBackendBase`): collection hashes `m_collections`/`m_shapeByCollection`
  (`rawfilesbackend.h:89-90`), **no mutex**. Accessors of the hashes: `availableCollections`
  (`.cpp:34` R), `collectionInfo` (`:39` R), `createCollection` (`:48-52` R/W — calls
  `saveManifest()` while logically mutating), `deleteCollection` (`:75` W — calls `clearCollection()`
  first, which touches **only the filesystem**, then `saveManifest()`), `nativeShapes` (`:59-62` R),
  `shapeFor` (`:69` R), `createRecord` (`:116` R — `m_collections.contains`, runs on worker),
  `loadManifest` (`:182` W — ctor only), `saveManifest` (`:189` R — `const`). No `<QMutex>` include.
- **CardDavProvider** (`src/sync/carddavprovider.{h,cpp}`): `connect()` allocates
  `bool *errorSeen = new bool(false)` (`.cpp:79`), captured by an `error`-signal lambda (`:80-84`,
  writes `*errorSeen = true`) and a `QFutureWatcher::finished` lambda (`:92-99`, reads `*errorSeen`
  then `delete errorSeen` at `:95`). Firing order of the two is undefined → use-after-free if
  `finished` runs first. The header already uses `std::unique_ptr<QPromise<bool>> m_connectPromise`
  (`carddavprovider.h:65`) so `<memory>` is in scope.
- **CardDavCapabilityDiscovery** (`src/sync/carddavcapabilitydiscovery.{h,cpp}`): member
  `class QPromise<QList<CollectionInfo>> *m_promise = nullptr;` (`.h:122`); `new` at `.cpp:74`;
  manual `delete m_promise; m_promise = nullptr;` at four sites — dtor (`:41`), `discover()`
  (`:65`), `resolveWithError()` (`:474`), `resolveWithSuccess()` (`:488`). `resolveWithSuccess`
  does `addResult(books)`; the other three `addResult({})`. The `delete` always follows
  `addResult(...); finish();`.
- **SyncEngine** (`src/engine/syncengine.{h,cpp}`, post-Plan-1): raw members
  `QFutureInterface<SyncResult>* m_currentSingleIface = nullptr;` and
  `QFutureInterface<QList<SyncResult>>* m_currentMultiIface = nullptr;` (`syncengine.h:577-578`).
  `new` at `.cpp:530` (multi) and `:586` (single). `delete X; X = nullptr;` at **8** sites: `:310`,
  `:355`, `:409`, `:464`, `:843`, `:874`, `:1235`, `:1274`. Handed to callers via `->future()`.
  Dtor (`.cpp:107-110`) calls only `stopWorkerThread()` — does **not** free the ifaces, so
  destroying mid-sync leaks the live one. The watchers `m_singleWatcher`/`m_multiWatcher`
  (`syncengine.h`, parented to `this`) are auto-freed by `~QObject` and are **out of scope**
  (they don't leak). `<memory>`, `<QFutureInterface>`, `<QFutureWatcher>` already included
  (`syncengine.h:26-29`). The dual-iface design is intentional (Qt6 `QFuture::then()` drops
  cancellation results — see the comment block at `.cpp:504-527`/`:555-571` and FINDINGS); this
  plan changes *ownership only*, not the dual-iface structure.
- **Build:** legacy `build/` workflow (no `CMakePresets.json`). Test helpers:
  `kalburator_add_blob_test` (`tests/blob/CMakeLists.txt`), `kalburator_add_sink_test`
  (`tests/sinks/CMakeLists.txt`). Universal-backend tests: `tests/sinks/tst_rawfiles_backend.cpp`,
  `tests/sinks/tst_generic_sqlite_backend.cpp`. CardDav guards: `tests/sync/tst_carddav_provider.cpp`,
  `tests/sync/tst_carddav_capability_discovery.cpp`. Engine cancellation harness (full SyncEngine
  fixture via stubs): `tests/calendar/tst_engine_cancellation.cpp`.

---

## File Structure

- **Modify (production):** `src/blob/mockblobbackend.{h,cpp}` (T1),
  `src/universal/genericsqlitebackend.{h,cpp}` (T2 + T3),
  `src/universal/rawfilesbackend.{h,cpp}` (T3), `src/sync/carddavprovider.cpp` (T4),
  `src/sync/carddavcapabilitydiscovery.{h,cpp}` (T5), `src/engine/syncengine.{h,cpp}` (T6).
- **Modify (tests):** `tests/blob/tst_mockblobbackend.cpp` (T1),
  `tests/sinks/tst_generic_sqlite_backend.cpp` (T2, T3),
  `tests/sinks/tst_rawfiles_backend.cpp` (T3), `tests/calendar/tst_engine_cancellation.cpp` (T6).
- **Modify (docs):** `docs/campaign/architectural-redress/STATUS.md`,
  `docs/campaign/architectural-redress/FINDINGS.md` (T7).
- **Create:** none — every new test slots into an existing file/fixture.

---

### Task 1: MockBlobBackend reports injected `OnLoadRecords` failure (false-green fix)

The mock injects an `OnLoadRecords` failure but only `loadRecords()` consults it; `loadRecordsOrError()`
falls through to the base default that returns `true`. Production (`SyncEngine`) reaches the backend
via `loadRecordsOrError()`, so injected fetch failures are invisible to engine tests.

**Files:** Modify `src/blob/mockblobbackend.h`, `src/blob/mockblobbackend.cpp`,
`tests/blob/tst_mockblobbackend.cpp`.

- [ ] **Step 1: Write the failing test.** In `tests/blob/tst_mockblobbackend.cpp`, add a private
  slot declaration `void loadRecordsOrError_reportsInjectedFailure();` alongside the existing slots,
  and add this implementation (model the fixture calls on the existing
  `failureInjectionOnLoadRecords` test in the same file — `makeCollection`/`makeRecord` helpers and
  the `MockBlobBackend::FailurePoint` enum already exist there):

```cpp
void TestMockBlobBackend::loadRecordsOrError_reportsInjectedFailure()
{
    MockBlobBackend b;
    b.createCollection(makeCollection(QStringLiteral("memos")));
    b.createRecord(QStringLiteral("memos"),
                   makeRecord(QStringLiteral("r-1"), QStringLiteral("x")));

    b.setFailNext(MockBlobBackend::FailurePoint::OnLoadRecords, 1);

    QList<Kalburator::Sync::BackendRecord> records;
    QString error;
    const bool ok = b.loadRecordsOrError(QStringLiteral("memos"), records, error);

    // Before the fix: ok == true, error empty, records empty — a silent false-green.
    QVERIFY(!ok);
    QVERIFY(!error.isEmpty());
    QVERIFY(records.isEmpty());

    // Failure was one-shot: the next call succeeds and returns the record.
    QVERIFY(b.loadRecordsOrError(QStringLiteral("memos"), records, error));
    QVERIFY(error.isEmpty());
    QCOMPARE(records.size(), 1);
}
```

- [ ] **Step 2: Run it to verify it fails.**

Run: `cmake --build build -j"$(nproc)" --target tst_mockblobbackend && ctest --test-dir build -R '^tst_mockblobbackend$' --output-on-failure`
Expected: the new slot FAILS at `QVERIFY(!ok)` (base default returns `true`).

- [ ] **Step 3: Add the override declaration.** In `src/blob/mockblobbackend.h`, immediately after
  the `loadRecords(...)` override (line ~46), add:

```cpp
    bool loadRecordsOrError(const QString &collectionId,
                            QList<BackendRecord> &records,
                            QString &error) override;
```

- [ ] **Step 4: Add the override definition.** In `src/blob/mockblobbackend.cpp`, immediately after
  the `loadRecords` definition (the block at lines ~48-54), add — note it consumes the failure
  exactly once and only delegates to `loadRecords()` on the success path (where no failure is
  pending, so no double-consume):

```cpp
bool MockBlobBackend::loadRecordsOrError(const QString &collectionId,
                                         QList<BackendRecord> &records,
                                         QString &error)
{
    if (consumeFailure(FailurePoint::OnLoadRecords, QStringLiteral("loadRecords"))) {
        records.clear();
        error = QStringLiteral("injected failure: loadRecords");
        return false;
    }
    records = loadRecords(collectionId);  // no failure pending → will not re-consume
    error.clear();
    return true;
}
```

- [ ] **Step 5: Run the new test + full suite.**

Run: `cmake --build build -j"$(nproc)" && ctest --test-dir build -j"$(nproc)" --output-on-failure`
Expected: `tst_mockblobbackend` PASSES (incl. the new slot and the untouched
`failureInjectionOnLoadRecords`); full suite still 133 green. (If `tst_engine_cancellation`
segfaults under `-jN`, rerun isolated `ctest --test-dir build -R '^tst_engine_cancellation$'` —
pre-existing flake per FINDINGS, not caused here.)

- [ ] **Step 6: Commit.**

```bash
git add src/blob/mockblobbackend.h src/blob/mockblobbackend.cpp tests/blob/tst_mockblobbackend.cpp
git commit -m "fix(blob): MockBlobBackend reports injected OnLoadRecords failure via loadRecordsOrError (P4.T1)"
```

---

### Task 2: `GenericSqliteBackend` surfaces DELETE/DROP failures (silent-failure fix)

`clearCollection`/`deleteCollection` are `void` and ignore `QSqlQuery::exec()` results. Change them
to `bool`, check every `exec()`, and `qWarning()` on failure — matching the existing
`createRecord`/`deleteRecord`/`updateRecord` error-check style in the same class. The methods are
concrete, non-virtual, not on any interface, with no internal override and no downstream caller
(verified), so the signature change is local and source-compatible for callers that ignore the
return.

**Files:** Modify `src/universal/genericsqlitebackend.h`, `src/universal/genericsqlitebackend.cpp`,
`tests/sinks/tst_generic_sqlite_backend.cpp`.

- [ ] **Step 1: Write the failing test.** In `tests/sinks/tst_generic_sqlite_backend.cpp`, add a
  private slot `void clearCollection_reportsFailure_whenTableMissing();` and this implementation
  (a `DELETE FROM "<missing>"` raises SQLite "no such table", so `exec()` fails — match the file's
  existing `QTemporaryDir`/ctor pattern for building the backend):

```cpp
void TestGenericSqliteBackend::clearCollection_reportsFailure_whenTableMissing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    GenericSqliteBackend be(dir.filePath(QStringLiteral("test.sqlite")));

    // No collection "ghost" was ever created, so its table does not exist;
    // DELETE FROM "ghost" must fail and clearCollection must report it.
    QVERIFY(!be.clearCollection(QStringLiteral("ghost")));

    // Sanity: clearing a real, empty collection succeeds.
    be.createCollection(Kalburator::Sync::CollectionInfo{
        QStringLiteral("real"), QStringLiteral("Real"), QStringLiteral("memo")},
        Kalburator::Shape::Shape::Any());
    QVERIFY(be.clearCollection(QStringLiteral("real")));
    QVERIFY(be.deleteCollection(QStringLiteral("real")));
}
```

  **Adapt to the real `CollectionInfo` aggregate + `createCollection` signature** by checking the
  top of the existing test file (it already constructs `CollectionInfo` and calls
  `createCollection` — copy that exact construction; the fields shown above are id/name/type and
  the shape arg matches `createCollection(const CollectionInfo&, const Shape&)`).

- [ ] **Step 2: Run it — verify it fails to COMPILE.**

Run: `cmake --build build -j"$(nproc)" --target tst_generic_sqlite_backend`
Expected: COMPILE ERROR — `QVERIFY(!be.clearCollection(...))` cannot negate a `void`. This is the
"red" (repo idiom: a test that fails to build until the API lands). Proceed to make it compile and
pass.

- [ ] **Step 3: Change the header signatures.** In `src/universal/genericsqlitebackend.h`, change:

```cpp
    void deleteCollection(const QString &collectionId);
    void clearCollection(const QString &collectionId);
```

to:

```cpp
    bool deleteCollection(const QString &collectionId);
    bool clearCollection(const QString &collectionId);
```

- [ ] **Step 4: Add `<QDebug>` include.** In `src/universal/genericsqlitebackend.cpp`, add
  `#include <QDebug>` to the include block (after `#include <QMutexLocker>`).

- [ ] **Step 5: Rewrite `clearCollection`.** Replace the whole body (`.cpp:115-122`) with:

```cpp
bool GenericSqliteBackend::clearCollection(const QString &collectionId)
{
    if (!m_open)
        return false;
    QSqlDatabase db = threadDb();
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("DELETE FROM \"%1\"").arg(tableNameFor(collectionId)))) {
        qWarning() << "GenericSqliteBackend::clearCollection: DELETE failed for"
                   << collectionId << ":" << q.lastError().text();
        return false;
    }
    return true;
}
```

- [ ] **Step 6: Rewrite `deleteCollection`.** Replace the whole body (`.cpp:102-113`) with — it
  accumulates failure across the three sub-operations so a partial failure is reported, and keeps
  removing the in-memory entry regardless so the cache doesn't diverge from intent:

```cpp
bool GenericSqliteBackend::deleteCollection(const QString &collectionId)
{
    bool ok = clearCollection(collectionId);
    QSqlDatabase db = threadDb();
    QSqlQuery q(db);
    const QString table = tableNameFor(collectionId);
    if (!q.exec(QStringLiteral("DROP TABLE IF EXISTS \"%1\"").arg(table))) {
        qWarning() << "GenericSqliteBackend::deleteCollection: DROP failed for"
                   << collectionId << ":" << q.lastError().text();
        ok = false;
    }
    q.prepare(QStringLiteral("DELETE FROM _shapes WHERE shape_key = ?"));
    q.addBindValue(collectionId);
    if (!q.exec()) {
        qWarning() << "GenericSqliteBackend::deleteCollection: _shapes cleanup failed for"
                   << collectionId << ":" << q.lastError().text();
        ok = false;
    }
    m_collections.remove(collectionId);
    return ok;
}
```

- [ ] **Step 7: Confirm no caller breaks.** Callers that ignored the `void` return compile
  unchanged against a `bool` return.

Run: `grep -rnE '\.(clearCollection|deleteCollection)\(|->(clearCollection|deleteCollection)\(' src/ tests/ | grep -iE 'sqlite|sink' ; echo "(review each: ignoring the bool is fine)"`
Expected: any hits are statement-form calls that ignore the result — no fix needed. If a caller
assigns the result to a `void`-typed context, fix it; none expected.

- [ ] **Step 8: Run the new test + full suite.**

Run: `cmake --build build -j"$(nproc)" && ctest --test-dir build -j"$(nproc)" --output-on-failure`
Expected: `tst_generic_sqlite_backend` PASSES incl. the new slot; full suite 133 green.

- [ ] **Step 9: Commit.**

```bash
git add src/universal/genericsqlitebackend.h src/universal/genericsqlitebackend.cpp tests/sinks/tst_generic_sqlite_backend.cpp
git commit -m "fix(sinks): GenericSqliteBackend clear/deleteCollection return bool + check DELETE/DROP (P4.T2)"
```

---

### Task 3: Guard `RawFilesBackend` + `GenericSqliteBackend` collection hashes with a `QMutex`

`shapeFor()`/`nativeShapes()` are read on the SyncEngine **worker thread**
(`SyncEngineWorker::dispatchSync`/`processSync`/`unifiedContinueAfterConflicts`) while
`createCollection`/`deleteCollection` write the same hashes from the owning thread. Add a dedicated
`mutable QMutex` per backend guarding `m_collections` + `m_shapeByCollection`, and lock every read
and write. The lock is **separate** from GenericSqlite's `m_connMutex` (different concern; sharing
it would couple connection setup with hash access). The design below is **deadlock-free**: no locked
region calls another method that takes the same lock (the file-I/O/SQL sub-calls touch neither
hash, and `saveManifest()` snapshots under a brief lock then writes unlocked).

**Files:** Modify `src/universal/rawfilesbackend.h`, `src/universal/rawfilesbackend.cpp`,
`src/universal/genericsqlitebackend.h`, `src/universal/genericsqlitebackend.cpp`,
`tests/sinks/tst_rawfiles_backend.cpp`, `tests/sinks/tst_generic_sqlite_backend.cpp`.

#### RawFilesBackend

- [ ] **Step 1: Declare the mutex.** In `src/universal/rawfilesbackend.h`, in the `private:`
  section right above `m_collections` (line ~89), add (and ensure `#include <QMutex>` is present in
  the header includes):

```cpp
    mutable QMutex m_collectionsMutex;  ///< guards m_collections + m_shapeByCollection
```

- [ ] **Step 2: Include the locker.** In `src/universal/rawfilesbackend.cpp`, add
  `#include <QMutexLocker>` and `#include <QMutex>` to the include block (after the existing Qt
  includes).

- [ ] **Step 3: Lock the readers.** Replace `availableCollections`, `collectionInfo`,
  `nativeShapes`, `shapeFor` bodies with locked versions:

```cpp
QList<CollectionInfo> RawFilesBackend::availableCollections()
{
    QMutexLocker lock(&m_collectionsMutex);
    return m_collections.values();
}

CollectionInfo RawFilesBackend::collectionInfo(const QString &collectionId)
{
    QMutexLocker lock(&m_collectionsMutex);
    return m_collections.value(collectionId);
}

QList<Kalburator::Shape::Shape> RawFilesBackend::nativeShapes() const
{
    QMutexLocker lock(&m_collectionsMutex);
    QList<Kalburator::Shape::Shape> out;
    for (auto it = m_shapeByCollection.constBegin();
         it != m_shapeByCollection.constEnd(); ++it) {
        if (!out.contains(it.value()))
            out.append(it.value());
    }
    return out;
}

Kalburator::Shape::Shape RawFilesBackend::shapeFor(const QString &collectionId) const
{
    QMutexLocker lock(&m_collectionsMutex);
    return m_shapeByCollection.value(collectionId, Kalburator::Shape::Shape::Any());
}
```

- [ ] **Step 4: Lock the writers — without re-entering the lock via `saveManifest`.** Replace
  `createCollection`, `deleteCollection`, the `m_collections.contains` read in `createRecord`,
  `loadManifest`, and `saveManifest`. The key move: `saveManifest()` snapshots `m_collections`
  under a short lock then writes the file unlocked, so callers may invoke it *outside* the locked
  hash region.

```cpp
QString RawFilesBackend::createCollection(const CollectionInfo &info,
                                          const Kalburator::Shape::Shape &shape)
{
    QDir dir(m_rootPath);
    if (!dir.exists() && !dir.mkpath(QLatin1String(".")))
        return {};
    bool added = false;
    {
        QMutexLocker lock(&m_collectionsMutex);
        if (!m_collections.contains(info.id)) {
            m_collections[info.id] = info;
            added = true;
        }
        m_shapeByCollection.insert(info.id, shape);
    }
    if (added)
        saveManifest();  // locks internally to snapshot; not nested with the block above
    return info.id;
}
```

```cpp
void RawFilesBackend::deleteCollection(const QString &collectionId)
{
    clearCollection(collectionId);  // filesystem only — touches neither hash
    {
        QMutexLocker lock(&m_collectionsMutex);
        m_collections.remove(collectionId);
    }
    saveManifest();
}
```

  In `createRecord`, replace the opening guard:

```cpp
    if (!m_collections.contains(collectionId))
        return {};
```

  with:

```cpp
    {
        QMutexLocker lock(&m_collectionsMutex);
        if (!m_collections.contains(collectionId))
            return {};
    }
```

  In `loadManifest`, wrap the populate loop (the `for (auto it = collections.begin()...)` block
  that writes `m_collections[ci.id] = ci;`) in a locker — read the file first (unlocked), then:

```cpp
    QMutexLocker lock(&m_collectionsMutex);
    for (auto it = collections.begin(); it != collections.end(); ++it) {
        const QJsonObject obj = it.value().toObject();
        CollectionInfo ci;
        ci.id = it.key();
        ci.name = obj.value(QStringLiteral("name")).toString();
        ci.type = obj.value(QStringLiteral("type")).toString();
        m_collections[ci.id] = ci;
    }
```

  Replace `saveManifest` (the whole `.cpp:186-200` body) with a snapshot-then-write version:

```cpp
void RawFilesBackend::saveManifest() const
{
    QHash<QString, CollectionInfo> snapshot;
    {
        QMutexLocker lock(&m_collectionsMutex);
        snapshot = m_collections;
    }
    QJsonObject collectionsObj;
    for (auto it = snapshot.begin(); it != snapshot.end(); ++it) {
        QJsonObject obj;
        obj[QStringLiteral("name")] = it.value().name;
        obj[QStringLiteral("type")] = it.value().type;
        collectionsObj[it.key()] = obj;
    }
    QJsonObject root;
    root[QStringLiteral("collections")] = collectionsObj;
    QFile f(QDir(m_rootPath).filePath(QLatin1String(kManifestName)));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}
```

#### GenericSqliteBackend

- [ ] **Step 5: Declare the mutex.** In `src/universal/genericsqlitebackend.h`, in `private:` right
  above `m_collections` (line ~86), add (`<QMutex>` arrives transitively via `<QMutexLocker>` in the
  `.cpp`; the header already declares `mutable QMutex m_connMutex`, so `<QMutex>` is in scope there):

```cpp
    mutable QMutex m_collectionsMutex;  ///< guards m_collections + m_shapeByCollection (separate from m_connMutex)
```

- [ ] **Step 6: Lock the readers.** Replace `availableCollections`, `collectionInfo`,
  `nativeShapes`, `shapeFor` bodies with locked versions (identical shape to RawFiles Step 3):

```cpp
QList<CollectionInfo> GenericSqliteBackend::availableCollections()
{
    QMutexLocker lock(&m_collectionsMutex);
    return m_collections.values();
}

CollectionInfo GenericSqliteBackend::collectionInfo(const QString &collectionId)
{
    QMutexLocker lock(&m_collectionsMutex);
    return m_collections.value(collectionId);
}

QList<Kalburator::Shape::Shape> GenericSqliteBackend::nativeShapes() const
{
    QMutexLocker lock(&m_collectionsMutex);
    QList<Kalburator::Shape::Shape> out;
    for (auto it = m_shapeByCollection.constBegin();
         it != m_shapeByCollection.constEnd(); ++it) {
        if (!out.contains(it.value()))
            out.append(it.value());
    }
    return out;
}

Kalburator::Shape::Shape GenericSqliteBackend::shapeFor(const QString &collectionId) const
{
    QMutexLocker lock(&m_collectionsMutex);
    return m_shapeByCollection.value(collectionId, Kalburator::Shape::Shape::Any());
}
```

- [ ] **Step 7: Lock the writers.** In `createCollection`, the `ensureOpen()`/`ensureTableFor()`
  calls stay **outside** the lock (they run before the hash region; `ensureOpen` takes the lock
  itself in Step 8). Wrap only the hash mutation in a short `m_collectionsMutex` block, capture a
  `needShapeRow` flag, then do the `threadDb()`/INSERT **after** the lock block closes. This is
  mandatory: `threadDb()` may take `m_connMutex` on its first-call-per-thread path, and
  `m_collectionsMutex` must never be held across it (lock-order invariant: the two mutexes are
  never held simultaneously). The semantics are preserved: the INSERT happens exactly when the
  collection was newly added (the `!contains` gate is captured by `needShapeRow`):

```cpp
QString GenericSqliteBackend::createCollection(const CollectionInfo &info,
                                               const Kalburator::Shape::Shape &shape)
{
    if (!m_open && !ensureOpen())
        return {};
    if (!ensureTableFor(info.id))
        return {};
    bool needShapeRow = false;
    {
        QMutexLocker lock(&m_collectionsMutex);
        if (!m_collections.contains(info.id)) {
            m_collections[info.id] = info;
            needShapeRow = true;
        }
        m_shapeByCollection.insert(info.id, shape);
    }
    if (needShapeRow) {
        // DB write outside the hash lock: threadDb() may take m_connMutex on first
        // use per thread, and m_collectionsMutex must never be held across it.
        QSqlDatabase db = threadDb();
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO _shapes "
            "(shape_key, shape_name, shape_type, created_at) "
            "VALUES (?, ?, ?, datetime('now'))"));
        q.addBindValue(info.id);
        q.addBindValue(info.name);
        q.addBindValue(info.type);
        q.exec();
    }
    return info.id;
}
```

  In `deleteCollection` (already rewritten in T2 Step 6), wrap the final `m_collections.remove`:
  change `m_collections.remove(collectionId);` to

```cpp
    {
        QMutexLocker lock(&m_collectionsMutex);
        m_collections.remove(collectionId);
    }
```

- [ ] **Step 8: Lock `ensureOpen`'s populate loop.** In `ensureOpen` (`.cpp:288-298`), wrap the
  `while (q.next()) {...}` populate of `m_collections` in the locker (the SQL `exec` may stay
  outside; lock only the hash writes):

```cpp
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("SELECT shape_key, shape_name, shape_type FROM _shapes"))) {
        QMutexLocker lock(&m_collectionsMutex);
        while (q.next()) {
            CollectionInfo ci;
            ci.id = q.value(0).toString();
            ci.name = q.value(1).toString();
            ci.type = q.value(2).toString();
            m_collections[ci.id] = ci;
        }
    }
```

  Note: `ensureOpen` is called from the ctor and from `createCollection` *before* its lock region,
  so this lock never nests with `createCollection`'s.

- [ ] **Step 9: Add a concurrency stress test for each backend.** These exercise the race path so
  the fix is meaningful under ThreadSanitizer (and they must not crash/deadlock without it). Add to
  `tests/sinks/tst_rawfiles_backend.cpp` a slot `void concurrentShapeForVsCreateCollection();`:

```cpp
void TestRawFilesBackend::concurrentShapeForVsCreateCollection()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RawFilesBackend be(dir.path());

    std::atomic<bool> stop{false};
    // Reader: hammer shapeFor()/nativeShapes() like the worker thread does.
    QFuture<void> reader = QtConcurrent::run([&] {
        while (!stop.load(std::memory_order_acquire)) {
            be.shapeFor(QStringLiteral("c-7"));
            be.nativeShapes();
        }
    });
    // Writer: churn collections on this thread.
    for (int i = 0; i < 200; ++i) {
        be.createCollection(Kalburator::Sync::CollectionInfo{
            QStringLiteral("c-%1").arg(i), QStringLiteral("C"), QStringLiteral("memo")},
            Kalburator::Shape::Shape::Any());
    }
    stop.store(true, std::memory_order_release);
    reader.waitForFinished();
    QVERIFY(true);  // reaching here without crash/TSan report is the assertion
}
```

  Add the same-shaped slot `concurrentShapeForVsCreateCollection()` to
  `tests/sinks/tst_generic_sqlite_backend.cpp`, constructing
  `GenericSqliteBackend be(dir.filePath(QStringLiteral("test.sqlite")));` instead. Ensure both test
  files `#include <QtConcurrent>` and `#include <atomic>`, and that the test target links
  `Qt6::Concurrent` — check `tests/sinks/CMakeLists.txt`; if `kalburator_add_sink_test` does not
  already link Concurrent, append after the two `kalburator_add_sink_test(...)` lines:

```cmake
target_link_libraries(tst_rawfiles_backend PRIVATE Qt6::Concurrent)
target_link_libraries(tst_generic_sqlite_backend PRIVATE Qt6::Concurrent)
```

  **Adapt** the `CollectionInfo` construction to the real aggregate (copy from the existing tests in
  each file, as in T2 Step 1).

- [ ] **Step 10: Build + run the sink tests + full suite.**

Run: `cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build -j"$(nproc)" && ctest --test-dir build -j"$(nproc)" --output-on-failure`
Expected: both stress tests PASS (no crash/deadlock); full suite 133 green.

- [ ] **Step 11: (Recommended) sanitizer pass.** Build a TSan variant and run the sink tests to
  confirm the race is gone:

Run: `cmake -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" && cmake --build build-tsan -j"$(nproc)" --target tst_rawfiles_backend tst_generic_sqlite_backend && ctest --test-dir build-tsan -R 'tst_(rawfiles|generic_sqlite)_backend' --output-on-failure`
Expected: no ThreadSanitizer data-race reports on `m_collections`/`m_shapeByCollection`. (If the
TSan toolchain is unavailable on this machine, note that in the commit body and rely on Step 10.)

- [ ] **Step 12: Commit.**

```bash
git add src/universal/rawfilesbackend.h src/universal/rawfilesbackend.cpp src/universal/genericsqlitebackend.h src/universal/genericsqlitebackend.cpp tests/sinks/tst_rawfiles_backend.cpp tests/sinks/tst_generic_sqlite_backend.cpp tests/sinks/CMakeLists.txt
git commit -m "fix(sinks): guard RawFiles/GenericSqlite collection hashes with QMutex (P4.T3)"
```

---

### Task 4: Replace raw `bool*` in `CardDavProvider` with `std::shared_ptr<bool>`

The `bool *errorSeen` is captured by two lambdas with undefined relative firing order; one deletes
it. A `std::shared_ptr<bool>` captured by value in both lambdas removes the manual `delete` and the
use-after-free: the flag lives until both lambdas are destroyed.

**Files:** Modify `src/sync/carddavprovider.cpp`.

- [ ] **Step 1: Replace the allocation.** In `src/sync/carddavprovider.cpp`, change (`.cpp:79`):

```cpp
    bool *errorSeen = new bool(false);
```

to:

```cpp
    auto errorSeen = std::make_shared<bool>(false);
```

- [ ] **Step 2: Keep the error lambda capturing by value.** The first lambda (`:80-84`) already
  captures `errorSeen` by value; with a `shared_ptr` that is a refcount bump. Confirm it reads:

```cpp
    QObject::connect(m_discovery, &CardDavCapabilityDiscovery::error,
                     this, [this, errorSeen](const QString &msg) {
        *errorSeen = true;
        emit error(msg);
    });
```

- [ ] **Step 3: Drop the manual delete from the finished lambda.** In the second lambda (`:92-99`),
  remove the `delete errorSeen;` line so it reads:

```cpp
    auto *watcher = new QFutureWatcher<QList<CollectionInfo>>(this);
    QObject::connect(watcher, &QFutureWatcher<QList<CollectionInfo>>::finished,
                     this, [this, watcher, errorSeen]() {
        const bool hadError = *errorSeen;
        const QList<CollectionInfo> books = watcher->result();
        watcher->deleteLater();
        onDiscoveryFinished(books, hadError);
    });
```

  (`<memory>` is already in scope via `carddavprovider.h`'s `std::unique_ptr` member; if the build
  complains, add `#include <memory>` to `carddavprovider.cpp`.)

- [ ] **Step 4: Build + run the CardDav guards + full suite.**

Run: `cmake --build build -j"$(nproc)" && ctest --test-dir build -j"$(nproc)" -R 'carddav' --output-on-failure && ctest --test-dir build -j"$(nproc)" --output-on-failure`
Expected: `tst_carddav_provider`, `tst_carddav_capability_discovery`,
`tst_carddav_engine_integration` PASS; full suite 133 green. (This fix is structural — the race
is nondeterministic, so the guard is the existing CardDav suite plus, optionally, an ASAN build:
`-DCMAKE_CXX_FLAGS="-fsanitize=address -g"` then run the carddav tests; with the raw `delete`
removed there is no heap-use-after-free to detect.)

- [ ] **Step 5: Commit.**

```bash
git add src/sync/carddavprovider.cpp
git commit -m "fix(sync): CardDavProvider error flag via shared_ptr, drop raw bool* delete (P4.T4)"
```

---

### Task 5: Convert `CardDavCapabilityDiscovery::m_promise` to `std::unique_ptr` (folded MODERATE)

The raw `QPromise*` is `new`ed once and `delete`d at four hand-maintained sites. Convert to
`std::unique_ptr` and replace each `delete X; X = nullptr;` with `X.reset()`. Behavior is identical;
the lifetime is now exception-safe and single-sited.

**Files:** Modify `src/sync/carddavcapabilitydiscovery.h`, `src/sync/carddavcapabilitydiscovery.cpp`.

- [ ] **Step 1: Change the member.** In `src/sync/carddavcapabilitydiscovery.h`, add
  `#include <QPromise>` and `#include <memory>` to the header includes, then replace (`.h:122`):

```cpp
    class QPromise<QList<CollectionInfo>> *m_promise = nullptr;
```

with:

```cpp
    std::unique_ptr<QPromise<QList<CollectionInfo>>> m_promise;
```

  (`~CardDavCapabilityDiscovery` is user-declared and defined out-of-line in the `.cpp`, where
  `QPromise` is complete, so `unique_ptr`'s requirement on a complete type at destruction is met.)

- [ ] **Step 2: Replace the allocation.** In `src/sync/carddavcapabilitydiscovery.cpp` (`.cpp:74`),
  change:

```cpp
    m_promise = new QPromise<QList<CollectionInfo>>();
    m_promise->start();
```

to:

```cpp
    m_promise = std::make_unique<QPromise<QList<CollectionInfo>>>();
    m_promise->start();
```

- [ ] **Step 3: Replace the four delete sites.** At each of the four sites — dtor (`~:41`),
  `discover()` (`~:65`), `resolveWithError()` (`~:474`), `resolveWithSuccess()` (`~:488`) — the
  pattern is:

```cpp
    if (m_promise) {
        m_promise->addResult(/* {} or books */);
        m_promise->finish();
        delete m_promise;
        m_promise = nullptr;
    }
```

  Replace the `delete m_promise; m_promise = nullptr;` pair in each with a single:

```cpp
        m_promise.reset();
```

  Leave the `addResult(...)`/`finish()` lines unchanged (`resolveWithSuccess` keeps
  `addResult(books)`; the other three keep `addResult(QList<CollectionInfo>{})`). `if (m_promise)`
  works unchanged on `unique_ptr` (explicit `operator bool`); `m_promise->` calls are unchanged.

- [ ] **Step 4: Build + run the discovery guard + full suite.**

Run: `cmake --build build -j"$(nproc)" && ctest --test-dir build -j"$(nproc)" -R 'carddav_capability' --output-on-failure && ctest --test-dir build -j"$(nproc)" --output-on-failure`
Expected: `tst_carddav_capability_discovery` PASS; full suite 133 green. Behavior is unchanged;
this is an ownership refactor.

- [ ] **Step 5: Commit.**

```bash
git add src/sync/carddavcapabilitydiscovery.h src/sync/carddavcapabilitydiscovery.cpp
git commit -m "refactor(sync): CardDavCapabilityDiscovery owns m_promise via unique_ptr (P4.T5)"
```

---

### Task 6: Own `SyncEngine`'s `QFutureInterface`s via `std::unique_ptr`

The two raw `QFutureInterface*` members are deleted only on conditional completion paths; the
destructor leaks the live one when the engine is destroyed mid-sync. Convert both to
`std::unique_ptr` so destruction frees them automatically; replace each `delete X; X = nullptr;`
with `X.reset()`; `new` with `std::make_unique`. The dual-iface design (and the watchers) are
unchanged.

**Files:** Modify `src/engine/syncengine.h`, `src/engine/syncengine.cpp`,
`tests/calendar/tst_engine_cancellation.cpp`.

- [ ] **Step 1: Change the member declarations.** In `src/engine/syncengine.h` (lines ~577-578),
  replace:

```cpp
    QFutureInterface<SyncResult>* m_currentSingleIface = nullptr;
    QFutureInterface<QList<SyncResult>>* m_currentMultiIface = nullptr;
```

with:

```cpp
    std::unique_ptr<QFutureInterface<SyncResult>> m_currentSingleIface;
    std::unique_ptr<QFutureInterface<QList<SyncResult>>> m_currentMultiIface;
```

  (`<memory>` is already included at `syncengine.h:29`.)

- [ ] **Step 2: Replace the allocations.** In `src/engine/syncengine.cpp`:
  - At `~:530`: `m_currentMultiIface = new QFutureInterface<QList<SyncResult>>;`
    → `m_currentMultiIface = std::make_unique<QFutureInterface<QList<SyncResult>>>();`
  - At `~:586`: `m_currentSingleIface = new QFutureInterface<SyncResult>;`
    → `m_currentSingleIface = std::make_unique<QFutureInterface<SyncResult>>();`
  All subsequent `m_currentXIface->reportStarted()` / `->setAddResultsIfCanceledEnabled(true)` /
  `->future()` calls are unchanged (`->` works through `unique_ptr`).

- [ ] **Step 3: Replace the eight delete sites.** At each of `~:310`, `~:355`, `~:409`, `~:464`,
  `~:843`, `~:874`, `~:1235`, `~:1274`, the code is one of:

```cpp
    delete m_currentMultiIface;  m_currentMultiIface = nullptr;
    // or
    delete m_currentSingleIface; m_currentSingleIface = nullptr;
```

  Replace each with the corresponding single statement:

```cpp
    m_currentMultiIface.reset();
    // or
    m_currentSingleIface.reset();
```

  Use grep to confirm all are converted (and to catch any not in the line list above):

  Run: `grep -nE 'delete m_current(Single|Multi)Iface|m_current(Single|Multi)Iface *= *nullptr' src/engine/syncengine.cpp`
  Expected after edits: empty.

- [ ] **Step 4: Catch any raw-pointer usage that needs `.get()`.** Grep for every remaining mention
  and confirm each is a `->` call, an `if (m_currentXIface)` test, a `make_unique` assignment, or a
  `.reset()` — none should pass the member to a function expecting a raw `QFutureInterface*`:

  Run: `grep -nE 'm_current(Single|Multi)Iface' src/engine/syncengine.cpp src/engine/syncengine.h src/engine/syncengine_p.h`
  Expected: only `->`, `if (...)`, `= std::make_unique<...>()`, `.reset()`, `.future()`. If a site
  passes the bare member where a `QFutureInterface*` is expected, append `.get()` there. (The HEAD
  audit found none.)

- [ ] **Step 5: Document the destructor.** In `src/engine/syncengine.cpp` (`~:107-110`), replace
  the destructor body with:

```cpp
SyncEngine::~SyncEngine()
{
    stopWorkerThread();
    // Architectural-redress Plan 4: the in-flight QFutureInterfaces are owned by
    // unique_ptr members and are freed automatically after this body, fixing the
    // mid-sync memory leak (AUDIT MAJOR "raw QFutureInterface* without lifecycle
    // management"). We deliberately do NOT reportFinished() here: the watchers
    // (m_singleWatcher/m_multiWatcher, parented to this) are torn down by ~QObject
    // immediately after, so emitting finished() now would re-enter the completion
    // slots during teardown. Unblocking a caller that still holds a future while its
    // engine is destroyed mid-sync is a misuse out of Plan 4's scope (see FINDINGS).
}
```

- [ ] **Step 6: Add a destroy-mid-sync crash/leak test.** In
  `tests/calendar/tst_engine_cancellation.cpp`, add a private slot declaration
  `void engineDestroyedMidSync_freesInterface();` in the `private slots:` section (near the other
  positive smoke tests), and this implementation — it reuses the existing `init()` fixture (which
  builds `m_registry`/`m_src`/`m_dst`/`m_host`/`m_engine` and a mapping), starts a single-mapping
  future via the deprecated shim (which allocates `m_currentSingleIface`), and destroys the engine
  before the future finishes:

```cpp
void TstEngineCancellation::engineDestroyedMidSync_freesInterface()
{
    // init() has already built the fixture and m_engine. Load the mapping so a
    // single-mapping run dispatches and allocates m_currentSingleIface.
    m_host->configStore()->saveMapping(makeCalendarMapping());  // adapt to the stub's seed API
    m_engine->loadSyncMappings();

    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QFuture<SyncResult> f = m_engine->runSyncFuture(QString::fromLatin1(kMappingId));
    QT_WARNING_POP

    // Destroy the engine while the future may still be in flight. With the
    // unique_ptr fix this frees m_currentSingleIface (ASAN/LSAN: no leak);
    // ~SyncEngine joins the worker via stopWorkerThread() so there is no crash.
    m_engine.reset();
    QVERIFY(true);  // no crash on teardown is the assertion; LSAN catches the leak
}
```

  **Adapt the two fixture calls** (`configStore()->saveMapping(...)` and `loadSyncMappings()`) to
  the real stub/engine API by copying how an existing slot in this file seeds a mapping and starts a
  run (e.g. `singleMappingFutureCompletes` — match its exact setup calls). The fixed intent: start a
  single-mapping future, then `m_engine.reset()` before it completes, asserting clean teardown.
  Because `cleanup()` resets `m_engine` anyway, resetting it here early is safe; guard against a
  double-run in `cleanup()` only if it assumes a live engine (it uses `unique_ptr::reset`, which is
  idempotent).

- [ ] **Step 7: Build + full suite.**

Run: `cmake --build build -j"$(nproc)" && ctest --test-dir build -j"$(nproc)" --output-on-failure`
Expected: `tst_engine_cancellation` PASSES incl. the new slot; full suite 133 green. Rerun
`tst_engine_cancellation` isolated if it segfaults under `-jN` (pre-existing flake per FINDINGS).

- [ ] **Step 8: (Recommended) LSAN pass.** Confirm the mid-sync leak is gone:

Run: `cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address -g" && cmake --build build-asan -j"$(nproc)" --target tst_engine_cancellation && ctest --test-dir build-asan -R '^tst_engine_cancellation$' --output-on-failure`
Expected: no `QFutureInterface` leak reported by LSAN. (If the ASAN toolchain is unavailable, note
it in the commit body and rely on Step 7 + the structural unique_ptr change.)

- [ ] **Step 9: Commit.**

```bash
git add src/engine/syncengine.h src/engine/syncengine.cpp tests/calendar/tst_engine_cancellation.cpp
git commit -m "fix(engine): own SyncEngine QFutureInterfaces via unique_ptr; free on destroy (P4.T6)"
```

---

### Task 7: Acceptance gate + close the plan

**Files:** Modify `docs/campaign/architectural-redress/STATUS.md`,
`docs/campaign/architectural-redress/FINDINGS.md`.

- [ ] **Step 1: Ownership/correctness acceptance greps.** Confirm the raw lifetimes are gone:

```
echo "=== no raw new bool / new QFutureInterface / new QPromise in the fixed files ==="
grep -nE 'new bool\(|new QFutureInterface|new QPromise' src/sync/carddavprovider.cpp src/sync/carddavcapabilitydiscovery.cpp src/engine/syncengine.cpp ; echo "(empty = good)"
echo "=== no leftover manual delete of the converted pointers ==="
grep -nE 'delete (errorSeen|m_promise|m_current(Single|Multi)Iface)' src/sync/carddavprovider.cpp src/sync/carddavcapabilitydiscovery.cpp src/engine/syncengine.cpp ; echo "(empty = good)"
echo "=== clear/deleteCollection now return bool ==="
grep -nE '(bool|void) (clear|delete)Collection' src/universal/genericsqlitebackend.h ; echo "(must show bool)"
echo "=== collection-hash mutex present in both universal backends ==="
grep -nE 'm_collectionsMutex' src/universal/rawfilesbackend.h src/universal/genericsqlitebackend.h ; echo "(must show one per file)"
echo "=== MockBlobBackend overrides loadRecordsOrError ==="
grep -nE 'loadRecordsOrError' src/blob/mockblobbackend.h ; echo "(must show the override)"
```
All four "(empty = good)" greps must be empty; the others must show the indicated lines.

- [ ] **Step 2: Full regression gate.**

Run: `cmake --build build -j"$(nproc)" && ctest --test-dir build --output-on-failure -j"$(nproc)"`
Expected: 133 green (no test count change — T1/T2/T3/T6 add slots to existing executables, not new
test binaries). If `tst_engine_cancellation` segfaults under `-jN`, rerun isolated to confirm the
pre-existing flake.

- [ ] **Step 3: Verify the PlanStan/WildPalms contract (INVARIANTS §10).** The only signature
  change is `GenericSqliteBackend::{clearCollection,deleteCollection}` `void`→`bool` — a concrete
  non-interface method. Reconfirm no downstream caller binds to it:

  Run: `grep -rn 'clearCollection\|deleteCollection' /home/clinton/dev/PlanStan /home/clinton/dev/WildPalms 2>/dev/null | grep -iv baselinestore`
  Expected: WildPalms hits are on `TodoBlobBackend` (its own `bool deleteCollection` override of a
  different class) — unrelated. Record "no downstream caller of GenericSqliteBackend::clear/delete"
  in FINDINGS. If a real consumer of `GenericSqliteBackend` appears, note the bool return as a
  benign source-compatible change (ignoring callers unaffected).

- [ ] **Step 4: Update STATUS + FINDINGS.** In `STATUS.md`: set the Plan 4 table row to
  `**DONE — feature/redress-4-correctness-ownership-sweep**`; set "Next action" to **Plan 5
  (`types/` purification, AUDIT B2-corrected)**; add a "Plan 4 outcome" note listing the five MAJOR
  fixes + the folded QPromise* MODERATE, and the locked scope-deviation decision (INVARIANTS §8,
  documented). Add to the "Locked decisions" ledger:

  > **2026-05-29 — Plan 4 folded the same-file `QPromise*` MODERATE into the MAJOR `bool*` fix.**
  > Fixing CardDavProvider's raw `bool*` while leaving its collaborator
  > CardDavCapabilityDiscovery's raw `QPromise*` is a half-fix of one file family. The PRAGMA
  > MODERATEs (SyncConflictStore/IDMappingStore) stay deferred. (Deviation from INVARIANTS §8,
  > documented per §"Scope and exceptions".)

  In `FINDINGS.md`: cross out (resolved) the AUDIT pointers in the Open/Baseline summary for the
  five MAJORs (raw `bool*`, raw `QFutureInterface*`, thread-unsafe RawFiles/GenericSqlite, silent
  SQLite DELETE, MockBlobBackend false-green) and the folded `QPromise*` MODERATE, each citing the
  P4 commit. Add one open line noting the PRAGMA silent-failure MODERATEs
  (`SyncConflictStore`/`IDMappingStore`) remain catalogued for a later plan.

- [ ] **Step 5: Commit.**

```bash
git add docs/campaign/architectural-redress/STATUS.md docs/campaign/architectural-redress/FINDINGS.md
git commit -m "docs(campaign): close Plan 4 — correctness/ownership sweep landed (P4.T7)"
```

Then use `superpowers:finishing-a-development-branch`.

---

## Self-Review

**1. Finding coverage (one task per MAJOR + the folded MODERATE):**
- MockBlobBackend false-green (MAJOR) → T1.
- GenericSqlite silent DELETE/DROP (MAJOR) → T2.
- RawFiles + GenericSqlite collection-hash race (MAJOR) → T3.
- CardDavProvider raw `bool*` UAF (MAJOR) → T4.
- CardDavCapabilityDiscovery raw `QPromise*` (folded MODERATE) → T5.
- SyncEngine raw `QFutureInterface*` leak (MAJOR) → T6.
- Acceptance + downstream contract + close → T7.
All five MAJORs and the one in-scope MODERATE are covered.

**2. Placeholder scan:** Concrete edits/code throughout. Three "adapt to real API" spots, each with
a *fixed intent* and a named model to copy (not a vague TODO): (a) T2/T3 `CollectionInfo`
construction — copy the existing tests in the same file; (b) T3 stress-test `QtConcurrent`/link —
verify `tests/sinks/CMakeLists.txt`; (c) T6 fixture seeding — copy `singleMappingFutureCompletes`
in the same file. These match how Plan 3's T7 handled the `RawFilesBackend`/`BackendRecord` shape.

**3. Type/signature consistency:** `loadRecordsOrError(const QString&, QList<BackendRecord>&,
QString&) -> bool` matches the base (`iblobbackend.h:55-61`) and the sibling mirror
(`mockbackend.cpp:607`). `clear/deleteCollection -> bool` is consistent between header (T2 Step 3)
and impl (T2 Steps 5-6) and the new test (T2 Step 1). `m_collectionsMutex` is the single mutex name
used in both backends (T3). `std::unique_ptr<QFutureInterface<...>>` is consistent across the
member decl (T6 S1), allocations (S2), reset sites (S3), and the dtor note (S5).
`std::make_shared<bool>` (T4) / `std::unique_ptr<QPromise<...>>` (T5) are each used consistently
within their task.

**4. Incrementality (each task leaves the tree green):** T1, T2 are self-contained red-green. T3
builds on T2's `deleteCollection` body but only wraps one line; order T2 → T3 is honored. T4, T5,
T6 are independent structural fixes touching disjoint files. No task depends on a later one to
compile.

**5. Scope:** Exactly the five MAJORs + the one same-file MODERATE the user approved. The PRAGMA
MODERATEs and all other AUDIT findings are explicitly left for later plans; the deviation from
INVARIANTS §8 is documented in the plan header and recorded in the STATUS ledger (T7 Step 4).
