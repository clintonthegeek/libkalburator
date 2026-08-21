# Conflict-resolution repair — plan

**Branch:** `feature/conflict-resolution-repair` (from `main` @ `b0bf3d5`, v0.97)
**Source of truth for the defects:**
`docs/2026-08-21-conflict-info-canonical-data-and-unmonitored-resolution-handoff.md`
**Opened:** 2026-08-21

Four defects, all in the same conflict code, all with the same origin (the
canon-upgrade campaign promoted `BackendRecord::data` to canonical Shape JSON
inside `dispatchSync()`, and the conflict code was never taught about it).

| # | Defect | Site |
|---|--------|------|
| A | `ConflictInfo::source/targetIcalData` carry canonical JSON, not native iCal | `unifiedHandleConflicts()` both branches, `syncengine.cpp:3273`/`:3299` |
| B | A resolution chosen in `Unmonitored` mode is never applied to any data | `conflictmanager.cpp` store-only paths + `SyncEngine::onWorkerSyncCompleted` |
| C | `resumeAfterConflict(resolution, mergedIcal)` never reads `mergedIcal` — `CustomMerge` silently runs the auto-merger instead | `syncengine.cpp:2105`, `CustomMerge` case |
| D | `Duplicate` rewrites the clone's uid with `data.replace("UID:"+id, ...)`, which never matches canonical JSON (`"uid":"…"`) — the clone keeps the original uid and collides | `syncengine.cpp:2152` |

D is PlanStan's separately-filed
`docs/bugs/sync-dialog-keepboth-duplicate-not-created.md`; it closes with this work.

## Locked decisions (user, 2026-08-21)

1. **Bug B mechanism = resolution injection into the next run.** Persist the
   resolution, hand it to the next `dispatchSync()` for that mapping, apply it
   inline in the `AskUser` branch through a helper extracted from
   `resumeAfterConflict()`'s `switch`. The existing write / transcode /
   baseline / host-notify path is reused verbatim — **no second write
   mechanism** (campaign INVARIANTS §1). Rejected: giving `SyncEngine` its own
   write-path access.
2. **Auto follow-up run.** Once a run reaches its terminal state, `SyncEngine`
   starts one targeted run for mappings that acquired pending resolutions,
   guarded against re-entry. Consumers need no change.
3. **Stale resolutions are discarded, not applied.** Compare the live records'
   `lastModified` against what was recorded at detection; on mismatch drop the
   stored resolution, `qWarning()`, and present a fresh conflict.
4. **Scope includes C and D**, plus explicit test coverage of
   `ConflictManager`'s `Deferred` and `AutoResolve` modes (not just coverage
   inherited from the Bug B mechanism).

## Task sequence

Tasks are **sequential** — 1 and 2 edit adjacent regions of `syncengine.cpp`
and 3 depends on the helper 2 extracts.

- **Task 1 — Bug A. DONE 2026-08-21.** Stashed `m_unifiedSrcToCanon` /
  `m_unifiedCanonToSrc` / `m_unifiedCanonToTgt` next to `m_unifiedCanonical`;
  both `ConflictInfo` sites collapsed onto one
  `SyncEngineWorker::buildConflictInfo(op)` that demotes each side back to
  its backend's native encoding; empty data guarded (modify-delete);
  `sourceEncoding`/`targetEncoding` added additively (transport-only — the
  conflict-store schema was left alone); doc comments corrected.
  **Correction to this plan:** `baselineIcalData` cannot be populated —
  `EngineDiffOp::baselineRecord` is a hash-only shell and the engine loads
  per-side hashes, never bytes, so the field is wired but always empty. See
  FINDINGS **O48**; PlanStan's 3-way diff stays unreachable until baseline
  storage changes. Also fixed in passing: the unmonitored branch never set
  `source/targetModified` (FINDINGS **O49**).
- **Task 2 — Bugs C + D + helper extraction.** Extract
  `resumeAfterConflict()`'s `switch` into
  `applyConflictResolution(op, resolution, mergedNative)` on the worker;
  honor `mergedIcal` (promote through `m_unifiedSrcToCanon`, fall back to the
  auto-merger when empty); rewrite the Duplicate clone's uid in the canonical
  envelope.
- **Task 3 — Bug B.** `ConflictManager::conflictResolved` as the single
  channel; `m_pendingResolutions` in `SyncEngine`, rehydrated from
  `SyncConflictStore`; carried on `SyncEngineWorker::Request`; consumed in the
  `AskUser` branch; applied ids returned in `SyncResult` and their rows
  deleted; staleness guard; auto follow-up run.
- **Task 4 — Verification and docs.** Full suite, FINDINGS O-numbers,
  `docs/2026-07-19-consumer-coordination-status.md`, `CLAUDE.md`, tag.

## Baseline suite state (from `main`, unchanged by this work)

179 total, 177 passing. Two pre-existing failures:
`tst_remotecalendarbackend` (broken local Radicale test-server auth) and
`tst_calendar_canon_roundtrip` (pre-existing on `main`, uncatalogued).
**Any third failure is ours.**
