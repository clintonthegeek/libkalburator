# Phase B2 — blob-layer implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land the net-new lower-layer blob sync substrate in libkalburator — `IBlobBackend`, `BackendRecord`, `CollectionInfo`, `BlobSyncEngine` (minimal), `LocalBlobBackend`, `MockBlobBackend` — plus the first library-side test tree. No churn to the calendar layer, PlanStan, or Wild Palms.

**Architecture:** Net-new files in `src/types/` (two header-only value types) and `src/blob/` (interface + engine + two concrete backends). New `tests/` tree gated by `KALBURATOR_BUILD_TESTS` (ON for top-level standalone builds, OFF for `add_subdirectory` consumers). The existing `KALBURATOR_SYNC_SUBDIRS` glob picks up `src/blob/*.{h,cpp}` automatically; `src/types/*.{h,cpp}` glob picks up the new value-type headers.

**Tech Stack:** Qt6 6.8+, CMake 3.19+, KF6CalendarCore (only needed transitively; blob layer doesn't use it), C++20 (`std::optional`, `operator== = default`). Test framework: Qt Test (`QTEST_MAIN`, `QCOMPARE`, `QVERIFY`).

**Reference:** This plan implements the design in `04h-blob-layer-design.md`. Full type signatures, rationale for each choice, and explicit deferrals are there. This plan sequences the work; when a task says "per design doc §X" it means that section is authoritative for the shape.

---

## File Structure

**Create (net-new):**

| Path | Purpose |
|---|---|
| `src/types/backendrecord.h` | Value-type struct; shared vocabulary |
| `src/types/collectioninfo.h` | Value-type struct; shared vocabulary |
| `src/blob/iblobbackend.h` | Abstract QObject interface |
| `src/blob/iblobbackend.cpp` | Ctor/dtor only (QObject requires out-of-line) |
| `src/blob/blobsyncengine.h` | Minimal engine: `mirror` + `twoWayNaive` |
| `src/blob/blobsyncengine.cpp` | Engine impl |
| `src/blob/localblobbackend.h` | Disk-backed reference impl |
| `src/blob/localblobbackend.cpp` | Reference impl |
| `src/blob/mockblobbackend.h` | In-memory + failure injection for tests |
| `src/blob/mockblobbackend.cpp` | Mock impl |
| `tests/CMakeLists.txt` | `add_subdirectory(blob)` |
| `tests/blob/CMakeLists.txt` | Three `QTEST_MAIN` executables |
| `tests/blob/tst_mockblobbackend.cpp` | Mock round-trip + failure injection |
| `tests/blob/tst_localblobbackend.cpp` | Disk round-trip; uses `QTemporaryDir` |
| `tests/blob/tst_blobsyncengine.cpp` | Engine: mirror, twoWayNaive, progress signal |

**Modify:**

| Path | Change |
|---|---|
| `CMakeLists.txt` (root) | Add `KALBURATOR_BUILD_TESTS` option + conditional `enable_testing()` / `add_subdirectory(tests)` block |
| `README.md` | Update phase-map line (add Phase B2 entry once complete) |
| `docs/phase0/04h-blob-layer-design.md` | Update **Status** line on completion |

---

## Task 1: Capture pre-phase ctest baselines

**Rationale:** Success criterion 3 is "PlanStan's ctest baseline preserved byte-for-byte." That means nothing unless we capture the baseline first. Same for WP.

**Files:** no file changes — capture output to scratch notes.

- [ ] **Step 1.1: Build PlanStan from current `main` and capture ctest output.**

```bash
cd ~/dev/PlanStan
cmake --build build-dev -j"$(nproc)" 2>&1 | tail -5
ctest --test-dir build-dev --output-on-failure 2>&1 | tee /tmp/planstan-baseline-before-b2.txt | tail -10
```

Expected: a "N% tests passed, M tests failed" line. Record that number. This is the baseline to preserve.

- [ ] **Step 1.2: Build WP and capture ctest output.**

```bash
cd ~/dev/WildPalms
cmake --build build -j"$(nproc)" 2>&1 | tail -5
ctest --test-dir build --output-on-failure 2>&1 | tee /tmp/wp-baseline-before-b2.txt | tail -10
```

Expected: `100% tests passed, 0 tests failed out of 16` (Phase D's result).

- [ ] **Step 1.3: Confirm libkalburator builds standalone on current `main`.**

```bash
cd ~/dev/libkalburator
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON 2>&1 | tail -5
cmake --build build -j"$(nproc)" 2>&1 | tail -5
```

Expected: configure + build succeed. No `ctest` yet — the library has no tests on current `main`.

---

## Task 2: Add `BackendRecord` and `CollectionInfo` value types

**Files:**
- Create: `src/types/backendrecord.h`
- Create: `src/types/collectioninfo.h`

- [ ] **Step 2.1: Write `src/types/backendrecord.h`.**

```cpp
#ifndef KALBURATOR_TYPES_BACKENDRECORD_H
#define KALBURATOR_TYPES_BACKENDRECORD_H

#include <QByteArray>
#include <QDateTime>
#include <QString>

namespace Kalburator::Sync {

/// Opaque record in a blob backend. Backends serialize/deserialize
/// their native formats to/from `data`. Lives at the library root
/// because both the blob engine and any consumer needing shared
/// vocabulary reads it.
struct BackendRecord {
    QString    id;                 ///< Backend-assigned unique id
                                   ///  (file path, CalDAV href, PalmID, …).
    QString    type;               ///< "memo", "contact", "event", "todo",
                                   ///  "binary", … — host-interpreted.
    QString    displayName;        ///< Human-readable, for UI/logs.
    QByteArray data;               ///< Opaque bytes.
    QString    contentHash;        ///< Backend-computed; algorithm is
                                   ///  backend's choice (SHA-256 for
                                   ///  LocalBlobBackend).
    QDateTime  lastModified;
    bool       isDeleted = false;

    QString description() const
    { return displayName.isEmpty() ? id : displayName; }

    bool operator==(const BackendRecord &other) const = default;
};

} // namespace Kalburator::Sync

#endif
```

- [ ] **Step 2.2: Write `src/types/collectioninfo.h`.**

```cpp
#ifndef KALBURATOR_TYPES_COLLECTIONINFO_H
#define KALBURATOR_TYPES_COLLECTIONINFO_H

#include <QString>

namespace Kalburator::Sync {

/// Lower-layer collection description. Unchanged from WP donor shape.
struct CollectionInfo {
    QString id;                ///< Unique identifier.
    QString name;              ///< Display name.
    QString path;              ///< Filesystem path if file-based (optional).
    QString type;              ///< "memos", "contacts", "calendar", "todos".
    bool    isDefault = false;

    bool operator==(const CollectionInfo &other) const = default;
};

} // namespace Kalburator::Sync

#endif
```

- [ ] **Step 2.3: Verify build still succeeds.**

```bash
cd ~/dev/libkalburator
cmake --build build -j"$(nproc)" 2>&1 | tail -10
```

Expected: build succeeds, `Built target kalburator-types` visible. The two new headers are globbed into `kalburator-types` automatically via `src/types/*.h`.

- [ ] **Step 2.4: Commit.**

```bash
git add src/types/backendrecord.h src/types/collectioninfo.h
git commit -m "Phase B2: add BackendRecord and CollectionInfo value types

Shared-vocabulary value types for the blob layer, lifted from WP's
Sync::BackendRecord / Sync::CollectionInfo with targeted
modernisation:

 - BackendRecord: was class-with-virtual-dtor, now struct with plain
   inline description() + operator== = default. No polymorphism was
   ever used in donor code.
 - CollectionInfo: unchanged in shape; adds operator== = default.

Both picked up by the existing src/types/*.h glob; no CMakeLists
edit required. Part of the Phase B2 blob-layer landing
(see docs/phase0/04h-blob-layer-design.md)."
```

---

## Task 3: Wire `KALBURATOR_BUILD_TESTS` option and empty tests scaffold

**Files:**
- Modify: `CMakeLists.txt` (root)
- Create: `tests/CMakeLists.txt`
- Create: `tests/blob/CMakeLists.txt`

- [ ] **Step 3.1: Add the option block to the root `CMakeLists.txt` after the `add_library(kalburator …)` block.**

Add the following at the bottom of `CMakeLists.txt`, after the existing `message(STATUS "libkalburator: configured …")` line:

```cmake
# -- Library tests ----------------------------------------------------------
# Gated so that add_subdirectory consumers (PlanStan, WP) don't pick up
# libkalburator's own test executables. Top-level standalone builds get
# them on by default.
if(PROJECT_IS_TOP_LEVEL)
    option(KALBURATOR_BUILD_TESTS "Build libkalburator's own tests" ON)
else()
    option(KALBURATOR_BUILD_TESTS "Build libkalburator's own tests" OFF)
endif()

if(KALBURATOR_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
```

- [ ] **Step 3.2: Create `tests/CMakeLists.txt`.**

```cmake
# libkalburator's test tree. Gated by KALBURATOR_BUILD_TESTS in the
# top-level CMakeLists.txt. Each subdirectory hosts a cluster of
# related test executables (QTEST_MAIN style).

add_subdirectory(blob)
```

- [ ] **Step 3.3: Create `tests/blob/CMakeLists.txt`** (empty placeholder with a comment; tests get added as they're written).

```cmake
# Phase B2 — blob-layer tests.
# Each test is a QTEST_MAIN executable linking Kalburator::Sync.
# Tests are appended as each implementation lands.
```

- [ ] **Step 3.4: Reconfigure and build.**

```bash
cd ~/dev/libkalburator
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON 2>&1 | tail -5
cmake --build build -j"$(nproc)" 2>&1 | tail -5
```

Expected: configure succeeds, shows `-- libkalburator: configured (org-io=OFF akonadi=OFF)` and no errors. Build succeeds. `build/CMakeCache.txt` contains `KALBURATOR_BUILD_TESTS:BOOL=ON`.

- [ ] **Step 3.5: Verify `ctest` runs (even with zero tests).**

```bash
ctest --test-dir build --output-on-failure 2>&1 | tail -5
```

Expected: `No tests were found!!!` (this is fine — empty test tree). Zero errors.

- [ ] **Step 3.6: Commit.**

```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/blob/CMakeLists.txt
git commit -m "Phase B2: add KALBURATOR_BUILD_TESTS option + empty tests scaffold

Opens the door to landing libkalburator's first library-side tests.
Option defaults ON for PROJECT_IS_TOP_LEVEL builds, OFF when
consumed via add_subdirectory (so PlanStan and WP don't pick up
library-internal tests).

Empty tests/blob/CMakeLists.txt — tests are added per implementation
as Phase B2 lands. See docs/phase0/04h-blob-layer-design.md §Tests."
```

---

## Task 4: Add `IBlobBackend` abstract interface

**Files:**
- Create: `src/blob/iblobbackend.h`
- Create: `src/blob/iblobbackend.cpp`

No test yet — can't test an abstract interface directly. Coverage comes via `MockBlobBackend` and `LocalBlobBackend` in subsequent tasks.

- [ ] **Step 4.1: Write `src/blob/iblobbackend.h`** (full signature per design §"`IBlobBackend`").

```cpp
#ifndef KALBURATOR_BLOB_IBLOBBACKEND_H
#define KALBURATOR_BLOB_IBLOBBACKEND_H

#include <optional>

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "backendrecord.h"
#include "collectioninfo.h"

namespace Kalburator::Sync {

/**
 * @brief Abstract lower-layer (blob-typed) sync backend.
 *
 * Stores and retrieves opaque `BackendRecord`s organized into
 * `CollectionInfo`-keyed collections. Host-neutral; no calendar,
 * contact, or other domain knowledge.
 *
 * The upper calendar-typed layer (`SyncBackend` etc.) is independent
 * of this interface in Phase B2. A later phase bridges the two via
 * per-backend adapters.
 */
class IBlobBackend : public QObject {
    Q_OBJECT
public:
    explicit IBlobBackend(QObject *parent = nullptr);
    ~IBlobBackend() override;

    // --- Identity ---
    virtual QString backendId() const = 0;
    virtual QString displayName() const = 0;
    virtual bool    isAvailable() const = 0;

    // --- Collections ---
    virtual QList<CollectionInfo> availableCollections() = 0;
    virtual CollectionInfo collectionInfo(const QString &collectionId) = 0;
    virtual QString createCollection(const CollectionInfo &info) = 0;

    // --- Records ---
    virtual QList<BackendRecord> loadRecords(const QString &collectionId) = 0;
    virtual std::optional<BackendRecord> loadRecord(const QString &recordId) = 0;
    virtual QString createRecord(const QString &collectionId,
                                 const BackendRecord &record) = 0;
    virtual bool    updateRecord(const BackendRecord &record) = 0;
    virtual bool    deleteRecord(const QString &recordId) = 0;

    // --- Change detection ---
    virtual QList<BackendRecord> modifiedSince(const QString &collectionId,
                                               const QDateTime &since) = 0;
    virtual QStringList deletedSince(const QString &collectionId,
                                     const QDateTime &since) = 0;
    virtual bool supportsDeleteTracking() const { return false; }

    // --- Batch / transaction ---
    virtual void beginBatch()       {}
    virtual bool commitBatch()      { return true; }
    virtual void rollbackBatch()    {}
    virtual bool supportsBatch() const { return false; }

Q_SIGNALS:
    void recordCreated(const QString &recordId);
    void recordUpdated(const QString &recordId);
    void recordDeleted(const QString &recordId);
    void errorOccurred(const QString &error);
    void progressUpdated(int current, int total, const QString &message);
};

} // namespace Kalburator::Sync

#endif
```

- [ ] **Step 4.2: Write `src/blob/iblobbackend.cpp`** (QObject ctor/dtor out-of-line so AUTOMOC generates the vtable in a TU).

```cpp
#include "iblobbackend.h"

namespace Kalburator::Sync {

IBlobBackend::IBlobBackend(QObject *parent)
    : QObject(parent)
{
}

IBlobBackend::~IBlobBackend() = default;

} // namespace Kalburator::Sync
```

- [ ] **Step 4.3: Build.**

```bash
cd ~/dev/libkalburator
cmake --build build -j"$(nproc)" 2>&1 | tail -10
```

Expected: `Built target kalburator`. The glob `src/blob/*.{h,cpp}` picks up both files automatically.

- [ ] **Step 4.4: Commit.**

```bash
git add src/blob/iblobbackend.h src/blob/iblobbackend.cpp
git commit -m "Phase B2: add IBlobBackend abstract interface

Lower-layer blob-typed sync backend interface. Ported from WP's
Sync::SyncBackend with targeted modernisation:

 - QList<BackendRecord*> -> QList<BackendRecord> (value return)
 - BackendRecord* loadRecord -> std::optional<BackendRecord>
 - Same identity / collection / CRUD / change-detection / batch
   method set and same signal set as the donor.

No consumer yet — MockBlobBackend and LocalBlobBackend (next two
tasks) are the first impls. See docs/phase0/04h-blob-layer-design.md
§IBlobBackend."
```

---

## Task 5: MockBlobBackend + its test

**Files:**
- Create: `src/blob/mockblobbackend.h`
- Create: `src/blob/mockblobbackend.cpp`
- Create: `tests/blob/tst_mockblobbackend.cpp`
- Modify: `tests/blob/CMakeLists.txt`

- [ ] **Step 5.1: Write `src/blob/mockblobbackend.h`.**

```cpp
#ifndef KALBURATOR_BLOB_MOCKBLOBBACKEND_H
#define KALBURATOR_BLOB_MOCKBLOBBACKEND_H

#include <QHash>
#include <QList>

#include "iblobbackend.h"

namespace Kalburator::Sync {

/**
 * @brief In-memory IBlobBackend for tests.
 *
 * Minimal test support — CRUD plus failure injection. Intentionally
 * does NOT include the latency injection, operation log, or
 * deterministic-mode features of the calendar-layer MockBackend;
 * those are deferred until a specific test needs them.
 */
class MockBlobBackend : public IBlobBackend {
    Q_OBJECT
public:
    enum class FailurePoint {
        None,
        OnLoadRecords,
        OnLoadRecord,
        OnCreateRecord,
        OnUpdateRecord,
        OnDeleteRecord,
        OnModifiedSince,
        OnCreateCollection,
    };

    explicit MockBlobBackend(QObject *parent = nullptr);
    ~MockBlobBackend() override;

    // IBlobBackend
    QString backendId() const override { return QStringLiteral("mock-blob"); }
    QString displayName() const override { return QStringLiteral("Mock Blob Backend"); }
    bool    isAvailable() const override { return true; }

    QList<CollectionInfo> availableCollections() override;
    CollectionInfo collectionInfo(const QString &collectionId) override;
    QString createCollection(const CollectionInfo &info) override;

    QList<BackendRecord> loadRecords(const QString &collectionId) override;
    std::optional<BackendRecord> loadRecord(const QString &recordId) override;
    QString createRecord(const QString &collectionId,
                         const BackendRecord &record) override;
    bool updateRecord(const BackendRecord &record) override;
    bool deleteRecord(const QString &recordId) override;

    QList<BackendRecord> modifiedSince(const QString &collectionId,
                                       const QDateTime &since) override;
    QStringList deletedSince(const QString &collectionId,
                             const QDateTime &since) override;
    bool supportsDeleteTracking() const override { return true; }

    // Test configuration
    void setFailNext(FailurePoint point, int count = 1);
    void clearFailures();

    // Direct store access for test assertions
    QHash<QString, BackendRecord> recordsIn(const QString &collectionId) const;

private:
    bool consumeFailure(FailurePoint point, const QString &context);

    QHash<QString, CollectionInfo> m_collections;
    QHash<QString, QHash<QString, BackendRecord>> m_records; // collectionId -> recordId -> record
    QHash<QString, QString> m_recordCollection;              // recordId -> collectionId
    QHash<QString, QStringList> m_deleted;                   // collectionId -> deleted recordIds
    QHash<FailurePoint, int> m_failures;
};

} // namespace Kalburator::Sync

#endif
```

- [ ] **Step 5.2: Write `src/blob/mockblobbackend.cpp`.**

```cpp
#include "mockblobbackend.h"

namespace Kalburator::Sync {

MockBlobBackend::MockBlobBackend(QObject *parent)
    : IBlobBackend(parent)
{
}

MockBlobBackend::~MockBlobBackend() = default;

bool MockBlobBackend::consumeFailure(FailurePoint point, const QString &context)
{
    auto it = m_failures.find(point);
    if (it == m_failures.end() || it.value() <= 0) {
        return false;
    }
    --(it.value());
    if (it.value() <= 0) {
        m_failures.erase(it);
    }
    Q_EMIT errorOccurred(QStringLiteral("injected failure: %1").arg(context));
    return true;
}

QList<CollectionInfo> MockBlobBackend::availableCollections()
{
    return m_collections.values();
}

CollectionInfo MockBlobBackend::collectionInfo(const QString &collectionId)
{
    return m_collections.value(collectionId);
}

QString MockBlobBackend::createCollection(const CollectionInfo &info)
{
    if (consumeFailure(FailurePoint::OnCreateCollection, QStringLiteral("createCollection"))) {
        return {};
    }
    if (info.id.isEmpty()) {
        return {};
    }
    m_collections.insert(info.id, info);
    return info.id;
}

QList<BackendRecord> MockBlobBackend::loadRecords(const QString &collectionId)
{
    if (consumeFailure(FailurePoint::OnLoadRecords, QStringLiteral("loadRecords"))) {
        return {};
    }
    return m_records.value(collectionId).values();
}

std::optional<BackendRecord> MockBlobBackend::loadRecord(const QString &recordId)
{
    if (consumeFailure(FailurePoint::OnLoadRecord, QStringLiteral("loadRecord"))) {
        return std::nullopt;
    }
    const QString cid = m_recordCollection.value(recordId);
    if (cid.isEmpty()) {
        return std::nullopt;
    }
    const auto &bucket = m_records.value(cid);
    auto it = bucket.constFind(recordId);
    if (it == bucket.constEnd()) {
        return std::nullopt;
    }
    return it.value();
}

QString MockBlobBackend::createRecord(const QString &collectionId,
                                      const BackendRecord &record)
{
    if (consumeFailure(FailurePoint::OnCreateRecord, QStringLiteral("createRecord"))) {
        return {};
    }
    if (record.id.isEmpty() || collectionId.isEmpty()) {
        return {};
    }
    m_records[collectionId].insert(record.id, record);
    m_recordCollection.insert(record.id, collectionId);
    Q_EMIT recordCreated(record.id);
    return record.id;
}

bool MockBlobBackend::updateRecord(const BackendRecord &record)
{
    if (consumeFailure(FailurePoint::OnUpdateRecord, QStringLiteral("updateRecord"))) {
        return false;
    }
    const QString cid = m_recordCollection.value(record.id);
    if (cid.isEmpty()) {
        return false;
    }
    m_records[cid].insert(record.id, record);
    Q_EMIT recordUpdated(record.id);
    return true;
}

bool MockBlobBackend::deleteRecord(const QString &recordId)
{
    if (consumeFailure(FailurePoint::OnDeleteRecord, QStringLiteral("deleteRecord"))) {
        return false;
    }
    const QString cid = m_recordCollection.take(recordId);
    if (cid.isEmpty()) {
        return false;
    }
    if (!m_records[cid].remove(recordId)) {
        return false;
    }
    m_deleted[cid].append(recordId);
    Q_EMIT recordDeleted(recordId);
    return true;
}

QList<BackendRecord> MockBlobBackend::modifiedSince(const QString &collectionId,
                                                    const QDateTime &since)
{
    if (consumeFailure(FailurePoint::OnModifiedSince, QStringLiteral("modifiedSince"))) {
        return {};
    }
    QList<BackendRecord> out;
    for (const auto &r : m_records.value(collectionId)) {
        if (!since.isValid() || r.lastModified >= since) {
            out.append(r);
        }
    }
    return out;
}

QStringList MockBlobBackend::deletedSince(const QString &collectionId,
                                          const QDateTime &since)
{
    Q_UNUSED(since);
    return m_deleted.value(collectionId);
}

void MockBlobBackend::setFailNext(FailurePoint point, int count)
{
    if (count <= 0 || point == FailurePoint::None) {
        return;
    }
    m_failures[point] = count;
}

void MockBlobBackend::clearFailures()
{
    m_failures.clear();
}

QHash<QString, BackendRecord> MockBlobBackend::recordsIn(const QString &collectionId) const
{
    return m_records.value(collectionId);
}

} // namespace Kalburator::Sync
```

- [ ] **Step 5.3: Write `tests/blob/tst_mockblobbackend.cpp`.**

```cpp
#include <QtTest/QtTest>
#include <QSignalSpy>

#include "mockblobbackend.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::MockBlobBackend;

namespace {

BackendRecord makeRecord(const QString &id, const QString &data)
{
    BackendRecord r;
    r.id = id;
    r.type = QStringLiteral("memo");
    r.displayName = id;
    r.data = data.toUtf8();
    r.contentHash = QStringLiteral("hash-of-%1").arg(data);
    r.lastModified = QDateTime::currentDateTimeUtc();
    return r;
}

CollectionInfo makeCollection(const QString &id)
{
    CollectionInfo c;
    c.id = id;
    c.name = id;
    c.type = QStringLiteral("memos");
    return c;
}

} // namespace

class TestMockBlobBackend : public QObject
{
    Q_OBJECT
private slots:
    void identityAndAvailability();
    void emptyBackendReportsEmpty();
    void createCollectionAndQuery();
    void recordCrudRoundTrip();
    void modifiedSinceFiltersByTime();
    void deletedSinceTracksDeletions();
    void failureInjectionOnLoadRecords();
    void failureInjectionOnCreateRecord();
    void recordCreatedSignalFires();
};

void TestMockBlobBackend::identityAndAvailability()
{
    MockBlobBackend b;
    QVERIFY(!b.backendId().isEmpty());
    QVERIFY(!b.displayName().isEmpty());
    QVERIFY(b.isAvailable());
    QVERIFY(b.supportsDeleteTracking());
    QVERIFY(!b.supportsBatch());
}

void TestMockBlobBackend::emptyBackendReportsEmpty()
{
    MockBlobBackend b;
    QVERIFY(b.availableCollections().isEmpty());
    QVERIFY(b.loadRecords(QStringLiteral("anything")).isEmpty());
    QVERIFY(!b.loadRecord(QStringLiteral("anything")).has_value());
    QVERIFY(b.modifiedSince(QStringLiteral("anything"), {}).isEmpty());
    QVERIFY(b.deletedSince(QStringLiteral("anything"), {}).isEmpty());
}

void TestMockBlobBackend::createCollectionAndQuery()
{
    MockBlobBackend b;
    QCOMPARE(b.createCollection(makeCollection(QStringLiteral("memos"))),
             QStringLiteral("memos"));
    QCOMPARE(b.availableCollections().size(), 1);
    QCOMPARE(b.collectionInfo(QStringLiteral("memos")).name,
             QStringLiteral("memos"));
}

void TestMockBlobBackend::recordCrudRoundTrip()
{
    MockBlobBackend b;
    b.createCollection(makeCollection(QStringLiteral("memos")));

    const auto rec = makeRecord(QStringLiteral("r-1"), QStringLiteral("hello"));
    QCOMPARE(b.createRecord(QStringLiteral("memos"), rec), QStringLiteral("r-1"));

    const auto loaded = b.loadRecord(QStringLiteral("r-1"));
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->data, QByteArrayLiteral("hello"));

    auto updated = rec;
    updated.data = QByteArrayLiteral("goodbye");
    updated.contentHash = QStringLiteral("hash-of-goodbye");
    QVERIFY(b.updateRecord(updated));
    QCOMPARE(b.loadRecord(QStringLiteral("r-1"))->data, QByteArrayLiteral("goodbye"));

    QVERIFY(b.deleteRecord(QStringLiteral("r-1")));
    QVERIFY(!b.loadRecord(QStringLiteral("r-1")).has_value());
}

void TestMockBlobBackend::modifiedSinceFiltersByTime()
{
    MockBlobBackend b;
    b.createCollection(makeCollection(QStringLiteral("memos")));

    auto old = makeRecord(QStringLiteral("r-old"), QStringLiteral("o"));
    old.lastModified = QDateTime::currentDateTimeUtc().addSecs(-3600);
    b.createRecord(QStringLiteral("memos"), old);

    auto fresh = makeRecord(QStringLiteral("r-new"), QStringLiteral("n"));
    fresh.lastModified = QDateTime::currentDateTimeUtc();
    b.createRecord(QStringLiteral("memos"), fresh);

    const auto since = QDateTime::currentDateTimeUtc().addSecs(-60);
    const auto filtered = b.modifiedSince(QStringLiteral("memos"), since);
    QCOMPARE(filtered.size(), 1);
    QCOMPARE(filtered.first().id, QStringLiteral("r-new"));
}

void TestMockBlobBackend::deletedSinceTracksDeletions()
{
    MockBlobBackend b;
    b.createCollection(makeCollection(QStringLiteral("memos")));
    b.createRecord(QStringLiteral("memos"), makeRecord(QStringLiteral("r-1"), QStringLiteral("x")));
    b.deleteRecord(QStringLiteral("r-1"));
    const auto deleted = b.deletedSince(QStringLiteral("memos"), {});
    QCOMPARE(deleted, QStringList{QStringLiteral("r-1")});
}

void TestMockBlobBackend::failureInjectionOnLoadRecords()
{
    MockBlobBackend b;
    b.createCollection(makeCollection(QStringLiteral("memos")));
    b.createRecord(QStringLiteral("memos"), makeRecord(QStringLiteral("r-1"), QStringLiteral("x")));

    QSignalSpy errSpy(&b, &Kalburator::Sync::IBlobBackend::errorOccurred);
    b.setFailNext(MockBlobBackend::FailurePoint::OnLoadRecords, 1);

    QVERIFY(b.loadRecords(QStringLiteral("memos")).isEmpty()); // first call fails
    QCOMPARE(errSpy.size(), 1);
    QCOMPARE(b.loadRecords(QStringLiteral("memos")).size(), 1); // second call succeeds
}

void TestMockBlobBackend::failureInjectionOnCreateRecord()
{
    MockBlobBackend b;
    b.createCollection(makeCollection(QStringLiteral("memos")));
    b.setFailNext(MockBlobBackend::FailurePoint::OnCreateRecord, 2);
    QVERIFY(b.createRecord(QStringLiteral("memos"), makeRecord(QStringLiteral("r-1"), QStringLiteral("x"))).isEmpty());
    QVERIFY(b.createRecord(QStringLiteral("memos"), makeRecord(QStringLiteral("r-2"), QStringLiteral("y"))).isEmpty());
    QCOMPARE(b.createRecord(QStringLiteral("memos"), makeRecord(QStringLiteral("r-3"), QStringLiteral("z"))),
             QStringLiteral("r-3"));
}

void TestMockBlobBackend::recordCreatedSignalFires()
{
    MockBlobBackend b;
    b.createCollection(makeCollection(QStringLiteral("memos")));
    QSignalSpy spy(&b, &Kalburator::Sync::IBlobBackend::recordCreated);
    b.createRecord(QStringLiteral("memos"), makeRecord(QStringLiteral("r-1"), QStringLiteral("x")));
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.first().first().toString(), QStringLiteral("r-1"));
}

QTEST_MAIN(TestMockBlobBackend)
#include "tst_mockblobbackend.moc"
```

- [ ] **Step 5.4: Register the test in `tests/blob/CMakeLists.txt`.**

Replace the placeholder-comment content with:

```cmake
# Phase B2 — blob-layer tests.
# Each test is a QTEST_MAIN executable linking Kalburator::Sync.

function(kalburator_add_blob_test TEST_NAME)
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt6::Core
            Qt6::Test
            Kalburator::Sync
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

kalburator_add_blob_test(tst_mockblobbackend)
```

- [ ] **Step 5.5: Configure + build + run.**

```bash
cd ~/dev/libkalburator
cmake -S . -B build 2>&1 | tail -5
cmake --build build -j"$(nproc)" 2>&1 | tail -10
ctest --test-dir build --output-on-failure 2>&1 | tail -10
```

Expected: build succeeds; `ctest` reports `100% tests passed, 0 tests failed out of 1` with `tst_mockblobbackend` passing.

- [ ] **Step 5.6: Commit.**

```bash
git add src/blob/mockblobbackend.h src/blob/mockblobbackend.cpp \
        tests/blob/tst_mockblobbackend.cpp tests/blob/CMakeLists.txt
git commit -m "Phase B2: add MockBlobBackend + tst_mockblobbackend

In-memory IBlobBackend impl for test fixtures. Deliberately minimal:
CRUD, modifiedSince/deletedSince, and failure injection via
setFailNext(FailurePoint, count). No latency injection, operation
log, or deterministic mode — deferred until a test requires them
per docs/phase0/04h-blob-layer-design.md §'Explicitly deferred'.

tst_mockblobbackend covers: identity, empty-state, collection CRUD,
record CRUD round-trip, modifiedSince time-filtering, deletedSince
tracking, failure-injection decrement semantics, and the
recordCreated signal.

This is libkalburator's first library-side test."
```

---

## Task 6: LocalBlobBackend + its test

**Files:**
- Create: `src/blob/localblobbackend.h`
- Create: `src/blob/localblobbackend.cpp`
- Create: `tests/blob/tst_localblobbackend.cpp`
- Modify: `tests/blob/CMakeLists.txt`

- [ ] **Step 6.1: Write `src/blob/localblobbackend.h`.**

```cpp
#ifndef KALBURATOR_BLOB_LOCALBLOBBACKEND_H
#define KALBURATOR_BLOB_LOCALBLOBBACKEND_H

#include <QDir>
#include <QString>

#include "iblobbackend.h"

namespace Kalburator::Sync {

/**
 * @brief Disk-backed IBlobBackend reference impl.
 *
 * Storage: <basePath>/<collectionId>/<slug-<short-hash-of-id>>.<ext>
 * where <ext> is derived from CollectionInfo::type:
 *   "memos"    → .md
 *   "contacts" → .vcf
 *   "calendar" → .ics
 *   "todos"    → .ics
 *   anything else → .bin
 *
 * contentHash: SHA-256 of data, computed on every read (no sidecar).
 * lastModified: filesystem mtime.
 * isDeleted: not tracked (file-based backend; supportsDeleteTracking
 * returns false).
 */
class LocalBlobBackend : public IBlobBackend {
    Q_OBJECT
public:
    explicit LocalBlobBackend(const QString &basePath,
                              QObject *parent = nullptr);
    ~LocalBlobBackend() override;

    QString backendId() const override { return QStringLiteral("local-blob"); }
    QString displayName() const override { return QStringLiteral("Local Blob Backend"); }
    bool    isAvailable() const override;

    QList<CollectionInfo> availableCollections() override;
    CollectionInfo collectionInfo(const QString &collectionId) override;
    QString createCollection(const CollectionInfo &info) override;

    QList<BackendRecord> loadRecords(const QString &collectionId) override;
    std::optional<BackendRecord> loadRecord(const QString &recordId) override;
    QString createRecord(const QString &collectionId,
                         const BackendRecord &record) override;
    bool updateRecord(const BackendRecord &record) override;
    bool deleteRecord(const QString &recordId) override;

    QList<BackendRecord> modifiedSince(const QString &collectionId,
                                       const QDateTime &since) override;
    QStringList deletedSince(const QString &collectionId,
                             const QDateTime &since) override;

    QString basePath() const { return m_basePath; }

private:
    QString extensionForType(const QString &type) const;
    QString filenameFor(const BackendRecord &record, const QString &type) const;
    QString pathFromRecordId(const QString &recordId) const;
    BackendRecord readFile(const QString &absolutePath, const QString &recordId) const;
    bool writeAtomic(const QString &absolutePath, const QByteArray &data);

    QString m_basePath;
    // Map: known collection type by id (populated from disk scan +
    // createCollection calls so loadRecords knows what extension to
    // expect).
    QHash<QString, QString> m_collectionTypes;
};

} // namespace Kalburator::Sync

#endif
```

- [ ] **Step 6.2: Write `src/blob/localblobbackend.cpp`.**

```cpp
#include "localblobbackend.h"

#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace Kalburator::Sync {

namespace {

QString slugify(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (QChar c : s) {
        if (c.isLetterOrNumber()) {
            out.append(c.toLower());
        } else if (c == QLatin1Char('-') || c == QLatin1Char('_')
                   || c == QLatin1Char(' ')) {
            out.append(QLatin1Char('-'));
        }
    }
    if (out.isEmpty()) {
        out = QStringLiteral("record");
    }
    return out;
}

QString shortHash(const QString &s)
{
    const QByteArray hash = QCryptographicHash::hash(s.toUtf8(),
                                                     QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex()).left(8);
}

QString sha256Hex(const QByteArray &data)
{
    const QByteArray hash = QCryptographicHash::hash(data,
                                                     QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex());
}

} // namespace

LocalBlobBackend::LocalBlobBackend(const QString &basePath, QObject *parent)
    : IBlobBackend(parent)
    , m_basePath(basePath)
{
}

LocalBlobBackend::~LocalBlobBackend() = default;

bool LocalBlobBackend::isAvailable() const
{
    QFileInfo fi(m_basePath);
    return fi.exists() && fi.isDir() && fi.isWritable();
}

QString LocalBlobBackend::extensionForType(const QString &type) const
{
    if (type == QLatin1String("memos"))    return QStringLiteral(".md");
    if (type == QLatin1String("contacts")) return QStringLiteral(".vcf");
    if (type == QLatin1String("calendar")) return QStringLiteral(".ics");
    if (type == QLatin1String("todos"))    return QStringLiteral(".ics");
    return QStringLiteral(".bin");
}

QString LocalBlobBackend::filenameFor(const BackendRecord &record,
                                      const QString &type) const
{
    const QString base = slugify(record.displayName.isEmpty()
                                 ? record.id : record.displayName);
    return QStringLiteral("%1-%2%3")
        .arg(base, shortHash(record.id), extensionForType(type));
}

QString LocalBlobBackend::pathFromRecordId(const QString &recordId) const
{
    // Record id *is* the absolute path in this backend.
    return recordId;
}

QList<CollectionInfo> LocalBlobBackend::availableCollections()
{
    QList<CollectionInfo> out;
    if (!isAvailable()) {
        return out;
    }
    QDir dir(m_basePath);
    const QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                              QDir::Name);
    for (const QString &name : subdirs) {
        CollectionInfo info;
        info.id = name;
        info.name = name;
        info.path = dir.absoluteFilePath(name);
        info.type = m_collectionTypes.value(name);
        out.append(info);
    }
    return out;
}

CollectionInfo LocalBlobBackend::collectionInfo(const QString &collectionId)
{
    CollectionInfo info;
    if (collectionId.isEmpty()) return info;
    QDir dir(m_basePath);
    if (!dir.exists(collectionId)) return info;
    info.id = collectionId;
    info.name = collectionId;
    info.path = dir.absoluteFilePath(collectionId);
    info.type = m_collectionTypes.value(collectionId);
    return info;
}

QString LocalBlobBackend::createCollection(const CollectionInfo &info)
{
    if (info.id.isEmpty()) {
        return {};
    }
    QDir base(m_basePath);
    if (!base.mkpath(info.id)) {
        Q_EMIT errorOccurred(QStringLiteral("createCollection: mkpath failed for %1").arg(info.id));
        return {};
    }
    m_collectionTypes.insert(info.id, info.type);
    return info.id;
}

BackendRecord LocalBlobBackend::readFile(const QString &absolutePath,
                                         const QString &recordId) const
{
    BackendRecord r;
    QFile f(absolutePath);
    if (!f.open(QIODevice::ReadOnly)) {
        return r;
    }
    r.data = f.readAll();
    r.id = recordId.isEmpty() ? absolutePath : recordId;
    QFileInfo fi(absolutePath);
    r.displayName = fi.completeBaseName();
    r.lastModified = fi.lastModified().toUTC();
    r.contentHash = sha256Hex(r.data);
    return r;
}

bool LocalBlobBackend::writeAtomic(const QString &absolutePath,
                                   const QByteArray &data)
{
    QSaveFile f(absolutePath);
    if (!f.open(QIODevice::WriteOnly)) {
        Q_EMIT errorOccurred(QStringLiteral("open failed: %1").arg(absolutePath));
        return false;
    }
    if (f.write(data) != data.size()) {
        Q_EMIT errorOccurred(QStringLiteral("short write: %1").arg(absolutePath));
        return false;
    }
    if (!f.commit()) {
        Q_EMIT errorOccurred(QStringLiteral("commit failed: %1").arg(absolutePath));
        return false;
    }
    return true;
}

QList<BackendRecord> LocalBlobBackend::loadRecords(const QString &collectionId)
{
    QList<BackendRecord> out;
    if (!isAvailable()) {
        return out;
    }
    const QString collDir = QDir(m_basePath).absoluteFilePath(collectionId);
    QDir dir(collDir);
    if (!dir.exists()) {
        return out;
    }
    const QStringList files = dir.entryList(QDir::Files, QDir::Name);
    for (const QString &name : files) {
        const QString abs = dir.absoluteFilePath(name);
        out.append(readFile(abs, abs));
    }
    return out;
}

std::optional<BackendRecord> LocalBlobBackend::loadRecord(const QString &recordId)
{
    const QString path = pathFromRecordId(recordId);
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        return std::nullopt;
    }
    return readFile(path, path);
}

QString LocalBlobBackend::createRecord(const QString &collectionId,
                                       const BackendRecord &record)
{
    if (!isAvailable() || collectionId.isEmpty() || record.id.isEmpty()) {
        return {};
    }
    const QString type = m_collectionTypes.value(collectionId);
    const QString collDir = QDir(m_basePath).absoluteFilePath(collectionId);
    QDir().mkpath(collDir);
    const QString absPath = QDir(collDir).absoluteFilePath(filenameFor(record, type));
    if (!writeAtomic(absPath, record.data)) {
        return {};
    }
    Q_EMIT recordCreated(absPath);
    return absPath;
}

bool LocalBlobBackend::updateRecord(const BackendRecord &record)
{
    const QString path = pathFromRecordId(record.id);
    if (!QFileInfo::exists(path)) {
        return false;
    }
    if (!writeAtomic(path, record.data)) {
        return false;
    }
    Q_EMIT recordUpdated(path);
    return true;
}

bool LocalBlobBackend::deleteRecord(const QString &recordId)
{
    const QString path = pathFromRecordId(recordId);
    if (!QFile::remove(path)) {
        return false;
    }
    Q_EMIT recordDeleted(path);
    return true;
}

QList<BackendRecord> LocalBlobBackend::modifiedSince(const QString &collectionId,
                                                     const QDateTime &since)
{
    QList<BackendRecord> out;
    const QString collDir = QDir(m_basePath).absoluteFilePath(collectionId);
    QDir dir(collDir);
    if (!dir.exists()) {
        return out;
    }
    const QStringList files = dir.entryList(QDir::Files, QDir::Name);
    for (const QString &name : files) {
        const QString abs = dir.absoluteFilePath(name);
        const QFileInfo fi(abs);
        if (!since.isValid() || fi.lastModified().toUTC() >= since) {
            out.append(readFile(abs, abs));
        }
    }
    return out;
}

QStringList LocalBlobBackend::deletedSince(const QString &collectionId,
                                           const QDateTime &since)
{
    Q_UNUSED(collectionId);
    Q_UNUSED(since);
    return {}; // file-based backend can't track deletions
}

} // namespace Kalburator::Sync
```

- [ ] **Step 6.3: Write `tests/blob/tst_localblobbackend.cpp`.**

```cpp
#include <QtTest/QtTest>
#include <QCryptographicHash>
#include <QDir>
#include <QTemporaryDir>

#include "localblobbackend.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::LocalBlobBackend;

namespace {

BackendRecord makeRecord(const QString &id, const QString &data)
{
    BackendRecord r;
    r.id = id;
    r.displayName = id;
    r.type = QStringLiteral("memo");
    r.data = data.toUtf8();
    return r;
}

CollectionInfo makeCollection(const QString &id, const QString &type)
{
    CollectionInfo c;
    c.id = id;
    c.name = id;
    c.type = type;
    return c;
}

} // namespace

class TestLocalBlobBackend : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { m_dir.setAutoRemove(true); QVERIFY(m_dir.isValid()); }

    void isAvailableTrueForExistingDir();
    void isAvailableFalseForMissingDir();
    void createCollectionMakesSubdir();
    void createRecordWritesFileWithCorrectExtension();
    void loadRecordsReadsAllFilesInCollection();
    void updateRecordRewritesFile();
    void deleteRecordRemovesFile();
    void contentHashIsSha256OfData();
    void modifiedSinceFiltersByMtime();

private:
    QTemporaryDir m_dir;
    QString base() const { return m_dir.path(); }
};

void TestLocalBlobBackend::isAvailableTrueForExistingDir()
{
    LocalBlobBackend b(base());
    QVERIFY(b.isAvailable());
}

void TestLocalBlobBackend::isAvailableFalseForMissingDir()
{
    LocalBlobBackend b(QStringLiteral("/no/such/path/kalburator/test"));
    QVERIFY(!b.isAvailable());
}

void TestLocalBlobBackend::createCollectionMakesSubdir()
{
    LocalBlobBackend b(base());
    QCOMPARE(b.createCollection(makeCollection(QStringLiteral("memos"), QStringLiteral("memos"))),
             QStringLiteral("memos"));
    QVERIFY(QDir(base() + QStringLiteral("/memos")).exists());
    QCOMPARE(b.availableCollections().size(), 1);
}

void TestLocalBlobBackend::createRecordWritesFileWithCorrectExtension()
{
    LocalBlobBackend b(base());
    b.createCollection(makeCollection(QStringLiteral("memos"), QStringLiteral("memos")));
    const QString recPath = b.createRecord(QStringLiteral("memos"),
                                           makeRecord(QStringLiteral("r-1"), QStringLiteral("hello")));
    QVERIFY(!recPath.isEmpty());
    QVERIFY(recPath.endsWith(QStringLiteral(".md")));
    QVERIFY(QFile::exists(recPath));
}

void TestLocalBlobBackend::loadRecordsReadsAllFilesInCollection()
{
    LocalBlobBackend b(base());
    b.createCollection(makeCollection(QStringLiteral("memos"), QStringLiteral("memos")));
    b.createRecord(QStringLiteral("memos"), makeRecord(QStringLiteral("r-1"), QStringLiteral("a")));
    b.createRecord(QStringLiteral("memos"), makeRecord(QStringLiteral("r-2"), QStringLiteral("b")));
    QCOMPARE(b.loadRecords(QStringLiteral("memos")).size(), 2);
}

void TestLocalBlobBackend::updateRecordRewritesFile()
{
    LocalBlobBackend b(base());
    b.createCollection(makeCollection(QStringLiteral("memos"), QStringLiteral("memos")));
    const QString recPath = b.createRecord(QStringLiteral("memos"),
                                           makeRecord(QStringLiteral("r-1"), QStringLiteral("before")));
    auto updated = makeRecord(recPath, QStringLiteral("after"));
    QVERIFY(b.updateRecord(updated));
    const auto loaded = b.loadRecord(recPath);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->data, QByteArrayLiteral("after"));
}

void TestLocalBlobBackend::deleteRecordRemovesFile()
{
    LocalBlobBackend b(base());
    b.createCollection(makeCollection(QStringLiteral("memos"), QStringLiteral("memos")));
    const QString recPath = b.createRecord(QStringLiteral("memos"),
                                           makeRecord(QStringLiteral("r-1"), QStringLiteral("x")));
    QVERIFY(QFile::exists(recPath));
    QVERIFY(b.deleteRecord(recPath));
    QVERIFY(!QFile::exists(recPath));
}

void TestLocalBlobBackend::contentHashIsSha256OfData()
{
    LocalBlobBackend b(base());
    b.createCollection(makeCollection(QStringLiteral("memos"), QStringLiteral("memos")));
    const QString recPath = b.createRecord(QStringLiteral("memos"),
                                           makeRecord(QStringLiteral("r-1"), QStringLiteral("payload")));
    const auto loaded = b.loadRecord(recPath);
    QVERIFY(loaded.has_value());
    const QByteArray expected = QCryptographicHash::hash(
        QByteArrayLiteral("payload"), QCryptographicHash::Sha256).toHex();
    QCOMPARE(loaded->contentHash, QString::fromLatin1(expected));
}

void TestLocalBlobBackend::modifiedSinceFiltersByMtime()
{
    LocalBlobBackend b(base());
    b.createCollection(makeCollection(QStringLiteral("memos"), QStringLiteral("memos")));
    const QString p1 = b.createRecord(QStringLiteral("memos"),
                                      makeRecord(QStringLiteral("r-old"), QStringLiteral("o")));
    QTest::qSleep(1100); // advance mtime resolution beyond 1s
    const QDateTime cutoff = QDateTime::currentDateTimeUtc();
    QTest::qSleep(100);
    const QString p2 = b.createRecord(QStringLiteral("memos"),
                                      makeRecord(QStringLiteral("r-new"), QStringLiteral("n")));
    Q_UNUSED(p1);
    Q_UNUSED(p2);

    const auto filtered = b.modifiedSince(QStringLiteral("memos"), cutoff);
    QCOMPARE(filtered.size(), 1);
    QVERIFY(filtered.first().data == QByteArrayLiteral("n"));
}

QTEST_MAIN(TestLocalBlobBackend)
#include "tst_localblobbackend.moc"
```

- [ ] **Step 6.4: Add the test to `tests/blob/CMakeLists.txt`.**

Append to the existing `kalburator_add_blob_test(tst_mockblobbackend)` line:

```cmake
kalburator_add_blob_test(tst_localblobbackend)
```

- [ ] **Step 6.5: Configure + build + ctest.**

```bash
cd ~/dev/libkalburator
cmake -S . -B build 2>&1 | tail -5
cmake --build build -j"$(nproc)" 2>&1 | tail -10
ctest --test-dir build --output-on-failure 2>&1 | tail -10
```

Expected: `100% tests passed, 0 tests failed out of 2`.

- [ ] **Step 6.6: Commit.**

```bash
git add src/blob/localblobbackend.h src/blob/localblobbackend.cpp \
        tests/blob/tst_localblobbackend.cpp tests/blob/CMakeLists.txt
git commit -m "Phase B2: add LocalBlobBackend + tst_localblobbackend

Disk-backed reference IBlobBackend. One collection = one
subdirectory; one record = one file under it, named
<slug>-<shorthash>.<ext> where the extension derives from the
collection's CollectionInfo::type.

Writes via QSaveFile for atomicity. Reads compute SHA-256 content
hash on demand (no sidecar). File mtime is the lastModified source.
supportsDeleteTracking=false (cannot distinguish 'deleted' from
'never existed' for file-based storage).

tst_localblobbackend uses QTemporaryDir; covers availability,
collection mkdir, record CRUD, content-hash correctness,
modifiedSince mtime filtering."
```

---

## Task 7: BlobSyncEngine + its test

**Files:**
- Create: `src/blob/blobsyncengine.h`
- Create: `src/blob/blobsyncengine.cpp`
- Create: `tests/blob/tst_blobsyncengine.cpp`
- Modify: `tests/blob/CMakeLists.txt`

- [ ] **Step 7.1: Write `src/blob/blobsyncengine.h`.**

```cpp
#ifndef KALBURATOR_BLOB_BLOBSYNCENGINE_H
#define KALBURATOR_BLOB_BLOBSYNCENGINE_H

#include <QObject>
#include <QString>

namespace Kalburator::Sync {

class IBlobBackend;

struct BlobSyncStats {
    int created   = 0;
    int updated   = 0;
    int deleted   = 0;
    int unchanged = 0;
    int errors    = 0;
};

struct BlobSyncResult {
    bool          success = true;
    QString       errorMessage;
    BlobSyncStats sourceStats;
    BlobSyncStats targetStats;
};

/**
 * @brief Minimum-viable lower-layer sync engine.
 *
 * Phase B2 scope: stateless one-way mirror and two-way naive
 * (last-write-wins-by-lastModified). No baseline, no 3-way merge,
 * no conflict-store integration — those are explicitly deferred
 * (see docs/phase0/04h-blob-layer-design.md §"Explicitly deferred").
 */
class BlobSyncEngine : public QObject {
    Q_OBJECT
public:
    explicit BlobSyncEngine(QObject *parent = nullptr);
    ~BlobSyncEngine() override;

    /// One-way: source → target. Target ends up mirroring source's
    /// `collectionId`. Records in target not present in source are
    /// deleted; records present in both with matching contentHash
    /// are left untouched.
    BlobSyncResult mirror(IBlobBackend *source,
                          IBlobBackend *target,
                          const QString &collectionId);

    /// Two-way: whichever side has the newer `lastModified` wins for
    /// records present on both sides. Records only on one side are
    /// copied to the other. Deletions are not propagated (cannot be
    /// distinguished from "never existed" without a baseline).
    BlobSyncResult twoWayNaive(IBlobBackend *a,
                               IBlobBackend *b,
                               const QString &collectionId);

Q_SIGNALS:
    void progressChanged(int current, int total, const QString &message);
    void finished(const BlobSyncResult &result);
};

} // namespace Kalburator::Sync

#endif
```

- [ ] **Step 7.2: Write `src/blob/blobsyncengine.cpp`.**

```cpp
#include "blobsyncengine.h"

#include <QHash>

#include "backendrecord.h"
#include "iblobbackend.h"

namespace Kalburator::Sync {

BlobSyncEngine::BlobSyncEngine(QObject *parent)
    : QObject(parent)
{
}

BlobSyncEngine::~BlobSyncEngine() = default;

namespace {

QHash<QString, BackendRecord> indexById(const QList<BackendRecord> &records)
{
    QHash<QString, BackendRecord> out;
    out.reserve(records.size());
    for (const auto &r : records) {
        out.insert(r.id, r);
    }
    return out;
}

} // namespace

BlobSyncResult BlobSyncEngine::mirror(IBlobBackend *source,
                                      IBlobBackend *target,
                                      const QString &collectionId)
{
    BlobSyncResult result;
    if (!source || !target) {
        result.success = false;
        result.errorMessage = QStringLiteral("mirror: null backend");
        Q_EMIT finished(result);
        return result;
    }

    const auto srcRecords = source->loadRecords(collectionId);
    const auto tgtRecords = target->loadRecords(collectionId);
    const auto tgtById = indexById(tgtRecords);

    const int total = srcRecords.size() + tgtRecords.size();
    int step = 0;
    Q_EMIT progressChanged(step, total, QStringLiteral("mirror: starting"));

    // Copy source → target (create or update).
    for (const auto &sr : srcRecords) {
        ++step;
        const auto it = tgtById.constFind(sr.id);
        if (it == tgtById.constEnd()) {
            // Record doesn't exist in target — create.
            if (target->createRecord(collectionId, sr).isEmpty()) {
                ++result.targetStats.errors;
            } else {
                ++result.targetStats.created;
            }
        } else if (it.value().contentHash != sr.contentHash) {
            // Exists but different — update.
            BackendRecord out = sr;
            out.id = it.value().id; // keep target's id
            if (!target->updateRecord(out)) {
                ++result.targetStats.errors;
            } else {
                ++result.targetStats.updated;
            }
        } else {
            ++result.targetStats.unchanged;
        }
        Q_EMIT progressChanged(step, total, QStringLiteral("mirror: copying"));
    }

    // Delete target records not in source.
    const auto srcById = indexById(srcRecords);
    for (const auto &tr : tgtRecords) {
        ++step;
        if (!srcById.contains(tr.id)) {
            if (!target->deleteRecord(tr.id)) {
                ++result.targetStats.errors;
            } else {
                ++result.targetStats.deleted;
            }
        }
        Q_EMIT progressChanged(step, total, QStringLiteral("mirror: pruning"));
    }

    result.success = (result.targetStats.errors == 0);
    Q_EMIT finished(result);
    return result;
}

BlobSyncResult BlobSyncEngine::twoWayNaive(IBlobBackend *a,
                                           IBlobBackend *b,
                                           const QString &collectionId)
{
    BlobSyncResult result;
    if (!a || !b) {
        result.success = false;
        result.errorMessage = QStringLiteral("twoWayNaive: null backend");
        Q_EMIT finished(result);
        return result;
    }

    const auto aRecords = a->loadRecords(collectionId);
    const auto bRecords = b->loadRecords(collectionId);
    const auto aById = indexById(aRecords);
    const auto bById = indexById(bRecords);

    const int total = aRecords.size() + bRecords.size();
    int step = 0;
    Q_EMIT progressChanged(step, total, QStringLiteral("twoWayNaive: starting"));

    // A → B: records only on A, or A newer than B
    for (const auto &ar : aRecords) {
        ++step;
        const auto it = bById.constFind(ar.id);
        if (it == bById.constEnd()) {
            if (b->createRecord(collectionId, ar).isEmpty()) {
                ++result.targetStats.errors;
            } else {
                ++result.targetStats.created;
            }
        } else if (ar.contentHash != it.value().contentHash
                   && ar.lastModified > it.value().lastModified) {
            BackendRecord out = ar;
            out.id = it.value().id;
            if (!b->updateRecord(out)) {
                ++result.targetStats.errors;
            } else {
                ++result.targetStats.updated;
            }
        }
        Q_EMIT progressChanged(step, total, QStringLiteral("twoWayNaive: A→B"));
    }

    // B → A: records only on B, or B newer than A
    for (const auto &br : bRecords) {
        ++step;
        const auto it = aById.constFind(br.id);
        if (it == aById.constEnd()) {
            if (a->createRecord(collectionId, br).isEmpty()) {
                ++result.sourceStats.errors;
            } else {
                ++result.sourceStats.created;
            }
        } else if (br.contentHash != it.value().contentHash
                   && br.lastModified > it.value().lastModified) {
            BackendRecord out = br;
            out.id = it.value().id;
            if (!a->updateRecord(out)) {
                ++result.sourceStats.errors;
            } else {
                ++result.sourceStats.updated;
            }
        }
        Q_EMIT progressChanged(step, total, QStringLiteral("twoWayNaive: B→A"));
    }

    result.success = (result.sourceStats.errors == 0 && result.targetStats.errors == 0);
    Q_EMIT finished(result);
    return result;
}

} // namespace Kalburator::Sync
```

- [ ] **Step 7.3: Write `tests/blob/tst_blobsyncengine.cpp`.**

```cpp
#include <QtTest/QtTest>
#include <QSignalSpy>

#include "blobsyncengine.h"
#include "iblobbackend.h"
#include "mockblobbackend.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::BlobSyncEngine;
using Kalburator::Sync::BlobSyncResult;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::MockBlobBackend;

namespace {

BackendRecord makeRecord(const QString &id, const QString &data,
                         qint64 mtimeOffsetSecs = 0)
{
    BackendRecord r;
    r.id = id;
    r.type = QStringLiteral("memo");
    r.displayName = id;
    r.data = data.toUtf8();
    r.contentHash = QStringLiteral("hash-of-%1").arg(data);
    r.lastModified = QDateTime::currentDateTimeUtc().addSecs(mtimeOffsetSecs);
    return r;
}

CollectionInfo makeCollection(const QString &id)
{
    CollectionInfo c;
    c.id = id;
    c.name = id;
    c.type = QStringLiteral("memos");
    return c;
}

void seed(MockBlobBackend &b, const QString &cid)
{
    b.createCollection(makeCollection(cid));
}

} // namespace

class TestBlobSyncEngine : public QObject
{
    Q_OBJECT
private slots:
    void mirrorCopiesSourceToEmptyTarget();
    void mirrorLeavesMatchingRecordsAlone();
    void mirrorUpdatesChangedRecords();
    void mirrorDeletesOrphansInTarget();
    void mirrorNullBackendReturnsFailure();
    void mirrorReportsSourceErrorsAsTargetErrors(); // injected failures
    void twoWayNaiveCopiesDisjoint();
    void twoWayNaiveNewerSideWins();
    void twoWayNaiveDoesNotPropagateDeletions();
    void progressSignalFiresDuringMirror();
};

void TestBlobSyncEngine::mirrorCopiesSourceToEmptyTarget()
{
    MockBlobBackend src, tgt;
    seed(src, QStringLiteral("memos"));
    seed(tgt, QStringLiteral("memos"));
    src.createRecord(QStringLiteral("memos"),
                     makeRecord(QStringLiteral("r-1"), QStringLiteral("a")));
    src.createRecord(QStringLiteral("memos"),
                     makeRecord(QStringLiteral("r-2"), QStringLiteral("b")));

    BlobSyncEngine eng;
    const auto result = eng.mirror(&src, &tgt, QStringLiteral("memos"));
    QVERIFY(result.success);
    QCOMPARE(result.targetStats.created, 2);
    QCOMPARE(tgt.recordsIn(QStringLiteral("memos")).size(), 2);
}

void TestBlobSyncEngine::mirrorLeavesMatchingRecordsAlone()
{
    MockBlobBackend src, tgt;
    seed(src, QStringLiteral("memos"));
    seed(tgt, QStringLiteral("memos"));
    const auto rec = makeRecord(QStringLiteral("r-1"), QStringLiteral("same"));
    src.createRecord(QStringLiteral("memos"), rec);
    tgt.createRecord(QStringLiteral("memos"), rec);

    BlobSyncEngine eng;
    const auto result = eng.mirror(&src, &tgt, QStringLiteral("memos"));
    QCOMPARE(result.targetStats.unchanged, 1);
    QCOMPARE(result.targetStats.updated, 0);
    QCOMPARE(result.targetStats.created, 0);
}

void TestBlobSyncEngine::mirrorUpdatesChangedRecords()
{
    MockBlobBackend src, tgt;
    seed(src, QStringLiteral("memos"));
    seed(tgt, QStringLiteral("memos"));
    src.createRecord(QStringLiteral("memos"),
                     makeRecord(QStringLiteral("r-1"), QStringLiteral("new")));
    tgt.createRecord(QStringLiteral("memos"),
                     makeRecord(QStringLiteral("r-1"), QStringLiteral("old")));

    BlobSyncEngine eng;
    const auto result = eng.mirror(&src, &tgt, QStringLiteral("memos"));
    QCOMPARE(result.targetStats.updated, 1);
    QCOMPARE(tgt.recordsIn(QStringLiteral("memos")).value(QStringLiteral("r-1")).data,
             QByteArrayLiteral("new"));
}

void TestBlobSyncEngine::mirrorDeletesOrphansInTarget()
{
    MockBlobBackend src, tgt;
    seed(src, QStringLiteral("memos"));
    seed(tgt, QStringLiteral("memos"));
    tgt.createRecord(QStringLiteral("memos"),
                     makeRecord(QStringLiteral("r-orphan"), QStringLiteral("x")));

    BlobSyncEngine eng;
    const auto result = eng.mirror(&src, &tgt, QStringLiteral("memos"));
    QCOMPARE(result.targetStats.deleted, 1);
    QVERIFY(tgt.recordsIn(QStringLiteral("memos")).isEmpty());
}

void TestBlobSyncEngine::mirrorNullBackendReturnsFailure()
{
    BlobSyncEngine eng;
    MockBlobBackend tgt;
    const auto result = eng.mirror(nullptr, &tgt, QStringLiteral("memos"));
    QVERIFY(!result.success);
    QVERIFY(!result.errorMessage.isEmpty());
}

void TestBlobSyncEngine::mirrorReportsSourceErrorsAsTargetErrors()
{
    MockBlobBackend src, tgt;
    seed(src, QStringLiteral("memos"));
    seed(tgt, QStringLiteral("memos"));
    src.createRecord(QStringLiteral("memos"),
                     makeRecord(QStringLiteral("r-1"), QStringLiteral("a")));
    tgt.setFailNext(MockBlobBackend::FailurePoint::OnCreateRecord, 1);

    BlobSyncEngine eng;
    const auto result = eng.mirror(&src, &tgt, QStringLiteral("memos"));
    QVERIFY(!result.success);
    QCOMPARE(result.targetStats.errors, 1);
}

void TestBlobSyncEngine::twoWayNaiveCopiesDisjoint()
{
    MockBlobBackend a, b;
    seed(a, QStringLiteral("memos"));
    seed(b, QStringLiteral("memos"));
    a.createRecord(QStringLiteral("memos"),
                   makeRecord(QStringLiteral("only-a"), QStringLiteral("x")));
    b.createRecord(QStringLiteral("memos"),
                   makeRecord(QStringLiteral("only-b"), QStringLiteral("y")));

    BlobSyncEngine eng;
    const auto result = eng.twoWayNaive(&a, &b, QStringLiteral("memos"));
    QVERIFY(result.success);
    QCOMPARE(a.recordsIn(QStringLiteral("memos")).size(), 2);
    QCOMPARE(b.recordsIn(QStringLiteral("memos")).size(), 2);
}

void TestBlobSyncEngine::twoWayNaiveNewerSideWins()
{
    MockBlobBackend a, b;
    seed(a, QStringLiteral("memos"));
    seed(b, QStringLiteral("memos"));
    a.createRecord(QStringLiteral("memos"),
                   makeRecord(QStringLiteral("r-1"), QStringLiteral("old"), -3600));
    b.createRecord(QStringLiteral("memos"),
                   makeRecord(QStringLiteral("r-1"), QStringLiteral("new"), 0));

    BlobSyncEngine eng;
    const auto result = eng.twoWayNaive(&a, &b, QStringLiteral("memos"));
    QVERIFY(result.success);
    QCOMPARE(a.recordsIn(QStringLiteral("memos")).value(QStringLiteral("r-1")).data,
             QByteArrayLiteral("new"));
}

void TestBlobSyncEngine::twoWayNaiveDoesNotPropagateDeletions()
{
    // A has r-1, B does not. twoWayNaive copies r-1 to B. It does NOT
    // infer that "B doesn't have r-1" means "B deleted r-1".
    MockBlobBackend a, b;
    seed(a, QStringLiteral("memos"));
    seed(b, QStringLiteral("memos"));
    a.createRecord(QStringLiteral("memos"),
                   makeRecord(QStringLiteral("r-1"), QStringLiteral("x")));

    BlobSyncEngine eng;
    eng.twoWayNaive(&a, &b, QStringLiteral("memos"));
    QVERIFY(a.recordsIn(QStringLiteral("memos")).contains(QStringLiteral("r-1")));
    QVERIFY(b.recordsIn(QStringLiteral("memos")).contains(QStringLiteral("r-1")));
}

void TestBlobSyncEngine::progressSignalFiresDuringMirror()
{
    MockBlobBackend src, tgt;
    seed(src, QStringLiteral("memos"));
    seed(tgt, QStringLiteral("memos"));
    src.createRecord(QStringLiteral("memos"),
                     makeRecord(QStringLiteral("r-1"), QStringLiteral("x")));

    BlobSyncEngine eng;
    QSignalSpy spy(&eng, &BlobSyncEngine::progressChanged);
    eng.mirror(&src, &tgt, QStringLiteral("memos"));
    QVERIFY(spy.size() >= 1);
}

QTEST_MAIN(TestBlobSyncEngine)
#include "tst_blobsyncengine.moc"
```

- [ ] **Step 7.4: Add the test to `tests/blob/CMakeLists.txt`.**

```cmake
kalburator_add_blob_test(tst_blobsyncengine)
```

- [ ] **Step 7.5: Configure + build + ctest.**

```bash
cd ~/dev/libkalburator
cmake -S . -B build 2>&1 | tail -5
cmake --build build -j"$(nproc)" 2>&1 | tail -10
ctest --test-dir build --output-on-failure 2>&1 | tail -10
```

Expected: `100% tests passed, 0 tests failed out of 3`.

- [ ] **Step 7.6: Commit.**

```bash
git add src/blob/blobsyncengine.h src/blob/blobsyncengine.cpp \
        tests/blob/tst_blobsyncengine.cpp tests/blob/CMakeLists.txt
git commit -m "Phase B2: add BlobSyncEngine + tst_blobsyncengine

Minimum-viable lower-layer sync engine: mirror() (one-way) and
twoWayNaive() (last-write-wins-by-mtime, no baseline, no deletion
propagation).

mirror()'s algorithm:
 - Pass 1: for each source record, create/update/skip on target
   based on presence and contentHash match.
 - Pass 2: delete target records not present in source.

twoWayNaive()'s algorithm:
 - Pass 1: A -> B, copying A-only and A-newer-than-B records.
 - Pass 2: B -> A, symmetric.
 - Deletions intentionally not propagated (see
   docs/phase0/04h-blob-layer-design.md §'Explicitly deferred' —
   requires BlobBaselineStore, future phase).

Both methods emit progressChanged + finished(BlobSyncResult).
Stats recorded in BlobSyncStats {created, updated, deleted,
unchanged, errors}."
```

---

## Task 8: Update phase status docs

**Files:**
- Modify: `docs/phase0/04h-blob-layer-design.md` — update **Status** line.
- Modify: `docs/phase0/README.md` — update the phase index.

- [ ] **Step 8.1: Update the Status line in the design doc.**

Change the first content line:

```
**Status:** Design approved 2026-04-21. Not yet implemented. First
```

to:

```
**Status:** ✅ Implemented 2026-04-21 (v0.6-phase-b2-blob-layer).
All success criteria met. First
```

- [ ] **Step 8.2: Update the phase index README.**

Open `docs/phase0/README.md` and find the phase table. Add a row for Phase B2 (or update the equivalent "Phase 4" row if it exists already) pointing at `04h-blob-layer-design.md` and stating "done".

- [ ] **Step 8.3: Commit.**

```bash
git add docs/phase0/04h-blob-layer-design.md docs/phase0/README.md
git commit -m "Phase B2: mark blob layer as done in phase index

Status line updated to 'implemented'. See 04h-blob-layer-design.md
for the full design record and its 'Explicitly deferred' section
for what bookend-phase work remains."
```

---

## Task 9: Cross-repo verification

**Files:** no file changes — only runs builds and records results.

- [ ] **Step 9.1: Rebuild PlanStan against this libkalburator `main` and compare ctest output.**

```bash
cd ~/dev/PlanStan
cmake --build build-dev -j"$(nproc)" 2>&1 | tail -5
ctest --test-dir build-dev --output-on-failure 2>&1 | tee /tmp/planstan-after-b2.txt | tail -10
diff /tmp/planstan-baseline-before-b2.txt /tmp/planstan-after-b2.txt || true
```

Expected: pass/fail counts identical to Task 1's capture. Any diff in test names or counts is a regression to investigate before tagging.

- [ ] **Step 9.2: Rebuild WP against this libkalburator `main` and compare ctest output.**

```bash
cd ~/dev/WildPalms
cmake --build build -j"$(nproc)" 2>&1 | tail -5
ctest --test-dir build --output-on-failure 2>&1 | tee /tmp/wp-after-b2.txt | tail -10
diff /tmp/wp-baseline-before-b2.txt /tmp/wp-after-b2.txt || true
```

Expected: `100% tests passed, 0 tests failed out of 16`. No diff from baseline.

- [ ] **Step 9.3: Final libkalburator standalone verification.**

```bash
cd ~/dev/libkalburator
cmake --build build -j"$(nproc)" 2>&1 | tail -5
ctest --test-dir build --output-on-failure 2>&1 | tail -10
```

Expected: 3 tests, 100% pass.

If any of 9.1 / 9.2 / 9.3 shows unexpected results, STOP and investigate. Do not tag until all three are clean.

---

## Task 10: Tag `v0.6-phase-b2-blob-layer`

**Files:** tag only — no file changes.

- [ ] **Step 10.1: Verify `git status` is clean.**

```bash
cd ~/dev/libkalburator
git status
```

Expected: `nothing to commit, working tree clean`.

- [ ] **Step 10.2: Tag and verify.**

```bash
git tag -a v0.6-phase-b2-blob-layer -m "Phase B2: net-new blob layer

First Wild-Palms-driven contribution to libkalburator. Lands the
lower-layer blob sync substrate described in
04-merged-interface-sketch.md as a sibling to (not replacement of)
the existing calendar-typed layer.

Delivered:
 - IBlobBackend + BackendRecord + CollectionInfo (types + interface)
 - BlobSyncEngine with mirror() + twoWayNaive()
 - LocalBlobBackend (disk-backed reference impl)
 - MockBlobBackend (in-memory + failure injection)
 - First library-side tests: tests/blob/tst_{mock,local,engine}.

Calendar layer untouched. PlanStan ctest baseline preserved
byte-for-byte. Wild Palms ctest 16/16 preserved.

Deferred work cataloged in docs/phase0/04h-blob-layer-design.md
§'Explicitly deferred' — most notably BlobBaselineStore,
ConflictStore integration in the engine, and the calendar-layer
refactor that composes this engine."
git tag -l v0.6-phase-b2-blob-layer
```

Expected: tag appears in `git tag -l` output.

---

## Self-review

**Spec coverage:**

| Design section | Task(s) covering it |
|---|---|
| §Types — `BackendRecord`, `CollectionInfo` | Task 2 |
| §`IBlobBackend` | Task 4 |
| §`BlobSyncEngine` | Task 7 |
| §`LocalBlobBackend` | Task 6 |
| §`MockBlobBackend` | Task 5 |
| §Tests (mock, local, engine) | Tasks 5, 6, 7 |
| §CMake wiring (`KALBURATOR_BUILD_TESTS`) | Task 3 |
| §Success criterion 1 (standalone build + ctest) | Task 9.3 |
| §Success criterion 2 (≥ 3 test exes) | Tasks 5–7 land 3 exes |
| §Success criterion 3 (PlanStan preserved) | Tasks 1 + 9.1 |
| §Success criterion 4 (WP preserved) | Tasks 1 + 9.2 |
| §Success criterion 5 (status docs updated) | Task 8 |
| §Tagging (`v0.6-phase-b2-blob-layer`) | Task 10 |

All design sections and success criteria map to at least one task. No gaps.

**Placeholder scan:** no `TBD` / `TODO` / "add appropriate" / "similar to Task N" — complete code blocks or exact commands everywhere.

**Type consistency:** `BackendRecord`, `CollectionInfo`, `IBlobBackend`, `MockBlobBackend::FailurePoint`, `BlobSyncEngine::mirror`/`twoWayNaive`, `BlobSyncResult`, `BlobSyncStats` — all names match between declaration (Task 2 / 4 / 5 / 7) and usage (tests in 5 / 6 / 7).

**Filenames consistency:** `iblobbackend`, `blobsyncengine`, `localblobbackend`, `mockblobbackend` — kebab-free, matching the existing libkalburator convention. Test filenames `tst_*` match the existing PlanStan / libkalburator pattern.
