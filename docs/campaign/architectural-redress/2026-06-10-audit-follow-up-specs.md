# Audit follow-up — implementation specs (2026-06-10)

**For the implementing agent.** These work packages sequence everything actionable from
`AUDIT-2026-06-10-supplement.md`. Read `INVARIANTS.md` first (you accept them by working
here), then the supplement, then this file. Evidence anchors live in the supplement —
re-verify each against HEAD before editing (line numbers drift).

**Ground rules** (beyond INVARIANTS): TDD for every behavior change (failing test first);
one concern per commit; full `ctest --test-dir build -j 8` after every task (`-j 8` ONLY —
never `--parallel`, never all-core: GCC ICEs); update FINDINGS.md/STATUS.md in the same
commit that changes plan state; consumer-coordinated items are marked ⚠RFC — write a
handoff doc in `docs/` and STOP for consumer sign-off rather than landing unilaterally.

**Sequence: WP-A → WP-B → WP-C → WP-D → Plan 7 → Plan 8 prep.** A/B/C are each ≈one
session; D is 1–2; they are intentionally front-loaded (correctness > docs > deletion >
coverage) so each later package works on truthful docs and a smaller tree.

---

## WP-A — Correctness & contract quick wins

1. **calendarsOnly mode design** ⚠RFC. Current state after `b47d75e`: contributions pass
   `(false, parent)`; provider ctors still default `calendarsOnly = true` (a trap: any
   direct construction silently differs from registry construction); the §4.3 dialog
   re-filter (providerconfigdialog.cpp:313) and the registry registration
   (providermanager.cpp:256, registers ALL collections) disagree about policy ownership.
   Proposed shape (validate with both consumers before landing): mode becomes a
   `BackendConfiguration.connectionParams["calendarsOnly"]` bool read in `load()`; ctor
   bool defaults flip to `false` (or the param is removed once config-driven); the dialog
   filter stays as UI concern; `registerProviderBackends` honors the provider's effective
   collection set by construction. Regression tests: composed FakeCalDav+FakeCardDav
   harness (see WP-D3) asserting collections() in both modes.
2. **In-flight connect() idempotency tests** — the v0.61 SIGSEGV path is untested in all
   four providers. For CalDav/CardDav/MultiProto: start `connect()` against a hung/slow
   fake (QTcpServer that accepts and stalls, cf. `disconnect_mid_flight_resolves_promise_false`
   tst_caldav_provider.cpp:403), call `connect()` again while in flight, assert the same
   future identity/compatible result, exactly one eventual `connectionStateChanged(true)`,
   no crash. Akonadi variant only under `KALBURATOR_HAVE_AKONADI=ON` if testable headless.
3. **URL pre-validation**: reject scheme-less URLs synchronously in CalDav/CardDav
   `connect()` (`m_serverUrl.scheme().isEmpty()`), then DELETE the two
   `QEXPECT_FAIL("connect() with non-scheme URL ...")` markers
   (tst_caldav_provider.cpp:438-region, tst_carddav_provider.cpp:423-region) so the tests
   assert the strict contract. TDD: flip the expectation first, watch it fail, fix.
4. **Remove dead calendar-typed includes** (supplement S7): providermanager.cpp:7,
   syncengine.cpp:30 `#include "syncbackend.h"`; dead fwd-decls/`using` syncengine.h:46/74,
   syncengine_p.h:44/63. Pure deletion; full rebuild both profiles (`build/`,
   `build-akonadi/`) proves nothing needed them.
