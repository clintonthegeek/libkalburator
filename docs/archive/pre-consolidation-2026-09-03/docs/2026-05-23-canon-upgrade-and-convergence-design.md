# Design: Rich Canonical Shapes, the Versioned Canonical Spine, and Transcoding/Shape-Graph Convergence

**Date:** 2026-05-23
**Status:** Design approved (architecture level); field-level canon schema deferred to a follow-on artifact.
**Revision (2026-05-23, during review):** added `todo` as a third upgraded domain (§4.3, §9.3);
resolved recurrence storage = **raw RFC5545 text** (§4.0); resolved registry lifetime =
**per-engine** (§8).
**Companion docs:**
- `docs/2026-05-23-transcoding-shape-graph-convergence-handoff.md` — the WildPalms→libkalburator handoff this design answers.
- `docs/2026-05-23-vendor-api-shapes-reference.md` — the durable field-level capture of the Google /
  Microsoft Graph / iCal shapes this design targets. §9 here is the summary; that file is the evidence.
- `docs/2026-05-23-canon-schema-design.md` — the concrete field-by-field schema of `calendar+canon`,
  `contacts+canon`, `todo+canon` (fills the §12 "field-by-field schema" follow-on).
- This file is the libkalburator-side design response.

---

## 1. Why we are doing this

Three motivations arrived together and turn out to be **one mechanism viewed at three time horizons**:

1. **Now — convergence (handoff goal a).** libkalburator has two parallel, non-referencing
   conversion subsystems: the shape graph (`src/shape/`, keyed on `(domain, encoding)`,
   byte-level `TransformationEdge`/`Pipeline`/`LossProfile`) and the legacy property
   transcoder (`src/transcoding/`, keyed on backend-type-string pairs, operating on live
   `KCalendarCore::Incidence`). Only calendar uses the legacy path, and in practice it does
   exactly one thing: org-mode RRULE simplification for PlanStan. We want **one** mechanism —
   the shape graph — so every domain's transformation, loss accounting, and warning surface
   flows through it, and `src/transcoding/` is retired.

2. **Now — richer canon (handoff goal b).** The current canonical shapes (`calendar+ical`,
   `contacts+vcard4`, `todo+ical-vtodo`) are based on decades-old formats. Google
   (Calendar/People/Tasks) and Microsoft Graph (events / contacts / To Do) model events, people,
   and todos far more richly (online meetings, response tracking, sensitivity, typed multi-value
   contact fields, cross-app task links, professional graph). We want to upgrade the canon for all
   three of these domains to models capable of holding that richness. (Todos were added to scope
   during review — see §4.3; PlanStan/WildPalms do not yet properly exercise the existing todo
   interface, so bringing them in now avoids a second pass.)

3. **Future — transparent canon upgrades.** Once a third party ships a libkalburator consumer,
   we can no longer rewrite every consumer's edges in lockstep when we bump a canon. We want the
   capacity for an **unmodified** downstream peer to keep working when libkalburator upgrades a
   domain's canonical shape, provided the upgrade is a backward-compatible widening.

The unifying insight: **a canon upgrade is a new encoding in the domain, and a canon-version
bridge is a `TransformationEdge` with a `LossProfile`.** The shape graph already has that
vocabulary. So (2) reshapes the graph so that (1) becomes "add the edges, delete
`src/transcoding/`," and (3) is "generalize the router to allow a *chain* of canon-version
bridges."

---

## 2. Scope boundary

This is the YAGNI line, confirmed during design.

**Build now (real, load-bearing):**
- Convergence: retire `src/transcoding/`; make calendar a first-class shape-graph citizen;
  re-express org-mode RRULE simplification as a loss-annotated shape-graph edge.
- Rich canon encodings for `calendar`, `contacts`, and `todo`, with `ical`/`vcard`/`ical-vtodo`
  demoted to **permanent lossy peer encodings**.
- Recurrence stored as **raw RFC5545 text** in the canon, parsed only at the edges that need
  structure (see §4.0).
- Router generalization from "single canonical, exactly 2 hops" to a **versioned canonical spine,
  N hops along the spine** — even though only one spine node exists per domain on day one.
- The **four-level loss model** (see §6), derived from the API research.
- **Per-engine registry instances** (see §8).

**Design + synthetic-test now, but do NOT build the live machinery:**
- Transparent auto-bridging for *unmodified third-party* peers (the consumer-transparency magic).
- Capability introspection/declaration by backends.
- Load-time enforcement of canon-version compatibility ("B" below).

