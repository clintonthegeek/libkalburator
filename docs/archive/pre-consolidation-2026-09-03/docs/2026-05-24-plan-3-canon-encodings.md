# Plan 3 — Canon Encodings Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Introduce rich JSON superset canonical encodings — `contacts+canon`, `todo+canon`, `calendar+canon` — as each domain's canonical head, demoting the legacy text encodings (`vcard4`/`vcard3`, `ical-vtodo`, `ical`) to lossy peer encodings reached through honestly-classified bridge edges, with one reusable canon-JSON differ/merger.

**Architecture:** Each domain's `DomainDefinition` flips `canonicalShape()` from its text encoding to `<domain>+canon` and returns a canon `PropertyCatalogue` + a shared canon-JSON differ/merger. A `ShapeContribution` registers the legacy encoding(s) as peers plus the bridge edges: `peer → canon` (lossless promote), `canon → peer` (lossy demote, four-kind `LossProfile`), and the `canon → canon` identity hub. The (de)serialization is done by `TransformationStage`s that parse/emit the vendor format against the canon JSON bytes (EIP Messaging Mapper). The engine already routes `backendShape → canon → backendShape` automatically via `TransformationRegistry::compile` once the head is the canon — so the differ/merger and the `canon → peer` stage **must land in the same task as each head-flip**, or the canonical differ would receive canon JSON it cannot parse.

**Tech Stack:** C++17 / Qt6 (`QJsonDocument`/`QJsonObject` for canon bytes), KCalendarCore (iCal / VTODO), KContacts (vCard), CMake, QtTest. Build/test commands per `docs/campaign/STATUS.md`.

**Source of truth:** schema doc (`docs/2026-05-23-canon-schema-design.md` §1–§6), design (`docs/2026-05-23-canon-upgrade-and-convergence-design.md` §2, §4–§6, §10), vendor reference (`docs/2026-05-23-vendor-api-shapes-reference.md`), INVARIANTS (esp. 1, 2, 3, 4, 5, 6, 8, 10; P1–P4), STATUS locked-decisions 1, 2, 4, 6, 7, 8, 10.

---

## Landed APIs this plan builds against (verified in tree, invariant P1)

Copy these signatures exactly; do not guess. All in `Kalburator::Shape` unless noted.

- `struct Shape { DomainId domain; EncodingId encoding; ... };` aggregate-init: `Shape{ DomainId{"contacts"}, EncodingId{"vcard4"} }` — `src/shape/shape.h:28`.
- `class PropertyId { explicit PropertyId(QString); QString toString() const; ... };` — `src/shape/propertycatalogue.h:10`.
- `enum class PropertyKind { String, Integer, Boolean, DateTime, Duration, Bytes, StringList, Json };` — `propertycatalogue.h:22`. **There is no `Json` *type*** — canon values are `QByteArray` JSON parsed with `QJsonDocument`/`QJsonObject`.
- `struct PropertyDescriptor { PropertyId id; PropertyKind kind = String; QString displayName; bool optional = true; };` — `propertycatalogue.h:33`.
- `class PropertyCatalogue { void addProperty(PropertyDescriptor); const QList<PropertyDescriptor>& properties() const; bool hasProperty(const PropertyId&) const; const PropertyDescriptor* find(const PropertyId&) const; QStringList sqlColumnDdl() const; };` — `propertycatalogue.h:40`.
- `struct CanonicalRecord { Shape shape; QByteArray data; QString recordId; bool isDeleted = false; };` — `src/shape/canonicalrecord.h:13`. **`data` is the opaque encoded bytes for `shape`'s encoding** (after Plan 3, canon records carry canon JSON in `data`).
- `class TransformationStage { virtual QByteArray transform(const QByteArray& sourceBytes) const = 0; };` and `class IdentityStage : public TransformationStage {...};` — `src/shape/transformationedge.h:15,24`. Stages are pure, I/O-free, byte-in/byte-out.
- `struct TransformationEdge { Shape from; Shape to; LossProfile loss; std::shared_ptr<TransformationStage> stage; };` — `transformationedge.h:31`. Edges are one-directional; invertibility = two edges.
- `enum class LossKind { Dropped, Simplified, Reversible, Degraded };` and `struct LossProfile { QHash<PropertyId, LossKind> affected; bool isLossless() const; LossProfile compose(const LossProfile&) const; QString summary() const; QSet<PropertyId> droppedProperties() const; };` — `src/shape/lossprofile.h:12,23`. **No helper ctors** — build via `loss.affected.insert(PropertyId{...}, LossKind::...)`.
- `class Pipeline { LossProfile composedLoss() const; QByteArray apply(const QByteArray&) const; bool isIdentity() const; const QList<TransformationEdge>& edges() const; ... };` — `src/shape/pipeline.h:16`.
- `class RecordDiffer { virtual QSet<PropertyId> diff(const CanonicalRecord& source, const CanonicalRecord& baseline) const = 0; virtual bool equal(const CanonicalRecord& a, const CanonicalRecord& b) const = 0; };` — `src/shape/recorddiffer.h:14`.
- `class RecordMerger { enum class FieldChoice { TakeSource, TakeTarget, TakeBaseline }; virtual CanonicalRecord merge(const CanonicalRecord& source, const CanonicalRecord& target, const CanonicalRecord& baseline, const Kalburator::Conflict::ConflictPolicy& policy) const = 0; };` — `src/shape/recordmerger.h:11`.
- `class DomainDefinition { virtual DomainId domain() const = 0; virtual Shape canonicalShape() const = 0; virtual PropertyCatalogue canonicalCatalogue() const = 0; virtual std::unique_ptr<RecordDiffer> createCanonicalDiffer() const = 0; virtual std::unique_ptr<RecordMerger> createCanonicalMerger() const = 0; virtual int richnessRank(const Shape&) const = 0; virtual QStringList baselineProperties() const { return {}; } };` — `src/shape/domaindefinition.h:24`.
- `class ShapeContribution { virtual DomainId targetDomain() const = 0; virtual QList<std::pair<Shape, PropertyCatalogue>> peerShapes() const = 0; virtual QList<TransformationEdge> edges() const = 0; };` — `src/shape/shapecontribution.h:23`.
- `TransformationRegistry`: `void registerShape(Shape, PropertyCatalogue); void declareCanonical(DomainId, Shape); void registerEdge(TransformationEdge); std::optional<Pipeline> compile(Shape from, Shape to) const; LossProfile inspect(Shape from, Shape to) const; const PropertyCatalogue* catalogueFor(const Shape&) const;` — `src/shape/transformationregistry.h`. `compile` freezes `from.domain` on first non-identity call.
- Plugin contract: `class Plugin { virtual QList<std::shared_ptr<Shape::DomainDefinition>> domainDefinitions(); virtual QList<std::shared_ptr<Shape::ShapeContribution>> shapeContributions(); virtual QList<std::shared_ptr<Shape::DomainOperations>> domainOperations(); ... };` — `src/plugin/plugin.h:17`.
- **Engine canonical resolution (the reason head-flip + differ must be atomic):** `SyncEngineWorker` reads `dd->canonicalShape()` (`src/engine/syncengine.cpp:1887`), compiles `srcShape→canonical`/`canonical→tgtShape` (`:1900-1903`), promotes records to canon on read (`:2000-2004`, `:2065-2069`), runs `dd->createCanonicalDiffer()/Merger()` on canon records (`:2097-2101`), demotes on write (`:2585-2611`). It never reads the graph's `canonicalFor()`.
- **Transcoding is dormant in the default build:** `TranscodingPlan` is keyed by `backendType()` and only the `orgmode`-keyed `RRuleTranscoder` is registered; with `KALBURATOR_HAVE_ORG_IO=OFF` no orgmode backend exists, so plans are empty no-ops (`src/transcoding/transcodingplan.h:36-38`). Plan 3 does **not** touch `src/transcoding/`, `ApplyContext.transcodingPlan`, or `CalendarPluginWriter`'s plan handling — those are Plan 4.

### Existing per-domain files to model new code on

- Contacts (the closest template — already has a peer): `src/contacts/contactsdomaindefinition.{h,cpp}`, `src/contacts/contactsstockshapes.{h,cpp}`, `src/contacts/vcardproperties.cpp` (`makeVCardCatalogue()`), `src/contacts/vcard3to4stage.*`/`vcard4to3stage.*` (peer stages), `src/contacts/vcarddiffer.cpp`/`vcardmerger.cpp` (`RecordDifferVCard`/`RecordMergerVCard`). Canonical today = `{contacts, vcard4}`, peer `{contacts, vcard3}`.
- Todo: `src/todo/tododomaindefinition.{h,cpp}` (canonical `{todo, ical-vtodo}`), `src/todo/todostockshapes.{h,cpp}`, the vtodo catalogue + `RecordDifferVTodo`/`RecordMergerVTodo`.
- Calendar: `src/calendar/calendardomaindefinition.{h,cpp}` (canonical `{calendar, ical}`), `src/calendar/calendarstockshapes.{h,cpp}` (today only an `ical→ical` identity edge, empty `peerShapes()`), `src/calendar/icalproperties.cpp` (`makeICalCatalogue()`), `src/calendar/icalrecorddiffer.cpp`/`icalrecordmerger.cpp`.
- Spine fixture template (widen/narrow edges, append-only): `tests/shape/tst_canonical_spine.cpp`.

---

## Why each task stays green (read before starting)

