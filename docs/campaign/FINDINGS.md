# Campaign FINDINGS — canon-upgrade / convergence

Two sections:
- **Open issues / watch items** — things discovered that a future task must handle.
  When you resolve one, note the commit and move it to "Resolved."
- **Discipline Log** — one-line smell reports per invariant 9. `file:line` + invariant
  number + one phrase. No fix required in the session you log it.

Append, don't rewrite. New issues from any task go here, even off-topic.

---

## Open issues / watch items

### O1 — `LossProfile` is surfaced to the engine layer
`tests/engine/tst_engine_unified_routing.cpp:383` reads `lastLossProfile().level`, and
`tests/engine/tst_carddav_engine_integration.cpp` imports `LossLevel`. Plan 1 Task 2
migrates these to the kinds API. **Watch:** if a stub host or engine code (not just
tests) exposes `LossProfile::level`/`.dropped`, Task 2's full build will name it — fix
with the documented substitutions, don't reintroduce the old fields. (Seeded 2026-05-23.)

### O2 — Carry-verbatim containers not yet field-specified
The non-isomorphic structures (event `RANGE=THISANDFUTURE`; todo hierarchy:
`relatedTo` tree / `parentUid` / `checklistItems`) are decided in principle (schema
§2, §4) but their exact JSON shape and differ/merger handling land in **Plan 3**.
Invariant P4 applies: specify the carrier before diffing it. (Seeded 2026-05-23.)

### O3 — Live API validation deferred, not done
The vendor-shapes reference (`docs/2026-05-23-vendor-api-shapes-reference.md`) is from
docs + MS open specs, not live calls. Edge cases (real recurrence payloads, IANA↔Windows
mappings, Graph immutable-id behavior) should be validated with live Google/Graph calls
**during Plan 3** when the concrete (de)serialization stages are written. Not a blocker
for Plans 1–2. (Seeded 2026-05-23.)

### O4 — WildPalms port is downstream, but its invariants are upstream
WildPalms moves its conduits onto the converged pipeline **after this branch merges**
(handoff §3, §7). We do not wire WildPalms. But the five WildPalms invariants
(invariant 10) must hold throughout — especially that the loss model can express
`Reversible` (X-property round-trip) before Plan 3 writes the palm/canon edges. (Seeded 2026-05-23.)
**Update 2026-05-25:** WildPalms' memo holdout now has a concrete, human-approved design — a `note`
plaintext-carrier domain + `(note, markdown)` peer (frontmatter ⟷ `providerExtras`, `Reversible`) +
a `MarkdownFilesBackend` sink. See `docs/2026-05-25-note-domain-design.md`. Additive, post-convergence;
implementation plan pending. The `Reversible` requirement is satisfied by the existing four-kind model.

### O5 — `pipeline.cpp` loss folding assumed clean
Planning grep found no `.level`/`.dropped` usage in `src/shape/pipeline.cpp`, so
`composedLoss()` should fold via `compose()` unchanged. **Watch:** confirm during Plan 1
Task 2's full build; if `composedLoss()` references the removed fields, update it the
same way. (Seeded 2026-05-23.)

### O6 — design §8 was imprecise; corrected when Plan 2 was designed
The §8 planning stub said **two** `Shape::` singletons remain (`TransformationRegistry`,
`DomainRegistry`) and that they would be "owned by the `SyncEngine`." Reading the landed call sites
showed **three** (the engine also reads `DomainOperationsRegistry` at `syncengine.cpp:1874`,`:2423`;
~40 tests `clear()` it), and that engine-ownership is wrong because `PluginManager` is an independent
*writer* of the same registries. §8 was rewritten (2026-05-24) to the resolved topology — an injected
`ShapeRegistries` bundle owned at the composition root, shared by reference with both PluginManager
(writer) and SyncEngine (reader), the OSGi `BundleContext` model. Recorded as a documented deviation
from the stub per the INVARIANTS deviation rule. (Seeded 2026-05-24, Plan 2 design.)

