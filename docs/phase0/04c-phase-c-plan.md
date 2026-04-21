# Phase C — layered split + namespace migration + deferred Phase B bundling

**Date:** 2026-04-20.
**Status:** Superseded 2026-04-20. C.2 split into C.2a (layering) and
C.2b (namespace) per `04d-phase-c2-blocker.md`. The layering migration
plan is in `04e-phase-c2a-design.md`; C.2b retains the plan sketched
here but assumes option-2 layering has landed.

Phase C reshapes libkalburator into its v1.0-target form. Bundles the
three deferred Phase B items (per user direction 2026-04-20).

## Scope

1. **Namespace migration** — every library type moves into
   `Kalburator::Sync::*`. Files currently in `QSyncCore::` (the lifted
   qsynccore) and files currently in global / PlanStan namespaces
   (the rest of `src/sync/` and `src/types/`) both consolidate under
   the target namespace.
2. **Directory layering** — `src/sync/` (flat, ~90 files) splits
   into `src/{blob,calendar,conflict,transcoding,journal,types}/`
   per `05-repo-strategy.md` §"Directory layout". `src/types/`
   content folds into `src/types/` at the new location (shared
   fundamentals) or into its domain-layer directory.
3. **Deferred Phase B items bundled:**
   - Add `ConflictHandlerRegistry` + public `coordinator->registry()`
     accessor (Audit 3).
   - Rewrite WP's `IdMappingStore` as SQLite-backed merged-schema
     `IDMappingStore` (Audit 2).
   - Migrate PlanStan call sites off `SyncStore::setIdMapping` onto
     the new `IDMappingStore`.

## Sub-step execution order

C is too large for one atomic commit. Break into these sub-steps, each
a commit, each individually verifiable:

- **C.1 — qsynccore namespace migration + `ConflictHandlerRegistry`.**
  Wrap the 11 lifted qsynccore files in `Kalburator::Sync::` instead
  of `QSyncCore::`. Add compatibility alias `namespace QSyncCore =
  Kalburator::Sync;` in `synccommon.h` so any future WP code that
  lands pointing at the old namespace keeps working until its own
  Phase E migration. Add new `conflicthandlerregistry.{h,cpp}` in
  the target namespace. *This session.*
- **C.2 — migrate PlanStan-originated library code into
  `Kalburator::Sync::*`.** Wrap every header/source under `src/sync/`
  (except the already-done qsynccore files) and `src/types/` in
  `Kalburator::Sync::`. Update every PlanStan consumer include to
  qualify. This is the big-churn sub-step; likely 80+ files in
  libkalburator, 200+ in PlanStan. Single commit, big review.
- **C.3 — directory layering.** Move files into `src/{blob,calendar,
  conflict,transcoding,journal,types}/` per `05-repo-strategy.md`.
  Update `libkalburator/CMakeLists.txt` to glob per-directory. Update
  every consumer's include paths. Could be combined with C.2 but
  keeping them separate makes the diffs reviewable.
- **C.4 — SQLite `IDMappingStore` rewrite.** Replace the lifted WP
  JSON-backed implementation with a SQLite-backed merged-schema
  implementation per Audit 2. New struct: `IDMapping` with all of
  `backendId`, `sourceUid`, `targetId`, `calendarId`, `lastSynced`,
  `sourceCategory`, `targetCategories`, `archived`. Methods: PS's
  per-backend-qualified API as primary, WP's category/archive
  methods as optional.
- **C.5 — PlanStan call-site migration.** PS's existing
  `SyncStore::setIdMapping` / `sourceUidForTargetId` callers move to
  `IDMappingStore`. Remove the identity-mapping methods from
  `SyncStore` (or leave as deprecated shim — decide during C.5
  planning).
- **C.6 — tag `v0.5-phase-c` once all sub-steps land clean.**

## Risk notes

- **C.2 is a flag-day.** All PlanStan consumer code breaks at the
  moment C.2 lands; cannot do piecemeal without per-file `using`
  shims that rot. Plan for a single large commit that's built + tested
  before push. No half-way state.
- **C.3's directory moves break clangd indices.** Consumers need to
  regenerate `compile_commands.json` after the move. Flag in the
  commit message.
- **C.4 has a migration path concern:** existing PlanStan users on
  disk have SQLite DBs at `.planstan-sync.db` with ID mappings in a
  specific schema. The new `IDMappingStore` must either preserve the
  on-disk schema or ship a migration. Decide during C.4.
- **C.5 dissolves `SyncStore` partially.** The audit said `SyncStore`
  dissolves in Phase C — C.5 does the identity-mapping piece;
  baseline/version/CTag/conflict pieces can move in follow-ups or
  bundle too (scope TBD during C.5).

## Not in scope for Phase C

- Contacts/memos upstreaming (deferred past Phase 4 per
  `00-open-questions.md` §6).
- Public-forge hosting decision (Phase 4+).
- WP's actual consumption of libkalburator (Phase E — happens after
  Phase C lands).

## C.5 outcome (2026-04-21)

**Scope correction.** Original plan assumed PlanStan had live callers of
`SyncStore::setIdMapping` et al. Grep across both repos (on the morning
of landing) showed zero production callers — the identity-mapping API
on `SyncStore` had been dormant. C.5 therefore became a pure dead-code
cleanup, not a call-site migration.

**Shim decision.** "Delete" won over "shim" because there were no
callers to protect. No deprecation window was needed.

**Landed:** single commit in each repo. libkalburator: removed 6
methods + CREATE + INDEX + clearBackendData's sync_id_mappings DELETE.
PlanStan: deleted 6 identity tests + 1 coexistence test; rewrote 5
tests that had used identity APIs as test fixtures
(`testDatabaseReopen`, `testVacuum`, `testClearBackendData`,
`testEmptyStrings`, `testSpecialCharacters`).

**ctest delta:** unchanged. 88 pass / 4 fail / 23 not-run.