These are proven against a **synthetic v1→v2 fixture** that pins the spine's contract, so adding a
real v2 later is *data, not a rewrite*. We do not ship consumer-transparency we have no consumer
for. The reason the spine itself is built now (not deferred) is that retrofitting "single
canonical, 2-hop" into "spine, N-hop" later is the expensive change; the spine is cheap to add
while we are already reshaping the graph.

**Compatibility verification ("B"):** a `v1 → v2 → v1` round-trip check lives in **libkalburator's
own CI**, not as a runtime load-time gate. It is most valuable where we own both canon versions.

---

## 3. The three-horizon picture in one table

| Horizon | Artifact | Status in this design |
|---|---|---|
| Now (a) | `src/transcoding/` retired; calendar routes through shape graph | Build |
| Now (b) | `calendar+canon`, `contacts+canon`, `todo+canon` rich encodings; `ical`/`vcard`/`ical-vtodo` as lossy peers | Build |
| Now (foundation) | Versioned canonical spine in `TransformationRegistry`; 4-level loss model | Build |
| Future (3) | Transparent auto-bridge for third-party peers; capability objects; load-time enforcement | Design + synthetic test only |

---

## 4. The canon is its own model — not a clone of any vendor

**Decision:** each upgraded canon is a **new, library-owned `EncodingId`** (e.g.
`calendar+canon`, `contacts+canon`, `todo+canon`), a normalized superset — **not** an adoption of
Google's or Microsoft's schema, and **not** raw iCal/vCard.

### 4.0 Recurrence is stored as raw RFC5545 text (custody, not interpretation)

**Decision:** the canon stores recurrence as the **verbatim RFC5545 text** (RRULE/RDATE/EXDATE
lines), not as a parsed structure.

Rationale: RFC5545 is the recurrence superset (§9.1, §9.3), so storing it loses nothing. Parsing it
into our own structure would mean trusting — and round-tripping through — our own re-serializer
against the many backend writers in the wild (CalDAV servers, Exchange, org-mode), which are
variously buggy. If a server emits malformed recurrence, that is the server's defect; by keeping the
finished form verbatim we do not convert their bug into ours. The canon is a **faithful custodian**.

Consequences:
- Parsing is **confined to the one edge that needs structure**: `canon → Microsoft`
  (`patternedRecurrence`). A parse failure or an out-of-subset rule there is a **localized,
  well-defined edge loss** (simplified-not-dropped, or dropped-with-warning per §6) — never canon
  corruption. `canon → Google`/`canon → iCal` emit the stored text directly; `iCal/Google → canon`
  ingest it directly.
- The differ/merger treat recurrence as a **single opaque field** (changed/unchanged), not a
  sub-part 3-way merge. This is intentional and safer: we never silently rewrite a user's
  recurrence by recomposing parsed fragments.

In EIP's **Levels of Transformation** terms (Hohpe & Woolf, "Message Translator"), this is a
decision to operate strictly at and above the **Data Representation** (syntax) layer for recurrence:
we transport the finished RFC5545 string and only the single `canon → Microsoft` edge descends to
the Data Types/Structures layers to parse it. A parse failure there is a localized edge loss
(§6), not canon corruption.

This is forced by the research (§9):

- **Events.** Google's recurrence model *is* RFC5545; Microsoft's structured `patternedRecurrence`
  is a **strict subset** of RFC5545 (cannot express sub-daily frequencies, `BYWEEKNO`/`BYYEARDAY`,
  intersected `BYMONTHDAY`+`BYDAY`, multi-value `BYMONTHDAY`, multiple RRULEs, `RDATE`, `EXRULE`,
  or `RANGE=THISANDFUTURE`). Therefore:
  - Canon recurrence is **RFC5545-native**. `canon → Google` is trivial; `canon → Microsoft` is
    **simplify-or-expand** (lossy but well-defined — the generalization of today's org-mode RRULE
    problem); `ical → canon` is **lossless** (iCal *is* RFC5545).
  - Neither vendor schema is a safe canon: Google cannot structurally hold Microsoft's materialized
    exception model; Microsoft cannot hold RFC5545's recurrence richness. The superset-of-both,
    RFC5545-anchored model is the only one that round-trips.
  - Two places require **dual representation**: recurrence (raw RFC5545, plus optionally a derived
    MS pattern when expressible) and `RANGE=THISANDFUTURE` overrides (no single-object form in any
    of the three — modeled as a series-split).
