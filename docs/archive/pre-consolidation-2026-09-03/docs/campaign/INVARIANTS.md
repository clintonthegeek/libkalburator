# Engineering invariants — canon-upgrade / convergence campaign

> This file exists because this campaign was **born from a discipline
> failure**: libkalburator grew two parallel record-transformation
> mechanisms (`src/shape/` and `src/transcoding/`) that never converged,
> and the cost of that divergence is the work we are now doing. The
> planning session that produced these docs reproduced the same class of
> failure twice more in miniature — caught, not avoided. These rules
> encode what was caught.
>
> They are **responsibilities you accept by working on this branch**, not
> suggestions. You follow them in your own work *and* you note when you
> see them violated in code you pass through — even off-topic from your
> task (invariant 9). A smell left unmarked is a vote for it being normal.

## Read once before your first non-trivial change

- `docs/2026-05-23-canon-upgrade-and-convergence-design.md` — the architecture
  (versioned spine, four-kind loss model, convergence, scope boundary §2).
- `docs/2026-05-23-canon-schema-design.md` — the canon field schema (JSON keyed
  by `PropertyId`; recurrence-as-text; provider-extras bag).
- `docs/2026-05-23-vendor-api-shapes-reference.md` — the durable vendor field
  tables. **This is the evidence; do not re-derive it from memory.**
- `docs/campaign/STATUS.md` — where we are, the plan sequence, the locked-decision
  ledger, and your next action.
- The current plan (`docs/2026-05-23-plan-1-shape-core-foundations.md`, then 2–4).

The failure pattern in one sentence: **a second mechanism is added beside the
shape graph instead of extending it; loss detail is flattened; an assumption
about "one canonical, two hops" is baked in; and the knowledge of why is left
in a chat log instead of a committed doc.**

---

## The invariants

### 1. One transformation mechanism — extend the shape graph, never fork it

All record transformation, loss accounting, and the lossy-sync warning surface
flow through `TransformationEdge` / `LossProfile` / `Pipeline`. This campaign
exists to retire the second mechanism (`src/transcoding/`); do not grow a third.

- New conversion behavior = a new peer encoding + edge(s) with an honest
  `LossProfile`. Capability differences between backends that share an encoding
  (the org-mode RRULE case) are modeled as *less-capable peer encodings*, not as
  backend-type-string routing.
- If you believe you genuinely need a parallel mechanism, your **spec** must name
  what it retires and your **plan** must delete that thing as a work-unit in the
  *same plan* — not a follow-up, not a queue item. (This is the rule whose
  absence created the campaign.)

### 2. The canon is a versioned *spine*, never a single hub

`TransformationRegistry` holds an ordered canonical spine per domain; the head is
the current canonical. Peer encodings attach to a spine node and the router walks
the spine. Never reintroduce a "single canonical / fixed two-hop" assumption in
routing, storage, or tests.

- A canon upgrade is **append-only**: a new `EncodingId` + a bridge edge pair
  (`v(k)→v(k+1)` lossless widen, `v(k+1)→v(k)` narrow). You **never rewrite an
  existing peer edge** to point at the new head. If a change would require editing
  peer edges to upgrade the canon, it is wrong — stop and reconsider.
- A single-node spine must reproduce prior behavior exactly. Any spine change
  must keep the existing `tst_transformation_registry` / `tst_pipeline` cases green.

### 3. Recurrence is opaque RFC5545 text — custody, not interpretation

The canon stores recurrence as verbatim RFC5545 (`recurrence`, the RRULE/RDATE/
EXDATE lines). No canon code parses it. Only the **one** edge that needs structure
(`canon → Microsoft patternedRecurrence`) parses it, and a parse failure there is a
**localized, classified edge loss**, never canon corruption.

- Do not re-serialize a user's finished recurrence by recomposing parsed parts.
  If a server emits malformed recurrence, that is the server's defect; keeping the
  finished form verbatim is how we avoid making it ours.
- The differ treats `recurrence` as one opaque field (changed/unchanged).

### 4. Classify every loss by kind — flat "dropped" is forbidden

`LossProfile::affected` maps each `PropertyId` to a `LossKind`:
**Dropped / Simplified / Reversible / Degraded** (design §6).

- **Reversible** (moved to an `X-`/extension property) keeps a round-trip lossless
  — never downgrade it to Dropped. This is what carries WildPalms `X-WP-PALM-*`
  identity and MS `X-MICROSOFT-CDO-BUSYSTATUS`.
- **Degraded** (lossy many-to-one vocabulary, e.g. IANA→Windows time zones) requires
  keeping the **original value verbatim** so it is recoverable.
- When you add an edge, classify its loss honestly. A wrong kind is a silent data
  contract violation, not a cosmetic label.

### 5. Test-first, and prove the test is falsifiable

When you generalize or refactor a seam (the spine router, the loss model, a
differ/merger, a transformation edge):

- Write the test **before** the production change and land the assertion logic first.
- **Prove it fails**: run it against the unmodified code (or a deliberately broken
  stub) and confirm red, before making it green. A test that was never seen to fail
  has not earned its protection.
