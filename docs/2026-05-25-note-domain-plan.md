# Note Domain Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Evolve the thin `(memo, text)` domain into a `note` plaintext-carrier domain with a canon head `(note, canon)`, a `(note, markdown)` peer whose YAML frontmatter round-trips byte-for-byte through `providerExtras`, and a `MarkdownFilesBackend` sink that writes human-readable `.md` files.

**Architecture:** Stratum-1 design — the canon `body` is the **verbatim** Markdown/raw text (never parsed). The `markdown ↔ canon` edge carries the frontmatter block **verbatim** in `providerExtras["frontmatter"]` (declared `Reversible`), peeking only at the `id:` line to give the engine a `uid`. The existing line-based `TextDiffer`/`TextMerger` are reused unchanged. The sink subclasses `RawFilesBackend`, overriding only file suffix (`.md`) and filename derivation (first body line). All additive; no converged API changes (invariant 1).

**Tech Stack:** C++17 / Qt6 (`QJsonDocument`/`QJsonObject`, `QRegularExpression`), CMake, QtTest. Build/test commands per `docs/campaign/STATUS.md`.

**Source of truth:** `docs/2026-05-25-note-domain-design.md`; WildPalms requirements `~/dev/WildPalms/docs/2026-05-25-document-domain-requirements-for-libkalburator.md`; INVARIANTS 1, 2, 4, 8; landed-API signatures in `docs/2026-05-24-plan-3-canon-encodings.md`.

---

## Landed APIs this plan builds against (verified in tree, invariant P1)

- `struct Shape { DomainId domain; EncodingId encoding; static Shape Any(); QString toString() const; };` — `src/shape/shape.h`. Aggregate-init: `Shape{ DomainId{"note"}, EncodingId{"canon"} }`.
- `class TransformationStage { virtual QByteArray transform(const QByteArray&) const = 0; };`, `class IdentityStage : public TransformationStage`; `struct TransformationEdge { Shape from; Shape to; LossProfile loss; std::shared_ptr<TransformationStage> stage; };` — `src/shape/transformationedge.h`.
- `enum class LossKind { Dropped, Simplified, Reversible, Degraded };` `struct LossProfile { QHash<PropertyId, LossKind> affected; bool isLossless() const; LossProfile compose(const LossProfile&) const; QSet<PropertyId> droppedProperties() const; };` — `src/shape/lossprofile.h`. **No helper ctors** — build via `loss.affected.insert(PropertyId{...}, LossKind::...)`.
- `class ShapeContribution { virtual DomainId targetDomain() const = 0; virtual QList<std::pair<Shape, PropertyCatalogue>> peerShapes() const = 0; virtual QList<TransformationEdge> edges() const = 0; };` — `src/shape/shapecontribution.h`.
- `class DomainDefinition { ... virtual QList<std::pair<Shape,PropertyCatalogue>> canonicalSpine() const { return {{canonicalShape(), canonicalCatalogue()}}; } };` — `src/shape/domaindefinition.h:49`. The default single-node spine is exactly what `note` needs; we override it explicitly for parity/clarity.
- `class Plugin { virtual QList<std::shared_ptr<Shape::DomainDefinition>> domainDefinitions() const; virtual QList<std::shared_ptr<Shape::ShapeContribution>> shapeContributions() const; ... };` — `src/plugin/plugin.h`.
- `TransformationRegistry::compile(Shape from, Shape to) const -> std::optional<Pipeline>`; `Pipeline::composedLoss() const -> LossProfile`; `Pipeline::apply(QByteArray) const -> QByteArray` — `src/shape/transformationregistry.h`, `src/shape/pipeline.h`.
- `namespace CanonEnvelope { int kCanonVersion; QString providerExtrasKey(); QJsonObject parse(const QByteArray&); QByteArray serialize(const QJsonObject&); void stampEnvelope(QJsonObject&, const QString& domain, const QString& uid); QString uid(const QJsonObject&); }` — `src/shape/canonenvelope.h`. `serialize` is compact and **sorts object keys** (deterministic).
- `RawFilesBackend` — `src/universal/rawfilesbackend.{h,cpp}`. Stores `<rootPath>/<sanitize(stem)>.<suffixFor(collectionId)>`. `suffixFor`/`sanitize` are `static`. `createCollection(CollectionInfo, Shape)` declares the per-collection shape. `loadRecords`/`clearCollection` match files by `"." + suffixFor(collectionId)`. `BackendRecord` fields used: `id`, `displayName`, `data`, `contentHash`, `lastModified`, `type` — `src/types/backendrecord.h`.
- `PluginManager` spine consumption — `src/plugin/pluginmanager.cpp:161-181`: calls `declareCanonical(domain, spine[0])` then `appendCanonicalVersion` for the rest; a single-element spine is `declareCanonical`-only.

### Files to model on

- Stock shapes + edges: `src/todo/todostockshapes.{h,cpp}`. Domain definition + spine: `src/todo/tododomaindefinition.{h,cpp}`.
- Differ/merger to lift unchanged: `src/memo/textdiffer.{h,cpp}`, `src/memo/textmerger.{h,cpp}`.
- WildPalms reference dialect: `~/dev/WildPalms/src/plugins/memo/memomarkdown.cpp` (`encode`/`decode`/`filenameFor`/`sanitiseFilenameStem`).

---

## File structure

