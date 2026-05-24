# Shape-Core Foundations Implementation Plan (Plan 1 of 4)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the flat loss model with a four-kind per-property loss model, and generalize the `TransformationRegistry` router from a single canonical hub to a versioned canonical *spine*, proven by a synthetic v1→v2 fixture — all within `src/shape/` and its direct callers.

**Architecture:** `LossProfile` gains a `QHash<PropertyId, LossKind>` (Dropped/Simplified/Reversible/Degraded) replacing the `LossLevel`+`dropped` pair. `TransformationRegistry` replaces `QHash<DomainId, Shape> m_canonicalByDomain` with `QHash<DomainId, QList<Shape>> m_spineByDomain`; `compile()` routes `from → fromAnchor → …spine walk… → toAnchor → to`. A single-node spine reproduces today's behavior exactly.

**Tech Stack:** C++17, Qt6 (Core, Test), CMake. Tests are Qt Test executables registered via `kalburator_add_shape_test()` in `tests/shape/CMakeLists.txt`, run with `ctest`.

**Campaign context:** This is Plan 1 of 4 from the design set:
- `docs/2026-05-23-canon-upgrade-and-convergence-design.md` (architecture)
- `docs/2026-05-23-canon-schema-design.md` (canon field schema)
- `docs/2026-05-23-vendor-api-shapes-reference.md` (vendor shapes)
Plan 2 = per-engine registries; Plan 3 = canon encodings; Plan 4 = calendar convergence. This plan delivers no canon yet — only the infrastructure those plans build on.

**Build/run reference (used by every task):**
```bash
# Configure once (if build/ absent):
cmake -S /home/clinton/dev/libkalburator -B /home/clinton/dev/libkalburator/build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
# Build one test target:
cmake --build /home/clinton/dev/libkalburator/build --target tst_loss_profile
# Run one test (verbose):
ctest --test-dir /home/clinton/dev/libkalburator/build -R tst_loss_profile -VV
```

---

## File Structure

**Modified:**
- `src/shape/lossprofile.h` — new `LossKind` enum; `affected` map replaces `level`+`dropped`.
- `src/shape/lossprofile.cpp` — `compose()`, `summary()`, `droppedProperties()`, `lossKindSeverity()`.
- `src/shape/transformationregistry.h` — `m_spineByDomain`; `appendCanonicalVersion()`, `canonicalSpine()`.
- `src/shape/transformationregistry.cpp` — spine-aware `declareCanonical`, `canonicalFor`, `compileImpl`, `unregisterShapes`, `clear`.
- `src/contacts/vcard3to4transformation.cpp` — construct `LossProfile` the new way.
- `src/todo/todotxttransformation.cpp` — construct `LossProfile` the new way (note: `rrule` is `Simplified`, not `Dropped`).
- Tests updated to the new API: `tests/shape/tst_loss_profile.cpp`, `tests/shape/tst_pipeline.cpp`, `tests/shape/tst_transformation_registry.cpp`, `tests/contacts/tst_vcard3_vcard4_edge.cpp`, `tests/engine/tst_engine_unified_routing.cpp`, `tests/engine/tst_carddav_engine_integration.cpp`.

**Created:**
- `tests/shape/tst_canonical_spine.cpp` — spine routing + v1→v2 fixture + round-trip guard.
- Registered in `tests/shape/CMakeLists.txt`.

---

## PHASE A — Four-level loss model

### Task 1: New `LossProfile` shape (kinds map + helpers)

**Files:**
- Modify: `src/shape/lossprofile.h`
- Modify: `src/shape/lossprofile.cpp`
- Test: `tests/shape/tst_loss_profile.cpp`

- [ ] **Step 1: Rewrite the loss-profile test to the new API**

Replace the entire body of `tests/shape/tst_loss_profile.cpp` with:

```cpp
#include <QTest>
#include "lossprofile.h"

using namespace Kalburator::Shape;

static LossProfile dropped(std::initializer_list<const char*> props) {
    LossProfile p;
    for (const auto* s : props) p.affected.insert(PropertyId{QString::fromUtf8(s)}, LossKind::Dropped);
    return p;
}

class TestLossProfile : public QObject {
    Q_OBJECT
private slots:
    void losslessByDefault() {
        LossProfile p;
        QVERIFY(p.isLossless());
        QCOMPARE(p.summary(), QStringLiteral("lossless"));
    }

    void composeUnionsAffected() {
        const LossProfile r = dropped({"x", "y"}).compose(dropped({"y", "z"}));
        QCOMPARE(r.affected.size(), 3);            // x, y, z (y deduped)
        QVERIFY(!r.isLossless());
    }

    void composeKeepsMoreSevereKind() {
        LossProfile a; a.affected.insert(PropertyId{QStringLiteral("rrule")}, LossKind::Reversible);
        LossProfile b; b.affected.insert(PropertyId{QStringLiteral("rrule")}, LossKind::Dropped);
        const LossProfile r = a.compose(b);
        QCOMPARE(r.affected.value(PropertyId{QStringLiteral("rrule")}), LossKind::Dropped);
    }

    void droppedPropertiesFiltersByKind() {
        LossProfile p;
        p.affected.insert(PropertyId{QStringLiteral("gender")}, LossKind::Dropped);
        p.affected.insert(PropertyId{QStringLiteral("rrule")}, LossKind::Simplified);
        const auto d = p.droppedProperties();
        QCOMPARE(d.size(), 1);
        QVERIFY(d.contains(PropertyId{QStringLiteral("gender")}));
    }

    void summaryGroupsByKind() {
        LossProfile p;
        p.affected.insert(PropertyId{QStringLiteral("gender")}, LossKind::Dropped);
        p.affected.insert(PropertyId{QStringLiteral("rrule")}, LossKind::Simplified);
        QCOMPARE(p.summary(), QStringLiteral("drops gender; simplifies rrule"));
    }
};

QTEST_GUILESS_MAIN(TestLossProfile)
#include "tst_loss_profile.moc"
```

