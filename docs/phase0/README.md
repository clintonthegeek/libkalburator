# libkalburator — extraction progress overview

**Last updated:** 2026-04-21 (after Phase B3 — BlobBaselineStore landed).
**Maintainer:** Clinton (solo).
**Branch:** `main` (libkalburator) / `master` (PlanStan) — no upstream
remote; static, single-developer branches.

This document is the **first thing a new agent should read** when
landing in either `~/dev/libkalburator/` or `~/dev/PlanStan/` with
work that touches the extraction.

---

## 🛑 READ THIS FIRST — keeping this document current is mandatory

**This file is the single source of truth for extraction status.**
Every future agent starts here. If it lies about progress, work gets
redone, skipped, or built on false assumptions.

**You MUST update this file in the same commit that changes phase state.**
This is not optional and not deferrable to "later". Specifically:

1. **When a phase lands** — flip its row in the Phase map (done /
   queued / deferred), update the **Current status** section, bump the
   **Last updated** date at the top, and update **Next:** to point at
   whatever is now queued.
2. **When a phase is paused, blocked, or reverted** — say so. A phase
   row that reads "done" when the work is actually stuck is the most
   destructive form of drift. Prefer "paused — see `04X-blocker.md`"
   over silence.
3. **When a decision flips** — if a design choice recorded here (e.g.
   "Audit 2 → SQLite merged schema") changes, update the prose AND
   update or supersede the referenced design doc.
4. **When a loose end gets tied off** — move items out of
   **Unfinished / future work** into the Phase map or delete them.
   A "known debt" entry that was resolved three commits ago is noise.
5. **When you discover this file is wrong** — fix it before doing
   anything else. An agent reading stale status will make worse
   decisions than one reading no status at all.

**If you land a commit that changes phase state without updating this
file, you have failed the task.** The PlanStan CLAUDE.md rule applies:
"If you commit code that changes the state of a phased effort, the
commit must also update whatever doc describes that effort's status."
For libkalburator extraction work, that doc is this one.

Do not create a parallel status doc elsewhere. Update this one.

---

## What libkalburator is

A reusable Qt6/KF6 calendar-sync substrate, extracted from PlanStan's
`libs/sync/`. Two named consumers:

- **PlanStan** (`~/dev/PlanStan/`) — multi-calendar personal PM app.
  Currently consumes libkalburator in-tree via `add_subdirectory`.
  Every commit that lands on `master` is expected to keep libkalburator
  building.
- **Wild Palms** (`~/dev/WildPalms/`) — Palm OS sync app. Not yet a
  consumer; adoption planned for Phase 4 via the two-mode split
  (Client Mode + Full Sync Mode) documented in
  `~/dev/PlanStan/docs/proposals/2026-04-20-sync-library-extraction.md`.

The library carries both PlanStan-lineage sync infrastructure
(SyncBackend, SyncCoordinator, CalendarJournal, transcoding,
IDMappingStore) and Wild-Palms-lifted "qsynccore" conflict machinery
(ConflictPolicy, ConflictRecord, ConflictStore, BaselineRecord).

---

## Current status (2026-04-21)

**Done:**

- Phase 0 (alignment + repo setup).
- Phase B partial (WP qsynccore lifted upstream).
- Phase 1–3 (PlanStan consumes libkalburator in-tree).
- Phase C.1 (qsynccore namespaced).
- **Phase C.2a** — option-2 layering. PlanStan's duplicate shared-type
  headers deleted; libkalburator is the single source via
  `Kalburator::Types` + `Kalburator::Sync` targets.
- **Phase C.2b** — namespace migration. Everything in libkalburator
  lives under `Kalburator::Sync::*` (qsynccore files in
  `Kalburator::Sync::QSyncCore` sub-namespace to resolve
  ConflictResolution/SyncStats/SyncResult collisions). PlanStan
  consumers carry TU-scope `using namespace Kalburator::Sync;`.