- A refactor that *generalizes existing behavior* must (a) keep the existing tests
  green and (b) add a test that pins the new capability and was shown to fail before
  the change. (Plan 1's single-node-spine cases + the v1→v2 fixture are the template.)

### 6. Test the production path, not a synonym

A unit test that calls a function directly does not protect that function if
production reaches it through a different surface. Before declaring something
covered, confirm the test exercises the same callsite production uses. For the
sync engine, that means an engine-level/integration test (e.g. the `tests/engine/`
and `tests/calendar/` harnesses), not only a `src/shape/` unit test, when the
behavior is reached through the engine.

### 7. No load-bearing knowledge lives only in a chat log

Every decision, rationale, and piece of external research that the work depends on
must be in a **committed doc**. The vendor field tables live in the reference doc;
the decisions live in `STATUS.md`'s ledger; the rationale lives in the design docs.

- When you make a non-obvious decision, record it in the right doc in the **same
  commit**. When you rely on an external API shape, cite the reference doc — and if
  it's not there, add it before relying on it.
- "I'll remember it" / "it's in the context" is the failure mode this campaign was
  nearly derailed by. Do not reproduce it.

### 8. Respect the scope boundary — build the foundations, not the deferred machinery

Design §2 draws the line. **Build:** convergence, the rich canons, the versioned
spine, the four-kind loss model, per-engine registries. **Design + synthetic-test
only (do NOT implement live):** transparent auto-bridging *relied upon* for
unmodified third-party peers, backend capability-introspection objects, and
load-time canon-compatibility enforcement. Building the deferred machinery "while
we're here" is scope creep with no consumer — don't.

### 9. Notice and note — the Findings log

You are responsible for noticing violations of invariants 1–8 in code you pass
through, **even off-topic from your task.** Keep the note cheap so the obligation
is real:

> **`docs/campaign/FINDINGS.md` — "Discipline Log."** Append one line: `file:line`,
> the invariant number, one phrase of context. No fix required this session.

Walking past an unnamed smell is the only failure here the next agent cannot undo,
because they won't know to look.

### 10. Keep the external contracts green — PlanStan and WildPalms

These are non-negotiable acceptance gates the campaign must not break:

- **PlanStan ctest baseline stays green on every commit.** Org-mode is the only
  current `src/transcoding/` consumer; its RRULE simplification must survive the
  convergence as a `canon → org-ical` Simplified-loss edge.
- **Preserve the five WildPalms invariants** (design §11 / handoff §5):
  X-property round-trip stamping (Reversible loss), per-category virtual
  sub-collections untouched, *a* lossy-sync warning channel, `SyncMapping.lossPolicy`
  honored on the composed loss of the whole path, and diff stays shape-side
  (`createCanonicalDiffer()` per domain — do not regress this).

---

## Planning invariants (for agents writing Plans 2–4)

### P1. Write detailed tasks only against landed APIs

Do not write bite-sized, code-bearing tasks for a plan whose dependencies haven't
landed — signatures drift and the tasks rot. When a plan lands, update `STATUS.md`,
*then* write the next plan against the real signatures now in the tree. (This is
why Plans 2–4 are outlines, not full task lists, today.)

### P2. No placeholders

Every step contains the actual code/command an engineer runs. No "TBD", no "add
error handling", no "similar to Task N", no reference to a type/function not defined
in some task. (From the writing-plans skill; it is a plan *failure*, not a style nit.)

### P3. Each plan produces independently testable software

A plan must leave the tree building and green on its own. Decompose so that no plan
depends on a *future* plan to be coherent.

### P4. Carry-verbatim structures are designed before they are diffed

The non-isomorphic cases (event `RANGE=THISANDFUTURE`; todo hierarchy: VTODO tree /
Google parent / MS checklist) are **retained side-by-side, never normalized**
(schema doc §2, §4). A plan that touches these must state how the canon carries all
representations before it writes the differ/merger for them.

---

## Scope and exceptions

These rules scope to the canon-upgrade / convergence campaign and the
`src/shape/`, `src/calendar/`, `src/contacts/`, `src/todo/`, `src/transcoding/`,
and `src/engine/` code it touches. Invariants 5, 6, 7, and 9 apply to **all** work
on this branch regardless of subsystem.

To deliberately deviate from an invariant or a locked decision (STATUS ledger):
**write the reason in the spec/commit and cite the rule by number, then proceed.**
The rules exist to make deviations visible, not to forbid them. An *undocumented*
deviation is the failure mode; a documented one is engineering.

What discipline means here: when the spec contradicts the code, fix one. When you
see two sources of truth, name both and pick. When a test passes but you never saw
it fail on a broken stub, you have not yet earned its protection. It does **not**
mean gold-plating, paralysis, or scope creep — the Findings log is the pressure
valve: log the smell, finish your task, move on.

## Maintenance

When a task or session produces a new prescription:
1. Land the finding/post-mortem in `docs/campaign/FINDINGS.md`.
2. Add or amend an invariant here, citing the finding.
3. Update the one-paragraph summary in `CLAUDE.md` (the auto-read entry point).
4. If a new invariant retires an older one, mark it retired in place — do not delete
   it, so historic commit references stay interpretable.