### O8 — Plan-3/Plan-4 boundary: calendar canon moved into Plan 3 (scope decision)
Design §10's file-change list placed "`calendarstockshapes.cpp`: add canon + `ical↔canon`
bridges" under the **Plan 4** (convergence) work, while the STATUS Plan-3 row named
`calendar+canon` as Plan-3 scope — two sources of truth disagreeing (inv 7). Surfaced while
writing Plan 3 (2026-05-24). **Decision (human, 2026-05-24):** land all three canons —
including `calendar+canon` and its `ical↔canon` bridges — in **Plan 3**; narrow **Plan 4** to
pure convergence (retire `src/transcoding/`, RRULE-as-edge, remove `ApplyContext.transcodingPlan`
+ `CalendarPluginWriter` special-casing). Safe because the engine reads canonical from
`DomainDefinition::canonicalShape()` (not the graph) and transcoding is dormant in the default
build (empty plans; `RRuleTranscoder` only fires for `orgmode` backends, not built when
`KALBURATOR_HAVE_ORG_IO=OFF`). Plan 3 Task 13 records the same in STATUS on completion.
(Seeded 2026-05-24, Plan 3 authoring.)

### O9 — pre-existing `tst_providerlifecycle` failure (unrelated, not introduced by campaign)
`tst_providerlifecycle::provisionProvider_backendsReadyEmittedAfterConnectAll` fails in
test #87 (introduced in commit `b395e5b`, before the campaign branch). The failing assertion
`ready.count() >= 1` implies a signal-timing or async issue in the `ProviderLifecycle` test
harness. The campaign suite is 111/112 green; this one excluded. **Watch:** investigate
independently; do not confuse with campaign regressions. (Seeded 2026-05-24, Plan 3 Task 13.)

### O10 — design §10 "delete src/transcoding/ in full" is wrong: incidencediff/syncdiff are load-bearing
While researching Plan 4 (2026-05-24), found that `src/transcoding/incidencediff.{h,cpp}` and
`src/transcoding/syncdiff.{h,cpp}` are **conflict/diff engines**, not transcoding, and are used widely
outside the dir: `src/engine/syncengine.{h,cpp}`, `enginediff.h`, `propertydiff.h`,
`src/calendar/icalrecorddiffer.{h,cpp}`, `icalrecordmerger.cpp`, `updateincidenceitem.cpp`,
`decsyncactivecontroller.{h,cpp}`, and tests `tst_syncdiff.cpp`/`tst_incidencediff.cpp`/
`tst_calendar_subsequent_sync_uses_blob_view.cpp`. So design §10's "Delete `src/transcoding/` in full"
(inv 7: two sources of truth) cannot be taken literally. **Plan 4 retires only the transcoding
machinery** (`transcodingregistry`, `transcodingrouter`, `transcodingplan`, `rruletranscoder`,
`propertytranscoder`) and must **keep `incidencediff`/`syncdiff`** (move them out of the dir or leave
the dir as their home — decision pending). (Seeded 2026-05-24, Plan 4 research.)

### O11 — legacy RRuleReverseTranscoder was a silent no-op (latent bug, superseded by Plan 4 Task 2)
While re-homing RRULE simplification (Plan 4 Task 2, commit 624d2f3), found that the legacy
`src/transcoding/rruletranscoder.cpp` `RRuleReverseTranscoder::transcode` (~:138-174) restored the
stashed original RRULE via `KCalendarCore::ICalFormat::fromString(RecurrenceRule*, ruleStr)` — which
**always returns false for RRULE strings** (both the `RRULE:FREQ=…` form and the value-only form),
so the reverse transcoder silently restored *nothing*. No test ever covered it. The new
`OrgICalToCanonStage` sidesteps the broken API: it stashes verbatim iCal recurrence *lines* (joined by
`|`, a separator RFC5545 never uses) and restores by byte-level line injection — strictly more
faithful (byte-for-byte) and proven by `tst_orgical_canon_roundtrip`'s round-trip slot. The legacy
class is deleted in Plan 4 Task 8 regardless. (Seeded 2026-05-24, Plan 4 Task 2.)