- **Phase C.3** — directory layering.
  `src/sync/` → `src/{calendar,conflict,transcoding,journal,discovery,blob}/`
  per `05-repo-strategy.md`. All six subdirs exposed as PUBLIC include
  paths so consumer code still uses bare `#include "foo.h"`.
- **Phase C.4** — SQLite `IDMappingStore`. New store at
  `src/journal/idmappingstore.{h,cpp}` (Kalburator::Sync top-level)
  replaces the dormant WP JSON version. Shares `.planstan-sync.db`
  with SyncStore; extends `sync_id_mappings` in place via idempotent
  ALTER TABLE ADD COLUMN for the four Audit-2 WP fields (`last_synced`,
  `source_category`, `target_categories`, `archived`). Adds
  `recurrenceId` to the API (Audit 2 oversight; needed because PS's
  PK includes `recurrence_id` for iCal exceptions). 12 new tests in
  PlanStan's `tests/sync/tst_idmappingstore.cpp`, including a
  characterization test for the pre-C.5 INSERT-OR-REPLACE hazard.
- **Phase C.5** — SyncStore identity-mapping dissolve. Removed
  `setIdMapping` / `sourceUidForTargetId` / `targetIdForSourceUid` /
  `allIdMappings` / `removeIdMapping` / `clearIdMappings` from
  `SyncStore`; removed sync_id_mappings CREATE + INDEX from
  `createTables`; narrowed `clearBackendData` to version hashes +
  conflicts only. `IDMappingStore` is now the sole owner of the
  `sync_id_mappings` table. Scope correction from original plan: no
  PlanStan call-site migration needed — grep confirmed zero production
  callers of the removed methods. Tests deleted (6 identity tests in
  `tst_syncstore.cpp`) and rewritten (5 tests that had used identity
  APIs as fixtures: `testDatabaseReopen`, `testVacuum`,
  `testClearBackendData`, `testEmptyStrings`, `testSpecialCharacters`);
  `test_coexistence_with_syncstore` in `tst_idmappingstore.cpp`
  deleted (hazard it characterized no longer exists).
- **Phase C.6** — `v0.5-phase-c` tag on libkalburator's `main`.
  Marks the end of Phase C and the first named release of the
  library. All Phase-C scope delivered: layered directory structure,
  full `Kalburator::Sync::*` namespacing, SQLite `IDMappingStore`,
  single-owner `sync_id_mappings`. Lightweight endpoint — no
  CMake version bump or install-target yet (those are Phase 4+
  concerns).
- **Phase B2** — net-new blob layer. First Wild-Palms-driven
  contribution. Landed `IBlobBackend` + `BackendRecord` +
  `CollectionInfo` + `BlobSyncEngine` (mirror + twoWayNaive) +
  `LocalBlobBackend` + `MockBlobBackend`. First library-side test
  tree at `tests/blob/` gated by `KALBURATOR_BUILD_TESTS`
  (ON for PROJECT_IS_TOP_LEVEL, OFF for add_subdirectory consumers).
  Calendar layer untouched; PlanStan ctest baseline preserved. Scope
  deliberately narrow — baseline store, conflict integration, and
  calendar-layer bridging all explicitly deferred. See
  `04h-blob-layer-design.md` §"Explicitly deferred" for the
  catalogue. Tag: `v0.6-phase-b2-blob-layer`.
- **Phase B3** — `BlobBaselineStore`. SQLite-backed hash-per-record
  baseline store at `src/journal/blobbaselinestore.{h,cpp}`
  (Kalburator::Sync top-level). Mirrors IDMappingStore's pattern:
  shares `.planstan-sync.db`, idempotent `CREATE TABLE IF NOT EXISTS`
  for new `blob_baselines` table + index, PRAGMA user_version = 3
  only on fresh DBs, per-instance connection with RAII cleanup. Ten
  internal tests in `tests/journal/tst_blobbaselinestore.cpp` covering
  CRUD, bulk commit atomicity, per-mapping filtering, clearMapping,
  coexistence with IDMappingStore on shared DB, and cross-reopen
  persistence. Enables correct 3-way diff for the future
  `twoWayWithBaseline` engine operation (Phase B4). Tag:
  `v0.7-phase-b3-baseline`.