- **Part 0 (Tasks 1–3)** adds new files only (`canonjson*`); nothing is wired into a domain yet. Unit-tested with synthetic catalogues. Zero behavior change to any domain → whole suite green.
- **Each domain Part (A/B/C)** flips that domain's head to canon **and** lands its canon differ/merger and `canon→peer` stage **in the same sequence**, with the domain's integration suite as the green-gate task at the end of the Part. Until a domain's Part runs, that domain keeps its text-encoding canonical and its existing differ — untouched and green.
- **Demotion is append-by-contribution, not edge rewrite (invariant 2):** existing peer edges (e.g. contacts `vcard3↔vcard4`) are **never** modified. The canon becomes a new hub; `vcard3` reaches it via the existing `vcard3→vcard4` edge composed with the new `vcard4→canon` edge (N-hop `compile`). No peer edge is repointed.
- **Calendar (Part C)** is safe because transcoding is dormant by default (empty plans) and the engine routes `ical→canon→ical` transparently; the only couplings that would break are the differ/merger byte-format and the `canon→ical` write-stage, both landed in Part C.

---

## File structure

| File | Responsibility | Change |
|------|----------------|--------|
| `src/shape/canonenvelope.{h,cpp}` | Build/read the `_canon` envelope + `uid` + `providerExtras` passthrough; semantic JSON value equality helper | **Create** |
| `src/shape/canonjsondiffer.{h,cpp}` | Reusable `RecordDiffer` over canon JSON, parameterized by the canon `PropertyId` list | **Create** |
| `src/shape/canonjsonmerger.{h,cpp}` | Reusable `RecordMerger` over canon JSON (per-`PropertyId` 3-way; `providerExtras` follows origin; `recurrence` opaque) | **Create** |
| `src/contacts/contactscanonproperties.{h,cpp}` | `makeContactsCanonCatalogue()` (schema §3) + the canon `PropertyId` list | **Create** |
| `src/contacts/vcardcanonstages.{h,cpp}` | `VCard4ToCanonStage` / `CanonToVCard4Stage` | **Create** |
| `src/contacts/contactsdomaindefinition.cpp` | Flip canonical to `{contacts, canon}`; canon catalogue; canon differ/merger; richnessRank | Modify |
| `src/contacts/contactsstockshapes.cpp` | Demote `vcard4` to peer; register canon hub + `vcard4↔canon` bridges (keep `vcard3↔vcard4`) | Modify |
| `src/todo/todocanonproperties.{h,cpp}` | `makeTodoCanonCatalogue()` (schema §4) + the canon `PropertyId` list | **Create** |
| `src/todo/vtodocanonstages.{h,cpp}` | `VTodoToCanonStage` / `CanonToVTodoStage` | **Create** |
| `src/todo/tododomaindefinition.cpp` | Flip canonical to `{todo, canon}`; canon catalogue; canon differ/merger; richnessRank | Modify |
| `src/todo/todostockshapes.cpp` | Demote `ical-vtodo` to peer; register canon hub + `ical-vtodo↔canon` bridges | Modify |
| `src/calendar/calendarcanonproperties.{h,cpp}` | `makeCalendarCanonCatalogue()` (schema §2) + the canon `PropertyId` list | **Create** |
| `src/calendar/icalcanonstages.{h,cpp}` | `ICalToCanonStage` / `CanonToICalStage` | **Create** |
| `src/calendar/calendardomaindefinition.cpp` | Flip canonical to `{calendar, canon}`; canon catalogue; canon differ/merger; richnessRank | Modify |
| `src/calendar/calendarstockshapes.cpp` | Demote `ical` to peer; register canon hub + `ical↔canon` bridges | Modify |
| `CMakeLists.txt` | Add the new `.cpp` sources to the library | Modify |
| `tests/shape/tst_canonjson_diff_merge.cpp` | Unit tests for the reusable differ/merger | **Create** |
| `tests/contacts/tst_contacts_canon_roundtrip.cpp` | vCard↔canon round-trip + loss tests | **Create** |
| `tests/todo/tst_todo_canon_roundtrip.cpp` | VTODO↔canon round-trip + loss tests | **Create** |
| `tests/calendar/tst_calendar_canon_roundtrip.cpp` | iCal↔canon round-trip + loss tests | **Create** |
| `docs/campaign/STATUS.md`, `docs/campaign/FINDINGS.md` | Status + the §10/STATUS reconciliation note | Modify (final task) |

---

## Conventions used in the per-domain tasks

**The field-mapping table is the spec, not a placeholder.** Each stage task carries an exhaustive table mapping every canon `PropertyId` ↔ its vendor representation ↔ the `LossKind` charged on the `canon → peer` direction. The implementer maps **every** row; the round-trip test enumerates the same fields as the executable acceptance check. "Map the remaining fields per the table" is therefore a complete instruction (the table lists them all), not a "similar to above" hand-wave.

**Canon envelope (all domains, from schema §1.2):** every canon JSON object has `_canon: { domain, v: 1 }`, a required `uid` (mirrors `CanonicalRecord::recordId`), the domain PropertyIds, and an optional `providerExtras` object (schema §1.3). Unknown top-level keys are preserved verbatim on round-trip (schema §1.1). Recurrence (where present) is one `StringList` property holding verbatim RFC5545 lines; no stage in this plan parses it except as opaque text passthrough (invariant 3 — the `canon → Microsoft` parsing edge is not in scope).

---

# PART 0 — Shared canon foundation (Tasks 1–3)

## Task 1: Canon envelope helpers

**Files:**
- Create: `src/shape/canonenvelope.h`, `src/shape/canonenvelope.cpp`
- Test: `tests/shape/tst_canonjson_diff_merge.cpp` (created here; extended in Tasks 2–3)
- Modify: `CMakeLists.txt`, `tests/shape/CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `src/shape/canonenvelope.h`:

```cpp
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "propertycatalogue.h"

namespace Kalburator::Shape {

/// Helpers for the canon JSON envelope (schema doc §1.2):
///   { "_canon": {"domain","v"}, "uid": "...", <props...>, "providerExtras": {...} }
/// Canon records carry these bytes in CanonicalRecord::data. These helpers are
/// the ONLY place that knows the envelope key names, so stages/differ/merger
/// agree on them.
namespace CanonEnvelope {

constexpr int kCanonVersion = 1;
QString canonKey();          // "_canon"
QString uidKey();            // "uid"
QString providerExtrasKey(); // "providerExtras"

/// Parse bytes into a JSON object (empty object if bytes are not a JSON object).
QJsonObject parse(const QByteArray& bytes);

/// Serialize compactly (stable: QJsonDocument sorts object keys).
QByteArray serialize(const QJsonObject& obj);

/// Stamp the envelope: sets _canon={domain,v=kCanonVersion} and uid. Leaves
/// all other keys (props, providerExtras, unknown keys) untouched.
void stampEnvelope(QJsonObject& obj, const QString& domain, const QString& uid);

/// Read uid (empty if absent).
QString uid(const QJsonObject& obj);

/// Semantic equality of two JSON values: objects compare key-by-key
/// (order-independent), arrays element-wise in order, scalars by value.
/// (QJsonValue::operator== already provides exactly this.)
bool valuesEqual(const QJsonValue& a, const QJsonValue& b);

}  // namespace CanonEnvelope
}  // namespace Kalburator::Shape
```

- [ ] **Step 2: Write the failing test**

Create `tests/shape/tst_canonjson_diff_merge.cpp`:

```cpp
#include <QtTest>
#include <QJsonObject>

#include "canonenvelope.h"

using namespace Kalburator::Shape;

class TestCanonJsonDiffMerge : public QObject
{
    Q_OBJECT
private slots:
    void envelopeStampsCanonAndUid()
    {
        QJsonObject o;
        o.insert("summary", "hi");
        CanonEnvelope::stampEnvelope(o, "calendar", "evt-1");
        QCOMPARE(CanonEnvelope::uid(o), QString("evt-1"));
        QCOMPARE(o.value("_canon").toObject().value("domain").toString(), QString("calendar"));
        QCOMPARE(o.value("_canon").toObject().value("v").toInt(), 1);
        QCOMPARE(o.value("summary").toString(), QString("hi"));  // untouched
    }

    void parseSerializeRoundTripsUnknownKeys()
    {
        const QByteArray in = R"({"uid":"x","futureKey":{"a":1}})";
        QJsonObject o = CanonEnvelope::parse(in);
        QVERIFY(o.contains("futureKey"));  // unknown key retained
        QByteArray out = CanonEnvelope::serialize(o);
        QJsonObject o2 = CanonEnvelope::parse(out);
        QVERIFY(CanonEnvelope::valuesEqual(o.value("futureKey"), o2.value("futureKey")));
    }

    void valuesEqualIsKeyOrderIndependent()
    {
        QJsonObject a{{"x",1},{"y",2}};
        QJsonObject b{{"y",2},{"x",1}};
        QVERIFY(CanonEnvelope::valuesEqual(a, b));
    }
};

QTEST_MAIN(TestCanonJsonDiffMerge)
#include "tst_canonjson_diff_merge.moc"
```

- [ ] **Step 3: Register the test in CMake**

In `tests/shape/CMakeLists.txt`, find an existing shape unit-test registration (e.g. `tst_transformation_registry`) and mirror it for target `tst_canonjson_diff_merge` linking the same library (`WHOLE_ARCHIVE Kalburator::Sync` or the equivalent the siblings use).

- [ ] **Step 4: Run it; verify it FAILS to build** (`canonenvelope.h` not found / `stampEnvelope` undefined)

Run: `cmake -S /home/clinton/dev/libkalburator -B /home/clinton/dev/libkalburator/build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build /home/clinton/dev/libkalburator/build --target tst_canonjson_diff_merge`
Expected: FAIL (no such file / undefined symbols).

- [ ] **Step 5: Implement `canonenvelope.cpp`**

Create `src/shape/canonenvelope.cpp`:

```cpp
#include "canonenvelope.h"

#include <QJsonDocument>

