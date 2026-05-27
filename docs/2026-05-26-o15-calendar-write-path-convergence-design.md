# O15 — Calendar write-path convergence (design)

**Status:** approved 2026-05-26. Targets approach **A (two-phase)**.
**Finding:** FINDINGS O15 (`docs/campaign/FINDINGS.md`).
**Invariant served:** §1 — extend the one mechanism (the shape graph + canon
diff/merge/baseline + `ConflictManager`); never keep a second.

---

## 1. Problem

`src/calendar/calendarplugin_writer.cpp` carries **two** write paths:

1. **SyncTransaction path** — taken when the engine's `ICalendarCollection`
   resolves a host `MemoryCalendar` for the collection. Parses iCal →
   `Incidence`, wraps `Create/Update/DeleteIncidenceItem` in a `SyncTransaction`,
   and calls `tx.commitAll()`.
2. **"blob-only fallback"** — taken otherwise. Drives
   `createRecord`/`updateRecord`/`deleteRecord` on the backend's `IBlobBackend`.
   This is **byte-for-byte the same logic as `DefaultBlobWriter`**.

Path (2) is, post-convergence, the *canonical* engine write mechanism (it is what
`DefaultBlobWriter` does and what the unified engine drives for cross-encoding
backends). Path (1) is the legacy calendar-specific special-case, and the
"fallback" label inverts reality. This is a lingering second write mechanism.

### Why path (1) is safe to retire (investigation, 2026-05-26)

- **It never updates the host `MemoryCalendar`.** `CreateIncidenceItem::commit`
  and `UpdateIncidenceItem::commit` only null-check `m_calendar`, then write via
  `backend()->pushItems(...)`. The "do I have a MemoryCalendar?" fork is
  meaningless to the *result*; there is no live-model-update benefit.
- **Its conflict/version/collision detection is dead code in this path.** Those
  checks live only in `simulate()`. The sole caller of `simulate()` is
  `SyncTransaction::simulateAll()`, and **nothing in production calls
  `simulateAll()`** — the writer calls only `commitAll()`. (`CalendarManager`'s
  direct path was already converged off in Plan 4 Task 6b.)
- **The robust feature it gestures at already exists, more completely, in the
  converged engine.** Conflict detection is a first-class, engine-wide subsystem:
  baseline + `perRecordDiff` (the `hasS && hasT && hasB`, both-changed →
  `Conflict` branch), `ConflictManager`, `SyncConflictStore`, `AskUser`
  pause/resume policies, `hasUnresolvedConflicts`, and the mass-delete guard.
  Path (1)'s `simulate()` is a calendar-only, never-invoked, less-complete
  duplicate of it.
- **Path (1) writes *less* faithfully.** `pushItems` does an iCal
  parse → re-serialize round-trip (lossy KCal normalization); `createRecord`
  writes the engine-demoted bytes verbatim. (`LocalBackend`: both write the same
  `<root>/<calendarId>/<uid>.ics` path.)
- **`pushItems`/`deleteItems` are already marked vestigial** in recent commits;
  `createRecord`/`updateRecord`/`deleteRecord` is the canonical `IBlobBackend`
  surface, proven working for every host backend by the `*_blob_view` tests.

Path (1) has one *live* differentiator the blob path lacks — **transactional
rollback** (`SyncTransaction::rollbackCommitted`, invoked from `commitAll()` on
commit failure). Four tests in `tst_calendar_sync_error_recovery` pin it as a
contract (`storeFailsPartial_rolledBack`, `pushFailsPartial_rolledBack`,
`deleteFailsPartial_rolledBack`, `partialWriteRollback_targetClean`). **But
investigation (2026-05-26) showed this rollback is a MockBackend artifact, not a
production capability:**

- `MockBackend::shouldFail` is sticky and per-op-type: once tripped it fails every
  subsequent op of that *one* type and never resets.
- Rollback succeeds in those tests only because it issues a *different* op type
  than the one failing: `CreateIncidenceItem::rollback` → `deleteItems`
  (`OnDelete`) while the test fails `OnPush`/`OnStoreItems`;
  `DeleteIncidenceItem::rollback` → `pushItems` while the test fails `OnDelete`.
- Under a realistic systemic failure (auth/network/5xx/disk), *all* writes fail,
  so the rollback ops (also writes) fail too — partial state remains regardless.
- `rollbackCommitted()` logs each failed item and then **unconditionally emits
  `rollbackCompleted(true)`** — so under real failure it both leaves partial state
  and misreports success.
- The genuinely robust property is already present and domain-uniform: on any
  write failure the engine **skips saving baselines** (`syncengine.cpp` ~`:2658`,
  "phantom deletions on retry"), making a failed sync safely retryable.

Conclusion: the rollback "contract" holds only under MockBackend's artificial
single-op-type failure and is silently false for the failure modes real backends
exhibit. Building genuine all-or-nothing (2-phase commit / compensation) is
unattainable across remote backends and unwarranted (YAGNI). We therefore
converge and **replace the four rollback tests** with assertions on the contract
that is actually meaningful (see §5/§7). Recorded as a non-goal in §6.