**Next:** Phase B4 — `BlobSyncEngine::twoWayWithBaseline` + `ConflictStore`
integration + `BlobSyncEngine::registerConflictHandler`. Consumes B3's
`BlobBaselineStore`. Will close the remaining B2-deferred items needed
to unblock Wild Palms Phase E.3+ on the WP side.

**Baseline health:** libkalburator standalone build clean — 4/4 ctest
pass (3 blob tests + 1 journal test covering 10 internal slots).
PlanStan builds clean against B3. PlanStan ctest (2026-04-21 run on
WP-side machine): 81 pass / 6 actual fail / 18 not-run / 105 total.
Real failures (5 integration_* SEGFAULTs + sync_error_recovery) are
all pre-existing — documented in "Known debt" below; none touch
journal/ code. No new failures introduced by Phase B3.

---

## Phase map

| Phase | Status | Key doc | Notes |
|---|---|---|---|
| 0 — alignment, inventory, repo | done | `00-open-questions.md`, `01-inventory-planstan.md`, `02-inventory-wildpalms.md`, `05-repo-strategy.md` | All 10 Phase-0 questions resolved. |
| 0 — conflict engine audit | done | `03-conflict-engine-audit.md` | Split: generic conflict framework in lib, Palm-specific handler stays in WP. |
| 0 — merged interface sketch | done | `04-merged-interface-sketch.md` | ICalendarBackend, IBlobBackend, ICalendarHost drafts. |
| 0 — follow-ups (audits 1–4) | mostly done | `04a-followups.md` | Audit 1 & 4 closed; 2 & 3 rolled into Phase C. |
| Phase 1 — extraction | done | `04b-phase3-status.md` | libs/sync's interface surface was already narrow; extraction copied files into `~/dev/libkalburator/src/`. |
| Phase 2 — smoke test | done (implicit) | — | Integrated into Phase 3's PlanStan consumption. |
| Phase 3 — PlanStan cutover | done | `04b-phase3-status.md` | add_subdirectory + KALBURATOR_PROVIDE_TYPES=OFF; libs/sync/ removed. |
| Phase B — WP qsynccore lift | done (partial) | `04b-phase3-status.md` | 6 pairs copied upstream. Rest (IDMappingStore SQLite rewrite, ConflictHandlerRegistry call-site migration) rolled into Phase C. |
| Phase C.1 — qsynccore namespace | done | `515ade2` commit | qsynccore files into `Kalburator::Sync` + compat alias. |
| Phase C.2 (original) | **reverted** | `04d-phase-c2-blocker.md` | Flag-day namespace migration hit option-1 layering blocker. Split into C.2a + C.2b. |
| **Phase C.2a** — layering | done 2026-04-20 | `04e-phase-c2a-design.md` | 4 commits. |
| **Phase C.2b** — namespace migration | done 2026-04-20 | `04f-phase-c2b-design.md` | 2 commits. |
| **Phase C.3** — directory layering | done 2026-04-21 | `05-repo-strategy.md` | 2 commits. |
| **Phase C.4** — SQLite IDMappingStore | done 2026-04-21 | `04g-phase-c4-design.md` | Merged-schema SQLite per Audit 2 + `recurrenceId`. Shares `.planstan-sync.db` via ALTER TABLE ADD COLUMN. 12 tests. |
| **Phase C.5** — SyncStore identity-mapping dissolve | done 2026-04-21 | `04c-phase-c-plan.md` §C.5 + `~/dev/PlanStan/docs/superpowers/specs/2026-04-21-c5-syncstore-identity-dissolve-design.md` | Dormant-code cleanup; zero production callers existed. IDMappingStore is sole owner of `sync_id_mappings`. |
| **Phase C.6** — v0.5 tag | done 2026-04-21 | `04c-phase-c-plan.md` §C.6 | `v0.5-phase-c` annotated tag on `main` at the C.5 commit. First named release. |
| **Phase B2** — blob layer | done 2026-04-21 | `04h-blob-layer-design.md` + `04h-blob-layer-plan.md` | Net-new lower-layer blob sync substrate: IBlobBackend, BackendRecord, CollectionInfo, BlobSyncEngine (mirror + twoWayNaive), LocalBlobBackend, MockBlobBackend. First library-side tests. Scope narrow by design; sequel phase wires calendar layer to compose the engine. |
| **Phase B3** — BlobBaselineStore | done 2026-04-21 | `04i-blob-baseline-store-design.md` | SQLite hash-per-record baseline store keyed by (mapping_id, record_id). Shares `.planstan-sync.db` with IDMappingStore + SyncStore. 10-slot test suite in `tests/journal/`. Tag: `v0.7-phase-b3-baseline`. Enables correct 3-way diff in Phase B4's twoWayWithBaseline. First WP-Phase-E-driven upstream deliverable. |
| **Phase B4** — BlobSyncEngine ↔ ConflictStore | queued | (design doc pending, planned for WP Phase E.2) | `twoWayWithBaseline` + `registerConflictHandler` + ConflictStore integration inside the engine. Consumes B3's BlobBaselineStore. Unblocks WP Phase E.3+ on the WP side. |
| Phase 4 — Wild Palms adoption | in progress | proposal §"Two-mode split" | Client Mode + Full Sync Mode profile selection. B2 + B3 are the first upstream pieces. Next up on WP side: full plugin ABI rewrite + PalmBackend refactor, designed in `~/dev/WildPalms/docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`. |
| Phase 5 — Wild Palms Client Mode adapters | deferred | proposal §"Phase 5" | Akonadi / PlanStan D-Bus / plain-files adapters. |

