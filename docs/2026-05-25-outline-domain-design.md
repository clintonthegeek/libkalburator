# Design: the `outline` domain (hierarchical lists) for libkalburator

**Date:** 2026-05-25
**Status:** design — approved in brainstorm, pending spec review → implementation plan.
**Scope:** this spec covers **only** the new libkalburator `outline` domain and its
first two peers (`org`, `opml`). It is self-contained and independently valuable
(org/markdown/OPML users benefit with no Palm involved). The ShadowPlan / WildPalms
integration is a **separate follow-on spec** (see §10 Out of Scope) and depends on this
landing first.

---

## 1. Motivation

WildPalms is gaining its first Palm app that goes *far* beyond the stock PIM apps:
**ShadowPlan**, an outliner whose lists are hierarchical trees of nodes that are
simultaneously headings, checkboxes/tasks, and notes. The stock `todo` domain (flat
VTODO) and `note` domain (single text body) each capture a *facet* of this but throw
away the rest — most fatally, the tree.

Rather than overfit a Palm-specific shape, we recognize a well-established data model:
the **outliner**. A tree of titled nodes, each optionally bearing a task facet and a
note facet, is the shared shape of Org-mode, OPML, OmniOutliner, Workflowy/Dynalist,
TaskPaper, GitHub-flavored-markdown task lists — and ShadowPlan. Modeling that as a
first-class libkalburator domain gives every one of those a path to canon, and turns
the existing flat-todo and single-note domains into *projections* (degenerate outlines)
rather than rivals.

This follows the convergence campaign's deepest rule (`docs/campaign/INVARIANTS.md` §1):
**extend the shape graph, never fork a new transformation mechanism.** The `outline`
domain is a new domain *on* the shape graph, built exactly like `note` (2026-05-25).

---

## 2. The canonical model

### 2.1 Record grain: one tree per record

**A record is one whole outline tree (a document / forest), not one node.** This is the
load-bearing decision. The Palm wire format (ShadowPlan `ShadP-*`) uses a stack-based
`level` byte whose meaning is *relative to the preceding record*; independently
creating, deleting, or reordering individual nodes at the engine's per-record baseline
layer would silently corrupt the tree. Making the whole tree the atomic transport unit
means a peer's encode/decode stage always sees the complete ordered node set and can
re-emit correct structure.

This grain also matches every peer's natural unit: one OPML document, one `.org` file,
one ShadowPlan list = one tree = one record.

### 2.2 Transport vs. merge are separate layers

Whole-tree *transport* does **not** imply whole-document conflict resolution. By the time
reconciliation runs, the record is **canon JSON** — an `id`-keyed, order-independent node
tree — so the domain's canonical differ/merger performs a **structural, node-by-node
3-way merge** (base vs. peer-A vs. peer-B): auto-merge non-overlapping node edits, surface
only genuinely conflicting nodes. The level encoding is irrelevant at this layer.

Precedent: the `note` domain has record-grain = whole file yet reconciles *inside* a
record via `TextDiffer`/`TextMerger`. `outline` mirrors this with `OutlineDiffer` /
`OutlineMerger` that recurse the node tree. The engine already delegates intra-record
reconciliation to the domain, so **no engine changes are required.**

> Sequencing note: the first implementation MAY ship a coarse differ (pick-a-side per
> tree) to land the round-trip, with the structural node-level merge as a fast-follow.
> The grain choice does not foreclose node-level merge.

### 2.3 The canonical `OutlineNode`

Canon is a JSON envelope (`CanonEnvelope`: `_canon{domain,v}`, `uid`, `providerExtras`,
plus mapped properties). A record's canon body is a **document object** containing a
forest of recursive nodes:

```jsonc
// canon record body (one tree)
{
  "uid":   "…",                 // stable record identity
  "title": "Groceries",         // optional document/list title
  "created": "…", "lastModified": "…",
  "attributes": { },            // document-level peer-specific bag
  "children": [ OutlineNode … ] // the forest
}

// OutlineNode (recursive)
{
  "id":     "…",                // stable node identity (within the tree)
  "text":   "Milk",             // Tier 1 — the row title (required)
  "note":   "2% organic",       // Tier 2 — body text, distinct from title
  "done":   false,              // Tier 1 — boolean checked/complete
  "status": "TODO",             // Tier 2 — workflow state (see statusVocabulary)
  "priority": 1,                // Tier 2 — integer; scale described at doc level
  "progress": 0,                // Tier 2 — percent 0..100
  "start":     { },             // Tier 2 — Json date (tz/floating/precision), ≈ SCHEDULED
  "due":       { },             // Tier 2 — Json date,                          ≈ DEADLINE
  "completed": "…",             // Tier 2 — DateTime,                           ≈ CLOSED
  "created":   "…",             // Tier 2 — DateTime
  "tags":      [ "errand" ],    // Tier 2 — StringList
  "attributes": { },            // Tier 3 — node-level peer-specific bag (providerExtras-style)
  "order":     0,               // Tier 1 — explicit sibling order (also implied by array index)
  "children":  [ OutlineNode … ]// Tier 1 — the tree
}
```

The three fidelity tiers (validated against primary sources — OPML 2.0, Org syntax,
TaskPaper, GFM; see §4):

- **Tier 1 — core, universal, lossless through even thin peers:** `id`, `text`,
  `children`, `order`, `done`.
- **Tier 2 — rich, first-class in canon, honest loss on thin peers:** `note`, `status`
  + `statusVocabulary` (doc-level), `priority` + scale (doc-level), `progress`, `start`,
  `due`, `completed`, `created`, `tags`.
- **Tier 3 — peer-specific, carried in `attributes` (Reversible loss):** OPML `type`/
  `xmlUrl`/`isComment`/`expansionState`; Org `:PROPERTIES:` beyond mapped keys; ShadowPlan
  `color`/`autonumber`/`bold`/`columnMask`/`viewRef`/`fileLink`/`todoLinkUid`.

`statusVocabulary` and the `priority` scale are **document-level** (in the top-level
`attributes`), because Org's workflow keywords and priority range are file/buffer-scoped,
not per-node.

### 2.4 PropertyCatalogue

`makeOutlineCanonCatalogue()` exposes record-level properties to the engine (baseline
SQLite columns, generic diff/merge fallback):

| PropertyId | Kind | Notes |
|---|---|---|
| `uid` | String | required |
| `title` | String | document title |
| `created` | DateTime | |
| `lastModified` | DateTime | |
| `attributes` | Json | document-level Tier-3 bag (incl. `statusVocabulary`, `priorityScale`) |
| `children` | Json | the forest of `OutlineNode`s — the structural payload |

Per-node fields live *inside* the `children` Json (like `todo`'s `checklistItems`/`relatedTo`
Json blobs). Tree-awareness — including node-level loss-profile reporting — is the
differ/merger's job, not the flat catalogue's. This keeps the catalogue honest about what
the *engine* sees while the §2.3 schema documents the canonical structure in full.

---

## 3. Domain registration (mirrors `note`)

Three classes under `src/outline/`, wired by `OutlinePlugin`, registered by
`PluginManager` exactly as `note`/`todo` are:

### 3.1 `OutlineDomainDefinition : Shape::DomainDefinition`
- `domain()` → `DomainId{"outline"}`
- `canonicalShape()` → `{ outline, canon }`
- `canonicalCatalogue()` → `makeOutlineCanonCatalogue()`
- `createCanonicalDiffer()` → `OutlineDiffer`; `createCanonicalMerger()` → `OutlineMerger`
- `canonicalSpine()` → single-node `[{ canon, catalogue }]` (like `note`; no legacy root)
- `richnessRank(s)`:
  - `canon` → 100
  - `org` → 70  (near-lossless: carries all Tier 1–2 + most Tier 3 via `:PROPERTIES:`)
  - `opml` → 40 (structure + extras; drops Tier-2 task semantics)

### 3.2 `OutlineStockShapes : Shape::ShapeContribution`
- `targetDomain()` → `DomainId{"outline"}`
- `peerShapes()` → `{ (outline, org), makeOrgCatalogue() }, { (outline, opml), makeOpmlCatalogue() }`
- `edges()`:
  ```
  canon → canon   IdentityStage           LossProfile{}
  org   → canon   OrgToCanonStage         orgToCanonLoss()
  canon → org     CanonToOrgStage         canonToOrgLoss()
  opml  → canon   OpmlToCanonStage        opmlToCanonLoss()
  canon → opml    CanonToOpmlStage        canonToOpmlLoss()
  ```
  Peers reach each other by routing through canon (`org → canon → opml`), per the
  shape-graph N-hop model — no direct peer↔peer edges.

