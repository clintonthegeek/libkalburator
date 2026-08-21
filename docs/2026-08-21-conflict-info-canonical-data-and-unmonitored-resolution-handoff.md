# Conflict resolution: `ConflictInfo` carries canonical JSON, not iCal, and Unmonitored resolutions are never applied

**Date:** 2026-08-21
**From:** PlanStan dev (live session, freshly-fixed conflict-dialog wiring
finally let a real conflict reach the dialog for the first time in
production — see "Why this was never caught" below)
**Affects:** `src/engine/syncengine.cpp` (`SyncEngineWorker::unifiedHandleConflicts`,
`SyncEngineWorker::resumeAfterConflict`, `SyncEngine::onWorkerSyncCompleted`),
`src/conflict/conflictmanager.cpp`. Observed on tag **v0.97**
(`63b7f76`, current `main` tip as of this handoff — confirmed the relevant
code is unchanged there).
**Severity:** critical for Bug B, high for Bug A. Together they mean
**conflict resolution is completely non-functional in `SyncBehavior::Unmonitored`**
— the only mode any real PlanStan sync run uses, ever (auto-sync-on-load,
the periodic background tick, and — as far as this session observed —
manual "Sync Now" too; every logged sync in two full live sessions said
`behavior= Unmonitored`).

## Summary

Two independent defects, found together in one live PlanStan session once
an unrelated PlanStan-side UI wiring bug got the conflict dialog appearing
in production for the first time (it had never actually rendered against
real data before this session — see "Why this was never caught"):

- **Bug A:** `ConflictInfo::sourceIcalData`/`targetIcalData` (documented as
  "Full iCal") are actually always canonical Shape JSON, for *every*
  conflict, regardless of backend type. `unifiedHandleConflicts()` never
  converts back. PlanStan's conflict UI (property diff table, Custom Merge)
  can't parse it, so both silently produce nothing.
- **Bug B (the severe one):** in `SyncBehavior::Unmonitored`, choosing a
  conflict resolution in the UI only writes a column in `SyncConflictStore`.
  Nothing ever applies it to the actual backend data or advances the
  baseline. The identical conflict re-detects and re-presents on every
  subsequent sync, forever. The code path that *does* correctly apply a
  resolution (`resumeAfterConflict`) is wired exclusively to the Monitored
  mid-run yield, which this mode never takes.

Both are reproduced below with a **minimal, self-contained change to an
existing libkalburator test** — no PlanStan, no live server, no display
needed.

## Why this was never caught until now

`tests/engine/tst_syncengine_unification.cpp::unmonitoredConflictRecordsIcalData`
already exists (added for a prior bug,
`docs/bugs/sync-conflict-store-duplicate-rows.md` per its own header
comment) and drives exactly this scenario — but only asserts
`!sourceIcalData.isEmpty()`. Canonical JSON is non-empty too, so that
test has been silently passing over Bug A's data the whole time. See
"Reproduction" below for the one-line-stronger assertion that catches it.

Bug B has no existing test at all in the Unmonitored+real-resolution shape
— `conflictPauseResumeRoundTrip` (same file) covers the Monitored
yield/resume round trip, which works correctly and isn't what's broken.
Nothing exercises "Unmonitored conflict, dialog resolves it, does the data
actually change on the next sync" — which is exactly PlanStan's real usage
and exactly what's broken.

Both bugs are old — nothing about this session changed engine behavior.
They were invisible because the conflict dialog itself had a **separate,
now-fixed, PlanStan-side wiring bug** that meant it never actually
constructed for a real provider-backed collection in any prior session (see
PlanStan's `docs/bugs/conflict-resolver-never-wired-for-late-local-mirror.md`
and its sibling `docs/bugs/conflict-dialog-never-wired-for-provider-backends.md`,
already fixed and deleted at PlanStan commit `ada10530`). Once that got
fixed, a real conflict finally reached the dialog for the first time in
production, and immediately exposed both of these.

## Live evidence (PlanStan session, v0.97)

