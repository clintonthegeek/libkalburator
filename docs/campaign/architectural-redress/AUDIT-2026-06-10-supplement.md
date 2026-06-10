# Audit supplement — 2026-06-10 (post-v0.66 tree, verified)

> **Companion to `AUDIT.md` (2026-05-29 verified rebuild), not a replacement.** That audit
> remains the campaign's source of truth for everything it covers; this supplement audits
> what grew AFTER its rebaseline — the consumer-driven releases v0.61–v0.67 and the
> ProviderConfigDialog/calendarsOnly wave — plus dimensions the campaign tracks loosely
> (doc truth, dead code, test gaps, consumer integration). Method: five parallel read-only
> agents (architecture, dead code, docs, tests, consumers, naming), every finding verified
> against source at `main` (then) `f170139` with file:line + verbatim quote; the CRITICAL
> was independently re-verified and fixed the same day. Where this supplement and AUDIT.md
> disagree on figures, this supplement is newer; where it disagrees with reality, write a
> FINDINGS entry and correct it in the same commit.
>
> **Implementation sequencing for everything below:** `2026-06-10-audit-follow-up-specs.md`
> (work packages WP-A … WP-F).

## Fixed same-day (no longer open; listed for the record)

- **CRITICAL C1 — `calendarsOnly` ctor-argument swallow.** Both contributions called
  `make_unique<Provider>(parent)` against `(bool calendarsOnly = true, QObject *parent)`
  ctors: the `QObject*` bound to the bool (pointer→bool), the parent was dropped, and the
  provider-side mode never engaged (the §4.3 dialog filter masked it; `5c063c1` was
  treating the symptom). Fixed behavior-preserving — `(false, parent)` explicit — at
  `fac8522` (merged `b47d75e`) with a pinning test
  (`tst_multiprotocoldavprovider::contributionCreateProviderHonorsParent`). Verified under
  both default and `KALBURATOR_HAVE_AKONADI=ON` profiles. **Per-account mode selection is
  an open design decision → WP-A1.**
- **`tst_providerconfigdialog::failedTestConnection_surfacesErrorMessage` red since the
  §4.4 polish** (pre-polish test asserted raw error in the status label; §4.4 moved it to
  the Details disclosure). Realigned to the §4.4 contract at `d1e924b`. Suite 137/137.
- **v0.67 fixes** (same wave, see `2026-06-10-v067-response.md`): CollectionInfo
  contentTypes (`1a48258`), pre-connected provider registration (`9584f2f`).

## MAJOR (open)