| File | Responsibility | Change |
|------|----------------|--------|
| `src/note/*` (was `src/memo/*`) | domain definition, catalogue, differ/merger, plugin | **Rename** memo→note + edit |
| `src/note/markdowncanonstages.{h,cpp}` | `MarkdownToCanonStage` / `CanonToMarkdownStage` + frontmatter split/join helpers | **Create** |
| `src/note/notestockshapes.{h,cpp}` | `(note, markdown)` peer + `markdown↔canon` edges | **Create** |
| `src/universal/rawfilesbackend.{h,cpp}` | add protected `virtual suffixFor` + `virtual recordStem` seams (no behavior change) | **Modify** |
| `src/universal/markdownfilesbackend.{h,cpp}` | `.md` sink: title-named files, frontmatter round-trip | **Create** |
| `tests/note/*` (was `tests/memo/*`) | existing memo tests, retargeted to `(note, canon)` | **Rename** + edit |
| `tests/note/tst_markdown_canon_roundtrip.cpp` | byte-stability + frontmatter verbatim + uid | **Create** |
| `tests/note/tst_note_shapes.cpp` | edge compile + loss profile + spine reachability | **Create** |
| `tests/sinks/tst_markdownfiles_backend.cpp` | one `.md`/record, naming, re-read | **Create** |
| `CMakeLists.txt`, `src/plugin/stock_plugins.cpp`, `tests/CMakeLists.txt`, `tests/note/CMakeLists.txt`, `tests/sinks/CMakeLists.txt` | wiring | **Modify** |

---

## Why each task stays green

- **Task 1** is a pure rename + head-flip; the differ/merger are shape-agnostic (operate on `data`), so retargeting the canonical shape to `(note, canon)` keeps the renamed tests green. Nothing else in-tree depends on the `memo` *plugin* (grep confirmed: other "memo" hits are the generic word or throwaway domain-id strings in blob fakes).
- **Task 2** adds new stage files, unit-tested in isolation — no wiring into a plugin yet.
- **Task 3** wires the peer + edges; until it runs, `note` has a canon head but no peer (still valid — like the other domains before their stock shapes).
- **Task 4** is a behavior-preserving refactor (extract two protected virtuals with identical defaults); `tst_rawfiles_backend` proves no regression.
- **Task 5** adds a subclass + its own test.
- **Task 6** is docs only.

---

## Task 1: Evolve `memo` → `note` (rename + flip head to `(note, canon)`)

**Files:**
- Rename: `src/memo/` → `src/note/` (all 8 files), `tests/memo/` → `tests/note/`
- Modify: `src/note/*`, `CMakeLists.txt:385-397,524-594`, `src/plugin/stock_plugins.cpp`, `tests/CMakeLists.txt:13`, `tests/note/CMakeLists.txt`, `tests/note/tst_memo_plugin.cpp`, `tests/note/tst_text_differ.cpp`

- [ ] **Step 1: Rename source + test dirs with git mv (preserve history)**

```bash
cd /home/clinton/dev/libkalburator
git mv src/memo src/note
git mv src/note/memoproperties.h        src/note/noteproperties.h
git mv src/note/memoproperties.cpp      src/note/noteproperties.cpp
git mv src/note/memodomaindefinition.h  src/note/notedomaindefinition.h
git mv src/note/memodomaindefinition.cpp src/note/notedomaindefinition.cpp
git mv src/note/memoplugin.h            src/note/noteplugin.h
git mv src/note/memoplugin.cpp          src/note/noteplugin.cpp
git mv tests/memo tests/note
git mv tests/note/tst_memo_plugin.cpp   tests/note/tst_note_plugin.cpp
# textdiffer.{h,cpp} and textmerger.{h,cpp} keep their names.
```

- [ ] **Step 2: Rewrite `src/note/noteproperties.h`**

```cpp
#pragma once

#include "propertycatalogue.h"

namespace Kalburator::Note {

/// Returns a PropertyCatalogue for note records (plaintext/raw-text carrier).
Kalburator::Shape::PropertyCatalogue makeNoteCatalogue();

} // namespace Kalburator::Note
```

- [ ] **Step 3: Rewrite `src/note/noteproperties.cpp`** (`id`→`uid` to match the canon envelope; other fields unchanged)

```cpp
#include "noteproperties.h"

using namespace Kalburator::Shape;

namespace Kalburator::Note {

PropertyCatalogue makeNoteCatalogue()
{
    PropertyCatalogue cat;

    cat.addProperty({ PropertyId{"uid"},          PropertyKind::String,     QStringLiteral("UID"),          false });
    cat.addProperty({ PropertyId{"body"},         PropertyKind::String,     QStringLiteral("Body") });
    cat.addProperty({ PropertyId{"categories"},   PropertyKind::StringList, QStringLiteral("Categories") });
    cat.addProperty({ PropertyId{"lastmodified"}, PropertyKind::DateTime,   QStringLiteral("Last Modified") });

    return cat;
}

} // namespace Kalburator::Note
```

- [ ] **Step 4: Rewrite `src/note/notedomaindefinition.h`**

```cpp
#pragma once

#include "domaindefinition.h"

namespace Kalburator::Note {

class NoteDomainDefinition : public Shape::DomainDefinition {
public:
    Shape::DomainId domain() const override;
    Shape::Shape canonicalShape() const override;
    Shape::PropertyCatalogue canonicalCatalogue() const override;
    std::unique_ptr<Shape::RecordDiffer> createCanonicalDiffer() const override;
    std::unique_ptr<Shape::RecordMerger> createCanonicalMerger() const override;
    int richnessRank(const Shape::Shape &) const override;

    /// Single-node spine: [(note, canon)]. PluginManager calls
    /// declareCanonical(note, canon) only. The (note, markdown) peer attaches
    /// to canon via the NoteStockShapes edges.
    QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>> canonicalSpine() const override;
};

} // namespace Kalburator::Note
```

- [ ] **Step 5: Rewrite `src/note/notedomaindefinition.cpp`**

```cpp
#include "notedomaindefinition.h"
#include "noteproperties.h"
#include "textdiffer.h"
#include "textmerger.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;

namespace Kalburator::Note {

Shape::DomainId NoteDomainDefinition::domain() const { return DomainId{"note"}; }

Shape::Shape NoteDomainDefinition::canonicalShape() const {
    return { DomainId{"note"}, EncodingId{"canon"} };
}

Shape::PropertyCatalogue NoteDomainDefinition::canonicalCatalogue() const {
    return makeNoteCatalogue();
}

std::unique_ptr<Shape::RecordDiffer> NoteDomainDefinition::createCanonicalDiffer() const {
    return std::make_unique<TextDiffer>();
}

std::unique_ptr<Shape::RecordMerger> NoteDomainDefinition::createCanonicalMerger() const {
    return std::make_unique<TextMerger>();
}

int NoteDomainDefinition::richnessRank(const Shape::Shape &s) const {
    if (s == canonicalShape())
        return 100;
    if (s.encoding == EncodingId{"markdown"})
        return 50;
    return 0;
}

QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>>
NoteDomainDefinition::canonicalSpine() const {
    return { { canonicalShape(), canonicalCatalogue() } };
}

} // namespace Kalburator::Note
```

