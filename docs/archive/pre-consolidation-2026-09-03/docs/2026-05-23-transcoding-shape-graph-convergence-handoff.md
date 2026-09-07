# Handoff: Converging `src/transcoding/` into the Shape Graph (`src/shape/`)

**Date:** 2026-05-23
**From:** WildPalms team (downstream consumer)
**To:** libkalburator developers / the dedicated agent taking on the shape-graph
work (on a separate branch, in parallel with the canonical-domain-types upgrade)
**Status:** Request + findings. Non-prescriptive on the final design — the canonical
types are being reworked in the same campaign, so treat the "approaches" section as
input, not a spec.

---

## 1. TL;DR

libkalburator currently has **two parallel, non-referencing conversion subsystems**:

- **`src/shape/`** — the shape graph. Records are `(domain, encoding)` *shapes*;
  conversions are `TransformationEdge`s carrying a `LossProfile`, compiled into a
  `Pipeline` (`from → canonical → to`). Keyed by **shape**.
- **`src/transcoding/`** — the legacy property transcoder. Operates on live
  `KCalendarCore::Incidence` objects; `PropertyTranscoder`s are keyed by
  **backend-type string pair** (e.g. `"*" → "orgmode"`), gathered into a
  `TranscodingPlan` by `TranscodingRouter`, executed at write time.

They only touch at one seam: `RecordWriter::ApplyContext` carries a `TranscodingPlan`
field (`src/shape/recordwriter.h:45`), and `SyncEngineWorker` drives both at once.

**We want these to converge into one mechanism — the shape graph — so that every
domain's record transformation, loss accounting, and transcoding-warning surface
flows through `TransformationEdge`/`LossProfile`/`Pipeline`, and `src/transcoding/`
can be retired.** When that lands, WildPalms will port its conduits (calendar,
contacts, todo, memo) onto the unified pipeline and remove the per-backend
in-codec transformation logic it carries today.

This document explains the current state precisely (some of it is surprising), states
what WildPalms needs, names the one hard design tension, and lists the invariants we
need preserved so the eventual port is clean.

---

## 2. Current state — verified findings

These were verified by reading the code on 2026-05-23 (libkalburator `main`). File:line
references are from that point; the parallel canonical-types work will move things.

### 2.1 The engine runs three stages from two subsystems

In `SyncEngineWorker` (`src/engine/syncengine.cpp`), a unified-path sync does:

1. **Diff** — `m_unifiedDiffer = dd->createCanonicalDiffer()` (`:2083`). This is
   **shape-side for every domain** (calendar → `RecordDifferICal`, contacts →
   `RecordDifferVCard`). The comment marks it "Phase N.1 … Replaces the Phase Ia.5
   batch helper." So *diffing* already converged onto the shape graph. Good.
2. **Shape conversion** — `TransformationRegistry::compile(srcShape, canonical)` etc.
   (`:1885-1889`). Produces `Pipeline`s. **For calendar this is identity**
   (src = tgt = canonical = `(calendar, ical)`), i.e. converts nothing. For contacts
   it is the real `PalmToVCardStage` / `VCardToPalmStage`.
3. **Write-time transcoding** — `m_router.plan(srcBackend->backendType(),
   tgtBackend->backendType())` (`:2434-2440`), injected as `ctx.transcodingPlan`
   (`:2481`). The comment is explicit:
   > "Compute backend-level TranscodingPlans for **calendar domain writes**.
   > Non-calendar writers ignore the plan; CalendarPluginWriter uses it to drive
   > property transcoding (transcodingWarning is emitted from here)."

So the diff is unified, the byte-conversion is shape-graph, but a **third, parallel
property-transcoding step keyed by backend type** sits on top for the calendar domain.

### 2.2 The transcoding subsystem is, in practice, an **orgmode** facility

The registry registers exactly **one** default transcoder family
(`src/transcoding/transcodingregistry.cpp:28`): `RRuleTranscoder` /
`RRuleReverseTranscoder`. Their routing (`rruletranscoder.h:30-31, 63-64`):

