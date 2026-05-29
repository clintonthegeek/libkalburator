# Architectural-redress campaign — STATUS

**Last updated:** 2026-05-29
**Branch:** TBD per plan (campaign docs live on `main`; each plan opens its own
`feature/redress-N-<slug>` branch)
**State:** Campaign opened 2026-05-29 from audit dated 2026-05-28. Plans drafted. No
code work has started.

## Next action

**Open Plan 1** (`plans/plan-1-syncengine-decomposition.md`). Create branch
`feature/redress-1-syncengine`. Read INVARIANTS.md once. Begin with Task 1.

## Plan sequence and dependencies

The order matters; do not parallelize without re-reading the dependency notes in each
plan's header. Plans 1 and 2 come first because everything downstream touches code they
restructure. Plan 8 comes near the end because renames through code that's about to move
waste motion. Plan 9 is last because some "dead" symbols only become truly dead after the
preceding refactors.

| # | Plan | Audit refs | State | Depends on |
|---|---|---|---|---|
| 1 | `plan-1-syncengine-decomposition.md` | B1 | drafted | — |
| 2 | `plan-2-sync-domain-cycle-break.md` | B4 | drafted | — (parallel with 1 possible but discouraged) |
| 3 | `plan-3-types-layer-purification.md` | B2 | drafted | 2 (so domain-typed interfaces have homes) |
| 4 | `plan-4-remote-and-local-backend-decomposition.md` | B3 | architectural; tasks pending Plan 2 land | 2, 3 |
| 5 | `plan-5-backend-dirs-consolidation.md` | B5 | architectural | 2 |
| 6 | `plan-6-shape-decoupling.md` | B6 | architectural | 3 (BackendRecord home decided) |
| 7 | `plan-7-calendar-manager-split.md` | B7, B8 | architectural | 4 (backend decomposition done) |
| 8 | `plan-8-vocabulary-cleanup.md` | U1–U5 | architectural | 1–7 (rename what survives) |
| 9 | `plan-9-dead-code-removal.md` | B9 | drafted | 1–8 |

Per invariant P1, plans 4–8 have architectural plan + first-task detail today; their
subsequent tasks are written after their predecessors land.

## Locked decisions

Add an entry here when a plan resolves an ambiguity that would otherwise re-litigate in
later work. Cite the invariant or audit finding the decision serves. Format:

> **YYYY-MM-DD — Decision.** Rationale. (Refs)

- **2026-05-28 — Campaign sits at `docs/campaign/architectural-redress/`, not at top of
  `docs/campaign/`.** The canon-upgrade campaign files at `docs/campaign/{INVARIANTS,
  STATUS, FINDINGS}.md` are preserved as historical record; this campaign nests one
  level deeper. Future campaigns follow the same pattern. (House-keeping.)
- **2026-05-28 — `SyncBackend` base moves to `sync/`, not stays in `calendar/`.** The
  audit's B4 finding shows `sync/` already has `syncbackendbase.h`; calendar/ has its
  own competing base. One base, in the layer that owns the contract. (B4, invariant 1.)
- **2026-05-28 — New layer split is `types/` + `models/` + `services/`, not `common/`.**
  Three named layers are more honest than one bucket; `types/` becomes truthful to its
  name. (B2, invariant 2.)
- **2026-05-28 — Generalization opportunities (the `*Plugin` templating, `*CanonProperties`
  macros, differ CRTP) are out of scope for this campaign.** Catalogued in AUDIT for
  memory; revisit only after structural redress lands. (Invariant 8.)
- **2026-05-28 — The `[[deprecated]]` baseline v2 surface in `storage/baselinestore.h` is
  out of scope for Plan 9.** It is intentional migration scaffolding for PlanStan/
  WildPalms; do not delete. (Invariant 10.)

## Acceptance gates

These are checked at the end of every plan, not just at campaign close:

- libkalburator `ctest` baseline stays green.
- PlanStan `ctest` baseline stays green (when the changed surface is reachable from
  PlanStan). The downstream port (prior campaign's FINDING O7/O12) is in-flight; assume
  PlanStan can consume any public header touched here.
- WildPalms' five invariants (see INVARIANTS §10) hold.
- `compile_commands.json` is regenerated and clangd has no new diagnostics on touched
  translation units.

## Out of scope (explicit)

These appear in the AUDIT but are **not** part of this campaign:

- Plugin template factory / domain plugin CRTP (AUDIT "Generalization Opportunities" §1).
- `*CanonProperties` builder macro (AUDIT §2).
- Differ template/CRTP (AUDIT §3 — explicitly noted by audit as NOT a copy-paste smell).
- Backend contribution registration macro (AUDIT §4).
- Spine declaration unification (AUDIT §5).
- Deprecated baseline v2 surface (held for downstream migration).
- Any change to the DI/no-singleton discipline, per-domain plugin symmetry, or shape
  graph as sole transformation mechanism (see AUDIT "What not to touch").

## Findings discipline

`FINDINGS.md` is the discipline log per invariant 9. Append one line per smell observed
in code you pass through: `file:line`, invariant number, one phrase. Findings discovered
during a plan must be in the same commit that closed the plan; do not let the log lag.

## Definition of done (campaign)

- All nine plans landed and merged to `main`.
- AUDIT findings B1–B9 and U1–U5 each have a referenced commit that resolved them or a
  `FINDINGS.md` entry explaining why they were closed differently.
- INVARIANTS 1–10 hold on `main` as verified by re-reading the relevant src/ trees.
- The follow-up doc `docs/2026-XX-XX-architectural-redress-retrospective.md` is written
  before this campaign's `STATUS.md` is closed out.