### 3.3 `OutlinePlugin : Plugin`
- `domainDefinitions()` → `{ OutlineDomainDefinition }`
- `shapeContributions()` → `{ OutlineStockShapes }`

---

## 4. The two peers

### 4.1 Org-mode (`(outline, org)`) — the rich/near-lossless reference

Org is the high-water mark and validates the full canon. Mapping:

| Canon | Org construct |
|---|---|
| node = headline (`*` depth = tree level) | `** TODO [#A] Title :tag:` + section |
| `text` | headline title |
| `note` | headline section body |
| `done` / `status` | TODO keyword; `statusVocabulary` ← `#+TODO:` / `org-todo-keywords` |
| `priority` | `[#A]` cookie; `priorityScale` ← configured range |
| `progress` | statistics cookie `[%]`/`[/]` on parent |
| `start` / `due` / `completed` | `SCHEDULED:` / `DEADLINE:` / `CLOSED:` planning line |
| `created` | `:CREATED:` property |
| `tags` | `:tag1:tag2:` |
| `attributes` (node) | `:PROPERTIES:` drawer keys beyond mapped |
| `title`/`attributes` (doc) | `#+TITLE:` / `#+`-keywords |

`orgToCanonLoss()`: lossless for Tier 1–2; unmapped `:PROPERTIES:`/`#+` keywords →
`attributes`, **Reversible**.
`canonToOrgLoss()`: Reversible on `attributes` (re-emitted as `:PROPERTIES:`/`#+`). Org
expresses everything else first-class, so no Dropped/Degraded for canon-origin data.

> Reuse: consult existing `src/calendar/orgicalcanonstages.*` and the org I/O plumbing
> (`KALBURATOR_HAVE_ORG_IO`) for parsing/serialization patterns; do not fork a parser.

### 4.2 OPML 2.0 (`(outline, opml)`) — the thin/broad-reach interchange

OPML is structurally perfect (nested `<outline text=…>`, arbitrary attributes) but
semantically thin: **no native task/status/priority/date/checkbox.** Mapping:

| Canon | OPML |
|---|---|
| node | `<outline>` (XML containment = nesting) |
| `text` | `text` attribute (required) |
| `created` | `created` attribute (RFC-822) |
| `tags` | `category` attribute (comma-joined) |
| `attributes` (node) | arbitrary `<outline>` attributes (incl. `type`, `xmlUrl`, `isComment`) |
| doc `title`/dates | `<head>` (`title`, `dateCreated`, `dateModified`) |

`opmlToCanonLoss()`: lossless (OPML carries less than canon; everything maps up, unknown
attrs → `attributes` Reversible).
`canonToOpmlLoss()`: **honest loss** — `note` → an `_note` attribute (Reversible);
`done`/`status`/`priority`/`progress`/`start`/`due`/`completed` have no OPML
representation → **Dropped** (or, optional follow-on, stashed in namespaced attributes as
Reversible). The OPML edge is the domain's primary **loss-honesty test surface**.

---

## 5. Loss model summary

| Canon field | → Org | → OPML |
|---|---|---|
| `text`, `children`, `order`, `id` | lossless | lossless |
| `done`, `status` | lossless (TODO kw) | **Dropped** |
| `priority`, `progress` | lossless | **Dropped** |
| `start`/`due`/`completed`/`created` | lossless (planning/`:CREATED:`) | `created` only; rest **Dropped** |
| `note` | lossless (body) | Reversible (`_note` attr) |
| `tags` | lossless (`:tags:`) | Reversible (`category`) |
| `attributes` (Tier 3) | Reversible (`:PROPERTIES:`) | Reversible (arbitrary attrs) |

`LossKind` values used: `Reversible` (round-trips via extras/attributes), `Dropped` (no
target representation). No `Degraded`/`Simplified` needed in the first cut.

---

