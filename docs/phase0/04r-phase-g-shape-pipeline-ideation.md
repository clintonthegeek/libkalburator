---
status: ideation
date: 2026-04-30
phase: G (pre-design)
supersedes: nothing
superseded-by: nothing yet
---

# Phase G — Shape pipeline architecture (ideation)

**Status:** Ideation. No design or plan yet. Captured 2026-04-30 from a
brainstorming session that opened with "we are free to rewrite and
refactor PlanStan and WildPalms as much as necessary to accommodate
libkalburator's intended final form."

This document is the architectural exploration record. It is **not** a
design document and does not commit the project to any specific
implementation. A formal `04r-phase-g-design.md` and `04r-phase-g-plan.md`
pair will follow once the open questions at the bottom converge.

The brainstorm covered two interleaved decisions: an **ordering** decision
(path D — extended design pass before any code lands) and an
**architectural shape** decision (the shape-pipeline model below). The
ordering decision is recorded in
`~/dev/refactor-engine-merger/CURRENT-STATUS.md`; this document captures
the architectural shape exploration in enough detail that the next
session can pick up the thread without re-deriving it.

## Destination context

The session reaffirmed the long-form destination libkalburator is bending
toward:

- **libkalburator** = an orchestrator (the unified `SyncEngine` shipped in
  F1/F2) plus a battery of stock PIM backends (calendar today; contacts,
  memos, todos, address books planned) plus a registered plugin point
  for novel backends and domains.