5. **Dedup `contentTypesFromCaps`** — two verbatim copies (caldavprovider.cpp:18,
   multiprotocoldavprovider.cpp:20). Home: a small free function next to
   `PerCalendarCapabilities` (src/typesupport/backendconfiguration.h) or a sync/-local
   header; NOT a new mechanism (invariant 1 does not apply — this is a converter, but
   don't grow it into one).
6. **`wipeCollection` overrides for GenericSqlite/RawFiles** mapping to their existing
   `clearCollection` semantics (supplement MODERATE: the fast path exists under another
   name; default base impl is per-record delete). TDD: temp-store wipe test per backend
   (also closes part of WP-D2).
7. **IProvider failure contract** ⚠RFC-lite (affects consumer UIs): decide
   `connectionStateChanged(false)`-on-connect-failure (recommend: never emit on failed
   connect — only on disconnect from connected state — matching CalDav/Akonadi; fix
   MultiProto multiprotocoldavprovider.cpp:349 + iprovider.h:153 doc), document
   signal-vs-future ordering, and add `lastError()` pull accessor symmetric to
   `lastWarning()`. Notify consumers in the next response doc; their dialogs subscribe to
   `error()` already.

## WP-B — Documentation truth sweep (no code)

1. **Root CLAUDE.md**: worst test-guidance lies were hot-fixed 2026-06-10; finish the
   sweep — delete the dead "Refactor-branch worktree" section (`~/dev/refactor-engine-merger/`
   gone); mark the canon-upgrade section's remaining work DONE (O7 resolved 2026-05-27,
   O12 effectively closed, merged to main); "9-plan/N=1..9" → 11-plan; drop the Phase 3b
   instruction (completed 2026-04-20).
2. **Stale src comments** (supplement S8): enginediff.h:17, propertydiff.h:24
   (`src/transcoding/` → `src/diff/`), synctypes.h:205 (`ISyncHost::transcodingWarning` →
   `SyncBackendBase/SyncEngine::transcodingWarning`), syncengine.h:116 (KalbConfigManager →
   "the host's ISyncConfigStore"), iprovider.h:101-103 (widget-calls-load lie → cite
   iproviderconfigwidget.h bridge), collectioninfo.h:9 (drop "Unchanged from WP donor
   shape"), subscriptionbackend.h:39 + mockbackend.cpp:83/247 (storeItems/updateItem →
   pushItems).
3. **Closure banners** on closed handoffs: 2026-06-03-caldav-discovery-redundancy
   (closed by v0.63), 2026-05-27-downstream-port-checklist (ports done),
   2026-05-27-o7-test-regressions (fixed, v0.57), 2026-04-28-honest-assessment (HISTORICAL
   banner — describes pre-F1 architecture + deleted transcoding/).
4. **phase0**: 04b-phase3-status.md self-contradiction (Phase 3b "resolved" vs "next";
   also "in-tree add_subdirectory" → FetchContent+pin reality); 04k roadmap's dead
   `~/dev/refactor-engine-merger/*` pointers + "tag pending" (tag exists).
5. **Campaign docs**: canon-upgrade STATUS "Next action" list (items done) + FINDINGS
   O12 → RESOLVED; redress STATUS.md — apply the supplement's "Campaign-state corrections"
   (pin/push reality), reconcile v0.67 + the 2026-06-10 fixes into "Out-of-campaign
   consumer releases", baseline 136 → 137.
6. **AUDIT.md anchor-drift note**: add a one-line provenance warning that 2026-05-29
   anchors have drifted (worst: syncengine.h ctor :390→:164; G5 :1940; resolved-finding
   anchors now show the FIXED code) — do NOT rewrite the audit.

## WP-C — Dead-code removal

1. Delete `src/journal/baselinestore.{h,cpp}` (zero refs; include-shadow hazard) + stale
   shim comment CMakeLists.txt:252-253. Full rebuild both profiles + consumer gate greps
   (`grep -rn "journal/baselinestore\|QSyncCore::BaselineStore"` in PlanStan/WildPalms = 0
   — re-verify).
2. Delete `src/todo/icalvtodomerger.{h,cpp}` + its two CMakeLists lines (:378/:390 region).
   Re-verify zero refs first (incl. consumers).
3. Untrack `.claude/scheduled_tasks.lock`; add `.claude/` to `.gitignore` (house
   convention).
