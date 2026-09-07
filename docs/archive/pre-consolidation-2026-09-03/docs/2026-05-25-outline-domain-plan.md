# Outline Domain Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a new libkalburator `outline` domain (hierarchical tree of nodes that are
simultaneously heading + task + note) with two peers — `org` (rich) and `opml` (thin) —
fully registered on the shape graph with round-trip and loss-honesty tests.

**Architecture:** Mirrors the `note` domain (2026-05-25). A record is one whole tree; the
canon body is a JSON document `{ uid, title, created, lastModified, attributes, children:[node…] }`
where each node recurses via `children`. Peers reach each other only through canon. A coarse
whole-tree differ/merger lands first; structural node-level merge is a deferred follow-on.

**Tech Stack:** C++17, Qt6 (Core, Test), QJson*, QXmlStreamReader/Writer (OPML), the
`Kalburator::Shape` registries / `CanonEnvelope` / `TransformationStage` / `RecordDiffer` /
`RecordMerger` APIs.

**Spec:** `docs/2026-05-25-outline-domain-design.md`.

**Decisions locked from spec §9 open questions:**
1. Org parser: **delegate to the OrgGrove library** (`codeberg.org/clintonthegeek/OrgGrove`,
   tree-sitter based, pinned `c7b7743`) — Task 5 is a thin model adapter, NOT a hand-rolled
   parser. (Superseded the original "minimal self-contained parser" decision after the
   OrgGrove foundation was built; `OrgBackend` (calendar) remains KCalendarCore-bound and is
   irrelevant here.)
2. Differ: **coarse** (whole-tree equality / pick-a-side merge) first. Structural node-level
   merge is a follow-on (not in this plan).
3. OPML: task fields (`done`/`status`/`priority`/`progress`/dates except `created`) are
   **Dropped** honestly. No namespaced stashing in the first cut.
4. **No explicit `order` field.** Sibling order == `children` array index.

**Canon JSON shape (authoritative — all stages/differ/node agree on these keys):**
```jsonc
// document (record body)
{ "_canon": {...}, "uid": "...",        // stamped by CanonEnvelope
  "title": "...", "created": "...", "lastModified": "...",
  "attributes": { },                    // doc-level Tier-3 bag (statusVocabulary, priorityScale, …)
  "children": [ node … ] }
// node (recursive)
{ "id": "...", "text": "...", "note": "...", "done": false,
  "status": "TODO", "priority": 1, "progress": 0,
  "start": "ISO", "due": "ISO", "completed": "ISO", "created": "ISO",
  "tags": [ "..." ], "attributes": { }, "children": [ node … ] }
```
Absent fields are **omitted** (not null). Dates are ISO-8601 **strings** in the first cut
(the rich Json date object from spec §2.3 is deferred — YAGNI for org/opml, which use plain
dates).

---

### Task 1: OutlineNode tree model + JSON (de)serialization + build wiring

The shared core every stage/differ depends on: an in-memory tree that round-trips to/from
the canon JSON node shape.

**Files:**
- Create: `src/outline/outlinenode.h`
- Create: `src/outline/outlinenode.cpp`
- Create: `tests/outline/tst_outline_node.cpp`
- Create: `tests/outline/CMakeLists.txt`
- Modify: `CMakeLists.txt` (add outline sources to the library SOURCES list, beside the `src/note/` entries)
- Modify: `tests/CMakeLists.txt` (add `add_subdirectory(outline)` after `add_subdirectory(note)`)

- [ ] **Step 1: Write the failing test**

`tests/outline/tst_outline_node.cpp`:
```cpp
#include <QTest>
#include <QJsonObject>
#include <QJsonArray>
#include "outlinenode.h"

using Kalburator::Outline::OutlineNode;

class TestOutlineNode : public QObject {
    Q_OBJECT
private slots:
    void roundTripsScalarFields();
    void roundTripsNestedChildren();
    void omitsAbsentFields();
};

void TestOutlineNode::roundTripsScalarFields()
{
    OutlineNode n;
    n.id = "n1";
    n.text = "Milk";
    n.note = "2% organic";
    n.done = true;
    n.status = "DONE";
    n.priority = 1;
    n.progress = 100;
    n.due = "2026-06-01";
    n.tags = { "errand", "shop" };
    n.attributes.insert("color", "red");

    const OutlineNode back = OutlineNode::fromJson(n.toJson());
    QCOMPARE(back.id, n.id);
    QCOMPARE(back.text, n.text);
    QCOMPARE(back.note, n.note);
    QCOMPARE(back.done, true);
    QCOMPARE(back.status, QString("DONE"));
    QCOMPARE(back.priority, 1);
    QCOMPARE(back.progress, 100);
    QCOMPARE(back.due, QString("2026-06-01"));
    QCOMPARE(back.tags, QStringList({ "errand", "shop" }));
    QCOMPARE(back.attributes.value("color").toString(), QString("red"));
}

void TestOutlineNode::roundTripsNestedChildren()
{
    OutlineNode root;
    root.text = "Groceries";
    OutlineNode a; a.text = "Milk";
    OutlineNode b; b.text = "Bread";
    OutlineNode b1; b1.text = "Sourdough";
    b.children = { b1 };
    root.children = { a, b };

    const OutlineNode back = OutlineNode::fromJson(root.toJson());
    QCOMPARE(back.children.size(), 2);
    QCOMPARE(back.children[0].text, QString("Milk"));
    QCOMPARE(back.children[1].text, QString("Bread"));
    QCOMPARE(back.children[1].children.size(), 1);
    QCOMPARE(back.children[1].children[0].text, QString("Sourdough"));
}

void TestOutlineNode::omitsAbsentFields()
{
    OutlineNode n;
    n.text = "bare";
    const QJsonObject obj = n.toJson();
    QVERIFY(obj.contains("text"));
    QVERIFY(!obj.contains("note"));
    QVERIFY(!obj.contains("status"));
    QVERIFY(!obj.contains("done"));      // false + no other signal → omitted
    QVERIFY(!obj.contains("priority"));
    QVERIFY(!obj.contains("children"));  // empty → omitted
}

QTEST_MAIN(TestOutlineNode)
#include "tst_outline_node.moc"
```

`tests/outline/CMakeLists.txt`:
```cmake
function(kalburator_add_outline_test TEST_NAME)
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt6::Core
            Qt6::Test
            $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

kalburator_add_outline_test(tst_outline_node)
```

- [ ] **Step 2: Wire the build**

In `tests/CMakeLists.txt`, after the line `add_subdirectory(note)` add:
```cmake
add_subdirectory(outline)
```
In root `CMakeLists.txt`, find the library SOURCES block listing `src/note/noteproperties.h`
etc. Add beside the note headers:
```cmake
    src/outline/outlinenode.h
```
and beside the note sources (`src/note/noteproperties.cpp` …):
```cmake
    src/outline/outlinenode.cpp
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build-dev -j$(($(nproc)-1)) 2>&1 | tail -20`
Expected: FAIL — `outlinenode.h` not found / `OutlineNode` undefined.

