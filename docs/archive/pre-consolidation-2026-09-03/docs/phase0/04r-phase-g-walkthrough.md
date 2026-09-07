---
status: ideation — concrete walkthrough
date: 2026-04-30
phase: G (pre-design)
companion-to: 04r-phase-g-shape-pipeline-ideation.md
---

# Phase G — Concrete walkthrough: Alice's todos

**Status:** Ideation — concrete walkthrough. Companion to
`04r-phase-g-shape-pipeline-ideation.md`. This document is the
question-6 gate from that ideation: walk a real example end-to-end
and verify the shape-pipeline architecture produces a defensible
behaviour at every step.

The walkthrough deliberately sits in the middle ground — most users
will hit intra-domain multi-encoding and universal-sink backups; very
few will hit cross-domain bidirectional sync. Cross-domain projection
is touched once at the end as a probe of the degenerate corner.

If the architecture survives this walkthrough, it graduates from
ideation to design. If a scene exposes a structural break the model
can't accommodate, we either patch the model or fall back to path A
(multi-domain backends, no transformation matrix).

## Setting

Alice keeps her todos in three native places:

- **Akonadi** (KDE PIM) — her primary, in VTODO format
- **`tasks.org`** — an Emacs org-mode file she edits in the terminal
- **`tasks.todo.txt`** — a plain `todo.txt` file her phone reads

She also wants two backups:

- **`~/backup/todos/`** — raw files, one per record, source-encoded
- **`~/backup/pim.sqlite`** — a SQLite database with shape-aware tables

She has 10 todos to start with, varying in richness (some have
attendees, attachments, recurrence; some are plain "do thing").

She configures five mappings:

| # | Source | Target | Mode | Why interesting |
|---|---|---|---|---|
| 1 | Akonadi-todos | org-tasks | bidirectional | rich ↔ medium encoding gap |
| 2 | Akonadi-todos | todotxt-tasks | bidirectional | rich ↔ minimal encoding gap |
| 3 | org-tasks | todotxt-tasks | bidirectional | medium ↔ minimal; tests transitive propagation and hub-vs-peer |
| 4 | Akonadi-todos | raw-files-backup | one-way mirror | `Shape::Any` sink, identity passthrough |
| 5 | Akonadi-todos | generic-db-backup | one-way mirror | `Shape::Any` sink, shape-aware schema |

The walk progresses scene by scene; each scene ends with a
**Validates** block listing what architectural questions it answered
and a **Exposes** block listing any new design tensions surfaced.

---

## Scene 1 — Backend self-declaration

Each backend declares the shapes it can produce and consume. The
declarations:

```cpp
// Each backend overrides:
QList<Shape> nativeShapes() const;
```

- `AkonadiTodoBackend::nativeShapes() = { (todo, ical-vtodo) }`
- `OrgTodoBackend::nativeShapes() = { (todo, org) }`
- `TodotxtBackend::nativeShapes() = { (todo, todotxt) }`
- `RawFilesBackend::nativeShapes() = { Shape::Any }` — universal sink
- `GenericSqliteBackend::nativeShapes() = { Shape::Any }` — universal sink

`Shape::Any` is **not** a wildcard in the shape graph — it's a
declaration that this backend has no native shape and adopts whatever
shape the source provides. The engine treats `Shape::Any` specially:
when a sink declares it, the pipeline is the identity edge regardless
of source shape, and the *bytes* flow through unchanged. The shape
*metadata* travels separately as a sidecar.

**Two `Shape::Any` strategies:**

- `RawFilesBackend` writes `<recordId>.<encoding-id>.<domain-id>` as
  the filename, plus the shape's canonical bytes as the file body. No
  transformation. The shape is recoverable from the filename when
  the directory is later read as a source.
- `GenericSqliteBackend` interprets the source shape *as a schema
  hint*. On first encountering a new shape, it queries the shape's
  property catalogue (every registered shape has one — see Scene 2)
  and creates a table named `<domain>_<encoding>` with a column per
  property. Subsequent records of that shape go to that table. A
  `_shapes` metadata table records each shape's full schema for
  re-discovery.

**Validates:**

- The `Shape::Any` declaration is meaningful: a backend can opt out of
  shape-typing without breaking the registry.
- Universal sinks are first-class, not special-cased in the engine.

**Exposes:**