4. Scaffolding decisions (each = delete or an explicit "retained for X" comment + FINDINGS
   entry; recommendations): REMOVE `incidencesyncadapter.h`+`isyncrecord.h` and
   `resourcelinearization.h` (overdue since 2026-05-29 audit; qsynccore/Palm futures never
   materialized — WildPalms went its own way); RETAIN-with-comment `LocalBlobBackend`
   (reference IBlobBackend impl + test fixture) and `NeutralProvider` (K.7.1 scaffolding,
   schedule a consumer or drop next quarter); DECIDE-with-user IcsFeedFetcher +
   LogicalCalendarBuilder (lib-test-only; subscription/builder features half-shipped);
   DecSyncGarbageCollector ⚠RFC (PlanStan test depends on it).
5. **BaselineStore v2 retirement** ⚠RFC: 0 production callers anywhere; PlanStan
   `tst_syncstore.cpp` (~20 calls) + lib `tst_baseline_store_per_record_keys.cpp` are the
   only users. Handoff to PlanStan: port their test to v3 keys, then delete the 5
   `[[deprecated]]` methods + the lib self-test + the v2 table migration path. AUDIT G8's
   "until all callers migrate" condition is now met.
6. **`recurrenceCapabilities()`** ⚠RFC: exactly 1 caller (PlanStan
   incidenceeditordialog.cpp:588). One-line migration there
   (`backendcapabilities`-equivalent), then delete from syncbackend.h:266 + orgbackend.cpp:48.

## WP-D — Test-gap closure

1. **Conflict-policy matrix through the engine**: `ConflictResolution::Duplicate`
   (implemented syncengine.cpp:1624/:2495, ZERO tests — highest-risk gap: "keep both"
   write path), TargetWins, Skip — one MockBackend engine test each, mirroring the
   SourceWins suites.
2. **Concrete-backend `wipeCollection`** (with WP-A6): LocalBlobBackend temp dir +
   fake-CalDAV RemoteCalendarBackend; assert emptiness + survivor isolation + failure
   reporting.
3. **Composed FakeCalDav+FakeCardDav harness**: unskip
   `connectPartialSuccessSkipped` (tst_multiprotocoldavprovider.cpp:150 — "follow-up"
   never happened) and add calendarsOnly-mode assertions (with WP-A1). Both fakes exist;
   composition = two servers, one provider config.
4. **TypeSupport units**: incidencelock_registry (tryLock/conflict/auto-release-on-owner-
   destroyed — lifetime-sensitive, 225 LOC dark), BackendConfiguration JSON codec
   field-exhaustive round-trip, calendarmetadatamanager basic I/O.
5. **SyncEngine config APIs** (open since 2026-05-29 AUDIT): loadSyncMappings,
   setMappingEnabled, registerActiveController/unregister, setSkipUnchangedMappings,
   hasSyncWork — direct unit tests via StubSyncConfigStore.
6. **discovery/ smoke tests** (519 LOC, zero coverage, PlanStan-load-bearing):
   SyncthingDiscovery/SyncthingMonitor against a fake endpoint.
7. **§4.3 dialog coverage**: 0-calendars → panel visible + Save disabled; happy-path
   `result()`/`selectedCollectionIds()` round-trip.
8. **Cancellation during the v0.63 unprimed fallback** (delayed fake PROPFIND +
   `future.cancel()`; assert clean finish, content-cache integrity).
9. **QSKIP revival**: tst_remotecalendarbackend_blob_view.cpp:75/82 claim missing fake
   PUT support — fakecaldavserver.cpp:258 has handled PUT (with ETags :324) since the
   convergence work; revive both bodies.
10. **Akonadi/Org dark-coverage decision**: 11 test sources exist but are gated out of
    the default profile (2.7k LOC of backends contribute 0 green tests). Either a
    documented periodic `build-akonadi` + `KALBURATOR_HAVE_ORG_IO=ON` lane (a
    `docs/perf/`-style runbook + FINDINGS watch item), or an explicit accepted-risk note
    in STATUS. Recommend the lane: `build-akonadi/` already exists and built clean today.