- [ ] **Step 4: Write minimal implementation**

`src/outline/outlinenode.h`:
```cpp
#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace Kalburator::Outline {

/// One node in an outline tree. Mirrors the canon JSON node shape
/// (see docs/2026-05-25-outline-domain-design.md §2.3). Absent optional
/// fields are omitted from toJson() rather than emitted as null.
struct OutlineNode {
    QString id;
    QString text;
    QString note;                 // empty == absent
    bool done = false;
    QString status;               // empty == absent
    std::optional<int> priority;
    std::optional<int> progress;
    QString start, due, completed, created;   // ISO strings; empty == absent
    QStringList tags;
    QJsonObject attributes;       // node-level Tier-3 bag
    QList<OutlineNode> children;

    /// True when `done` carries meaning (we only emit `done` if it is true
    /// or a status is present; a bare unchecked node omits it).
    QJsonObject toJson() const;
    static OutlineNode fromJson(const QJsonObject& obj);
};

}  // namespace Kalburator::Outline
```

`src/outline/outlinenode.cpp`:
```cpp
#include "outlinenode.h"

#include <QJsonArray>

namespace Kalburator::Outline {

QJsonObject OutlineNode::toJson() const
{
    QJsonObject o;
    if (!id.isEmpty())        o.insert("id", id);
    o.insert("text", text);                          // always present
    if (!note.isEmpty())      o.insert("note", note);
    if (done)                 o.insert("done", true);
    if (!status.isEmpty())    o.insert("status", status);
    if (priority)             o.insert("priority", *priority);
    if (progress)             o.insert("progress", *progress);
    if (!start.isEmpty())     o.insert("start", start);
    if (!due.isEmpty())       o.insert("due", due);
    if (!completed.isEmpty()) o.insert("completed", completed);
    if (!created.isEmpty())   o.insert("created", created);
    if (!tags.isEmpty()) {
        QJsonArray t;
        for (const QString& s : tags) t.append(s);
        o.insert("tags", t);
    }
    if (!attributes.isEmpty()) o.insert("attributes", attributes);
    if (!children.isEmpty()) {
        QJsonArray c;
        for (const OutlineNode& ch : children) c.append(ch.toJson());
        o.insert("children", c);
    }
    return o;
}

OutlineNode OutlineNode::fromJson(const QJsonObject& obj)
{
    OutlineNode n;
    n.id        = obj.value("id").toString();
    n.text      = obj.value("text").toString();
    n.note      = obj.value("note").toString();
    n.done      = obj.value("done").toBool(false);
    n.status    = obj.value("status").toString();
    if (obj.contains("priority")) n.priority = obj.value("priority").toInt();
    if (obj.contains("progress")) n.progress = obj.value("progress").toInt();
    n.start     = obj.value("start").toString();
    n.due       = obj.value("due").toString();
    n.completed = obj.value("completed").toString();
    n.created   = obj.value("created").toString();
    for (const QJsonValue& v : obj.value("tags").toArray())
        n.tags.append(v.toString());
    n.attributes = obj.value("attributes").toObject();
    for (const QJsonValue& v : obj.value("children").toArray())
        n.children.append(OutlineNode::fromJson(v.toObject()));
    return n;
}

}  // namespace Kalburator::Outline
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build-dev -j$(($(nproc)-1)) && ctest --test-dir build-dev -R tst_outline_node --output-on-failure`
Expected: PASS (3 tests).

- [ ] **Step 6: Commit**

```bash
git add src/outline/outlinenode.* tests/outline/ CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(outline): OutlineNode tree model + JSON round-trip"
```

---

### Task 2: Canon property catalogue

