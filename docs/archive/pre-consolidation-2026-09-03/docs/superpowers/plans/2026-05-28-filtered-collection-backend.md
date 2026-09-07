# `FilteredCollectionBackend` + `RecordFilter` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land the WildPalms-requested generic primitive — `Kalburator::Shape::RecordFilter` + `Kalburator::Sinks::FilteredCollectionBackend` — that exposes a *sliced* view of an existing backend collection as if it were its own collection on its own backend. Used by WildPalms for category-routing (one remote per category) and available to PlanStan for any property-based slicing.

**Architecture:**
- `RecordFilter` is a plain value type (`property`, `op` ∈ {Contains, Equals}, `value`) with a `matches(QJsonDocument)` predicate that operates on canon-JSON records.
- `FilteredCollectionBackend` inherits `Kalburator::Sync::SyncBackend` (the calendar-typed base — same as `RawFilesBackend`), so it is registrable through `BackendRegistry::registerBackendInstance` and the engine can drive sync to/from it transparently. It *borrows* a parent backend pointer + collection id and projects exactly one virtual collection.
- Reads filter through `matches()`. Writes stamp the filter property: `Contains` is additive (append if absent, preserve order); `Equals` is filter-authoritative (always overwrite to the filter value). Deletes delegate. `discoveredWritable` delegates so v0.57 authority enforcement applies transparently.
- Lifetime: the FCB connects to a passed-in `BackendRegistry::backendInstanceUnregistered(QString)` signal; on receipt for the parent's id, the FCB nulls its parent pointer and `isAvailable()` returns false, so subsequent reads/writes return clean failure values rather than crashing.

**Tech Stack:** Qt6 (QObject, QJsonDocument, QJsonObject, QJsonArray, QVariant, QUrl), `Kalburator::Shape::PropertyId`, `Kalburator::Sync::SyncBackend`/`BackendRegistry`, QtTest. Built into the existing `Kalburator::Sync` library; tests live alongside the other universal-sink tests.

**Spec source:** `~/dev/WildPalms/docs/2026-05-28-libkalburator-filteredcollectionbackend-proposal.md`. Where the spec leaves a constructor detail open (registry pointer for the unregister hook), this plan resolves it by adding an optional `Sync::BackendRegistry*` parameter — see Task 7.

---

## File structure

**New files:**
- `src/shape/recordfilter.h` — `RecordFilter` struct + `matches()` declaration. Header-only-ish struct; `matches()` implemented in the .cpp because it parses JSON and does ops-specific logic.
- `src/shape/recordfilter.cpp` — `matches()` implementation.
- `src/universal/filteredcollectionbackend.h` — `FilteredCollectionBackend` class declaration.
- `src/universal/filteredcollectionbackend.cpp` — implementation.
- `tests/sinks/tst_filtered_collection_backend.cpp` — QtTest suite covering filter semantics, stamping semantics, delegation, lifetime, and `resourceId` stability.

**Modified files:**
- `CMakeLists.txt` — add the new source files into `KALBURATOR_SHAPE_*` and `KALBURATOR_SINKS_*` lists (lines around 410–435 — see Task 8 for exact lines).
- `tests/sinks/CMakeLists.txt` — register the new test via `kalburator_add_sink_test(tst_filtered_collection_backend)`.

---

## Conventions to follow

- `#pragma once` (matches `rawfilesbackend.h`, `propertycatalogue.h`, etc.).
- Namespaces: `Kalburator::Shape` for `RecordFilter`; `Kalburator::Sinks` for `FilteredCollectionBackend` (matches `RawFilesBackend` placement).
- Fully-qualify `Kalburator::Sync::SyncBackend`, `Kalburator::Sync::CollectionInfo`, `Kalburator::Sync::BackendRecord`, `Kalburator::Shape::Shape` in headers; `using` aliases in `.cpp` files only — matches the existing `src/universal/` style.
- Tests are QtTest single-binary classes named `TestFilteredCollectionBackend` with `private slots:` methods named `<feature>_<condition>` — matches `tst_rawfiles_backend.cpp`.
- After each Task that touches code, run **just the new test binary** (`ctest -R tst_filtered_collection_backend --output-on-failure`) plus a `cmake --build build -j` to confirm the library still compiles. The full ctest suite runs once at the end (Task 8).

---

## Task 1: `RecordFilter` struct + `matches()` semantics

**Files:**
- Create: `src/shape/recordfilter.h`
- Create: `src/shape/recordfilter.cpp`
- Test: `tests/sinks/tst_filtered_collection_backend.cpp` (file created in this task; will be extended by later tasks)

- [ ] **Step 1: Write `src/shape/recordfilter.h`**

```cpp
#pragma once

#include <QJsonDocument>
#include <QVariant>

#include "propertycatalogue.h"  // PropertyId

namespace Kalburator::Shape {

/// Predicate over a canon-JSON record. One filter = one property + one op.
///
/// The set of ops is deliberately tiny (Contains/Equals). The intent is that
/// new ops are added one at a time with a concrete use case rather than
/// shipping a query language — see the v0.58 RFC.
///
/// `Contains`: the property at `property` is expected to be a JSON array;
///             `matches()` returns true iff the array contains a value
///             semantically equal to `value`. String comparison is
///             case-sensitive (canonical for category routes).
/// `Equals`:   `matches()` returns true iff the property's JSON value is
///             semantically equal to `value`.
///
/// Missing property, type mismatch, or unparseable bytes => false. No
/// exceptions thrown.
struct RecordFilter {
    enum class Op { Contains, Equals };

    PropertyId property;
    Op         op = Op::Contains;
    QVariant   value;

    /// Evaluate against a parsed canon-JSON document. The document's root
    /// must be a JSON object (the canon envelope); anything else => false.
    bool matches(const QJsonDocument& canonRecord) const;

    /// Convenience: parse the bytes (must be a JSON object) and evaluate.
    /// Failure to parse => false.
    bool matches(const QByteArray& canonRecordBytes) const;
};

}  // namespace Kalburator::Shape
```

- [ ] **Step 2: Write `src/shape/recordfilter.cpp`**

```cpp
#include "recordfilter.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace Kalburator::Shape {

bool RecordFilter::matches(const QByteArray& canonRecordBytes) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(canonRecordBytes);
    if (doc.isNull() || !doc.isObject())
        return false;
    return matches(doc);
}

bool RecordFilter::matches(const QJsonDocument& canonRecord) const
{
    if (!canonRecord.isObject())
        return false;
    const QJsonObject obj = canonRecord.object();
    const QString key = property.toString();
    if (key.isEmpty() || !obj.contains(key))
        return false;

    const QJsonValue filterValue = QJsonValue::fromVariant(value);
    const QJsonValue field = obj.value(key);

    switch (op) {
    case Op::Contains: {
        if (!field.isArray())
            return false;
        const QJsonArray arr = field.toArray();
        for (const QJsonValue& v : arr) {
            // QJsonValue::operator== is semantic (key-order-independent for
            // objects, element-wise for arrays). Case-sensitive for strings.
            if (v == filterValue)
                return true;
        }
        return false;
    }
    case Op::Equals:
        return field == filterValue;
    }
    return false;
}

}  // namespace Kalburator::Shape
```

- [ ] **Step 3: Add the test file scaffold + RecordFilter tests at `tests/sinks/tst_filtered_collection_backend.cpp`**

Create the file with the following content (later tasks will add slots and `QTEST_MAIN` stays at the bottom):