namespace Kalburator::Shape::CanonEnvelope {

QString canonKey()          { return QStringLiteral("_canon"); }
QString uidKey()            { return QStringLiteral("uid"); }
QString providerExtrasKey() { return QStringLiteral("providerExtras"); }

QJsonObject parse(const QByteArray& bytes)
{
    return QJsonDocument::fromJson(bytes).object();
}

QByteArray serialize(const QJsonObject& obj)
{
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

void stampEnvelope(QJsonObject& obj, const QString& domain, const QString& uid)
{
    QJsonObject canon;
    canon.insert(QStringLiteral("domain"), domain);
    canon.insert(QStringLiteral("v"), kCanonVersion);
    obj.insert(canonKey(), canon);
    obj.insert(uidKey(), uid);
}

QString uid(const QJsonObject& obj)
{
    return obj.value(uidKey()).toString();
}

bool valuesEqual(const QJsonValue& a, const QJsonValue& b)
{
    return a == b;  // QJsonValue::operator== is recursive, key-order-independent
}

}  // namespace Kalburator::Shape::CanonEnvelope
```

- [ ] **Step 6: Add `canonenvelope.cpp` to the build**

In `CMakeLists.txt`, add `src/shape/canonenvelope.cpp` to the shape sources list, immediately after `src/shape/shaperegistries.cpp`.

- [ ] **Step 7: Build + run; verify PASS**

Run: `cmake --build /home/clinton/dev/libkalburator/build --target tst_canonjson_diff_merge && ctest --test-dir /home/clinton/dev/libkalburator/build -R tst_canonjson_diff_merge -VV`
Expected: PASS (3 cases).

- [ ] **Step 8: Commit**

```bash
git add src/shape/canonenvelope.h src/shape/canonenvelope.cpp \
        tests/shape/tst_canonjson_diff_merge.cpp tests/shape/CMakeLists.txt CMakeLists.txt
git commit -m "shape: add canon JSON envelope helpers (Plan 3 Task 1)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 2: Reusable `CanonJsonDiffer`

Implements `RecordDiffer` over canon JSON, parameterized by the list of canon `PropertyId`s to compare (locked decision 6: coarse, one `PropertyId` per row; ignore `_canon` and `providerExtras`; treat composites and `recurrence` as whole opaque fields — schema §5).

**Files:**
- Create: `src/shape/canonjsondiffer.h`, `src/shape/canonjsondiffer.cpp`
- Modify: `tests/shape/tst_canonjson_diff_merge.cpp`, `CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `src/shape/canonjsondiffer.h`:

```cpp
#pragma once

#include <QList>

#include "propertycatalogue.h"
#include "recorddiffer.h"

namespace Kalburator::Shape {

/// RecordDiffer for canon JSON records. Coarse, one PropertyId per row
/// (STATUS decision 6): a change anywhere inside a composite (Json) property
/// marks the whole property changed. Ignores the `_canon` envelope and
/// `providerExtras` (schema §5: providerExtras is never a conflict axis).
/// Compares only the PropertyIds it is constructed with (the domain's canon
/// catalogue property ids).
class CanonJsonDiffer : public RecordDiffer {
public:
    explicit CanonJsonDiffer(QList<PropertyId> properties);

    QSet<PropertyId> diff(const CanonicalRecord& source,
                          const CanonicalRecord& baseline) const override;
    bool equal(const CanonicalRecord& a, const CanonicalRecord& b) const override;

private:
    QList<PropertyId> m_properties;
};

}  // namespace Kalburator::Shape
```

- [ ] **Step 2: Add failing tests to `tst_canonjson_diff_merge.cpp`**

Add `#include "canonjsondiffer.h"` and these slots:

```cpp
    void differMarksChangedPropertyOnly()
    {
        CanonJsonDiffer d({ PropertyId{"summary"}, PropertyId{"location"} });
        CanonicalRecord src; src.data = R"({"summary":"new","location":"home"})";
        CanonicalRecord base; base.data = R"({"summary":"old","location":"home"})";
        const QSet<PropertyId> changed = d.diff(src, base);
        QVERIFY(changed.contains(PropertyId{"summary"}));
        QVERIFY(!changed.contains(PropertyId{"location"}));
    }

    void differIgnoresProviderExtrasAndCanon()
    {
        CanonJsonDiffer d({ PropertyId{"summary"} });
        CanonicalRecord src; src.data = R"({"summary":"x","providerExtras":{"x-ms":{"a":1}},"_canon":{"v":1}})";
        CanonicalRecord base; base.data = R"({"summary":"x","providerExtras":{"x-ms":{"a":2}},"_canon":{"v":1}})";
        QVERIFY(d.diff(src, base).isEmpty());   // providerExtras change does not register
        QVERIFY(d.equal(src, base));
    }

    void differTreatsCompositeAsWhole()
    {
        CanonJsonDiffer d({ PropertyId{"attendees"} });
        CanonicalRecord src; src.data = R"({"attendees":[{"email":"a@x"},{"email":"b@x"}]})";
        CanonicalRecord base; base.data = R"({"attendees":[{"email":"a@x"}]})";
        QVERIFY(d.diff(src, base).contains(PropertyId{"attendees"}));
    }
```

- [ ] **Step 3: Run; verify FAIL** (undefined `CanonJsonDiffer`)

Run: `cmake --build /home/clinton/dev/libkalburator/build --target tst_canonjson_diff_merge`
Expected: FAIL (link/compile error).

- [ ] **Step 4: Implement `canonjsondiffer.cpp`**

Create `src/shape/canonjsondiffer.cpp`:

```cpp
#include "canonjsondiffer.h"

#include <QJsonObject>

#include "canonenvelope.h"

namespace Kalburator::Shape {

CanonJsonDiffer::CanonJsonDiffer(QList<PropertyId> properties)
    : m_properties(std::move(properties))
{
}

QSet<PropertyId> CanonJsonDiffer::diff(const CanonicalRecord& source,
                                       const CanonicalRecord& baseline) const
{
    const QJsonObject s = CanonEnvelope::parse(source.data);
    const QJsonObject b = CanonEnvelope::parse(baseline.data);
    QSet<PropertyId> changed;
    for (const PropertyId& id : m_properties) {
        const QString k = id.toString();
        if (!CanonEnvelope::valuesEqual(s.value(k), b.value(k)))
            changed.insert(id);
    }
    return changed;
}

bool CanonJsonDiffer::equal(const CanonicalRecord& a, const CanonicalRecord& b) const
{
    const QJsonObject ja = CanonEnvelope::parse(a.data);
    const QJsonObject jb = CanonEnvelope::parse(b.data);
    for (const PropertyId& id : m_properties) {
        const QString k = id.toString();
        if (!CanonEnvelope::valuesEqual(ja.value(k), jb.value(k)))
            return false;
    }
    return true;
}

}  // namespace Kalburator::Shape
```

- [ ] **Step 5: Add to build, build, run; verify PASS**

Add `src/shape/canonjsondiffer.cpp` to `CMakeLists.txt` shape sources.
Run: `cmake --build /home/clinton/dev/libkalburator/build --target tst_canonjson_diff_merge && ctest --test-dir /home/clinton/dev/libkalburator/build -R tst_canonjson_diff_merge`
Expected: PASS (all cases).

- [ ] **Step 6: Commit**

```bash
git add src/shape/canonjsondiffer.h src/shape/canonjsondiffer.cpp \
        tests/shape/tst_canonjson_diff_merge.cpp CMakeLists.txt
git commit -m "shape: add reusable coarse canon-JSON RecordDiffer (Plan 3 Task 2)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 3: Reusable `CanonJsonMerger`

Implements `RecordMerger` over canon JSON: per-`PropertyId` 3-way merge mirroring the existing per-domain mergers' `ConflictPolicy` usage; `providerExtras` follows the chosen record's origin (never merged, never a conflict axis — schema §5); `recurrence` merged whole (opaque).

**Files:**
- Create: `src/shape/canonjsonmerger.h`, `src/shape/canonjsonmerger.cpp`
- Modify: `tests/shape/tst_canonjson_diff_merge.cpp`, `CMakeLists.txt`

- [ ] **Step 1: Read the existing merger to copy the exact `ConflictPolicy` usage**

Read `src/contacts/vcardmerger.cpp` (`RecordMergerVCard::merge`) and `src/shape/recordmerger.h`. Note exactly how it: (a) decides per-field whether source/target/baseline wins (`source==baseline ⇒ take target`; `target==baseline ⇒ take source`; both-changed ⇒ consult `policy`), and (b) the concrete `Kalburator::Conflict::ConflictPolicy` calls it makes (method names/enums). The canon merger must use the **same** policy API so behavior matches the contract the engine and `tests/contacts`/`tests/calendar` conflict tests already pin. Write down the exact policy method(s) used; you will mirror them in Step 4.

- [ ] **Step 2: Write the header**

Create `src/shape/canonjsonmerger.h`:

```cpp
#pragma once

#include <QList>

#include "propertycatalogue.h"
#include "recordmerger.h"

namespace Kalburator::Shape {

/// RecordMerger for canon JSON records. Per-PropertyId 3-way merge over the
/// domain's canon catalogue property ids (coarse; STATUS decision 6).
/// `providerExtras` follows the merge result's origin record rather than being
/// three-way merged (schema §5: never a conflict axis). `recurrence`, like any
/// composite, is merged as one opaque whole.
class CanonJsonMerger : public RecordMerger {
public:
    /// `domain` is the canon domain string for the merged record's envelope.
    CanonJsonMerger(QString domain, QList<PropertyId> properties);