## 6. Component layout (isolation)

Mirror `src/note/` and `src/todo/`:

```
src/outline/
  outlinedomaindefinition.{h,cpp}   // DomainDefinition
  outlinecanonproperties.{h,cpp}    // makeOutlineCanonCatalogue()
  outlinestockshapes.{h,cpp}        // peers + edges
  outlineplugin.{h,cpp}             // Plugin wiring
  outlinediffer.{h,cpp}             // tree-aware 3-way diff (coarse first, structural follow-on)
  outlinemerger.{h,cpp}
  orgcanonstages.{h,cpp}            // OrgToCanonStage / CanonToOrgStage
  opmlcanonstages.{h,cpp}           // OpmlToCanonStage / CanonToOpmlStage
  outlinenode.{h,cpp}               // in-memory tree model + JSON (de)serialization
```

Each unit has one job, a clear interface, and is testable in isolation. `outlinenode`
(parse/serialize/compare the tree + JSON) is the shared core both stages and the differ
depend on — the single place tree structure is understood.

---

## 7. Testing (round-trip + loss-honesty, mirroring `tst_todo_canon_roundtrip`)

Under `tests/outline/`:

1. **`tst_outline_canon_roundtrip`** — build registries from `OutlineDomainDefinition`
   spine + `OutlineStockShapes`; assert `org → canon → org` is lossless for a fixture
   exercising every Tier 1–2 field + nesting + Tier-3 `:PROPERTIES:`.
2. **`tst_outline_loss_honesty`** — assert `canon → opml` reports exactly the Dropped set
   in §5, and `opml → canon → opml` round-trips OPML-expressible data; assert no *silent*
   loss (every dropped field appears in the composed `LossProfile`).
3. **`tst_outline_shapes`** — registry wiring: shapes registered, canon declared, edges
   present, `richnessRank` ordering.
4. **`tst_outline_differ`** — node-level 3-way merge: non-overlapping edits on different
   nodes auto-merge; same-node edits surface as conflict. (Coarse-differ variant first if
   sequenced that way.)

Fixtures: a small `.org` file and an `.opml` file checked into `tests/outline/data/`,
plus a canon-JSON golden.

---

## 8. Versioned-spine / invariants compliance

- Single-node canonical spine (`[canon]`) — no legacy root needed (new domain).
- All transformation via registered edges (INVARIANTS §1): no bespoke conversion paths.
- Unmapped data carried verbatim in `attributes`/`providerExtras`, never dropped silently
  (INVARIANTS loss-honesty).
- Recurrence/verbatim-line discipline not applicable (outline has no RRULE analog).

---

## 9. Open questions (resolve in plan or early implementation)

1. **Org parser source.** Reuse libkalburator's existing org I/O (calendar domain) vs. a
   minimal outline-focused parser? Lean reuse; confirm the existing parser exposes
   headline tree + planning + properties, not just calendar-relevant bits.
2. **Coarse vs. structural differ in the first cut.** Recommend coarse (pick-a-side) to
   land the round-trip, structural merge as the immediate follow-on. Confirm.
3. **OPML task stashing.** First cut Drops task fields to OPML (honest). Optional: stash
   in a `wp:`-namespaced attribute set as Reversible. Defer unless a real OPML consumer
   needs round-trip.
4. **`order` redundancy.** `children` array order already implies sibling order; keep
   explicit `order` only if a peer needs stable reordering identity. Likely drop it from
   Tier 1 and rely on array order. Decide in plan.

---

## 10. Out of scope (future, dependent specs)

- **`(outline, shadowplan-palm)` peer** + wiring ShadowStan's `libs/shadow` codec.
- **`(outline, shadowplan-json)` peer** (ShadowStan's JSON serialization as a 2nd peer).
- **WildPalms integration:** the ShadowPlan conduit rewrite to the current plugin ABI,
  companion-database resolution (tag IDs → names *into* canon), profile/mapping wiring,
  and any "alternative database handler / conduit picker" work.
- **Markdown-tasks and TaskPaper peers** — straightforward fast-follows once canon exists.
- **Cross-domain projection** (outline → todo VTODO, outline → note markdown) for syncing
  outline data to flat PIM targets — a separate design question.
```