```cpp
/// Tests for RecordFilter + FilteredCollectionBackend
/// (consumer RFC 2026-05-28).

#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "recordfilter.h"

using Kalburator::Shape::PropertyId;
using Kalburator::Shape::RecordFilter;

namespace {

QByteArray canonJson(const QJsonObject& obj)
{
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QJsonObject withCategories(QStringList cats)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("uid"), QStringLiteral("u1"));
    QJsonArray arr;
    for (const auto& c : cats) arr.append(c);
    obj.insert(QStringLiteral("categories"), arr);
    return obj;
}

} // namespace

class TestFilteredCollectionBackend : public QObject
{
    Q_OBJECT
private slots:
    // ---- RecordFilter (Task 1) -------------------------------------------
    void filter_contains_matchingArrayElement_returnsTrue();
    void filter_contains_nonMatchingArray_returnsFalse();
    void filter_contains_caseSensitive_doesNotMatchDifferentCase();
    void filter_contains_propertyAbsent_returnsFalse();
    void filter_contains_propertyNotAnArray_returnsFalse();
    void filter_equals_matchingScalar_returnsTrue();
    void filter_equals_nonMatching_returnsFalse();
    void filter_unparseableBytes_returnsFalse();
    void filter_emptyPropertyId_returnsFalse();
};

void TestFilteredCollectionBackend::filter_contains_matchingArrayElement_returnsTrue()
{
    RecordFilter f{ PropertyId{"categories"}, RecordFilter::Op::Contains,
                    QStringLiteral("Work") };
    const QJsonObject rec = withCategories({"Personal", "Work"});
    QVERIFY(f.matches(canonJson(rec)));
}

void TestFilteredCollectionBackend::filter_contains_nonMatchingArray_returnsFalse()
{
    RecordFilter f{ PropertyId{"categories"}, RecordFilter::Op::Contains,
                    QStringLiteral("Work") };
    const QJsonObject rec = withCategories({"Personal", "Family"});
    QVERIFY(!f.matches(canonJson(rec)));
}

void TestFilteredCollectionBackend::filter_contains_caseSensitive_doesNotMatchDifferentCase()
{
    RecordFilter f{ PropertyId{"categories"}, RecordFilter::Op::Contains,
                    QStringLiteral("Work") };
    const QJsonObject rec = withCategories({"work"});
    QVERIFY(!f.matches(canonJson(rec)));
}

void TestFilteredCollectionBackend::filter_contains_propertyAbsent_returnsFalse()
{
    RecordFilter f{ PropertyId{"categories"}, RecordFilter::Op::Contains,
                    QStringLiteral("Work") };
    QJsonObject rec;
    rec.insert(QStringLiteral("uid"), QStringLiteral("u1"));
    QVERIFY(!f.matches(canonJson(rec)));
}

void TestFilteredCollectionBackend::filter_contains_propertyNotAnArray_returnsFalse()
{
    RecordFilter f{ PropertyId{"categories"}, RecordFilter::Op::Contains,
                    QStringLiteral("Work") };
    QJsonObject rec;
    rec.insert(QStringLiteral("uid"), QStringLiteral("u1"));
    rec.insert(QStringLiteral("categories"), QStringLiteral("Work"));  // scalar
    QVERIFY(!f.matches(canonJson(rec)));
}

void TestFilteredCollectionBackend::filter_equals_matchingScalar_returnsTrue()
{
    RecordFilter f{ PropertyId{"status"}, RecordFilter::Op::Equals,
                    QStringLiteral("Done") };
    QJsonObject rec;
    rec.insert(QStringLiteral("uid"), QStringLiteral("u1"));
    rec.insert(QStringLiteral("status"), QStringLiteral("Done"));
    QVERIFY(f.matches(canonJson(rec)));
}

void TestFilteredCollectionBackend::filter_equals_nonMatching_returnsFalse()
{
    RecordFilter f{ PropertyId{"status"}, RecordFilter::Op::Equals,
                    QStringLiteral("Done") };
    QJsonObject rec;
    rec.insert(QStringLiteral("uid"), QStringLiteral("u1"));
    rec.insert(QStringLiteral("status"), QStringLiteral("InProgress"));
    QVERIFY(!f.matches(canonJson(rec)));
}

void TestFilteredCollectionBackend::filter_unparseableBytes_returnsFalse()
{
    RecordFilter f{ PropertyId{"categories"}, RecordFilter::Op::Contains,
                    QStringLiteral("Work") };
    QVERIFY(!f.matches(QByteArray("not json at all")));
    QVERIFY(!f.matches(QByteArray()));
}

void TestFilteredCollectionBackend::filter_emptyPropertyId_returnsFalse()
{
    RecordFilter f{ PropertyId{}, RecordFilter::Op::Contains,
                    QStringLiteral("Work") };
    QVERIFY(!f.matches(canonJson(withCategories({"Work"}))));
}

QTEST_MAIN(TestFilteredCollectionBackend)
#include "tst_filtered_collection_backend.moc"
```

- [ ] **Step 4: Wire CMakeLists.txt so this builds even though no FCB exists yet**

Edit `CMakeLists.txt`. Find the block at line ~395 starting with `set(KALBURATOR_SHAPE_SOURCES` (or whichever names the shape lib). Run this to locate first:

```bash
grep -n 'recordfilter\|KALBURATOR_SHAPE_SOURCES\|KALBURATOR_SHAPE_HEADERS' CMakeLists.txt
```

If shape sources are wired into a `KALBURATOR_SHAPE_*` list, append the two new entries there. If shape sources are listed inline in the library `target_sources` block, append them inline alongside `propertycatalogue.{h,cpp}`. Show the exact edit using:

```bash
grep -n 'propertycatalogue.cpp\|propertycatalogue.h' CMakeLists.txt
```

Add (header line first, source line second, immediately after the matching `propertycatalogue` lines):

```cmake
    src/shape/recordfilter.h
    src/shape/recordfilter.cpp
```

Then edit `tests/sinks/CMakeLists.txt` and append at the end (after the existing `kalburator_add_sink_test(tst_markdownfiles_backend)` line):

```cmake
kalburator_add_sink_test(tst_filtered_collection_backend)
```

- [ ] **Step 5: Build + run the new test**

```bash
cmake --build build -j 2>&1 | tail -20
ctest --test-dir build -R tst_filtered_collection_backend --output-on-failure
```

Expected: build succeeds; test binary runs; **all 9 RecordFilter slots PASS**.

- [ ] **Step 6: Commit**

```bash
git add src/shape/recordfilter.h src/shape/recordfilter.cpp \
        tests/sinks/tst_filtered_collection_backend.cpp \
        tests/sinks/CMakeLists.txt CMakeLists.txt
git commit -m "feat(shape): RecordFilter (Contains/Equals over canon JSON)

Per the WildPalms v0.58 RFC. Pure predicate value type; matches()
fails closed (returns false) on missing property, type mismatch,
or unparseable bytes. Contains semantics are array membership with
case-sensitive string compare; Equals is direct value equality.

Tests at tests/sinks/tst_filtered_collection_backend.cpp cover
all match/no-match axes called out in the RFC's test list (§4).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: `FilteredCollectionBackend` skeleton (identity, shape, collectionInfo)

**Files:**
- Create: `src/universal/filteredcollectionbackend.h`
- Create: `src/universal/filteredcollectionbackend.cpp`
- Test: `tests/sinks/tst_filtered_collection_backend.cpp` (extend)

- [ ] **Step 1: Write `src/universal/filteredcollectionbackend.h`**

```cpp
#pragma once

#include <QString>

#include "syncbackend.h"          // Kalburator::Sync::SyncBackend (calendar-typed base)
#include "recordfilter.h"         // Kalburator::Shape::RecordFilter
#include "collectioninfo.h"       // Kalburator::Sync::CollectionInfo

namespace Kalburator::Sync { class BackendRegistry; }

namespace Kalburator::Sinks {

/// Borrowed view of one collection on a parent SyncBackend, projected
/// through a `RecordFilter` as if it were its own backend with one
/// collection. Reads filter; writes stamp the filter property (`Contains`
/// additive, `Equals` filter-authoritative); delete + writability delegate.
///
/// The parent pointer is borrowed — not owned. If a `BackendRegistry` is
/// passed to the constructor, the FCB listens for
/// `backendInstanceUnregistered(parentBackendId)` and nulls its parent
/// pointer on receipt so subsequent reads/writes return clean failure
/// values rather than crash.
///
/// Composition layer only: the FCB is NOT a `BackendContribution` and has
/// no add-account UI. Consuming apps build instances at runtime and
/// register them via `BackendRegistry::registerBackendInstance` like any
/// other backend.
class FilteredCollectionBackend : public Kalburator::Sync::SyncBackend {
    Q_OBJECT
public:
    FilteredCollectionBackend(Kalburator::Sync::SyncBackend* parentBackend,
                              QString parentCollectionId,
                              QString virtualCollectionId,
                              Kalburator::Shape::RecordFilter filter,
                              Kalburator::Sync::BackendRegistry* registry = nullptr,
                              QString displayNameOverride = QString(),
                              QObject* parent = nullptr);

    QString backendType()  const override { return QStringLiteral("filtered-view"); }
    QString displayName()  const override;
    QString resourceId()   const override;
    bool    isAvailable()  const override;