- [ ] **Step 2: Run the test to confirm it fails to compile**

Run: `cmake --build /home/clinton/dev/libkalburator/build --target tst_loss_profile`
Expected: compile error — `LossProfile` has no member `affected`; no `LossKind`.

- [ ] **Step 3: Rewrite `lossprofile.h`**

Replace the entire file with:

```cpp
#pragma once

#include <QHash>
#include <QSet>
#include <QString>

#include "propertycatalogue.h"

namespace Kalburator::Shape {

/// How a single property is affected when a pipeline transforms a record.
enum class LossKind {
    Dropped,      // target cannot represent the property; information is gone
    Simplified,   // property survives in a reduced form (e.g. RRULE -> patternedRecurrence)
    Reversible,   // moved into an extension/X- property; a round-trip is lossless
    Degraded,     // mapped through a lossy many-to-one vocabulary; original kept verbatim
};

/// Severity ordering used when composing two profiles that touch the same
/// property: Reversible(0) < Degraded(1) < Simplified(2) < Dropped(3).
int lossKindSeverity(LossKind) noexcept;

struct LossProfile {
    QHash<PropertyId, LossKind> affected;

    bool isLossless() const noexcept { return affected.isEmpty(); }

    /// Composes self with a downstream profile when stacking edges into a
    /// pipeline. Result is the union of affected maps; on a key collision the
    /// more severe kind wins. Composition is associative.
    LossProfile compose(const LossProfile& downstream) const;

    /// Human-readable summary, e.g. "lossless" or "drops gender; simplifies rrule".
    QString summary() const;

    /// Keys whose loss kind is Dropped (policy / compatibility helper).
    QSet<PropertyId> droppedProperties() const;
};

}  // namespace Kalburator::Shape
```

- [ ] **Step 4: Rewrite `lossprofile.cpp`**

Replace the entire file with:

```cpp
#include "lossprofile.h"

#include <QStringList>

namespace Kalburator::Shape {

int lossKindSeverity(LossKind k) noexcept {
    switch (k) {
        case LossKind::Reversible: return 0;
        case LossKind::Degraded:   return 1;
        case LossKind::Simplified: return 2;
        case LossKind::Dropped:    return 3;
    }
    return 0;
}

LossProfile LossProfile::compose(const LossProfile& downstream) const {
    LossProfile out;
    out.affected = affected;
    for (auto it = downstream.affected.constBegin(); it != downstream.affected.constEnd(); ++it) {
        const auto existing = out.affected.constFind(it.key());
        if (existing == out.affected.constEnd()
            || lossKindSeverity(it.value()) > lossKindSeverity(existing.value())) {
            out.affected.insert(it.key(), it.value());
        }
    }
    return out;
}

QSet<PropertyId> LossProfile::droppedProperties() const {
    QSet<PropertyId> s;
    for (auto it = affected.constBegin(); it != affected.constEnd(); ++it)
        if (it.value() == LossKind::Dropped) s.insert(it.key());
    return s;
}

QString LossProfile::summary() const {
    if (affected.isEmpty()) return QStringLiteral("lossless");
    QStringList drop, simp, rev, deg;
    for (auto it = affected.constBegin(); it != affected.constEnd(); ++it) {
        const QString id = it.key().toString();
        switch (it.value()) {
            case LossKind::Dropped:    drop << id; break;
            case LossKind::Simplified: simp << id; break;
            case LossKind::Reversible: rev  << id; break;
            case LossKind::Degraded:   deg  << id; break;
        }
    }
    QStringList parts;
    const auto add = [&](const QString& verb, QStringList& l) {
        if (!l.isEmpty()) { l.sort(); parts << verb + QStringLiteral(" ") + l.join(QStringLiteral(", ")); }
    };
    add(QStringLiteral("drops"), drop);
    add(QStringLiteral("simplifies"), simp);
    add(QStringLiteral("stashes"), rev);
    add(QStringLiteral("degrades"), deg);
    return parts.join(QStringLiteral("; "));
}

}  // namespace Kalburator::Shape
```

- [ ] **Step 5: Build and run; expect pass**

