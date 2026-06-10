# Architectural-redress campaign — STATUS

**Last updated:** 2026-06-10 (Plan 7 COMPLETE on its branch; baseline **145/145** —
+1 from the T1 write-paths pin)
**Branch:** Campaign docs live on `main`. Plans 1–6 landed; each subsequent plan opens its own
`feature/redress-N-<slug>` branch.
**State:** **Audit rebaselined; Plans 1–6 landed; WP-A through WP-D ALL COMPLETE (2026-06-10).**
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

**Next action = Plan 8** (PlanStan-first `backendById` neutralization +
`runSyncFuture` consumer wave — see "Plan 8 prep" below; it is a consumer wave, open
with a joint handoff doc). The LocalBackend half of AUDIT B3 is deferred to Plan 7b /
Plan 11 with a FINDINGS mirror-sketch.

## Plan 7 outcome (2026-06-10, branch `feature/redress-7-remotecalendarbackend-decomposition`)

Subtract-first decomposition of `RemoteCalendarBackend`; full metrics + bug list in
the plan file's Outcome section. Headlines:

- **Net −322 LOC** across the touched src files (3190 → 2868 incl. the new
  collaborator) — the explicit anti-Plan-1-bloat goal (Plan 1 grew +629). The −350
  gate missed by 28 LOC (estimate variance, documented in the plan; structural gates
  all met: QEventLoop 11→2, RCB-specific publics 16→9 all consumer-verified,
  stateful members 13→6).
- **Dead surface deleted** (all per-symbol grep-verified vs lib+PlanStan+WildPalms):
  `primeCtagCache` + the never-engaged 60s primed-ctag path, `discoveredCtag`,
  `currentEtags`, `runJobsSequentially`, `clearCachedContentForCalendar`,
  `CTagStore::clearAll`, `m_etags`, `m_itemUrls`, `m_configuredCollectionUrls`,
  `etagForItem` decl.
- **One collaborator extracted:** `CalDavContentCache` (self-contained SQLite payload
  cache; fixes a per-instance QSqlDatabase connection leak). CTagStore stays
  file-local. The rest consolidated into helpers — `davSyncRequest` (the AUDIT-named
  extraction), shared iCal codec, `noteItemWritten/Erased`, `awaitOperation`.
