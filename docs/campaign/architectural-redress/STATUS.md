# Architectural-redress campaign — STATUS

**Last updated:** 2026-05-29 (audit rebaselined; Plans 1, 2, 3 & 4 landed)
**Branch:** Campaign docs live on `main`. Plans 1–4 landed; each subsequent plan opens its own
`feature/redress-N-<slug>` branch.
**State:** **Audit rebaselined; Plans 1 (SyncEngine decomposition), 2 (CalendarManager safety
net), 3 (neutralize the calendar-typed sync core) and 4 (correctness/ownership sweep) landed.**
The original 2026-05-28 audit and its nine drafted plans rested on material factual errors (see
`AUDIT.md` provenance) and are **archived under `archive/`**. The new `AUDIT.md` is a verified
rebuild (139-agent run, every finding adversarially checked against source). Plan 2 pinned three
real latent bugs (see FINDINGS). **Plan 3 resolved AUDIT CRITICALs #1–#3** (the calendar-typed sync
core) and the `engine`/`contacts`/`universal` → calendar include MAJORs — the orchestration layer no
longer names a calendar type. **Plan 4 resolved the five MAJOR correctness/ownership bugs** (mock
false-green, silent SQLite DELETE/DROP, RawFiles/GenericSqlite collection-hash race, `CardDavProvider`
raw-`bool*` UAF, `SyncEngine` raw-`QFutureInterface*` leak) + the folded `QPromise*` MODERATE; 133
tests green (ASAN/TSan-confirmed on the touched paths).

## Next action

Plans 1–4 are landed. **Next: detail Plan 5 — `types/` purification** (AUDIT B2-corrected: split
the pure value-type vocabulary from the behavioral offenders — JSON ser/deser, atomic `QSaveFile`
I/O, the QObject lock registry, crash-journal I/O — that `types/` accreted). Re-decide the target
shape (`types/` + `helpers/`/`services/`) when writing the plan, per the locked decision below;
the `types/→shape.h` co-bundle is NOT a violation. Use the `writing-plans` discipline (P1–P4).
Re-confirm the full sequence below as each plan lands.

## Plan 4 outcome (2026-05-29, landed on `feature/redress-4-correctness-ownership-sweep`)

Seven tasks, one commit each (T1–T7), full suite 133 green after every task. Resolved:

| Fix | AUDIT | Commit |
|---|---|---|
| `MockBlobBackend::loadRecordsOrError` reports injected `OnLoadRecords` failure | MAJOR (mock false-green) | P4.T1 |
| `GenericSqliteBackend::clear/deleteCollection` → `bool`, check DELETE/DROP | MAJOR (silent SQLite) | P4.T2 |
| `QMutex` guard on `RawFiles`/`GenericSqlite` collection hashes (deadlock-free) | MAJOR (thread-unsafe) | P4.T3 |
| `CardDavProvider` raw `bool*` → `std::make_shared<bool>` | MAJOR (UAF) | P4.T4 |
| `CardDavCapabilityDiscovery` raw `QPromise*` → `std::unique_ptr` | MODERATE (folded) | P4.T5 |
| `SyncEngine` raw `QFutureInterface*` → `std::unique_ptr` + documented dtor | MAJOR (leak) | P4.T6 |

Verification beyond ctest: T3 ran ThreadSanitizer (no races on the hashes); T6 ran ASAN (no
use-after-free / iface leak over the full sync lifecycle). New FINDINGS surfaced during the work
(eviction tradeoff, RawFiles sibling silent-failure, the mid-blocking-fetch teardown deadlock, the
stale clangd compile-DB) are logged in `FINDINGS.md`. The T3 plan code carried a latent lock-order
bug (mutex acquired before `threadDb()` which takes `m_connMutex`); the plan doc + code were both
corrected in the same commit (INVARIANTS §7).

## Parallel downstream fix — v0.61 (`9f8a220`), INTEGRATION PENDING