---

## Key documents (annotated)

### In `~/dev/libkalburator/docs/phase0/`

- **`README.md`** — this file. Index + high-level state.
- **`00-open-questions.md`** — initial Phase-0 questions (all resolved).
- **`01-inventory-planstan.md`** — what PlanStan's `libs/sync/` looked
  like before extraction.
- **`02-inventory-wildpalms.md`** — what WP's `src/sync/` +
  `qsynccore/` looked like.
- **`03-conflict-engine-audit.md`** — conflict engine analysis; the
  case for keeping Palm-specific handler in WP.
- **`04-merged-interface-sketch.md`** — provisional interface names
  and ownership.
- **`04a-followups.md`** — four audit items from early Phase-0 review.
  Audit 1 (SyncBackend lineage) and Audit 4 (AsyncFileWriter QSaveFile)
  closed. Audit 2 → Phase C.4. Audit 3 → done in Phase C.1
  (ConflictHandlerRegistry).
- **`04b-phase3-status.md`** — what Phase 3 landed; defines the
  "option 1 vs option 2" layering choice. **Load-bearing** for
  understanding the pre-C.2a world.
- **`04c-phase-c-plan.md`** — **superseded** for C.2 but still the
  source of truth for C.3 / C.4 / C.5 / C.6 scope.
- **`04d-phase-c2-blocker.md`** — post-mortem of the first C.2 attempt.
  Explains why option-1 layering is incompatible with namespace
  migration; source of the C.2a/C.2b split recommendation.
- **`04e-phase-c2a-design.md`** — C.2a spec. Layering migration.
- **`04f-phase-c2b-design.md`** — C.2b spec. Namespace migration with
  the QSyncCore sub-namespace collision resolution.
- **`04g-phase-c4-design.md`** — C.4 spec. SQLite IDMappingStore,
  in-place ALTER TABLE migration, schema-version coordination with
  SyncStore, the pre-C.5 double-writer hazard, and the full test
  matrix.
- **`05-repo-strategy.md`** — naming, licensing, versioning,
  directory layout, build system, stewardship. Target directory
  layout lives here; C.3 implemented the mechanical move but kept
  existing file names (renames are future work).