```
SyncEngine: Batch-presenting 1 conflicts for mapping "auto_..._sync1"
DialogConflictResolver::resolveConflict - resolution: 1 mergedIcalData length: 0
```
— then, later, driving into Custom Merge:
```
ConflictResolutionDialog::buildMergedIcal - starting
  sourceIcalData length: 304
kf.calendarcore: parse error from icalcomponent_new_from_string. string=
"{\"_canon\":{\"domain\":\"calendar\",\"kind\":\"vtodo\",\"v\":1},\"created\":
\"2025-08-07T14:47:40Z\",\"lastModified\":\"2026-08-21T18:28:06Z\",\"priority\":5,
\"providerExtras\":{\"x-vtodo\":{\"X-APPLE-SORT-ORDER\":\"730411708\"}},
\"status\":\"needsAction\",\"summary\":\"Iron dress shirts soon\",
\"uid\":\"c1ee29b6-ce54-4ba7-8fcc-9755f3065d85\"}"
ConflictResolutionDialog: Parsed with VCALENDAR wrapper
ConflictResolutionDialog: No incidences in source iCal
DialogConflictResolver: CustomMerge selected but merged data is empty!
```
The `.ics` file actually on disk for that item, confirmed by `cat`, is
genuine iCalendar text (`BEGIN:VCALENDAR ... SUMMARY:Iron dress shirts
soon ... END:VCALENDAR`) — so this is not a `LocalBackend`
storage-format quirk (an earlier draft of this diagnosis, on the PlanStan
side, wrongly assumed that; corrected before this handoff was written).
`LocalBackend::loadRecords()`/`recordFromBytes()`
(`src/calendar/localbackend.cpp:1262`, `:1136`) genuinely return native
`.ics` bytes. The JSON is introduced later, inside `dispatchSync()` itself
— see Bug A below — and it is **universal**, not backend-specific: it would
happen for a CalDAV↔CalDAV conflict too.

After that: choosing **Keep Local** or **Keep CalDAV** repeatedly, across
several subsequent auto-sync cycles, produced the *identical* conflict
every time:
```
SyncEngine: Batch-presenting 1 conflicts for mapping "auto_..._sync1"
DialogConflictResolver::resolveConflict - resolution: 1 mergedIcalData length: 0
SyncRunCoordinator: mapping completed: success: false ... target: "+0 ~0 -0 =0 !1 E0"
```
— repeating, unchanged, every ~30s auto-sync tick, for the rest of the
session.

## Bug A — `dispatchSync()` promotes both sides to canonical shape before diffing; `unifiedHandleConflicts()` never converts back for display

`dispatchSync()` (`src/engine/syncengine.cpp`) deliberately transcodes
**both** fetched record lists into canonical Shape encoding before the diff
runs — by design, so the per-record `IRecordDiffer`/`IRecordMerger` can
compare records from mismatched native backends generically:

```cpp
// src/engine/syncengine.cpp:2900-2903 ("Phase Ia.5 Task 8: promote source
// records to canonical shape")
if (!srcToCanon->isIdentity()) {
    for (auto &r : sourceRecords) {
        ...
        r.data = srcToCanon->apply(r.data);
        ...
```
```cpp
// src/engine/syncengine.cpp:3034-3036 (identical, target side)
if (!tgtToCanon->isIdentity()) {
    for (auto &r : targetRecords) {
        r.data = tgtToCanon->apply(r.data);
    }
}
```

`perRecordDiff()` (`src/engine/perrecorddiff.cpp`) copies these already-
canonicalized records verbatim into `EngineDiffOp::record`/`targetRecord`
(`makeConflict()`, `perrecorddiff.cpp:88-96`) — it does not touch `.data`
itself, it just carries forward whatever it was handed. So by the time
`unifiedHandleConflicts()` builds a `ConflictInfo` for the UI, `op.record.data`
/`op.targetRecord.data` are **always** canonical Shape JSON — for a
LocalBackend↔CalDAV mapping, a CalDAV↔CalDAV mapping, anything — and get
assigned straight into fields documented as iCal:

```cpp
// src/engine/syncengine.cpp:3273-3274 (Monitored-yield branch) and
// again at :3299-3300 (Unmonitored-defer branch) — same bug, both sites
info.sourceIcalData  = QString::fromUtf8(op.record.data);
info.targetIcalData  = QString::fromUtf8(op.targetRecord.data);
```

This is silent because `ConflictInfo::hasFullData()`
(`src/types/synctypes.h:100-102`) only checks emptiness, and nothing
downstream in the engine itself parses these fields — only PlanStan's UI
tries to, and fails quietly (Qt's iCal parser just logs and returns no
incidences).

**The fix is already half-built and sitting right next to the bug.** The
*reverse* transcoders already exist in the very same function, used to
convert canonical merge results back to native encoding before writing to
each backend:

```cpp
// src/engine/syncengine.cpp:2712-2715
std::optional<Kalburator::Shape::Pipeline> srcToCanon = reg.compile(srcShape, canonical);
std::optional<Kalburator::Shape::Pipeline> tgtToCanon = reg.compile(tgtShape, canonical);
std::optional<Kalburator::Shape::Pipeline> canonToTgt = reg.compile(canonical, tgtShape);
std::optional<Kalburator::Shape::Pipeline> canonToSrc = reg.compile(canonical, srcShape);
```
```cpp
// already used this exact direction, elsewhere in the same file, e.g.
// src/engine/syncengine.cpp:3776 and :3815
rec.data = canonToTgt->apply(rec.data);   // (:3776, applying to target)
rec.data = canonToSrc->apply(rec.data);   // (:3815, applying to source)
```

**The catch:** `srcToCanon`/`tgtToCanon`/`canonToTgt`/`canonToSrc` are local
variables of `dispatchSync()` (which starts at `:2575`). `unifiedHandleConflicts()`
is a separate method (`:3213`) and does not see them. The class already
solves exactly this problem for three other pieces of per-run state needed
across `dispatchSync`/`unifiedHandleConflicts`/`resumeAfterConflict` — they're
stashed as worker members right before the conflict walk begins:

```cpp
// src/engine/syncengine.cpp:3149-3155
m_unifiedDiff     = std::move(engineDiff);
m_unifiedMerge    = EngineMerge{};
m_unifiedConflictIdx = 0;
m_unifiedPolicy   = request.mapping.conflictPolicy;
m_unifiedOverride = request.override;
m_unifiedCanonical = canonical;
```

The natural fix: add `m_unifiedCanonToSrc`/`m_unifiedCanonToTgt` (same
`std::optional<Pipeline>` type) to that block, then in
`unifiedHandleConflicts()`'s two `info.sourceIcalData =`/`targetIcalData =`
sites, apply the reverse pipeline before the UTF-8 wrap:

```cpp
info.sourceIcalData = QString::fromUtf8(
    m_unifiedCanonToSrc->apply(op.record.data));
info.targetIcalData = QString::fromUtf8(
    m_unifiedCanonToTgt->apply(op.targetRecord.data));
```

(Both pipelines were already proven non-null by the `!srcToCanon || !tgtToCanon
|| !canonToTgt || !canonToSrc` guard earlier in `dispatchSync()`, `:2717`, so
no new null-check is needed as long as the stored members are only read from
inside a run that reached the conflict walk.)

`baselineIcalData` (`ConflictInfo`'s third data field, used for 3-way diff)
is currently never populated by either branch at all — `op.baselineRecord.data`
exists on the diff op (see `makeConflict`'s `baseline` parameter,
`perrecorddiff.h`) and is presumably also canonical; worth wiring through
with the same `canonToSrc` conversion while this code is being touched,
since PlanStan's `ConflictResolutionDialog` already checks
`!conflict.baselineIcalData.isEmpty()` to decide 2-way vs 3-way diff
(`src/sync/conflictresolutiondialog.cpp:166-172`) and currently always gets
the 2-way path.

## Bug B — a resolution chosen in `Unmonitored` mode is never applied to any data

`dispatchSync()`'s `AskUser` branch behaves completely differently
depending on `behavior`:

**`Monitored`** (`syncengine.cpp:3250-3278`) — yields mid-run
(`m_yieldedForConflict = true; emit conflictPauseRequested(info); return;`).
A caller resolves and calls `SyncEngine::resumeAfterConflictResolution()`
(`:941-960`), which signals the owning worker
(`resumeAfterConflictRequested`, connected `Qt::QueuedConnection` at
`:204-205` to `SyncEngineWorker::resumeAfterConflict`, `:2105`). That slot
**genuinely applies** the resolution — writes `SourceWins`/`TargetWins`/
`LastWriteWins`/`Duplicate`/`CustomMerge` into `m_unifiedMerge.finalTarget`/
`finalSource`/`updatedBaselines` (`:2105-2216`), which the rest of the
run's completion path (`unifiedContinueAfterConflicts`, batch-writes,
baseline persistence) then actually commits. **This path is correct.**