Run: `cmake --build /home/clinton/dev/libkalburator/build --target tst_loss_profile && ctest --test-dir /home/clinton/dev/libkalburator/build -R tst_loss_profile -VV`
Expected: PASS (5 test cases). (Other targets won't build yet — that's Task 2.)

- [ ] **Step 6: Commit**

```bash
git add src/shape/lossprofile.h src/shape/lossprofile.cpp tests/shape/tst_loss_profile.cpp
git commit -m "shape: four-kind LossProfile (Dropped/Simplified/Reversible/Degraded)"
```

---

### Task 2: Migrate all `LossProfile` construction & assertion sites

The new struct drops `level` and `dropped`. Fix every site found by:
`grep -rn "LossLevel\|\.dropped\|->dropped\|\.level" src/ tests/ --include=*.cpp --include=*.h`

**Files:**
- Modify: `src/shape/transformationregistry.cpp:70-78` (the `registerEdge` idempotence check)
- Modify: `src/contacts/vcard3to4transformation.cpp:44-58`
- Modify: `src/todo/todotxttransformation.cpp:135-142`
- Modify: `tests/shape/tst_pipeline.cpp`, `tests/shape/tst_transformation_registry.cpp`,
  `tests/contacts/tst_vcard3_vcard4_edge.cpp`, `tests/engine/tst_engine_unified_routing.cpp`,
  `tests/engine/tst_carddav_engine_integration.cpp`

- [ ] **Step 1: Fix `registerEdge` idempotence check**

In `src/shape/transformationregistry.cpp`, replace lines 72-76 (the `sameLevel`/`sameDropped` block) with:

```cpp
        // Idempotent on identical re-registration; assert on conflict.
        const bool same = existing->loss.affected == edge.loss.affected;
        Q_ASSERT_X(same,
                   "TransformationRegistry::registerEdge",
                   "conflicting re-registration of (from, to) edge");
        return;
```

- [ ] **Step 2: Fix the vCard3→4 transformation loss**

In `src/contacts/vcard3to4transformation.cpp`, replace the loss-profile construction (the `p.level = ...` line and the four `p.dropped.insert(...)` lines, around 45-58) with:

```cpp
    p.affected.insert(PropertyId{QStringLiteral("gender")}, LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("kind")},   LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("lang")},   LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("member")}, LossKind::Dropped);
```

Ensure `#include "lossprofile.h"` is present and `using`/qualified `LossKind` resolves (it is in `Kalburator::Shape`). Remove any now-unused `LossLevel` reference.

- [ ] **Step 3: Fix the todotxt transformation loss**

In `src/todo/todotxttransformation.cpp`, replace the `p.level = ...` + six `p.dropped.insert(...)` lines (135-142) with — note `rrule` is **Simplified**, the rest **Dropped**:

```cpp
    p.affected.insert(PropertyId{QStringLiteral("description")},      LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("attendees")},        LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("rrule")},            LossKind::Simplified);
    p.affected.insert(PropertyId{QStringLiteral("attachments")},      LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("alarms")},           LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("customproperties")}, LossKind::Dropped);
```

- [ ] **Step 4: Fix the test helpers and assertions**

In `tests/shape/tst_pipeline.cpp` and `tests/shape/tst_transformation_registry.cpp`, the local helper builds a lossy profile via `p.level = LossLevel::IntraDomainLossy; p.dropped.insert(PropertyId{dropped});`. Replace each such helper body with:

```cpp
    p.affected.insert(PropertyId{dropped}, LossKind::Dropped);
```

Replace assertions:
- `QCOMPARE(p.composedLoss().level, LossLevel::Lossless);` → `QVERIFY(p.composedLoss().isLossless());`
- `QCOMPARE(loss.level, LossLevel::IntraDomainLossy);` / `QCOMPARE(p->composedLoss().level, LossLevel::IntraDomainLossy);` / `QCOMPARE(lp.level, LossLevel::IntraDomainLossy);` → `QVERIFY(!<expr>.isLossless());`
- `loss.dropped` / `lp.dropped` / `p.composedLoss().dropped` → `<expr>.droppedProperties()` (same `QSet<PropertyId>` API: `.size()`, `.contains(...)`, `.isEmpty()`, range-for).

In `tests/contacts/tst_vcard3_vcard4_edge.cpp`: remove `using Kalburator::Shape::LossLevel;`; change `QCOMPARE(loss.level, LossLevel::IntraDomainLossy);` → `QVERIFY(!loss.isLossless());`; change `loss.dropped` (both the `.isEmpty()` check and the `for (const auto &p : loss.dropped)`) → `loss.droppedProperties()`.

In `tests/engine/tst_engine_unified_routing.cpp`: remove `using Kalburator::Shape::LossLevel;`; change `QCOMPARE(host.lastLossProfile().level, LossLevel::Lossless);` → `QVERIFY(host.lastLossProfile().isLossless());`.

In `tests/engine/tst_carddav_engine_integration.cpp`: remove `using Kalburator::Shape::LossLevel;` (line 90; confirm no other `LossLevel`/`.dropped`/`.level` references remain in the file via grep — if any, apply the same substitutions).