A provider-connect crash fix landed **outside** the campaign while Plan 4 was in flight (authored
by the PlanStan-side dev, 2026-05-29, tagged **v0.61**, on `origin/fix/provider-connect-idempotent`).
**It is the same ownership-bug family Plan 4 swept** but a distinct site Plan 4 did not cover.

- **Bug:** SIGSEGV at `providermanager.cpp:154` when a second `connect()` lands on an `IProvider`
  whose previous `connect()` future is still in flight (e.g. `ProviderManager::connectAll` racing
  `CalendarDiscoveryStep::startConnect` in the new-collection / Akonadi wizard). Overwriting
  `m_connectPromise` dropped the last ref to the old `QPromise`, whose dtor `cancel()`+`reportFinished()`s
  the underlying interface with no result → any `QFutureWatcher::result()` observer crashes in
  `QFutureInterface::resultReference`.
- **Fix (2 parts):** (1) all four async providers (Akonadi/CalDav/CardDav/MultiProtocolDav)
  `connect()` is now idempotent — returns the in-flight `m_connectPromise->future()` instead of
  overwriting; (2) `ProviderManager::connectAll`'s watcher short-circuits `result()` on a canceled
  future. `NeutralProvider` unchanged. Sync/provider suite 24/24.
- **Topology:** branched off `6579dfb` (v0.60 sqlite fix) — i.e. **behind `origin/main` (cd798b3,
  Plan 3); contains neither Plan 3 nor Plan 4.** It is one commit; `merge-base` with our `main` is
  `6579dfb`.
- **Integration:** **`git merge-tree main 9f8a220` is CLEAN (no conflicts).** The CardDav `connect()`
  idempotency guard (lands ~line 66, between `m_connected` and the `m_discovery` reset) does not
  overlap Plan 4 T4's `errorSeen`/discovery-watcher region (~line 81+); `providermanager.cpp`'s
  `connectAll` watcher edit does not overlap Plan 3's `registerProviderBackends`. **Action: merge
  `9f8a220` into the campaign `main` (clean) when the campaign `main` is next reconciled/pushed.**
  Relation to Plan 4: Plan 4 fixed the discovery's `QPromise` (T5) and `CardDavProvider`'s
  `errorSeen` `bool*` (T4) but NOT the provider `m_connectPromise` overwrite — this fix completes
  that corner. (Logged in `FINDINGS.md`.)

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
| 4 | Correctness/ownership sweep | MAJOR (raw ptrs, RawFiles thread-safety, silent SQLite/DELETE, mock false-greens) + folded QPromise* MODERATE | **DONE — feature/redress-4-correctness-ownership-sweep (7 tasks)** |
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
- **2026-05-29 — Plan 4 folded the same-file `QPromise*` MODERATE into the MAJOR `bool*` fix.**
  Fixing `CardDavProvider`'s raw `bool*` while leaving its collaborator
  `CardDavCapabilityDiscovery`'s raw `QPromise*` is a half-fix of one file family's ownership
  story. The PRAGMA-silent-failure MODERATEs (`SyncConflictStore`/`IDMappingStore`) stay deferred.
  (Deviation from INVARIANTS §8, documented per §"Scope and exceptions".)
- **2026-05-29 — Plan 4 thread-safety fix = add a `QMutex`** (vs. document-and-serialize). The
  `RawFiles`/`GenericSqlite` collection hashes are now guarded by a dedicated `m_collectionsMutex`
  (separate from GenericSqlite's `m_connMutex`), making the backends genuinely thread-safe rather
  than relying on a caller contract. (AUDIT MAJOR; the worker reads `shapeFor()` off-thread.)
- **2026-05-29 — `GenericSqliteBackend::deleteCollection` keeps best-effort eviction.** It removes
  the in-memory `m_collections` entry even on partial DB-cleanup failure (returning `false`), so
  the cache reflects delete intent. Behavior-preserving vs. the old `void` impl; the bool return
  signals the partial failure. (Plan 4 T2 review; named in `FINDINGS.md`.)

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