    QList<Kalburator::Shape::Shape> nativeShapes() const override;
    Kalburator::Shape::Shape shapeFor(const QString& collectionId) const override;

    QList<Kalburator::Sync::CollectionInfo> availableCollections() override;
    Kalburator::Sync::CollectionInfo        collectionInfo(const QString& collectionId) override;

    bool    discoveredWritable(const QString& calendarId) const override;

    QList<Kalburator::Sync::BackendRecord>            loadRecords(const QString& collectionId) override;
    std::optional<Kalburator::Sync::BackendRecord>    loadRecord(const QString& recordId)      override;
    QString createRecord(const QString& collectionId,
                         const Kalburator::Sync::BackendRecord& record) override;
    bool    updateRecord(const Kalburator::Sync::BackendRecord& record) override;
    bool    deleteRecord(const QString& recordId)            override;

    // Accessors for tests / consumers.
    QString parentCollectionId() const { return m_parentColId; }
    QString virtualCollectionId() const { return m_virtualColId; }
    const Kalburator::Shape::RecordFilter& filter() const { return m_filter; }

private:
    /// Compose the parent's `CollectionInfo` for `m_parentColId` into a
    /// CollectionInfo for the virtual collection: rewrites `id`, applies
    /// `displayName` override (or composes a default), inherits `color`
    /// (via CollectionInfo defaults) and `readOnly`.
    Kalburator::Sync::CollectionInfo composeCollectionInfo() const;

    QString defaultComposedDisplayName(const QString& parentName) const;
    QString filterDescription() const;

    Kalburator::Sync::SyncBackend*   m_parent = nullptr;
    QString                          m_parentBackendId;
    QString                          m_parentColId;
    QString                          m_virtualColId;
    Kalburator::Shape::RecordFilter  m_filter;
    QString                          m_displayNameOverride;
};

} // namespace Kalburator::Sinks
```

- [ ] **Step 2: Write `src/universal/filteredcollectionbackend.cpp` (skeleton — fill in just the methods this task covers)**

```cpp
#include "filteredcollectionbackend.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrl>

#include "backendregistry.h"  // Kalburator::Sync::BackendRegistry

namespace Kalburator::Sinks {

using Kalburator::Shape::Shape;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;

FilteredCollectionBackend::FilteredCollectionBackend(
        Kalburator::Sync::SyncBackend* parentBackend,
        QString parentCollectionId,
        QString virtualCollectionId,
        Kalburator::Shape::RecordFilter filter,
        Kalburator::Sync::BackendRegistry* registry,
        QString displayNameOverride,
        QObject* parent)
    : Kalburator::Sync::SyncBackend(parent)
    , m_parent(parentBackend)
    , m_parentBackendId(parentBackend ? parentBackend->backendId() : QString())
    , m_parentColId(std::move(parentCollectionId))
    , m_virtualColId(std::move(virtualCollectionId))
    , m_filter(std::move(filter))
    , m_displayNameOverride(std::move(displayNameOverride))
{
    Q_UNUSED(registry); // wired in Task 7
}

QString FilteredCollectionBackend::displayName() const
{
    if (!m_displayNameOverride.isEmpty())
        return m_displayNameOverride;
    if (!m_parent)
        return defaultComposedDisplayName(QString());
    const CollectionInfo info = const_cast<Kalburator::Sync::SyncBackend*>(m_parent)
                                    ->collectionInfo(m_parentColId);
    return defaultComposedDisplayName(info.name);
}

QString FilteredCollectionBackend::filterDescription() const
{
    using Op = Kalburator::Shape::RecordFilter::Op;
    const QString prop = m_filter.property.toString();
    const QString val  = m_filter.value.toString();
    if (m_filter.op == Op::Contains)
        return QStringLiteral("%1 ∋ %2").arg(prop, val);  // " ∋ "
    return QStringLiteral("%1 = %2").arg(prop, val);
}

QString FilteredCollectionBackend::defaultComposedDisplayName(const QString& parentName) const
{
    if (parentName.isEmpty())
        return QStringLiteral("[%1]").arg(filterDescription());
    return QStringLiteral("%1 [%2]").arg(parentName, filterDescription());
}

bool FilteredCollectionBackend::isAvailable() const
{
    // Task 7 will tighten this to also follow parent's isAvailable.
    return m_parent != nullptr;
}

QList<Shape> FilteredCollectionBackend::nativeShapes() const
{
    if (!m_parent) return {};
    return { m_parent->shapeFor(m_parentColId) };
}

Shape FilteredCollectionBackend::shapeFor(const QString& collectionId) const
{
    if (!m_parent) return Shape::Any();
    if (collectionId != m_virtualColId) return Shape::Any();
    return m_parent->shapeFor(m_parentColId);
}

CollectionInfo FilteredCollectionBackend::composeCollectionInfo() const
{
    CollectionInfo out;
    if (!m_parent) {
        out.id = m_virtualColId;
        out.name = displayName();
        return out;
    }
    const CollectionInfo parentInfo = const_cast<Kalburator::Sync::SyncBackend*>(m_parent)
                                          ->collectionInfo(m_parentColId);
    out = parentInfo;             // inherit type, color (if any), readOnly, contentTypes
    out.id = m_virtualColId;
    out.name = displayName();
    return out;
}

QList<CollectionInfo> FilteredCollectionBackend::availableCollections()
{
    return { composeCollectionInfo() };
}

CollectionInfo FilteredCollectionBackend::collectionInfo(const QString& collectionId)
{
    if (collectionId != m_virtualColId) return CollectionInfo{};
    return composeCollectionInfo();
}

QString FilteredCollectionBackend::resourceId() const
{
    // Implemented in Task 6.
    if (!m_parent) return QStringLiteral("filtered-view:?");
    return QStringLiteral("filtered-view:") + m_parent->resourceId()
         + QLatin1Char('/') + m_parentColId;
}

bool FilteredCollectionBackend::discoveredWritable(const QString& calendarId) const
{
    // Implemented in Task 5.
    Q_UNUSED(calendarId);
    return m_parent != nullptr;
}

QList<BackendRecord> FilteredCollectionBackend::loadRecords(const QString& collectionId)
{
    Q_UNUSED(collectionId);
    return {};  // implemented in Task 3
}

std::optional<BackendRecord> FilteredCollectionBackend::loadRecord(const QString& recordId)
{
    Q_UNUSED(recordId);
    return std::nullopt;  // implemented in Task 3
}

QString FilteredCollectionBackend::createRecord(const QString& collectionId,
                                                const BackendRecord& record)
{
    Q_UNUSED(collectionId);
    Q_UNUSED(record);
    return {};  // implemented in Task 4
}

bool FilteredCollectionBackend::updateRecord(const BackendRecord& record)
{
    Q_UNUSED(record);
    return false;  // implemented in Task 4
}

bool FilteredCollectionBackend::deleteRecord(const QString& recordId)
{
    Q_UNUSED(recordId);
    return false;  // implemented in Task 5
}

} // namespace Kalburator::Sinks
```

- [ ] **Step 3: Add CMake wiring**

Edit `CMakeLists.txt`. After the existing entries in `KALBURATOR_SINKS_HEADERS` and `KALBURATOR_SINKS_SOURCES` (lines 424–435), append:

```cmake
# (header list, after universalstorageplugin.h)
    src/universal/filteredcollectionbackend.h
```

```cmake
# (source list, after universalstorageplugin.cpp)
    src/universal/filteredcollectionbackend.cpp
```

- [ ] **Step 4: Extend the test file with skeleton tests + a `FakeParentBackend`**

Add the following near the top of `tst_filtered_collection_backend.cpp` (after the existing `namespace { ... }` block but before `class TestFilteredCollectionBackend`):

```cpp
#include "filteredcollectionbackend.h"
#include "backendregistry.h"
#include "syncbackend.h"

using Kalburator::Sinks::FilteredCollectionBackend;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::SyncBackend;
using Kalburator::Shape::Shape;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;

namespace {

/// Minimal in-memory SyncBackend used as a filtered-view parent in tests.
/// Holds a single collection of BackendRecords keyed by record id; ops
/// (load/create/update/delete) record their effects in the in-memory map.
class FakeParentBackend : public SyncBackend {
    Q_OBJECT
public:
    explicit FakeParentBackend(QString backendId,
                               QString collectionId,
                               Shape shape,
                               QObject* parent = nullptr)
        : SyncBackend(parent)
        , m_backendId(std::move(backendId))
        , m_colId(std::move(collectionId))
        , m_shape(shape) {}