| # | Finding | Evidence anchor |
|---|---------|-----------------|
| S1 | **SyncEngine regrowth: 2846 → 2933 LOC** (.h 642). v0.63–v0.67 added LWW consumption, clobber orchestration (`m_queueOverride`, wipe block syncengine.cpp:2175-2191), 10 registry-lookup sites. Plan-1 decomposition trend has reversed; clobber/LWW/fast-path are collaborator candidates. | FINDINGS inv-4 watch (2915→2933) |
| S2 | **Discovery→provider→backend duplication worsened.** One href can live in 5 maps; capability data has 4 representations (`PerCalendarCapabilities` bools / `contentTypes` QStringList / KDAV bitmask / `CalendarType`); `contentTypesFromCaps` duplicated verbatim in caldavprovider.cpp + multiprotocoldavprovider.cpp. | multiprotocoldavprovider.h:85,98-102 |
| S3 | **Calendars-only policy lives in three layers that disagree** (provider mode, dialog re-filter providerconfigdialog.cpp:313, registry registration providermanager.cpp:256 registering ALL collections). Post-C1-fix the dialog filter is still the only active one. | →WP-A1 design |
| S4 | **RemoteCalendarBackend (Plan 7 target): 2718/472, ~61 methods, 13 stateful containers; state triplicated per axis** (CTags×3, ETags×3, URLs×3). v0.63 primer layered on top of, not into, the discovery surface. Does NOT override `wipeCollection` (clobber = per-item DELETE over CalDAV). Full concern inventory in the specs doc → Plan 7 replan inputs. | remotecalendarbackend.h:113-245,386-415 |
| S5 | **Component-capability concept has six spellings across five types** (contentTypes QStringList / KDAV ContentTypes / supportedComponentTypes / supportsV* bool triples ×3 incl. exact-duplicate `DiscoveredCalendar`). v0.67 added spelling #1 on top. | collectioninfo.h:21; discoveredcalendar.h:28-30; backendconfiguration.h:22-47 |
| S6 | **IProvider error/warning channels are transport-asymmetric and the failure contract diverges per implementation**: error = push-only signal (no `lastError()`), warning = pull-only accessor (no signal, only MultiProto implements); CalDav/Akonadi never emit `connectionStateChanged(false)` on connect failure while MultiProto always does — contradicting iprovider.h:153 docs; future-finish vs signal-emit ordering flips between implementations. | iprovider.h:150,165; caldavprovider.cpp:131-144; multiprotocoldavprovider.cpp:344-349 |
| S7 | **Dead calendar-typed includes survive in the neutralized core**: providermanager.cpp:7 + syncengine.cpp:30 `#include "syncbackend.h"` (KCalendarCore into sync/+engine/ for nothing — `SyncBackend` appears only in comments); dead fwd-decls syncengine.h:46/74, syncengine_p.h:44/63. | grep-verified |
| S8 | **Doc-comment lies in public headers**: synctypes.h:205 names nonexistent `ISyncHost::transcodingWarning`; enginediff.h:17 + propertydiff.h:24 cite deleted `src/transcoding/`; syncengine.h:116 names PlanStan's `KalbConfigManager`; iprovider.h:101 contradicts the iproviderconfigwidget.h:17 bridge contract; collectioninfo.h:9 "Unchanged from WP donor shape" above three post-donation fields. | →WP-B |
| S9 | **Root CLAUDE.md actively misleads fresh sessions** (worst: instructs using deleted `TranscodingRegistry`, deleted `storeItems`/`TranscodingPlan` write API, nonexistent `SyncStore::setBaseline`, outdated runSyncFuture guidance, dead `~/dev/refactor-engine-merger/` section). Worst bullets fixed 2026-06-10 (same commit as this supplement); full sweep →WP-B. | doc agent, verified |
| S10 | **Test gaps with teeth** (full list →WP-D): v0.61 in-flight connect idempotency untested in all four providers (the actual SIGSEGV path); `ConflictResolution::Duplicate` zero tests, TargetWins/Skip never engine-exercised; `wipeCollection` untested on every concrete backend; Akonadi+Org backends contribute 0 of 137 green tests (gated off; 2.7k LOC dark); incidencelock_registry (225 LOC) zero coverage; discovery/ (519 LOC) zero coverage; SyncEngine config APIs still untested (AUDIT gap, still open). | test agent, verified anchors |

## MODERATE (open, selected)

- **ProviderConfigDialog is becoming the add-account orchestrator** (instantiates
  providers, drives connect, captures errors, filters collections, builds panels, hands
  ownership out via `takeProvider`); the v0.67 preconnected fix completes a distributed
  dialog↔manager↔provider handshake with no single owner. Watch; candidates: extract an
  AddAccountFlow collaborator. (providerconfigdialog.cpp:199-434)
- **Destructive-verb sprawl**: `wipeCollection` ≡ `clearCollection` (GenericSqlite) but
  neither GenericSqlite nor RawFiles overrides `wipeCollection` — the fast path with the
  exact semantics sits one name away; `deleteCollection` + engine `clobber` complete four
  verbs; `BaselineStore::clearCollection` reuses the verb for a third domain. (→WP-A6 the
  override; →Plan 10 the naming)