- [ ] **Step 5: Build everything; run the full suite**

Run: `cmake --build /home/clinton/dev/libkalburator/build && ctest --test-dir /home/clinton/dev/libkalburator/build`
Expected: full build succeeds; all tests PASS. If any target still references `LossLevel`/`.level`/`.dropped`, the compiler names the file:line — fix with the substitutions above and rebuild.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "shape: migrate all LossProfile call sites to the kinds map"
```

---

## PHASE B — Versioned canonical spine

### Task 3: Spine storage + `appendCanonicalVersion` / `canonicalSpine`

**Files:**
- Modify: `src/shape/transformationregistry.h`
- Modify: `src/shape/transformationregistry.cpp`
- Test: `tests/shape/tst_canonical_spine.cpp` (created here)
- Modify: `tests/shape/CMakeLists.txt`

- [ ] **Step 1: Register a new test target**

In `tests/shape/CMakeLists.txt`, after the existing `kalburator_add_shape_test(tst_transformation_registry)` line, add:

```cmake
kalburator_add_shape_test(tst_canonical_spine)
```

- [ ] **Step 2: Write the failing spine-storage test**

Create `tests/shape/tst_canonical_spine.cpp`:

```cpp
#include <QTest>
#include "transformationregistry.h"
#include "propertycatalogue.h"

using namespace Kalburator::Shape;

static Shape cal(const char* enc) {
    return Shape{ DomainId{QStringLiteral("calendar")}, EncodingId{QString::fromUtf8(enc)} };
}
static PropertyCatalogue stubCat() {
    PropertyCatalogue c;
    c.addProperty(PropertyDescriptor{ PropertyId{QStringLiteral("uid")}, PropertyKind::String, QStringLiteral("uid"), false });
    return c;
}

class TestCanonicalSpine : public QObject {
    Q_OBJECT
private slots:
    void cleanup() { TransformationRegistry::instance().clear(); }

    void singleNodeSpineHeadIsCanonical() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(cal("canon"), stubCat());
        r.declareCanonical(DomainId{QStringLiteral("calendar")}, cal("canon"));
        QCOMPARE(r.canonicalFor(DomainId{QStringLiteral("calendar")}), cal("canon"));
        QCOMPARE(r.canonicalSpine(DomainId{QStringLiteral("calendar")}).size(), 1);
    }

    void appendMakesNewHead() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(cal("canon"),  stubCat());
        r.registerShape(cal("canon2"), stubCat());
        r.declareCanonical(DomainId{QStringLiteral("calendar")}, cal("canon"));
        r.appendCanonicalVersion(DomainId{QStringLiteral("calendar")}, cal("canon2"));
        QCOMPARE(r.canonicalFor(DomainId{QStringLiteral("calendar")}), cal("canon2"));   // head moved
        const auto spine = r.canonicalSpine(DomainId{QStringLiteral("calendar")});
        QCOMPARE(spine.size(), 2);
        QCOMPARE(spine.first(), cal("canon"));
        QCOMPARE(spine.last(),  cal("canon2"));
    }
};

QTEST_GUILESS_MAIN(TestCanonicalSpine)
#include "tst_canonical_spine.moc"
```

- [ ] **Step 3: Run; expect compile failure**

Run: `cmake --build /home/clinton/dev/libkalburator/build --target tst_canonical_spine`
Expected: FAIL — `canonicalSpine` / `appendCanonicalVersion` not declared.

- [ ] **Step 4: Update the header**

In `src/shape/transformationregistry.h`:
- Replace the member `QHash<DomainId, Shape> m_canonicalByDomain;` with `QHash<DomainId, QList<Shape>> m_spineByDomain;`.
- Add `#include <QList>` if not already present (it is).
- After the `declareCanonical` declaration, add:

```cpp
    /// Append a newer canonical version to a domain's spine, making it the
    /// new head (current canonical). Requires the spine to already exist
    /// (declareCanonical first) and the domain not yet frozen. The bridge
    /// edges between the previous head and `newCanonical` must be registered
    /// separately. Idempotent if `newCanonical` is already the head.
    void appendCanonicalVersion(DomainId domain, Shape newCanonical);

    /// The full ordered canonical spine for a domain (oldest → current).
    /// Empty if no canonical declared.
    QList<Shape> canonicalSpine(const DomainId&) const;
```

- [ ] **Step 5: Update the implementation**

In `src/shape/transformationregistry.cpp`, replace `declareCanonical`, `canonicalFor`, and `unregisterShapes` with the spine-aware versions, and add `appendCanonicalVersion` + `canonicalSpine`:

```cpp
void TransformationRegistry::declareCanonical(DomainId domain, Shape canonical) {
    if (m_frozenDomains.contains(domain)) {
        qWarning("TransformationRegistry::declareCanonical: domain is frozen — redeclaration ignored");
        return;
    }
    auto it = m_spineByDomain.find(domain);
    if (it != m_spineByDomain.end() && !it->isEmpty()) {
        if (it->first() != canonical) {
            qCritical("TransformationRegistry::declareCanonical: "
                      "conflicting canonical for same domain — "
                      "second plugin must not redeclare; declaration ignored");
        }
        return;  // idempotent same-value (compares the v1 root)
    }
    m_spineByDomain.insert(domain, QList<Shape>{ canonical });
}

void TransformationRegistry::appendCanonicalVersion(DomainId domain, Shape newCanonical) {
    if (m_frozenDomains.contains(domain)) {
        qWarning("TransformationRegistry::appendCanonicalVersion: domain is frozen — ignored");
        return;
    }
    auto it = m_spineByDomain.find(domain);
    if (it == m_spineByDomain.end() || it->isEmpty()) {
        qCritical("TransformationRegistry::appendCanonicalVersion: no canonical declared yet");
        return;
    }
    if (it->last() == newCanonical) return;  // idempotent
    it->append(newCanonical);
}

Shape TransformationRegistry::canonicalFor(const DomainId& d) const {
    auto it = m_spineByDomain.constFind(d);
    if (it == m_spineByDomain.constEnd() || it->isEmpty()) return Shape::Any();
    return it->last();  // head = current canonical
}

QList<Shape> TransformationRegistry::canonicalSpine(const DomainId& d) const {
    return m_spineByDomain.value(d);
}
```

And in `unregisterShapes`, replace the canonical-removal block with spine-aware removal:

```cpp
void TransformationRegistry::unregisterShapes(const QList<Shape> &shapes) {
    for (const auto &s : shapes) {
        m_catalogues.remove(s);
        auto it = m_spineByDomain.find(s.domain);
        if (it != m_spineByDomain.end()) {
            it->removeAll(s);
            if (it->isEmpty()) m_spineByDomain.erase(it);
        }
    }
}
```

And in `clear()`, replace `m_canonicalByDomain.clear();` with `m_spineByDomain.clear();`.

- [ ] **Step 6: Build & run; expect pass**

Run: `cmake --build /home/clinton/dev/libkalburator/build --target tst_canonical_spine && ctest --test-dir /home/clinton/dev/libkalburator/build -R tst_canonical_spine -VV`
Expected: PASS (2 cases).

- [ ] **Step 7: Commit**

```bash
git add src/shape/transformationregistry.h src/shape/transformationregistry.cpp \
        tests/shape/tst_canonical_spine.cpp tests/shape/CMakeLists.txt
git commit -m "shape: TransformationRegistry stores a versioned canonical spine"
```

---

### Task 4: Spine-aware `compileImpl` routing

**Files:**
- Modify: `src/shape/transformationregistry.cpp` (`compileImpl`)
- Test: `tests/shape/tst_canonical_spine.cpp` (extend)

- [ ] **Step 1: Add spine-routing test cases**

Add these private-slot methods to `TestCanonicalSpine` in `tests/shape/tst_canonical_spine.cpp`. (Reuse the `cal`/`stubCat` helpers; add an identity transformation stage so edges can be built.)

Add this include and a trivial stage near the top of the file (after the existing includes):

```cpp
#include "transformationedge.h"
#include "pipeline.h"

namespace {
class IdentityStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& in) const override { return in; }
};
Kalburator::Shape::TransformationEdge edge(Kalburator::Shape::Shape from,
                                           Kalburator::Shape::Shape to) {
    return Kalburator::Shape::TransformationEdge{
        from, to, Kalburator::Shape::LossProfile{}, std::make_shared<IdentityStage>() };
}
}  // namespace
```

New test cases:

```cpp
    void singleNodeReproducesPeerToCanon() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(cal("canon"), stubCat());
        r.registerShape(cal("ical"),  stubCat());
        r.declareCanonical(DomainId{QStringLiteral("calendar")}, cal("canon"));
        r.registerEdge(edge(cal("ical"), cal("canon")));
        r.registerEdge(edge(cal("canon"), cal("ical")));
        auto promote = r.compile(cal("ical"), cal("canon"));
        QVERIFY(promote.has_value());
        QCOMPARE(promote->edges().size(), 1);
        auto demote = r.compile(cal("canon"), cal("ical"));
        QVERIFY(demote.has_value());
        QCOMPARE(demote->edges().size(), 1);
    }

    void singleNodePeerToPeerGoesThroughHub() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(cal("canon"), stubCat());
        r.registerShape(cal("ical"),  stubCat());
        r.registerShape(cal("org"),   stubCat());
        r.declareCanonical(DomainId{QStringLiteral("calendar")}, cal("canon"));
        r.registerEdge(edge(cal("ical"), cal("canon")));
        r.registerEdge(edge(cal("canon"), cal("org")));
        auto p = r.compile(cal("ical"), cal("org"));
        QVERIFY(p.has_value());
        QCOMPARE(p->edges().size(), 2);   // ical -> canon -> org
    }

    void twoNodeSpinePromotesPeerToHead() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(cal("canon"),  stubCat());
        r.registerShape(cal("canon2"), stubCat());
        r.registerShape(cal("ical"),   stubCat());
        r.declareCanonical(DomainId{QStringLiteral("calendar")}, cal("canon"));
        r.registerEdge(edge(cal("ical"),  cal("canon")));   // peer attaches at v1
        r.registerEdge(edge(cal("canon"), cal("ical")));
        r.registerEdge(edge(cal("canon"), cal("canon2")));  // bridge v1 -> v2
        r.registerEdge(edge(cal("canon2"), cal("canon")));  // bridge v2 -> v1
        r.appendCanonicalVersion(DomainId{QStringLiteral("calendar")}, cal("canon2"));
        // peer (attached at v1) promoted to head v2: ical -> canon -> canon2
        auto promote = r.compile(cal("ical"), cal("canon2"));
        QVERIFY(promote.has_value());
        QCOMPARE(promote->edges().size(), 2);
        // head v2 demoted back to peer: canon2 -> canon -> ical
        auto demote = r.compile(cal("canon2"), cal("ical"));
        QVERIFY(demote.has_value());
        QCOMPARE(demote->edges().size(), 2);
    }

    void unbridgedSpineGapFailsToCompile() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(cal("canon"),  stubCat());
        r.registerShape(cal("canon2"), stubCat());
        r.registerShape(cal("ical"),   stubCat());
        r.declareCanonical(DomainId{QStringLiteral("calendar")}, cal("canon"));
        r.registerEdge(edge(cal("ical"), cal("canon")));
        r.appendCanonicalVersion(DomainId{QStringLiteral("calendar")}, cal("canon2"));
        // no bridge canon->canon2 registered:
        QVERIFY(!r.compile(cal("ical"), cal("canon2")).has_value());
    }
```