- [ ] **Step 6: Rewrite `src/note/noteplugin.h`** (add `shapeContributions` — implemented in Task 3; declare it now so the class is final-shaped)

```cpp
#pragma once

#include "plugin.h"

namespace Kalburator::Note {

class NotePlugin : public Plugin {
public:
    QList<std::shared_ptr<Shape::DomainDefinition>> domainDefinitions() const override;
    QList<std::shared_ptr<Shape::ShapeContribution>> shapeContributions() const override;
};

} // namespace Kalburator::Note
```

- [ ] **Step 7: Rewrite `src/note/noteplugin.cpp`** (Task 1 returns an empty `shapeContributions`; Task 3 fills it)

```cpp
#include "noteplugin.h"
#include "notedomaindefinition.h"

namespace Kalburator::Note {

QList<std::shared_ptr<Shape::DomainDefinition>> NotePlugin::domainDefinitions() const {
    return { std::make_shared<NoteDomainDefinition>() };
}

QList<std::shared_ptr<Shape::ShapeContribution>> NotePlugin::shapeContributions() const {
    return {};  // Task 3 returns { std::make_shared<NoteStockShapes>() }
}

} // namespace Kalburator::Note
```

- [ ] **Step 8: Update `src/note/textdiffer.h`, `textdiffer.cpp`, `textmerger.h`, `textmerger.cpp`** — change only `namespace Kalburator::Memo {` → `namespace Kalburator::Note {` (and the matching closing comment). The diff/merge bodies (which key on JSON `"body"`/`"categories"`/`"lastModified"`) are unchanged — they ignore `uid`/`_canon`/`providerExtras`.

- [ ] **Step 9: Update `CMakeLists.txt`** — replace the memo block (lines 385-397) and references:

```cmake
set(KALBURATOR_NOTE_HEADERS
    src/note/noteproperties.h
    src/note/textdiffer.h
    src/note/textmerger.h
    src/note/notedomaindefinition.h
    src/note/noteplugin.h
)
set(KALBURATOR_NOTE_SOURCES
    src/note/noteproperties.cpp
    src/note/textdiffer.cpp
    src/note/textmerger.cpp
    src/note/notedomaindefinition.cpp
    src/note/noteplugin.cpp
)
```

Then: line 526 `${KALBURATOR_MEMO_HEADERS}` → `${KALBURATOR_NOTE_HEADERS}`; line 544 `${KALBURATOR_MEMO_SOURCES}` → `${KALBURATOR_NOTE_SOURCES}`; line 594 `$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/memo>` → `.../src/note>`.

- [ ] **Step 10: Update `src/plugin/stock_plugins.cpp`**

Line 6: `#include "memoplugin.h"` → `#include "noteplugin.h"`.
Line 35: `static Memo::MemoPlugin s_memo;` → `static Note::NotePlugin s_note;`.
Line 48: `{&s_memo, mkManifest(QStringLiteral("kalburator.memo"), {QStringLiteral("memo")})},` → `{&s_note, mkManifest(QStringLiteral("kalburator.note"), {QStringLiteral("note")})},`.

- [ ] **Step 11: Update `tests/note/tst_note_plugin.cpp`** — replace `MemoDomainDefinition`→`NoteDomainDefinition`, the include `memodomaindefinition.h`→`notedomaindefinition.h`, expected shape `{DomainId{"memo"}, EncodingId{"text"}}` → `{DomainId{"note"}, EncodingId{"canon"}}`, and `def.domain().toString()` expected `"memo"`→`"note"`. Rename the test class `TestMemoPlugin`→`TestNotePlugin` and `QTEST_GUILESS_MAIN(TestNotePlugin)`.

- [ ] **Step 12: Update `tests/note/tst_text_differ.cpp`** — change `const Shape kShape{ DomainId{"memo"}, EncodingId{"text"} };` → `{ DomainId{"note"}, EncodingId{"canon"} };`. The include path for `textdiffer.h` is unchanged. (The differ logic is byte/JSON based; the shape value is incidental.)

- [ ] **Step 13: Update `tests/note/CMakeLists.txt`** — rename the function `kalburator_add_memo_test`→`kalburator_add_note_test` and its two calls:

```cmake
kalburator_add_note_test(tst_note_plugin)
kalburator_add_note_test(tst_text_differ)
```

- [ ] **Step 14: Update `tests/CMakeLists.txt:13`** — `add_subdirectory(memo)` → `add_subdirectory(note)`.

- [ ] **Step 15: Configure + build + run note tests**

Run:
```bash
cmake -S /home/clinton/dev/libkalburator -B /home/clinton/dev/libkalburator/build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build /home/clinton/dev/libkalburator/build --target tst_note_plugin tst_text_differ
ctest --test-dir /home/clinton/dev/libkalburator/build -R "tst_note_plugin|tst_text_differ" -VV
```
Expected: both PASS.

- [ ] **Step 16: Full suite sanity (catch any stray memo references)**

Run: `ctest --test-dir /home/clinton/dev/libkalburator/build`
Expected: same green count as before the change (111/112; the lone `tst_providerlifecycle` flake, FINDINGS O9, is pre-existing). If a different test fails to *build*, it referenced the memo plugin — fix the reference.

- [ ] **Step 17: Commit**