    QString backendId()    const override { return m_backendId; }
    QString backendType()  const override { return QStringLiteral("fake-parent"); }
    QString displayName()  const override { return QStringLiteral("Fake Parent"); }
    QString resourceId()   const override { return QStringLiteral("fake://") + m_backendId; }
    bool    isAvailable()  const override { return true; }

    QList<Shape> nativeShapes() const override { return { m_shape }; }
    Shape shapeFor(const QString&) const override { return m_shape; }

    QList<CollectionInfo> availableCollections() override {
        CollectionInfo ci;
        ci.id = m_colId;
        ci.name = m_colName;
        ci.readOnly = m_readOnly;
        return { ci };
    }
    CollectionInfo collectionInfo(const QString& id) override {
        if (id != m_colId) return {};
        CollectionInfo ci;
        ci.id = m_colId;
        ci.name = m_colName;
        ci.readOnly = m_readOnly;
        return ci;
    }

    bool discoveredWritable(const QString& id) const override {
        return id == m_colId ? !m_readOnly : false;
    }

    QList<BackendRecord> loadRecords(const QString& id) override {
        if (id != m_colId) return {};
        return m_records.values();
    }
    std::optional<BackendRecord> loadRecord(const QString& recordId) override {
        if (!m_records.contains(recordId)) return std::nullopt;
        return m_records.value(recordId);
    }
    QString createRecord(const QString& id, const BackendRecord& r) override {
        if (id != m_colId) return {};
        BackendRecord copy = r;
        if (copy.id.isEmpty()) copy.id = QStringLiteral("auto-%1").arg(++m_autoId);
        m_records.insert(copy.id, copy);
        m_lastWritten = copy;
        return copy.id;
    }
    bool updateRecord(const BackendRecord& r) override {
        if (!m_records.contains(r.id)) return false;
        m_records.insert(r.id, r);
        m_lastWritten = r;
        return true;
    }
    bool deleteRecord(const QString& recordId) override {
        return m_records.remove(recordId) > 0;
    }

    void setRecord(const BackendRecord& r) { m_records.insert(r.id, r); }
    void setReadOnly(bool ro) { m_readOnly = ro; }
    void setColName(QString n) { m_colName = std::move(n); }

    const BackendRecord& lastWritten() const { return m_lastWritten; }
    int recordCount() const { return m_records.size(); }

private:
    QString m_backendId;
    QString m_colId;
    QString m_colName = QStringLiteral("Parent Collection");
    Shape   m_shape;
    bool    m_readOnly = false;
    int     m_autoId = 0;
    QHash<QString, BackendRecord> m_records;
    BackendRecord m_lastWritten;
};

BackendRecord makeJsonRecord(const QString& id, const QJsonObject& obj)
{
    BackendRecord r;
    r.id = id;
    r.displayName = id;
    r.type = QStringLiteral("event");
    r.data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    return r;
}

const Shape kCalendarCanonShape{ DomainId{"calendar"}, EncodingId{"canon"} };

} // namespace
```

Add these new slots to the `private slots:` list (after the Task-1 RecordFilter slots), and define them at the bottom of the file (before `QTEST_MAIN`):

```cpp
    // ---- Identity / shape / collectionInfo (Task 2) ----------------------
    void identity_backendType_isFilteredView();
    void identity_displayName_overrideWins();
    void identity_displayName_composedDefault_includesParentNameAndFilter();
    void shape_delegatesToParentsShapeForParentColId();
    void availableCollections_returnsOneVirtualEntry();
    void collectionInfo_unknownId_returnsDefault();
    void collectionInfo_inheritsReadOnlyFromParent();
```

```cpp
void TestFilteredCollectionBackend::identity_backendType_isFilteredView()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    QCOMPARE(v.backendType(), QStringLiteral("filtered-view"));
}

void TestFilteredCollectionBackend::identity_displayName_overrideWins()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") },
                                /*registry=*/nullptr,
                                QStringLiteral("Work Route"));
    QCOMPARE(v.displayName(), QStringLiteral("Work Route"));
}

void TestFilteredCollectionBackend::identity_displayName_composedDefault_includesParentNameAndFilter()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    parent.setColName(QStringLiteral("Calendar"));
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    const QString name = v.displayName();
    QVERIFY2(name.contains(QStringLiteral("Calendar")), qPrintable(name));
    QVERIFY2(name.contains(QStringLiteral("categories")), qPrintable(name));
    QVERIFY2(name.contains(QStringLiteral("Work")), qPrintable(name));
}

void TestFilteredCollectionBackend::shape_delegatesToParentsShapeForParentColId()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    QCOMPARE(v.shapeFor("v1"), kCalendarCanonShape);
    QCOMPARE(v.nativeShapes().size(), 1);
    QCOMPARE(v.nativeShapes().first(), kCalendarCanonShape);
}

void TestFilteredCollectionBackend::availableCollections_returnsOneVirtualEntry()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    const auto cols = v.availableCollections();
    QCOMPARE(cols.size(), 1);
    QCOMPARE(cols.first().id, QStringLiteral("v1"));
}

void TestFilteredCollectionBackend::collectionInfo_unknownId_returnsDefault()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    const auto info = v.collectionInfo("other");
    QCOMPARE(info.id, QString());
}

void TestFilteredCollectionBackend::collectionInfo_inheritsReadOnlyFromParent()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    parent.setReadOnly(true);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    QVERIFY(v.collectionInfo("v1").readOnly);
}
```

- [ ] **Step 5: Build + run the test**

```bash
cmake --build build -j 2>&1 | tail -30
ctest --test-dir build -R tst_filtered_collection_backend --output-on-failure
```

Expected: build succeeds; **all Task-1 + Task-2 slots PASS** (16 slots).

- [ ] **Step 6: Commit**

```bash
git add src/universal/filteredcollectionbackend.h \
        src/universal/filteredcollectionbackend.cpp \
        tests/sinks/tst_filtered_collection_backend.cpp \
        CMakeLists.txt
git commit -m "feat(sinks): FilteredCollectionBackend skeleton