    CanonicalRecord merge(const CanonicalRecord& source,
                          const CanonicalRecord& target,
                          const CanonicalRecord& baseline,
                          const Kalburator::Conflict::ConflictPolicy& policy) const override;

private:
    QString m_domain;
    QList<PropertyId> m_properties;
};

}  // namespace Kalburator::Shape
```

- [ ] **Step 3: Add failing tests to `tst_canonjson_diff_merge.cpp`**

Add `#include "canonjsonmerger.h"` and `#include "conflictpolicy.h"` (confirm the include path from `recordmerger.h`'s include of `Kalburator::Conflict::ConflictPolicy`). Add slots that pin the three non-conflict cases plus providerExtras-follows-origin. Use the same `ConflictPolicy` construction the existing merger tests use (read `tests/contacts/` conflict tests for the exact construction). Example structure:

```cpp
    void mergerTakesSourceWhenTargetUnchanged()
    {
        CanonJsonMerger m("calendar", { PropertyId{"summary"} });
        CanonicalRecord src;  src.data  = R"({"uid":"e","summary":"edited"})"; src.recordId = "e";
        CanonicalRecord tgt;  tgt.data  = R"({"uid":"e","summary":"base"})";   tgt.recordId = "e";
        CanonicalRecord base; base.data = R"({"uid":"e","summary":"base"})";   base.recordId = "e";
        const CanonicalRecord out = m.merge(src, tgt, base, /* policy as used by existing tests */);
        QJsonObject o = QJsonDocument::fromJson(out.data).object();
        QCOMPARE(o.value("summary").toString(), QString("edited"));
    }

    void mergerKeepsProviderExtrasFromChosenOrigin()
    {
        CanonJsonMerger m("calendar", { PropertyId{"summary"} });
        CanonicalRecord src;  src.data  = R"({"uid":"e","summary":"edited","providerExtras":{"x":1}})"; src.recordId="e";
        CanonicalRecord tgt;  tgt.data  = R"({"uid":"e","summary":"base","providerExtras":{"x":2}})";   tgt.recordId="e";
        CanonicalRecord base; base.data = R"({"uid":"e","summary":"base","providerExtras":{"x":2}})";   base.recordId="e";
        const CanonicalRecord out = m.merge(src, tgt, base, /* policy */);
        QJsonObject o = QJsonDocument::fromJson(out.data).object();
        QCOMPARE(o.value("providerExtras").toObject().value("x").toInt(), 1); // followed source (the changed origin)
    }
```

Add `#include <QJsonDocument>` to the test.

- [ ] **Step 4: Run (FAIL), then implement `canonjsonmerger.cpp`**

Implement per-property 3-way using the policy API recorded in Step 1. Skeleton (fill the conflict branch with the exact policy call from Step 1):

```cpp
#include "canonjsonmerger.h"

#include <QJsonObject>

#include "canonenvelope.h"

namespace Kalburator::Shape {

CanonJsonMerger::CanonJsonMerger(QString domain, QList<PropertyId> properties)
    : m_domain(std::move(domain)), m_properties(std::move(properties)) {}

CanonicalRecord CanonJsonMerger::merge(const CanonicalRecord& source,
                                       const CanonicalRecord& target,
                                       const CanonicalRecord& baseline,
                                       const Kalburator::Conflict::ConflictPolicy& policy) const
{
    const QJsonObject s = CanonEnvelope::parse(source.data);
    const QJsonObject t = CanonEnvelope::parse(target.data);
    const QJsonObject b = CanonEnvelope::parse(baseline.data);

    QJsonObject out = t;  // start from target; override per-property below
    bool tookSourceForAny = false;

    for (const PropertyId& id : m_properties) {
        const QString k = id.toString();
        const bool srcChanged = !CanonEnvelope::valuesEqual(s.value(k), b.value(k));
        const bool tgtChanged = !CanonEnvelope::valuesEqual(t.value(k), b.value(k));

        if (srcChanged && !tgtChanged) {
            out.insert(k, s.value(k));            // take source
            tookSourceForAny = true;
        } else if (!srcChanged && tgtChanged) {
            /* keep target (already in out) */
        } else if (srcChanged && tgtChanged) {
            // CONFLICT — use the SAME policy decision the existing per-domain
            // mergers make (recorded in Step 1). Replace the next line with the
            // exact policy call/branch from vcardmerger.cpp.
            const bool takeSource = /* policy decision per Step 1 */;
            if (takeSource) { out.insert(k, s.value(k)); tookSourceForAny = true; }
        }
        // else: neither changed — keep target value.
    }

    // providerExtras follows the chosen origin (schema §5): if any property took
    // source, the record's provenance is source; else target.
    const QString peKey = CanonEnvelope::providerExtrasKey();
    const QJsonObject& origin = tookSourceForAny ? s : t;
    if (origin.contains(peKey))
        out.insert(peKey, origin.value(peKey));

    const QString mergedUid = CanonEnvelope::uid(t).isEmpty()
                                  ? CanonEnvelope::uid(s) : CanonEnvelope::uid(t);
    CanonEnvelope::stampEnvelope(out, m_domain, mergedUid);

    CanonicalRecord merged = target;
    merged.data = CanonEnvelope::serialize(out);
    return merged;
}

}  // namespace Kalburator::Shape
```

The only place to fill is the `takeSource` conflict decision — replace the `/* policy decision per Step 1 */` comment with the exact `ConflictPolicy` call recorded in Step 1. Run `cmake --build ... --target tst_canonjson_diff_merge` (FAIL first — the conflict branch is incomplete), complete the policy branch, rebuild, and run `ctest -R tst_canonjson_diff_merge`. Expected: PASS.

- [ ] **Step 5: Add to build + commit**

Add `src/shape/canonjsonmerger.cpp` to `CMakeLists.txt`.
```bash
git add src/shape/canonjsonmerger.h src/shape/canonjsonmerger.cpp \
        tests/shape/tst_canonjson_diff_merge.cpp CMakeLists.txt
git commit -m "shape: add reusable canon-JSON RecordMerger (Plan 3 Task 3)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

# PART A — `contacts+canon` (the worked template)

Contacts is the cleanest domain (no transcoding) and already has the peer pattern (`vcard3↔vcard4`). Part A establishes `contacts+canon` as the head, demotes `vcard4` to a peer (keeping `vcard3`), and proves the full pattern other domains follow.

## Task A1: Contacts canon catalogue

**Files:**
- Create: `src/contacts/contactscanonproperties.h`, `src/contacts/contactscanonproperties.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Header**

Create `src/contacts/contactscanonproperties.h`:

```cpp
#pragma once

#include <QList>

#include "propertycatalogue.h"

namespace Kalburator::Contacts {

/// The contacts+canon catalogue: vCard4 ∪ Google People (schema doc §3).
Kalburator::Shape::PropertyCatalogue makeContactsCanonCatalogue();

/// The property ids of the canon catalogue (for the differ/merger). Excludes
/// `_canon` and `providerExtras` (handled specially by the envelope).
QList<Kalburator::Shape::PropertyId> contactsCanonPropertyIds();

}  // namespace Kalburator::Contacts
```

- [ ] **Step 2: Implementation — enumerate every schema §3 PropertyId**

Create `src/contacts/contactscanonproperties.cpp`. Map schema §3 exactly. Each row below is `{ PropertyId, PropertyKind, displayName, optional }`; `uid` is required (`optional=false`), all others optional. Composites use `PropertyKind::Json`.

| PropertyId | Kind | PropertyId | Kind |
|---|---|---|---|
| `uid` | String (req) | `names` | Json |
| `nicknames` | Json | `emails` | Json |
| `phones` | Json | `addresses` | Json |
| `organizations` | Json | `occupations` | StringList |
| `urls` | Json | `imClients` | Json |
| `sipAddresses` | StringList | `calendarUrls` | Json |
| `relations` | Json | `birthday` | Json |
| `anniversary` | Json | `significantDates` | Json |
| `gender` | Json | `notes` | String |
| `photos` | Json | `categories` | StringList |
| `languages` | StringList | `timeZone` | String |
| `externalIds` | Json | `memberships` | Json |
| `interests` | StringList | `skills` | StringList |

```cpp
#include "contactscanonproperties.h"

using namespace Kalburator::Shape;

namespace Kalburator::Contacts {

Kalburator::Shape::PropertyCatalogue makeContactsCanonCatalogue()
{
    PropertyCatalogue cat;
    cat.addProperty({ PropertyId{"uid"}, PropertyKind::String, QStringLiteral("UID"), false });
    cat.addProperty({ PropertyId{"names"}, PropertyKind::Json, QStringLiteral("Names") });
    cat.addProperty({ PropertyId{"nicknames"}, PropertyKind::Json, QStringLiteral("Nicknames") });
    cat.addProperty({ PropertyId{"emails"}, PropertyKind::Json, QStringLiteral("Emails") });
    cat.addProperty({ PropertyId{"phones"}, PropertyKind::Json, QStringLiteral("Phones") });
    cat.addProperty({ PropertyId{"addresses"}, PropertyKind::Json, QStringLiteral("Addresses") });
    cat.addProperty({ PropertyId{"organizations"}, PropertyKind::Json, QStringLiteral("Organizations") });
    cat.addProperty({ PropertyId{"occupations"}, PropertyKind::StringList, QStringLiteral("Occupations") });
    cat.addProperty({ PropertyId{"urls"}, PropertyKind::Json, QStringLiteral("URLs") });
    cat.addProperty({ PropertyId{"imClients"}, PropertyKind::Json, QStringLiteral("IM Clients") });
    cat.addProperty({ PropertyId{"sipAddresses"}, PropertyKind::StringList, QStringLiteral("SIP Addresses") });
    cat.addProperty({ PropertyId{"calendarUrls"}, PropertyKind::Json, QStringLiteral("Calendar URLs") });
    cat.addProperty({ PropertyId{"relations"}, PropertyKind::Json, QStringLiteral("Relations") });
    cat.addProperty({ PropertyId{"birthday"}, PropertyKind::Json, QStringLiteral("Birthday") });
    cat.addProperty({ PropertyId{"anniversary"}, PropertyKind::Json, QStringLiteral("Anniversary") });
    cat.addProperty({ PropertyId{"significantDates"}, PropertyKind::Json, QStringLiteral("Significant Dates") });
    cat.addProperty({ PropertyId{"gender"}, PropertyKind::Json, QStringLiteral("Gender") });
    cat.addProperty({ PropertyId{"notes"}, PropertyKind::String, QStringLiteral("Notes") });
    cat.addProperty({ PropertyId{"photos"}, PropertyKind::Json, QStringLiteral("Photos") });
    cat.addProperty({ PropertyId{"categories"}, PropertyKind::StringList, QStringLiteral("Categories") });
    cat.addProperty({ PropertyId{"languages"}, PropertyKind::StringList, QStringLiteral("Languages") });
    cat.addProperty({ PropertyId{"timeZone"}, PropertyKind::String, QStringLiteral("Time Zone") });
    cat.addProperty({ PropertyId{"externalIds"}, PropertyKind::Json, QStringLiteral("External IDs") });
    cat.addProperty({ PropertyId{"memberships"}, PropertyKind::Json, QStringLiteral("Memberships") });
    cat.addProperty({ PropertyId{"interests"}, PropertyKind::StringList, QStringLiteral("Interests") });
    cat.addProperty({ PropertyId{"skills"}, PropertyKind::StringList, QStringLiteral("Skills") });
    return cat;
}

QList<Kalburator::Shape::PropertyId> contactsCanonPropertyIds()
{
    QList<PropertyId> ids;
    for (const auto& d : makeContactsCanonCatalogue().properties())
        ids.append(d.id);
    return ids;
}

}  // namespace Kalburator::Contacts
```

- [ ] **Step 3: Add to build + commit**

Add `src/contacts/contactscanonproperties.cpp` to `CMakeLists.txt`. Build the library (`cmake --build .../build`) to confirm it compiles. No behavior change yet.

```bash
git add src/contacts/contactscanonproperties.h src/contacts/contactscanonproperties.cpp CMakeLists.txt
git commit -m "contacts: add contacts+canon catalogue (Plan 3 Task A1)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

## Task A2: `VCard4ToCanonStage` (vcard4 → canon JSON, lossless)

Parses a vCard4 string (via `KContacts::VCardConverter`/`Addressee`, as `vcarddiffer.cpp` does) and emits canon JSON. This is lossless: every vCard4 field maps into the richer canon. Vendor `UID` → both `uid` and stashed in `providerExtras["x-vcard"]["uid"]` per STATUS decision 7 (canon mints/normalizes `uid`).

**Files:** Create `src/contacts/vcardcanonstages.h`, `.cpp`; Test `tests/contacts/tst_contacts_canon_roundtrip.cpp`; Modify `CMakeLists.txt`, `tests/contacts/CMakeLists.txt`.

**Mapping table (vCard4 property → canon PropertyId). Map every row.**

| vCard4 | canon PropertyId | Notes |
|---|---|---|
| `UID` | `uid` (+ `providerExtras["x-vcard"]["uid"]`) | mint if absent (use record id) |
| `N` / `FN` | `names` | `[{ given, family, middle, prefix, suffix, formatted }]` |
| `NICKNAME` | `nicknames` | `[{ value }]` |
| `EMAIL` (+ `TYPE`,`PREF`) | `emails` | `[{ value, type?, primary }]` |
| `TEL` (+ `TYPE`,`PREF`) | `phones` | `[{ value, type?, primary }]` |
| `ADR` (+ `TYPE`) | `addresses` | `[{ street, locality, region, postalCode, country, type? }]` |
| `ORG`,`TITLE`,`ROLE` | `organizations` | `[{ name, title, role, department }]` |
| `URL` | `urls` | `[{ value, type? }]` |
| `IMPP` | `imClients` | `[{ value, protocol? }]` |
| `BDAY` | `birthday` | `{ date, hasYear }` |
| `ANNIVERSARY` | `anniversary` | `{ date, hasYear }` |
| `GENDER` | `gender` | `{ sex, identity? }` |
| `NOTE` | `notes` | String |
| `PHOTO` | `photos` | `[{ data(base64)|url, mediaType? }]` |
| `CATEGORIES` | `categories` | StringList |
| `LANG` | `languages` | StringList |
| `TZ` | `timeZone` | String |
| `RELATED` | `relations` | `[{ value, type? }]` |
| `MEMBER` | `memberships` | `[{ value }]` |
| any `X-*` unmapped | `providerExtras["x-vcard"]` | verbatim (Reversible carrier) |
| (Google-only fields: `occupations`,`sipAddresses`,`calendarUrls`,`significantDates`,`externalIds`,`interests`,`skills`) | absent from vCard4 input → omit | populated only by a future Google peer |

- [ ] **Step 1: Header**

```cpp
#pragma once
#include "transformationedge.h"
namespace Kalburator::Contacts {
class VCard4ToCanonStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& vcardBytes) const override;
};
class CanonToVCard4Stage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& canonBytes) const override;
};
}  // namespace Kalburator::Contacts
```

- [ ] **Step 2: Write failing round-trip test** (created here; CanonToVCard4Stage lands in A3)

Create `tests/contacts/tst_contacts_canon_roundtrip.cpp` with a `vcard4ToCanonExtractsCoreFields` slot that feeds a known vCard4 string (FN/N/EMAIL;TYPE=work/TEL/ORG/CATEGORIES) through `VCard4ToCanonStage`, parses the result with `CanonEnvelope::parse`, and asserts: `uid` present, `names[0].formatted` matches FN, `emails[0].value`+`type`, `phones[0].value`, `organizations[0].name`, `categories` list. Register the test target in `tests/contacts/CMakeLists.txt` mirroring `tst_vcard3_vcard4_edge`.

- [ ] **Step 3: Run (FAIL), implement `VCard4ToCanonStage::transform`** per the mapping table using `KContacts::VCardConverter::parseVCard` → `Addressee` getters, building a `QJsonObject`, stamping the envelope (`CanonEnvelope::stampEnvelope`), serializing. Stash unmapped `X-` props into `providerExtras["x-vcard"]`. Run; PASS.

- [ ] **Step 4: Add sources to build + commit**

```bash
git add src/contacts/vcardcanonstages.h src/contacts/vcardcanonstages.cpp \
        tests/contacts/tst_contacts_canon_roundtrip.cpp tests/contacts/CMakeLists.txt CMakeLists.txt