```bash
git add -A
git commit -m "note: evolve the thin (memo,text) domain into (note,canon)

Renames src/memo -> src/note, domain id memo -> note, canonical head
(memo,text) -> (note,canon). Differ/merger lifted unchanged. Single-node
spine; markdown peer + edges land in the next task.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: `markdown ↔ canon` stages (verbatim frontmatter carry)

**Files:**
- Create: `src/note/markdowncanonstages.h`, `src/note/markdowncanonstages.cpp`
- Test: `tests/note/tst_markdown_canon_roundtrip.cpp`

**Contract:**
- A leading `---\n … \n---\n` block is the frontmatter; everything after (after a single optional blank line) is the body.
- `markdown → canon`: store the frontmatter **inner text verbatim** in `providerExtras["frontmatter"]` (a JSON string); parse the first `id:` line into `uid`; body → `"body"`. No frontmatter ⇒ no `providerExtras`, empty `uid`, whole input is body.
- `canon → markdown`: if `providerExtras["frontmatter"]` is present, emit `---\n<verbatim>\n---\n\n`; then the body, normalized to end in exactly one `\n`.
- Body is normalized to end in exactly one `\n` on both directions (documented normalization, matches WildPalms' existing format).

- [ ] **Step 1: Write the failing test `tests/note/tst_markdown_canon_roundtrip.cpp`**

```cpp
#include <QtTest>
#include "markdowncanonstages.h"
#include "canonenvelope.h"

using namespace Kalburator::Note;
using namespace Kalburator::Shape;

class TestMarkdownCanonRoundtrip : public QObject {
    Q_OBJECT
private slots:
    void roundTripsMarkdownWithFrontmatterByteForByte() {
        const QByteArray md =
            "---\n"
            "id: 42\n"
            "category: 3\n"
            "private: true\n"
            "---\n"
            "\n"
            "# Heading\n"
            "\n"
            "- item one\n"
            "- item two\n"
            "\n"
            "Some *emphasis* and `code`.\n";

        MarkdownToCanonStage promote;
        CanonToMarkdownStage demote;

        const QByteArray canon = promote.transform(md);
        const QByteArray back   = demote.transform(canon);
        QCOMPARE(back, md);   // byte-for-byte
    }

    void extractsUidFromIdLine() {
        const QByteArray md = "---\nid: 42\ncategory: 3\n---\n\nbody\n";
        MarkdownToCanonStage promote;
        const QJsonObject obj = CanonEnvelope::parse(promote.transform(md));
        QCOMPARE(CanonEnvelope::uid(obj), QStringLiteral("42"));
        QCOMPARE(obj.value(QStringLiteral("body")).toString(), QStringLiteral("body"));
    }

    void carriesFrontmatterVerbatimInProviderExtras() {
        const QByteArray md = "---\nid: 7\ncategory: 2\ncategoryName: Work\n---\n\nhi\n";
        MarkdownToCanonStage promote;
        const QJsonObject obj = CanonEnvelope::parse(promote.transform(md));
        const QString fm = obj.value(CanonEnvelope::providerExtrasKey())
                              .toObject().value(QStringLiteral("frontmatter")).toString();
        QCOMPARE(fm, QStringLiteral("id: 7\ncategory: 2\ncategoryName: Work"));
    }

    void handlesNoFrontmatter() {
        const QByteArray md = "just a plain note\nsecond line\n";
        MarkdownToCanonStage promote;
        CanonToMarkdownStage demote;
        const QByteArray canon = promote.transform(md);
        const QJsonObject obj = CanonEnvelope::parse(canon);
        QVERIFY(obj.value(CanonEnvelope::providerExtrasKey()).toObject()
                   .value(QStringLiteral("frontmatter")).isUndefined());
        QCOMPARE(CanonEnvelope::uid(obj), QString());
        QCOMPARE(demote.transform(canon), md);   // byte-for-byte
    }

    void normalisesBodyToSingleTrailingNewline() {
        const QByteArray noNl = "---\nid: 1\n---\n\nbody no newline";
        MarkdownToCanonStage promote;
        CanonToMarkdownStage demote;
        const QByteArray out = demote.transform(promote.transform(noNl));
        QVERIFY(out.endsWith("body no newline\n"));
        QVERIFY(!out.endsWith("\n\n"));
    }
};

QTEST_GUILESS_MAIN(TestMarkdownCanonRoundtrip)
#include "tst_markdown_canon_roundtrip.moc"
```

- [ ] **Step 2: Run it to verify it fails (no such header)**

Run: `cmake --build /home/clinton/dev/libkalburator/build --target tst_markdown_canon_roundtrip`
Expected: FAIL to compile — `markdowncanonstages.h` not found.

- [ ] **Step 3: Create `src/note/markdowncanonstages.h`**

```cpp
#pragma once

#include "transformationedge.h"

namespace Kalburator::Note {

/// (note, markdown) → (note, canon). Stores the body verbatim and the leading
/// YAML frontmatter block verbatim in providerExtras["frontmatter"]; peeks at
/// the first `id:` line to set the canon uid. Never interprets the body.
class MarkdownToCanonStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& sourceBytes) const override;
};

/// (note, canon) → (note, markdown). Re-emits providerExtras["frontmatter"]
/// verbatim (fenced) followed by the body (normalised to one trailing newline).
class CanonToMarkdownStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& sourceBytes) const override;
};

} // namespace Kalburator::Note
```

- [ ] **Step 4: Create `src/note/markdowncanonstages.cpp`**

```cpp
#include "markdowncanonstages.h"
#include "canonenvelope.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

using namespace Kalburator::Shape;

namespace Kalburator::Note {

namespace {

constexpr char kFrontmatter[] = "frontmatter";

// Split raw markdown text into (frontmatterInner, body).
// frontmatterInner is empty (and hasFm=false) when there is no leading fence.
struct Split { QString frontmatter; QString body; bool hasFm = false; };

Split splitMarkdown(const QString& text)
{
    Split s;
    if (!text.startsWith(QStringLiteral("---\n"))) {
        s.body = text;
        return s;
    }
    // Find the closing fence: a line "---" after the opener.
    const int close = text.indexOf(QStringLiteral("\n---"), 3);
    if (close < 0) {           // unterminated fence: treat whole thing as body
        s.body = text;
        return s;
    }
    s.hasFm = true;
    s.frontmatter = text.mid(4, close - 4);     // between "---\n" and "\n---"
    int after = close + 4;                      // past "\n---"
    if (after < text.size() && text.at(after) == QLatin1Char('\n'))
        ++after;                                // trailing newline of the close fence
    QString body = text.mid(after);
    while (body.startsWith(QLatin1Char('\n')))  // drop the blank separator line(s)
        body.remove(0, 1);
    s.body = body;
    return s;
}

QString uidFromFrontmatter(const QString& fm)
{
    const QStringList lines = fm.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0) continue;
        if (line.left(colon).trimmed().compare(QStringLiteral("id"), Qt::CaseInsensitive) == 0)
            return line.mid(colon + 1).trimmed();
    }
    return {};
}