- **Three "seed before use" verbs on RemoteCalendarBackend** (`registerCalendarUrl` /
  `primeCtagCache` 60s / `primeCalendars` permanent) + cache-vs-store naming inversion
  (`CTagStore` persistent, "content cache" persistent, ctag "cache" in-memory). (→Plan 7/10)
- **`MultiProtocolDavProvider::m_connectPromise` is `shared_ptr` while siblings use
  `unique_ptr`** — inconsistent ownership idiom in one family.
- **PlanStan test CMake reaches into `${LIBKALBURATOR_SOURCE_ROOT}/src/*` (31 include-dir
  reach-ins) including the DELETED `src/transcoding`** (tests/backends/CMakeLists.txt:172)
  — consumer-side, but the lib should export a test-support target. (→RFC proposals)
- **libkalcal standalone pins v0.64 while in-tree inherits PlanStan's v0.66-pdp** —
  two-version skew between build modes.

## Dead code (adjudicated; removal mechanics →WP-C)

SAFE-REMOVE: `src/journal/baselinestore.{h,cpp}` (zero refs anywhere + include-shadow
hazard vs storage/baselinestore.h); `src/todo/icalvtodomerger.{h,cpp}` (`RecordMergerVTodo`
— zero refs incl. own domain, which registers `CanonJsonMerger`); stale CMake shim comment
(CMakeLists.txt:252); `.claude/scheduled_tasks.lock` is git-tracked (convention violation).
PROBABLE: BaselineStore v2 deprecated surface (0 production callers in all three repos;
only its own tests + PlanStan tst_syncstore — coordinate); `incidencesyncadapter.h` +
`isyncrecord.h`; `resourcelinearization.h` (decisions now overdue — AUDIT flagged them
2026-05-29, untouched since). NEEDS-DECISION (test-only alive): IcsFeedFetcher,
LogicalCalendarBuilder, DecSyncGarbageCollector (PlanStan test dep), NeutralProvider,
LocalBlobBackend (reference impl? say so). NOT dead (re-verified): discovery/ (PlanStan),
IIncidenceSource, FilteredCollection/MarkdownFiles (WildPalms), HAVE_* flags all live.
Deprecated-API liveness: `runSyncFuture` heavily alive (1 lib prod + 87 lib-test + 11
PlanStan-test + **2 WildPalms PROD** sites — Plan 8 is migration work, not deletion);
`recurrenceCapabilities` has exactly 1 PlanStan caller.

## Campaign-state corrections (apply to STATUS.md)

- PlanStan relink `69e7df90` **is pushed**; pin = `v0.66-provider-dialog-polish` (pushed).
- libkalcal `b4ef4ae0` **is pushed** (stale local tracking refs caused the "unpushed" claim).
- WildPalms: pins v0.65→v0.66 committed locally but **origin/main is 96 commits behind**
  (origin still pins raw hash `948dce88`); `3afc074` only on origin/feature/three-tier-sync.
- Plan 8 blocker inverted by consumer data: WildPalms has **0** `backendById` lookups (2
  forced overrides); **PlanStan has ~20 call/override sites** — migration must be
  PlanStan-first, and `runSyncFuture` deletion must migrate WildPalms `palmruntime.cpp`
  (2 prod sites) first.
- Consumer RFC clusters (4 rounds on discovery/CollectionInfo; 3 on backend identity; 3+
  on write-path semantics) mark the weakest abstractions — see specs §RFC proposals.

## Confirmed still clean (spot-checks)

`types/` purity gate holds through v0.67 (`ExecutionOverride::clobber` pure flag);
`shape/`→`conflict/` edges still 0; `wipeCollection` landed on the neutral `IBlobBackend`;
`lastwritewins.h` engine-internal; no new sync/→calendar includes beyond the AUDIT's known
B4-corrected list; no `_p.h`/private leaks in either consumer; WildPalms is the cleanest
consumer of the heavy stack; every `tst_*.cpp` is wired into its CMakeLists (zero orphans);
no tracked scratch files.