## Plan 7 (replanned) — RemoteCalendarBackend decomposition

Inputs are now current (supplement S4): 2718/472 LOC, ~61 public methods + 3 signals, 13
stateful containers, state triplication CTags×3 (`CTagStore m_ctags` persistent /
`m_calendarCtags` discovery / `m_primedCtags` 60s) — ETags×3 (`m_etagCache` KDAV /
`m_localEtags` / `m_etags`) — URLs×3 (`m_davUrls` / `m_configuredCollectionUrls` /
`m_itemUrls`). Concern inventory for the collaborator split: (1) credentials/configured
URL; (2) discovery & primed replay — `PrimedCalendar` is the seed of the AUDIT's proposed
`DiscoveredCalendarInfo` DTO (folds the 7 `discoveredX` getters); (3) ctag
change-detection (4 `ChangeDetection` delegations); (4) SQLite content cache (6 methods);
(5) etag/item-URL bookkeeping; (6) calendar CRUD (MKCALENDAR/PROPPATCH/DELETE);
(7) operations API + IBlobBackend impl + the 11× QEventLoop wait boilerplate (extract
`sendCustomRequestSync` — AUDIT MAJOR); (8) legacy `storeCalendars`/`startSync`/`removeItem`.
Constraints: preserve `tst_remotecalendarbackend_convergence` + blob_view contracts
verbatim; primer short-circuit semantics are consumer-load-bearing (v0.63); write the
task-level plan as `plans/plan-7-remotecalendarbackend-decomposition.md` AFTER WP-A lands
(invariant P1: next-plan-only detail; WP-A6 touches this class).

## Plan 8 prep (sequencing corrected by consumer data)

The audit inverted the assumed blocker: WildPalms has ZERO `backendById` lookups (two
forced pure-virtual overrides only); **PlanStan has ~20 call/override sites** and is the
real migration. Order: (1) make `ISyncHost::backendById`/`backends()` non-pure with a
`BackendRegistry`-backed default (lib-side, non-breaking); (2) ⚠RFC PlanStan migrates its
~20 sites + WildPalms deletes its 2 shim overrides; (3) `runSyncFuture` deletion requires
migrating WildPalms `palmruntime.cpp:916/:1031` (PROD) and PlanStan's 11 test sites to
`runSync(SyncRequest)` + the lib's own `syncruncoordinator.cpp:60` + 87 lib-test sites +
`examples/reference_consumer`; only then delete the four overloads. This is a consumer
wave, not a lib-only plan — open with a joint handoff doc.

## RFC proposals to draft for consumers (not unilateral lib work)

1. **CollectionInfo capability query** — both consumers re-derive domain matching
   (WildPalms domainfilter.cpp, PlanStan discovery helpers); contentTypes (v0.67) fixed
   the data, not the API. Propose `bool supportsComponent(QStringView)` /
   `Domain domainHint()` on CollectionInfo + documented closed sets for `type`.
2. **Engine-assembly facade** — both hosts hand-wire Registry+BaselineStore+ConflictStore+
   SyncEngine (PlanStan collectioncontroller.cpp:1611-1726; WildPalms palmruntime ~158ff);
   the recipe drifts and produced the preconnected/lifecycle RFCs. Propose a
   `SyncSessionBuilder` (DI-style, no singletons — invariant).
3. **Exported test-support target** — PlanStan's 31 source-tree include reach-ins (incl.
   deleted `src/transcoding`) should become `Kalburator::TestSupport` exporting the fakes
   (FakeCalDavServer etc. are consumer-useful).
4. **Pin hygiene** — libkalcal standalone v0.64 vs in-tree v0.66-pdp skew; WildPalms
   origin/main 96 commits behind with pin bumps unpushed.