**Files:**
- Create: `src/outline/outlinecanonproperties.h`
- Create: `src/outline/outlinecanonproperties.cpp`
- Create: `tests/outline/tst_outline_catalogue.cpp`
- Modify: `CMakeLists.txt` (add the two new sources beside Task 1's)
- Modify: `tests/outline/CMakeLists.txt` (add the test)

- [ ] **Step 1: Write the failing test**

`tests/outline/tst_outline_catalogue.cpp`:
```cpp
#include <QTest>
#include "outlinecanonproperties.h"

using namespace Kalburator::Shape;
using Kalburator::Outline::makeOutlineCanonCatalogue;

class TestOutlineCatalogue : public QObject {
    Q_OBJECT
private slots:
    void hasRecordLevelProperties();
    void uidIsRequired();
};

void TestOutlineCatalogue::hasRecordLevelProperties()
{
    const PropertyCatalogue cat = makeOutlineCanonCatalogue();
    QVERIFY(cat.hasProperty(PropertyId{"uid"}));
    QVERIFY(cat.hasProperty(PropertyId{"title"}));
    QVERIFY(cat.hasProperty(PropertyId{"created"}));
    QVERIFY(cat.hasProperty(PropertyId{"lastModified"}));
    QVERIFY(cat.hasProperty(PropertyId{"attributes"}));
    QVERIFY(cat.hasProperty(PropertyId{"children"}));
}

void TestOutlineCatalogue::uidIsRequired()
{
    const PropertyCatalogue cat = makeOutlineCanonCatalogue();
    const PropertyDescriptor* uid = cat.find(PropertyId{"uid"});
    QVERIFY(uid != nullptr);
    QCOMPARE(uid->optional, false);
}

QTEST_MAIN(TestOutlineCatalogue)
#include "tst_outline_catalogue.moc"
```
Add to `tests/outline/CMakeLists.txt`: `kalburator_add_outline_test(tst_outline_catalogue)`

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build-dev -j$(($(nproc)-1)) 2>&1 | tail -20`
Expected: FAIL — `outlinecanonproperties.h` not found.

- [ ] **Step 3: Write minimal implementation**

`src/outline/outlinecanonproperties.h`:
```cpp
#pragma once
#include "propertycatalogue.h"

namespace Kalburator::Outline {
/// Record-level canon catalogue for the outline domain. Per-node fields
/// live inside the `children` Json payload; tree-awareness is the differ's job.
Kalburator::Shape::PropertyCatalogue makeOutlineCanonCatalogue();
}  // namespace Kalburator::Outline
```

`src/outline/outlinecanonproperties.cpp`:
```cpp
#include "outlinecanonproperties.h"

using namespace Kalburator::Shape;

namespace Kalburator::Outline {

PropertyCatalogue makeOutlineCanonCatalogue()
{
    PropertyCatalogue cat;
    cat.addProperty({ PropertyId{"uid"},          PropertyKind::String,   QStringLiteral("UID"), false });
    cat.addProperty({ PropertyId{"title"},        PropertyKind::String,   QStringLiteral("Title") });
    cat.addProperty({ PropertyId{"created"},      PropertyKind::DateTime, QStringLiteral("Created") });
    cat.addProperty({ PropertyId{"lastModified"}, PropertyKind::DateTime, QStringLiteral("Last Modified") });
    cat.addProperty({ PropertyId{"attributes"},   PropertyKind::Json,     QStringLiteral("Attributes") });
    cat.addProperty({ PropertyId{"children"},     PropertyKind::Json,     QStringLiteral("Children") });
    return cat;
}

}  // namespace Kalburator::Outline
```
Add both sources to root `CMakeLists.txt` beside the Task 1 outline entries.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-dev -j$(($(nproc)-1)) && ctest --test-dir build-dev -R tst_outline_catalogue --output-on-failure`
Expected: PASS (2 tests).

- [ ] **Step 5: Commit**

```bash
git add src/outline/outlinecanonproperties.* tests/outline/ CMakeLists.txt
git commit -m "feat(outline): canon property catalogue"
```

---

### Task 3: Coarse differ + merger

Whole-tree equality and pick-a-side merge over the canon JSON document. Structural
node-level merge is a deferred follow-on.

**Files:**
- Create: `src/outline/outlinediffer.h`, `src/outline/outlinediffer.cpp`
- Create: `src/outline/outlinemerger.h`, `src/outline/outlinemerger.cpp`
- Create: `tests/outline/tst_outline_differ.cpp`
- Modify: `CMakeLists.txt`, `tests/outline/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`tests/outline/tst_outline_differ.cpp`:
```cpp
#include <QTest>
#include <QJsonObject>
#include "outlinediffer.h"
#include "canonenvelope.h"

using namespace Kalburator::Shape;
using Kalburator::Outline::OutlineDiffer;

static CanonicalRecord rec(const QJsonObject& body, const QString& id = "r1")
{
    CanonicalRecord r;
    r.shape = Shape{ DomainId{"outline"}, EncodingId{"canon"} };
    r.data = CanonEnvelope::serialize(body);
    r.recordId = id;
    return r;
}

class TestOutlineDiffer : public QObject {
    Q_OBJECT
private slots:
    void equalTreesReportNoDiff();
    void changedChildReportsChildrenProperty();
};

void TestOutlineDiffer::equalTreesReportNoDiff()
{
    QJsonObject body{ {"title","L"}, {"children", QJsonArray{ QJsonObject{{"text","a"}} }} };
    OutlineDiffer d;
    QVERIFY(d.equal(rec(body), rec(body)));
    QVERIFY(d.diff(rec(body), rec(body)).isEmpty());
}

void TestOutlineDiffer::changedChildReportsChildrenProperty()
{
    QJsonObject base{ {"title","L"}, {"children", QJsonArray{ QJsonObject{{"text","a"}} }} };
    QJsonObject mod { {"title","L"}, {"children", QJsonArray{ QJsonObject{{"text","b"}} }} };
    OutlineDiffer d;
    QVERIFY(!d.equal(rec(mod), rec(base)));
    QVERIFY(d.diff(rec(mod), rec(base)).contains(PropertyId{"children"}));
}

QTEST_MAIN(TestOutlineDiffer)
#include "tst_outline_differ.moc"
```
Add `kalburator_add_outline_test(tst_outline_differ)`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build-dev -j$(($(nproc)-1)) 2>&1 | tail -20`
Expected: FAIL — `outlinediffer.h` not found.

- [ ] **Step 3: Write minimal implementation**

`src/outline/outlinediffer.h`:
```cpp
#pragma once
#include "recorddiffer.h"

namespace Kalburator::Outline {

/// Coarse RecordDiffer for (outline, canon): compares the record-level
/// canon properties (title/created/lastModified/attributes/children) by
/// JSON value-equality. Whole-tree granularity; node-level structural
/// diff is a deferred follow-on.
class OutlineDiffer : public Kalburator::Shape::RecordDiffer {
public:
    QSet<Kalburator::Shape::PropertyId> diff(
        const Kalburator::Shape::CanonicalRecord& source,
        const Kalburator::Shape::CanonicalRecord& baseline) const override;
    bool equal(const Kalburator::Shape::CanonicalRecord& a,
               const Kalburator::Shape::CanonicalRecord& b) const override;
};

}  // namespace Kalburator::Outline
```

`src/outline/outlinediffer.cpp`:
```cpp
#include "outlinediffer.h"
#include "canonenvelope.h"

#include <QJsonObject>

using namespace Kalburator::Shape;

namespace Kalburator::Outline {

namespace {
const char* const kProps[] = { "title", "created", "lastModified", "attributes", "children" };
}

QSet<PropertyId> OutlineDiffer::diff(const CanonicalRecord& source,
                                     const CanonicalRecord& baseline) const
{
    const QJsonObject a = CanonEnvelope::parse(source.data);
    const QJsonObject b = CanonEnvelope::parse(baseline.data);
    QSet<PropertyId> changed;
    for (const char* k : kProps) {
        if (!CanonEnvelope::valuesEqual(a.value(k), b.value(k)))
            changed.insert(PropertyId{QString::fromLatin1(k)});
    }
    return changed;
}

bool OutlineDiffer::equal(const CanonicalRecord& a, const CanonicalRecord& b) const
{
    return diff(a, b).isEmpty();
}

}  // namespace Kalburator::Outline
```

`src/outline/outlinemerger.h`:
```cpp
#pragma once
#include "recordmerger.h"

namespace Kalburator::Outline {

/// Coarse RecordMerger for (outline, canon): whole-tree pick-a-side per the
/// conflict policy (no structural node merge in the first cut).
class OutlineMerger : public Kalburator::Shape::RecordMerger {
public:
    Kalburator::Shape::CanonicalRecord merge(
        const Kalburator::Shape::CanonicalRecord& source,
        const Kalburator::Shape::CanonicalRecord& target,
        const Kalburator::Shape::CanonicalRecord& baseline,
        const Kalburator::Conflict::ConflictPolicy& policy) const override;
};

}  // namespace Kalburator::Outline
```

`src/outline/outlinemerger.cpp`:
```cpp
#include "outlinemerger.h"
#include "outlinediffer.h"

using namespace Kalburator::Shape;

namespace Kalburator::Outline {

CanonicalRecord OutlineMerger::merge(const CanonicalRecord& source,
                                     const CanonicalRecord& target,
                                     const CanonicalRecord& baseline,
                                     const Kalburator::Conflict::ConflictPolicy& policy) const
{
    Q_UNUSED(policy);
    OutlineDiffer d;
    // If only one side changed since baseline, take that side. If both changed,
    // prefer source (the conflict-policy-aware structural merge is a follow-on).
    const bool sourceChanged = !d.equal(source, baseline);
    const bool targetChanged = !d.equal(target, baseline);
    if (targetChanged && !sourceChanged)
        return target;
    return source;
}

}  // namespace Kalburator::Outline
```
Add the four sources to root `CMakeLists.txt`.

> Note: confirm the `ConflictPolicy` include path matches the note merger's
> (`conflictpolicy.h` via `recordmerger.h`); no extra include should be needed.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-dev -j$(($(nproc)-1)) && ctest --test-dir build-dev -R tst_outline_differ --output-on-failure`
Expected: PASS (2 tests).

- [ ] **Step 5: Commit**

```bash
git add src/outline/outlinediffer.* src/outline/outlinemerger.* tests/outline/ CMakeLists.txt
git commit -m "feat(outline): coarse whole-tree differ + pick-a-side merger"
```

---

### Task 4: OPML peer stages + loss profiles

`(outline, opml) ↔ (outline, canon)`. OPML carries structure + arbitrary attributes;
task semantics are Dropped honestly.

**Files:**
- Create: `src/outline/opmlcanonstages.h`, `src/outline/opmlcanonstages.cpp`
- Create: `tests/outline/tst_outline_opml.cpp`
- Modify: `CMakeLists.txt`, `tests/outline/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`tests/outline/tst_outline_opml.cpp`:
```cpp
#include <QTest>
#include <QJsonObject>
#include <QJsonArray>
#include "opmlcanonstages.h"
#include "canonenvelope.h"

using namespace Kalburator::Shape;
using Kalburator::Outline::OpmlToCanonStage;
using Kalburator::Outline::CanonToOpmlStage;

class TestOutlineOpml : public QObject {
    Q_OBJECT
private slots:
    void opmlToCanonBuildsTree();
    void canonToOpmlEmitsNestedOutlines();
    void opmlRoundTripPreservesStructureAndText();
};

void TestOutlineOpml::opmlToCanonBuildsTree()
{
    const QByteArray opml =
        "<opml version=\"2.0\"><head><title>L</title></head><body>"
        "<outline text=\"Groceries\">"
        "<outline text=\"Milk\"/><outline text=\"Bread\"/>"
        "</outline></body></opml>";
    const QJsonObject body = CanonEnvelope::parse(OpmlToCanonStage().transform(opml));
    QCOMPARE(body.value("title").toString(), QString("L"));
    const QJsonArray top = body.value("children").toArray();
    QCOMPARE(top.size(), 1);
    QCOMPARE(top[0].toObject().value("text").toString(), QString("Groceries"));
    QCOMPARE(top[0].toObject().value("children").toArray().size(), 2);
}

void TestOutlineOpml::canonToOpmlEmitsNestedOutlines()
{
    QJsonObject body{ {"title","L"}, {"children", QJsonArray{
        QJsonObject{ {"text","P"}, {"children", QJsonArray{ QJsonObject{{"text","C"}} }} } }} };
    CanonEnvelope::stampEnvelope(body, "outline", "u1");
    const QByteArray opml = CanonToOpmlStage().transform(CanonEnvelope::serialize(body));
    QVERIFY(opml.contains("text=\"P\""));
    QVERIFY(opml.contains("text=\"C\""));
    QVERIFY(opml.contains("<title>L</title>"));
}

void TestOutlineOpml::opmlRoundTripPreservesStructureAndText()
{
    const QByteArray opml =
        "<opml version=\"2.0\"><head><title>L</title></head><body>"
        "<outline text=\"A\"><outline text=\"B\"/></outline></body></opml>";
    const QByteArray back = CanonToOpmlStage().transform(OpmlToCanonStage().transform(opml));
    const QJsonObject b1 = CanonEnvelope::parse(OpmlToCanonStage().transform(opml));
    const QJsonObject b2 = CanonEnvelope::parse(OpmlToCanonStage().transform(back));
    QVERIFY(CanonEnvelope::valuesEqual(b1.value("children"), b2.value("children")));
}

QTEST_MAIN(TestOutlineOpml)
#include "tst_outline_opml.moc"
```
Add `kalburator_add_outline_test(tst_outline_opml)`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build-dev -j$(($(nproc)-1)) 2>&1 | tail -20`
Expected: FAIL — `opmlcanonstages.h` not found.

- [ ] **Step 3: Write minimal implementation**

`src/outline/opmlcanonstages.h`:
```cpp
#pragma once
#include "transformationedge.h"

namespace Kalburator::Outline {

/// (outline, opml) → (outline, canon). Maps <outline> XML containment to the
/// node tree; `text`→text, `created`→created, `category`→tags, all other
/// attributes → node `attributes`. <head><title> → doc title.
class OpmlToCanonStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& sourceBytes) const override;
};