### In `~/dev/PlanStan/docs/`

- **`proposals/2026-04-20-sync-library-extraction.md`** — top-level
  proposal. Two-mode user-experience split (Client Mode + Full Sync
  Mode in WP), motivation, phases. **Status line at the top of this
  doc must track current reality.**
- **`superpowers/plans/2026-04-20-c2a-layering-migration.md`** — the
  four-task plan that implemented C.2a. Useful as a template for
  future plans.
- **`superpowers/plans/2026-04-20-c2b-namespace-migration.md`** —
  the two-task plan that implemented C.2b, including the
  `wrap_namespace.py` tooling notes.
- **`todo/phase-c-qualify-consumer-headers.md`** — tracks the
  `using namespace Kalburator::Sync;`-in-headers anti-pattern that
  C.2b landed. 92 headers carry `TODO(phase-c-cleanup)` markers.
  Cleanup is optional polish; build is semantically correct.
- **`LibraryDecomposition.md`** — PlanStan's own library modularization
  plan. libkalburator extraction is listed as a separate effort
  orthogonal to the track 1/2 decomposition.

---

## Repo layout after Phase C.3

### `~/dev/libkalburator/`

```
libkalburator/
├── CMakeLists.txt          ← targets: kalburator-types, kalburator
├── docs/
│   └── phase0/             ← this dir; new agents start at README.md
└── src/
    ├── types/  (21 files)  ← Kalburator::Types target; no libkalburator-sync deps
    ├── calendar/  (61)     ← backends, coordination, CRUD items, ISyncHost
    ├── conflict/  (11)     ← ConflictManager + qsynccore (sub-namespace)
    ├── transcoding/ (10)   ← property/RRULE transcoders, diffs
    ├── journal/ (8)        ← CalendarJournal, BaselineStore, IDMappingStore,
    │                         AsyncFileWriter
    ├── discovery/ (4)      ← Syncthing auto-discovery (experimental)
    └── blob/ (8)           ← IBlobBackend, BlobSyncEngine,
                              LocalBlobBackend, MockBlobBackend (Phase B2)
```

CMake targets: `kalburator-types` (alias `Kalburator::Types`),
`kalburator` (alias `Kalburator::Sync` + transitional `PlanStan::Sync`,
`planstan-sync`). No `KALBURATOR_PROVIDE_TYPES` flag; types are always
built into `kalburator-types`.

Namespaces:
- `Kalburator::Sync::*` — everything in `src/types/` and everything in
  `src/{calendar,conflict,transcoding,journal,discovery,blob}/` except:
- `Kalburator::Sync::QSyncCore::*` — the remaining qsynccore files
  (`baselinestore`, `conflicthandlerregistry`, `conflictpolicy`,
  `conflictrecord`, `conflictstore`, `synccommon`). C.4 promoted
  `idmappingstore` out of this sub-namespace into `Kalburator::Sync`
  top-level as part of the SQLite rewrite. Sub-namespace exists to
  avoid `ConflictResolution` / `SyncStats` / `SyncResult` collisions
  with `src/types/synctypes.h`.

### `~/dev/PlanStan/`

Relevant libs (unchanged elsewhere):

- `libs/core/` — PUBLIC-links `Kalburator::Types`; lost 10 shared-type
  headers + 2 .cpps to libkalburator in C.2a.
- `libs/models/` — PUBLIC-links `Kalburator::Types`; lost 4 headers +
  3 .cpps.
- `libs/scheduling/` — PUBLIC-links `Kalburator::Types`; lost 2
  headers.
- Top-level `CMakeLists.txt` — `add_subdirectory(libkalburator)` comes
  **before** the PlanStan libs (so `Kalburator::Types` is available
  when they reference it); the `target_link_libraries(kalburator
  PUBLIC PlanStan::Core …)` wiring comes **after** (so PlanStan::*
  aliases resolve).