git commit -m "contacts: add VCard4ToCanonStage (vcard4 -> canon JSON) (Plan 3 Task A2)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

## Task A3: `CanonToVCard4Stage` (canon → vcard4, with loss) + round-trip

Emits vCard4 from canon JSON. Charges loss on Google-only fields vCard4 cannot hold. Per the four-kind model:

| canon PropertyId | vCard4 target | LossKind on `canon→vcard4` |
|---|---|---|
| names, emails, phones, addresses, organizations, urls, imClients, birthday, anniversary, gender, notes, photos, categories, languages, timeZone, relations, memberships, nicknames | native vCard4 props | none (lossless) |
| `sipAddresses` | `IMPP` (sip:) or `X-SIP` | Reversible (stash in `X-`/providerExtras) |
| `occupations` | none | Dropped |
| `calendarUrls` | `CALURI`/`FBURL` if present else `X-` | Reversible |
| `significantDates` | none | Dropped |
| `externalIds` | none (kept in providerExtras) | Reversible |
| `interests`, `skills` | none | Dropped |
| `providerExtras["x-vcard"]` | re-emit verbatim as `X-` props | none (round-trip carrier) |

- [ ] **Step 1: Add failing tests** to `tst_contacts_canon_roundtrip.cpp`:
  - `vcard4RoundTripPreservesCoreFields`: vcard4 → canon (`VCard4ToCanonStage`) → vcard4 (`CanonToVCard4Stage`); parse both vCards via `KContacts` and assert FN/N/EMAIL/TEL/ORG/CATEGORIES equal.
  - `canonToVcard4ChargesLossForGoogleOnlyFields`: build a canon object with `occupations` and `interests` set, run `CanonToVCard4Stage`, and (since the stage is byte-only) assert via the **edge's** LossProfile in Task A5 — here just assert the vCard output omits them. (The LossProfile assertion is in A5.)

- [ ] **Step 2: Run (FAIL), implement `CanonToVCard4Stage::transform`** per the table: parse canon JSON, populate a `KContacts::Addressee`, emit via `VCardConverter::createVCard(..., VCardConverter::v4_0)`. Re-emit `providerExtras["x-vcard"]` entries as `X-` properties. Run; PASS round-trip.

- [ ] **Step 3: Commit**

```bash
git add src/contacts/vcardcanonstages.h src/contacts/vcardcanonstages.cpp \
        tests/contacts/tst_contacts_canon_roundtrip.cpp
git commit -m "contacts: add CanonToVCard4Stage + round-trip test (Plan 3 Task A3)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

## Task A4: Flip `ContactsDomainDefinition` to the canon head

**Files:** Modify `src/contacts/contactsdomaindefinition.cpp` (and `.h` if includes needed).

- [ ] **Step 1: Read the current file** to see exact include/method style.

- [ ] **Step 2: Change the four methods.** `canonicalShape()` returns `{ DomainId{"contacts"}, EncodingId{"canon"} }`; `canonicalCatalogue()` returns `Kalburator::Contacts::makeContactsCanonCatalogue()`; `createCanonicalDiffer()` returns `std::make_unique<Kalburator::Shape::CanonJsonDiffer>(contactsCanonPropertyIds())`; `createCanonicalMerger()` returns `std::make_unique<Kalburator::Shape::CanonJsonMerger>("contacts", contactsCanonPropertyIds())`; `richnessRank(s)` returns `s == canonicalShape() ? 100 : (s.encoding == EncodingId{"vcard4"} ? 50 : 10)` (canon richest, vcard4 next, vcard3 least). Add includes: `contactscanonproperties.h`, `canonjsondiffer.h`, `canonjsonmerger.h`.

- [ ] **Step 3: Build the library + run the contacts UNIT/differ tests that don't go through the engine yet** (e.g. `tst_vcard3_vcard4_edge`, any contacts domain-def test): `cmake --build .../build && ctest --test-dir .../build -R "vcard3_vcard4_edge|contactsdomain"`. The engine-level contacts tests are validated in A6 after the bridges exist (A5). Expected: these compile and pass (differ now returns a `CanonJsonDiffer`, but no one routes to canon until A5 registers the bridges — so at this point the contacts plugin would register canon as canonical with NO edge from vcard4 → canon, which means the engine cannot route. **A5 must immediately follow A4; do not run the full contacts engine suite between A4 and A5.**). Commit.

```bash
git add src/contacts/contactsdomaindefinition.cpp src/contacts/contactsdomaindefinition.h
git commit -m "contacts: flip canonical head to contacts+canon with canon differ/merger (Plan 3 Task A4)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

