# Plan 9 — Remove confirmed dead code

**Audit refs:** B9 (MINOR)
**Depends on:** Plans 1–8 (some "dead" symbols only become truly dead after the
preceding refactors complete).
**Branch:** `feature/redress-9-dead-code`
**State:** Task-level detail. Tasks are short and verification-heavy.

## Goal

Delete code that is genuinely unreferenced after the rest of the campaign lands.
Confirm zero downstream consumers before each deletion (INVARIANTS §10).

## Problem (from AUDIT B9)

Candidates with 0–1 external references inside libkalburator:

- `src/types/icommanddispatcher.h` — undo/command interface; no implementations.
- `src/types/iincidencesource.h` — read-only incidence iterator; not wired up.
- `src/types/incidenceref.h` — lightweight pairing struct; not used in production.
- `src/calendar/incidencesyncadapter.h` — fully implemented adapter; never instantiated.
- `src/backend/resourcelinearization.h` — pure-virtual interface; not adopted.
- `src/calendar/iconflictpresenter.h` — UI interface; no implementation.
- `src/blob/mockblobbackend.h` and `src/blob/localblobbackend.h` — five signals
  declared (`recordCreated`, `recordUpdated`, `recordDeleted`, `errorOccurred`,
  `progressUpdated`) never connected anywhere.

**Out of scope (per INVARIANTS §10 and STATUS "Out of scope"):**

- The `[[deprecated]]` v2 baseline APIs in `storage/baselinestore.h` are intentional
  migration scaffolding for downstream (PlanStan/WildPalms). **Do not delete.**

## Approach

For each candidate, three checks before deletion:

1. **In-repo grep**: `git grep -n "<symbol>" src/ tests/ examples/ widgets/` returns
   only the defining file (and possibly test stubs that are themselves dead).
2. **Downstream grep**: same in `~/dev/PlanStan/src` and `~/dev/WildPalms/src` if
   reachable. If a downstream consumer exists, the symbol is **not** dead even if
   unreferenced inside libkalburator — open a coordination thread before deletion.
3. **Plan-overlap check**: confirm no in-flight plan or open task references the symbol
   (e.g. `IIncidenceRegistry` moved by Plan 3 is *not* dead, even though it might look
   it from a static scan during this plan).

## Tasks

### Task 1 — Cross-repo grep for each candidate

For each candidate file, run the three-check sequence. Record results in a table in the
plan branch's commit message. If a downstream consumer exists, **stop on that
candidate** and either coordinate the downstream removal or move that candidate to
STATUS "Out of scope".

```
for sym in ICommandDispatcher IIncidenceSource IncidenceRef \
           IncidenceSyncAdapter ResourceLinearization IConflictPresenter; do
  echo "=== $sym ==="
  git grep -n "$sym" src/ tests/ examples/ widgets/
  grep -rn "$sym" ~/dev/PlanStan/src ~/dev/WildPalms/src 2>/dev/null
done
```

### Task 2 — Delete the confirmed-dead interface files

For each confirmed-dead file:

1. `git rm src/<path>/<file>.h` (and `.cpp` if present).
2. Update `CMakeLists.txt` source list if the file was explicitly listed.
3. Build. If anything breaks, the candidate wasn't actually dead; revert and move it to
   "needs coordination" in STATUS.

Expected deletions:

- `src/types/icommanddispatcher.h`
- `src/types/iincidencesource.h` (if not already moved by Plan 3)
- `src/types/incidenceref.h`
- `src/calendar/incidencesyncadapter.h`
- `src/backend/resourcelinearization.h` (if not moved by Plan 5)
- `src/calendar/iconflictpresenter.h`

### Task 3 — Delete the unused blob backend signals

For each of `mockblobbackend.h` and `localblobbackend.h`:

1. Grep `connect(*, &XBlobBackend::recordCreated)` and the same for the other four
   signal names. Confirm zero matches across src/, tests/, downstream.
2. Delete the five signal declarations from each header.
3. Delete the `Q_EMIT` call sites (or `emit`) in the corresponding `.cpp`.
4. Build. Tests pass.

### Task 4 — Sweep for newly-dead symbols after Plans 1–8

The preceding plans may have killed symbols not in this plan's original list:

1. `SyncEngineWorker` is no longer publicly visible (Plan 1) — its internal
   declaration in `engine/syncengine_p.h` survives but anything that referenced it
   externally was deleted then. Re-grep.
2. `BackendContribution` may have been replaced by `IBackendProvider` (Plan 2). If so,
   the old name is dead.
3. The deprecated shims from Plans 1/4/7 were deleted in Plan 8 — confirm none of their
   helper types survive uselessly.
4. Any symbol that grep returns as "defined here, referenced nowhere" after this sweep
   is a candidate; apply Task 1's three-check sequence and delete.

### Task 5 — Re-run tests and close

1. Full ctest.
2. PlanStan ctest.
3. WildPalms smoke if locally runnable.
4. Update FINDINGS: cross out B9 entries with closing commit hash.
5. Close the campaign: write the retrospective doc named in STATUS "Definition of done"
   (`docs/2026-XX-XX-architectural-redress-retrospective.md`).

## Files affected

- Several `.h` (and possibly `.cpp`) deleted; root and per-dir `CMakeLists.txt` updated.

## Acceptance criteria

- Each deleted file has a documented cross-repo grep result in the commit message
  showing zero references.
- All tests pass; PlanStan ctest baseline holds.
- The `[[deprecated]]` v2 baseline surface in `storage/baselinestore.h` is untouched.

## Risks

- **A downstream consumer in a branch we didn't check.** PlanStan and WildPalms have
  feature branches; grep the working tree of those repos, not just main. Where in
  doubt, leave the file and add a `FINDINGS.md` entry for the next campaign instead.
- **A "dead" interface is actually a public extension point.** `IIncidenceRegistry` and
  `IIncidenceSource` were meant for the qsynccore extraction. If that extraction is
  still planned, they aren't dead — they're scaffolding. Confirm in STATUS or in the
  prior campaign's notes before deletion.

## Estimated effort

1–2 sessions. The deletions themselves are minutes; the cross-repo verification is the
bulk of the work.
