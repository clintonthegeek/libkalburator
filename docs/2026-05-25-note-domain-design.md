# Design — a `note` plaintext-carrier domain, `(note, markdown)` peer, and Markdown-file sink

**Date:** 2026-05-25
**Status:** Design approved (human, 2026-05-25); implementation plan pending.
**Driver:** WildPalms requirements doc `~/dev/WildPalms/docs/2026-05-25-document-domain-requirements-for-libkalburator.md`.
**Relates to:** campaign FINDINGS O4 (WildPalms port is downstream; its invariants are upstream).
This is **additive, post-convergence** work — it adds a domain; it does not touch the converged
shape-graph mechanism (invariant 1).

---

## 1. The question this answers

WildPalms asks for a `document` domain whose canonical round-trips Markdown losslessly, a
`(document, markdown)` peer with round-trippable side metadata surfaced as YAML frontmatter, and a
Markdown-file sink. The deep question is *how to model something as fluid as a document* in the
property-catalogue / four-kind-loss paradigm — and whether that means reinventing pandoc.

**It does not.** There are three strata one could call a "document canonical":

1. **Markdown-string canon ("text equals canon").** The canonical *body is the source string*,
   stored verbatim, plus structured side-metadata in `providerExtras`. No AST, no body parsing.
   Markdown structure survives because the bytes survive. Round-trip is byte-stable trivially.
2. **Shallow block/inline AST.** Canon is a JSON document tree; Markdown is parsed in and rendered
   back out. This reinvents a slice of pandoc; byte-stability becomes hard, normalizing work.
   WildPalms explicitly says they don't need it (requirements §1, §2.1, §6).
3. **Full pandoc-grade AST + format conversion.** Explicit non-goal.

The shape graph's canon is a **JSON envelope, not a forced property decomposition** — a canon whose
`body` is a string is a perfectly legal canon. So "documents are too fluid for properties" only bites
at stratum 2. We build **stratum 1**.

### 1.1 Semantic boundary (locked decision, human 2026-05-25)

This domain is an **honest opaque text carrier**, named `note` — *not* `document`. Its body is raw
text that *may contain* Markdown markup, stored verbatim and **never parsed or interpreted**.
Markdown's structure survives the round-trip precisely because we make no claim to understand it: we
do not transform it, so we cannot damage it. Markdown is a *surface/serialization* whose only real
transformation is `frontmatter ⟷ providerExtras`.

The word **`document` is reserved** for a *future, separate* domain that will do real document
*conversion* — a block/inline AST, format-to-format transforms (stratum 2/3, the pandoc-shaped
problem). Structural loss (mangled nested lists, dropped tables) is *that* domain's concern. At the
`note` layer, the only loss that exists is text/identity loss (a Palm category slot, a private flag,
a record id) — never "did we mangle the markup."

---

## 2. Architecture

### 2.1 Shape graph

```
            (note, palm)              ← WildPalms declares & owns this edge (their DocumentDomainExtension)
                 ↕   [WP edge: palm→markdown lossless; markdown→palm Simplified]
         (note, markdown)             ← peer we register — the on-disk YAML-frontmatter + body dialect
                 ↕   [our edge: frontmatter↔providerExtras, body passthrough — Reversible]
           (note, canon)              ← canonical head, v1; spine = [(note, canon)]
```

- **Canonical** `(note, canon)`, JSON `CanonEnvelope` (`src/shape/canonenvelope.h`). First-class
  properties (memo-parity, deliberately minimal):
  - `uid` (String, required) — identity.
  - `body` (String) — the verbatim Markdown / raw text; **never parsed**.
  - `lastModified` (DateTime) — sync axis.
  - `categories` (StringList) — cross-PIM axis, present in today's memo canon.

  Everything Palm-flavored (record id beyond uid, category *slot* integer, private flag, any other
  frontmatter key) rides in `providerExtras` — never a conflict axis (locked decision 8).
- **Spine** = single node `[(note, canon)]` at v1. Satisfies "versioned, append-only" (invariant 2)
  with room for a v2 later; `DocumentDomainDefinition::canonicalSpine()` returns one element.
- **Peer** `(note, markdown)` — the YAML-frontmatter + body dialect (the on-disk and wire surface).
- **Differ / merger** — lift `TextDiffer` / `TextMerger` from `src/memo/` unchanged. Line-based diff
  is the correct tool for prose. `providerExtras` is ignored as a conflict axis, same as the other
  canons (CanonJson differ/merger behavior).