- Every shape needs a *property catalogue* — a list of `PropertyId`s
  that records of that shape can carry. This catalogue is what
  GenericSqliteBackend reads to build schemas; what loss profiles
  reference; what differs/mergers operate over. **The property
  catalogue per shape is now load-bearing.** Promoted to design-doc
  scope.

---

## Scene 2 — Edge registration

A `todo` domain plugin (sibling to a `calendar` plugin in
libkalburator) registers transformation edges. The walk picks
**hub-and-spoke** as the default structure: each domain has one
canonical shape, and peer encodings only have edges to/from the
canonical.

For todos, canonical is `(todo, ical-vtodo)`. The edges:

- `(todo, ical-vtodo) ↔ (todo, ical-vtodo)` — identity (always
  registered for every shape)
- `(todo, ical-vtodo) → (todo, org)` — lossy: drops `attendees`,
  `attachments`, full RRULE complexity, custom X-* properties,
  alarms, attachments
- `(todo, org) → (todo, ical-vtodo)` — lossless modulo unrepresentable
  org-specific markup (org tags survive as iCal CATEGORIES; org
  scheduled/deadline survive as DTSTART/DUE; org body survives as
  DESCRIPTION; org TODO state survives as STATUS)
- `(todo, ical-vtodo) → (todo, todotxt)` — heavily lossy: keeps
  summary, priority (mapped 1-9 → A-Z), completion mark, due-date as
  todotxt extension; drops description, recurrence, attendees,
  attachments, categories-as-tags become `+projects`, contexts must
  be inferred or empty
- `(todo, todotxt) → (todo, ical-vtodo)` — lossless modulo todotxt's
  oddities

The hub-and-spoke choice means org↔todotxt routes through
`(todo, ical-vtodo)`. Pipeline compilation for mapping #3 is a
two-step compose:

```
(todo, org) → (todo, ical-vtodo) → (todo, todotxt)
```

Loss profile is the *union* of the two steps' dropped properties.

**Why hub-and-spoke over peer-graph:**

- Adding a new encoding (say, Markdown todos) means registering 2
  edges (in/out of canonical), not N (one per peer)
- Loss compounds visibly through the hub — easier to explain to users
- The canonical shape is the natural shared vocabulary for diff/merge
  (Scene 5)
- Peer-graph optimisation can be added later as a registry hint
  without changing semantics

The trade-off: a peer pair like org↔todotxt that *could* preserve
some property the canonical doesn't model (hypothetically) loses it
unnecessarily. For the encodings in scope, this isn't a problem —
ical-vtodo strictly dominates both org and todotxt in expressivity.
We accept the trade.

**Validates:**

- Open question 2 (where edges live): **decided**. Backends declare
  native shapes only; **domain plugins own all transformation
  edges**. A backend that wants to register a non-canonical edge
  (e.g., a hypothetical Akonadi-internal-binary direct to org) is
  permitted but discouraged; it should fold into the domain plugin.
- Hub-and-spoke as the default; peer-graph as a future optimisation.

**Exposes:**