- [ ] **Step 2: Run; expect failures**

Run: `cmake --build /home/clinton/dev/libkalburator/build --target tst_canonical_spine && ctest --test-dir /home/clinton/dev/libkalburator/build -R tst_canonical_spine -VV`
Expected: the two-node cases FAIL (current `compileImpl` only does 2-hop through a single hub).

- [ ] **Step 3: Replace `compileImpl` with spine routing**

In `src/shape/transformationregistry.cpp`, replace the whole body of `compileImpl` (lines 88-131) with:

```cpp
std::optional<Pipeline> TransformationRegistry::compileImpl(Shape from, Shape to) const {
    if (to.isAny()) return Pipeline{from};
    if (from.isAny()) return std::nullopt;
    if (from == to) return Pipeline{from};
    if (from.domain != to.domain) return std::nullopt;  // cross-domain not in v1

    const QList<Shape> spine = m_spineByDomain.value(from.domain);
    if (spine.isEmpty()) return std::nullopt;

    QList<TransformationEdge> edges;

    // 1. Source side: resolve `from` to a spine node `fromIdx`.
    int fromIdx = spine.indexOf(from);
    if (fromIdx < 0) {
        const TransformationEdge* lead = nullptr;
        for (int i = 0; i < spine.size(); ++i) {
            if (const auto* e = findEdge(from, spine[i])) { lead = e; fromIdx = i; break; }
        }
        if (!lead) return std::nullopt;
        edges.append(*lead);
    }

    // 2. Target side: resolve `to` to a spine node `toIdx` (tail edge applied last).
    int toIdx = spine.indexOf(to);
    const TransformationEdge* tail = nullptr;
    if (toIdx < 0) {
        for (int i = 0; i < spine.size(); ++i) {
            if (const auto* e = findEdge(spine[i], to)) { tail = e; toIdx = i; break; }
        }
        if (!tail) return std::nullopt;
    }

    // 3. Walk the spine between the two anchors via adjacent bridge edges.
    if (fromIdx < toIdx) {
        for (int i = fromIdx; i < toIdx; ++i) {
            const auto* e = findEdge(spine[i], spine[i + 1]);
            if (!e) return std::nullopt;
            edges.append(*e);
        }
    } else if (fromIdx > toIdx) {
        for (int i = fromIdx; i > toIdx; --i) {
            const auto* e = findEdge(spine[i], spine[i - 1]);
            if (!e) return std::nullopt;
            edges.append(*e);
        }
    }

    // 4. Apply the target tail edge last.
    if (tail) edges.append(*tail);

    if (edges.isEmpty()) return std::nullopt;  // defensive; unreachable when from != to
    return Pipeline{ edges };
}
```

- [ ] **Step 4: Run the spine test + the existing registry/pipeline tests**

Run: `cmake --build /home/clinton/dev/libkalburator/build && ctest --test-dir /home/clinton/dev/libkalburator/build -R "tst_canonical_spine|tst_transformation_registry|tst_pipeline" -VV`
Expected: all PASS. The existing `tst_transformation_registry` cases (identity, single-leg, two-leg-through-hub) confirm the single-node spine reproduces the old behavior.

- [ ] **Step 5: Run the FULL suite (guard against engine/contacts/todo regressions)**

Run: `ctest --test-dir /home/clinton/dev/libkalburator/build`
Expected: all PASS.

- [ ] **Step 6: Commit**

```bash
git add src/shape/transformationregistry.cpp tests/shape/tst_canonical_spine.cpp
git commit -m "shape: compile() routes along the canonical spine (N-hop)"
```