- **PlanStan** = a calendar-app consumer of libkalburator. Its current
  ~8000 lines of backend tests in `tests/backends/` belong to
  libkalburator (the contracts they pin are libkalburator's contracts)
  and will migrate during this phase. Tests for PlanStan-specific app
  behavior stay in PlanStan.
- **WildPalms** = a multi-PIM consumer of libkalburator that happens to
  ship with a Palm-pilot backend. The Palm device's HotSync
  serial-occupancy property is **a property of the Palm backend**
  (single-occupancy resource), not of the orchestrator. WildPalms's
  current `SyncRunner_wp` orchestration layer dissolves; per-profile
  `SyncMapping` registries replace it. WildPalms gains multi-way
  coordination across Akonadi, CalDAV, DecSync, etc. as a first-class
  feature, "for free" via libkalburator adoption.
- **Test ownership rule:** stock backends → libkalburator owns the tests.
  Consumer-provided backends (e.g., WildPalms's `PalmCalendarBackend`,
  `PalmBackend`, the plugin V2 backends) → consumer owns the tests.

The user has explicitly accepted that achieving this destination involves
substantial work — *"I don't mind that outcome taking a lot of work"* —
including, if necessary, re-inventing WildPalms's sync layer from scratch.

## The original Phase G framing — path A

The opening question of the design pass was structural: in a multi-PIM
world, **what's the relationship between a backend and a PIM domain**?
Today libkalburator's `SyncBackend` declares its domain via a single
`DataDomain dataDomain()` getter (`Calendar` or `Project`). That's a
single-domain assumption hangover from PlanStan's calendar-only origin.
In the destination world (Akonadi serving calendar + contacts + tasks;
DecSync serving calendar + contacts + memos; Palm serving all four), a
single-domain assumption breaks immediately.

Three options were proposed:

- **Path A — Multi-domain backends; collection carries the domain.** A
  `SyncBackend` instance can serve any number of domains. Domain is a
  property of each `Collection` it exposes (`akonadi:calendar/personal`
  vs `akonadi:contacts/personal`). `SyncMapping` references
  `(backend, collection)`; the engine reads the collection's domain to
  route diff/merge through the right plugin. The `dataDomain()` getter
  on the backend goes away. Closest to the existing shape; minimal
  interface churn. **The recommended path.**
- **Path B — Multi-domain backends; mapping carries the domain
  explicitly.** Backend serves N domains; mapping says
  `(sourceBackend, sourceCollection, domain)`. Lets the same collection
  sync as different domains in unusual cases (e.g., a folder of `.ics`
  files used as either a calendar or a tasks list). More flexible,
  slightly weirder.
- **Path C — Single-domain backend instances; one backend per domain.**
  `AkonadiCalendarBackend`, `AkonadiContactsBackend`,
  `AkonadiTasksBackend` are three separate classes each implementing
  `SyncBackend`. Maximum interface conservatism; pushes complexity into
  backend implementation duplication.

Path A was the recommendation because it matches how Akonadi, DecSync,
and Palm actually structure their data internally (one connection / one
device, many collections of different shapes), it makes "the collection
is the unit of sync" the lead concept, and it falls out naturally in a
multi-PIM world. The cost is killing the `dataDomain()` getter and
migrating its few callers (mostly registry routing).

## The pushback — generalization toward path B

The user pushed back on path A and asked to "play around with the
potential of B a bit." But the pushback wasn't really about path B as
posed (mapping-carries-domain). It was about a much larger structural
insight:

> *we already have lossy transformation matrices for intra-domain
> translations (between org-mode files or palm database entries and
> icalendar, for instance). how much would we gain or lose by
> generalizing that? so that mapping becomes a matter of dynamically
> determined compatibility? […] each backend is now responsible for,
> somehow, meeting every other backend part-way or all the way in
> determining co-compatibility. but it absorbs the lossy transformational
> model into an inter-domain problem.*

The insight: **encoding transcoding** (Phase E's `TranscodingPlan`,
`PropertyTranscoder`, `RruleTranscoder`), **capability mismatch handling**
(the warning-emission path), and **domain routing** (the `IDomainAdapter`
introduced in F1) are three special cases of the same operation:
*transforming a record from one shape to another, possibly with loss*.
If we generalize that, all three concepts collapse into a single
architectural primitive, and inter-domain syncing falls out as just a
special case of "the destination's shape can't represent some of the
source's properties."

The whimsical example — *"if I attach an address book to a to-do list, I
get a todo list full of people"* — was raised explicitly as a
**correctness probe**. A coherent design must have defined behavior for
that case that doesn't crash and doesn't refuse, even if the behavior is
degenerate. The cases that less-extreme syncs care about are then
automatically handled.

## The shape-pipeline architecture

What follows is the architectural sketch this generalization implies. It
is the substance of what the brainstorm produced as exploration; it has
not been validated against a concrete end-to-end example yet (see open
question 6).

### Vocabulary

The engine's vocabulary is **records-with-shape**, where shape is
`(domain, encoding)`:

- `(calendar, ical)` — canonical calendar
- `(contacts, vcard)` — canonical contacts
- `(memo, plaintext)` — canonical memos
- `(todo, ical-vtodo)` — canonical todos
- `(palm-datebook, native-binary)` — Palm-specific calendar shape
- `(palm-address, native-binary)` — Palm-specific contacts shape
- `(blob, opaque)` — generic byte-level (the existing blob substrate)

A backend declares which shapes it natively produces and consumes. The
engine, when planning a sync, computes a **transformation pipeline** from
source-shape to target-shape by composing registered transformations.
Each pipeline has a known **loss profile**:

- **Lossless** — pure encoding round-trip
- **Intra-domain lossy** — same domain, capability loss (today's
  `PropertyTranscoder` case)
- **Inter-domain projection** — different domain, structural reduction
  (calendar → memo summary)
- **Degenerate-name-only** — different domain, only string-name preserved
  (contacts → todo titles)

### Code shape (sketch)

```cpp
struct Shape { DomainId domain; EncodingId encoding; };

struct TransformationEdge {
    Shape from;
    Shape to;
    LossProfile loss;            // lossless / intra-lossy / projection / degenerate
    QSet<PropertyId> dropped;    // what gets lost going from -> to
    Pipeline::Stage stage;       // the actual work fn
};

class TransformationRegistry {
    void registerEdge(TransformationEdge);
    std::optional<Pipeline> compile(Shape from, Shape to);   // graph search
    LossProfile inspect(Shape from, Shape to);               // pre-flight UX
};
```

Backends declare their native shape (e.g. `palm:datebook + native-binary`).
Domain plugins (or libkalburator core) register the edges:

- `palm-datebook+native-binary ↔ calendar+ical`
- `calendar+ical ↔ calendar+org`
- `calendar+ical → memo+plaintext` (one-way projection)
- `contacts+vcard → todo+ical-vtodo` (degenerate projection: name → todo
  title)

The graph is sparse — most edges cluster around the canonical encoding
for each domain.

The engine's diff/merge becomes shape-typed: `IRecordDiffer` for
`calendar+ical`, for `contacts+vcard`, etc. Cross-shape diff doesn't
exist; the pipeline normalizes both sides to a common canonical shape
*before* diffing. So a sync between `(palm, datebook+native)` and
`(akonadi, calendar+ical)` runs both sides through transformations to a
common `(calendar+ical)` middle, diffs there, and transforms outputs back
out — like a compiler IR.

### What gets cleaner

- **`TranscodingPlan` dissolves.** It becomes a property of an edge. The
  "warning path" becomes "the compiled pipeline reports its loss profile;
  the host decides whether to abort, warn, or proceed."
- **Phase E's "where does transcoding live?" question evaporates.** It
  lives at edges. Backends declare their native shape; everything else
  is registry routing.
- **Phase G's `IRecordDiffer` / `IRecordMerger` plugins get a sharper
  definition.** They're keyed on a canonical shape per domain, not on a
  backend or a domain. The differ for `calendar+ical` is reused by every
  backend that produces or consumes calendar data.
- **The "what does Palm actually do" model gets cleaner.** Each Palm DB
  type is a backend-shape. The Palm backend doesn't pretend to be a
  calendar — it's `palm-datebook`. The transformation
  `palm-datebook ↔ calendar+ical` is a registered edge, owned by either
  the Palm backend or a domain plugin, but visible to the engine as
  "just an edge."
- **Adding a new PIM type is structurally cheap.** Bookmarks
  (`bookmark+netscape-html`)? Register the shape; register edges to/from
  `memo+plaintext` and `contacts+vcard`. Done. Interoperates with
  everything.

### What gets harder

- **Conflict resolution across loss boundaries is genuinely hard.** If
  both sides edited and the round-trip composition isn't an identity
  (almost never the case across domain gaps), 3-way merge has a
  pre-canonicalization problem: which side's richer shape wins?
- **The transformation graph isn't always unique.** From
  `(akonadi, calendar+ical)` to `(palm, datebook+native)` there might be
  `direct` and `via-org-mode`. We need a cost metric. Probably: lossless
  edges first, then minimal-loss-profile, then shortest path. Tiebreak
  deterministically.
- **Pre-flight UX gets bigger.** "You're about to sync your
  `(akonadi-contacts, vcard)` collection to your
  `(palm, memo+plaintext)` collection. The pipeline will drop: phone
  numbers, email addresses, postal addresses, photos, custom fields.
  Continue?" PlanStan and WildPalms both grow this UI surface.
- **Test combinatorics.** A naive "test every shape pair against every
  backend" matrix is N² × M and explodes. Pragmatic discipline: each
  transformation edge gets a unit test; each backend gets a
  "round-trips on its native shape" test; a small number of end-to-end
  pipeline tests cover composition; nothing tests the full cross-product.
- **`BackendRecord` opacity assumption tightens.** Today
  `BackendRecord::data` is opaque bytes. For shape-aware diff, the bytes
  must be parseable in their declared shape by the differ. That's
  already implicit (a calendar record's bytes are iCal); the design just
  makes it contractual.

### Conflict resolution proposal

Three resolution shapes, picked per mapping:

1. **Same shape on both sides.** Standard 3-way merge using the shape's
   `IRecordDiffer`. Identical to today's calendar story.
2. **Different shapes, common richer ancestor.** Promote both sides to
   the canonical richer shape, merge there, project back to the lossier
   side. The lossy projection has been pre-baselined so the projection
   direction is deterministic.
3. **Different shapes, no common richer ancestor (degenerate
   cross-domain).** Bidirectional refused unless the user has explicitly
   declared one side authoritative. Default is one-way mirror with the
   user-chosen primary.

Domains have a partial order of richness for case 2:
`calendar > todo > memo`, `contacts > memo`, etc. Bidirectional sync
across a domain gap is opt-in with the richer side declared as
authoritative. One-way sync across gaps is the unmarked default.

The "todo list of contacts" case is bucket 3. The user has explicitly
opted into a degenerate sync; the system happily projects names from
contacts into todo titles, never tries to do the reverse. If the user
*also* wants the reverse, they configure a second mapping with the
inverse direction; both project through the same `(contacts ↔ todo)`
edges but only ever in one direction at a time per mapping.

### The degenerate edge case as correctness probe

The address-book-as-todo-list example sounds whimsical but does real
architectural work. It tells us a coherent design must define a behavior
for it that doesn't crash and doesn't refuse — even if the behavior is
"turn each contact into a todo named `Contact: <full name>` with no
other data." That's the **degenerate-but-defined** corner; if the
architecture handles it gracefully, it handles every less-degenerate
case automatically. It's the same probe physicists use when they ask
"what does this theory say about a one-electron universe?"

The corollary: every domain we add has a *required canonical projection*
to every other domain we want it to be cross-syncable with. Most
projections will be "drop everything except a name-like field." That
projection is automatable from a "name field" annotation per domain.

### Cost estimate

- Original Phase G as roadmapped (plugin domain model with
  `IRecordDiffer`/`IRecordMerger`): ~6-8 weeks.
- This generalization: probably **+4-6 weeks on top**, mostly in design
  rigor and pre-flight UX surface, less in raw implementation (the
  engine itself gets *simpler* under this model than under hardcoded
  calendar+blob).
- Total: **10-14 week phase if done end-to-end**.

The implementation can follow a **walking-skeleton pattern**: stand up
the registry, the pipeline compiler, and the canonical-shape-diff
infrastructure with calendar-only registered first; verify the
architecture against the calendar contract tests; only then start adding
edges. If the architecture proves wrong at calendar-only, we've spent
2-3 weeks not 12.

The framework can be designed once and implemented incrementally — ship
with calendar + blob edges only, add contacts/memos/todos as they become
useful, never having to refactor the engine again.

## Confidence

Per the brainstorm's closing self-assessment: **70% sold on this being
the right architecture**. The 30% uncertainty centers on whether the
conflict-across-domain-gaps story is buildable in practice or whether
real users immediately hit cases the design doesn't cover.

The next session should perform the question-6 concrete walk-through
(below) before this document graduates to a design.

## Open questions to push on next

These are the questions the next design session needs to address before
this becomes a buildable design.

1. **What's the canonical shape per domain?** `calendar+ical` is obvious.
   `contacts+vcard` is obvious. `memo+plaintext` (vs `+markdown`?).
   `todo+ical-vtodo` (vs `+textual`?). `palm-{datebook, address, memo,
   todo, plucker, webcal}+native` for Palm-specific shapes. Choosing
   wrong here costs us later.
2. **Where do edges live?** Three plausible homes: in domain plugins
   (each owns its outgoing edges), in backends (each declares its
   conversions to/from canonical shapes), or in a third "transformation
   library" sibling that depends on neither. Probable mix: backends
   declare `native ↔ canonical` edges; domain plugins declare
   `canonical ↔ canonical` cross-domain edges. Cleanly separates "this
   backend's idiosyncrasies" from "this is how PIM types relate."
3. **Loss profile reporting — how rich?** Boolean "lossy yes/no"? Enum
   with four levels? Structured `QSet<PropertyId>` of what gets dropped?
   Probably structured form, because the host needs to render *what*
   will be lost.
4. **Does this absorb conflict policies too?** Today
   `ConflictPolicy::SourceWins/TargetWins/LastWriteWins/AskUser` is
   per-mapping. In the new model, "policy" might also have a shape-aware
   dimension: `WhenLossWouldOccur::Abort/Warn/Proceed/AskUser`. Probably
   worth folding in.
5. **Run-time vs compile-time pipeline.** Should the registry be
   populated at static-init time (everything compiled in, fastest,
   simplest) or support plugins loaded from `.so` files? Static-init is
   much simpler; user-installable backends would load their edges in
   their `Q_PLUGIN_METADATA` init. Push for static-only at first;
   defer dynamic-plugin question.
6. **Concrete walk-through.** Walk a real example end-to-end (e.g.,
   Akonadi-contacts ↔ Palm-memos in both directions) and verify the
   design produces a defensible behavior at every step. **This is the
   test that determines whether to commit to this architecture or fall
   back to path A.**

## Open questions deferred from the original brainstorm

The original brainstorm listed nine design questions. The shape-pipeline
ideation answers question 1 (single-domain vs multi-domain) implicitly —
backends are multi-domain by virtue of declaring multiple
`(domain, encoding)` shapes — and reframes question 2 (granularity of
plugin registration) as the edges question above. The remaining seven
still need to be addressed:

3. **Where conflict resolution policy lives** (generic vs per-domain) —
   partially addressed above; bidirectional cross-domain sync semantics
   need to be nailed down.
4. **How "single-occupancy" resources (Palm device) are modeled** —
   backend mutex? scheduler constraint? sync-session concept?
5. **Concurrent mapping execution** — are independent mappings parallel
   by default?
6. **Shape of `ISyncHost`** after the bend — stays calendar-shaped,
   fragments into per-domain hosts, or dissolves into a generic event
   sink?
7. **Where transcoding/capability lives in a multi-domain world** —
   answered: at edges in the transformation registry. But the
   *capability declaration* (what does a backend support?) still needs
   to be modeled.
8. **Test organization post-migration** — per-(backend, domain)?
   per-edge? per-pipeline? Mix.
9. **WildPalms HotSync UX-to-engine mapping** — does HotSync become
   "a session of N mappings", a single multi-mapping invocation, or
   just normal mappings the user happens to fire when the device is
   connected?

## Cross-references

- `~/dev/refactor-engine-merger/CURRENT-STATUS.md` — current refactor
  state, ordering decision (path D)
- `~/dev/refactor-engine-merger/ROADMAP.md` — the original Phase G
  framing
- `~/dev/refactor-engine-merger/FINDINGS.md` — cumulative findings;
  relevant entries: F1 dispatch decoupling status, F2
  `BlobDomainAdapter` not registered for unified dispatch
- `04q-phase-f2-threading-outcome.md` — F2 closure, Angle 3 documents
  WildPalms structural blocker that this ideation aims to resolve
- `04k-engine-merger-roadmap.md` — phase tagging convention; Phase G's
  tag will be `v0.15-phase-g-opaque-plugin` per the current convention
  (may want to rename if the shape-pipeline architecture is adopted)