- The "richness ordering" between canonical shapes (per the ideation
  doc's conflict-resolution proposal) needs to be declared by the
  domain plugin too. Probably `domainPlugin->richnessRank()`
  returning a partial order. Promoted to design-doc scope.

---

## Scene 3 — Pre-flight loss profiles

Alice opens the sync configuration UI. For each of her five mappings,
the engine compiles the pipeline both directions and reports the loss
profile to the host.

The host renders, per mapping:

- **Mapping #1 (Akonadi ↔ org), bidirectional:**
  - `Akonadi → org`: lossy. Will not preserve: attendees,
    attachments, alarms, full recurrence (will simplify), custom
    X-* properties.
  - `org → Akonadi`: lossless modulo org-specific markup that has no
    iCal equivalent (rare).
- **Mapping #2 (Akonadi ↔ todotxt), bidirectional:**
  - `Akonadi → todotxt`: heavily lossy. Will not preserve:
    description, recurrence, attendees, attachments, alarms, custom
    X-* properties, exact priority levels (compressed to A-Z).
    Categories will be rendered as `+projects` if simple, dropped
    if complex.
  - `todotxt → Akonadi`: lossless modulo todotxt extensions that
    aren't standard.
- **Mapping #3 (org ↔ todotxt), bidirectional:**
  - `org → todotxt`: lossy (compounds two-step pipeline). Will not
    preserve: scheduled/deadline distinction (todotxt has one date),
    body text (todotxt has no description), …
  - `todotxt → org`: lossless modulo …
- **Mapping #4 (Akonadi → raw-files), one-way:** lossless. Identity
  pipeline. Files will be named `<uid>.ical-vtodo.todo`.
- **Mapping #5 (Akonadi → generic-db), one-way:** lossless. Will
  create table `todo_ical_vtodo` with columns `(uid TEXT PRIMARY
  KEY, summary TEXT, description TEXT, priority INTEGER, due TEXT,
  completed INTEGER, status TEXT, categories_json TEXT,
  attendees_json TEXT, attachments_json TEXT, custom_x_json TEXT,
  rrule TEXT, alarms_json TEXT)`.

The host **does not block** on lossy profiles — it shows them,
defaults each mapping's `WhenLossWouldOccur` policy to `Warn`, and
lets Alice click through. Mapping #2's profile is loud enough that
the host might surface it as a yellow icon next to the mapping.

**Validates:**

- Pre-flight UX is concrete and renderable.
- The structured loss profile (`QSet<PropertyId>` of dropped
  properties) is what the host needs.
- Open question 3 (loss profile reporting richness): **decided**.
  Structured `QSet<PropertyId>`, not boolean or enum.
- Open question 4 (does conflict policy absorb a `WhenLossWouldOccur`
  dimension?): **decided yes**. The mapping configuration grows a
  separate `WhenLossWouldOccur::Abort/Warn/Proceed` field, defaulting
  to `Warn`. Folded in.

**Exposes:**

- The host needs a UI for "show me what each pipeline drops in
  detail." Both PlanStan and WildPalms grow this surface. A small
  reusable Qt widget (`LossProfileDetailView`) probably belongs in
  libkalburator-qtwidgets (separate from the core library), to be
  consumed by both apps.

---

## Scene 4 — Initial mirror

Alice clicks "Sync now." All five mappings run. Walk through what
each destination contains afterward.

**Pre-state:** Akonadi has 10 todos; the other four targets are
empty.

**Mapping execution order:** scheduled by the engine. For now,
arbitrary — each mapping is a self-contained `SyncMapping`, the
engine has no "sync graph" awareness. (Scene 6 challenges this.)
Concretely the engine picks them in registration order: #1, #2, #3,
#4, #5. Each runs to completion before the next starts.

**Mapping #1 — Akonadi ↔ org, first sync.**

- Both sides have `Mode = Bidirectional`. No baselines yet.
- The first-sync quick-path (per the existing `dispatchFirstSync`
  guard from Phase D Task 21) checks: target empty? Yes. Route
  through the equivalent of `BlobSyncEngine::mirror`, but at the
  shape-pipeline level: source records flow through
  `(todo, ical-vtodo) → (todo, org)`, written to org file.
- Result: `tasks.org` has 10 todos, each lossy-projected.
  Attendees/attachments dropped. Org body holds iCal DESCRIPTION.
  Org tags hold iCal CATEGORIES. Org TODO state holds iCal STATUS.
  Org priority A/B/C derived from iCal PRIORITY 1-9.

**Mapping #2 — Akonadi ↔ todotxt, first sync.**

- Same first-sync path, target empty.
- Pipeline `(todo, ical-vtodo) → (todo, todotxt)`.
- `tasks.todo.txt` ends up with 10 lines like:
  ```
  (A) 2026-05-01 Buy milk +shopping
  x 2026-04-25 Call dentist
  Write design doc +work due:2026-05-15
  ```
- Many fields dropped.

**Mapping #3 — org ↔ todotxt, first sync.**

- *Both targets are now non-empty* (mappings #1 and #2 wrote them).
- The engine sees: org-tasks has 10 records, todotxt-tasks has 10
  records, both populated, no baselines.
- Quick-path's "target empty" guard fails. Falls into a
  `computeQuickDiff`-equivalent at canonical shape: bring both sides
  to `(todo, ical-vtodo)`, diff there, generate per-side change ops.
- For 10/10 records that match (Alice hasn't edited anything yet),
  diff is empty. No writes.
- For any record where the org-projection and todotxt-projection
  differ when both are promoted to canonical (e.g., a category that
  org preserved but todotxt dropped): canonical-shape diff sees
  org's version has CATEGORIES that todotxt's version doesn't have.
  With no baseline, conflict-policy `AskUser` would downgrade to
  `SourceWins` (org-side). Result: org's richer info promoted, sent
  through pipeline back to todotxt, which drops the category again.
  Round-tripped to a fixed point.

**Mapping #4 — Akonadi → raw-files, first sync.**

- One-way mirror, lossless.
- 10 files created: `~/backup/todos/<uid>.ical-vtodo.todo`, each
  containing the full iCal record bytes.
- A sidecar `~/backup/todos/_shapes.json` records each file's shape
  tuple for fast re-discovery (the filename encodes it too, but the
  manifest is faster).

**Mapping #5 — Akonadi → generic-db, first sync.**

- First-sync triggers schema creation. Engine gives the backend the
  shape `(todo, ical-vtodo)`; backend reads its property catalogue,
  creates table `todo_ical_vtodo`.
- 10 rows inserted, one per record.
- A `_shapes` metadata table records the schema and creation time.

**Validates:**

- The first-sync path generalises cleanly to shape-pipelines.
- Universal sinks accept records with no engine modifications.

**Exposes:**

- Mapping #3's behaviour at first sync — choosing org as source-wins
  by default — is *arbitrary*. The user might reasonably want
  todotxt to win, or to be asked. The mapping config needs an
  explicit `firstSyncPolicy` separate from the steady-state conflict
  policy. Promoted to design-doc scope.

---

## Scene 5 — Subsequent edits, encoding-gap conflict

A week passes. Alice edits one todo, "Buy milk", in two places:

- In **Akonadi**: changes priority 5→1 (so the iCal PRIORITY field
  goes from 5 to 1); adds an attendee `bob@example.org`.
- In **org-mode**: changes priority `[#B]` → `[#A]` (which the
  org→canonical edge maps to iCal PRIORITY 5→1); edits the body text
  from "milk for cereal" to "milk and cookies".

She fires sync. Mapping #1 runs.

**Engine walk:**

1. **Fetch** both sides. Receive 10 records each.
2. **Promote to canonical.** Akonadi side stays `(todo, ical-vtodo)`.
   Org side runs through edge `(todo, org) → (todo, ical-vtodo)`,
   producing canonical records.
3. **Load baseline** from baseline store (the per-record canonical
   form last successfully synced). Baseline for "Buy milk" has
   priority 5, body "milk for cereal", no attendees.
4. **Compute 3-way diff at canonical shape.**
   - Source diff (Akonadi vs baseline): priority 5→1; attendees ∅
     → {bob}.
   - Target diff (org vs baseline): priority 5→1; description
     "milk for cereal" → "milk and cookies".
   - Per-property merge:
     - `priority`: both sides agree on 1. **Not a conflict** —
       agreement.
     - `attendees`: source modified, target unchanged. Take source
       (Akonadi added bob).
     - `description`: source unchanged, target modified. Take
       target (org changed body).
   - Result canonical record: priority 1, description "milk and
     cookies", attendees {bob}, everything else from baseline.
5. **Project back to each side.**
   - To Akonadi (canonical → `(todo, ical-vtodo)`): identity. Write
     full merged record. Akonadi's "Buy milk" now has priority 1,
     description "milk and cookies", attendee bob.
   - To org (canonical → `(todo, org)`): lossy. Drops the attendee
     bob (org doesn't model attendees). Writes priority A,
     description "milk and cookies". Org's "Buy milk" has the merged
     body but never knew about the attendee.
6. **Update baseline** with the post-merge canonical record.

**Architectural observations:**

- Both sides agreed on priority despite using different encodings.
  The canonical-shape merge naturally handled this by promoting both
  to the same vocabulary before diffing. **Encoding-level "false
  conflicts" don't happen.** This is a real win.
- The org side never sees the attendee, but no information is
  *lost* — the attendee is in Akonadi (which models it) and in the
  baseline. If Alice later edits the org body again without
  touching the (already absent) attendee, the next merge keeps the
  attendee from the baseline. The org side is a *projection*, not a
  copy, and the merge model treats it as such.
- If Alice edits the org body again *and* somehow tries to also
  affect the attendee from org's side (impossible — org has no
  attendees), there's no regression because there's no attendee
  representation on org-side to diff against.

**Validates:**

- 3-way merge at canonical shape works for intra-domain
  multi-encoding.
- Open question 3 from the original list (where conflict resolution
  policy lives): **per-mapping for now**. The shape-aware
  `WhenLossWouldOccur` is separate. We don't need per-domain conflict
  policy yet.

**Exposes:**

- The "lossy projection on write" pattern means the target's
  *visible state* isn't always a faithful copy of canonical. If the
  user reads target and expects to see what canonical knows, they'll
  be confused. Mapping configuration UI should explain this with
  per-mapping wording: "this destination cannot represent attendees,
  attachments, …; they are preserved in baseline but not in this
  destination's records."
- Baseline storage is now *per-(mapping, record-id)* with the
  canonical-shape bytes. The existing `BlobBaselineStore`'s
  triple-keyed schema (`backend_id, collection_id, record_id`)
  generalises naturally to mapping-keyed; F1 already collapsed it.

---

## Scene 6 — Transitive propagation through the mapping graph

Alice's edit from Scene 5 has propagated through mapping #1
(Akonadi↔org). Now mapping #2 (Akonadi↔todotxt) runs.

**Engine walk:**

- Mapping #2 sees Akonadi has the *post-merge* "Buy milk" (priority
  1, body "milk and cookies", attendee bob).
- todotxt-side "Buy milk" is unchanged from initial mirror.
- 3-way diff at canonical: source modified (priority, body,
  attendee); target unchanged.
- Take source. Project to todotxt: drops attendee, drops body
  (todotxt has no description), keeps priority change and updates
  the line.
- todotxt's "Buy milk" line goes from `(B) Buy milk +shopping` to
  `(A) Buy milk +shopping`.

The org-side body change reached todotxt indirectly via Akonadi.
This is **emergent transitive propagation through pairwise
mappings**, not engine-aware graph traversal.

Mapping #3 (org↔todotxt) runs next:

- org-side "Buy milk" body is "milk and cookies" (from Scene 5).
- todotxt-side "Buy milk" priority is A (from above).
- Both promoted to canonical: priority A↔1, body present in org but
  absent in todotxt's projection.
- 3-way diff vs baseline (the org↔todotxt baseline, separate from
  mapping #1's): need to think about what *that* baseline contains.

**This is where it gets interesting.** Mapping #3's baseline records
*what canonical-shape state was last in agreement between org and
todotxt*. After the initial mirror in Scene 4 it agreed on the
projected-to-todotxt state — so the baseline for "Buy milk" had no
description. Then Akonadi's body edit propagated to org via mapping
#1; mapping #3 hasn't run yet. So mapping #3's baseline still says
"no description" and org-side now has a description.

Diff at canonical: source (org) has description "milk and cookies"
that the baseline doesn't have. Target (todotxt's projected
canonical) has nothing different from baseline. Take source's
description.

Project back to todotxt: dropped (todotxt has no description). No
todotxt-visible change. Update baseline to include the description.

This is *correct* but subtle: mapping #3 records that canonical
state has a description, even though todotxt can't carry it,
because next time someone edits todotxt, the baseline will let the
merge correctly identify that the description didn't come from a
todotxt edit.

**The order of mapping execution matters.** If mapping #3 had run
*before* mapping #1, it would have seen org-and-todotxt agreeing
(both unchanged from initial mirror), no diff, no work. Then mapping
#1 would have run, promoted Akonadi's change to org, and *not
propagated to todotxt* until the next sync round. Same eventual
state, slower convergence.

**Architectural choice surfaced:** does the engine grow a
`computeMappingGraph()` step that orders mappings by dependency, or
does it stay strictly pairwise-independent and converge over multiple
rounds?

**Decision (provisional):** stay strictly pairwise-independent.

Rationale:

- A graph-aware scheduler is significant complexity for a small
  win (one-round convergence vs eventual consistency).
- Real users sync periodically; eventual consistency over 2-3 rounds
  isn't user-visible.
- Pairwise mappings are a clean mental model; users can reason about
  them locally.
- The engine's existing `runSyncFuture(behavior)` already runs all
  mappings; the order can be a stable tiebreak (registration order,
  or alphabetical by mapping id) without claims of optimality.
- If a user wants all-at-once convergence, they can sync twice.

**Validates:**

- Open question 5 (concurrent mapping execution): **decided**.
  Mappings are independent; execution order is stable but not
  graph-optimised. Mappings can run *concurrently* if they don't
  share a backend (a future optimisation, gated on per-backend
  exclusivity declarations — see Scene 7).
- Transitive propagation across overlapping pairwise mappings
  works correctly with per-mapping baselines.

**Exposes:**

- Each mapping needs its **own** baseline keyspace. Today
  `BlobBaselineStore` is keyed `(backend_id, collection_id,
  record_id)`. For multiple mappings sharing a backend pair, this is
  ambiguous. Need to extend to `(mapping_id, record_id)` or
  equivalent. **Mapping-keyed baselines are required.** Promoted to
  design-doc scope. (F1's `BlobBaselineStore` consolidation provides
  the structural starting point, but the key shape changes again.)

---

## Scene 7 — Three-way encoded conflict

Harder version of Scene 5. Alice edits "Buy milk" in *all three*
native backends in conflicting ways:

- Akonadi: body → "milk and cookies"
- org: body → "milk and bread"
- todotxt: priority changed to (B); summary text → "Get milk"

She fires sync. Engine runs mappings #1, #2, #3 in registration
order.

**Mapping #1 — Akonadi ↔ org:**

- Promotes both to canonical. Akonadi: body "milk and cookies", priority 1. Org: body "milk and bread", priority 1.
- Baseline (post-Scene 5): body "milk and cookies", priority 1, attendee bob.
- Diffs:
  - Source (Akonadi vs baseline): no body change (still "milk and cookies").
  - Target (org vs baseline): body "milk and cookies" → "milk and bread".
- Per-property:
  - body: target modified, source unchanged. Take target. Result: "milk and bread".
- Write to both sides. Akonadi now has "milk and bread, attendee bob, priority 1". Org now has "milk and bread, priority A". Baseline updated.

**Mapping #2 — Akonadi ↔ todotxt:**

- Akonadi (now "milk and bread", priority 1, attendee bob) vs todotxt ("Get milk", priority B).
- Promote both. Canonical-Akonadi has body "milk and bread", summary "Buy milk", priority 1. Canonical-todotxt has summary "Get milk", priority 5 (mapped from B).
- Baseline (post-Scene 5 for this mapping): summary "Buy milk", body "milk and cookies", priority 1.
- Diffs:
  - Source (Akonadi vs baseline): body "milk and cookies" → "milk and bread". (Note: this baseline still says "cookies" because mapping #2's baseline is separate.)
  - Target (todotxt vs baseline): summary "Buy milk" → "Get milk"; priority 1 → 5.
- Per-property:
  - body: source modified, target absent. Take source: "milk and bread". todotxt projection drops it.
  - summary: target modified, source unchanged. Take target: "Get milk".
  - priority: target modified (1→5), source unchanged. Take target: 5.
- Write back. Akonadi now has summary "Get milk", body "milk and bread", priority 5. todotxt's line is `(?) Get milk +shopping`.
- *Wait*: priority went from 1 (Akonadi's edit) to 5 (todotxt's edit). Akonadi's priority change was already merged in mapping #1; now it gets *overwritten* by todotxt's change.

**This is genuinely confusing for Alice.**

The problem: mapping #1's merge baseline got updated to priority 1 in Scene 5, *before* mapping #2 saw that priority change. Mapping #2's separate baseline still believed priority was the original 5. Alice's todotxt edit changing priority to B (=5) is, from mapping #2's perspective, "matching baseline." But from mapping #1's perspective, the agreed canonical priority is 1.

Pairwise mappings with separate baselines mean **disagreements between mappings about what canonical state is "current."**

**Mapping #3 — org ↔ todotxt:**

- Runs next, sees current org (post-#1) and current todotxt (post-#2). Both have potentially diverged from this mapping's baseline.
- Without belabouring the walk: similar reconciliation, picks values per-property based on its own baseline.

**Final state after one round:**

- Akonadi: summary "Get milk", body "milk and bread", priority 5
- org: summary depends on mapping #3's outcome; body "milk and bread", priority depends on outcome
- todotxt: summary "Get milk", priority B (=5)

This is *not* what any of the three single-source merges would have produced. It's a genuine artefact of the pairwise-baseline model.

**Architectural choice:** does libkalburator grow a "mapping group" abstraction (3+ backends sharing a single canonical baseline, with one N-way merge per record) or stay pairwise?

**Decision (provisional):** support **mapping groups as a future feature**, optional, off by default. For initial design, pairwise-only with the documented behaviour above. Reasons:

- Pairwise covers the common case (most users sync 1-2 backends per data type).
- Mapping groups need conflict-resolution semantics for N-way merges that are non-trivial to specify and explain.
- The artefacts of pairwise convergence are well-known in PIM-sync history (Apple's CalDAV-vs-Akonadi-vs-Google triangles, e.g.); users have learned to manage them.
- A mapping group can be *layered on* later: it's syntactic sugar for "use one shared baseline for these N pairwise mappings", which the engine can implement without changing the per-mapping pipeline shape.

**Validates:**

- The pairwise model is buildable and produces *some* defensible behaviour at every step, even if "defensible" requires explanation in scene 7.
- Mapping groups are a clean future extension, not an architectural earthquake.

**Exposes:**

- The user-facing documentation needs a "what to expect when you have 3+ backends" section. PlanStan and WildPalms both grow this.
- A `mapping group id` field on `SyncMapping` is a forward-compatible hook to enable the future feature without schema migration.

---

## Scene 8 — Universal-sink read-back

Alice's `tasks.org` file is gone. She rm'd it by accident. She wants
to restore from `~/backup/todos/`.

She configures a temporary mapping #6: `raw-files-backup → org-tasks`,
one-way mirror.

**Engine walk:**

- RawFilesBackend, when used as a *source*, scans its directory.
  For each `<uid>.<encoding-id>.<domain-id>` file, it parses the
  filename to recover the shape, reads the bytes, hands the engine a
  `BackendRecord` with the shape attached.
- Mapping #6 source shape: `(todo, ical-vtodo)` (recovered per-record).
  Target shape: `(todo, org)`.
- Pipeline: `(todo, ical-vtodo) → (todo, org)`, lossy projection.
- 10 records flow through, written to a fresh `tasks.org`.
- Result: org file restored, with the same lossy-projection it had
  originally. Alice's *org-side* edits to "Buy milk" are gone (they
  were never in the backup), but the rest of her data is intact.

**Architectural observation:** RawFilesBackend's symmetric
source/sink behaviour requires the shape metadata to round-trip
through storage. The filename-encoded scheme works. A sidecar
`_shapes.json` is redundant; it's a fast-path index, not the source
of truth. If `_shapes.json` is corrupt or missing, the directory is
still recoverable from filenames.

**Validates:**

- `Shape::Any` backends round-trip cleanly when used as both source
  and sink.
- The shape metadata storage strategy works.

**Exposes:**

- If a user manually edits `~/backup/todos/abc123.ical-vtodo.todo`
  without renaming the file, the shape claim "ical-vtodo" might no
  longer match the bytes. The sink-as-source path needs a
  *validation* step: parse the bytes against the shape's schema
  before yielding the record. If parsing fails, the record is
  reported as corrupt and skipped (or aborts the sync, per a
  `corruptRecordPolicy` mapping field). Promoted to design-doc scope.

---

## Scene 9 — One degenerate inter-domain touch

Alice adds a sixth mapping: `org-tasks → memo-summary`, one-way,
where the memo backend writes a single `~/notes/todos.txt` plaintext
file that summarises her todos as a list.

**Engine walk:**

- Source shape: `(todo, org)`. Target shape: `(memo, plaintext)`.
- Pipeline compilation: search the edge graph for a path. Direct
  edge doesn't exist. Hub-and-spoke: `(todo, org) →
  (todo, ical-vtodo)`. Then need an edge `(todo, ical-vtodo) →
  (memo, plaintext)`. The `todo` and `memo` domain plugins
  jointly register cross-domain edges; one is `(todo, ical-vtodo)
  → (memo, plaintext)` with loss profile **degenerate-projection**:
  drops everything except summary, priority marker, completion
  status. Output is a one-line-per-record render.
- Pre-flight loss profile rendered prominently:
  > Mapping `org-tasks → memo-summary` will produce a memo where each
  > todo becomes one line. The line will contain: `[ ]` or `[x]` for
  > completion, the summary text, and the priority marker. **All
  > other fields (description, due dates, attendees, attachments,
  > tags, recurrence, …) will be dropped.** Continue?
- Alice clicks through. `WhenLossWouldOccur::Proceed` for this mapping.
- Sync runs. `~/notes/todos.txt`:
  ```
  [ ] (A) Buy milk
  [x] Call dentist
  [ ] Write design doc
  ...
  ```

**Reverse direction not registered.** The `(memo, plaintext) →
(todo, ical-vtodo)` edge could be defined (parse each line as a
todo) but it would be an *enrichment* — fabricating priority,
status, summary from arbitrary text. The domain plugin chooses not
to register it. If Alice tries to configure
`memo-summary → org-tasks`, the engine reports "no path"; the
mapping is rejected at config time.

**Validates:**

- Cross-domain projection edges work and produce useful output.
- Pre-flight UX scales to "everything gets dropped" cases without
  changing shape.
- The "no reverse edge" decision is enforced cleanly: pipelines that
  don't compile are rejected at config time, not silently zero-output
  at run time.

**Exposes:**

- Domain plugins make policy decisions (which cross-domain edges
  exist) that are effectively *library-level UX choices*. A future
  design-doc concern: should domain plugins be opinionated (libkalburator
  ships an opinionated set; users can't add edges) or extensible
  (users can register their own edges via plugin API)? Provisional
  answer: opinionated for stock domains; extensibility deferred.

---

## What graduated; what's still ideation

**Decisions taken inline (graduate to design doc):**

- **`Shape::Any` is a real declaration**, not a wildcard. (Scene 1)
- **Each shape has a property catalogue** that drives diff/merge,
  schema generation in universal sinks, and loss profile reporting.
  (Scene 1, 5)
- **Domain plugins own all transformation edges**; backends declare
  native shapes only. (Scene 2)
- **Hub-and-spoke** is the default edge structure; peer-graph is a
  future optimisation hint. (Scene 2)
- **Domain plugins declare a richness rank** for cross-domain
  conflict resolution. (Scene 2)
- **Loss profile is structured `QSet<PropertyId>`**, not boolean or
  enum. (Scene 3)
- **`WhenLossWouldOccur::Abort/Warn/Proceed`** is a separate mapping
  field from conflict policy. (Scene 3)
- **First-sync policy is a separate mapping field** from steady-state
  conflict policy. (Scene 4)
- **Mappings are pairwise-independent** with eventual-consistency
  convergence over multiple rounds. (Scenes 6, 7)
- **Baselines are mapping-keyed**, not just (backend, collection,
  record). (Scene 6)
- **Mapping groups are a future extension**, off by default in
  initial design. (Scene 7)
- **Universal-sink round-trip requires shape metadata in storage**;
  filename-encoded for raw-files, schema-table for generic-db.
  (Scenes 1, 8)
- **Corrupt-record policy** is a mapping field. (Scene 8)
- **Stock domain plugins are opinionated**; cross-domain edges are
  library-curated, not user-extensible (initially). (Scene 9)

**Still open (deferred to design doc):**

- The exact `PropertyId` type and registration mechanism per shape
- The shape graph compilation algorithm (hub-and-spoke with
  shortest-path tiebreaks)
- How "single-occupancy resource" backends (Palm) declare exclusivity
  — unaddressed in this walk; needs a separate walkthrough or design-
  doc scene
- The shape of `ISyncHost` post-bend (open question 6 from the
  ideation doc) — unaddressed in this walk; existing shape probably
  survives with minor additions for shape-aware reporting
- Concrete migration ordering once design is settled

**Architectural break-glass triggers — none hit in this walk.**

The walkthrough produced a defensible behaviour at every step. The
"three-way encoded conflict" in Scene 7 is the nearest the model
came to a structural break, but the artefact is well-known in PIM
sync, the mitigation (mapping groups as future feature) is clean,
and the documentation burden is acceptable.

## Recommendation

**Graduate the ideation to design.** The shape-pipeline architecture
survives concrete contact with reality at the middle-ground intensity
this walkthrough applies. The 13 inline decisions above feed directly
into a `04r-phase-g-design.md` that can be written next.

Two follow-up walkthroughs would strengthen the design before
implementation:

1. **WildPalms HotSync walkthrough** — exercises single-occupancy
   resources (Palm device), the dissolution of `SyncRunner_wp` into
   per-profile mapping registries, and HotSync UX as a session of
   N mappings. Probes open questions 4 and 9 from the original list.
2. **Migration walkthrough** — concrete plan for moving the ~8000
   lines of PlanStan backend tests, deleting the deprecated
   synchronous I/O API, retiring the F1 facade, and rewriting
   WildPalms's `SyncRunner_wp`. The implementation slicing question
   from path D's deferred scope.

Both are smaller than this walkthrough; they're scoped enough to
follow as discrete next moves rather than blocking on one big design
pass.

## Cross-references

- `04r-phase-g-shape-pipeline-ideation.md` — the architectural
  exploration this walkthrough validates
- `04q-phase-f2-threading-outcome.md` — the F2 closure that left
  WildPalms structurally insulated; this design replaces that
  insulation with first-class participation
- `~/dev/refactor-engine-merger/CURRENT-STATUS.md` — Phase G design
  exploration in flight (path D)
- `~/dev/refactor-engine-merger/ROADMAP.md` — original Phase G
  framing (opaque + plugin diff); the shape-pipeline architecture
  broadens that framing