/// (outline, canon) → (outline, opml). Emits nested <outline text=…> with
/// `created`/`category` and node `attributes` as XML attributes. Task fields
/// (done/status/priority/progress/start/due/completed) are DROPPED (no OPML
/// representation); `note` → an `_note` attribute (Reversible).
class CanonToOpmlStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& sourceBytes) const override;
};

}  // namespace Kalburator::Outline
```

`src/outline/opmlcanonstages.cpp`:
```cpp
#include "opmlcanonstages.h"
#include "outlinenode.h"
#include "canonenvelope.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

using namespace Kalburator::Shape;

namespace Kalburator::Outline {

namespace {

// Build an OutlineNode from the <outline> element the reader is positioned on.
OutlineNode readOutline(QXmlStreamReader& xml)
{
    OutlineNode n;
    const QXmlStreamAttributes attrs = xml.attributes();
    for (const QXmlStreamAttribute& a : attrs) {
        const QString name = a.name().toString();
        const QString val = a.value().toString();
        if (name == "text")            n.text = val;
        else if (name == "created")    n.created = val;
        else if (name == "category")   n.tags = val.split(',', Qt::SkipEmptyParts);
        else if (name == "_note")      n.note = val;
        else                           n.attributes.insert(name, val);
    }
    while (!xml.atEnd()) {
        const auto tok = xml.readNext();
        if (tok == QXmlStreamReader::StartElement && xml.name() == QStringLiteral("outline"))
            n.children.append(readOutline(xml));
        else if (tok == QXmlStreamReader::EndElement && xml.name() == QStringLiteral("outline"))
            break;
    }
    return n;
}

void writeOutline(QXmlStreamWriter& xml, const OutlineNode& n)
{
    xml.writeStartElement("outline");
    xml.writeAttribute("text", n.text);
    if (!n.created.isEmpty()) xml.writeAttribute("created", n.created);
    if (!n.tags.isEmpty())    xml.writeAttribute("category", n.tags.join(','));
    if (!n.note.isEmpty())    xml.writeAttribute("_note", n.note);   // Reversible
    for (auto it = n.attributes.begin(); it != n.attributes.end(); ++it)
        xml.writeAttribute(it.key(), it.value().toString());
    // Task fields intentionally dropped (no OPML representation).
    for (const OutlineNode& c : n.children) writeOutline(xml, c);
    xml.writeEndElement();
}

}  // namespace

QByteArray OpmlToCanonStage::transform(const QByteArray& sourceBytes) const
{
    QXmlStreamReader xml(sourceBytes);
    QString title;
    QJsonArray children;
    while (!xml.atEnd()) {
        const auto tok = xml.readNext();
        if (tok != QXmlStreamReader::StartElement) continue;
        if (xml.name() == QStringLiteral("title"))
            title = xml.readElementText();
        else if (xml.name() == QStringLiteral("outline"))
            children.append(readOutline(xml).toJson());
    }
    QJsonObject body;
    CanonEnvelope::stampEnvelope(body, "outline", QString());
    if (!title.isEmpty()) body.insert("title", title);
    body.insert("children", children);
    return CanonEnvelope::serialize(body);
}

QByteArray CanonToOpmlStage::transform(const QByteArray& sourceBytes) const
{
    const QJsonObject body = CanonEnvelope::parse(sourceBytes);
    QByteArray out;
    QXmlStreamWriter xml(&out);
    xml.writeStartElement("opml");
    xml.writeAttribute("version", "2.0");
    xml.writeStartElement("head");
    if (body.contains("title")) xml.writeTextElement("title", body.value("title").toString());
    xml.writeEndElement();  // head
    xml.writeStartElement("body");
    for (const QJsonValue& v : body.value("children").toArray())
        writeOutline(xml, OutlineNode::fromJson(v.toObject()));
    xml.writeEndElement();  // body
    xml.writeEndElement();  // opml
    return out;
}

}  // namespace Kalburator::Outline
```
Add both sources to root `CMakeLists.txt`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-dev -j$(($(nproc)-1)) && ctest --test-dir build-dev -R tst_outline_opml --output-on-failure`
Expected: PASS (3 tests).