---

### Task 5: Freeze guards a frozen spine against late version appends

**Files:**
- Test: `tests/shape/tst_canonical_spine.cpp` (extend)

(The implementation already guards: `appendCanonicalVersion` checks `m_frozenDomains`. This task pins that contract.)

- [ ] **Step 1: Add the freeze test**

Add to `TestCanonicalSpine`:

```cpp
    void appendAfterCompileIsRejected() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(cal("canon"),  stubCat());
        r.registerShape(cal("canon2"), stubCat());
        r.registerShape(cal("ical"),   stubCat());
        r.declareCanonical(DomainId{QStringLiteral("calendar")}, cal("canon"));
        r.registerEdge(edge(cal("ical"), cal("canon")));
        // First non-identity compile freezes the calendar domain:
        QVERIFY(r.compile(cal("ical"), cal("canon")).has_value());
        QVERIFY(r.isFrozen(DomainId{QStringLiteral("calendar")}));
        // Appending a version now must be ignored (spine stays size 1):
        r.appendCanonicalVersion(DomainId{QStringLiteral("calendar")}, cal("canon2"));
        QCOMPARE(r.canonicalSpine(DomainId{QStringLiteral("calendar")}).size(), 1);
    }
```

- [ ] **Step 2: Run; expect pass (no code change needed)**

Run: `cmake --build /home/clinton/dev/libkalburator/build --target tst_canonical_spine && ctest --test-dir /home/clinton/dev/libkalburator/build -R tst_canonical_spine -VV`
Expected: PASS. If it fails, the guard in `appendCanonicalVersion` (Task 3 Step 5) is missing — add the `m_frozenDomains.contains` check.

- [ ] **Step 3: Commit**

```bash
git add tests/shape/tst_canonical_spine.cpp
git commit -m "shape: pin that a frozen domain rejects canonical-version appends"
```

---

## PHASE C — Synthetic v1→v2 fixture + round-trip guard

This phase proves the future-proofing contract (architecture §5.3, §2 "B"): an **unchanged peer edge** keeps working when a v2 canonical is appended, and a `v1 → v2 → v1` round-trip is identity. It uses byte-level JSON stages to mimic a real widening/narrowing without any domain code.

### Task 6: Widening/narrowing fixture stages + auto-extension test

**Files:**
- Test: `tests/shape/tst_canonical_spine.cpp` (extend)

- [ ] **Step 1: Add fixture stages and the auto-extension test**

Add near the top of `tests/shape/tst_canonical_spine.cpp` (after `IdentityStage`):

```cpp
#include <QJsonDocument>
#include <QJsonObject>

namespace {
// v1 -> v2 widening: add a v2-only field with a default. Lossless.
class WidenStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& in) const override {
        QJsonObject o = QJsonDocument::fromJson(in).object();
        if (!o.contains(QStringLiteral("v2field")))
            o.insert(QStringLiteral("v2field"), QStringLiteral("default"));
        return QJsonDocument(o).toJson(QJsonDocument::Compact);
    }
};
// v2 -> v1 narrowing: drop the v2-only field.
class NarrowStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& in) const override {
        QJsonObject o = QJsonDocument::fromJson(in).object();
        o.remove(QStringLiteral("v2field"));
        return QJsonDocument(o).toJson(QJsonDocument::Compact);
    }
};
Kalburator::Shape::TransformationEdge widenEdge(Kalburator::Shape::Shape f, Kalburator::Shape::Shape t) {
    return { f, t, Kalburator::Shape::LossProfile{}, std::make_shared<WidenStage>() };
}
Kalburator::Shape::TransformationEdge narrowEdge(Kalburator::Shape::Shape f, Kalburator::Shape::Shape t) {
    Kalburator::Shape::LossProfile loss;   // v2field is reversible: re-widen restores the default
    loss.affected.insert(Kalburator::Shape::PropertyId{QStringLiteral("v2field")},
                         Kalburator::Shape::LossKind::Reversible);
    return { f, t, loss, std::make_shared<NarrowStage>() };
}
}  // namespace
```

Add the test case:

```cpp
    void unchangedPeerEdgeSurvivesVersionBump() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(cal("canon"),  stubCat());
        r.registerShape(cal("ical"),   stubCat());
        r.declareCanonical(DomainId{QStringLiteral("calendar")}, cal("canon"));
        // The "third-party" peer edge — written once, never touched again:
        r.registerEdge(edge(cal("ical"), cal("canon")));
        r.registerEdge(edge(cal("canon"), cal("ical")));

        // Library ships a v2 canon + bridge, WITHOUT touching the peer edge:
        r.registerShape(cal("canon2"), stubCat());
        r.registerEdge(widenEdge(cal("canon"),  cal("canon2")));
        r.registerEdge(narrowEdge(cal("canon2"), cal("canon")));
        r.appendCanonicalVersion(DomainId{QStringLiteral("calendar")}, cal("canon2"));

        // The unchanged peer now reaches the new head automatically:
        auto promote = r.compile(cal("ical"), cal("canon2"));
        QVERIFY(promote.has_value());
        const QByteArray out = promote->apply(QByteArray("{\"uid\":\"A\"}"));
        const QJsonObject o = QJsonDocument::fromJson(out).object();
        QVERIFY(o.contains(QStringLiteral("v2field")));   // widened by the auto-inserted bridge
        QCOMPARE(o.value(QStringLiteral("uid")).toString(), QStringLiteral("A"));
    }
```