## Task A5: Register peers + bridge edges in `ContactsStockShapes`

**Files:** Modify `src/contacts/contactsstockshapes.cpp` (and `.h`).

- [ ] **Step 1: Read the current file** (it returns `vcard3` peer + the `vcard4↔vcard4` identity / `vcard3↔vcard4` edges).

- [ ] **Step 2: Update `peerShapes()`** to register BOTH `vcard4` (now a peer) and `vcard3` as peers, each with its catalogue (`makeVCardCatalogue()` — confirm whether vcard3 needs a distinct catalogue; reuse what the file uses today). The canon shape itself is registered by the DomainDefinition (`pluginmanager.cpp` registers `canonicalShape()`+`canonicalCatalogue()`), so do **not** add canon to `peerShapes()`.

- [ ] **Step 3: Update `edges()`** to return (do NOT modify the existing `vcard3↔vcard4` edges — keep them so vcard3 reaches canon via vcard3→vcard4→canon):

```cpp
const Shape canon{ DomainId{"contacts"}, EncodingId{"canon"} };
const Shape v4{ DomainId{"contacts"}, EncodingId{"vcard4"} };
const Shape v3{ DomainId{"contacts"}, EncodingId{"vcard3"} };
return {
    { canon, canon, LossProfile{}, std::make_shared<Shape::IdentityStage>() },         // hub
    { v4, canon, LossProfile{}, std::make_shared<VCard4ToCanonStage>() },               // promote (lossless)
    { canon, v4, canonToVcard4Loss(), std::make_shared<CanonToVCard4Stage>() },         // demote (lossy)
    { v3, v4, LossProfile{}, std::make_shared<VCard3To4Stage>() },                      // existing, unchanged
    { v4, v3, vcard4ToVcard3Loss(), std::make_shared<VCard4To3Stage>() },               // existing, unchanged
};
```

Add a `canonToVcard4Loss()` free function in the `.cpp` building the `LossProfile` per the Task A3 table (insert `occupations`/`interests`/`skills` ⇒ `Dropped`; `sipAddresses`/`calendarUrls`/`externalIds` ⇒ `Reversible`). Add includes for `vcardcanonstages.h`, `lossprofile.h`.

- [ ] **Step 4: Add failing+passing edge tests** to `tst_contacts_canon_roundtrip.cpp` (these need a populated `ShapeRegistries` — build one with `PluginManager pm(&reg, m_shape); registerStockPlugins(pm);` as the Plan-2 isolation test does):
  - `vcard3RoutesToCanonViaTwoHops`: `m_shape.transformation.compile(v3, canon).has_value()` is true (vcard3→vcard4→canon).
  - `canonToVcard4LossProfileChargesGoogleOnlyFields`: `m_shape.transformation.inspect(canon, v4).affected.value(PropertyId{"occupations"}) == LossKind::Dropped`.

- [ ] **Step 5: Build + run the FULL contacts + engine-contacts suites** (the green-gate): `cmake --build .../build && ctest --test-dir .../build -R "contacts|vcard|tst_contacts_engine_witness|tst_unified_askuser_pause|tst_unified_custom_merge"`. Expected: all PASS. The engine now promotes backend `vcard4`/`vcard3` records to `contacts+canon`, diffs/merges with `CanonJson*`, and demotes on write. If a conflict test fails, re-check Task 3 Step 1 (policy mirroring) — the canon merger must match the prior merger's conflict decisions.

- [ ] **Step 6: Commit**

```bash
git add src/contacts/contactsstockshapes.cpp src/contacts/contactsstockshapes.h \
        tests/contacts/tst_contacts_canon_roundtrip.cpp
git commit -m "contacts: demote vcard4 to peer; register canon hub + bridges (Plan 3 Task A5)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

## Task A6: Contacts convergence green-gate

- [ ] **Step 1: Run the whole suite** to confirm no cross-domain regression: `ctest --test-dir /home/clinton/dev/libkalburator/build`. Expected: same green set as the Plan-2 baseline (107/108; `tst_providerlifecycle` is a known pre-existing flake, passes on isolated re-run) — now with the new contacts canon tests green. Record results. If any non-contacts test newly fails, STOP and investigate (it would indicate a shared-infra issue from Part 0).

- [ ] **Step 2: Commit** (no code change expected; if a fix was needed, commit it with a message naming the failure). Otherwise skip.

---

# PART B — `todo+canon`

Mirrors Part A. Todo's current canonical is `{todo, ical-vtodo}` (no other peer today). The carry-verbatim hierarchy (invariant P4): `relatedTo` (VTODO tree), `parentUid` (Google single-level), `checklistItems` (MS) are **three independent PropertyIds held side-by-side**; the VTODO stage populates only `relatedTo`; the differ treats each as one coarse opaque field; no stage derives one from another.

## Task B1: Todo canon catalogue

**Files:** Create `src/todo/todocanonproperties.{h,cpp}`; Modify `CMakeLists.txt`.

- [ ] **Step 1–2:** Create `makeTodoCanonCatalogue()` + `todoCanonPropertyIds()` (same shape as Task A1), enumerating schema §4 exactly:

| PropertyId | Kind | PropertyId | Kind |
|---|---|---|---|
| `uid` String (req) | | `created` DateTime | |
| `lastModified` DateTime | | `summary` String | |
| `description` String | | `descriptionHtml` String | |
| `status` String | | `percentComplete` Integer | |
| `priority` Integer | | `categories` StringList | |
| `start` Json | | `due` Json | |
| `completed` DateTime | | `recurrence` StringList | |
| `alarms` Json | | `location` String | |
| `geo` Json | | `sortOrder` String | |
| `relatedTo` Json | | `parentUid` String | |
| `checklistItems` Json | | `linkedResources` Json | |

- [ ] **Step 3:** Add to `CMakeLists.txt`, build, commit:
```bash
git add src/todo/todocanonproperties.h src/todo/todocanonproperties.cpp CMakeLists.txt
git commit -m "todo: add todo+canon catalogue (Plan 3 Task B1)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

## Task B2: `VTodoToCanonStage` (ical-vtodo → canon JSON, lossless)

