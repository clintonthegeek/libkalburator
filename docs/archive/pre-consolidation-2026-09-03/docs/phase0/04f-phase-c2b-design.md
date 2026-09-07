# Phase C.2b — namespace migration design

**Date:** 2026-04-20.
**Status:** Design spec, approved for implementation. Follows C.2a.
Implementation plan lives in PlanStan at
`docs/superpowers/plans/2026-04-20-c2b-namespace-migration.md`.

## Goal

Wrap every global-scope libkalburator symbol in `Kalburator::Sync::*`
so the library carries a proper namespace identity instead of
polluting the global namespace. This is the original C.2 scope per
`04c-phase-c-plan.md`, now unblocked by C.2a's layering migration.

## Scope

### libkalburator

- **Kalburator::Sync (flat)** — every file in `src/types/*` and every
  non-qsynccore file in `src/sync/*`. Currently at global scope.
- **Kalburator::Sync::QSyncCore (sub-namespace)** — the 13 qsynccore
  files: `baselinestore.{h,cpp}`, `conflictpolicy.{h,cpp}`,
  `conflictrecord.{h,cpp}`, `conflictstore.{h,cpp}`,
  `idmappingstore.{h,cpp}`, `synccommon.h`,
  `conflicthandlerregistry.{h,cpp}`. Currently in `Kalburator::Sync`
  (post-C.1); move **down** one level to avoid collision with the
  identically-named `ConflictResolution`, `SyncStats`, `SyncResult`
  now promoted into `Kalburator::Sync` from `src/types/synctypes.h`.
- **synccommon.h compat alias** — change `namespace QSyncCore =
  Kalburator::Sync;` to `namespace QSyncCore =
  Kalburator::Sync::QSyncCore;` so any dormant WP-facing code that
  referenced `QSyncCore::ConflictPolicy` keeps resolving correctly.

### PlanStan consumers

~181 files (50 headers + 131 .cpps) under `libs/*/include/` and
`libs/*/src/` that reference kalburator symbols. The prior-attempt
consumer list at `/tmp/planstan_real_consumers.txt` is stale (it
includes files deleted by C.2a) and must be regenerated from the
current source tree against `/tmp/kalb_real_syms.txt`.

Each consumer file gets `using namespace Kalburator::Sync;` inserted
after its last `#include`. Headers carry the anti-pattern marker
`// TODO(phase-c-cleanup): qualify inline, remove this` per the prior
attempt's add_using_header.py convention. Cleanup (convert using to
inline qualification) is deferred to a follow-up phase; tracked in
`docs/todo/phase-c-qualify-consumer-headers.md`.

### Out of scope

- Any qualification of references across the `Kalburator::Sync` vs
  `Kalburator::Sync::QSyncCore` boundary inside libkalburator. If an
  edge case surfaces where flat-sync code needs QSyncCore types, it
  gets explicit `Kalburator::Sync::QSyncCore::` qualification inline;
  not a blanket using directive.
- Wild Palms consumption (Phase E).
- Any reorganization of files into `src/{blob,calendar,conflict,...}/`
  subdirectories — that's C.3.

## Commit sequence

Two commits, both flag-day scale. Executed in order:

### Commit 1 — libkalburator: wrap everything

Repo: `~/dev/libkalburator`.

1. Move the 13 qsynccore files from `Kalburator::Sync` to
   `Kalburator::Sync::QSyncCore` by rewriting the outer namespace open
   line and close comment.
2. Wrap every remaining `src/sync/*.{h,cpp}` and `src/types/*.{h,cpp}`
   that is currently at global scope in `Kalburator::Sync`. Metatype
   macros hoisted outside the namespace and qualified with
   `Kalburator::Sync::`. Qt forward-decls (`QNetworkReply`, `QWidget`,
   `QUndoStack`) hoisted above the namespace open.
3. Update `synccommon.h`'s compat alias.
4. Verify: standalone libkalburator build green.
   **PlanStan will NOT build between commits 1 and 2** — that's
   expected and OK on a solo / static branch.

### Commit 2 — PlanStan: add using directives

Repo: `~/dev/PlanStan`.

1. Regenerate consumer list against the current tree:
   `scripts/phase-c2b/find_consumers.sh` (written as part of Task 2 in
   the plan) scans `libs/*/{include,src}/*.{h,cpp}` for references to
   any symbol in `/tmp/kalb_real_syms.txt` and writes the live list.