- `RRuleTranscoder`: `sourceBackendType="*"`, `targetBackendType="orgmode"`, **Lossy**
  (simplifies complex RRULEs org-mode can't represent).
- `RRuleReverseTranscoder`: `orgmode → "*"`, Lossless.

`src/transcoding/` **never references `palm`** (grep-verified). The backend types that
appear in the subsystem are `caldav`, `carddav`, `local`, `orgmode`. Consequence:

> **For WildPalms's actual backend pairs (`palm-calendar ↔ caldav`/`local`), the
> `TranscodingPlan` is always empty. The transcoding subsystem is inert for us today.**

This means our earlier internal characterization ("calendar rides the legacy
transcoder") was half-right: the engine *wires* the legacy path in for calendar, but
the only thing it actually does is org-mode RRULE simplification — a PlanStan concern.

### 2.3 Where calendar's *real* transformation actually lives

WildPalms's Palm appointment ↔ iCalendar VEVENT conversion does **not** happen in
either libkalburator subsystem. It happens **inside the WildPalms backend**
(`DatebookCodec` / `icstranscoder` in the calendar plugin). That is exactly the same
shape as todo and memo: transform-in-backend, present record to the engine as canonical
bytes. So the honest domain table is:

| Domain | Diff | Real record transformation | In the shape graph? |
|---|---|---|---|
| **Contacts** | shape `RecordDifferVCard` | shape-graph stage (`PalmToVCardStage`) | ✅ the only one |
| **Calendar** | shape `RecordDifferICal` | in WP backend (`DatebookCodec`); legacy transcoder inert for palm | ❌ pipeline is identity |
| **ToDo** | blob hash | in WP backend (`todoicstranscoder`) | ❌ `(blob, raw)` |
| **Memo** | blob hash | in WP backend (`memomarkdown`) | ❌ `(blob, raw)` |

**Contacts is the only domain whose transformation flows through the shape graph.**
The machinery the library was built for (lossy/lossless edges, composed `LossProfile`)
is exercised by exactly one domain.

### 2.4 Known hazards

- `TranscodingRegistry` is a **process-wide singleton** (`transcodingregistry.h:47`).
  Tests must `clear()` it in `cleanup()` or transcoders leak across tests. This is
  flagged in the engine-merger `FINDINGS.md` with the note that "Phase G design should
  consider de-singletonising." That de-singletonising was step one of this convergence
  and stalled. The shape registries (`TransformationRegistry`, `DomainRegistry`) are
  also singletons (`*::instance()`), so convergence should decide the lifetime story
  for all of them together rather than perpetuating it.
- `TranscodingRouter`'s gate is literally `sourceType != targetType`; "capability
  objects are not consulted. Capability-aware routing is deferred to Phase F"
  (`transcodingrouter.h:17-19`). Phase F never happened. This is the crux — see §4.

---

## 3. What WildPalms needs / intends

**Intent:** after this convergence lands (and the canonical-types upgrade alongside it),
WildPalms ports all four conduits onto the **single** shape-transformation pipeline:

- Each Palm DB declares a native shape: `(calendar, palm)`, `(contacts, palm)` (exists),
  `(todo, palm)`, `(memo, palm)` — **not** `(blob, raw)`.
- Each registers `TransformationEdge`s to its domain's canonical encoding, with an
  honest `LossProfile`, exactly as `contactsdomainextension.cpp` does today.
- The Palm↔canonical conversion logic WildPalms currently carries **inside its backends**
  (`DatebookCodec`, `todoicstranscoder`, `memomarkdown`) moves into shape-graph
  `TransformationStage`s, so the engine — not the backend — owns conversion.
- The org-mode RRULE transcoding currently in `src/transcoding/` becomes ordinary
  loss-annotated transformation step(s) in the calendar domain, reachable by PlanStan
  the same way every other transformation is.
- `src/transcoding/` is deleted; `CalendarPluginWriter`'s special-case apply path and
  the `ApplyContext.transcodingPlan` seam go away.

**We are deliberately NOT asking you to preserve the backend-type-keyed routing model.**
We think it's the wrong axis (see §4). We'd rather it be reconceived in shape/capability
terms during the canonical-types upgrade.

---

## 4. The one hard design tension — keying

This is the question we most want you to resolve, because it's why the two subsystems
never merged:

- The **shape graph keys on `(domain, encoding)`.** It deliberately knows nothing about
  *which backend* a record came from.
- **Property transcoding keys on backend-type pairs.** RRULE simplification is needed
  because *org-mode as a backend* can't represent complex recurrence — even though the
  encoding on both sides is "calendar/ical". Two backends can share an encoding and
  still differ in capability.

So calendar transcoding is conceptually a `(calendar, ical) → (calendar, ical)`
transformation — **same shape on both ends** — gated on *capability*, not encoding. The
shape graph has no edge to hang that on, because an edge from a shape to itself is the
identity. That mismatch is the whole reason `src/transcoding/` exists as a separate
thing.

Options we see (input, not a mandate — the canonical-types work may suggest better):

- **A. Capability-refined shapes / encoding variants.** Model capability tiers as
  distinct encodings within a domain, e.g. `(calendar, ical-full)` vs
  `(calendar, ical-basic-rrule)`. Backends declare the refined shape they actually
  support; the RRULE-simplification edge carries the `LossProfile`. Clean and fully
  inside the shape graph, but requires (a) capability introspection/declaration by
  backends — the "Phase F capability objects" that were deferred — and (b) the graph to
  handle several encodings per domain with edges between them.
- **B. Capability descriptor on the edge/pipeline.** Keep one canonical encoding, but
  let a `TransformationStage` consult a capability descriptor (a successor to
  `PropertyTranscoder`) supplied per-mapping. Less graph churn; risks smuggling the old
  backend-type keying back in under a new name.
- **C. Property-level loss as a first-class part of `LossProfile`.** Today `LossProfile`
  names *dropped* `PropertyId`s. `PropertyTranscoder` additionally has a
  `Reversible` fidelity (data preserved in `X-` properties) and emits human-readable
  warnings. Whichever of A/B you choose, the loss model needs to express
  "transformed-but-reversible-via-X-property" and "simplified-but-not-dropped", not just
  "dropped". WildPalms relies on exactly this (see §5, X-WP-PALM-* round-trips).

We lean toward **A** because it keeps everything in one mechanism and makes loss visible
at compile time, but we defer to what the canonical-types redesign makes natural.

---

## 5. Invariants WildPalms needs preserved

So our port doesn't regress, please keep these working (or give us an equivalent):

1. **Round-trip extension stamping.** WildPalms preserves Palm-only identity/state in
   `X-WP-PALM-*` properties (e.g. `RECORDID`, `CATEGORY-SLOT`, `SECRET`) so a
   palm→canonical→palm round-trip is lossless even when canonical→palm is lossy in
   general. This is the `Reversible` fidelity concept. The unified loss model must let a
   transformation declare "I moved this into an X- property" distinctly from "I dropped
   this." (See `contactsdomainextension.cpp` / `contactsvcardtranscoder.cpp` for the
   pattern we'd replicate for calendar/todo/memo.)
2. **Per-category virtual sub-collections.** WildPalms surfaces one collection per Palm
   category slot (`palm:todo/<slot>`, `palm:contact/<slot>`, etc.). Convergence is about
   the *transformation* layer; please don't entangle it with collection identity.
3. **`transcodingWarning` → UI.** WildPalms surfaces lossy-sync warnings to the user via
   the engine's `transcodingWarning` signal. After convergence we expect the equivalent
   to come from the pipeline's composed `LossProfile` (`Pipeline::composedLoss()` /
   `LossProfile::summary()`), surfaced at the same point in the sync lifecycle. We need
   *a* warning channel; it doesn't have to be the current signal.
4. **`WhenLossWouldOccur` policy still honored.** `SyncMapping.lossPolicy`
   (Abort/Warn/Proceed) must continue to gate on the composed loss of the whole path.
5. **Diff stays shape-side.** Don't regress the Phase N.1 win — `createCanonicalDiffer()`
   per domain is the right model; keep it.

---

## 6. What we hope to see (acceptance criteria, from WildPalms's vantage)

A convergence we could port onto cleanly would show:

- [ ] `src/transcoding/` deleted, or reduced to a thin shim with a deprecation note.
- [ ] The org-mode RRULE simplification re-expressed as a shape-graph transformation with
      a `LossProfile`, reachable by PlanStan with no backend-type-string routing.
- [ ] `RecordWriter::ApplyContext` no longer carries a `TranscodingPlan`; no
      `CalendarPluginWriter` special-casing in the engine.
- [ ] A single documented way for a backend to say "I speak `(domain, encoding[+capability])`"
      and register edges — calendar, contacts, todo, memo all use the identical pattern.
- [ ] A loss model that distinguishes dropped / reversible-via-X-property / simplified.
- [ ] Singleton lifetime story decided for the registries (per-engine or documented
      process-global), so tests don't need defensive `clear()` rituals.
- [ ] `docs/` note on how capability is declared/introspected (closing the deferred
      "Phase F capability objects" gap), since that's the keying decision in §4.

We do **not** need WildPalms wired up by you — we'll do that port. We need the mechanism
to exist and be singular.

---

## 7. Coordination notes

- **PlanStan is the other consumer** and the *only* current user of `src/transcoding/`
  (org-mode RRULE). Any change to the transcoding/loss model must keep PlanStan's
  org-mode sync working. Per existing project policy, every libkalburator commit must
  pass PlanStan's ctest baseline before landing — that policy covers this work too.
- **Parallel canonical-types upgrade.** This convergence and the canonical-domain-types
  rework are happening together on your branch. They're coupled: the keying decision
  (§4) and the loss model (§5.1) likely want to be designed against the *new* canonical
  types, not the current ones. Sequence as you see fit; we just need the end state to be
  one pipeline.
- **WildPalms timing.** WildPalms will not port until your branch is done and merged. We
  have our own open queue (typed routing for todo/memo, category-lifecycle conflict
  handling, etc.) that we're holding precisely because it should land *on top of* the
  converged pipeline, not before it. Ping us when the shape of the new API stabilizes and
  we'll review it against our four conduits early.

---

## 8. Appendix — artifact inventory (libkalburator `main`, 2026-05-23)

**Shape graph (`src/shape/`)** — keep & extend:
- `shape.h` — `Shape{DomainId, EncodingId}`, `Shape::Any()`
- `transformationedge.h` — `TransformationEdge{from, to, LossProfile, TransformationStage}`
- `transformationregistry.{h,cpp}` — `registerShape`, `registerEdge`, `compile`, `inspect`
- `pipeline.{h,cpp}` — composed edges, `apply(QByteArray)`, `composedLoss()`
- `lossprofile.{h,cpp}` — `LossLevel{Lossless, IntraDomainLossy, InterDomainProjection,
  Degenerate}` + dropped `PropertyId` set + `compose()` / `summary()`
- `domaindefinition.h` — `canonicalShape`, `createCanonicalDiffer/Merger`, `richnessRank`
- `recorddiffer.h` / `recordmerger.h` — per-property diff/3-way merge on canonical bytes
- `recordwriter.h` — `ApplyContext` (currently carries the `TranscodingPlan` seam to remove)

**Transcoding (`src/transcoding/`)** — target for absorption/retirement:
- `propertytranscoder.h` — abstract; keyed by `(propertyName, srcBackendType,
  tgtBackendType)` with `*` wildcards; `TranscodingFidelity{Lossless, Reversible, Lossy,
  Unsupported}`; operates on `KCalendarCore::Incidence::Ptr` (typed, not bytes)
- `transcodingregistry.{h,cpp}` — process-wide singleton; registers only
  `RRuleTranscoder`(+reverse) today
- `transcodingrouter.{h,cpp}` — per-engine; `plan(srcType, tgtType)`; gate is
  `srcType != tgtType`; capability-aware routing deferred (never built)
- `transcodingplan.{h,cpp}` — `TranscodingPlan{QList<PropertyTranscoder*>, routingDecision}`;
  `executeTranscodingPlan()` clones incidence, runs transcoders, accumulates warnings
- `rruletranscoder.{h,cpp}` — the only live transcoder; org-mode RRULE simplification
- `incidencediff.{h,cpp}`, `syncdiff.{h,cpp}`, `transcodingrouter`, `transcodingplan` —
  the legacy diff path; note diff has already moved to the shape side for the unified
  engine path, so check whether these are still on any live path before relying on them

**Engine seam (`src/engine/syncengine.cpp`)** — the two subsystems meet here:
- `:2083` `createCanonicalDiffer()` (shape diff)
- `:1885-1889` shape pipeline compilation
- `:2434-2481` `TranscodingPlan` computation + injection into `ApplyContext`

**Glossary mismatch to reconcile:**
| transcoding term | shape-graph analogue | gap |
|---|---|---|
| backend-type pair (`src→tgt`) | `(domain, encoding)` shape pair | capability axis (§4) |
| `TranscodingFidelity` | `LossProfile.level` | `Reversible`/`X-prop` not modeled (§5.1) |
| warning strings | `LossProfile.summary()` + dropped set | warning surface/lifecycle (§5.3) |
| `Incidence::Ptr` (typed) | `QByteArray` (opaque) | stage works on bytes; calendar stage must (de)serialize |

---

*This file lives in `libkalburator/docs/` (uncommitted) as the WildPalms→libkalburator
handoff. WildPalms-side context lives in its memory note
`project_typed_routing_and_abandoned_queue.md`.*
</content>
</invoke>