**`Unmonitored`** (`syncengine.cpp:3279-3303`, "Unmonitored AskUser: defer
to next sync") — does **no data write of any kind**. It appends the
conflict to `m_currentResult.unresolvedConflicts` and
`m_pendingUnmonitoredConflicts`, and `continue`s the diff-op loop. The
mapping's sync run then finishes and reports success/failure based on
whether any conflicts remain unresolved — the conflict itself is just
carried forward as data, never acted on.

After the mapping completes, `SyncEngine::onWorkerSyncCompleted()`
batch-presents whatever got deferred:

```cpp
// src/engine/syncengine.cpp:1408-1419
if (m_conflictManager && !m_pendingUnmonitoredConflicts.isEmpty()) {
    m_conflictManager->handleConflicts(m_pendingUnmonitoredConflicts);
    m_pendingUnmonitoredConflicts.clear();
}
```

`ConflictManager::handleConflicts()` → `showImmediateDialog()`
(`src/conflict/conflictmanager.cpp:78-153`) shows the dialog and, on a real
choice, does exactly this:

```cpp
// src/conflict/conflictmanager.cpp:145-151
ConflictResolution resolution = m_conflictResolver->resolveConflict(conflict, m_parentWidget);
if (resolution != ConflictResolution::Skip && m_syncStore && !conflictId.isEmpty()) {
    m_syncStore->resolveConflict(conflictId, resolution);   // one DB column, nothing else
    emit conflictResolved(conflictId, resolution);
}
```

`ConflictManager::applyResolution()` — the method whose own doc comment
says "SyncEngine reads the resolution and applies data modifications"
(`conflictmanager.cpp:221-222`) — exists (`:208-227`) but **nothing calls
it** from this flow, and nothing in `SyncEngine` ever polls
`SyncConflictStore` for a resolved-but-unapplied conflict to act on. The
comment describes a design intent that was never wired up on this path.

Net effect: `resumeAfterConflict()`'s apply logic — the *only* code that
ever turns a `ConflictResolution` into an actual write — is structurally
unreachable from `Unmonitored` mode. It operates on live, mid-flight worker
state (`m_unifiedDiff`, `m_unifiedConflictIdx`, `m_unifiedMerge`) that
belongs to a single `dispatchSync()` invocation and does not exist anymore
by the time the batch-presented dialog returns a choice — the run that
detected the conflict is long finished. **A fix cannot just call
`resumeAfterConflict()` after the fact; that call assumes an in-progress
run that no longer exists.**

### Fix directions (needs a decision, not attempted here)

1. **Make the batch-present path actually apply, as a fresh targeted
   operation.** After `showImmediateDialog()`/`handleConflicts()` returns a
   real (non-Skip) resolution for a conflict, either:
   - Extract `resumeAfterConflict()`'s `switch (resolution)` body
     (`:2114-2211`) into a standalone helper that takes the two records +
     baseline + resolution and returns the records to write + baseline to
     update, callable outside the yielded-run state machine. Then have
     `SyncEngine` (not the worker, since the worker/run is gone) apply that
     result: write to both backends via the same `applyBatch`/write-path
     `unifiedContinueAfterConflicts` uses, and persist the baseline via
     `BaselineStore`. This is more invasive but fixes the general case,
     including `CustomMerge`.
   - Or: treat a resolved-but-unapplied conflict as a trigger to schedule a
     small, targeted follow-up sync for just that one mapping/record, with
     the resolution somehow forced/injected so the *next* `dispatchSync()`
     run applies it deterministically instead of re-detecting a fresh
     `AskUser` conflict. Needs a way to carry "this specific conflict has a
     pending resolution" into the next run's diff — `SyncConflictStore`
     already has the resolution persisted (`resolveConflict()`), so this is
     plausible, but the diff/merge loop would need to consult it before
     falling into `AskUser` again for the same id.
2. **Route background sync through `Monitored` instead**, since that path
   already works. Rejected as the primary fix by the PlanStan side: per
   their own docs, `resolveEffectiveCap(Monitored)` forces concurrency to 1
   unconditionally (`syncengine.cpp:975-981`, confirmed while writing
   this handoff), which would give up the parallel-sync work for any run
   that *might* hit a conflict — not knowable in advance. Might be
   reasonable specifically for a user-invoked "Sync Now" if PlanStan
   doesn't already use Monitored there (per this session's logs, it
   currently does not — every observed run said Unmonitored).

Option 1's first sub-approach (extract-and-reuse) is probably the more
correct fix long-term, since it's the only one that doesn't change
PlanStan's concurrency model at all — but it touches more surface
(`SyncEngine` needs write-path access it doesn't currently need outside a
live worker run). Worth a design pass before implementation; this handoff
stops at "these are the two real options" deliberately.