- [ ] **Step 2: Run; expect pass**

Run: `cmake --build /home/clinton/dev/libkalburator/build --target tst_canonical_spine && ctest --test-dir /home/clinton/dev/libkalburator/build -R tst_canonical_spine -VV`
Expected: PASS — the unchanged `ical → canon` edge composes with the auto-inserted `canon → canon2` bridge.

- [ ] **Step 3: Commit**

```bash
git add tests/shape/tst_canonical_spine.cpp
git commit -m "shape: prove an unchanged peer edge survives a canon version bump"
```

---

### Task 7: `v1 → v2 → v1` round-trip compatibility guard (the CI "B" check)

**Files:**
- Test: `tests/shape/tst_canonical_spine.cpp` (extend)

- [ ] **Step 1: Add the round-trip guard test**

Add to `TestCanonicalSpine`:

```cpp
    void spineRoundTripIsIdentityForWidenedFields() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(cal("canon"),  stubCat());
        r.registerShape(cal("canon2"), stubCat());
        r.declareCanonical(DomainId{QStringLiteral("calendar")}, cal("canon"));
        r.registerEdge(widenEdge(cal("canon"),  cal("canon2")));
        r.registerEdge(narrowEdge(cal("canon2"), cal("canon")));
        r.appendCanonicalVersion(DomainId{QStringLiteral("calendar")}, cal("canon2"));

        // A v2 record that only uses fields v1 also has must survive v2->v1->v2:
        const QByteArray v2in = QByteArray("{\"uid\":\"A\",\"v2field\":\"default\"}");
        auto down = r.compile(cal("canon2"), cal("canon"));
        auto up   = r.compile(cal("canon"),  cal("canon2"));
        QVERIFY(down.has_value() && up.has_value());
        const QByteArray back = up->apply(down->apply(v2in));
        const QJsonObject o = QJsonDocument::fromJson(back).object();
        QCOMPARE(o.value(QStringLiteral("uid")).toString(), QStringLiteral("A"));
        QCOMPARE(o.value(QStringLiteral("v2field")).toString(), QStringLiteral("default"));

        // And the narrowing declares its single field loss as Reversible:
        QCOMPARE(down->composedLoss().affected.value(
                     PropertyId{QStringLiteral("v2field")}), LossKind::Reversible);
    }
```

- [ ] **Step 2: Run; expect pass**

Run: `cmake --build /home/clinton/dev/libkalburator/build --target tst_canonical_spine && ctest --test-dir /home/clinton/dev/libkalburator/build -R tst_canonical_spine -VV`
Expected: PASS.

- [ ] **Step 3: Run the FULL suite a final time**

Run: `ctest --test-dir /home/clinton/dev/libkalburator/build`
Expected: all PASS.

- [ ] **Step 4: Commit**

```bash
git add tests/shape/tst_canonical_spine.cpp
git commit -m "shape: v1->v2->v1 round-trip compatibility guard for the spine"
```

---

## Plan 1 done — what it delivered

- `LossProfile` now classifies each affected property as Dropped / Simplified / Reversible / Degraded (architecture §6); all call sites migrated.
- `TransformationRegistry` holds a **versioned canonical spine**; `compile()` routes N-hop along it; a single-node spine reproduces the prior behavior (existing tests still green).
- A synthetic fixture proves the future-proofing contract: an unchanged peer edge survives a canon-version append, and `v1→v2→v1` round-trips (architecture §5.3, acceptance "synthetic v1→v2 fixture" + "CI round-trip check").

## Next plans (outlines — full task detail written when this lands, against the concrete APIs above)

- **Plan 2 — Per-engine registries.** Inject `TransformationRegistry` & `DomainRegistry` into `SyncEngine`; thread references through collaborators; remove `instance()` from the engine path; delete the defensive `clear()` rituals in tests (architecture §8).
- **Plan 3 — Canon encodings.** Implement `calendar+canon`/`contacts+canon`/`todo+canon` per the canon-schema doc: `canonicalCatalogue()` (the `PropertyId`/`PropertyKind` tables), JSON (de)serialization `TransformationStage`s for the iCal/vCard/vtodo↔canon bridge edges (with §6-classified `LossProfile`s), and `RecordDiffer`/`RecordMerger` operating on the JSON canon (recurrence/extras/hierarchy diffed whole).
- **Plan 4 — Calendar convergence.** Retire `src/transcoding/`; re-express org-mode RRULE simplification as a `canon → org-ical` edge (Simplified loss); remove the `ApplyContext.transcodingPlan` seam and `CalendarPluginWriter` special-casing; calendar routes entirely through the shape graph. Gate on the PlanStan ctest baseline.