- **People.** Canon-people v1 = superset of **vCard ∪ Google `Person` ∪ Microsoft `contact`**. The
  Microsoft relevance/insight layer (`person`, scored email) is out-of-band metadata, not authored
  data. Microsoft's rich `profile` professional graph is **beta-only** and is **explicitly excluded
  from canon v1** — depending on a non-GA surface would violate the stability a canon requires.
  (When `profile` reaches GA, it becomes the first real **people-canon v1 → v2** bump — see §7.)

### 4.3 Todos (added during review)

Canon-todo upgrades today's `todo+ical-vtodo`. The research (§9.3) confirms the same spine logic as
events: **VTODO/RFC5545 is the superset**, Microsoft To Do (`todoTask`) reuses the identical
`patternedRecurrence` subset (so the recurrence conclusion and §4.0 text-custody apply verbatim),
and Google Tasks is a deliberately minimal subset (no API recurrence, date-only `due`, binary
status, one-level nesting, no priority/percent). Therefore:

- `canon-todo` = superset of **VTODO ∪ Google Tasks ∪ MS To Do**, anchored on VTODO, augmented by
  the handful of fields VTODO lacks: **extended status** (`waitingOnOthers`/`deferred`),
  **cross-app linked resources** (app/externalId/webUrl), **lightweight checklist items** (distinct
  from full subtasks), **HTML body** (+ plaintext fallback), a **due-precision flag**
  (date-only | datetime+zone), and a **sibling-ordering** field (Google `position`).
- `ical-vtodo → canon` is **lossless**; `canon → Google Tasks` and `canon → MS To Do` are heavily
  but well-definedly lossy (Google loses almost everything but title/notes/due-date/status; MS loses
  PERCENT-COMPLETE, fine-grained priority, extra alarms, deep subtask trees, exotic RRULEs).
- **Hierarchy is the carry-multiple-representations spot for todos** (analogous to `THISANDFUTURE`
  for events): VTODO's arbitrary `RELATED-TO` tree, Google's single-level full-task subtasks, and
  MS's degenerate checklist items are **not isomorphic**. The canon retains the source's hierarchy
  form rather than normalizing, so no collapse loses information.

Scope note: this design covers the **libkalburator-side** `todo+canon` shape and its iCal-vtodo
bridge. WildPalms moving its `palm-todo` backend off `(blob, raw)` onto `(todo, palm) → todo+canon`
remains WildPalms's follow-on port (handoff §3), but now has a real target to port onto.

**Consequence for the graph:** `ical`, `vcard`, `gcal`, `msgraph`, `palm`, `org`, `todotxt`, etc.
all become **peer encodings** of their domain, each with an honest `LossProfile` on its edges
to/from canon. `contacts` already works exactly this way (`vcard3 ↔ vcard4`,
`PalmToVCardStage`); this design makes calendar match, and upgrades the hub.

---

## 5. The versioned canonical spine (router generalization)

### 5.0 Pattern grounding — this is a Canonical Data Model with a Format Indicator

This section is not a local invention; it is a composite of two named, published patterns from
Hohpe & Woolf, *Enterprise Integration Patterns* (Addison-Wesley, 2003), confirmed against the
primary text:

- **Canonical Data Model** ("Canonical Data Model", EIP). "Design a Canonical Data Model that is
  independent from any specific application. Require each application to produce and consume
  messages in this common format." This is our canon: peer encodings (iCal/vCard/Graph) translate
  *to and from* the canon rather than to each other. EIP quantifies the payoff — *"a solution
  consisting of six applications requires 30 (!) Message Translators without a Canonical Data Model
  and only 12 when using a Canonical Data Model"* — the N² → 2N collapse that justifies the hub.
  The src→canon→tgt two-hop is what EIP calls **double translation**, and the canon-vs-peer split
  is EIP's **public (canonical) vs. private (application-specific) messages** distinction. Note EIP
  also licenses §4's "model only what syncs": *"the Canonical Data Model does not have to model the
  complete set of data… only the portion that participates"* — the provider-extras bag (schema §1.3)
  carries the rest.
- **Format Indicator** ("Format Indicator", EIP). EIP closes the Canonical Data Model pattern with:
  *"As always, the only constant is change. Therefore, messages conforming to the Canonical Data
  Model should specify a Format Indicator."* A canonical model is **incomplete without a version
  indicator** — which is precisely the spine's per-domain version (`v1, v2, …`). Of EIP's three
  Format Indicator implementations (Version Number / Foreign Key / Format Document) the spine uses
  the **Version Number**.