## Reproduction (libkalburator-only, no PlanStan, ~2 minutes)

`tests/engine/tst_syncengine_unification.cpp::unmonitoredConflictRecordsIcalData`
(line ~409) already builds exactly this scenario (two `MockBackend`s, both
default `{calendar, ical}` native shape — same as every real backend —
`AskUser` policy, `SyncBehavior::Unmonitored`, a genuine three-way
conflict against a seeded baseline). Strengthening its two existing
`QVERIFY2` calls (currently just `!isEmpty()`) confirms Bug A directly:

```cpp
// after the existing QCOMPARE(unresolved.size(), 1) at line ~475:
KCalendarCore::ICalFormat fmt;
auto diagCal = QSharedPointer<KCalendarCore::MemoryCalendar>::create(QTimeZone::systemTimeZone());
QVERIFY(fmt.fromString(diagCal, unresolved.first().sourceIcalData));
QCOMPARE(diagCal->incidences().size(), 1);
```

**Confirmed RED against current `main` (`63b7f76`)** — actual
`sourceIcalData` from a live run of this exact test:
```
"{\"_canon\":{\"domain\":\"calendar\",\"v\":1},\"allDay\":false,\"classification\":\"public\",
\"created\":\"2026-08-21T19:32:47Z\",\"end\":{\"dateTime\":\"2026-08-21T19:32:47Z\",\"floating\":false,
\"tz\":\"UTC\"},\"lastModified\":\"2026-08-21T19:32:47Z\",\"start\":{\"dateTime\":\"2026-08-21T19:32:47Z\",
\"floating\":false,\"tz\":\"UTC\"},\"summary\":\"Source-Modified\",\"timeTransparency\":\"opaque\",
\"uid\":\"evt-conflict\"}"
```
```
kf.calendarcore: parse error from icalcomponent_new_from_string. string= "{...same JSON...}"
```
— i.e. the exact same failure mode as the live PlanStan session, byte-for-byte
consistent with the `_canon` envelope described in Bug A, reproduced with
zero PlanStan code involved. (This diagnostic edit was reverted after
confirming — not left in the tree; the real fix should land its own
regression assertion here, ideally checking `diagCal->incidences().first()->summary()
== "Source-Modified"` too, so a fix that parses-but-mangles content still
fails.)

**Bug B has no existing test to strengthen** — it needs a new one. Natural
shape, same file/fixture: run `unmonitoredConflictRecordsIcalData`'s setup
through to a resolved conflict (attach a `ConflictManager` with a stub
`IConflictResolver` returning e.g. `SourceWins`, matching the pattern
`conflictPauseResumeRoundTrip` already uses for the Monitored case a few
tests up in the same file), then run a **second** `runSync()` on the same
mapping and assert the target backend's record now actually equals the
source's — today it will still show the original conflicting pair,
unchanged, and a fresh `AskUser` conflict for the very same id.

## Not investigated / out of scope for this handoff

- Whether a CalDAV↔CalDAV (no local side) conflict looks any different in
  practice — the code path is identical (both sides go through the same
  `srcToCanon`/`tgtToCanon` promotion), so it should reproduce Bug A
  identically, but wasn't independently live-verified.
- `ConflictManager::WorkflowMode::Deferred`/`AutoResolve` paths
  (`queueForDeferred()`, `applyAutoPolicy()`) — not exercised in the live
  session (PlanStan uses `Hybrid`), not audited for the same defect class,
  though `applyAutoPolicy()`'s comment ("Record and immediately resolve")
  suggests it has the identical "store-only, no data write" shape as
  `showImmediateDialog()` and is worth a look while in this code.
- Any interaction with the parallel-sync worker pool beyond the
  single-mapping case demonstrated here.

## PlanStan-side context (for anyone who wants the consumer's view)

`docs/bugs/conflict-resolution-nonfunctional-in-unmonitored-sync.md` in the
PlanStan repo has the original live-session narrative and PlanStan's own
severity framing; it now points back to this document as the authoritative
root cause. `docs/bugs/sync-dialog-keepboth-duplicate-not-created.md`
(PlanStan) is a related, narrower, previously-filed defect in the same
`resumeAfterConflict()` `Duplicate` case (`:2137-2160`) — worth checking
whether fixing Bug A/B here also resolves or reshapes that one, since it's
in the exact code this handoff's fix will touch.