---

## 2. Goal & non-goals

**Goal.** The calendar domain writes through the same uniform record path
(`DefaultBlobWriter`) as every other domain. `CalendarPluginWriter`, the
`SyncTransaction`/`*IncidenceItem` machinery, and the engine's calendar-specific
writer plumbing are retired.

**Non-goals.**
- Adding transactional rollback to the converged write path (separate concern;
  uniform-at-engine-layer if ever wanted).
- Touching `ICalendarCollection` / `setCollection` — `CalendarManager` still uses
  them. We only stop *feeding* the host `MemoryCalendar` to writers.
- Any change to conflict detection, the loss model, or the canon differ.
- Touching `incidencediff`/`syncdiff` (now in `src/diff/`, load-bearing — O10).

---

## 3. Architecture (unchanged downstream of the writer)

```
engine: demote canon → target native encoding (ical/palm/vcard/…)
      → RecordWriter::apply(creates, updates, deletes)
      → IBlobBackend::create/update/deleteRecord
```

The single change is **which `RecordWriter` calendar uses**: `DefaultBlobWriter`
instead of `CalendarPluginWriter`. All type-aware work (typed diff, four-kind
loss, conflict detection) is upstream in the canon/diff/merge/`ConflictManager`
layer and is untouched.

### Consumer map (verified 2026-05-26)

Real code consumers of `SyncTransaction` + `*IncidenceItem`:
- `calendarplugin_writer.cpp` (the path being converged)
- the classes themselves + `synctransactionitem.{h,cpp}` + `synctesthooks.h`
- tests: `tst_synctransaction`, `tst_calendar_plugin_writer`,
  `tst_calendar_sync_error_recovery`, `tst_engine_cancellation`

Every `localbackend.cpp` and `syncengine.cpp` reference to these names is a
**comment** (label on `LocalBackend`'s `pushItems`/`deleteItems` ops; description
of the already-removed engine `dynamic_cast`). `ICalendarCollection` is also used
by `calendarmanager.{h,cpp}` and stays.

---

## 4. Phase 1 — sever + delete the writer

Each step keeps the calendar integration suite green (that suite is the
behavioral contract per repo `CLAUDE.md`).

1. **`CalendarDomainOperations::createWriter`** (`src/calendar/calendardomainoperations.cpp`)
   returns `nullptr`.
2. **Engine writer selection** (`src/engine/syncengine.cpp`, the two
   `opsUCC ? opsUCC->createWriter(b) : DefaultBlobWriter` sites ~`:2633`, `:2655`):
   treat a *null* `createWriter()` result as "build
   `DefaultBlobWriter(blobBackend)`". General, all-domains fix; `tgtBlob`/`srcBlob`
   are already in scope at both call sites. Concretely:
   ```cpp
   auto tgtWriter = opsUCC ? opsUCC->createWriter(tgtBackend) : nullptr;
   if (!tgtWriter)
       tgtWriter = std::make_unique<Kalburator::Shape::DefaultBlobWriter>(tgtBlob);
   ```
3. **`applyBatch`** (`src/engine/syncengine.cpp` ~`:2529`): stop sourcing
   `ctx.calendarCollection` for writers (surviving writers ignore it). Leave
   `RecordWriter::ApplyContext` / `prepareForApply` in the interface (harmless);
   just don't populate the calendar field. `m_collection` / `setCollection` stay.
4. **Delete** `src/calendar/calendarplugin_writer.{h,cpp}`; remove it from
   `tests/calendar/CMakeLists.txt` and the library `CMakeLists.txt`; delete
   `tests/calendar/tst_calendar_plugin_writer.cpp`.
5. **Threading note:** `DefaultBlobWriter` declares the default
   `Threading::BackendThread`. The engine's `applyBatch` already handles both
   threading modes; the calendar writer's `WorkerThread` override (which existed
   only because `SyncTransaction::commitAll` marshalled internally) is no longer
   needed. No engine change beyond removing the now-dead writer.
6. **Rewrite the four rollback tests** in `tst_calendar_sync_error_recovery.cpp`
   (`storeFailsPartial_rolledBack`, `pushFailsPartial_rolledBack`,
   `deleteFailsPartial_rolledBack`, `partialWriteRollback_targetClean`). Routing
   calendar through `DefaultBlobWriter` removes rollback, so their
   all-or-nothing assertions (`targetUids() == 0`, target restored to 3) no
   longer hold. Re-point each to the **meaningful, retry-safe contract**: the
   sync reports failure (`!m_lastResult.success`), and baselines are *not* saved
   so a retry re-attempts the failed writes (no phantom deletions). Drop the
   `targetUids() == 0` / `== 3` rollback assertions; where useful, assert that
   partial writes may remain (best-effort) and that a subsequent successful sync
   converges. The other error-recovery slots (immediate-failure, fetch-failure)
   already assert failure propagation and are unaffected — keep them.
