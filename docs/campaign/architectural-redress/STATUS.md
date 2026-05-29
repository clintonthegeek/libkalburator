# Architectural-redress campaign — STATUS

**Last updated:** 2026-05-29 (audit rebaselined; Plans 1, 2 & 3 landed)
**Branch:** Campaign docs live on `main`. Plans 1–3 landed; each subsequent plan opens its own
`feature/redress-N-<slug>` branch.
**State:** **Audit rebaselined; Plans 1 (SyncEngine decomposition), 2 (CalendarManager safety
net) and 3 (neutralize the calendar-typed sync core) landed.** The original 2026-05-28 audit and
its nine drafted plans rested on material factual errors (see `AUDIT.md` provenance) and are
**archived under `archive/`**. The new `AUDIT.md` is a verified rebuild (139-agent run, every
finding adversarially checked against source). Plan 2 pinned three real latent bugs (see
FINDINGS). **Plan 3 resolved AUDIT CRITICALs #1–#3** (the calendar-typed sync core) and the
`engine`/`contacts`/`universal` → calendar include MAJORs — the orchestration layer no longer
names a calendar type; 133 tests green incl. a proof-of-neutrality test.

## Next action

Plans 1–3 are landed; **Plan 4 is now detailed** at
`plans/plan-4-correctness-ownership-sweep.md` (7 tasks). **Next: execute Plan 4** on branch
`feature/redress-4-correctness-ownership-sweep` — fix the five MAJOR correctness/ownership bugs
(`MockBlobBackend` false-greens, silent SQLite DELETE/DROP, RawFiles/GenericSqlite collection-hash
race, raw `bool*` UAF in `CardDavProvider`, raw `QFutureInterface*` leak in `SyncEngine`) plus the
folded same-file `QPromise*` MODERATE. Two scope decisions were locked while detailing (see ledger:
folded the `QPromise*` MODERATE per a documented INVARIANTS §8 deviation; thread-safety fix = add a
`QMutex`). Re-confirm the full sequence below as each plan lands.

## What changed in the rebaseline

- `AUDIT.md` ← verified rebuild (was the 2026-05-28 four-agent audit).
- `archive/AUDIT-2026-05-28-original.md` ← the superseded audit, preserved (invariant 7).
- `archive/plans-2026-05-28-original/` ← the nine drafted plans, preserved. **Superseded, not
  authoritative.** Mine for task ideas only after checking the new audit agrees.
- `archive/audit-v2-findings.json` ← the raw verified payload (80 survivors, 23 rejected).

## Prior code work reconciled

- **Plan 1 (SyncEngine decomposition) — MERGED to `main` 2026-05-29** (verified green on merge:
  incremental build clean, ctest 131/131). Under the verified audit this resolves most of the
  corrected B1 (now MAJOR); `SyncEngine` remains a god class, tracked for the later
  decomposition plan. Outcome detail below.
- **Plan 2 (CalendarManager safety net) — MERGED to `main` 2026-05-29** (`feature/redress-2-calendarmanager-tests`,
  17 tests, verified green, code-reviewed). Test-only; pinned three latent bugs (above) without
  touching production code.
- **`feature/redress-2-cycle-break`** (original empty scaffolding, premise refuted) was
  **deleted** 2026-05-29.

## Plan 1 outcome (2026-05-29, merged)

Six tasks completed across nine commits (T1 + qWait fix, T2 four-commit collapse + review fixes
+ FINDINGS, T3 two-commit MappingQueue + FINDINGS, T4 unified commit incl. fix-up + FINDINGS, T5
single commit). Final state:

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

Deferred (per FINDINGS, all to a later plan):
- Forward decl `class SyncEngineWorker;` remains in `syncengine.h` (PIMPL deepening is a
  separate refactor).
- `m_baselineStoreAnchor` thread-anchor pattern (full ablation needs thread-safe `BaselineStore`).
- Dual `m_currentSingleIface` / `m_currentMultiIface` (collapses naturally when deprecated shims
  are deleted in the vocabulary/cleanup plan).
- `SyncRequest` cannot express "explicitly empty subset" (no canonical-API consumer needs it
  yet).
- Two paper-cut overlap-rejection result shapes (pre-existing, surfaced in T4 review).

The F2 Task 23 cancellation contract (`resultCount() == 1` with `resultAt(0).cancelled == true`
after `reportCanceled()`) is preserved via asymmetric routing (deprecated single-mapping shims
bypass the canonical `runSync(SyncRequest)` because Qt6's `QFuture::then()` drops continuations
on canceled sources).

## Plan sequence and dependencies

**Being re-derived from the verified `AUDIT.md`.** Proposed backbone (sequencing open to
revision; only the next plan is detailed at any time, per invariant P1). Items map to AUDIT
severities, not the retired old plan numbers:

| # | Plan | AUDIT refs | State |
|---|---|---|---|
| 1 | SyncEngine decomposition | B1 (MAJOR, corrected) | **DONE — merged 2026-05-29** |
| 2 | `CalendarManager` safety net (protective tests) | CRITICAL #4 | **DONE — feature/redress-2-calendarmanager-tests (17 tests)** |
| 3 | Neutralize the calendar-typed sync core | CRITICAL #1–#3 + cross-domain MAJORs | **DONE — feature/redress-3-neutralize-sync-core (133 tests)** |
| 4 | Correctness/ownership sweep | MAJOR (raw ptrs, RawFiles thread-safety, silent SQLite/DELETE, mock false-greens) + folded QPromise* MODERATE | **detailed — ready to execute (`plans/plan-4-correctness-ownership-sweep.md`)** |
| 5 | `types/` purification | B2 (MAJOR, corrected) | proposed |
| 6 | `shape/` decoupling (move `ConflictPolicy` down) | B6 (MAJOR, corrected) | proposed |
| 7 | Remote/Local backend decomposition | B3 (MAJOR) | proposed (after 3) |
| 8 | `CalendarManager` split + `IncidenceDiff`→free fns | B7/B8 | proposed (after 2, 7) |
| 9 | Backend-adjacent dir consolidation + discovery placement | B5 + MODERATE | proposed |
| 10 | Vocabulary cleanup (Backend/Store/Manager/Canon) | U1–U5 | proposed (late — rename what survives) |
| 11 | Dead-code + test-gap closure | B9-corrected + test gaps | proposed (last) |

Per invariant P1, only the next plan carries task-level detail; later plans get detail once
their prerequisites land.

## Locked decisions

Add an entry when a plan resolves an ambiguity that would otherwise re-litigate later. Cite
the invariant or audit finding. Format:

> **YYYY-MM-DD — Decision.** Rationale. (Refs)

- **2026-05-28 — Campaign sits at `docs/campaign/architectural-redress/`.** (House-keeping;
  still holds.)
- **2026-05-29 — Audit rebaselined; the 2026-05-28 audit + 9 plans are archived, not
  authoritative.** They contained factual errors that survived four-agent cross-validation;
  the verified rebuild added an adversarial gate. (See `AUDIT.md` provenance.)
- **2026-05-29 — Plan 1 (SyncEngine decomposition) merged to `main`** after verified-green
  build + ctest 131/131. (Corrected B1.)
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