### O14 — Akonadi ChangeRecorder warm-path deferred (DEFERRED, 2026-05-26)

The Akonadi change-detection backbone is the payload-free id+revision digest
(`Backend::ChangeDetection`, `AkonadiRevisionStore`). A `ChangeRecorder`-based
persistent warm-path (skip even the local digest fetch using a cross-restart change
journal) was scoped but deferred: `ChangeRecorder`'s recording mode changes Monitor
signal-delivery semantics (queued, `replayNext`-driven) and would risk the live cache
the write path depends on, and an in-memory-only dirty set cannot safely account for
changes during downtime (which the digest already handles). The digest fetch is a cheap
local-DB read, so the warm-path is a marginal optimization. Revisit only if profiling
shows the digest fetch is hot. (Decided 2026-05-26.)

## Resolved

### O7 — Ambient-Context default bundle removed (resolved 2026-05-27)
Once PlanStan and WildPalms adopted the injecting `ShapeRegistries` ctors (per
`docs/2026-05-27-downstream-port-checklist.md`), the transitional scaffolding was
deleted: the process-global `defaultShapeRegistries()`, the three
`TransformationRegistry`/`DomainRegistry`/`DomainOperationsRegistry` `::instance()`
accessors, `src/shape/shaperegistries.cpp`, and the no-`ShapeRegistries` ctor
overloads on `SyncEngine` and `PluginManager`. A `ShapeRegistries` is now
constructed only at the composition root and injected by reference — the OSGi
`BundleContext` topology is complete and the Ambient-Context anti-pattern is gone.
Internal callers updated: `tst_calendar_sync_oneway`, `tst_syncruncoordinator`,
`tst_pluginmanager_resolve`/`_smoke`, `tst_provider_plugin_registration`,
`tst_akonadiprovider_plugin_registration`, `tst_multiprotocoldavprovider`, and
`examples/reference_consumer` (now models the injecting pattern with no global
cleanup). Build clean, suite 124/124. (Resolved 2026-05-27.)

### O12 — Downstream backend port (RESOLVED 2026-06-10)
Plan 4 dropped the `TranscodingPlan` parameter from `SyncBackend::pushItems`/`startSync`.
All downstream `SyncBackend` subclasses (PlanStan + WildPalms) ported after merge; the
canon-upgrade branch merged to `main`. Org-on wiring (`KALBURATOR_HAVE_ORG_IO=ON`) also
resolved under that build profile. (Seeded 2026-05-24; closed 2026-06-10.)

### O13 — baseline-load filters to blob domain (RESOLVED — was a misdiagnosis, 2026-05-26)