7. **Comment cleanup:** drop the stale `CalendarPluginWriter`/`SyncTransaction`
   references in `syncengine.{h,cpp}` comments where they describe the removed
   path (don't rewrite unrelated text).

**Phase 1 exit criteria:** full `ctest` is green except the known O9 flake; the
calendar integration tests (`tst_calendar_sync_full`, `_oneway`, `_conflict`,
`_subsequent_sync_uses_blob_view`, `_first_sync_via_blob_engine`,
`_sync_error_recovery`, `tst_engine_cancellation`) pass. The second write
**mechanism** is gone — invariant §1's concern is resolved at this point.

---

## 5. Phase 2 — delete the now-dead transaction classes

After Phase 1, `SyncTransaction` + `*IncidenceItem` are consumed only by their own
tests + `synctesthooks.h`.

1. **Confirm no test depends on `SyncTransaction` for injection.**
   `tst_calendar_sync_error_recovery` and `tst_engine_cancellation` already inject
   via `MockBackend::setFailurePoint` (not `SyncTransaction` hooks), and after
   Phase 1 their write-path assertions already pass on the converged path. The
   only `SyncTransaction`-coupled test is `tst_synctransaction` itself (deleted in
   step 2). `synctesthooks.h` (`SYNC_HOOK`/`SyncTestHooks`) is used only inside
   `synctransaction.cpp` — verify with `grep -rn "SyncTestHooks\|SYNC_HOOK"` and
   confirm it has no other consumers before deleting.
2. **Delete** `synctransaction.{h,cpp}`, `synctransactionitem.{h,cpp}`,
   `createincidenceitem.{h,cpp}`, `updateincidenceitem.{h,cpp}`,
   `deleteincidenceitem.{h,cpp}`, `synctesthooks.h`, and `tst_synctransaction.cpp`;
   remove all from the library + test `CMakeLists.txt`.
3. **Sweep** for residual includes/forward-decls of the deleted headers
   (`grep -rn` across `src/` and `tests/`); fix `localbackend.{h,cpp}` /
   `syncengine` comments that name `SyncTransaction` so the tree has no dangling
   references.

**Phase 2 exit criteria:** the deleted files are gone, no dangling references,
full `ctest` green except O9. `KALBURATOR_HAVE_ORG_IO=ON` / `HAVE_AKONADI=ON`
profiles also build (they compile the org/akonadi backends, which use the
`IBlobBackend` surface — confirm no transaction dependency was introduced there).

---

## 6. Risks & mitigations

- **A backend's `createRecord`/`updateRecord`/`deleteRecord` is weaker than its
  `pushItems`.** Mitigated: the `*_blob_view` tests already pin the blob surface
  for every host backend; Phase 1 runs the full calendar integration suite
  through the new path. If any backend regresses, it surfaces as a failing
  integration test, not silent data loss.
- **Lost transactional rollback** changes partial-failure semantics for
  multi-record calendar batches. Accepted (non-goal §2): the rollback was a
  MockBackend artifact (§1), the retry-safe baseline behavior is preserved, and
  converged behavior matches every other domain. The four rollback tests are
  rewritten (Phase 1 step 6), not deleted, so the *real* contract stays pinned.
  Documented in FINDINGS when O15 is closed.
- **Downstream (PlanStan/WildPalms)** may construct `CalendarPluginWriter` or the
  `*IncidenceItem` classes directly. Mitigated: deletion makes this a compile
  error downstream, surfaced at their port (consistent with O7/O12's
  port-post-merge model). Note it in the FINDINGS O15 closeout.

---

## 7. Test strategy

- **Contract net:** the existing calendar integration suite is the behavioral
  contract; it must stay green through both phases (run after every step).
- **Behavior change — partial-failure semantics.** The four rollback tests are
  rewritten in Phase 1 (step 6) to assert the retry-safe contract instead of
  all-or-nothing (rollback was a MockBackend artifact, §1). No *new* feature is
  introduced.
- **Phase 2** has no test-behavior changes beyond deleting `tst_synctransaction`
  (it tested the now-removed class). `tst_engine_cancellation` already injects via
  `MockBackend::setFailurePoint`, not `SyncTransaction` hooks — confirm its
  comment reference to the writer is updated, but its assertions are unaffected.
- Run the full `ctest` (default profile) at each phase boundary; build the
  `ORG_IO=ON` / `AKONADI=ON` profiles at the Phase 2 boundary.

---

## 8. Documentation / bookkeeping

- Close **O15** in `docs/campaign/FINDINGS.md` (move to Resolved) in the commit
  that lands Phase 2, with: the investigation summary (path 1 vestigial, not
  load-bearing), the rollback non-goal, and the downstream-compile-error note.
- One Discipline-Log line if any incidental smell is found mid-refactor.
