# 04z — Phase Ib.5 status

**Status: landed 2026-05-08** — tag `v0.28.5-phase-ib.5-engine-generalization`

---

## What exists now

- `dispatchCalendarLegacy` deleted; calendar routes through unified
  `dispatchSync` alongside all other domains.
- `AskUser` pause/resume protocol lifted into unified path
  (`unifiedHandleConflicts` + `resumeAfterConflict`).
- `dispatchFirstSync` hoisted into `dispatchSync`; blob-baseline
  existence check added to skip fast-path when prior baselines exist.
- Dead `SyncEngine::itemReady` / `itemFetched` signals deleted (zero
  subscribers confirmed by exhaustive grep across all three repos).
- `IDomainAdapter` (`src/engine/idomainadapter.h`) deleted.
- `CalendarDomainAdapter` (`src/calendar/calendardomainadapter.{h,cpp}`)
  deleted.
- KCalendarCore removed from `src/engine/`: zero `#include` hits.
- `ConflictResolution::Duplicate` case added to `resumeAfterConflict`.
- `OneWayDownload` conflict handling converted from early-return to
  `filterNonConflictToTarget` flag (correct for the unified path).
- PlanStan: `tst_sync_dialog.cpp` wired with `BlobBaselineStore`;
  `tst_akonadibackend` `HAVE_AKONADI` define fixed.

## Acceptance criteria (all met)

- [x] `grep -rn KCalendarCore libkalburator/src/engine/` → empty
- [x] `grep -rn dispatchCalendarLegacy libkalburator` → empty
- [x] `ls libkalburator/src/engine/idomainadapter.h` → no such file
- [x] `ls libkalburator/src/calendar/calendardomainadapter.*` → no such files
- [x] `grep -rn "SyncEngine::itemReady\|SyncEngine::itemFetched" libkalburator PlanStan WildPalms` → empty
- [x] libkalburator: 73/75 tests pass (2 pre-existing flaky; see FINDINGS.md)
- [x] PlanStan: 82/106 (unchanged)
- [x] WildPalms: 78/78 (unchanged)
- [x] `verify-all.sh` exits 0

## Deferred-work items closed

- A.1 Calendar-typed signals → domain-generic (deleted outright)
- A.2 Remove KCalendarCore from engine TU
- A.3 Delete IDomainAdapter and CalendarDomainAdapter

## What remains (Phase Ic+)

- Phase Ic — WildPalms accounts UX (no design yet)
- A.4 — Restructure blob batch diff/merge into per-record loop
- A.5 — CustomMerge and Duplicate policy UI (consumer-side)
- Calendar property phase baseline-aware diff (deferred; no test)

## Tag

`v0.28.5-phase-ib.5-engine-generalization` on libkalburator
`refactor/engine-merger` HEAD (commit `3917e36`).