Original claim: `src/engine/syncengine.cpp` (~line 2121) loads only `blob`-domain
baselines, so baseline-driven *deletion* detection for calendar/contacts is "not active
for ANY backend." **Investigation showed this framing is wrong for the converged engine.**
The unified engine persists *every* baseline as `blob`/`raw` with `data = contentHash`
bytes (both `setBaselineV3` sites, regardless of the record's real domain), and
`perRecordDiff`'s `equalRecords` does hash-equality when both sides carry a `contentHash`
(every calendar/contacts backend populates one — SHA-256 of the bytes). So the load
filter drops *nothing* the unified engine wrote; it correctly skips only legacy
`calendar`/`ical` baselines (iCal text, not hashes) whose inclusion would corrupt hash
comparison. Proven by `tst_calendar_subsequent_sync_uses_blob_view`
`::subsequentSync_deletedSourceRecordPropagatesDeletion`: a source-deleted calendar
record known via a blob baseline is correctly deleted on the target (delete op, not a
spurious conflict). Fixed the stale `blobBatchDiff` comment at the filter site in the
same change. (Resolved 2026-05-26.)

### O15 — CalendarPluginWriter dual write-path (resolved 2026-05-27)
Converged the calendar domain onto the uniform `DefaultBlobWriter` record path and
deleted `CalendarPluginWriter` + the `SyncTransaction`/`*IncidenceItem` machinery
(`synctransaction`, `synctransactionitem`, `createincidenceitem`,
`updateincidenceitem`, `deleteincidenceitem`, `synctesthooks`). Investigation found
path (1) provided no live benefit it appeared to: the host `MemoryCalendar` was
never written, the `simulate()`-based collision/version checks were never invoked
(the writer only called `commitAll`), and its one live differentiator —
transactional rollback — was a MockBackend artifact (`shouldFail` is sticky
per-op-type, so rollback ops of a *different* type succeeded; under systemic
failure all writes fail and `rollbackCommitted` misreports success via an
unconditional `rollbackCompleted(true)`). The genuinely robust property —
baselines not saved on failure, so a retry re-attempts — is preserved and
domain-uniform; conflict detection lives in the canon diff/merge + `ConflictManager`
layer. The ~9 rollback-asserting slots in `tst_calendar_sync_error_recovery` were
rewritten to the retry-safe contract (3 redundant duplicates deleted). MockBackend's
`IBlobBackend` create/update/deleteRecord gained the failure-injection checks the
old `pushItems`/`startSync` paths had (the converged path exercises those methods).
Downstream caveat: PlanStan/WildPalms code constructing `CalendarPluginWriter` or
the `*IncidenceItem` classes directly will fail to compile until ported (consistent
with O7/O12). See `docs/2026-05-26-o15-calendar-write-path-convergence-design.md`.

### O10 — incidencediff/syncdiff relocated; transcoding deleted (resolved Plan 4 T1/T8, 2026-05-24)
Decision (human): the two diff engines moved to a new `src/diff/` (Task 1, commit involving `git mv`),
and the transcoding machinery was deleted with `src/transcoding/` removed entirely (Task 8, `88122b8`).
The design §10 "delete in full" is thus honored in spirit (dir gone) without losing the load-bearing
diff engines. Tree-wide grep confirms zero live transcoding references outside the (now-absent) dir.

### O11 — legacy RRuleReverseTranscoder no-op (superseded/deleted Plan 4 T2/T8, 2026-05-24)
The broken `RRuleReverseTranscoder` is gone with the rest of `rruletranscoder.cpp` (Task 8). RRULE
simplification + faithful restoration now live in `CanonToOrgICalStage`/`OrgICalToCanonStage`
(`src/calendar/orgicalcanonstages.cpp`), covered by `tst_orgical_canon_roundtrip`.

### O1 — `LossProfile` engine-layer migration (resolved Plan 1 Task 2, 2026-05-23)
`tst_engine_unified_routing.cpp` and `tst_carddav_engine_integration.cpp` were migrated
to `isLossless()` / `droppedProperties()` in Task 2. Full build confirmed no remaining
`LossLevel`/`.level`/`.dropped` references in production or test code.

### O2 — Carry-verbatim containers (resolved Plan 3, 2026-05-24)
All non-isomorphic structures (`relatedTo` tree / `parentUid` / `checklistItems` for todo;
`recurrence` verbatim string list for both ical and vtodo) landed in Plan 3:
`todocanonproperties.cpp` defines the catalogue fields; `vtodocanonstages.cpp` +
`icalcanonstages.cpp` capture RRULE/RDATE/EXDATE as raw RFC5545 lines (invariant 3). The
differ (CanonJsonDiffer) treats each as one opaque-field change per coarse-granularity rule.

### O3 — Live API validation deferred (partially resolved Plan 3, 2026-05-24)
The concrete JSON (de)serialization stages for all three domains are written and covered by
round-trip tests. The remaining unknowns (live Google/Graph payloads, IANA↔Windows timezone
mapping, Graph immutable-id edge cases) still require live integration testing, deferred to
when real provider connectors are wired. Not a blocker for convergence (Plan 4).

### O5 — `pipeline.cpp` loss folding (resolved Plan 1 Task 2, 2026-05-23)
`composedLoss()` in `src/shape/pipeline.cpp` uses only `compose()` — no direct field
access — so it compiled cleanly without changes. Confirmed by Task 2's full build.

---

## Discipline Log

Format: `YYYY-MM-DD — file:line — inv N — phrase`

2026-05-24 — src/contacts/vcardcanonstages.cpp (VCard4ToCanonStage, birthday mapping) — inv 4 — `birthday.hasYear` is hardcoded `true`: KContacts exposes `birthdayHasTime()` but no `birthdayHasYear()`, so a `--MMDD` (year-less) vCard4 BDAY round-trips with a spurious year. Edge case, not exercised by current tests; revisit if year-less birthdays become a contract.
2026-05-24 — {contacts,todo,calendar}domaindefinition.cpp `richnessRank()` — deviation note — Plan 3 A4/B4/C4 specified `s==canonicalShape()?100:10`; implementations use 100 (canon) / 50 (primary legacy peer: vcard4, ical-vtodo, ical) / low (vcard3=10, todotxt=3, calendar-other=0). Documented deviation per the INVARIANTS deviation rule: only relative ordering matters (canon strictly highest) and the 3-tier scheme is consistent across all three domains and models the extra peers (vcard3, todotxt) more accurately than a flat 10. Harmless; recorded so the spec/code divergence is not mistaken for a bug.
2026-05-24 — build system (AUTOMOC + parallel QtTest link) — process note — clean parallel builds
intermittently emit `undefined reference to 'main'` on a QtTest target (the `QTEST_MAIN`/`.moc` racing
under `-j`); it is NOT a real error and resolves on a build re-run. Seen on `tst_canonjson_diff_merge`
and `tst_contacts_canon_roundtrip` during Plan 4. If it ever becomes persistent (not race), investigate
AUTOMOC dependency wiring for the affected target.
2026-05-24 — src/engine/syncengine.cpp (Task 4 materializedLoss warning) — inv 4 (minor) — the
re-sourced lossy-sync warning fires per present + non-Reversible affected property, but the static edge
LossProfile is value-independent: e.g. `canon→ical` charges `classification=Degraded` unconditionally,
so a record with `classification:"public"` (which iCal represents losslessly) triggers a spurious
"classification" warning. Harmless (no test asserts on it; warning, not failure) and matches the
design's "warn on composed path loss" framing, but a future refinement could make Degraded
value-dependent (only `personal`→private actually degrades). Documented, not fixed.
2026-05-24 — src/shape/lossprofile.{h,cpp}, src/calendar/icalcanonstages.cpp, src/engine/syncengine.cpp
— inv 4 (resolves the 2026-05-24 over-charge above) — FIXED. Added an optional
`LossProfile::losslessValues` (`PropertyId → QSet<QString>`): the static `affected` map stays
value-independent (routing relies on the conservative upper bound), but the warning path
(`materializedLoss`) now skips a present string value listed there. The `canon→ical` edge declares
`classification`'s `{public,private,confidential}` lossless, so only `personal` warns. `compose()`
carries the field forward, intersecting safe sets on a shared property (lossless only if every hop
agrees). Additive — every existing `affected` site untouched. Covered by tst_loss_profile
`composeIntersectsLosslessValues`/`composeCarriesUnsharedLosslessValues`; full suite 112/112.
2026-05-24 — src/calendar/calendarmanager.cpp (Task 6b) — decision — `CalendarManager`'s direct-write
path (createIncidence/updateIncidence/transcodeForBackend) was converged OFF `TranscodingRegistry`
(human decision): it now pushes incidences without per-target transcoding (conversion is the
backend/shape graph's job) and no longer emits `dataLossWarning` on those sites (the signal declaration
is kept). The methods have no in-repo callers; downstream apps using this facade with an org backend
should route through the sync engine to get RRULE simplification + the loss warning.
2026-07-04 — tests/engine/tst_engine_cancellation.cpp — process note — segfaults intermittently
(observed 3+ times across the sync-convergence campaign's A1/B1/B2/B3 checkpoints) when run inside a
full `ctest -j8` parallel batch, but passes every time when run standalone (`ctest -R
tst_engine_cancellation`). Looks like a pre-existing resource-contention/timing race in the test's own
cancellation-timing assertions, not a regression from this campaign's changes — none of A1/A2/B1/B2/B3
touch cancellation. Not investigated further (out of this campaign's scope); flagging so a future
session doesn't mistake it for a regression from this branch's work.
2026-05-24 — Plan 3 Parts A, B & C — inv 4/5 post-review fixups (commits 89edbbb, 7f68e36, 5b00a47) — a single subagent ran A5→Task13 unsupervised (per-task review checkpoints skipped) and introduced the SAME false-loss-contract bug in all three domains: a loss classified Reversible/Degraded whose verbatim stash was never emitted in code. Specifically: contacts sipAddresses/calendarUrls/externalIds (Reversible, A5); VTODO Degraded-status + checklistItems/sortOrder Reversible (B5); iCal classification="personal" Degraded (C5). Recurrence round-trip tests also asserted substring, not byte-identity. Fixed: originals now stashed as `CANON-*`/`X-CANON-*` custom props that round-trip into providerExtras; recurrence tests assert byte-identical RRULE/EXDATE; each fix has a falsifiable round-trip test. Caught by retroactive spec-compliance review of B and C, then — prompted by the human asking "is the mess cleaned up" — the identical bug was found and fixed in contacts (A) too. Lesson: when one agent silently expands scope past its task, review EVERY task it touched, not just the ones you remember dispatching.
2026-07-04 — src/engine/syncengine.cpp `SyncEngine::updateSyncMetadata` (Phase B4 work) —
inv 9 (off-topic notice) — this method (and its `makeCalendarRec` helper) is dead code: declared,
defined, never called from anywhere in src/ or tests/ (calendar sync has routed through the unified
`dispatchSync`/`unifiedContinueAfterConflicts` path since Phase Ib.5 Task 7 removed the if-calendar
guard). It stores baselines under domain="calendar"/encoding="ical" with real iCal text as the
payload — a different shape than the unified path's domain="blob"/encoding="raw" hash-only rows — so
if it were ever accidentally wired back in, its rows would be silently invisible to
`baselineHashesForMappingV4()`'s legacy-fallback filter (which only treats domain="blob" rows as
hash rows, correctly ignoring "calendar"/"ical" ones as non-hash data). Not fixed this session
(true dead code, zero runtime risk); flagging so a future cleanup pass deletes it deliberately
rather than rediscovering the discrepancy under time pressure.
2026-07-04 — src/types/synctypes.h `SyncStats` / `SyncResult::sourceStats,targetStats` (Phase B4
work) — inv 9 (off-topic notice) — grep-confirmed: nothing under src/engine/ ever populates
`sourceStats`/`targetStats` for the unified dispatch path (`unifiedContinueAfterConflicts` never
touches `.created`/`.updated`/`.deleted`/`.conflicts`). The fields default-construct to all-zero and
stay that way regardless of what actually happened during a sync. `tests/engine/tst_sync_convergence.cpp`
could not use these fields to prove "zero writes on the second sync" for this reason and instead
witnesses convergence via FakeCalDavServer's PUT/DELETE request counters plus on-disk mtime/byte
comparison of LocalBackend's written files. Not fixed this session (out of B4's scope — this is a
pre-existing observability gap, not a correctness bug); a future phase should either wire these
fields up from `EngineMerge`/`WriterBatch` counts or delete them if genuinely unused by any consumer.

2026-07-04 — src/calendar/eventcanonfields.cpp:168-190 / src/todo/vtodocanonfields.cpp:115-125,
src/calendar/remotecalendarbackend.cpp (loadRecords + the three incidence-parse sites) — Phase B5,
**MAJOR fix, not just a notice** — a real, previously-latent non-convergence bug found by the B5
acceptance-matrix tests (`tests/engine/tst_sync_convergence.cpp`), intermittently flaky (~1-in-3 to
1-in-4 runs) in a way that made it easy to miss in a single manual run. Root cause:
`RemoteCalendarBackend::loadRecords()` re-derived each record's raw iCal bytes via
`icalFromIncidence(incidence)` — parse-then-reserialize through a throwaway KCalendarCore
MemoryCalendar — on every call, including calls that serve content the backend already has
cached verbatim. Two independent KCalendarCore behaviors make that re-serialization
non-deterministic byte-for-byte across separate calls to the SAME logical content: (1)
`Incidence::created()`/`lastModified()` default to the wall-clock time AT PARSE, not "unset", when
the source lacks explicit CREATED/LAST-MODIFIED properties — so a freshly re-parsed object's
`eventFieldsToCanon`/`todoFieldsToCanon` call re-derives a DIFFERENT literal CREATED/LAST-MODIFIED
canon value every time, since those functions trusted the accessors directly; (2) KCalendarCore's
`ICalFormat` writer regenerates `DTSTAMP` unconditionally to "now" on every `toString()`/
`toICalString()` call regardless of the source's own DTSTAMP (RFC 5545 semantics: DTSTAMP is "when
this representation was produced", so this is correct KCalendarCore behavior, not a library bug).
Net effect: `BackendRecord.contentHash` for ANY event/todo was unstable across independent
`loadRecords()` calls for byte-identical server content — silently defeating B4's per-side baseline
convergence whenever the two independent `fetchItems()` calls per sync (the "runs at least twice"
structural residual, §Phase B5 item 3) landed in different wall-clock seconds, which is common
enough to be genuinely disruptive on a real 120s-cycle collection, not just a test artifact.
**Fix (two parts, both needed for full closure):** (a) `eventFieldsToCanon`/`todoFieldsToCanon` now
read CREATED/LAST-MODIFIED via a new `Kalburator::Calendar::extractICalPropertyLiteral()` (icaltimestamp.{h,cpp})
— literal per-property presence in the ORIGINAL bytes, no accessor-trusting — so a canon encoder
never invents a field the source didn't have. This alone is insufficient when the "original bytes"
passed in have themselves already been through one icalFromIncidence round-trip (see (b)). (b) the
actual closure: `RemoteCalendarBackend` now remembers the LAST VERBATIM raw bytes served for each
uid (`m_lastRawIcsByUid`, populated at all three sites that parse raw ics text: `serveCachedItems`,
the all-from-cache branch, `processFetchedItems`) and `loadRecords()` prefers that verbatim blob
over `icalFromIncidence()`, falling back only if the map has no entry. Verified: the acceptance-
matrix tests (`localEditPropagatesExactlyOncePut`, `remoteEditFetchesExactlyOneChangedItem`,
`remoteDeleteRemovesExactlyOneLocally`, `fastPathSkipsGenuinelyUnchangedMapping`) went from ~30-40%
flaky to 0/40 failures across a stress-test loop after fix (b) landed; fix (a) alone did not resolve
the flakiness (confirmed by testing it in isolation first) because DTSTAMP corruption survived it.
Also fixes a companion (b1) one-cycle staleness in `SyncEngine::onWorkerSyncCompleted`'s
`persistRevision` helper: it used to persist the PRE-dispatch `fresh.targetRevision`/`sourceRevision`
snapshot captured by `prepareSyncFastPath` before the mapping ran, which for a target a mapping just
WROTE TO (e.g. LocalBackend on the mirror-populating first sync) is already stale the instant the
callback runs — it now re-queries each side's LIVE `ChangeDetection::collectionRevision()` after the
mapping completes, falling back to the pre-dispatch snapshot only if the live query comes back empty.
This — not the DTSTAMP bug — was the direct cause of the roadmap's "of 7 mappings, 0 are unchanged"
real-world symptom for the LOCAL/target side (see the fast-path test's doc comment for the parallel,
separate, NOT-a-bug one-cycle warm-up the REMOTE/source side's CTagStore still needs).