2. Run `/tmp/add_using_cpp.py` against the cpp subset; run
   `/tmp/add_using_header.py` against the header subset.
3. Configure + build PlanStan. Fix any misses (should be rare, and
   likely mechanical — symbols used in ways the scan didn't detect).
4. Verify: clean build + ctest baseline (86/26/112 per C.2a's
   close).

## Tooling

`/tmp/wrap_namespace.py`, `/tmp/add_using_cpp.py`,
`/tmp/add_using_header.py` from the prior attempt are still on disk
and functional. Two adaptations needed:

- **wrap_namespace.py** defaults to `Kalburator::Sync`. Add a
  `--namespace Kalburator::Sync::QSyncCore` flag (or a second script)
  to target the sub-namespace for the 13 qsynccore files.
- **add_using_header.py / add_using_cpp.py** are correct as-is. The
  consumer list they read is the file passed on the command line —
  pass the freshly regenerated list.

The regenerate-consumer-list helper doesn't exist yet. Implement as a
small bash/Python script that greps each symbol in kalb_real_syms.txt
across PlanStan `libs/` source tree and emits the uniq'd file list.

## Collision handling details

Three specific collision pairs drove the QSyncCore sub-namespace
decision:

| Symbol | src/types/ (now Kalburator::Sync) | qsynccore (→ Kalburator::Sync::QSyncCore) |
|---|---|---|
| `ConflictResolution` | 7 enumerators including LastWriteWins / CustomMerge | 6 enumerators with Merge; values differ |
| `SyncStats` | struct with different fields | struct with different fields |
| `SyncResult` | struct with different fields | struct with different fields |

Sub-namespacing keeps both alive under distinct qualified names. A
future phase that merges qsynccore's vocabulary into src/types/
(Phase 4+ Wild Palms adoption work) can remove the sub-namespace; not
in scope here.

## Testing strategy

Per-commit:
- Commit 1 (libkalburator): standalone build
  (`cmake -S ~/dev/libkalburator -B ~/dev/libkalburator/build && cmake --build ~/dev/libkalburator/build -j`).
- Commit 2 (PlanStan): `/project:build` (or equivalent) + ctest with
  Wayland env. Baseline: 86 pass / 26 fail / 112 total.

## Risks and rollback

- **Missed consumers.** Regeneration step + post-build fallback (`grep
  -r 'BackendConfiguration\|…' libs/` on any compile error). Each
  missed file gets a manual using directive.
- **Metatype macro regressions.** `wrap_namespace.py` already hoists
  Q_DECLARE_METATYPE lines outside and qualifies them. Verified via
  standalone build in Commit 1.
- **Header anti-pattern leak.** Accepted as deferred tech-debt; the
  TODO marker makes it greppable for cleanup.
- **Rollback:** Both commits are `git revert`-safe; libkalburator is
  a single-file-scope-edit revert, PlanStan is a bulk using-directive
  delete.

## What unblocks after C.2b lands

- **C.3** — directory layering into
  `src/{blob,calendar,conflict,transcoding,journal,types}/` per
  `05-repo-strategy.md`.
- **C.4** — SQLite IDMappingStore rewrite (IDMappingStore is in
  QSyncCore sub-namespace; sub-namespace survives the rewrite).
- **C.5** — PlanStan SyncStore call-site migration.
- **Phase 4** — Wild Palms adoption. Wild Palms code that references
  `QSyncCore::ConflictPolicy` continues to work via the compat alias.

## Decisions log

Q — Collision handling: **sub-namespace qsynccore into
Kalburator::Sync::QSyncCore** (option A from brainstorm). Rationale:
minimal surface change; qsynccore is dormant so the indirection costs
nothing; reversible when/if Wild Palms unifies its vocabulary with
src/types/.

(All other design points are dictated by 04d and C.2a's completion
state; not re-decided here.)

## References

- `04c-phase-c-plan.md` — original C.2 plan (superseded for C.2a;
  C.2b still follows its shape).
- `04d-phase-c2-blocker.md` — post-mortem; source of the C.2a/C.2b
  split.
- `04e-phase-c2a-design.md` — the layering migration whose completion
  unblocks this phase.
- `/tmp/wrap_namespace.py`, `/tmp/add_using_cpp.py`,
  `/tmp/add_using_header.py`, `/tmp/kalb_real_syms.txt` — prior-attempt
  tooling; still on disk.
