# Architectural-redress campaign — STATUS

**Last updated:** 2026-05-29 (audit rebaselined)
**Branch:** `docs/audit-v2-rebaseline` for this re-baseline; each plan later opens its own
`feature/redress-N-<slug>` branch. Campaign docs live on `main`.
**State:** **Audit rebaselined.** The original 2026-05-28 audit and its nine drafted plans
were found to rest on material factual errors (see `AUDIT.md` provenance) and have been
**archived under `archive/`**. The new `AUDIT.md` is a verified rebuild (139-agent run,
every finding adversarially checked against source). The fix-plan sequence has **not** yet
been re-derived from it.

## Next action

**Re-derive the plan sequence** from the verified `AUDIT.md`. The root cause (the
calendar-typed sync core — two CRITICALs plus the cluster of cross-domain MAJORs it
generates) is the natural Plan 1; sequencing of the rest is open. Do this with the
`writing-plans` discipline (P1–P4 below) before opening any `feature/redress-N` branch.

## What changed in the rebaseline

- `AUDIT.md` ← verified rebuild (was the 2026-05-28 four-agent audit).
- `archive/AUDIT-2026-05-28-original.md` ← the superseded audit, preserved (invariant 7).
- `archive/plans-2026-05-28-original/` ← the nine drafted plans, preserved. **They are
  superseded, not authoritative.** Mine them for task ideas only after checking the new
  audit agrees.
- `archive/audit-v2-findings.json` ← the raw verified payload (80 survivors, 23 rejected)
  behind `AUDIT.md`.

## Prior code work to reconcile

- **Plan 1 (SyncEngine decomposition)** was completed on branch
  `feature/redress-1-syncengine` (closed at `6d77bdd`), **not merged to `main`**. The
  verified audit downgrades the old B1 from CRITICAL to MAJOR (worker completion is
  correctly wired via `Qt::QueuedConnection`; the prior `invokeMethod` claim was false) but
  confirms `SyncEngine` is still a god class. Decide during re-sequencing whether to merge
  that branch as-is, rebase it, or fold its goals into the new sequence.
- **Plan 2 (cycle break)** branch `feature/redress-2-cycle-break` exists but is empty
  (scaffolding only). Its premise — a `sync/↔calendar/` *cycle* — was refuted; the real
  defect is the one-way calendar-typed-core violation. Re-scope or delete the branch.

## Plan sequence and dependencies

**To be re-derived.** (The old table referenced the archived plans and the wrong B4; it is
intentionally removed rather than left to mislead.) When re-derived, restate the order and
each plan's audit refs + dependencies here.

## Locked decisions

Add an entry when a plan resolves an ambiguity that would otherwise re-litigate later. Cite
the invariant or audit finding. Format:

> **YYYY-MM-DD — Decision.** Rationale. (Refs)

- **2026-05-28 — Campaign sits at `docs/campaign/architectural-redress/`.** (House-keeping;
  still holds.)
- **2026-05-29 — Audit rebaselined; the 2026-05-28 audit + 9 plans are archived, not
  authoritative.** They contained factual errors that survived four-agent cross-validation;
  the verified rebuild added an adversarial gate. (See `AUDIT.md` provenance.)
- **2026-05-29 — REVISED: the SyncBackend ownership decision.** The old decision ("`SyncBackend`
  base moves to `sync/`; calendar/ has a competing base") rested on the wrong B4. Reality:
  the neutral base `SyncBackendBase` **already lives in `sync/`** and `calendar/SyncBackend`
  already inherits it. The real fix is to make `BackendRegistry`/`ProviderManager` traffic in
  the neutral `IBlobBackend`/`SyncBackendBase` and reparent the non-calendar backends off the
  calendar-typed `SyncBackend`. (Verified AUDIT, CRITICALs 1–3.)
- **2026-05-29 — UNDER REVIEW: the `types/` split.** The old "`types/` + `models/` +
  `services/`" idea was driven partly by the (corrected) claim that `types/→shape.h` is a
  violation; it is not (intentional CMake co-bundling). The *behavioral* offenders in `types/`
  (JSON, atomic I/O, lock registry) are still real — re-decide the target shape when the
  types-purification plan is written. (Verified AUDIT, B2-corrected.)
- **2026-05-29 — Generalization opportunities remain out of scope** (plugin templating,
  `*CanonProperties` macros). The verified audit confirms the differ polymorphism and plugin
  symmetry are *principled*, not copy-paste — do not "collapse" them. (Invariant 8; AUDIT
  "What not to touch".)
- **2026-05-29 — The `[[deprecated]]` baseline v2 surface stays.** Intentional migration
  scaffolding for PlanStan/WildPalms; not dead code. (AUDIT G8 / "What not to touch".)

## Acceptance gates

Checked at the end of every plan, not just at campaign close:

- libkalburator `ctest` baseline stays green.
- PlanStan `ctest` baseline stays green when the changed surface is reachable from PlanStan.
- WildPalms' five invariants (see INVARIANTS §10) hold.
- `compile_commands.json` regenerated; clangd has no new diagnostics on touched TUs.

## Out of scope (explicit)

- Plugin template factory / domain plugin templating.
- `*CanonProperties` builder macro.
- Differ polymorphism (verified-confirmed principled, not a copy-paste smell).
- Deprecated baseline v2 surface (held for downstream migration).
- Any change to the DI/no-singleton discipline, per-domain plugin symmetry, or shape graph as
  sole transformation mechanism (AUDIT "What not to touch").

## Findings discipline

`FINDINGS.md` is the discipline log per invariant 9. Append one line per smell observed in
code you pass through: `file:line`, invariant number, one phrase. Findings discovered during a
plan must land in the same commit that closed the plan.

## Definition of done (campaign)

- The re-derived plan sequence is fully landed and merged to `main`.
- Every CRITICAL and MAJOR in `AUDIT.md` has a referenced commit that resolved it, or a
  `FINDINGS.md` entry explaining why it was closed differently.
- INVARIANTS 1–10 hold on `main` as verified by re-reading the relevant `src/` trees.
- A retrospective `docs/2026-XX-XX-architectural-redress-retrospective.md` is written before
  this `STATUS.md` is closed out.
