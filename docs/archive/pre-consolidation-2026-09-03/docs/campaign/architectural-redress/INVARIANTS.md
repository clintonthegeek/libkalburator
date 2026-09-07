# Engineering invariants — architectural-redress campaign

> This campaign exists because the canon-upgrade convergence (now complete) **changed the
> tissue but not the bones**. The shape graph won, `src/transcoding/` is gone, the domain
> spines are healthy — and *underneath* that, the layering grew leaks no one stopped to
> name. `types/` quietly became a behaviour layer. The orchestration layer (`sync/`) reaches
> down into concrete domain backends — a calendar-typed sync core that drags KCalendarCore
> through `sync/`. `SyncEngine` ate a Worker that was supposed to be separate. The audit at
> `AUDIT.md` (verified rebuild, 2026-05-29) is the evidence.
>
> The invariants below are how we keep the next round of churn from cementing the same
> shape. You accept them by working on this campaign — in your own code *and* by noting
> violations you walk past (invariant 9), even when off-topic.

## Read once before your first non-trivial change

- `AUDIT.md` — the fresh-eyes findings the campaign exists to redress. **Do not paraphrase
  it from memory.** When a plan and the audit disagree, the audit wins.
- `STATUS.md` — current state, plan sequence, locked decisions, and your next action.
- The current plan in `plans/`.

The failure pattern in one sentence: **a layer grows a responsibility it was never named
for, callers reach in without resistance because the cost is one line of `#include`, and
the next refactor inherits both the leak and the assumption that "this is normal."**

---

## The invariants

### 1. Layer direction is one-way — `types/` → `shape/` → domains → `engine/`

A lower layer never `#include`s an upper one. `types/` (and any new `models/`/`services/`
that emerge from Plan 3) is the foundation; `shape/` is the abstract transformation
layer; domain dirs (`calendar/`, `contacts/`, `todo/`, `note/`, `outline/`) layer on
shape; `engine/` and `sync/` orchestrate above the domains. **Breaking the calendar-typed
sync core (AUDIT CRITICALs 1–3) is the proof of work: `sync/` must traffic in neutral
interfaces, never a domain-typed backend; do not reintroduce the coupling.**

- If a layer "needs" something from above, the something is in the wrong layer. Move it
  down, not the include up.