### 2.2 The `markdown ↔ canon` edge — the only transform we own

The body is **passed through untouched** — this is what makes `markdown → canon → markdown`
byte-stable and what honors "we assert no document semantics." Only the frontmatter moves:

- **`markdown → canon` (promote, `Reversible`):** parse the leading `---`-fenced YAML block. One
  **reserved key, `id`,** promotes to canon `uid` (matches WildPalms' existing dialect and gives the
  engine identity). *All other frontmatter keys* land in `providerExtras["frontmatter"]` as an
  ordered object. The body (everything after the closing `---`) → canon `body`. If there is no
  frontmatter, the whole input is the body and `uid` is left for the engine to assign.
- **`canon → markdown` (demote, `Reversible`):** re-emit `providerExtras["frontmatter"]` (plus `id:`
  derived from `uid`) as a canonical YAML block — stable key order, omit-default rules modeled on
  WildPalms' `encode()` (`memomarkdown.cpp`) — then the body. Body ends in exactly one `\n`.

This is the `providerExtras` analogue WildPalms asked for (requirements §2.2), surfaced as YAML.
WildPalms stashes their record-id / category-slot / private flag into frontmatter on *their*
`palm → markdown` edge; our edge carries them blind through canon and back. Byte-stability is
"modulo documented YAML normalization" — exactly the latitude their hard requirement allows
(requirements §2.1).

**Loss profile:** both directions `Reversible` for the frontmatter (round-trips byte-for-byte via
`providerExtras`); body is lossless. The lossy step (`markdown → palm` flattening Markdown structure
to plain text — `Simplified`) lives on *WildPalms'* edge, not ours.

### 2.3 Fate of `memo` — evolve, don't fork (locked decision)

The memo domain is small and cleanly isolated: `src/memo/` (8 files), one registration in
`src/plugin/stock_plugins.cpp` (`kalburator.memo`), and `tests/memo/`. (Most tree-wide "memo" grep
hits are the *word* used generically in comments / throwaway domain-id strings in blob test fakes —
not coupling to the memo plugin.)

**Plan:** rename `src/memo/` → `src/note/`; domain id `memo` → `note`; canonical `(memo, text)` →
`(note, canon)`; keep `TextDiffer` / `TextMerger`; **add** the markdown peer + `markdown ↔ canon`
edge + `providerExtras` handling + a `NoteStockShapes` contribution + the single-node spine. Retire
`(memo, text)`. Update `stock_plugins.cpp`, `CMakeLists.txt`, and move `tests/memo/` → `tests/note/`.
WildPalms migrates off `(memo, text)` regardless (requirements §6), so there is no compat burden —
one domain, not two.

### 2.4 Markdown-file sink

A thin subclass **`MarkdownFilesBackend : RawFilesBackend`** (in `src/universal/`), overriding only
the two behaviors that differ from the generic raw sink:

1. **Suffix** → `.md` (instead of the generic `.<encoding>.<domain>`).
2. **Filename** → first non-empty body line, sanitized, with a stable `note_<uid>.md` fallback for
   empty/degenerate bodies (lifting WildPalms' `filenameFor` / `sanitiseFilenameStem`).

It declares `(note, markdown)` at `createCollection(info, shape)` time, so the engine routes
`palm → markdown → canon` *into* it (rather than mirroring the source shape). The bytes it writes are
the `(note, markdown)` peer encoding — already the human-readable frontmatter + body. Re-reading a
file parses those bytes straight back into a `(note, markdown)` record, identity intact via the §2.2
round-trip. Manifest persistence, CRUD, hashing, and read-back are all inherited unchanged.

> **Seam check (for the plan):** confirm `RawFilesBackend` exposes a clean override point for suffix
> and filename derivation (e.g. `virtual QString suffixFor(...)` / a filename hook). If `suffixFor`
> is non-virtual and naming is inlined in `createRecord`, the minimal change is to extract a small
> protected `virtual` naming hook on `RawFilesBackend` and override it — no behavior change to the
> base. Prefer that over duplicating the CRUD body.

---

## 3. Parity with the other canons (requirements §3)

- **Four-kind `LossProfile`** (`Dropped` / `Simplified` / `Reversible` / `Degraded`) on the edges,
  including `losslessValues` where relevant. Our `markdown ↔ canon` edges are `Reversible` (frontmatter)
  / lossless (body); WildPalms' `palm → markdown` is `Simplified`.
- **Canonical differ + merger** — `TextDiffer` / `TextMerger`, the line-based pair already in `memo`.
- **Registration** through the same `ShapeRegistries` bundle / stock-shapes path: a `NotePlugin`
  returns a `NoteDomainDefinition` (domain id `note`) from `domainDefinitions()`
  and a `NoteStockShapes` from `shapeContributions()`. The spine is declared via `declareCanonical` +
  the single-node `canonicalSpine()`.
- **Stock-shapes contribution** registers the canonical, the `(note, markdown)` peer, and the
  `markdown ↔ canon` edges — the analogue of `TodoStockShapes` / `CalendarStockShapes` that memo
  lacks today (which is why nothing routes).

No breaking changes to the converged API; everything here is additive (requirements §3, invariant 1).

---

## 4. Acceptance (mirrors requirements §7)

When, building against this branch, a consumer can:

1. Register `(note, palm)` and a `(note, palm) ↔ (note, markdown)` edge, and `compile(palm, canon)`
   succeeds (the `markdown ↔ canon` edges exist and the spine is reachable).
2. Route a record `palm → canon → palm` and recover **body and identity** (record id + category
   slot) intact, the canonical holding the Markdown text verbatim.
3. Run a sync whose on-disk artifacts are **readable `.md` files** — one per record, title-named,
   identity in YAML frontmatter — produced by `MarkdownFilesBackend`, re-readable into identical
   records.
4. Read an honest composed `LossProfile` (markdown→canon lossless/Reversible; the `Simplified`
   markdown→palm loss is the consumer's edge).

## 5. Testing

- `markdown → canon → markdown` byte-stability over representative Markdown (headings, lists,
  emphasis, inline + block code, blockquotes) — the lossless-peer hard requirement.
- Side-metadata round-trip: arbitrary namespaced frontmatter survives `markdown → canon → markdown`
  byte-for-byte; declared `Reversible`.
- Composed `LossProfile` honesty across the path.
- Sink: one `.md` per record, title-named with `note_<uid>.md` fallback, re-readable into an
  identical record (frontmatter + body).
- `compile((note,*) → (note,canon))` reachability + spine append-only guard (mirror
  `tst_canonical_spine`).

## 6. Non-goals

- No document AST / format conversion (that is the future `document` domain).
- No rich features beyond Markdown-as-opaque-text (tables/footnotes/HTML are not modeled; they ride
  as body bytes like any other text).
- No `(note, text)` plain-text peer (not needed; `markdown` is the only peer we register).
- No new conflict axis from `providerExtras`.

## 7. Source lifted from WildPalms (requirements §4)

Reference / adopt: `src/plugins/memo/memomarkdown.{h,cpp}` (encode/decode + `filenameFor` /
`sanitiseFilenameStem`), `memoview.cpp` (the on-disk frontmatter dialect we must stay compatible
with), `memoblobbackend.{h,cpp}` (per-record backend behavior). Once shipped, WildPalms deletes its
redundant encode/decode/sink code and depends on ours.

## 8. Landed APIs this design builds against (verify before coding, invariant P1)

Re-confirm against tree at plan-authoring time (signatures from `docs/2026-05-24-plan-3-canon-encodings.md`):
`Shape`, `PropertyId`, `PropertyKind` (no `Json` *type* — JSON bytes via `QJsonDocument`),
`PropertyDescriptor`, `PropertyCatalogue`, `CanonicalRecord`, `TransformationStage`/`IdentityStage`,
`TransformationEdge`, `LossKind`/`LossProfile` (+ `losslessValues`), `Pipeline`, `RecordDiffer`,
`RecordMerger`, `DomainDefinition` (incl. `canonicalSpine()`), `ShapeContribution`,
`TransformationRegistry` (`declareCanonical` / `appendCanonicalVersion` / `registerEdge` / `compile`),
`Plugin`, and the `CanonEnvelope` / `providerExtrasKey()` helpers in `src/shape/canonenvelope.{h,cpp}`.
Model new code on `src/todo/` (closest rich template) and the existing `src/memo/` (for the
differ/merger to lift).