QString normaliseBody(QString body)
{
    while (body.endsWith(QLatin1Char('\n')))
        body.chop(1);
    return body + QLatin1Char('\n');
}

} // namespace

QByteArray MarkdownToCanonStage::transform(const QByteArray& sourceBytes) const
{
    const Split s = splitMarkdown(QString::fromUtf8(sourceBytes));

    QJsonObject obj;
    CanonEnvelope::stampEnvelope(obj, QStringLiteral("note"),
                                 s.hasFm ? uidFromFrontmatter(s.frontmatter) : QString());
    obj.insert(QStringLiteral("body"), s.body);
    if (s.hasFm) {
        QJsonObject extras;
        extras.insert(QString::fromLatin1(kFrontmatter), s.frontmatter);
        obj.insert(CanonEnvelope::providerExtrasKey(), extras);
    }
    return CanonEnvelope::serialize(obj);
}

QByteArray CanonToMarkdownStage::transform(const QByteArray& sourceBytes) const
{
    const QJsonObject obj = CanonEnvelope::parse(sourceBytes);
    const QString body = obj.value(QStringLiteral("body")).toString();
    const QString fm = obj.value(CanonEnvelope::providerExtrasKey())
                          .toObject().value(QString::fromLatin1(kFrontmatter)).toString();

    QString out;
    if (!fm.isEmpty()) {
        out += QStringLiteral("---\n");
        out += fm;
        out += QStringLiteral("\n---\n\n");
    }
    out += normaliseBody(body);
    return out.toUtf8();
}

} // namespace Kalburator::Note
```

> **Note on byte-stability:** the round-trip is byte-stable because the frontmatter inner block is carried verbatim (no YAML re-serialization, so no key reordering). The only normalization is the single trailing `\n` on the body — documented per requirements §2.1. `CanonEnvelope::serialize` sorting canon keys is irrelevant to the markdown surface, since the markdown is reconstructed from `body` + verbatim `frontmatter`, not from the canon byte order.

- [ ] **Step 5: Add the new sources to `CMakeLists.txt`** — append to the lists from Task 1 Step 9:

```cmake
# in KALBURATOR_NOTE_HEADERS:
    src/note/markdowncanonstages.h
# in KALBURATOR_NOTE_SOURCES:
    src/note/markdowncanonstages.cpp
```

- [ ] **Step 6: Register the test in `tests/note/CMakeLists.txt`**

```cmake
kalburator_add_note_test(tst_markdown_canon_roundtrip)
```

- [ ] **Step 7: Build + run the test**

Run:
```bash
cmake -S /home/clinton/dev/libkalburator -B /home/clinton/dev/libkalburator/build
cmake --build /home/clinton/dev/libkalburator/build --target tst_markdown_canon_roundtrip
ctest --test-dir /home/clinton/dev/libkalburator/build -R tst_markdown_canon_roundtrip -VV
```
Expected: PASS (all 5 slots).

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "note: markdown<->canon stages with verbatim frontmatter carry

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: `NoteStockShapes` — `(note, markdown)` peer + `markdown↔canon` edges

**Files:**
- Create: `src/note/notestockshapes.h`, `src/note/notestockshapes.cpp`
- Modify: `src/note/noteplugin.cpp` (return the contribution), `CMakeLists.txt`
- Test: `tests/note/tst_note_shapes.cpp`

**Loss model:** the frontmatter rides in `providerExtras` (a side channel), not as first-class canon properties, so both `markdown↔canon` edges declare `affected[PropertyId{"frontmatter"}] = Reversible`. The body is lossless. This is the `Reversible` declaration WildPalms requires (§2.2).

- [ ] **Step 1: Write the failing test `tests/note/tst_note_shapes.cpp`**

```cpp
#include <QtTest>
#include "notestockshapes.h"
#include "notedomaindefinition.h"
#include "transformationregistry.h"
#include "canonenvelope.h"

using namespace Kalburator::Note;
using namespace Kalburator::Shape;

class TestNoteShapes : public QObject {
    Q_OBJECT

    TransformationRegistry buildRegistry() {
        TransformationRegistry reg;
        NoteDomainDefinition def;
        // Single-node spine: declareCanonical(note, canon).
        reg.declareCanonical(def.domain(), def.canonicalShape());
        NoteStockShapes stock;
        for (const auto& [shape, cat] : stock.peerShapes())
            reg.registerShape(shape, cat);
        for (const auto& edge : stock.edges())
            reg.registerEdge(edge);
        return reg;
    }

private slots:
    void compilesMarkdownToCanon() {
        auto reg = buildRegistry();
        const Shape md{ DomainId{"note"}, EncodingId{"markdown"} };
        const Shape canon{ DomainId{"note"}, EncodingId{"canon"} };
        QVERIFY(reg.compile(md, canon).has_value());
        QVERIFY(reg.compile(canon, md).has_value());
    }

    void frontmatterIsReversible() {
        auto reg = buildRegistry();
        const Shape md{ DomainId{"note"}, EncodingId{"markdown"} };
        const Shape canon{ DomainId{"note"}, EncodingId{"canon"} };
        const LossProfile loss = reg.compile(canon, md)->composedLoss();
        QCOMPARE(loss.affected.value(PropertyId{"frontmatter"}), LossKind::Reversible);
        QVERIFY(loss.droppedProperties().isEmpty());   // nothing dropped
    }