**Files:** Create `src/todo/vtodocanonstages.{h,cpp}` (both stage classes, mirroring Task A2's header shape); Test `tests/todo/tst_todo_canon_roundtrip.cpp`; Modify CMake.

**Mapping table (VTODO property → canon PropertyId). Map every row.**

| VTODO | canon | Notes |
|---|---|---|
| `UID` | `uid` | mint if absent |
| `SUMMARY`/`DESCRIPTION` | `summary`/`description` | |
| `X-ALT-DESC` | `descriptionHtml` | Reversible carrier |
| `STATUS` | `status` | NEEDS-ACTION/IN-PROCESS/COMPLETED/CANCELLED |
| `PERCENT-COMPLETE` | `percentComplete` | Integer |
| `PRIORITY` | `priority` | Integer |
| `CATEGORIES` | `categories` | StringList |
| `DTSTART` | `start` | `{ date?|dateTime?, tz, floating }` |
| `DUE` | `due` | `{ …, precision }` |
| `COMPLETED` | `completed` | DateTime |
| `CREATED`/`LAST-MODIFIED` | `created`/`lastModified` | |
| `RRULE`/`RDATE`/`EXDATE` | `recurrence` | StringList, **verbatim lines** (invariant 3) |
| `VALARM` | `alarms` | Json array |
| `LOCATION`/`GEO` | `location`/`geo` | |
| `RELATED-TO` | `relatedTo` | `[{ uid, reltype }]` — VTODO hierarchy, verbatim (P4) |
| any `X-*` unmapped | `providerExtras["x-vtodo"]` | verbatim |
| (Google `position`→`sortOrder`, `parentUid`; MS `checklistItems`,`linkedResources`) | omit on VTODO input | populated only by future vendor peers |

- [ ] **Step 1:** Header (two stage classes). **Step 2:** failing `vtodoToCanonExtractsCoreFields` test (SUMMARY/STATUS/DUE/RELATED-TO). **Step 3:** implement using `KCalendarCore::ICalFormat` → `Todo::Ptr` getters, recurrence captured verbatim from the source lines (do NOT recompose from parsed parts). **Step 4:** build/run PASS; commit.

```bash
git add src/todo/vtodocanonstages.h src/todo/vtodocanonstages.cpp \
        tests/todo/tst_todo_canon_roundtrip.cpp tests/todo/CMakeLists.txt CMakeLists.txt
git commit -m "todo: add VTodoToCanonStage (ical-vtodo -> canon JSON) (Plan 3 Task B2)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

## Task B3: `CanonToVTodoStage` (canon → ical-vtodo, with loss) + round-trip

**Loss table on `canon→ical-vtodo`:**

| canon PropertyId | VTODO target | LossKind |
|---|---|---|
| uid, summary, description, status, percentComplete, priority, categories, start, due, completed, created, lastModified, recurrence, alarms, location, geo, relatedTo | native VTODO | none |
| `descriptionHtml` | `X-ALT-DESC` | Reversible |
| `status` value `waitingOnOthers`/`deferred` | `NEEDS-ACTION` + keep original in `providerExtras` | Degraded (original kept verbatim) |
| `parentUid` | `RELATED-TO;RELTYPE=PARENT` if not already in relatedTo, else drop | Reversible |
| `checklistItems` | none (kept in providerExtras) | Reversible |
| `linkedResources` | none | Dropped |
| `sortOrder` | none (kept in providerExtras) | Reversible |

- [ ] **Step 1:** failing `vtodoRoundTripPreservesCoreFieldsAndRecurrence` (assert SUMMARY/STATUS/DUE/RELATED-TO and **recurrence lines byte-identical** after vtodo→canon→vtodo). **Step 2:** implement per table (status downgrade keeps original verbatim in `providerExtras["x-vtodo"]["status"]`). **Step 3:** build/run PASS; commit.

```bash
git add src/todo/vtodocanonstages.h src/todo/vtodocanonstages.cpp tests/todo/tst_todo_canon_roundtrip.cpp
git commit -m "todo: add CanonToVTodoStage + round-trip test (Plan 3 Task B3)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

## Task B4: Flip `TodoDomainDefinition` to canon head

- [ ] Mirror Task A4 on `src/todo/tododomaindefinition.cpp`: `canonicalShape()` → `{todo, canon}`; `canonicalCatalogue()` → `makeTodoCanonCatalogue()`; differ/merger → `CanonJsonDiffer(todoCanonPropertyIds())` / `CanonJsonMerger("todo", todoCanonPropertyIds())`; `richnessRank(s)` → `s == canonicalShape() ? 100 : 10`. Add includes. Build library; **B5 must immediately follow** (no engine-todo run between B4 and B5). Commit.

```bash
git add src/todo/tododomaindefinition.cpp src/todo/tododomaindefinition.h
git commit -m "todo: flip canonical head to todo+canon with canon differ/merger (Plan 3 Task B4)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

## Task B5: Register peer + bridges in `TodoStockShapes`

- [ ] Mirror Task A5 on `src/todo/todostockshapes.cpp`: `peerShapes()` registers `{todo, ical-vtodo}` with its catalogue; `edges()` returns canon-hub identity, `ical-vtodo→canon` (lossless, `VTodoToCanonStage`), `canon→ical-vtodo` (`canonToVtodoLoss()`, `CanonToVTodoStage`). Add `canonToVtodoLoss()` per the B3 table. Add edge/loss tests to `tst_todo_canon_roundtrip.cpp` (compile `ical-vtodo→canon`; inspect loss charges `linkedResources` Dropped, `descriptionHtml` Reversible). Build + run the full todo + engine-todo suite (green-gate): `ctest -R "vtodo|todo"`. Commit.

```bash
git add src/todo/todostockshapes.cpp src/todo/todostockshapes.h tests/todo/tst_todo_canon_roundtrip.cpp
git commit -m "todo: demote ical-vtodo to peer; register canon hub + bridges (Plan 3 Task B5)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

## Task B6: Todo convergence green-gate

- [ ] Run the whole suite (`ctest --test-dir .../build`); confirm baseline-green plus new todo canon tests. STOP+investigate any non-todo regression. Commit only if a fix was needed.

---

# PART C — `calendar+canon`

Mirrors Parts A/B. Calendar's current canonical is `{calendar, ical}`. Transcoding is dormant in the default build (empty plans), so introducing the canon head touches only the differ/merger byte-format and the `canon→ical` write-stage — both landed here. **This plan does NOT retire `src/transcoding/`, touch `ApplyContext.transcodingPlan`, or change `CalendarPluginWriter`'s plan handling (Plan 4).** The `canon→ical` stage must emit valid iCal so the existing writer (`CalendarPluginWriter`, which parses iCal via `parseIncidence`) still round-trips; the dormant transcoding plan then applies as a no-op.

## Task C1: Calendar canon catalogue

**Files:** Create `src/calendar/calendarcanonproperties.{h,cpp}`; Modify CMake.

- [ ] Create `makeCalendarCanonCatalogue()` + `calendarCanonPropertyIds()`, enumerating schema §2. Core rows (map every schema §2 PropertyId):

| PropertyId | Kind | PropertyId | Kind |
|---|---|---|---|
| `uid` String(req) | | `sequence` Integer | |
| `created`/`lastModified` DateTime | | `summary`/`description` String | |
| `descriptionHtml` String | | `location` String | |
| `locations` Json | | `status` String | |
| `classification` String | | `timeTransparency` String | |
| `freeBusyStatus` String | | `start`/`end` Json | |
| `allDay` Boolean | | `recurrence` StringList | |
| `recurrenceId` Json | | `recurrenceRange` String | |
| `color` String | | `categories` StringList | |
| `url` String | | `organizer` Json | |
| `attendees` Json | | `responseRequested` Boolean | |
| `priority` Integer | | `alarms` Json | |
| `onlineMeeting` Json | | `attachments` Json | |
| `eventType` String | | `typedProperties` Json | |
| `guestsCanModify`/`guestsCanInviteOthers`/`guestsCanSeeOtherGuests` Boolean | | `allowNewTimeProposals`/`hideAttendees`/`locked`/`privateCopy` Boolean | |

- [ ] Add to CMake, build, commit:
```bash
git add src/calendar/calendarcanonproperties.h src/calendar/calendarcanonproperties.cpp CMakeLists.txt
git commit -m "calendar: add calendar+canon catalogue (Plan 3 Task C1)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

## Task C2: `ICalToCanonStage` (ical → canon JSON, lossless)

**Files:** Create `src/calendar/icalcanonstages.{h,cpp}`; Test `tests/calendar/tst_calendar_canon_roundtrip.cpp`; Modify CMake.

**Mapping table (iCal VEVENT property → canon PropertyId). Map every row.** Use `KCalendarCore::ICalFormat`→`Event::Ptr`.

| iCal | canon | Notes |
|---|---|---|
| `UID` | `uid` | mint if absent |
| `SEQUENCE` | `sequence` | |
| `CREATED`/`LAST-MODIFIED` | `created`/`lastModified` | |
| `SUMMARY`/`DESCRIPTION` | `summary`/`description` | |
| `X-ALT-DESC` | `descriptionHtml` | Reversible carrier |
| `LOCATION` | `location` | |
| `STATUS` | `status` | |
| `CLASS` | `classification` | |
| `TRANSP` | `timeTransparency` | |
| `X-MICROSOFT-CDO-BUSYSTATUS` | `freeBusyStatus` | Reversible carrier |
| `DTSTART`(+TZID) | `start` | `{ date?|dateTime?, tz(verbatim IANA), floating }` |
| `DTEND`/`DURATION` | `end` | normalize duration→end; keep both representable |
| `X-...ALLDAY`/date-only DTSTART | `allDay` | Boolean |
| `RRULE`/`RDATE`/`EXDATE` | `recurrence` | StringList, **verbatim lines** (invariant 3) |
| `RECURRENCE-ID`(+RANGE) | `recurrenceId` / `recurrenceRange` | RANGE=THISANDFUTURE → `recurrenceRange="thisAndFuture"` (P4: override is a separate record sharing uid) |
| `COLOR` | `color` | |
| `CATEGORIES` | `categories` | StringList |
| `URL` | `url` | |
| `ORGANIZER` | `organizer` | `{ email, name }` |
| `ATTENDEE`(+ROLE,PARTSTAT,RSVP,CUTYPE) | `attendees` | `[{ email, name, role, partstat, rsvp }]` |
| `PRIORITY` | `priority` | |
| `VALARM` | `alarms` | Json array (VALARM superset) |
| `ATTACH` | `attachments` | Json |
| any `X-*` unmapped | `providerExtras["x-ical"]` | verbatim |
| (Google/MS-only: locations, onlineMeeting, eventType, typedProperties, guest* flags, allowNewTimeProposals, hideAttendees, locked, privateCopy, responseRequested) | omit on iCal input | future vendor peers |

- [ ] **Step 1:** Header (both stages). **Step 2:** failing `icalToCanonExtractsCoreFields` (UID/SUMMARY/DTSTART/DTEND/ATTENDEE/RRULE). **Step 3:** implement; recurrence captured verbatim. Register test target in `tests/calendar/CMakeLists.txt`. **Step 4:** build/run PASS; commit.

```bash
git add src/calendar/icalcanonstages.h src/calendar/icalcanonstages.cpp \
        tests/calendar/tst_calendar_canon_roundtrip.cpp tests/calendar/CMakeLists.txt CMakeLists.txt
git commit -m "calendar: add ICalToCanonStage (ical -> canon JSON) (Plan 3 Task C2)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

## Task C3: `CanonToICalStage` (canon → ical, with loss) + round-trip

**Loss table on `canon→ical`** (design §6):

| canon PropertyId | iCal target | LossKind |
|---|---|---|
| uid, sequence, created, lastModified, summary, description, location, status, classification, timeTransparency, start, end, allDay, recurrence, recurrenceId, color, categories, url, organizer, attendees, priority, alarms, attachments | native iCal | none |
| `descriptionHtml` | `X-ALT-DESC` | Reversible |
| `freeBusyStatus` oof/workingElsewhere | `X-MICROSOFT-CDO-BUSYSTATUS` | Reversible |
| `recurrenceRange` | `RANGE=THISANDFUTURE` on RECURRENCE-ID | none (native) |
| `onlineMeeting`, `eventType`, `typedProperties` | none | Dropped |
| `classification` value `personal` | `PRIVATE` | Degraded (MS personal→private; original in providerExtras) |
| `locations` (multi) | first → `LOCATION` | Simplified |
| guest* flags, allowNewTimeProposals, hideAttendees, locked, privateCopy, responseRequested | none (kept in providerExtras) | Reversible |
| `providerExtras["x-ical"]` | re-emit verbatim as `X-` | none |

- [ ] **Step 1:** failing `icalRoundTripPreservesCoreFieldsAndRecurrence` (UID/SUMMARY/DTSTART/DTEND/ATTENDEE preserved; **RRULE/RDATE/EXDATE lines byte-identical** ical→canon→ical). **Step 2:** implement per table; emit valid iCal via `ICalFormat::toString`; re-emit `providerExtras["x-ical"]` as `X-` props. **Step 3:** build/run PASS; commit.

```bash
git add src/calendar/icalcanonstages.h src/calendar/icalcanonstages.cpp tests/calendar/tst_calendar_canon_roundtrip.cpp
git commit -m "calendar: add CanonToICalStage + round-trip test (Plan 3 Task C3)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

## Task C4: Flip `CalendarDomainDefinition` to canon head

- [ ] Mirror A4/B4 on `src/calendar/calendardomaindefinition.cpp`: `canonicalShape()` → `{calendar, canon}`; `canonicalCatalogue()` → `makeCalendarCanonCatalogue()`; differ/merger → `CanonJsonDiffer(calendarCanonPropertyIds())` / `CanonJsonMerger("calendar", calendarCanonPropertyIds())`; `richnessRank(s)` → `s == canonicalShape() ? 100 : 10`; **keep `baselineProperties()` returning `{"color","description"}`** (still valid PropertyIds in the canon catalogue — confirm both are canon property ids; `color` and `description` are). Add includes. Build library; **C5 must immediately follow.** Commit.

```bash
git add src/calendar/calendardomaindefinition.cpp src/calendar/calendardomaindefinition.h
git commit -m "calendar: flip canonical head to calendar+canon with canon differ/merger (Plan 3 Task C4)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

## Task C5: Register peer + bridges in `CalendarStockShapes`

- [ ] Mirror A5/B5 on `src/calendar/calendarstockshapes.cpp`. Today it has empty `peerShapes()` and only an `ical→ical` identity edge — replace that: `peerShapes()` registers `{calendar, ical}` with `makeICalCatalogue()`; `edges()` returns canon-hub identity, `ical→canon` (lossless, `ICalToCanonStage`), `canon→ical` (`canonToIcalLoss()`, `CanonToICalStage`). Add `canonToIcalLoss()` per the C3 table. Add edge/loss tests to `tst_calendar_canon_roundtrip.cpp` (compile `ical→canon`; inspect loss charges `onlineMeeting` Dropped, `descriptionHtml` Reversible, `locations` Simplified). Build. **Do NOT run the engine suite yet — Step 2 is the green-gate.**

- [ ] **Step 2 (green-gate):** Run the calendar + engine suites: `ctest --test-dir .../build -R "calendar|tst_engine_unified_routing|tst_carddav|tst_engine_universal_sink_dispatch|tst_engine_silent_success_guard|tst_mass_delete_guard|tst_cancellation_reason|tst_engine_subset_dispatch|tst_engine_unified_boundary"`. Expected: ALL PASS. The engine now promotes backend `ical` → `calendar+canon`, diffs/merges with `CanonJson*`, demotes `canon→ical` on write; the dormant transcoding plan applies as a no-op on the resulting valid iCal. The seeded-record tests (`calendar_test_helpers.h` builds `{calendar,ical}` records) round-trip through `ical→canon→ical`. If `tst_calendar_transcoding_warning` fails, verify the `canon→ical` stage emits iCal the writer can parse (the warning path depends on `parseIncidence` succeeding). If a conflict test fails, re-check Task 3 Step 1 policy mirroring.

- [ ] **Step 3: Commit**

```bash
git add src/calendar/calendarstockshapes.cpp src/calendar/calendarstockshapes.h tests/calendar/tst_calendar_canon_roundtrip.cpp
git commit -m "calendar: demote ical to peer; register canon hub + bridges (Plan 3 Task C5)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 13: Final verification + STATUS/FINDINGS update

**Files:** Modify `docs/campaign/STATUS.md`, `docs/campaign/FINDINGS.md`.

- [ ] **Step 1: Full clean build + suite**

```bash
rm -rf /home/clinton/dev/libkalburator/build
cmake -S /home/clinton/dev/libkalburator -B /home/clinton/dev/libkalburator/build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build /home/clinton/dev/libkalburator/build
ctest --test-dir /home/clinton/dev/libkalburator/build
```
Expected: clean build; suite green at the Plan-2 baseline (107/108; the lone failure is the pre-existing flake `tst_providerlifecycle`, which passes on isolated re-run) PLUS the new canon round-trip + diff/merge tests. Record exact counts; re-run any single failure once to confirm it is the known flake. STOP if any new failure.

- [ ] **Step 2: Prove each domain's canonical is now the canon encoding**

```bash
grep -rn "EncodingId{\"canon\"}\|EncodingId{ \"canon\" }" src/contacts/contactsdomaindefinition.cpp src/todo/tododomaindefinition.cpp src/calendar/calendardomaindefinition.cpp
grep -rn "transcoding" src/contacts/ src/todo/ | grep -v "//" || echo "no transcoding coupling added to contacts/todo"
```
Expected: each domain definition returns a `canon` canonical; Plan 3 added no `src/transcoding/` coupling.

- [ ] **Step 3: Update STATUS**

In `docs/campaign/STATUS.md`: set the Plan 3 row to **Complete**; update the top Status line (Plans 1–3 landed; Plan 4 next, not yet written); update Last updated to the implementation date; in the Plan-4 row, **narrow its scope to pure convergence** — retire `src/transcoding/`, RRULE-as-edge (`canon → org-ical` Simplified loss), remove `ApplyContext.transcodingPlan` + `CalendarPluginWriter` special-casing — and note that **`calendar+canon` was introduced in Plan 3** (the canon + `ical↔canon` bridges already exist), so Plan 4 only converges the live path, it does not introduce the calendar canon. Set Next action to "write Plan 4 against the landed canon encodings."

- [ ] **Step 4: Log the reconciliation in FINDINGS**

Append to `docs/campaign/FINDINGS.md` one entry: the design §10 file-change list placed "calendarstockshapes.cpp: add canon + ical↔canon bridges" under Plan 4, but the human chose (2026-05-24) to land all three canons — including calendar — in Plan 3; Plan 4 is therefore pure convergence. Cite invariant 7 (decision recorded) and the scope deviation note.

- [ ] **Step 5: Commit**

```bash
git add docs/campaign/STATUS.md docs/campaign/FINDINGS.md
git commit -m "docs: Plan 3 complete — rich canon encodings landed for all three domains (Plan 3 Task 13)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Self-review notes (author)

- **Spec coverage:** canon JSON model + envelope (Task 1) ✓; coarse one-PropertyId diff ignoring `_canon`/`providerExtras` (Task 2, decision 6) ✓; per-PropertyId 3-way merge with providerExtras-follows-origin (Task 3, schema §5) ✓; the three rich canons enumerated from schema §2/§3/§4 (Tasks A1/B1/C1) ✓; legacy encodings demoted to peers with lossless inbound + four-kind lossy outbound bridges, peer edges never rewritten (Tasks A5/B5/C5, invariant 2) ✓; recurrence verbatim StringList, never parsed (B2/C2, invariant 3) ✓; contacts uid minting + vendor id to providerExtras (A2, decision 7) ✓; X-property Reversible carriers + IANA tz verbatim + org/MS Simplified/Degraded honestly classified (loss tables, invariant 4) ✓; differ stays shape-side per domain via `createCanonicalDiffer()` (A4/B4/C4, invariant 10) ✓.
- **Scope boundary (invariant 8) respected:** no transparent third-party auto-bridge, no capability objects, no load-time enforcement; no `src/transcoding/` retirement, no `ApplyContext.transcodingPlan`/`CalendarPluginWriter` change (all Plan 4). The N-hop spine is *used* (vcard3→vcard4→canon) but only for code we own.
- **P3 (each plan independently green):** Part 0 is unit-tested standalone; each domain Part ends with a green-gate task and the head-flip+differ+bridge land together so no intermediate red. The A4→A5 (and B4→B5, C4→C5) ordering note prevents a between-task red where the canon head exists without a route to it.
- **P4 (carry-verbatim before diff):** todo hierarchy held as three side-by-side PropertyIds (`relatedTo`/`parentUid`/`checklistItems`), event THISANDFUTURE as `recurrenceRange` + separate override record — stated in the catalogues (B1/C1) before the differ (coarse, whole-field) touches them.
- **P1 (landed APIs):** every signature used is copied from the tree (see "Landed APIs"). The merger's `ConflictPolicy` usage is deliberately pinned to "read the existing merger and mirror it" (Task 3 Step 1) rather than guessed, because the exact `ConflictPolicy` method names were not captured during planning — this is the one place the implementer must read before writing, and it is called out explicitly.
- **Deliberate convention:** stage field-mapping bodies are specified by exhaustive mapping tables + exhaustive TDD round-trip/loss fixtures rather than line-by-line KCalendarCore/KContacts code, because that vendor-API code must be developed test-first against the real libraries (writing it speculatively in the plan would rot, against P1's spirit). The tables list every field; the tests are the executable contract.
- **Known risk:** the calendar green-gate (C5 Step 2) is the highest-risk point — if `ical→canon→ical` is not faithful for a field a seeded test uses, that test goes red. Mitigation: C3's round-trip test must cover every field the `calendar_test_helpers.h` fixtures exercise (UID/SUMMARY/DTSTART/DTEND/STATUS at minimum) before C4/C5 flip the head.