Identity (backendType=\"filtered-view\"), display-name composition with
override, parent-shape delegation, single-entry availableCollections
inheriting readOnly from parent. loadRecords/createRecord/... are stubs
filled by subsequent tasks. Tests use a FakeParentBackend that records
writes for later assertions.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: `loadRecords` / `loadRecord` with filter

**Files:**
- Modify: `src/universal/filteredcollectionbackend.cpp` (replace `loadRecords` and `loadRecord` stubs)
- Test: `tests/sinks/tst_filtered_collection_backend.cpp` (extend)

- [ ] **Step 1: Add the failing tests** — append slots & implementations:

```cpp
    // ---- Read filtering (Task 3) -----------------------------------------
    void loadRecords_returnsOnlyMatchingRecords();
    void loadRecords_excludesRecordsWithoutFilterProperty();
    void loadRecords_excludesNonJsonPayloads();
    void loadRecord_matching_returnsRecord();
    void loadRecord_nonMatching_returnsNullopt();
    void loadRecord_unknownId_returnsNullopt();
```

```cpp
void TestFilteredCollectionBackend::loadRecords_returnsOnlyMatchingRecords()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    parent.setRecord(makeJsonRecord("r1", withCategories({"Personal"})));
    parent.setRecord(makeJsonRecord("r2", withCategories({"Work"})));
    parent.setRecord(makeJsonRecord("r3", withCategories({"Work", "Personal"})));

    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    const auto recs = v.loadRecords("v1");
    QCOMPARE(recs.size(), 2);
    QSet<QString> ids;
    for (const auto& r : recs) ids.insert(r.id);
    QVERIFY(ids.contains("r2"));
    QVERIFY(ids.contains("r3"));
}

void TestFilteredCollectionBackend::loadRecords_excludesRecordsWithoutFilterProperty()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    QJsonObject noCats;
    noCats.insert(QStringLiteral("uid"), QStringLiteral("u1"));
    parent.setRecord(makeJsonRecord("r1", noCats));
    parent.setRecord(makeJsonRecord("r2", withCategories({"Work"})));

    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    const auto recs = v.loadRecords("v1");
    QCOMPARE(recs.size(), 1);
    QCOMPARE(recs.first().id, QStringLiteral("r2"));
}

void TestFilteredCollectionBackend::loadRecords_excludesNonJsonPayloads()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    BackendRecord bad;
    bad.id = "bad";
    bad.data = QByteArray("not json at all");
    parent.setRecord(bad);
    parent.setRecord(makeJsonRecord("good", withCategories({"Work"})));

    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    const auto recs = v.loadRecords("v1");
    QCOMPARE(recs.size(), 1);
    QCOMPARE(recs.first().id, QStringLiteral("good"));
}

void TestFilteredCollectionBackend::loadRecord_matching_returnsRecord()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    parent.setRecord(makeJsonRecord("r1", withCategories({"Work"})));
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    const auto rec = v.loadRecord("r1");
    QVERIFY(rec.has_value());
    QCOMPARE(rec->id, QStringLiteral("r1"));
}

void TestFilteredCollectionBackend::loadRecord_nonMatching_returnsNullopt()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    parent.setRecord(makeJsonRecord("r1", withCategories({"Personal"})));
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    const auto rec = v.loadRecord("r1");
    QVERIFY(!rec.has_value());
}

void TestFilteredCollectionBackend::loadRecord_unknownId_returnsNullopt()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    QVERIFY(!v.loadRecord("nope").has_value());
}
```

- [ ] **Step 2: Run the new tests to confirm they fail**

```bash
cmake --build build -j 2>&1 | tail -10
ctest --test-dir build -R tst_filtered_collection_backend --output-on-failure
```

Expected: 6 new slots FAIL (current stubs return empty lists / nullopt for everything; `loadRecords_excludesRecordsWithoutFilterProperty` & `loadRecord_matching_returnsRecord` etc. will fail with mismatched counts).

- [ ] **Step 3: Implement `loadRecords` and `loadRecord`** — replace the stubs in `filteredcollectionbackend.cpp`:

```cpp
QList<BackendRecord> FilteredCollectionBackend::loadRecords(const QString& collectionId)
{
    if (!m_parent || collectionId != m_virtualColId) return {};
    QList<BackendRecord> all = m_parent->loadRecords(m_parentColId);
    QList<BackendRecord> filtered;
    filtered.reserve(all.size());
    for (const BackendRecord& r : all) {
        if (m_filter.matches(r.data))
            filtered.append(r);
    }
    return filtered;
}

std::optional<BackendRecord> FilteredCollectionBackend::loadRecord(const QString& recordId)
{
    if (!m_parent) return std::nullopt;
    auto rec = m_parent->loadRecord(recordId);
    if (!rec.has_value()) return std::nullopt;
    if (!m_filter.matches(rec->data)) return std::nullopt;
    return rec;
}
```

- [ ] **Step 4: Run tests to confirm pass**

```bash
cmake --build build -j 2>&1 | tail -10
ctest --test-dir build -R tst_filtered_collection_backend --output-on-failure
```

Expected: all slots PASS (22 total now).

- [ ] **Step 5: Commit**

```bash
git add src/universal/filteredcollectionbackend.cpp \
        tests/sinks/tst_filtered_collection_backend.cpp
git commit -m "feat(sinks): FilteredCollectionBackend reads filter via RecordFilter

loadRecords delegates to parent, then filters via RecordFilter::matches
on each record's canon-JSON payload. Records without the filter property,
records whose payload is not JSON, and records whose property fails the
predicate are all omitted (fail-closed). loadRecord(id) applies the same
predicate; returns nullopt if the parent has no such record or if it
doesn't pass.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: `createRecord` / `updateRecord` with filter stamping

**Files:**
- Modify: `src/universal/filteredcollectionbackend.cpp` (replace `createRecord` and `updateRecord` stubs; add `stampFilterValue` helper)
- Modify: `src/universal/filteredcollectionbackend.h` (declare `stampFilterValue` private helper)
- Test: `tests/sinks/tst_filtered_collection_backend.cpp` (extend)

- [ ] **Step 1: Add the failing tests**

Add slots:

```cpp
    // ---- Write stamping (Task 4) -----------------------------------------
    void createRecord_contains_appendsFilterValueIfAbsent();
    void createRecord_contains_noDuplicateIfAlreadyPresent();
    void createRecord_contains_preservesExistingOrder();
    void createRecord_contains_preservesOtherCategoryValues();
    void createRecord_contains_payloadHasNoCategoriesField_addsArrayWithFilterValue();
    void createRecord_equals_alwaysOverwritesFilterProperty();
    void updateRecord_contains_stampsAndUpdatesParent();
    void updateRecord_equals_overwritesFilterProperty();
    void createRecord_unknownCollectionId_returnsEmpty();
```

Implementations:

```cpp
void TestFilteredCollectionBackend::createRecord_contains_appendsFilterValueIfAbsent()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    BackendRecord r = makeJsonRecord("r1", withCategories({"Personal"}));
    const QString id = v.createRecord("v1", r);
    QCOMPARE(id, QStringLiteral("r1"));
    const auto written = QJsonDocument::fromJson(parent.lastWritten().data).object();
    const auto cats = written.value("categories").toArray();
    QCOMPARE(cats.size(), 2);
    QCOMPARE(cats.at(0).toString(), QStringLiteral("Personal"));
    QCOMPARE(cats.at(1).toString(), QStringLiteral("Work"));
}

void TestFilteredCollectionBackend::createRecord_contains_noDuplicateIfAlreadyPresent()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    v.createRecord("v1", makeJsonRecord("r1", withCategories({"Work", "Personal"})));
    const auto cats = QJsonDocument::fromJson(parent.lastWritten().data)
                          .object().value("categories").toArray();
    QCOMPARE(cats.size(), 2);  // still 2, no dup
}

void TestFilteredCollectionBackend::createRecord_contains_preservesExistingOrder()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    v.createRecord("v1", makeJsonRecord("r1",
        withCategories({"Important", "Work", "Personal"})));
    const auto cats = QJsonDocument::fromJson(parent.lastWritten().data)
                          .object().value("categories").toArray();
    QCOMPARE(cats.at(0).toString(), QStringLiteral("Important"));
    QCOMPARE(cats.at(1).toString(), QStringLiteral("Work"));
    QCOMPARE(cats.at(2).toString(), QStringLiteral("Personal"));
}

void TestFilteredCollectionBackend::createRecord_contains_preservesOtherCategoryValues()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    v.createRecord("v1", makeJsonRecord("r1", withCategories({"Important"})));
    const auto cats = QJsonDocument::fromJson(parent.lastWritten().data)
                          .object().value("categories").toArray();
    QCOMPARE(cats.size(), 2);
    QStringList values;
    for (const auto& v : cats) values.append(v.toString());
    QVERIFY(values.contains("Important"));
    QVERIFY(values.contains("Work"));
}

void TestFilteredCollectionBackend::createRecord_contains_payloadHasNoCategoriesField_addsArrayWithFilterValue()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    QJsonObject noCats;
    noCats.insert(QStringLiteral("uid"), QStringLiteral("u1"));
    v.createRecord("v1", makeJsonRecord("r1", noCats));
    const auto cats = QJsonDocument::fromJson(parent.lastWritten().data)
                          .object().value("categories").toArray();
    QCOMPARE(cats.size(), 1);
    QCOMPARE(cats.first().toString(), QStringLiteral("Work"));
}

void TestFilteredCollectionBackend::createRecord_equals_alwaysOverwritesFilterProperty()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"status"},
                                              RecordFilter::Op::Equals,
                                              QStringLiteral("Done") });
    QJsonObject inObj;
    inObj.insert(QStringLiteral("uid"), QStringLiteral("u1"));
    inObj.insert(QStringLiteral("status"), QStringLiteral("InProgress"));
    v.createRecord("v1", makeJsonRecord("r1", inObj));
    const auto written = QJsonDocument::fromJson(parent.lastWritten().data).object();
    QCOMPARE(written.value("status").toString(), QStringLiteral("Done"));
}

void TestFilteredCollectionBackend::updateRecord_contains_stampsAndUpdatesParent()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    parent.setRecord(makeJsonRecord("r1", withCategories({"Personal"})));
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    BackendRecord r = makeJsonRecord("r1", withCategories({"Family"}));
    QVERIFY(v.updateRecord(r));
    const auto cats = QJsonDocument::fromJson(parent.lastWritten().data)
                          .object().value("categories").toArray();
    QStringList values;
    for (const auto& v : cats) values.append(v.toString());
    QVERIFY(values.contains("Family"));
    QVERIFY(values.contains("Work"));
}