    void routesRecordThroughCanonAndBack() {
        auto reg = buildRegistry();
        const Shape md{ DomainId{"note"}, EncodingId{"markdown"} };
        const Shape canon{ DomainId{"note"}, EncodingId{"canon"} };
        const QByteArray input = "---\nid: 9\ncategory: 1\n---\n\nhello\n";
        const QByteArray inCanon = reg.compile(md, canon)->apply(input);
        const QByteArray back     = reg.compile(canon, md)->apply(inCanon);
        QCOMPARE(back, input);
    }
};

QTEST_GUILESS_MAIN(TestNoteShapes)
#include "tst_note_shapes.moc"
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build /home/clinton/dev/libkalburator/build --target tst_note_shapes`
Expected: FAIL — `notestockshapes.h` not found.

- [ ] **Step 3: Create `src/note/notestockshapes.h`**

```cpp
#pragma once
#include "shapecontribution.h"

namespace Kalburator::Note {

class NoteStockShapes : public Shape::ShapeContribution {
public:
    Shape::DomainId targetDomain() const override { return Shape::DomainId{QStringLiteral("note")}; }
    QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>> peerShapes() const override;
    QList<Shape::TransformationEdge> edges() const override;
};

} // namespace Kalburator::Note
```

- [ ] **Step 4: Create `src/note/notestockshapes.cpp`**

```cpp
#include "notestockshapes.h"
#include "noteproperties.h"
#include "markdowncanonstages.h"
#include "lossprofile.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::LossKind;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::TransformationEdge;
using Kalburator::Shape::IdentityStage;

namespace Kalburator::Note {

namespace {
// The frontmatter rides in providerExtras (side channel), not as a first-class
// canon property, so both markdown<->canon edges declare it Reversible.
LossProfile frontmatterReversible() {
    LossProfile p;
    p.affected.insert(PropertyId{QStringLiteral("frontmatter")}, LossKind::Reversible);
    return p;
}
} // namespace

QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>> NoteStockShapes::peerShapes() const
{
    const Shape::Shape markdown{ DomainId{QStringLiteral("note")}, EncodingId{QStringLiteral("markdown")} };
    return { { markdown, makeNoteCatalogue() } };
}

QList<Shape::TransformationEdge> NoteStockShapes::edges() const
{
    const Shape::Shape canon{ DomainId{QStringLiteral("note")}, EncodingId{QStringLiteral("canon")} };
    const Shape::Shape markdown{ DomainId{QStringLiteral("note")}, EncodingId{QStringLiteral("markdown")} };

    return {
        TransformationEdge{ canon,    canon,    LossProfile{},            std::make_shared<IdentityStage>() },
        TransformationEdge{ markdown, canon,    frontmatterReversible(),  std::make_shared<MarkdownToCanonStage>() },
        TransformationEdge{ canon,    markdown, frontmatterReversible(),  std::make_shared<CanonToMarkdownStage>() },
    };
}

} // namespace Kalburator::Note
```

- [ ] **Step 5: Fill in `src/note/noteplugin.cpp` `shapeContributions`**

Change the body to:
```cpp
#include "notestockshapes.h"
// ...
QList<std::shared_ptr<Shape::ShapeContribution>> NotePlugin::shapeContributions() const {
    return { std::make_shared<NoteStockShapes>() };
}
```

- [ ] **Step 6: Add sources to `CMakeLists.txt`** (append to the Task-1 lists)

```cmake
# KALBURATOR_NOTE_HEADERS:
    src/note/notestockshapes.h
# KALBURATOR_NOTE_SOURCES:
    src/note/notestockshapes.cpp
```

- [ ] **Step 7: Register the test in `tests/note/CMakeLists.txt`**

```cmake
kalburator_add_note_test(tst_note_shapes)
```

- [ ] **Step 8: Build + run; then run the full plugin smoke suite (the stock-plugin registration path now exercises note)**

Run:
```bash
cmake -S /home/clinton/dev/libkalburator -B /home/clinton/dev/libkalburator/build
cmake --build /home/clinton/dev/libkalburator/build --target tst_note_shapes tst_stock_plugins
ctest --test-dir /home/clinton/dev/libkalburator/build -R "tst_note_shapes|tst_stock_plugins" -VV
```
Expected: PASS. (`tst_stock_plugins` looks up the `note` domain by id; it passed for `memo` before — confirm it still finds a domain registered as `note`. If `tst_stock_plugins.cpp:31` still hardcodes `DomainId{"memo"}`, update it to `"note"`.)

- [ ] **Step 9: Commit**

```bash
git add -A
git commit -m "note: register (note,markdown) peer + markdown<->canon edges (Reversible frontmatter)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: `RawFilesBackend` — extract suffix/stem virtual seams (no behavior change)

**Files:**
- Modify: `src/universal/rawfilesbackend.h`, `src/universal/rawfilesbackend.cpp`
- Test: `tests/sinks/tst_rawfiles_backend.cpp` (existing — must stay green)

- [ ] **Step 1: In `rawfilesbackend.h`, change the two `static` helpers into protected virtuals and add a stem hook**

Replace the `private:` helper declarations:
```cpp
    static QString sanitize(const QString &id);
    static QString suffixFor(const QString &collectionId);
```
with:
```cpp
protected:
    /// File suffix for a collection. Default: "<encoding>.<domain>" derived from
    /// the shape-keyed collection id. Subclasses may override (e.g. "md").
    virtual QString suffixFor(const QString &collectionId) const;

    /// Unsanitised filename stem for a record. Default: record id, or
    /// displayName when id is empty. createRecord() sanitises the result.
    virtual QString recordStem(const QString &collectionId,
                               const Kalburator::Sync::BackendRecord &record) const;

private:
    static QString sanitize(const QString &id);
```

- [ ] **Step 2: In `rawfilesbackend.cpp`, update the definitions and call sites**

Change the static `suffixFor` definition (near line 221) to a const member:
```cpp
QString RawFilesBackend::suffixFor(const QString &collectionId) const
{
    const int plus = collectionId.indexOf(QLatin1Char('+'));
    if (plus < 0)
        return collectionId;
    const QString domain = collectionId.left(plus);
    const QString encoding = collectionId.mid(plus + 1);
    return encoding + QLatin1Char('.') + domain;
}
```