Consumer pattern: every PlanStan `.cpp` that references libkalburator
symbols carries `using namespace Kalburator::Sync;` at TU scope
(inserted after the first contiguous `#include` block, preceded by a
single-line `namespace Kalburator::Sync {}` in case the file's
existing includes don't declare the namespace). Headers carry the
same with a `TODO(phase-c-cleanup)` marker.

---

## Unfinished / future work

### Short-term (queued phases)

None. Phase C is complete as of `v0.5-phase-c`. The next active phase
(Phase 4 — Wild Palms adoption) is gated on WP's own roadmap.

### Medium-term (Phase 4+)

- **Wild Palms adoption.** Currently WP has its own `src/sync/` +
  `qsynccore/`; those need to move to consuming libkalburator. The
  two-mode UX (Client Mode vs Full Sync Mode) is the main UX
  deliverable; the technical lift is replacing WP's parallel
  abstractions with libkalburator's.
- **Public forge decision.** Phase 0 deferred the choice of where
  libkalburator's public repo lives (GitHub / KDE Invent / Codeberg).
  Only matters once we want external contributors.
- **KalburatorConfig.cmake + install target.** Currently libkalburator
  is consumed in-tree (add_subdirectory). For `find_package`
  consumption by external apps, a proper install target + Config.cmake
  is needed. Phase 5-ish.

### Known debt

- **`using namespace Kalburator::Sync;` in 92 PlanStan headers.**
  Tracked at `~/dev/PlanStan/docs/todo/phase-c-qualify-consumer-headers.md`.
  Correct semantics today but anti-pattern. Cleanup = qualify inline
  and remove directive. No urgency.
- **Syncthing files in `src/discovery/`.** Not part of
  05-repo-strategy's canonical architecture. Kept in-tree because
  PlanStan uses them today, but long-term they may spin out to a
  `Kalburator::Discovery::*` sibling module (see 05-repo-strategy
  namespace note) or retire.
- **Blob-layer followups.** Phase B2 landed the minimum-viable blob
  layer (mirror + twoWayNaive, no baseline, no conflict integration).
  Explicit followups catalogued in `04h-blob-layer-design.md`
  §"Explicitly deferred" — most notably `BlobBaselineStore`,
  `ConflictStore` integration inside the engine, the calendar-layer
  refactor that composes `BlobSyncEngine` from `SyncCoordinator`,
  and the `AsyncFileWriter` blob/calendar split. Not blocking WP's
  current Phase E.
- **Remaining ctest failures (26).** Pre-existing baseline. Inventory
  in `memory/project_library_decomposition.md`: `tst_blockstore`
  testMoveBlock_withChildren, `tst_treeflatteningproxymodel`,
  `sync_error_recovery` SEGFAULT, `sync_workflow_conflicts` SEGFAULT,
  plus integration tests that don't run in dev environment, plus
  12 graph tests marked "Not Run". **Not caused by the extraction.**
  Independent bugs.
- **remotebackend.cpp's scattered `#include` pattern.** C.2b
  consolidated the late-file includes; file is clean now. Mentioning
  here only because it's a data point for future code-quality sweeps.

---

## How to work on this

### Build + test (libkalburator standalone)

```bash
cmake -S ~/dev/libkalburator -B ~/dev/libkalburator/build
cmake --build ~/dev/libkalburator/build -j"$(nproc)"
```

Tests live at `tests/blob/` as of Phase B2, gated by
`KALBURATOR_BUILD_TESTS` (ON for standalone, OFF for
add_subdirectory consumers). Run with:

```bash
ctest --test-dir ~/dev/libkalburator/build --output-on-failure
```

Expected: 3/3 pass (`tst_mockblobbackend`, `tst_localblobbackend`,
`tst_blobsyncengine`).

### Build + test (PlanStan consuming libkalburator)

```bash
cmake -S ~/dev/PlanStan -B ~/dev/PlanStan/build -DPLANSTAN_DEV_BUILD=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build ~/dev/PlanStan/build -j"$(nproc)"
cd ~/dev/PlanStan/build && WAYLAND_DISPLAY=wayland-0 QT_QPA_PLATFORM=wayland ctest -j"$(nproc)"
```

Baseline: **86 pass / 26 fail / 112 total**. A new failure blocks the
commit.

PlanStan's `CLAUDE.md` says "use `/project:build` instead of running
make directly" — that's a project slash command. The equivalent raw
commands are above. Either works.

### Before landing a phase (mandatory checklist)

Every one of these is required. No exceptions. The commit that lands
the code must also land the doc updates — not the next commit, not a
follow-up PR, the **same commit**.

- [ ] Update the **Status** line at the top of
      `~/dev/PlanStan/docs/proposals/2026-04-20-sync-library-extraction.md`.
- [ ] Update the phase map row in **this file** (`README.md`).
- [ ] Update the **Current status** section in this file (what's done,
      what's next, baseline health numbers).
- [ ] Bump the **Last updated** date at the top of this file.
- [ ] If the phase was spec'd in a `04X-phase-…-design.md` doc, append
      a short "Outcome" section to that doc (what actually landed vs
      what was planned, any deviations).
- [ ] If the phase resolves an item in **Unfinished / future work**,
      remove it or move it to the phase map.
- [ ] Ensure build + ctest baseline is held.
- [ ] Commit message: follow the phase tag convention (`Phase C.X: …`).

If you find yourself wanting to skip one of these "just this once" —
don't. The whole point of this file existing is that future agents
can trust its contents. Every untracked phase landing makes it less
trustworthy.

### Tooling survivors from the C.2 attempt

- `/tmp/wrap_namespace.py` — wraps libkalburator files in `Kalburator::Sync`.
  Handles Qt metatype hoisting and namespace forward-decls. Known quirks:
  it places the namespace open after the last top-level `#include`; if a
  file has late-file `#include`s (like remotebackend.cpp did), the
  namespace opens too late and needs a manual adjustment.
- `/tmp/add_using_cpp_v3.py` — inserts `namespace Kalburator::Sync {}`
  + `using namespace Kalburator::Sync;` after the first contiguous
  include block. The v1 script (still in `/tmp/add_using_cpp.py`) used
  the LAST #include which misfires on files with late moc includes.
- `/tmp/add_using_header_v2.py` — header variant; same fix.
- `/tmp/kalb_real_syms.txt` — symbol list (105 entries). Used to
  regenerate consumer lists.
- `/tmp/planstan_consumers_live.txt` — most recent generated consumer
  list (261 files post-C.2a).

These live in `/tmp` and may not survive a reboot. If lost, regenerate
from the plan docs and the tooling section of `04f-phase-c2b-design.md`.

---

## Single-developer caveat

Every document in `docs/phase0/` assumes a static branch with one
developer. There is **no upstream remote** on either repo's branches
(`main` for libkalburator, `master` for PlanStan). Design decisions
that would matter for multi-developer workflows (cross-repo CI,
version pinning, PR etiquette) are out of scope until Phase 4.

Until then: when a phase lands in libkalburator, the PlanStan
dependency update can follow immediately in the same session. No
coordination overhead.

---

## When to ask the maintainer vs decide autonomously

The extraction work has had two modes:

1. **Brainstorm-then-approve** — used for each new sub-phase (C.2a,
   C.2b, C.3) to pin design decisions (target structure, collision
   handling, atomicity strategy, directory layout). The pattern:
   one clarifying question at a time, options A/B/C with a
   recommendation, get approval, proceed.
2. **Autonomous execution** — once the design is pinned, the
   maintainer has been comfortable with autonomous execution through
   the implementation, including commits landed directly on `master`
   / `main`. Stop only on a real blocker (build fails unexpectedly,
   scope drifts, semantic ambiguity in the plan).

For a new agent: err toward mode (1) on any design-shaped question
and toward mode (2) on mechanical execution. If a phase is already
spec'd (there's a `docs/phase0/04X-phase-…-design.md` with
"approved for implementation"), mode (2) is safe.