- **Discovery state unified:** 4 parallel maps → `QMap<QString, CalendarFacts>`
  (supplement S2's backend-internal half); ctag cluster privatized behind
  `Backend::ChangeDetection` (one public face per concept, inv 4).
- **Three latent bugs found & fixed by the refactor:** startSync 412-retry
  dangling-reference UB (captured-by-ref lambdas outliving their frame); `pushItems`
  trailing-null item left the operation unsettled forever; the content-cache
  connection leak.
- **Protective tests first (inv 6):** new `tst_remotecalendarbackend_writepaths`
  pins the startSync signal contract (PlanStan PROD caller) + calendar CRUD in the
  default lane (FakeCalDavServer grew MKCALENDAR/PROPPATCH/collection-DELETE);
  falsifiability demonstrated. Convergence + blob_view contracts preserved verbatim
  (files untouched). The live-Radicale lane ran green throughout.
- **Downstream gates (inv 10):** PlanStan `build-dev` consumes this working tree
  (`PLANSTAN_LIBKALBURATOR_SOURCE_DIR` override) — suite run against the branch:
  93/115 with failed-set = 21 Not-Run headless-GUI binaries +
  `tst_loader_empty_backends`, and that single real failure **A/B-verified
  pre-existing** (fails identically against libkalburator `main`) and subsequently
  **root-caused to PlanStan's own unpushed `203744a4`** ("allow account-less
  collections to load (O.5 guard removal)") — the commit removes exactly the guard
  the test pins; flagged to the PlanStan dev in the Plan 8 RFC, no libkalburator
  action needed. WildPalms: grep-verified per-symbol
  non-consumer of every changed/removed RemoteCalendarBackend symbol (its only
  reference is a comment), so the five invariants hold by construction; the
  96-commits-behind clone gate was not run (per plan).

All audit-follow-up work packages are complete:

| Work package | What | Status |
|---|---|---|
| **WP-A** (7 tasks) | Correctness/contract quick wins, calendarsOnly RFC, failure-contract RFC | ✅ DONE |
| **WP-B** | Doc/comment truth sweep | ✅ DONE |
| **WP-C** (6 tasks) | Dead-code deletion, retirement RFCs | ✅ DONE |
| **WP-D** (10 tasks) | Test-gap closure (conflict-policy, wipeCollection, convergence, TypeSupport unit tests, SyncEngine config API, discovery/ smoke, §4.3 dialog, cancel cache-integrity, QSKIP revival, Akonadi/Org lane) | ✅ DONE |

Baseline: **144/144** (was 137 at the start of the WP-A–D session; +7 new test executables).

**Plan 7 inputs are current** (re-surveyed in `AUDIT-2026-06-10-supplement.md` §S4 +
`2026-06-10-audit-follow-up-specs.md` §"Plan 7"): `remotecalendarbackend.cpp` 2718 LOC /
header 472 LOC, ~61 public methods, 8-concern inventory (credentials/URL; discovery & primed
replay; ctag change-detection; SQLite content cache; etag/item-URL bookkeeping; calendar CRUD;
operations API + IBlobBackend + QEventLoop boilerplate; legacy storeCalendars/startSync).
Constraints: preserve `tst_remotecalendarbackend_convergence` + blob_view contracts verbatim.

**A fresh agent must write
`docs/campaign/architectural-redress/plans/plan-7-remotecalendarbackend-decomposition.md`
BEFORE implementing** (INVARIANTS §11: next-plan-only detail). Read INVARIANTS → supplement
S4 → specs §"Plan 7" → FINDINGS B3 / AUDIT B3 for the full concern inventory.

**Plan 8** is resequenced PlanStan-first by consumer data: WildPalms has 0 `backendById`
lookups (2 shim overrides); PlanStan has ~20 call/override sites and is the real migration.
Order: (1) make `ISyncHost::backendById`/`backends()` non-pure with BackendRegistry-backed
defaults; (2) RFC PlanStan migrates ~20 sites + WildPalms deletes its 2 shims; (3)
`runSyncFuture` deletion requires a PlanStan migration wave (2 PROD callers).

Same-day fixes landed 2026-06-10 (all on `main`, part of the baseline 137 before WP-A–D):
the `calendarsOnly` ctor-argument swallow (`b47d75e`, audit C1), the §4.4 dialog-test
contract realignment (`d1e924b`), and the v0.67 consumer wave (below).

Plan 6 record (2026-06-06, kept for reference): merged `806392c`; downstream gates passed
— PlanStan 98/118 failed-set identical to baseline (20 headless GUI tests); WildPalms temp
clone 116/120 vs 117/120 with the single delta A/B-verified pre-existing
(`tst_palm_mass_delete_guard_e2e`, FINDINGS).

**Plan 5 close-out state:** libkalburator side MERGED to `main` and pushed (`7d8a4ef`,
2026-05-31; merged-tree ctest 133/133). Phase 2 downstream relinks are **done and verified green
locally** but **held unpushed** on clinton-desktop: libkalcal `b4ef4ae0`, PlanStan `69e7df90`,
WildPalms `3afc074` (PlanEngine needed no change — it gets `Kalburator::TypeSupport` transitively
via `KalCal::Core`). The gating libkalburator **tag `v0.62` was cut 2026-06-03** (annotated
`b6483ca` → `8a35e54`, pushed). **Remaining manual step (user):** bump the downstream pins to
`v0.62`+ and push the libkalcal/WildPalms relink commits; PlanStan bumps its pin in its own
master wave. The WildPalms v0.65 pin bump is also outstanding. **Do these pushes before Plan 6
merges** so the invariant-10 downstream gates run against current pins. See
[[plan5-typesupport-pending-integration]] and [[caldav-primer-coordination-v063]] in auto-memory.

## Out-of-campaign consumer releases reconciled (2026-06-06)

Three consumer-driven releases landed on `main` between Plan 5 and Plan 6 (none are redress
plans; all reconciled against the campaign invariants on 2026-06-06):

- **v0.63 (2026-06-03)** — RemoteCalendarBackend convergence: CalDAV discovery primer,
  content-cache FNV determinism, `setCacheDir`. **Grows Plan 7's target:**
  `remotecalendarbackend.cpp` is now 2718 LOC / header 472 (audit B3 said 2649/427), and the new
  primer surface expands the discovery-state cluster (the 7 `discoveredX` getters + URL-map
  triplication MODERATEs). AUDIT B3 figures annotated in the same commit; **Plan 7 must be
  planned against the current tree.**
- **v0.64 (2026-06-04)** — LastWriteWins tie-bias fix. New `src/engine/lastwritewins.h` shared
  comparator — verified engine-internal (only `syncengine.cpp` includes it; no layering leak).
- **v0.65 (2026-06-06)** — clobber sync (WildPalms RFC): `ExecutionOverride::clobber` in
  `types/synctypes.h` (pure value flag — **`types/` purity gate verified intact**) +
  `IBlobBackend::wipeCollection` (added to the **neutral** interface — consistent with Plan 3).
  `syncengine.cpp` regrew 2846 → 2915 LOC (logged in FINDINGS as an inv-4 watch item).

Baseline after the three releases: **136/136 ctest green** (was 133 at Plan 5 close; +3 from the
new v0.63–v0.65 tests). Verified 2026-06-06 on a clean rebuild.

## Out-of-campaign consumer releases reconciled, second wave (2026-06-10)

- **v0.66 (2026-06-08)** — dispatch backends fetched from BackendRegistry as neutral
  `SyncBackendBase*` (WildPalms RFC); +1 test (`tst_engine_baseonly_backend`) → baseline 137.
- **v0.66-provider-dialog-polish + calendarsOnly (2026-06-08/09)** — ProviderConfigDialog
  §4.3/§4.4 + provider-side calendarsOnly mode. Two defects discovered by the 2026-06-10
  audit, both fixed same day: the mode never engaged (ctor-argument swallow, `b47d75e`) and
  the §4.4 polish landed with a red pre-polish dialog test (`d1e924b`).
- **v0.67 (2026-06-10)** — two consumer fixes merged: `CollectionInfo.contentTypes`
  populated by the DAV providers (WildPalms RFC `3474c30`; + CardDAV VCARD symmetry) and
  `ProviderManager::addProvider` registering backends for pre-connected providers
  (PlanStan, v0.61 precedent, `f170139`). Both TDD'd; suite 137/137.
- **Pin/push reality check (corrects the Plan-5 close-out claims above):** PlanStan relink
  `69e7df90` IS pushed and PlanStan pins `v0.66-provider-dialog-polish` (pushed); libkalcal
  `b4ef4ae0` IS pushed (the "unpushed" claim came from stale local tracking refs); WildPalms
  pin bumps v0.65→v0.66 are committed locally but **WildPalms origin/main is 96 commits
  behind** (origin still pins raw hash `948dce88`). The remaining manual push is
  WildPalms-side only.

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

## Plan 5 Phase 1 outcome (2026-05-30, on `feature/redress-5-types-purification`)

Five tasks, one commit each (T1–T5), purity gate + close-out (T6):

- **New light `Kalburator::TypeSupport` target** (`src/typesupport/`): depends only on
  `Kalburator::Types` + Qt — NOT the Sync engine — so light downstream foundation libs can link it
  without dragging in sync machinery.
- **Moved into `TypeSupport`:** `backendconfiguration` (T1), `crashjournal` (T2),
  `incidencelock_registry` (T3), `LogicalCalendarJson` codec (T4). All four were behavioral
  surfaces in `types/` that the AUDIT (B2) flagged.
- **Moved into `calendar/`:** `calendarmetadatamanager` (T5). Already belonged in the calendar
  layer; moving it clears the last file-I/O surface from `types/`.
- **`types/` passes the purity grep** (no `Q_OBJECT`, no `QJsonDocument`/`toJson`/`fromJson`, no
  `QSaveFile`/`QFile`/`QDir`/`QTextStream`). Redundant `QJsonObject`/`QJsonArray` includes removed
  from `logicalcalendar.h`.
- **133 ctest green after every task** (same baseline as Plans 1–4).
- **Branch UNMERGED** pending Phase 2 (libkalcal, PlanStan, PlanEngine relinks + WildPalms include
  updates) per INVARIANTS §10.

## Parallel downstream fix — v0.61 (`9f8a220`), INTEGRATED (merged `04a9876`, pushed)

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
- **Integration: DONE.** Merged `--no-ff` into `main` as `04a9876` (clean, as the pre-merge
  `merge-tree` predicted: the CardDav `connect()` idempotency guard at ~line 66 did not overlap Plan
  4 T4's `errorSeen` region at ~line 81+; the `connectAll` watcher edit did not overlap Plan 3's
  `registerProviderBackends`). Merged tree rebuilt + 133/133 ctest green; `main` pushed to
  `origin/main` (was at Plan 3 `cd798b3` → now `04a9876`). Relation to Plan 4: Plan 4 fixed the
  discovery's `QPromise` (T5) and `CardDavProvider`'s `errorSeen` `bool*` (T4); this fix adds the
  provider `m_connectPromise` overwrite guard — the three together close the `sync/` provider-future
  ownership corner. (Logged in `FINDINGS.md`.)

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
| 5 | `types/` purification | B2 (MAJOR, corrected) | **DONE — merged to main (7d8a4ef), tag v0.62 cut; downstream relink pushes = user's manual step** |
| 6 | `shape/` decoupling (move `ConflictPolicy` down) | B6 (MAJOR, corrected) | **DONE — merged to main `806392c` 2026-06-06 (resolved by narrowing, see Locked decisions)** |
| 6.5 | Audit follow-up WP-A…WP-D (correctness, doc truth, dead code, test gaps) | 2026-06-10 supplement | **in progress** — WP-A DONE, WP-B current; specs at `2026-06-10-audit-follow-up-specs.md` |
| 7 | Remote/Local backend decomposition | B3 (MAJOR) + supplement S4 | **DONE (RCB half) 2026-06-10** — net −322 LOC, 3 latent bugs fixed, ctag surface privatized; LocalBackend half deferred to 7b/11 (FINDINGS sketch) |
| 8 | `CalendarManager` split + `IncidenceDiff`→free fns + `backendById` neutralization | B7/B8 + supplement | proposed (after 2, 7; **resequenced PlanStan-first** — see specs §Plan 8 prep: WildPalms has 0 lookup sites, PlanStan ~20; `runSyncFuture` has 2 WildPalms prod callers) |
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
- **2026-05-30 — Plan 5 target shape = a NEW light `kalburator-typesupport` target (Types + Qt, no Sync), NOT distribute-to-domains.** The distribute-to-domains shape (first draft, reset at `6010ee2`) broke deliberately-light Types-only downstream consumers (libkalcal `KalCal::Core`/`Models`, PlanStan tests, PlanEngine) that reach into `types/` for behavior; relocating that behavior behind the heavy `Kalburator::Sync` target removes the symbols from them and cannot be shimmed at link time. A light helpers target reachable by Types-only consumers is what AUDIT B2's fix direction actually prescribed. (AUDIT B2; user decisions 2026-05-30; supersedes the earlier "distribute to domain homes" entry, which is withdrawn.)
- **2026-06-06 — Plan 6 fix shape = narrow `RecordMerger::merge()` to
  `Shape::AutoResolveStrategy`, NOT move `ConflictPolicy` down.** Verified evidence: all 9
  merger implementations read only `policy.autoResolve`; both production callsites
  (`syncengine.cpp:1665/:2529`) pass the constant `deferAll()`; zero downstream
  `RecordMerger` implementors or `merge()` callers exist (PlanStan/WildPalms grep, 2026-06-06);
  the audit-literal move would break the Plan 5 purity gate (the policy struct carries
  JSON + behavior + `ConflictRecord` coupling). The enum lands in `src/shape/`
  (NOT `types/` — `types/synctypes.h` already holds the rival `ConflictResolution` enum;
  a second near-synonym conflict enum there would deepen the U3-family collision Plan 10
  must untangle). `Kalburator::Conflict::AutoResolveStrategy` survives as an alias for
  source compatibility. (AUDIT B6, corrected from code; user decisions 2026-06-06.)

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
