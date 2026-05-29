# Architectural-redress campaign — STATUS

**Last updated:** 2026-05-29
**Branch:** Plan 1 landed on `feature/redress-1-syncengine`; campaign docs live on `main`.
Each subsequent plan opens its own `feature/redress-N-<slug>` branch.
**State:** Plan 1 complete (2026-05-29). 131/131 ctest pass, cancellation flake check 5/5
clean. Awaiting merge to main and Plan 2 open.

## Next action

**Merge `feature/redress-1-syncengine` to `main`** (user review), then **open Plan 2**
(`plans/plan-2-sync-domain-cycle-break.md`). Create branch `feature/redress-2-cycle-break`.
Read INVARIANTS.md once. Begin with Task 1.

## Plan 1 outcome (2026-05-29)

Six tasks completed across nine commits (T1 + qWait fix, T2 four-commit collapse + review
fixes + FINDINGS, T3 two-commit MappingQueue + FINDINGS, T4 unified commit incl. fix-up +
FINDINGS, T5 single commit). Final state:

| Metric | Before | After | Delta |
|---|---|---|---|
| `syncengine.h` LOC | 840 | 632 | −208 (−25%) |
| `syncengine.cpp` LOC | 2780 | 2846 | +66 (+2%) |
| `syncengine_p.h` LOC | — | 365 | new (worker private impl) |
| `mappingqueue.{h,cpp}` LOC | — | 250 + 92 | new (queue collaborator) |
| `syncrequest.h` LOC | — | 64 | new (canonical request) |
| **engine surface total** | 3620 | 4249 | +629 (richer structure, fewer responsibilities per file) |

Structural wins:
- `SyncEngineWorker` is private impl (`syncengine_p.h`), not publicly declared.
- Queue state lives in `MappingQueue` (one collaborator) instead of seven sprawled fields.
- `runSync(SyncRequest)` is the canonical entry; four overloads marked `[[deprecated]]`.
- `m_pendingOverride` no longer an implicit state machine — flows as parameter.
- Zero `QMetaObject::invokeMethod(m_*, "stringName", ...)` cross-class slot calls.

Deferred (per FINDINGS, all to Plan 8 or later):
- Forward decl `class SyncEngineWorker;` remains in `syncengine.h` (PIMPL deepening
  is a separate refactor).
- `m_baselineStoreAnchor` thread-anchor pattern (full ablation needs thread-safe
  `BaselineStore`).
- Dual `m_currentSingleIface` / `m_currentMultiIface` (collapses naturally when
  deprecated shims are deleted in Plan 8).
- `SyncRequest` cannot express "explicitly empty subset" (no canonical-API consumer
  needs it yet; revisit during Plan 8 migration).
- Two paper-cut overlap-rejection result shapes (pre-existing, surfaced in T4 review).

The cancellation contract from F2 Task 23 (`resultCount() == 1` with
`resultAt(0).cancelled == true` after `reportCanceled()`) is preserved across the
consolidation via the asymmetric routing (deprecated single-mapping shims bypass the
canonical `runSync(SyncRequest)` to keep the F2 Task 23 semantic intact — Qt6's
`QFuture::then()` drops continuations on canceled sources).

## Plan sequence and dependencies

The order matters; do not parallelize without re-reading the dependency notes in each
plan's header. Plans 1 and 2 come first because everything downstream touches code they
restructure. Plan 8 comes near the end because renames through code that's about to move
waste motion. Plan 9 is last because some "dead" symbols only become truly dead after the
preceding refactors.

| # | Plan | Audit refs | State | Depends on |
|---|---|---|---|---|
| 1 | `plan-1-syncengine-decomposition.md` | B1 | **complete (2026-05-29)** | — |
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