Add the default `recordStem` near it:
```cpp
QString RawFilesBackend::recordStem(const QString & /*collectionId*/,
                                    const BackendRecord &record) const
{
    return record.id.isEmpty() ? record.displayName : record.id;
}
```

In `createRecord` (lines 122-123), replace:
```cpp
    const QString base = record.id.isEmpty() ? record.displayName : record.id;
    const QString fileName = sanitize(base) + QLatin1Char('.') + suffixFor(collectionId);
```
with:
```cpp
    const QString fileName = sanitize(recordStem(collectionId, record))
                             + QLatin1Char('.') + suffixFor(collectionId);
```

The other `suffixFor` call sites (`clearCollection`, `loadRecords`) already use the member-call syntax `suffixFor(collectionId)` — they now resolve to the virtual; no edit needed.

- [ ] **Step 3: Build + run the existing sink tests (regression gate)**

Run:
```bash
cmake --build /home/clinton/dev/libkalburator/build --target tst_rawfiles_backend
ctest --test-dir /home/clinton/dev/libkalburator/build -R tst_rawfiles_backend -VV
```
Expected: PASS, unchanged — defaults reproduce the prior behavior exactly.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "universal: extract suffixFor/recordStem virtual seams on RawFilesBackend (no behavior change)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: `MarkdownFilesBackend` sink

**Files:**
- Create: `src/universal/markdownfilesbackend.h`, `src/universal/markdownfilesbackend.cpp`
- Modify: `CMakeLists.txt` (UNIVERSAL header/source lists, lines ~403-410)
- Test: `tests/sinks/tst_markdownfiles_backend.cpp`, `tests/sinks/CMakeLists.txt`

**Behavior:** suffix `md`; filename = first non-empty body line (after any frontmatter), sanitized à la WildPalms, with a `note_<recordId>.md` fallback. Bytes written are the `(note, markdown)` peer encoding (already human-readable). Re-reading re-parses those bytes; the engine's `markdown→canon` promote recovers identity.

- [ ] **Step 1: Write the failing test `tests/sinks/tst_markdownfiles_backend.cpp`**

```cpp
#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include "markdownfilesbackend.h"
#include "collectioninfo.h"
#include "backendrecord.h"
#include "shape.h"

using namespace Kalburator::Sinks;
using namespace Kalburator::Sync;
using namespace Kalburator::Shape;

class TestMarkdownFilesBackend : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

    static BackendRecord rec(const QString &id, const QByteArray &data) {
        BackendRecord r; r.id = id; r.data = data; return r;
    }

private slots:
    void writesTitleNamedMarkdownFile() {
        MarkdownFilesBackend b(m_dir.path());
        CollectionInfo ci; ci.id = QStringLiteral("note+markdown"); ci.name = QStringLiteral("Notes");
        b.createCollection(ci, Shape{ DomainId{"note"}, EncodingId{"markdown"} });

        const QByteArray md = "---\nid: 5\n---\n\nShopping list\nmilk\neggs\n";
        const QString path = b.createRecord(ci.id, rec(QStringLiteral("5"), md));

        QVERIFY(path.endsWith(QStringLiteral("Shopping_list.md")));
        QVERIFY(QFile::exists(path));
    }

    void fallsBackToNoteIdForEmptyBody() {
        MarkdownFilesBackend b(m_dir.path());
        CollectionInfo ci; ci.id = QStringLiteral("note+markdown"); ci.name = QStringLiteral("Notes");
        b.createCollection(ci, Shape{ DomainId{"note"}, EncodingId{"markdown"} });

        const QByteArray md = "---\nid: 8\n---\n\n";
        const QString path = b.createRecord(ci.id, rec(QStringLiteral("8"), md));
        QVERIFY(path.endsWith(QStringLiteral("note_8.md")));
    }

    void roundTripsRecordViaDisk() {
        MarkdownFilesBackend b(m_dir.path());
        CollectionInfo ci; ci.id = QStringLiteral("note+markdown"); ci.name = QStringLiteral("Notes");
        b.createCollection(ci, Shape{ DomainId{"note"}, EncodingId{"markdown"} });

        const QByteArray md = "---\nid: 5\n---\n\nShopping list\n";
        b.createRecord(ci.id, rec(QStringLiteral("5"), md));

        const auto loaded = b.loadRecords(ci.id);
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded.first().data, md);
    }
};

QTEST_GUILESS_MAIN(TestMarkdownFilesBackend)
#include "tst_markdownfiles_backend.moc"
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build /home/clinton/dev/libkalburator/build --target tst_markdownfiles_backend`
Expected: FAIL — `markdownfilesbackend.h` not found.

- [ ] **Step 3: Create `src/universal/markdownfilesbackend.h`**

```cpp
#pragma once

#include "rawfilesbackend.h"

namespace Kalburator::Sinks {

/// RawFilesBackend specialised for human-readable Markdown notes:
///   - file suffix is ".md" (not "<encoding>.<domain>")
///   - filename stem is the first non-empty body line (after any YAML
///     frontmatter), sanitised, with a "note_<recordId>" fallback.
/// The bytes written are the (note, markdown) peer encoding verbatim.
class MarkdownFilesBackend : public RawFilesBackend {
    Q_OBJECT
public:
    explicit MarkdownFilesBackend(QString rootPath, QObject *parent = nullptr)
        : RawFilesBackend(std::move(rootPath), parent) {}

    QString backendType() const override { return QStringLiteral("markdown-files"); }
    QString displayName() const override { return QStringLiteral("Markdown Files Backend"); }

protected:
    QString suffixFor(const QString &collectionId) const override;
    QString recordStem(const QString &collectionId,
                       const Kalburator::Sync::BackendRecord &record) const override;
};

} // namespace Kalburator::Sinks
```

- [ ] **Step 4: Create `src/universal/markdownfilesbackend.cpp`**