- [ ] **Step 5: Commit**

```bash
git add src/outline/opmlcanonstages.* tests/outline/ CMakeLists.txt
git commit -m "feat(outline): OPML peer stages (structure + attrs; task fields dropped)"
```

---

### Task 5: Org peer stages over OrgGrove + loss profiles

`(outline, org) ↔ (outline, canon)`. **We do NOT hand-roll an org parser.** Org parsing and
serialization are delegated to the **OrgGrove** library
(`codeberg.org/clintonthegeek/OrgGrove`, public, pinned at `c7b7743`), which exposes
`OrgGrove::Parser().parse(bytes) → OrgGrove::Document` and `OrgGrove::serialize(Document) →
bytes`. These stages are thin **model adapters** mapping `OrgGrove::Headline` ⇄ the canon
`OutlineNode` (their field sets line up almost 1:1, including `priority` as `std::optional<int>`
on both). All org-syntax knowledge lives in OrgGrove; this task adds no parsing logic.

**Prerequisite:** OrgGrove links the system tree-sitter runtime via pkg-config, so building the
outline domain's org peer now requires `libtree-sitter` dev headers present
(`pkg-config --exists tree-sitter`). This is the one new system dependency the org peer adds.

**Files:**
- Create: `src/outline/orgcanonstages.h`, `src/outline/orgcanonstages.cpp`
- Create: `tests/outline/tst_outline_org.cpp`
- Modify: `CMakeLists.txt` (add OrgGrove FetchContent dependency + link; add the two new sources), `tests/outline/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`tests/outline/tst_outline_org.cpp`:
```cpp
#include <QTest>
#include <QJsonObject>
#include <QJsonArray>
#include "orgcanonstages.h"
#include "canonenvelope.h"

using namespace Kalburator::Shape;
using Kalburator::Outline::OrgToCanonStage;
using Kalburator::Outline::CanonToOrgStage;

class TestOutlineOrg : public QObject {
    Q_OBJECT
private slots:
    void parsesHeadlineFacets();
    void parsesNesting();
    void roundTripsRichNode();
};

void TestOutlineOrg::parsesHeadlineFacets()
{
    const QByteArray org =
        "* TODO [#A] Buy milk :errand:shop:\n"
        "DEADLINE: <2026-06-01>\n"
        "Some body text\n";
    const QJsonObject body = CanonEnvelope::parse(OrgToCanonStage().transform(org));
    const QJsonObject n = body.value("children").toArray()[0].toObject();
    QCOMPARE(n.value("text").toString(), QString("Buy milk"));
    QCOMPARE(n.value("status").toString(), QString("TODO"));
    QCOMPARE(n.value("priority").toInt(), 1);          // A→1
    QCOMPARE(n.value("due").toString(), QString("2026-06-01"));
    QCOMPARE(n.value("note").toString().trimmed(), QString("Some body text"));
    const QJsonArray tags = n.value("tags").toArray();
    QCOMPARE(tags.size(), 2);
}

void TestOutlineOrg::parsesNesting()
{
    const QByteArray org = "* Parent\n** Child\n*** Grandchild\n";
    const QJsonObject body = CanonEnvelope::parse(OrgToCanonStage().transform(org));
    const QJsonObject p = body.value("children").toArray()[0].toObject();
    QCOMPARE(p.value("text").toString(), QString("Parent"));
    const QJsonObject c = p.value("children").toArray()[0].toObject();
    QCOMPARE(c.value("text").toString(), QString("Child"));
    QCOMPARE(c.value("children").toArray()[0].toObject().value("text").toString(),
             QString("Grandchild"));
}

void TestOutlineOrg::roundTripsRichNode()
{
    const QByteArray org =
        "* TODO [#B] Task :work:\n"
        "DEADLINE: <2026-07-01>\n"
        "Body line\n";
    const QByteArray back = CanonToOrgStage().transform(OrgToCanonStage().transform(org));
    const QJsonObject a = CanonEnvelope::parse(OrgToCanonStage().transform(org));
    const QJsonObject b = CanonEnvelope::parse(OrgToCanonStage().transform(back));
    QVERIFY(CanonEnvelope::valuesEqual(a.value("children"), b.value("children")));
}

QTEST_MAIN(TestOutlineOrg)
#include "tst_outline_org.moc"
```
Add `kalburator_add_outline_test(tst_outline_org)`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build-dev -j$(($(nproc)-1)) 2>&1 | tail -20`
Expected: FAIL — `orgcanonstages.h` not found.

- [ ] **Step 3: Write minimal implementation**

`src/outline/orgcanonstages.h`:
```cpp
#pragma once
#include "transformationedge.h"

namespace Kalburator::Outline {

/// (outline, org) → (outline, canon). Delegates parsing to OrgGrove and maps
/// each OrgGrove::Headline → canon OutlineNode (title→text, body→note, todoKeyword
/// →status, priority, planning→start/due/completed, tags, properties→attributes).
class OrgToCanonStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& sourceBytes) const override;
};

/// (outline, canon) → (outline, org). Maps canon OutlineNode → OrgGrove::Headline
/// and delegates serialization to OrgGrove::serialize.
class CanonToOrgStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& sourceBytes) const override;
};

}  // namespace Kalburator::Outline
```