- `sync/` does not `#include` any concrete domain backend header, and its registry/provider
  machinery stores the neutral `IBlobBackend` (or `sync/syncbackendbase.h`'s `SyncBackendBase`),
  never the calendar-typed `SyncBackend`. Concrete backends are produced behind the
  `IProvider`/plugin-contribution mechanism and never named in `sync/`.
- New cross-layer needs go through interfaces declared in the lower layer, not concrete
  types passed across.

### 2. `types/` is types — no I/O, no JSON, no behaviour heavier than helpers

A header in `types/` defines value types, simple enums, and small invariant-checking
helpers (e.g. `isValid()` that returns true when fields are well-formed). It does **not**:

- Open files, parse formats, persist anything, or hold locks.
- `#include <KCalendarCore/...>` or any other domain SDK. Domain-typed interfaces live in
  the relevant domain dir.
- Carry validation that depends on external context (config, capabilities, registries) —
  that is a service.

Plan 3 moves the existing offenders out. **Do not put new ones in.** If you find yourself
adding a setter that writes a file, you are not editing a type; stop and place it in
`services/` or the relevant domain dir.

### 3. One class, one reason to change — and the worker is not a class

`SyncEngine` swallowed its Worker without the surgery to actually unify them. Either a
class is one collaborator (private impl, anonymous-namespace helpers, no back-pointer) or
two collaborators that talk by signals — never one-and-a-half. The same rule applies to
the next refactor that's tempted to "fold" something in.

- A QObject that lives only as a private impl of another QObject must not be declared
  publicly in the consumer's header.
- A "fold" that leaves the folded class's public surface intact has folded nothing. Real
  folds delete the inner class's `Q_OBJECT`, public methods, and signals as part of the
  same plan, not a follow-up.
- Cross-class slot invocation through `QMetaObject::invokeMethod("slotName", ...)` is
  the symptom that the fold is incomplete or the separation is fake. Connect signals;
  don't `invokeMethod` by string.

### 4. Public surface answers a question — split the ones that answer five

A class with 25+ public methods has 25+ ways to misuse it. The two god classes
(`SyncEngine`, `RemoteCalendarBackend`) are the immediate targets, but the rule
generalizes: when a class's public methods sort into ≥3 distinct concerns, the audit
considers it god-class shaped and the plan must split it before extending it.

- Four overloads of the same verb (`runSyncFuture`) with different parameter packs are
  one method that needs a parameter struct, not four methods.
- Six getters that return parts of the same conceptual object (`discoveredUrl`,
  `discoveredColor`, `discoveredCtag`, ...) are one DTO accessor that didn't get written.
- A capability mixin whose only implementation forwards to another method on the same
  class (`collectionRevision` → `ctag`) is duplicated API; pick one and delete the other.

### 5. Vocabulary is precise — "Backend" means one thing per scope

The audit's U1/U2/U3 findings are not a style nit. When "Backend" means six things, every
new reader pays the cost of disambiguation, and every refactor risks moving the wrong
one. Plan 8 cleans the vocabulary; until it lands, **do not introduce new ambiguous
uses** — when you name a class, check whether your word already means something else in
the same scope.

- A new type called `FooBackend` must reuse the existing `SyncBackend` taxonomy, not
  start a sixth.
- "Store" persists. "Registry" looks up. "Manager" dispatches stateless commands. If
  your class does something else, it isn't one of these.

### 6. Test the production callsite, not its synonym

Inherited from the canon-upgrade campaign and reaffirmed here: a unit test that calls a
function directly does not protect that function if production reaches it through a
different surface. When you decompose a god class, the integration test through the
public entry point is the protective test — write it first.

- When extracting `CalDavCTagManager` from `RemoteCalendarBackend`, the protective test
  is at the engine/sync integration level, not a CTagManager unit test in isolation.
- A refactor that *generalizes existing behaviour* must (a) keep existing tests green
  and (b) add a test that pins the new seam and was shown to fail before the change.

### 7. No load-bearing knowledge lives only in a chat log

Every decision, rationale, and piece of external research the work depends on lives in a
**committed doc**. Locked decisions go in `STATUS.md`'s ledger in the same commit they're
made. Audit findings that arise during work go in `FINDINGS.md` (invariant 9). External
references go in the relevant design doc.

- When a plan turns out to be wrong, update the plan and the AUDIT in the same commit
  that abandons the wrong approach. The next agent reading this campaign should not have
  to reconstruct why an obvious-looking path was not taken.
- "I'll remember it" is the failure mode that nearly derailed the prior campaign. Do not
  reproduce it.

### 8. Respect the scope boundary — fix the named issues, don't tour the codebase

The campaign's scope is the nine plans in `plans/`. A finding outside that scope goes in
`FINDINGS.md` for a later campaign — it does not get a stealth fix in the middle of
unrelated work. "While I'm here" is how the next round of leaks arrives.

- Generalization opportunities (`*Plugin` templating, `*CanonProperties` macros, etc.)
  are **explicitly out of scope** for this campaign. They are in the AUDIT for memory;
  they get their own future campaign or none, after the structural redress lands.
- The `[[deprecated]]` baseline v2 surface is held for downstream migration and is **not**
  to be deleted as part of Plan 9.

### 9. Notice and note — the Findings log

You are responsible for noticing violations of invariants 1–8 in code you pass through,
**even off-topic from your task.** Keep the note cheap so the obligation is real:

> **`FINDINGS.md` — "Discipline Log."** Append one line: `file:line`, the invariant
> number, one phrase of context. No fix required this session.

Walking past an unnamed smell is the only failure here the next agent cannot undo, because
they won't know to look.

### 10. Keep the external contracts green — PlanStan and WildPalms

These are non-negotiable acceptance gates the campaign must not break:

- **PlanStan ctest baseline stays green on every commit** that touches libkalburator code
  PlanStan consumes. The downstream port (FINDING O7/O12 from the prior campaign) is
  in-flight and the contract surface is live.
- **WildPalms' five invariants survive** (X-property round-trip, per-category virtual
  sub-collections, lossy-sync warning channel, `SyncMapping.lossPolicy` honored,
  shape-side diff). The redress moves code; it does not change these contracts.
- When you change a public header, run the PlanStan and WildPalms test suites before
  committing if either is reasonably reachable from your local machine.

## Planning invariants (inherited from the prior campaign)

### P1. Write detailed tasks only against landed APIs

Plan N+1's detailed tasks are written **after Plan N lands**, against the real signatures
now in the tree. Plans 1 and 2 in this campaign have task-level detail today; Plans 3–9
have an architectural plan + first-task detail, with subsequent tasks deferred until
their prerequisites land.

### P2. No placeholders

Every step contains the actual code/command an engineer runs. No "TBD", no "add error
handling", no "similar to Task N", no reference to a type/function not defined in some
task.

### P3. Each plan produces independently testable software

A plan must leave the tree building and green on its own. Decompose so that no plan
depends on a *future* plan to be coherent.

### P4. Honor the dependency order

Plan 1 (SyncEngine) and Plan 2 (cycle break) come first because everything downstream
touches code they restructure. Plan 8 (vocabulary) comes near the end because renames
through code that's about to move waste motion. Plan 9 (dead code) is last because some
"dead" symbols are only made truly dead by the preceding refactors.

## Scope and exceptions

These rules scope to the architectural-redress campaign and the layering, encapsulation,
and naming issues catalogued in `AUDIT.md`. Invariants 6, 7, 9, and 10 apply to **all**
work on the campaign branch regardless of subsystem.

To deliberately deviate from an invariant or a locked decision (STATUS ledger): **write
the reason in the spec/commit and cite the rule by number, then proceed.** The rules
exist to make deviations visible, not to forbid them. An *undocumented* deviation is the
failure mode; a documented one is engineering.