```cpp
#include "markdownfilesbackend.h"
#include "backendrecord.h"

#include <QRegularExpression>
#include <QString>
#include <QStringList>

using Kalburator::Sync::BackendRecord;

namespace Kalburator::Sinks {

namespace {

// Sanitise a filename stem (mirrors WildPalms' sanitiseFilenameStem).
QString sanitiseStem(const QString &input)
{
    static const QRegularExpression invalidChars(QStringLiteral("[^a-zA-Z0-9_\\-. ]"));
    static const QRegularExpression multiSpace(QStringLiteral("\\s+"));
    QString stem = input;
    stem.replace(invalidChars, QStringLiteral("_"));
    stem.replace(multiSpace, QStringLiteral("_"));
    while (stem.startsWith(QLatin1Char('_'))) stem.remove(0, 1);
    while (stem.endsWith(QLatin1Char('_')))   stem.chop(1);
    return stem.trimmed();
}

// First non-empty body line, skipping a leading ---\n...\n--- frontmatter block.
QString firstBodyLine(const QByteArray &data)
{
    QString text = QString::fromUtf8(data);
    if (text.startsWith(QStringLiteral("---\n"))) {
        const int close = text.indexOf(QStringLiteral("\n---"), 3);
        if (close >= 0) {
            int after = close + 4;
            if (after < text.size() && text.at(after) == QLatin1Char('\n')) ++after;
            text = text.mid(after);
        }
    }
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString t = line.trimmed();
        if (!t.isEmpty()) return t;
    }
    return {};
}

} // namespace

QString MarkdownFilesBackend::suffixFor(const QString & /*collectionId*/) const
{
    return QStringLiteral("md");
}

QString MarkdownFilesBackend::recordStem(const QString & /*collectionId*/,
                                         const BackendRecord &record) const
{
    const QString line = firstBodyLine(record.data);
    const QString stem = sanitiseStem(line.left(50));
    if (!stem.isEmpty())
        return stem;
    const QString id = record.id.isEmpty() ? QStringLiteral("0") : record.id;
    return QStringLiteral("note_") + id;
}

} // namespace Kalburator::Sinks
```

> **Naming note:** `RawFilesBackend::createRecord` calls `sanitize(recordStem(...))`. `sanitize` only strips filesystem-illegal chars; our `sanitiseStem` already produces a clean stem, so the outer `sanitize` is idempotent on it. The `.md` suffix means `loadRecords`/`clearCollection` match files ending in `.md` (the inherited `_shapes.json` manifest ends in `.json`, so it is not matched).

- [ ] **Step 5: Add sources to `CMakeLists.txt`** — in the universal header/source lists (lines ~403-410):

```cmake
# headers:
    src/universal/markdownfilesbackend.h
# sources:
    src/universal/markdownfilesbackend.cpp
```

- [ ] **Step 6: Register the test in `tests/sinks/CMakeLists.txt`** — follow the existing pattern there (e.g. add `kalburator_add_sink_test(tst_markdownfiles_backend)` or the matching helper used by `tst_rawfiles_backend`; copy the exact function name from that file).

- [ ] **Step 7: Build + run**

Run:
```bash
cmake -S /home/clinton/dev/libkalburator -B /home/clinton/dev/libkalburator/build
cmake --build /home/clinton/dev/libkalburator/build --target tst_markdownfiles_backend
ctest --test-dir /home/clinton/dev/libkalburator/build -R tst_markdownfiles_backend -VV
```
Expected: PASS (all 3 slots).

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "universal: MarkdownFilesBackend sink (.md files, title-named, frontmatter round-trip)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: Full suite + docs

**Files:**
- Modify: `docs/2026-05-25-note-domain-design.md` (Status line), `docs/campaign/STATUS.md` (Next action #5)

- [ ] **Step 1: Run the full suite**

Run: `ctest --test-dir /home/clinton/dev/libkalburator/build`
Expected: prior green count + the new tests (`tst_note_plugin`, `tst_text_differ`, `tst_markdown_canon_roundtrip`, `tst_note_shapes`, `tst_markdownfiles_backend`) all green; `tst_providerlifecycle` remains the only (pre-existing, FINDINGS O9) failure. Record the exact count in the commit.

- [ ] **Step 2: Update `docs/2026-05-25-note-domain-design.md`** — change the Status line to: `**Status:** Implemented (<commit>); WildPalms adoption is downstream.`

- [ ] **Step 3: Update `docs/campaign/STATUS.md`** — change Next-action item #5 from "Implementation plan pending." to a one-line "Implemented <date> per `docs/2026-05-25-note-domain-plan.md`; WildPalms adoption downstream."

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "docs: note domain implemented — design + campaign STATUS updated

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Self-review notes (addressed)

- **Spec coverage:** §2.1 canon (Task 1) · §2.2 markdown peer + Reversible frontmatter (Tasks 2-3) · §2.3 sink (Tasks 4-5) · §2.4 evolve memo (Task 1) · §3 parity differ/merger/stockshapes/spine (Tasks 1, 3) · §4 acceptance (tests across Tasks 2,3,5) · §5 tests (Tasks 2,3,5) · §6 non-goals (no AST, no `(note,text)` peer — honored). The WildPalms `palm` edge + `palm→canon→palm` device test are **WildPalms-side** (requirements §5) and out of this plan's scope.
- **Placeholder scan:** none — every code step shows complete code; the only deferred lookups are explicitly "copy the exact helper name from this neighboring file" (test CMake helpers), which vary by tree and must be read at execution time (invariant P1).
- **Type consistency:** `MarkdownToCanonStage`/`CanonToMarkdownStage`, `NoteStockShapes`, `NoteDomainDefinition`, `makeNoteCatalogue`, `suffixFor`/`recordStem`, `providerExtras["frontmatter"]`, `PropertyId{"frontmatter"}` used consistently across tasks.
- **Open execution-time check:** Task 3 Step 8 and Task 5 Step 6 require reading the neighboring CMake/test files for the exact helper-function names and the `tst_stock_plugins.cpp` domain-id assertion — these are tree facts to confirm, not guesses to bake in.