void TestFilteredCollectionBackend::updateRecord_equals_overwritesFilterProperty()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    QJsonObject seed;
    seed.insert(QStringLiteral("uid"), QStringLiteral("u1"));
    seed.insert(QStringLiteral("status"), QStringLiteral("Done"));
    parent.setRecord(makeJsonRecord("r1", seed));
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"status"},
                                              RecordFilter::Op::Equals,
                                              QStringLiteral("Done") });
    QJsonObject newObj;
    newObj.insert(QStringLiteral("uid"), QStringLiteral("u1"));
    newObj.insert(QStringLiteral("status"), QStringLiteral("InProgress"));
    QVERIFY(v.updateRecord(makeJsonRecord("r1", newObj)));
    const auto written = QJsonDocument::fromJson(parent.lastWritten().data).object();
    QCOMPARE(written.value("status").toString(), QStringLiteral("Done"));
}

void TestFilteredCollectionBackend::createRecord_unknownCollectionId_returnsEmpty()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    const QString id = v.createRecord("not-v1",
        makeJsonRecord("r1", withCategories({"Work"})));
    QVERIFY(id.isEmpty());
}
```

- [ ] **Step 2: Run the tests to confirm they fail**

```bash
cmake --build build -j 2>&1 | tail -10
ctest --test-dir build -R tst_filtered_collection_backend --output-on-failure
```

Expected: 9 new slots FAIL.

- [ ] **Step 3: Add the `stampFilterValue` declaration to `filteredcollectionbackend.h`**

Inside the `private:` section, after `composeCollectionInfo()`:

```cpp
    /// Apply the filter's stamp semantics to a canon-JSON payload and
    /// return the rewritten bytes. Contains => append filter value to
    /// the property's array if absent (preserve order); Equals =>
    /// overwrite the property to the filter value (always).
    /// Returns the original bytes unchanged if the payload is not a
    /// JSON object (caller decides what to do with that).
    QByteArray stampFilterValue(const QByteArray& payload) const;
```

- [ ] **Step 4: Implement `stampFilterValue`, `createRecord`, `updateRecord`** — replace stubs in `.cpp`:

```cpp
QByteArray FilteredCollectionBackend::stampFilterValue(const QByteArray& payload) const
{
    using Op = Kalburator::Shape::RecordFilter::Op;
    QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject())
        return payload;
    QJsonObject obj = doc.object();
    const QString key = m_filter.property.toString();
    if (key.isEmpty())
        return payload;

    const QJsonValue filterValue = QJsonValue::fromVariant(m_filter.value);
    switch (m_filter.op) {
    case Op::Contains: {
        QJsonArray arr = obj.value(key).toArray();
        bool found = false;
        for (const QJsonValue& v : arr) {
            if (v == filterValue) { found = true; break; }
        }
        if (!found) arr.append(filterValue);
        obj.insert(key, arr);
        break;
    }
    case Op::Equals:
        obj.insert(key, filterValue);
        break;
    }
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QString FilteredCollectionBackend::createRecord(const QString& collectionId,
                                                const BackendRecord& record)
{
    if (!m_parent || collectionId != m_virtualColId) return {};
    BackendRecord stamped = record;
    stamped.data = stampFilterValue(record.data);
    return m_parent->createRecord(m_parentColId, stamped);
}

bool FilteredCollectionBackend::updateRecord(const BackendRecord& record)
{
    if (!m_parent) return false;
    BackendRecord stamped = record;
    stamped.data = stampFilterValue(record.data);
    return m_parent->updateRecord(stamped);
}
```

- [ ] **Step 5: Run tests to confirm pass**

```bash
cmake --build build -j 2>&1 | tail -10
ctest --test-dir build -R tst_filtered_collection_backend --output-on-failure
```

Expected: all slots PASS (31 total).

- [ ] **Step 6: Commit**

```bash
git add src/universal/filteredcollectionbackend.h \
        src/universal/filteredcollectionbackend.cpp \
        tests/sinks/tst_filtered_collection_backend.cpp
git commit -m "feat(sinks): FilteredCollectionBackend write-side filter stamping

Contains: createRecord/updateRecord append the filter value to the
property's array if absent (preserving existing element order); if the
property is absent, the array is created with just the filter value.
Existing other elements are preserved unchanged (a record tagged
['Work','Important'] keeps 'Important' when written through a 'Work'
filter — RFC §2.2).

Equals: always overwrite the filter property to the filter value on both
create and update, even when the caller's BackendRecord carried a
different value (filter-authoritative — sliced view IS the predicate).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: `deleteRecord` + `discoveredWritable` delegation

**Files:**
- Modify: `src/universal/filteredcollectionbackend.cpp` (replace `deleteRecord` and `discoveredWritable` stubs)
- Test: `tests/sinks/tst_filtered_collection_backend.cpp` (extend)

- [ ] **Step 1: Add failing tests**

Add slots:

```cpp
    // ---- Delete + writability delegation (Task 5) ------------------------
    void deleteRecord_delegatesToParent();
    void discoveredWritable_delegatesToParentForParentColId();
    void discoveredWritable_readOnlyParentYieldsReadOnlyView();
```

Implementations:

```cpp
void TestFilteredCollectionBackend::deleteRecord_delegatesToParent()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    parent.setRecord(makeJsonRecord("r1", withCategories({"Work"})));
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    QVERIFY(v.deleteRecord("r1"));
    QCOMPARE(parent.recordCount(), 0);
}

void TestFilteredCollectionBackend::discoveredWritable_delegatesToParentForParentColId()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    QVERIFY(v.discoveredWritable("v1"));
}

void TestFilteredCollectionBackend::discoveredWritable_readOnlyParentYieldsReadOnlyView()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    parent.setReadOnly(true);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    QVERIFY(!v.discoveredWritable("v1"));
}
```

- [ ] **Step 2: Run, confirm fail**

```bash
cmake --build build -j 2>&1 | tail -10
ctest --test-dir build -R tst_filtered_collection_backend --output-on-failure
```

Expected: `discoveredWritable_readOnlyParentYieldsReadOnlyView` and `deleteRecord_delegatesToParent` FAIL (current stubs always return false on delete; discoveredWritable returns parent!=null only).

- [ ] **Step 3: Replace stubs in `.cpp`**

```cpp
bool FilteredCollectionBackend::deleteRecord(const QString& recordId)
{
    if (!m_parent) return false;
    return m_parent->deleteRecord(recordId);
}

bool FilteredCollectionBackend::discoveredWritable(const QString& calendarId) const
{
    if (!m_parent) return false;
    if (calendarId != m_virtualColId) return false;
    return m_parent->discoveredWritable(m_parentColId);
}
```

- [ ] **Step 4: Run, confirm pass**

```bash
cmake --build build -j 2>&1 | tail -10
ctest --test-dir build -R tst_filtered_collection_backend --output-on-failure
```

Expected: all slots PASS (34 total).

- [ ] **Step 5: Commit**

```bash
git add src/universal/filteredcollectionbackend.cpp \
        tests/sinks/tst_filtered_collection_backend.cpp
git commit -m "feat(sinks): FilteredCollectionBackend delete + writability delegate

deleteRecord forwards to the parent unchanged — the filter doesn't
gate deletes (the engine has already decided this id must go). A
read-only parent yields a read-only filtered view via discoveredWritable
delegation, so v0.57's authority enforcement on the write path applies
transparently (RFC §2.2).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: `resourceId()` canonical encoding

**Files:**
- Modify: `src/universal/filteredcollectionbackend.cpp` (replace `resourceId` stub; add `canonJsonOfValue` helper)
- Modify: `src/universal/filteredcollectionbackend.h` (declare helper)
- Test: `tests/sinks/tst_filtered_collection_backend.cpp` (extend)

- [ ] **Step 1: Add failing tests**

Add slots:

```cpp
    // ---- resourceId stability (Task 6) -----------------------------------
    void resourceId_includesParentResourceColIdPropertyOpAndValue();
    void resourceId_equivalentConstructions_yieldEqualIds();
    void resourceId_differingFilters_yieldDifferentIds();
    void resourceId_urlEncodesNonAsciiValue();