**Step 3a — add the OrgGrove dependency to the root `CMakeLists.txt`** (mirror how this repo
fetches its own deps; provide a local `SOURCE_DIR` override for dev):
```cmake
set(KALBURATOR_ORGGROVE_SOURCE_DIR "" CACHE PATH
    "Optional local OrgGrove checkout (overrides FetchContent)")
set(KALBURATOR_ORGGROVE_GIT_TAG "c7b7743fa0ecbc218e399ceaacc3f49dfcd69978" CACHE STRING
    "OrgGrove commit to fetch when SOURCE_DIR is unset")

if(KALBURATOR_ORGGROVE_SOURCE_DIR)
    add_subdirectory(${KALBURATOR_ORGGROVE_SOURCE_DIR} orggrove EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(OrgGrove
        GIT_REPOSITORY https://codeberg.org/clintonthegeek/OrgGrove.git
        GIT_TAG        ${KALBURATOR_ORGGROVE_GIT_TAG})
    FetchContent_MakeAvailable(OrgGrove)
endif()
```
Then link OrgGrove into the target that compiles the outline sources (the one aliased
`Kalburator::Sync`): `target_link_libraries(<that target> PRIVATE OrgGrove::OrgGrove)`. Add the
two `src/outline/orgcanonstages.*` files to the SOURCES list beside the other outline sources.