- The individual edges are EIP **Message Translators** (themselves "the messaging equivalent of the
  **Adapter** pattern [GoF]"); multiple peer encodings converging on the canon via one translator
  each, routed by source format, is EIP's **Normalizer**; and the canon (de)serialization stages
  (Plan 3) are EIP's **Messaging Mapper** (a mapper *inside* the boundary) as opposed to an external
  Message Translator.

The append-only / never-rewrite-a-peer rule (§5.2, invariant 2) is **the Format Indicator
rationale verbatim**: EIP motivates it by observing that *"some applications will be converted
before others, while some less-used applications may never be converted at all… applications will
have to be able to support the old format and the new format simultaneously."* That is exactly why
an existing peer edge keeps pointing at its original spine node and is bridged forward by the
router rather than rewritten.

### 5.1 Today

`TransformationRegistry` (`src/shape/transformationregistry.h`) holds one canonical per domain
(`QHash<DomainId, Shape> m_canonicalByDomain`) and `compile(from, to)` does at most two hops:
`from → canonicalFor(domain) → to`. A domain **freezes** on first `compile()`.

### 5.2 Target

Replace "one canonical Shape per domain" with an **ordered canonical spine per domain**: a list of
canon encodings `[v1, v2, …, vN]` where `vN` (the head) is the **current** canonical — the diff/merge
anchor and `richnessRank` ceiling. Older spine nodes are **retained permanently** as bridge anchors.

Adjacent spine nodes are connected by a **bridge edge pair**:
- `v(k) → v(k+1)` — **lossless widening** (older canon is a subset of newer).
- `v(k+1) → v(k)` — **well-defined narrowing** (drops, simplifies, or reversibly-stashes the
  newer-only fields; carries a `LossProfile` classified per §6).

`compile(from, to)` becomes: route `from → (its anchor spine node) → … along the spine … → (to's
anchor spine node) → to`. Concretely, a peer edge written against `v1` and a diff happening at the
head `vN` compose as `peer → v1 → v2 → … → vN` (promote) and the reverse on demote. The single-
canonical 2-hop case is just the N=1 spine.

**This is the whole point:** a future canon bump appends one node + one bridge-edge pair to the
spine. Existing peer edges, still pointing at their original spine node, are **automatically
extended along the spine** by the router. No peer edge is rewritten.

### 5.3 What is deferred

The router generalization (build now) is the mechanism. **Transparent auto-bridging for
unmodified third-party peers** is the *policy* of relying on it in production for code we do not
own — deferred. We build the spine, prove the auto-extension with a synthetic v1→v2 fixture, but
do not advertise/guarantee third-party transparency until a third party exists.

### 5.4 The migration-decoupling seam (the corrected "dogfood")

The `ical ↔ canon` bridge edge is **not** a transitory shim to be deleted — iCal is permanent
(caldav, local, org-mode are genuinely iCalendar backends). It is a **permanent peer** that, during
the migration window, doubles as the seam letting our own consumers (PlanStan, WildPalms) port on
their own schedule:

- During migration, a consumer keeps its existing iCal/vCard-targeted codecs and rides the bridge
  (lossy toward rich canon, but **works unchanged**) — which is exactly why this branch can merge
  before WildPalms ports.
- Later, each consumer rewrites its codec to target `canon` directly and gains the rich fields
  losslessly.

Nothing is deleted; consumers merely stop *relying* on the bridge as a fallback. PlanStan's
org-mode sync running green through `canon → org-ical` edges *is* the live dogfood of the converged,
bridged pipeline.

---

## 6. The four-level loss model

The handoff (§5.1) flagged that `LossProfile` (`src/shape/lossprofile.h`) models only "dropped"
properties. The API research makes the required taxonomy concrete. Replace the current
`LossLevel { Lossless, IntraDomainLossy, InterDomainProjection, Degenerate }` characterization with
a model that distinguishes **how** a property is lost, because consumers (and the
`WhenLossWouldOccur` policy) must treat these differently:

1. **Dropped** — the target encoding cannot represent the property and it is gone.
   *Example:* `canon → ical` drops online-meeting info, `sensitivity=personal`, `eventType`.
2. **Simplified-not-dropped** — the property survives in a reduced form.
   *Example:* `canon` RFC5545 recurrence → Microsoft `patternedRecurrence` (simplify-or-expand);
   the org-mode RRULE case.
3. **Reversible-via-extension** — the property is moved into an extension/`X-` property so a
   round-trip is lossless even when the forward direction is nominally lossy.
   *Example:* `showAs=oof` → `X-MICROSOFT-CDO-BUSYSTATUS`; WildPalms `X-WP-PALM-*` identity stamping.
4. **Preserved-but-degraded** — the value is mapped through a lossy, many-to-one vocabulary, so the
   original must be kept verbatim to be recoverable.
   *Example:* IANA → Windows time-zone mapping (`windowsZones.xml` is many-to-one). The canon keeps
   the original IANA string.

These four kinds sit at specific layers of Hohpe & Woolf's **Levels of Transformation** stack
(EIP, "Message Translator": Data Structures / Data Types / Data Representation / Transport).
*Preserved-but-degraded* is a **Data Types**-layer loss — EIP's own example for that layer is a
lossy vocabulary remap (*"Replace U.S. state name with two-character code"*), the exact shape of our
IANA → Windows time-zone case. *Simplified-not-dropped* and *Dropped* are **Data Structures**-layer
losses (an entity/cardinality can't be expressed in the target). *Reversible-via-extension* is the
escape hatch that keeps a Data-Types/Structures loss round-trippable by stashing at the
representation layer. (Recurrence custody, §4.0, is the deliberate refusal to descend *below* the
**Data Representation** layer — we keep the finished RFC5545 syntax verbatim and never re-render it.)

`LossProfile` must carry, per affected `PropertyId`, *which* of these applies (not just a flat
"dropped" set), and `compose()`/`summary()` must fold them sensibly along a pipeline. The
`transcodingWarning` surface becomes a projection of `Pipeline::composedLoss()` /
`LossProfile::summary()` at the same point in the sync lifecycle (handoff §5.3). The
`SyncMapping.lossPolicy` (Abort/Warn/Proceed) continues to gate on the composed loss of the **whole**
path (handoff §5.4).

---

## 7. The §4 keying tension dissolves

The handoff's one hard blocker (§4) — capability differs even when encoding is identical
("org-mode can't represent complex RRULEs even though both sides are `calendar/ical`") — is
**removed by the canon upgrade**, not worked around. Once `canon` is the rich hub and everything
else is a lossy downgrade peer:

- Org-mode is just a **less-capable peer encoding**. RRULE simplification is the `LossProfile` on
  the `canon → org-ical` edge (a "simplified-not-dropped" loss). No backend-type-string keying, no
  capability objects, no `src/transcoding/`.
- The thing that kept the two subsystems from merging for years is gone.

(Backend *capability introspection* — the deferred "Phase F capability objects" — is therefore not
required for this work. If two backends share the `ical` peer but differ in capability, model the
weaker one as its own peer encoding with its own loss edge, exactly as we do for org-mode.)

---

## 8. Registry lifetime (Plan 2)

> **Resolution note (2026-05-24).** This section was rewritten from its planning-stub form after
> Plan 1 landed and the real call sites were read. The stub said "**two** singletons remain
> (`TransformationRegistry`, `DomainRegistry`)" and "owned by the `SyncEngine`." Both were
> imprecise; the corrections below are **documented deviations** from the stub per the INVARIANTS
> deviation rule. See FINDINGS O6.

### 8.1 The problem, named

There are **four** registry singletons in play, not two. `TranscodingRegistry` disappears with
`src/transcoding/` (Plan 4). Of the rest, `BackendRegistry` (`Sync::`) is **already
dependency-injected** — `PluginManager(BackendRegistry*)` and `SyncEngine(BackendRegistry*, …)`
both take it by reference. The holdouts are the **three `Shape::` registries**, all still reached
via `::instance()`:

- `TransformationRegistry` — the shape graph + the versioned spine (Plan 1).
- `DomainRegistry` — domain definitions / canonical shapes.
- `DomainOperationsRegistry` — per-domain operation handlers. **The stub omitted this one; the
  engine reads it at `syncengine.cpp:1874` and `:2423`, and ~40 tests `clear()` it.**

The shared state is **ambient**: `PluginManager` is the *writer* (`applyPlugin`, pluginmanager.cpp
:120–199 — registers domains/shapes/edges/operations and unwinds on failure) and `SyncEngine` is the
*reader* (`:1403/1861/1874/1885/2417/2423/2641` — `inspect`/`compile`/`definitionFor`/`operationsFor`).
Neither holds the other's state; both just trust `::instance()` resolves to the same global. This is
the textbook **Service Locator** anti-pattern (Seemann, *Service Locator is an Anti-Pattern*, 2010):
hidden dependencies, runtime-not-compile-time failure, and no test isolation — hence the defensive
`clear()` ritual in every `cleanup()`.

### 8.2 Pattern grounding

The three registries are legitimate instances of Fowler's **Registry** pattern (*PoEAA*, 2002) — the
bug is that they were given **process-global Singleton scope** for state that is actually
**per-engine** (mutable, plugin-populated, test-varying). Fowler's own caution applies: a Registry
reached through hardcoded statics "becomes a Service Locator… an effective sinkhole for
dependencies," and the fix he points to is Dependency Injection. The end state is **Constructor
Injection wired at a Composition Root** (Fowler, *IoC Containers and the DI pattern*, 2004; van
Deursen & Seemann, *DIPPP*, 2019). The reference architecture for "a plugin host *writes* a registry
and a separate consumer *reads* it, sharing without globals" is the **OSGi service registry /
Eclipse extension registry**, which hand the registry to both sides via an injected context
(`BundleContext`), never a language singleton. We already do exactly this for `BackendRegistry`;
Plan 2 finishes the job for the three `Shape::` registries.

### 8.3 Decision — an injected `ShapeRegistries` bundle

A new composition-root product, `src/shape/shaperegistries.h`:

```cpp
struct ShapeRegistries {
    TransformationRegistry   transformation;   // holds the versioned spine
    DomainRegistry           domain;
    DomainOperationsRegistry operations;
};
```

Owned **by value at the composition root** (the consumer, or a stack-local in a test) — this is the
shape-state `BundleContext`. It is passed **by reference to both** `PluginManager` (writer) and
`SyncEngine` (reader); the same instance is shared structurally, not ambiently. The versioned spine
lives in `transformation`, so each bundle gives an engine its own spine state. Engine read sites
become `m_shape.transformation` / `.domain` / `.operations`; PluginManager write/unwind sites use its
injected bundle.

`BackendRegistry` is **not** folded into the bundle — it is a `Sync::` concern with a different
lifetime, already injected; folding it is unrelated refactoring (scope-boundary discipline,
invariant 8) and an explicit non-goal of Plan 2.

### 8.4 Migration via a documented Ambient-Context scaffold

A hard cutover would break the external contracts (invariant 10): PlanStan and WildPalms call the
current `SyncEngine(BackendRegistry*, ISyncHost*)` ctor. So Plan 2 keeps a **process-global default
bundle** — `ShapeRegistries& defaultShapeRegistries()` — and the three `::instance()` accessors are
retained but **delegate to its members**. The existing ctors are kept as overloads binding to the
default bundle; **new injecting overloads** take a `ShapeRegistries&`. Downstream compiles unchanged
and stays green per commit.

This default is exactly Seemann's **Ambient Context** — and he classifies it as an *anti-pattern*
usable **only as removable migration scaffolding**, not an end state. Therefore:

- Plan 2 converts the engine path and the integration tests to **explicit injection** (stack-local
  bundles, dropping their `clear()` calls — isolation becomes lexical scope).
- The Ambient-Context default and the `::instance()` accessors are **scheduled for removal** once
  PlanStan/WildPalms adopt the injecting ctor — downstream-port work, **not** Plan 2 (tracked as
  FINDINGS O7). Plan 2 lands the seam green; it does not delete the global.

The implementation plan owns the mechanical migration (the `ShapeRegistries` header, constructor
wiring, threading the reference through `SyncEngine`/`SyncEngineWorker` and `PluginManager`, the
seven engine read sites, the `::instance()`→default-bundle delegation, and converting the ~40 test
`cleanup()` blocks).

---

## 9. Research grounding (Google / Microsoft / iCal)

Verified 2026-05-23 from vendor docs + Microsoft `[MS-OXCICAL]`/`[MS-STANOICAL]` open specs. These
are the *conclusions*; the full field tables (Google Calendar event, MS `event`, Google People
`Person`, MS `contact`, Google Tasks, MS `todoTask`, plus the recurrence/exception/timezone analysis
and per-domain canon-targeting summary) live in `docs/2026-05-23-vendor-api-shapes-reference.md`.

### 9.1 Events
- **Recurrence is mutually lossy but asymmetric.** RFC5545 (= Google = iCal) is the superset;
  Microsoft is a strict subset. `MS → RFC5545` is lossless if `index`/`last` are emitted as
  `BYSETPOS` (not numbered `BYDAY`). `RFC5545 → MS` loses sub-daily freq, `BYWEEKNO`, `BYYEARDAY`,
  intersected/multi-value `BYxxx`, multiple RRULEs, `RDATE`, `EXRULE`; `EXDATE` becomes materialized
  cancellations. Exchange itself expands-or-fails on unsupported rules.
- **Exceptions** key on **original start time** across all three (iCal `RECURRENCE-ID` ↔ Google
  `originalStartTime` ↔ MS `originalStart`/`occurrenceId`). Hard cases: `RANGE=THISANDFUTURE` (no
  single-object form anywhere — series-split), and RDATE-added instances under Microsoft (no attach
  point).
- **Time/zone:** store **zone per endpoint**, keep the **original IANA string verbatim** (Windows
  mapping is many-to-one/lossy), and carry an explicit **floating-time** flag (neither Google nor
  Microsoft has true floating time).
- **Rich event fields with no iCal home** (canon holds them; `→ ical` drops or X-prop's them):
  online meeting (`isOnlineMeeting`/`onlineMeetingProvider`/`onlineMeeting`), `allowNewTimeProposals`,
  `hideAttendees`, `isDraft`, attendee `type=resource` (rooms), `sensitivity=personal`,
  `showAs=oof/workingElsewhere`, multi-`locations[]`, item-attachments, Google `eventType`
  (focusTime/outOfOffice/workingLocation/birthday) and their typed property sub-objects.

### 9.2 People
- Google `Person`: rich, repeated typed sub-objects, each with source/primary metadata.
- Microsoft splits a person across three layers: `contact` (editable, vCard-like, **GA**), `person`
  (read-only relevance aggregation), `profile` (rich professional graph, **beta-only**).
- Canon-people v1 = superset of vCard ∪ Google `Person` ∪ MS `contact`; relevance layer is metadata;
  `profile` excluded until GA (then a real v2 bump).

### 9.3 Todos
- **VTODO/RFC5545 is the superset spine** — it dominates on recurrence (full RRULE ⊇
  `patternedRecurrence`), reminders (multi-`VALARM` ⊇ single reminder), priority (0–9 ⊇ 3-level),
  progress (`PERCENT-COMPLETE`), and hierarchy (`RELATED-TO` tree ⊇ both single-level models).
- **Microsoft To Do (`todoTask`)** reuses the **same `patternedRecurrence`** object as events, so
  the events recurrence conclusion holds verbatim (RFC5545 superset; MS a cleanly-mappable subset).
  Adds beyond VTODO: extended `status` (`waitingOnOthers`/`deferred`), `linkedResources` (cross-app),
  `checklistItems` (lightweight checkboxes, *not* full tasks), HTML `body`, `importance`.
  (Older `outlookTask` is deprecated; `todoTask` is current.)
- **Google Tasks** is minimal: **no API-level recurrence**, **date-only `due`**, binary
  status (`needsAction`/`completed`), **one-level** nesting, no priority/percent/categories/alarms.
  Contributes `position` (sibling ordering), `assignmentInfo`, typed `links` — mostly output-only.
- **Hierarchy is the irreducible divergence** (carry multiple representations): arbitrary VTODO tree
  vs. Google single-level full-task subtasks vs. MS degenerate checklist items — not isomorphic.
- Due/start precision differs (VTODO date|datetime; MS datetime+zone; Google date-only) → canon
  needs a precision flag. Reminders: VTODO multi-`VALARM` ⊇ MS single reminder ⊇ Google none.

---

## 10. Affected code (orientation for the implementation plan)

Shape graph (`src/shape/`) — extend:
- `transformationregistry.{h,cpp}` — replace `m_canonicalByDomain` single-Shape with an ordered
  spine per domain; generalize `compile()` to N-hop spine routing; preserve freeze semantics.
- `lossprofile.{h,cpp}` — four-level loss taxonomy + per-`PropertyId` classification; `compose()`/
  `summary()` updates.
- `domaindefinition.h` — `canonicalShape()` returns the spine head; differ/merger operate at head.
- `pipeline.{h,cpp}` — compose along the spine; `composedLoss()` folds the new taxonomy.
- `recordwriter.h` — remove the `ApplyContext.transcodingPlan` seam.

Calendar (`src/calendar/`) — bring up to contacts parity:
- `calendarstockshapes.cpp` — today registers only an identity `ical → ical` edge. Add the rich
  `canon` encoding + `ical ↔ canon` bridge edges + `org-ical` peer with the RRULE-simplification
  loss edge.
- `createincidenceitem.*`, `calendarplugin_writer.*`, all backend `pushItems`/`startSync` — drop the
  `TranscodingPlan` parameter and the `CalendarPluginWriter` special-case apply path.

Todo (`src/todo/`) — upgrade the hub:
- `tododomaindefinition.{h,cpp}` — `canonicalShape()` becomes `todo+canon`; `ical-vtodo` demoted to
  peer; differ/merger operate at the `todo+canon` head.
- `todostockshapes.cpp` — register `todo+canon` + `ical-vtodo ↔ canon` bridge edges; keep the
  existing `todotxt` peer with its (now §6-classified) loss edges.

Engine (`src/engine/syncengine.cpp`) — remove the second subsystem:
- `:1885-1889` shape pipeline compilation — now does real work for calendar (not identity).
- `:2434-2481` `TranscodingPlan` computation + `ApplyContext` injection — **deleted**.
- `:2083` `createCanonicalDiffer()` — **unchanged** (handoff §5, invariant 5: keep diff shape-side).

Delete: `src/transcoding/` in full (after RRULE simplification is re-homed as a shape edge), and its
references from the engine constructor/worker.

---

## 11. Acceptance criteria

Mirrors the handoff's §6 plus our additions:

- [ ] `src/transcoding/` deleted (or reduced to a thin deprecation shim).
- [ ] Org-mode RRULE simplification re-expressed as a shape-graph edge with a `LossProfile`,
      reachable by PlanStan with no backend-type-string routing.
- [ ] `RecordWriter::ApplyContext` no longer carries a `TranscodingPlan`; no `CalendarPluginWriter`
      special-casing in the engine.
- [ ] Calendar, contacts (and the path for todo/memo) all use the identical
      "declare peer encoding + register edges to canon" pattern.
- [ ] `calendar+canon`, `contacts+canon`, and `todo+canon` rich encodings exist; `ical`/`vcard`/
      `ical-vtodo` are lossy peers; `ical → canon` / `vcard → canon` / `ical-vtodo → canon` are
      **lossless**.
- [ ] Canon recurrence is stored as raw RFC5545 text; recurrence parsing exists only on the
      `canon → Microsoft` edge, and a parse failure there is a localized edge loss, not corruption.
- [ ] Loss model distinguishes dropped / simplified / reversible-via-extension / preserved-but-degraded.
- [ ] `TransformationRegistry` holds a versioned canonical **spine**; `compile()` routes N-hop along
      it; the single-node case reproduces today's behavior.
- [ ] Synthetic `v1 → v2` fixture: an unchanged peer edge against `v1` still resolves and round-trips
      when `v2` is appended to the spine.
- [ ] CI `v1 → v2 → v1` round-trip compatibility check (the "B" guard) passes.
- [ ] Registry lifetime story decided and documented.
- [ ] PlanStan ctest baseline green (org-mode sync preserved) — per project policy, every commit.
- [ ] WildPalms invariants preserved or given equivalents: X-property round-trip stamping
      (reversible loss), per-category virtual sub-collections untouched, a warning channel,
      `lossPolicy` honored on composed path loss, diff stays shape-side.

---

## 12. Non-goals / explicitly deferred

- Field-by-field canon **schema** for `calendar+canon` / `contacts+canon` / `todo+canon` — **now
  written** in `docs/2026-05-23-canon-schema-design.md` (no longer deferred). Live Google/Graph calls
  to validate edge cases remain optional follow-up during implementation.
- `memo` domain: **left on `(blob, raw)`** — no rich-vendor API forces richness and WildPalms treats
  memos as markdown-over-blob. Not upgraded in this campaign.
- Transparent auto-bridging **relied upon in production for third-party peers**; capability
  introspection objects; load-time compatibility enforcement.
- Microsoft `profile` professional-graph fields in canon-people (beta; future v2).
- WildPalms's own port (they do it after this branch merges) and their queued work (typed routing
  for todo/memo, category-lifecycle conflicts).
- Cross-domain routing (still out of scope; this work is intra-domain along the spine).

---

## 13. Open questions for the implementation plan

1. Exact shape of the spine data structure and whether `richnessRank` is subsumed by spine order.
2. How the canon represents the **non-isomorphic hierarchy** shapes it must retain — events'
   `THISANDFUTURE` series-split and todos' three subtask models (§4.3) — i.e. the concrete
   "carry multiple representations" container.
**Resolved during review (2026-05-23):**
- *Recurrence storage* → **raw RFC5545 text** (§4.0). Differ treats recurrence as one opaque field.
- *Registry lifetime* → **per-engine instances** (§8).
- *Todo scope* → `todo+canon` is **in scope, libkalburator-side** (§4.3); WildPalms's `palm-todo`
  port off `(blob, raw)` remains their follow-on.
- *Memo scope* → **out** (stays `(blob, raw)`; §12).
- *Vendor API research* → **done and persisted** to `docs/2026-05-23-vendor-api-shapes-reference.md`;
  the canon **schema** (concrete field definitions) is the remaining follow-on.