```

Implementations:

```cpp
void TestFilteredCollectionBackend::resourceId_includesParentResourceColIdPropertyOpAndValue()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    const QString rid = v.resourceId();
    QVERIFY2(rid.startsWith("filtered-view:"), qPrintable(rid));
    QVERIFY2(rid.contains("fake://p1"),        qPrintable(rid));
    QVERIFY2(rid.contains("/cal-1?"),          qPrintable(rid));
    // Fixed key order: p, op, v.
    const int pIdx  = rid.indexOf("p=");
    const int opIdx = rid.indexOf("op=");
    const int vIdx  = rid.indexOf("v=");
    QVERIFY(pIdx > 0 && opIdx > pIdx && vIdx > opIdx);
    QVERIFY2(rid.contains("p=categories"), qPrintable(rid));
    QVERIFY2(rid.contains("op=contains"),  qPrintable(rid));
}

void TestFilteredCollectionBackend::resourceId_equivalentConstructions_yieldEqualIds()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend a(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    FilteredCollectionBackend b(&parent, "cal-1", "v1-alt",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    // virtualColId is NOT part of resourceId — it's the consumer's local
    // handle, not part of identity. Spec §2.2 lists only parent.resourceId,
    // parentColId, propertyId, op, value.
    QCOMPARE(a.resourceId(), b.resourceId());
}

void TestFilteredCollectionBackend::resourceId_differingFilters_yieldDifferentIds()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend work(&parent, "cal-1", "v1",
                                   RecordFilter{ PropertyId{"categories"},
                                                 RecordFilter::Op::Contains,
                                                 QStringLiteral("Work") });
    FilteredCollectionBackend personal(&parent, "cal-1", "v2",
                                       RecordFilter{ PropertyId{"categories"},
                                                     RecordFilter::Op::Contains,
                                                     QStringLiteral("Personal") });
    QVERIFY(work.resourceId() != personal.resourceId());

    FilteredCollectionBackend equalsDone(&parent, "cal-1", "v3",
                                         RecordFilter{ PropertyId{"status"},
                                                       RecordFilter::Op::Equals,
                                                       QStringLiteral("Done") });
    QVERIFY(equalsDone.resourceId() != work.resourceId());
}

void TestFilteredCollectionBackend::resourceId_urlEncodesNonAsciiValue()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("café") });
    const QString rid = v.resourceId();
    QVERIFY2(rid.contains("v="), qPrintable(rid));
    // "café" should be percent-encoded — no raw é byte.
    QVERIFY2(!rid.contains(QStringLiteral("é")), qPrintable(rid));
}
```

- [ ] **Step 2: Run, confirm fail**

```bash
cmake --build build -j 2>&1 | tail -10
ctest --test-dir build -R tst_filtered_collection_backend --output-on-failure
```

Expected: all 4 new slots FAIL (current `resourceId` stub returns only `filtered-view:<parent.resourceId>/<colId>`).

- [ ] **Step 3: Declare helper in `.h` (private)**

```cpp
    /// Canonical JSON serialization of the filter value, suitable for
    /// embedding in resourceId(). Strings come back as JSON (`"Work"`);
    /// objects/arrays come back with sorted keys and no whitespace.
    static QByteArray canonJsonOfValue(const QVariant& value);

    /// Lowercase token for the filter op ("contains" / "equals").
    static QString opToken(Kalburator::Shape::RecordFilter::Op op);
```

- [ ] **Step 4: Implement helpers + final `resourceId()` in `.cpp`**

Replace `resourceId()` and add helpers:

```cpp
QByteArray FilteredCollectionBackend::canonJsonOfValue(const QVariant& value)
{
    // QJsonDocument cannot serialize scalars at the root; wrap in a
    // single-element array, serialize compactly (keys auto-sorted for
    // objects), and strip the brackets.
    QJsonArray wrapper;
    wrapper.append(QJsonValue::fromVariant(value));
    const QByteArray bytes = QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
    // bytes looks like "[\"Work\"]" — strip outer [ and ].
    if (bytes.size() >= 2 && bytes.startsWith('[') && bytes.endsWith(']'))
        return bytes.mid(1, bytes.size() - 2);
    return bytes;
}

QString FilteredCollectionBackend::opToken(Kalburator::Shape::RecordFilter::Op op)
{
    using Op = Kalburator::Shape::RecordFilter::Op;
    switch (op) {
    case Op::Contains: return QStringLiteral("contains");
    case Op::Equals:   return QStringLiteral("equals");
    }
    return QStringLiteral("unknown");
}

QString FilteredCollectionBackend::resourceId() const
{
    const QString parentRes = m_parent ? m_parent->resourceId() : QString();
    const QString encodedValue = QString::fromUtf8(
        QUrl::toPercentEncoding(QString::fromUtf8(canonJsonOfValue(m_filter.value))));
    return QStringLiteral("filtered-view:%1/%2?p=%3&op=%4&v=%5")
        .arg(parentRes,
             m_parentColId,
             m_filter.property.toString(),
             opToken(m_filter.op),
             encodedValue);
}
```

- [ ] **Step 5: Run, confirm pass**

```bash
cmake --build build -j 2>&1 | tail -10
ctest --test-dir build -R tst_filtered_collection_backend --output-on-failure
```

Expected: all slots PASS (38 total).

- [ ] **Step 6: Commit**

```bash
git add src/universal/filteredcollectionbackend.h \
        src/universal/filteredcollectionbackend.cpp \
        tests/sinks/tst_filtered_collection_backend.cpp
git commit -m "feat(sinks): FilteredCollectionBackend stable resourceId

Per RFC §2.2: filtered-view:<parent.resourceId>/<parentColId>?p=<propertyId>
&op=<contains|equals>&v=<urlencode(canonJson(value))>. virtualColId is
NOT part of resourceId — it's the consumer's local handle, not part of
identity, so two FCBs naming the same slice differently still hash to
the same baseline key. canonJson uses QJsonDocument's compact form
(keys auto-sorted for objects); value is percent-encoded (RFC 3986
via QUrl::toPercentEncoding).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: Parent-unregister hook via `BackendRegistry`

**Files:**
- Modify: `src/universal/filteredcollectionbackend.h` (no API change; remember the registry param already exists from Task 2 — wire its connect)
- Modify: `src/universal/filteredcollectionbackend.cpp` (connect to signal in constructor; tighten `isAvailable()`)
- Test: `tests/sinks/tst_filtered_collection_backend.cpp` (extend)

- [ ] **Step 1: Add failing tests**

Add slots:

```cpp
    // ---- Parent lifetime (Task 7) ----------------------------------------
    void isAvailable_followsParentAvailability();
    void parentUnregistered_isAvailableBecomesFalse();
    void parentUnregistered_loadRecordsReturnsEmpty();
    void parentUnregistered_createRecordReturnsEmpty();
    void parentUnregistered_updateRecordReturnsFalse();
    void parentUnregistered_deleteRecordReturnsFalse();
    void parentUnregisteredOtherId_isStillAvailable();
```

Add an `isAvailable_followsParentAvailability` helper. Extend `FakeParentBackend` near its existing declaration with:

```cpp
    void setAvailable(bool a) { m_available = a; }
    bool isAvailable() const override { return m_available; }
private:
    bool m_available = true;
```

(Replace the existing fixed-true `isAvailable()` override with the toggleable one — adjust the placement.)

Implementations:

```cpp
void TestFilteredCollectionBackend::isAvailable_followsParentAvailability()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    QVERIFY(v.isAvailable());
    parent.setAvailable(false);
    QVERIFY(!v.isAvailable());
    parent.setAvailable(true);
    QVERIFY(v.isAvailable());
}

void TestFilteredCollectionBackend::parentUnregistered_isAvailableBecomesFalse()
{
    Kalburator::Sync::BackendRegistry registry;
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    registry.registerBackendInstance("p1", &parent);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") },
                                &registry);
    QVERIFY(v.isAvailable());
    registry.unregisterBackendInstance("p1");
    QVERIFY(!v.isAvailable());
}

void TestFilteredCollectionBackend::parentUnregistered_loadRecordsReturnsEmpty()
{
    Kalburator::Sync::BackendRegistry registry;
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    parent.setRecord(makeJsonRecord("r1", withCategories({"Work"})));
    registry.registerBackendInstance("p1", &parent);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") },
                                &registry);
    registry.unregisterBackendInstance("p1");
    QVERIFY(v.loadRecords("v1").isEmpty());
    QVERIFY(!v.loadRecord("r1").has_value());
}

void TestFilteredCollectionBackend::parentUnregistered_createRecordReturnsEmpty()
{
    Kalburator::Sync::BackendRegistry registry;
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    registry.registerBackendInstance("p1", &parent);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") },
                                &registry);
    registry.unregisterBackendInstance("p1");
    QVERIFY(v.createRecord("v1", makeJsonRecord("r1", withCategories({"Work"})))
              .isEmpty());
}

void TestFilteredCollectionBackend::parentUnregistered_updateRecordReturnsFalse()
{
    Kalburator::Sync::BackendRegistry registry;
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    parent.setRecord(makeJsonRecord("r1", withCategories({"Work"})));
    registry.registerBackendInstance("p1", &parent);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") },
                                &registry);
    registry.unregisterBackendInstance("p1");
    QVERIFY(!v.updateRecord(makeJsonRecord("r1", withCategories({"Personal"}))));
}

void TestFilteredCollectionBackend::parentUnregistered_deleteRecordReturnsFalse()
{
    Kalburator::Sync::BackendRegistry registry;
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    parent.setRecord(makeJsonRecord("r1", withCategories({"Work"})));
    registry.registerBackendInstance("p1", &parent);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") },
                                &registry);
    registry.unregisterBackendInstance("p1");
    QVERIFY(!v.deleteRecord("r1"));
}

void TestFilteredCollectionBackend::parentUnregisteredOtherId_isStillAvailable()
{
    Kalburator::Sync::BackendRegistry registry;
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    registry.registerBackendInstance("p1", &parent);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") },
                                &registry);
    registry.unregisterBackendInstance("some-other-backend");
    QVERIFY(v.isAvailable());
}
```

- [ ] **Step 2: Run, confirm fail**

```bash
cmake --build build -j 2>&1 | tail -10
ctest --test-dir build -R tst_filtered_collection_backend --output-on-failure
```

Expected: the new slots involving registry unregister FAIL (constructor's `Q_UNUSED(registry)` line in Task 2 means no signal is connected); `isAvailable_followsParentAvailability` will fail (current `isAvailable()` only checks `m_parent != nullptr`).

- [ ] **Step 3: Wire the constructor + tighten `isAvailable()` in `.cpp`**

Replace the constructor body:

```cpp
FilteredCollectionBackend::FilteredCollectionBackend(
        Kalburator::Sync::SyncBackend* parentBackend,
        QString parentCollectionId,
        QString virtualCollectionId,
        Kalburator::Shape::RecordFilter filter,
        Kalburator::Sync::BackendRegistry* registry,
        QString displayNameOverride,
        QObject* parent)
    : Kalburator::Sync::SyncBackend(parent)
    , m_parent(parentBackend)
    , m_parentBackendId(parentBackend ? parentBackend->backendId() : QString())
    , m_parentColId(std::move(parentCollectionId))
    , m_virtualColId(std::move(virtualCollectionId))
    , m_filter(std::move(filter))
    , m_displayNameOverride(std::move(displayNameOverride))
{
    if (registry && !m_parentBackendId.isEmpty()) {
        connect(registry, &Kalburator::Sync::BackendRegistry::backendInstanceUnregistered,
                this, [this](const QString& backendId) {
            if (backendId == m_parentBackendId) {
                m_parent = nullptr;
            }
        });
    }
}
```

Replace `isAvailable()`:

```cpp
bool FilteredCollectionBackend::isAvailable() const
{
    return m_parent != nullptr && m_parent->isAvailable();
}
```

- [ ] **Step 4: Run, confirm pass**

```bash
cmake --build build -j 2>&1 | tail -10
ctest --test-dir build -R tst_filtered_collection_backend --output-on-failure
```

Expected: all slots PASS (45 total).

- [ ] **Step 5: Commit**

```bash
git add src/universal/filteredcollectionbackend.cpp \
        tests/sinks/tst_filtered_collection_backend.cpp
git commit -m "feat(sinks): FilteredCollectionBackend honours parent unregister + availability

Constructor optionally takes a BackendRegistry pointer; if given, the
FCB listens for backendInstanceUnregistered with the parent's backendId
and nulls its parent pointer on receipt. After that, isAvailable
returns false and all CRUD methods return clean failure values
(empty record id / empty list / nullopt / false) instead of UAF-ing
through a dangling pointer. isAvailable also follows the live parent's
isAvailable while the parent is still registered.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 8: Final verification — full ctest suite

**Files:** none (verification only).

- [ ] **Step 1: Reconfigure (in case CMakeLists changed) + full build**

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j 2>&1 | tail -30
```

Expected: build succeeds.

- [ ] **Step 2: Run the full ctest suite**

```bash
ctest --test-dir build --output-on-failure 2>&1 | tail -40
```

Expected: **the new test binary PASSES**, and the rest of the suite is at the same green level as before the branch (per `docs/campaign/STATUS.md`, 111/112 with the one known pre-existing async flake at `tst_providerlifecycle` — already documented as RESOLVED in MEMORY, but if you see exactly that one failure and nothing else new, that is acceptable; rerun in isolation to confirm).

If any test that was green before this branch now fails, stop and investigate before continuing.

- [ ] **Step 3: Update the symlink + clangd compile database**

```bash
ln -sf build/compile_commands.json compile_commands.json
```

- [ ] **Step 4: Verify branch is clean and write summary commit (no-op if previous commits already cover everything)**

```bash
git status
git log --oneline -10
```

Expected: the 7 task commits (Tasks 1, 2, 3, 4, 5, 6, 7) are on top of the starting `main`.

- [ ] **Step 5: Push branch + offer PR**

Per the project rules (`/home/clinton/dev/CLAUDE.md`): every project has a remote on Codeberg; push before ending a session. Don't push to `main` directly; use a feature branch.

```bash
git push -u origin feature/filtered-collection-backend
```

Then surface to the user: "feature/filtered-collection-backend is pushed. Want a PR opened, or merge to main directly?" Defer the choice — do not auto-open a PR or merge.

---

## Self-review checklist

**Spec coverage** (RFC §0–§4):
- §0 / §2.1 `RecordFilter` struct — Task 1 ✓
- §2.1 `matches()` Contains semantics (array, case-sensitive) — Task 1 ✓
- §2.1 `matches()` Equals semantics — Task 1 ✓
- §2.1 missing property / type mismatch / bad JSON — Task 1 ✓
- §2.2 `FilteredCollectionBackend` identity (backendType, displayName, resourceId) — Tasks 2, 6 ✓
- §2.2 shape delegation — Task 2 ✓
- §2.2 reads (loadRecords / loadRecord) — Task 3 ✓
- §2.2 writes Contains additive (no dup, preserve order, preserve other elements) — Task 4 ✓
- §2.2 writes Equals overwriting — Task 4 ✓
- §2.2 deleteRecord delegation — Task 5 ✓
- §2.2 discoveredWritable delegation (read-only parent => read-only view) — Task 5 ✓
- §2.3 CollectionInfo composition (id, displayName override + composed default, color/readOnly inheritance) — Task 2 ✓
- §2.4 parent unregister hook (null parent, isAvailable→false, ops return clean failure values) — Task 7 ✓
- §2.4 transient availability follows parent — Task 7 ✓
- §3 NOT asked for (no query language, no indexing, no persistence) — covered by omission ✓
- §4 test list — all 8 bullets covered across Tasks 1, 3, 4, 5, 6, 7 ✓

**Type / signature consistency:**
- `RecordFilter::Op { Contains, Equals }` — used consistently across Tasks 1, 4, 6.
- `FilteredCollectionBackend` constructor parameter order locked in Task 2 and used unchanged in every subsequent test fixture.
- `BackendRegistry::backendInstanceUnregistered(QString)` signal name verified in `src/sync/backendregistry.h:94`.

**Placeholders:** none. Every code step shows the actual code; every command shows the expected outcome.

**Layout match:**
- `RecordFilter` is in `Kalburator::Shape` (matches RFC §0).
- `FilteredCollectionBackend` is in `Kalburator::Sinks` (matches RFC §0 + existing `RawFilesBackend` namespace).
- Header location `src/universal/filteredcollectionbackend.{h,cpp}` matches RFC §0.
- RFC §0 has `RecordFilter` in `src/types/recordfilter.h` — this plan puts it in `src/shape/recordfilter.h` instead because `PropertyId` lives in `src/shape/propertycatalogue.h` and the codebase keeps shape-typed values under `src/shape/`. Note in the Task 1 commit message that the RFC's hint was followed structurally, just with a more accurate directory.