> Note for dev builds: pass `-DKALBURATOR_ORGGROVE_SOURCE_DIR=/home/clinton/dev/OrgGrove` (and,
> downstream, the equivalent through WildPalms) to build against the local checkout instead of
> fetching. Requires `pkg-config --exists tree-sitter` (OrgGrove's system runtime dep).

`src/outline/orgcanonstages.cpp` — **thin adapter over OrgGrove (no parsing logic):**
```cpp
#include "orgcanonstages.h"
#include "outlinenode.h"
#include "canonenvelope.h"

#include <orggrove/parser.h>
#include <QJsonArray>
#include <QJsonObject>

using namespace Kalburator::Shape;

namespace Kalburator::Outline {

namespace {

// OrgGrove::Headline -> canon OutlineNode (recursive). Fields line up 1:1.
OutlineNode fromOrg(const OrgGrove::Headline& h)
{
    OutlineNode n;
    n.text      = h.title;
    n.note      = h.body;
    n.done      = h.isDone;
    n.status    = h.todoKeyword;
    n.priority  = h.priority;            // both std::optional<int>
    n.start     = h.planning.scheduled;
    n.due       = h.planning.deadline;
    n.completed = h.planning.closed;
    n.tags      = h.tags;
    for (auto it = h.properties.cbegin(); it != h.properties.cend(); ++it)
        n.attributes.insert(it.key(), it.value());   // QString -> QJsonValue
    for (const OrgGrove::Headline& c : h.children)
        n.children.append(fromOrg(c));
    return n;
}

// canon OutlineNode -> OrgGrove::Headline (recursive). OrgGrove::serialize derives
// star depth from tree position, so `level` need not be set here.
OrgGrove::Headline toOrg(const OutlineNode& n)
{
    OrgGrove::Headline h;
    h.title              = n.text;
    h.body               = n.note;
    h.isDone             = n.done;
    h.todoKeyword        = n.status;
    h.priority           = n.priority;
    h.planning.scheduled = n.start;
    h.planning.deadline  = n.due;
    h.planning.closed    = n.completed;
    h.tags               = n.tags;
    const QJsonObject attrs = n.attributes;
    for (auto it = attrs.begin(); it != attrs.end(); ++it)
        h.properties.insert(it.key(), it.value().toString());
    for (const OutlineNode& c : n.children)
        h.children.append(toOrg(c));
    return h;
}

}  // namespace

QByteArray OrgToCanonStage::transform(const QByteArray& sourceBytes) const
{
    const OrgGrove::Document doc = OrgGrove::Parser().parse(sourceBytes);
    QJsonArray children;
    for (const OrgGrove::Headline& h : doc.children)
        children.append(fromOrg(h).toJson());

    QJsonObject body;
    CanonEnvelope::stampEnvelope(body, "outline", QString());
    if (!doc.title.isEmpty())
        body.insert("title", doc.title);
    if (!doc.todoKeywords.isEmpty()) {     // doc-level vocab rides in Tier-3 attributes
        QJsonObject attrs;
        attrs.insert("statusVocabulary", doc.todoKeywords.join(QLatin1Char(' ')));
        body.insert("attributes", attrs);
    }
    body.insert("children", children);
    return CanonEnvelope::serialize(body);
}

QByteArray CanonToOrgStage::transform(const QByteArray& sourceBytes) const
{
    const QJsonObject body = CanonEnvelope::parse(sourceBytes);
    OrgGrove::Document doc;
    doc.title = body.value("title").toString();
    const QString vocab = body.value("attributes").toObject()
                              .value("statusVocabulary").toString();
    if (!vocab.isEmpty())
        doc.todoKeywords = vocab.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QJsonValue& v : body.value("children").toArray())
        doc.children.append(toOrg(OutlineNode::fromJson(v.toObject())));
    return OrgGrove::serialize(doc);
}

}  // namespace Kalburator::Outline
```

> Implementation note: all org syntax handling lives in OrgGrove. These stages are pure model
> adapters. If `roundTripsRichNode` reveals a mismatch, fix the **field mapping** here (or, if it's
> an org-syntax issue, fix it in OrgGrove and re-pin) — do NOT add org parsing to this file. The
> test compares canon JSON, not bytes. `OutlineNode::priority` and `OrgGrove::Headline::priority`
> are both `std::optional<int>`, so they assign directly.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-dev -j$(($(nproc)-1)) && ctest --test-dir build-dev -R tst_outline_org --output-on-failure`
Expected: PASS (3 tests).

- [ ] **Step 5: Commit**

```bash
git add src/outline/orgcanonstages.* tests/outline/ CMakeLists.txt
git commit -m "feat(outline): Org peer stages over OrgGrove (adapter, no hand-rolled parser)"
```

---

### Task 6: Domain definition, stock shapes, plugin (full wiring)

Tie peers + edges + differ/merger into a registrable domain, mirroring `note`.

**Files:**
- Create: `src/outline/outlinedomaindefinition.h`, `.cpp`
- Create: `src/outline/outlinestockshapes.h`, `.cpp`
- Create: `src/outline/outlineplugin.h`, `.cpp`
- Create: `tests/outline/tst_outline_shapes.cpp`
- Modify: `CMakeLists.txt`, `tests/outline/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`tests/outline/tst_outline_shapes.cpp`:
```cpp
#include <QTest>
#include "outlinedomaindefinition.h"
#include "outlinestockshapes.h"
#include "shaperegistries.h"

using namespace Kalburator::Shape;

class TestOutlineShapes : public QObject {
    Q_OBJECT
private slots:
    void registersCanonAndPeers();
    void richnessOrdersOrgAboveOpml();
};

static ShapeRegistries makeRegistries()
{
    ShapeRegistries regs;
    auto& reg = regs.transformation;
    Kalburator::Outline::OutlineDomainDefinition def;
    const auto spine = def.canonicalSpine();
    reg.registerShape(spine.first().first, spine.first().second);
    reg.declareCanonical(def.domain(), spine.first().first);
    Kalburator::Outline::OutlineStockShapes shapes;
    for (const auto& [s, cat] : shapes.peerShapes()) reg.registerShape(s, cat);
    for (const auto& e : shapes.edges()) reg.registerEdge(e);
    return regs;
}

void TestOutlineShapes::registersCanonAndPeers()
{
    ShapeRegistries regs = makeRegistries();
    const Shape canon{ DomainId{"outline"}, EncodingId{"canon"} };
    const Shape org  { DomainId{"outline"}, EncodingId{"org"} };
    const Shape opml { DomainId{"outline"}, EncodingId{"opml"} };
    QCOMPARE(regs.transformation.canonicalFor(DomainId{"outline"}), canon);
    QVERIFY(regs.transformation.hasShape(org));
    QVERIFY(regs.transformation.hasShape(opml));
}

void TestOutlineShapes::richnessOrdersOrgAboveOpml()
{
    Kalburator::Outline::OutlineDomainDefinition def;
    const Shape org  { DomainId{"outline"}, EncodingId{"org"} };
    const Shape opml { DomainId{"outline"}, EncodingId{"opml"} };
    QVERIFY(def.richnessRank(def.canonicalShape()) > def.richnessRank(org));
    QVERIFY(def.richnessRank(org) > def.richnessRank(opml));
}

QTEST_MAIN(TestOutlineShapes)
#include "tst_outline_shapes.moc"
```
Add `kalburator_add_outline_test(tst_outline_shapes)`.

> Verify the registry accessor names against `src/shape/shaperegistries.h` /
> `transformationregistry.h` (`canonicalFor`, `hasShape`); adjust the test if the real
> names differ (e.g. `canonicalForDomain`).

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build-dev -j$(($(nproc)-1)) 2>&1 | tail -20`
Expected: FAIL — `outlinedomaindefinition.h` not found.

- [ ] **Step 3: Write minimal implementation**

`src/outline/outlinedomaindefinition.h` — mirror `notedomaindefinition.h` with class
`OutlineDomainDefinition` in namespace `Kalburator::Outline`.

`src/outline/outlinedomaindefinition.cpp`:
```cpp
#include "outlinedomaindefinition.h"
#include "outlinecanonproperties.h"
#include "outlinediffer.h"
#include "outlinemerger.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;

namespace Kalburator::Outline {

Shape::DomainId OutlineDomainDefinition::domain() const { return DomainId{"outline"}; }

Shape::Shape OutlineDomainDefinition::canonicalShape() const {
    return { DomainId{"outline"}, EncodingId{"canon"} };
}

Shape::PropertyCatalogue OutlineDomainDefinition::canonicalCatalogue() const {
    return makeOutlineCanonCatalogue();
}

std::unique_ptr<Shape::RecordDiffer> OutlineDomainDefinition::createCanonicalDiffer() const {
    return std::make_unique<OutlineDiffer>();
}

std::unique_ptr<Shape::RecordMerger> OutlineDomainDefinition::createCanonicalMerger() const {
    return std::make_unique<OutlineMerger>();
}

int OutlineDomainDefinition::richnessRank(const Shape::Shape& s) const {
    if (s == canonicalShape())            return 100;
    if (s.encoding == EncodingId{"org"})  return 70;
    if (s.encoding == EncodingId{"opml"}) return 40;
    return 0;
}

QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>>
OutlineDomainDefinition::canonicalSpine() const {
    return { { canonicalShape(), canonicalCatalogue() } };
}

}  // namespace Kalburator::Outline
```

`src/outline/outlinestockshapes.h` — mirror `notestockshapes.h`, `targetDomain()` →
`DomainId{"outline"}`.

`src/outline/outlinestockshapes.cpp`:
```cpp
#include "outlinestockshapes.h"
#include "outlinecanonproperties.h"
#include "orgcanonstages.h"
#include "opmlcanonstages.h"
#include "lossprofile.h"

using namespace Kalburator::Shape;

namespace Kalburator::Outline {

namespace {
// canon → opml drops the task tier; reports it honestly.
LossProfile canonToOpmlLoss() {
    LossProfile p;
    for (const char* k : { "done", "status", "priority", "progress", "start", "due", "completed" })
        p.affected.insert(PropertyId{QString::fromLatin1(k)}, LossKind::Dropped);
    p.affected.insert(PropertyId{"note"}, LossKind::Reversible);   // _note attr
    return p;
}
LossProfile attributesReversible() {   // org <-> canon: only doc/node attributes ride the side channel
    LossProfile p;
    p.affected.insert(PropertyId{"attributes"}, LossKind::Reversible);
    return p;
}
}  // namespace

QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>> OutlineStockShapes::peerShapes() const
{
    const Shape org { DomainId{"outline"}, EncodingId{"org"} };
    const Shape opml{ DomainId{"outline"}, EncodingId{"opml"} };
    return { { org, makeOutlineCanonCatalogue() }, { opml, makeOutlineCanonCatalogue() } };
}

QList<Shape::TransformationEdge> OutlineStockShapes::edges() const
{
    const Shape canon{ DomainId{"outline"}, EncodingId{"canon"} };
    const Shape org  { DomainId{"outline"}, EncodingId{"org"} };
    const Shape opml { DomainId{"outline"}, EncodingId{"opml"} };
    return {
        TransformationEdge{ canon, canon, LossProfile{},        std::make_shared<IdentityStage>() },
        TransformationEdge{ org,   canon, attributesReversible(), std::make_shared<OrgToCanonStage>() },
        TransformationEdge{ canon, org,   attributesReversible(), std::make_shared<CanonToOrgStage>() },
        TransformationEdge{ opml,  canon, LossProfile{},          std::make_shared<OpmlToCanonStage>() },
        TransformationEdge{ canon, opml,  canonToOpmlLoss(),      std::make_shared<CanonToOpmlStage>() },
    };
}

}  // namespace Kalburator::Outline
```

`src/outline/outlineplugin.h` / `.cpp` — mirror `noteplugin.*`: `OutlinePlugin::domainDefinitions()`
returns `{ std::make_shared<OutlineDomainDefinition>() }`, `shapeContributions()` returns
`{ std::make_shared<OutlineStockShapes>() }`.

Add all six sources to root `CMakeLists.txt`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-dev -j$(($(nproc)-1)) && ctest --test-dir build-dev -R tst_outline_shapes --output-on-failure`
Expected: PASS (2 tests).

- [ ] **Step 5: Commit**

```bash
git add src/outline/outlinedomaindefinition.* src/outline/outlinestockshapes.* \
        src/outline/outlineplugin.* tests/outline/ CMakeLists.txt
git commit -m "feat(outline): domain definition, stock shapes, plugin wiring"
```

---

### Task 7: Integration round-trip + loss-honesty tests through the registry

Prove the full graph: org round-trips losslessly through canon; OPML reports exactly the
Dropped set with no silent loss.

**Files:**
- Create: `tests/outline/tst_outline_canon_roundtrip.cpp`
- Modify: `tests/outline/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`tests/outline/tst_outline_canon_roundtrip.cpp`:
```cpp
#include <QTest>
#include <QJsonObject>
#include "outlinedomaindefinition.h"
#include "outlinestockshapes.h"
#include "shaperegistries.h"
#include "canonenvelope.h"
#include "lossprofile.h"

using namespace Kalburator::Shape;

static ShapeRegistries makeRegistries()
{
    ShapeRegistries regs;
    auto& reg = regs.transformation;
    Kalburator::Outline::OutlineDomainDefinition def;
    const auto spine = def.canonicalSpine();
    reg.registerShape(spine.first().first, spine.first().second);
    reg.declareCanonical(def.domain(), spine.first().first);
    Kalburator::Outline::OutlineStockShapes shapes;
    for (const auto& [s, cat] : shapes.peerShapes()) reg.registerShape(s, cat);
    for (const auto& e : shapes.edges()) reg.registerEdge(e);
    return regs;
}

class TestOutlineRoundtrip : public QObject {
    Q_OBJECT
private slots:
    void orgPathIsLossless();
    void opmlDropsTaskFieldsHonestly();
};

void TestOutlineRoundtrip::orgPathIsLossless()
{
    ShapeRegistries regs = makeRegistries();
    const Shape org  { DomainId{"outline"}, EncodingId{"org"} };
    const Shape canon{ DomainId{"outline"}, EncodingId{"canon"} };
    // org → canon → org composed loss is Reversible-only (lossless data preservation).
    const LossProfile toCanon = regs.transformation.pipeline(org, canon).loss;
    const LossProfile back     = regs.transformation.pipeline(canon, org).loss;
    const LossProfile composed = toCanon.compose(back);
    QVERIFY(composed.droppedProperties().isEmpty());   // no Dropped on the org loop
}

void TestOutlineRoundtrip::opmlDropsTaskFieldsHonestly()
{
    ShapeRegistries regs = makeRegistries();
    const Shape opml { DomainId{"outline"}, EncodingId{"opml"} };
    const Shape canon{ DomainId{"outline"}, EncodingId{"canon"} };
    const LossProfile demote = regs.transformation.pipeline(canon, opml).loss;
    const QSet<PropertyId> dropped = demote.droppedProperties();
    QVERIFY(dropped.contains(PropertyId{"priority"}));
    QVERIFY(dropped.contains(PropertyId{"status"}));
    QVERIFY(dropped.contains(PropertyId{"due"}));
    QVERIFY(!dropped.contains(PropertyId{"text"}));   // structure never dropped
}

QTEST_MAIN(TestOutlineRoundtrip)
#include "tst_outline_canon_roundtrip.moc"
```
Add `kalburator_add_outline_test(tst_outline_canon_roundtrip)`.

> Verify the pipeline accessor against `transformationregistry.h`. The todo round-trip test
> (`tests/todo/tst_todo_canon_roundtrip.cpp`) shows the real call for building/querying a
> pipeline and its composed `LossProfile`; match that exact API (method name may be
> `pipeline`, `findPipeline`, or similar, and may return a struct whose `.loss` you read).

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build-dev -j$(($(nproc)-1)) 2>&1 | tail -20`
Expected: FAIL — test exe undefined / pipeline API mismatch (fix per the note above).

- [ ] **Step 3: Make it pass**

Adjust the pipeline/loss accessor calls to match the real `TransformationRegistry` API
(copy the pattern from `tests/todo/tst_todo_canon_roundtrip.cpp`). No production code change
is expected — Task 6 already registered the edges with these loss profiles.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-dev -j$(($(nproc)-1)) && ctest --test-dir build-dev -R tst_outline_canon_roundtrip --output-on-failure`
Expected: PASS (2 tests).

- [ ] **Step 5: Commit**

```bash
git add tests/outline/ && git commit -m "test(outline): registry round-trip + loss-honesty"
```

---

### Task 8: Register OutlinePlugin in the stock plugin set

Make the domain available at runtime alongside note/todo/etc., with a load test mirroring
`tst_note_plugin`.

**Files:**
- Modify: `src/plugin/stock_plugins.cpp` (add `OutlinePlugin` to the stock list)
- Create: `tests/outline/tst_outline_plugin.cpp` (mirror `tests/note/tst_note_plugin.cpp`)
- Modify: `tests/outline/CMakeLists.txt`

- [ ] **Step 1: Inspect the precedent**

Read `src/plugin/stock_plugins.cpp` and `tests/note/tst_note_plugin.cpp` to see exactly how
`NotePlugin` is registered and load-tested.

- [ ] **Step 2: Write the failing test**

Create `tests/outline/tst_outline_plugin.cpp` as a copy of `tst_note_plugin.cpp` with
`Note`→`Outline`, `note`→`outline`, asserting the plugin reports the `outline` domain
definition and the org/opml shape contributions. Add
`kalburator_add_outline_test(tst_outline_plugin)`.

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build-dev -j$(($(nproc)-1)) && ctest --test-dir build-dev -R tst_outline_plugin --output-on-failure`
Expected: FAIL — OutlinePlugin not in the stock set / not found.

- [ ] **Step 4: Register the plugin**

In `src/plugin/stock_plugins.cpp`, add the `#include "outlineplugin.h"` and append
`std::make_shared<Kalburator::Outline::OutlinePlugin>()` to the stock plugin list exactly as
`NotePlugin` is added.

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build-dev -j$(($(nproc)-1)) && ctest --test-dir build-dev -R tst_outline_plugin --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Run the full suite (no regressions)**

Run: `ctest --test-dir build-dev --output-on-failure -j$(($(nproc)-1))`
Expected: all green, including the new `tst_outline_*` tests.

- [ ] **Step 7: Commit**

```bash
git add src/plugin/stock_plugins.cpp tests/outline/
git commit -m "feat(outline): register OutlinePlugin in the stock plugin set"
```

---

## Definition of done

- `src/outline/` contains the domain (node model, catalogue, differ/merger, org+opml stages,
  domain definition, stock shapes, plugin).
- `outline` domain registers a `canon` shape + `org`/`opml` peers with correct edges and loss
  profiles; `OutlinePlugin` is in the stock set.
- Tests: node round-trip, catalogue, differ, opml stages, org stages, registry wiring,
  registry round-trip + loss-honesty, plugin load — all pass; full suite green.
- Matches spec `docs/2026-05-25-outline-domain-design.md` §§2–7. (§9 open questions resolved
  as recorded at the top of this plan; §10 items remain out of scope.)

## Deferred to follow-on (NOT in this plan)
- Structural node-level 3-way merge (replace the coarse differ/merger).
- Configurable Org TODO vocab / priority range at doc level; doc title parse from `#+TITLE:`.
- Markdown-tasks and TaskPaper peers.
- Rich Json date objects (tz/floating/precision) in place of ISO strings.
- The ShadowPlan palm/json peers and all WildPalms integration (separate spec).
