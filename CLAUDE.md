# libkalburator — Claude instructions

## GraphCLI / vendor-convergence experiment (OPENED 2026-08-23)

Phase 0 of the **vendor-convergence (EEE) campaign** is live: `tools/graphcli/`
(opt-in CMake option `KALBURATOR_BUILD_GRAPHCLI`) is a Qt6 console lab that
authenticates to Microsoft Graph via device-code flow against a personal
Outlook.com account and captures real payloads. Everything machine-local
lives in gitignored `msgraph/` (credentials in `GraphCLIinfo.md` — never
commit; `token-cache.json`; `captured/*.json` = the Phase-0 golden corpus;
`general_plan.md` = the auth/architecture research input). Scenario matrix:
`tools/graphcli/corpus-sweep.sh list`; bulk cleanup: `graphcli sweep-clean`.
First findings: FINDINGS **O57** (Graph payload realities vs our reference
doc — notably: default `/events` listing returns series MASTERS ONLY, so any
Graph backend needs `calendarview`/`instances`; events carry top-level `uid`
= `iCalUId`; plus three addenda from live two-account iTIP experimentation:
delivery-path-dependent attendant ingestion, consumer-account RSVP limits,
and attendee alias expansion (t) that makes naive email-matching of
attendees non-convergent — **consumer-relevant, see coordination page §2d**). Roadmap placement + the pinned Microsoft-Graph-backend design:
`docs/2026-08-22-campaign-proposal-vendor-convergence-eee.md` (§Status,
Phase 7); readiness context:
`docs/2026-08-22-canon-domains-and-cross-format-readiness.md`. Consumers are
unaffected (additive-only when it lands). Do not rebuild this tooling from
scratch — read the proposal's Status section first.

## EEE campaign — current state (CONSOLIDATED 2026-08-23; history in git + STATUS.md)

All work below is on `main`, PUSHED to origin (through 9d34fd1 + docs).
Campaign status page: **`docs/campaign/eee/STATUS.md`** (snapshot table,
ordered next actions, findings index). Findings: **O57–O62** in
`docs/campaign/FINDINGS.md`.

**Landed, in build order:**

- Phase 0 (corpus): Google + Graph corpora captured live; sanitized
  fixtures committed under `tests/fixtures/vendor/{google,microsoft}/`
  (generators: `tools/googlecli/make-fixtures.py`,
  `tools/graphcli/make-fixtures.py` — note the @odata.context rewrite pass;
  key-driven rules alone leak the Exchange identity).
- Phase 2 (google-event ⇄ canon) TAGGED v1.02, live checkpoint PASSED
  (G→C→G diffs = 4, all declared normalizations).
- Stage D mock Graph server (`tests/graph/mockgraphserver.*`) + transport
  foundation `src/graph/graphapiclient.{h,cpp}` (pagination walks, /delta
  steps with typed 410 ResyncRequired; wire nuance: non-empty change pages
  answer nextLink — fixpoint is "empty set + deltaLink").
- Phase 7.B (ms-event ⇄ canon) — THE deep component:
  `recurrencepatternconverter` (MS→RFC5545 lossless; RFC5545→MS carries
  every cannot-represent ruling verbatim), `mseventcanonstages` +
  vendored CLDR zone map (`windowszonesmap.h`, 139 zones), CalendarStockShapes
  at **9 edges**, `tst_ms_event_canon_edge` (11 slots incl. committed-fixture
  promotion). Live checkpoint PASSED (delegated run) and caught a BLOCKING
  stub-invisible bug: sentinel `range.endDate:"0001-01-01"` on numbered
  ranges honored as real UNTIL ⇒ series amputation (O61(a), fixed) plus 3
  stash/passthrough defects (O61(b)-(d)). Offline checkpoint runner:
  `tools/msroundtrip` (promote/demote/roundtrip/canon-compare; exit 1 iff
  UNDECLARED diffs).
- **KEY FINDING O61(e): carriers do NOT survive creates on consumer
  Outlook.com** — Reversible loss class is OFFLINE-only. Any backend write
  path must prefer PATCH over delete+re-create. Also O61(f): uid/iCalUId are
  PER-COPY anchors (regenerate per create) — identity layer input.
- Phase 7.C COMPLETE (`MSGraphCalendarBackend`, Stage-D verified,
  `tst_ms_graph_calendar_backend` 11 slots):
  - Design decision RESOLVED: records carry RAW ms-event wire JSON
    (`nativeShapes={calendar,ms-event}`); the ENGINE promotes via the
    registered edge. Incidence conversion exists only for FetchOperation's
    legacy surface.
  - Delta-driven fetches: initial walk seeds merged cache + resume token;
    later walks upsert changes (@removed ⇒ evict) and report FULL merged
    views; 410 self-heals via fresh initial walk (O42 pattern).
  - Discovery: /me/calendars → calendarDiscovered + collections DTOs
    (VEVENT-only; tasks live in /me/todo/lists); per-calendar event paths
    for reads AND writes.
  - Persistence: `setCacheDir()` atomic JSON — restarted backends present
    the PERSISTED token with no re-listing.
  - Writes POST/PATCH/DELETE sequentially-async; creates bridge ids via
    WriteOperation::idAliases (O55); updates PATCH-in-place per O61(e).
- Phase 3 google-person ⇄ contacts-canon edge LANDED (stub-level): loss
  profile declared first (`docs/2026-08-23-google-person-edge-loss-profile.md`);
  carriers ride Google People `clientData` rows (the resource's only
  extension point; write-back semantics UNVERIFIED until checkpoint);
  uid ⇄ resourceName (per-account anchor); `tst_google_person_canon_edge`
  7 slots incl. promotion of all 72 sanitized fixture connections.

- Phase 3 COMPLETE (`google-person`, `ms-contact`, `google-task`,
  `ms-todotask` edges all landed stub-level, loss profiles declared first):
  - contacts: `mscontactcanonstages` — flat Graph name collapses onto
    names[0], fixed phone/address buckets typed on promote, positional
    primaryEmailAddress ⇒ primary flag, carriers ride `kalburator.canon`
    open extensions (survival UNVERIFIED, O61(e) class);
    ContactsStockShapes at **9 edges**; `tst_ms_contact_canon_edge`
    (7 slots incl. contacts-listing.json fixture promotion).
  - todo: `googletaskcanonstages` (NO carrier channel exists on Tasks —
    unrepresented props declared Dropped honestly) and
    `mstodotaskcanonstages` (recurrence reuses the 7.B converter;
    cannot-represent rulings + EXDATEs ride the open-extension carrier;
    importance⇄priority {low:9,normal:5,high:1}; checklistItems/
    linkedResources = transport, out of edge scope); TodoStockShapes at
    **9 edges**; suites `tst_google_task_canon_edge` (6 slots),
    `tst_ms_todotask_canon_edge` (7 slots incl. unrepresentable-RRULE
    carry drill).
  - Deferred: task-side corpus captures (corpus sweeps never covered
    Google Tasks or /me/todo) + live checkpoints incl. open-extension
    carrier survival.

**Tooling traps made house rules:** O60 (QJsonValue default is Null-typed,
not Undefined — never signal absence with `return {}` + isUndefined();
offset-less wall time must be constructed directly IN the target zone),
O62 (async continuations must own heap-held state — three occurrences this
campaign), O63 (stock-shape edge-count pins must be grepped/updated in the
SAME commit that grows an `edges()` list; Graph `dateTimeTimeZone` is the
TYPE name, its zone property key is plain `timeZone`), O64 (the Phase-6
crossing gate catches per-edge suite blindness — google-person demote was
silently dropping email display names; fixed via displayName, not declared
away), O59 tooling notes (moc × terminated raw string literals = silent
no-output; AUTOMOC timestamp staleness).

- Identity layer LANDED (proposal §5): `src/identity/` — SQLite registry
  `(domain, record-uid) → entity-id`; one rule: contacts emails[].value ↔
  calendar organizer.email/attendees[].email share an entity; never a
  merge; unlink dissolves only the own link. `tst_identity_links` 10 slots.
  Plus **PersonDirectory** (`persondirectory.h`) — the §5 payoff:
  observe() ingests both vendors' fixture corpora into one store;
  eventRoster(event) answers "who is in this meeting?" with names joined
  across vendors (strangers stay strangers). `tst_person_directory` 7
  slots. This is the Phase-6 demo artifact.
- Phase 6 pipeline gate LANDED: `tst_gm_pipeline_convergence` (8 slots) —
  every vendor crossing must stay within the declared LossProfile union;
  plus the GENERATED convergence ledger at
  `docs/campaign/eee/CONVERGENCE-MATRIX.md` (regenerate with
  `tools/matrixgen`; byte-pinned by the suite).

Suite baseline: **195 total / 193 passing** (two documented Radicale slots;
`tst_backend_thread_relocation`/`tst_engine_cancellation` occasionally
load-flaky under full-suite parallelism but pass isolated).

**Pending next actions (ordered)** — Tier A of the adopted roadmap in
`docs/campaign/eee/2026-08-24-reconnaissance-assessment-and-roadmap.md`
(read it; it also holds the Tier-B interior expeditions — scheduling/
free-busy, visibility/ACLs, resource calendars, taxonomy entities, MAPI
named properties, beta-horizon watch — and the binding doctrine incl.
**Part IV ethics of the data model**: never a merge, the graph forgets,
strangers stay strangers, one explicit rule, local custody, loud about
limits, and the seizure test — every new component must pass it):
1. ~~A1 engine-level vendor-shaped hub convergence~~ DONE 2026-08-24
   (`tst_engine_vendor_shaped_hub` 6 slots; FINDINGS O65: events never
   index participant emails — convergence belongs to persons, not
   meetings).
2. A2/A3 live-session DONE 2026-08-24 — captures exist machine-local
   (/me/todo + Google Tasks corpora), carrier verdicts in FINDINGS O66
   ALL THREE channels SURVIVE when spoken to per docs (People clientData;
todoTask/contact extensions via nav POSTs; Outlook extension-id prefix is
Microsoft.OutlookServices.OpenTypeExtension.*); consumer contact GET-by-id
is flaky — drive reads through listings/delta (O66 + correction).
   NEXT: sanitize task fixtures + promotion slots for both todo edges,
   annotate matrix with carrier verdicts. Runbook (reusable):
   `docs/campaign/eee/2026-08-24-live-session-runbook-a2-a3-a4.md`.
   A4 Phase-6 live checkpoint still USER-RUN per runbook.
5. A5 tag the phase boundary; consumer pin bumps voluntary.

## Remotes — push to `origin` (GitHub), NOT `codeberg` (2026-08-22)

Canonical remote is **`origin` = `git@github.com:clintonthegeek/libkalburator.git`**.
All pushes (`main`, tags, branches) go to `origin`. The `codeberg` remote
(`git@codeberg.org:clintonthegeek/libkalburator.git`) is a legacy mirror being
**phased out** — do not push to it, do not add it to new scripts/docs/handoffs.
(Note: `codeberg.org/clintonthegeek/OrgGrove` URLs inside `docs/` refer to a
*different* repository and are unaffected by this.)

## O56 — RESOLVED 2026-08-22, merged to `main`, tagged v1.01

The WildPalms recategorization followup (FINDINGS **O56**) is fixed — two
defects behind one handoff: (A) O55's alias/baseline anchors were chosen
per-batch ("requested id of THIS apply"), so a back-propagation whose op
carries the hub-space id persisted the CROSSED alias and a SECOND baseline
row; pass 2 then split the record across two join keys → phantom AskUser
conflict + phantom delete. Fixed by anchor-stable persisting: aliases and
baselines chain-resolve to the component SINK before writing; crossings are
no-ops. Plus a load-time heal (`healedIdAliases()` + baseline dedup) that
repairs v1.00-poisoned stores in memory every run — **the manual
mapping-state-clear recovery from O55 is no longer needed**. (B) Destructive
ops applied while an AskUser conflict deferred unresolved: `unifiedContinueAfterConflicts`
now holds ALL writes for a mapping with any unresolved conflict
(all-or-nothing; "N unresolved conflict(s); no data was written"). **PlanStan
behavior note:** this changes Unmonitored AskUser semantics for it too — a
run with an unanswered conflict commits nothing until resolutions replay.
Three RED→GREEN slots in `tst_engine_id_aliasing` (recategorization,
poisoned-store heal, defer-moves-nothing). Suite 180/177 baseline unchanged.
Wrap-up: `docs/2026-08-22-o56-recategorization-followup-response.md`.

## O55 — RESOLVED 2026-08-22, merged to `main`, tagged v1.00

The WildPalms hub record-id join churn (FINDINGS **O55**) is fixed: engine-side
id aliasing — `WriteOperation::idAliases()` captured by the default
`SyncBackendBase::applyRecords()`, persisted per mapping (`BaselineStore`
schema **v8**, `blob_id_aliases`), resolved in `perRecordDiff()`, with baseline
hash lookups resolving through the same run's aliases and the first-sync mirror
recording pairings too. Plus the fail-loud piece: `EngineDiff::
identityConflicts` makes dispatchSync REFUSE a mapping whose diff would
cross-create canonically-equal records under unjoined ids (the churn signature)
instead of silently emptying the peer while reporting success. Root cause:
v0.77 converged by accident (failed duplicate-INSERT aborted pre-destruction);
B4 per-side baselines removed the accidental abort. RED→GREEN gate
`tst_engine_id_aliasing` also closes the coverage gap (`GenericSqliteBackend`
is now a real mapping endpoint in the suite). Schema-version pins bumped 7→8
in the two storage tests. Suite **177/180** — identical pre-existing baseline;
note `tst_backend_signals` is ALSO live-Radicale-state-dependent and fails on
the pristine tree at this commit (stash-verified; same family as the documented
`tst_remotecalendarbackend` flake). All changes additive: consumers pin-bump
only, no code change. PlanStan unaffected (engine-stable ids everywhere).
Profiles already churned by pre-fix runs carry orphan dual-form baselines and
need one mapping-state clear to recover. Wrap-up for WildPalms:
`docs/2026-08-22-o55-hub-record-id-aliasing-response.md`.

## O54 — CLOSED 2026-08-22, merged to `main`, tagged v0.99

The `RemoteCalendarBackend` `<calendar>/<uid>.ics` URL-assumption bug
(FINDINGS **O54**) is fixed: a `m_uidToUrl` cache populated wherever an
item's real server URL meets its parsed UID, resolved via
`resolveItemUrl()` on every update/delete/read path (create-only paths
deliberately keep the guess). Regression test RED→GREEN in
`tst_remotecalendarbackend_convergence`; CardDAV (`RemoteContactsBackend`)
audited clean — it already stores real hrefs. Suite 177/179, identical
pre-existing baseline, re-verified on `main` post-merge.

**Live-verified 2026-08-22 against a real Nextcloud account**, exactly the
scenario that found the bug: editing the same previously-failing item and
choosing Keep Local applied cleanly on the next sync (a real write,
`success: true`, no HTTP 400, no repeat).

Branch `fix/o54-uid-url-assumption` was a clean fast-forward onto `main`
@ `f7d3800` (zero divergence, strict superset of the already-released
v0.98 conflict-resolution-repair work) — merged, tagged **v0.99**, branch
deleted. PlanStan pins **v0.97**; bumping to v0.99 (which also carries
v0.98's conflict-resolution fixes) needs no code change on either
consumer — everything in both releases is additive. Closure summary in
`docs/campaign/FINDINGS.md` **O54**; original analysis (status header
updated) at
`docs/2026-08-21-remotecalendarbackend-uid-url-assumption-critical-bug.md`.

---

This repo is the in-flight extraction of PlanStan's sync library into a
standalone project shared with Wild Palms. The source of truth for the
overall plan lives in PlanStan at
`~/dev/PlanStan/docs/proposals/2026-04-20-sync-library-extraction.md`.

## Consumer coordination — cross-repo status index (updated 2026-08-22)

Current release **v0.99**, on `main` (conflict-resolution-repair merged as
v0.98, O54 merged as v0.99, both 2026-08-22 — see below). PlanStan is
bumping its pin to v0.99 this session. WildPalms is still on an
older pin (mid-port from v0.77 as of the last check) and needs zero changes
for this campaign — it never calls `setMaxConcurrentMappings()`, so it stays
at the library's default concurrency of 1, bit-identical to before. The
single "where do the three
repos stand" page is **`docs/2026-07-19-consumer-coordination-status.md`** —
consult it (and update it) whenever a consumer files an RFC/handoff, an inbound
item resolves, or a pin moves. **Open inbound items** (logged in
`docs/campaign/FINDINGS.md`, full index in §2c of the status page):
**O55** — non-blocking, can wait — TwoWay sync between a bare-id backend
and the `GenericSqliteBackend` hub churns and silently empties the hub from pass
2 on, regression v0.77→v0.93+ (WildPalms handoff 2026-08-21, catching up past
a dormant pin). O54, O46, O47, and **WP-A1 calendarsOnly** are all
resolved/closed (§2c/§3 of the status page). Historical note: the
**calendar per-kind VTODO/VJOURNAL
canon dispatch** shipped as **v0.80** (spec/plan under
`docs/superpowers/{specs,plans}/2026-06-28-calendar-per-kind-canon-dispatch*`;
resolved the 2026-06-28 PlanStan handoff).

## Conflict-resolution repair — CLOSED 2026-08-22, merged to `main`, tagged v0.98

Branch **`feature/conflict-resolution-repair`** (`3902f40` → `57a889b`) was a
clean fast-forward onto `main` @ `b0bf3d5` (zero divergence) — merged and
tagged **v0.98**, branch deleted. Answers PlanStan's
`docs/2026-08-21-conflict-info-canonical-data-and-unmonitored-resolution-handoff.md`.
Suite re-verified on `main` post-merge: 177/179, same pre-existing baseline
(`tst_remotecalendarbackend` Radicale auth, `tst_calendar_canon_roundtrip`).
Neither consumer needs a code change — all four fixes are additive; bump the
pin whenever convenient.

**Read `docs/2026-08-21-conflict-resolution-repair-response.md` first** — it is
the authoritative summary for any consumer question about conflicts. Plan and
locked decisions: `docs/2026-08-21-conflict-resolution-repair-plan.md`.

Four defects, one root cause (the canon-upgrade campaign promoted
`BackendRecord::data` to canonical Shape JSON inside `dispatchSync()`, and the
conflict code was never taught):

- **A** — `ConflictInfo::source/targetIcalData`, documented "Full iCal", always
  carried canonical JSON. Both construction sites collapsed into one
  `SyncEngineWorker::buildConflictInfo(op)` that demotes through the reverse
  pipelines (`m_unifiedCanonToSrc`/`m_unifiedCanonToTgt`, stashed beside
  `m_unifiedCanonical`).
- **B** (critical) — a resolution chosen in `SyncBehavior::Unmonitored` wrote
  one `SyncConflictStore` column and never touched data, so the same conflict
  re-presented forever. Fixed by **resolution injection**: the resolution is
  persisted, rehydrated at run start, carried on `SyncEngineWorker::Request`,
  and replayed in `unifiedHandleConflicts()` through
  `applyConflictResolution(op, resolution, mergedNative)` — the helper
  extracted from `resumeAfterConflict()`'s `switch`. **The existing write path
  is reused verbatim; `SyncEngine` never got its own backend write access**
  (INVARIANTS §1). A bounded follow-up pass (`kMaxResolutionPasses = 2`) rides
  `pumpQueue()`'s L2 re-prime so the resolution lands in the same `runSync`.
  Stale resolutions are discarded, not applied; applied ones are consumed once,
  only on the successful-write branch.
- **C** — `resumeAfterConflict(resolution, mergedIcal)` never read `mergedIcal`;
  `CustomMerge` silently ran the automatic merger instead. Now promoted through
  `m_unifiedSrcToCanon`, with guarded fallbacks.
- **D** — `Duplicate` rewrote the clone's uid via `data.replace("UID:"…)`,
  which never matches canonical JSON (`"uid"`), so "Keep Both" emitted a
  colliding clone. Now rewritten through `CanonEnvelope`. Closes PlanStan's
  `docs/bugs/sync-dialog-keepboth-duplicate-not-created.md`.

**Suite: 179 total, 177 passing** — exactly the pre-existing baseline; the same
two failures (`tst_remotecalendarbackend` Radicale auth,
`tst_calendar_canon_roundtrip`). `tst_syncengine_unification` grew from 4 test slots to 14 (runner totals 6 → 16, counting fixtures).

`tests/calendar/tst_calendar_conflict.cpp`'s contract was deliberately
**flipped**: it asserted an Unmonitored run left the target unwritten, which was
pinning defect B in place. It now asserts the resolution lands.

New FINDINGS: **O48** (baseline *bytes* are stored nowhere → `baselineIcalData`
always empty → PlanStan's 3-way diff unreachable; needs a storage decision),
**O49**/**O50** (fixed), **O51** (staleness guard is second-granular and
unprotective for backends reporting no `lastModified`), **O52** (a rehydrated
`CustomMerge` loses the user's payload), **O53** (pre-existing: the batch
conflict dialog is modal inside `onWorkerSyncCompleted` while other mappings are
in flight — live because PlanStan defaults to 4 concurrent mappings).

**Still explicitly USER-RUN:** live verification against a real CalDAV account
that a resolved conflict lands on the server, stops re-presenting, and that
"Keep Both" now yields two items. All coverage above is stub-backend.

## Parallel-sync campaign — CLOSED 2026-08-21, merged to `main` at v0.97

`parallel-sync` fast-forwarded onto `main` (was a strict superset — zero
divergence) and has been deleted. `SyncEngine` now runs multiple sync
mappings concurrently: `setMaxConcurrentMappings(int)` (library default
**1**, bit-identical to every pre-campaign consumer — concurrency is
opt-in per host), an endpoint-collision scheduler (`pumpQueue()`) that
never lets two mappings diff/apply against the same (backend, calendar) at
once, source/target fetch overlap within a mapping, chunked `LocalBackend`
I/O, and a per-backend `maxConcurrentOperations()` veto
(`RemoteCalendarBackend` declares 4). `phaseChanged`/`progressUpdated`
semantics were redefined for concurrency — `Complete` describes the RUN,
not one mapping (WildPalms' `shouldPauseTickle()` depends on this, though
WildPalms itself is unaffected since it never raises concurrency above 1).

**Suite: 179 total, 177 passing** — the same two pre-existing failures
throughout campaign and after merge (`tst_remotecalendarbackend`: broken
local Radicale test-server auth; `tst_calendar_canon_roundtrip`:
pre-existing on `main`, uncatalogued, still needs triage). Verified
identical at N=1 and N=4 (`KALBURATOR_TEST_MAX_CONCURRENT_MAPPINGS=4`)
during the campaign; re-verified on `main` post-merge at default N=1.

`KALBURATOR_TEST_MAX_CONCURRENT_MAPPINGS` is a **test-only** env knob
(read once into a `static` in `resolveEffectiveCap()`, memoized for the
whole process): forces every Queue-mode run's concurrency to the given
value regardless of what the host requested, except Monitored runs, which
stay pinned to 1 unconditionally. Never consulted unless set — production
and every real consumer are unaffected. Because the `static` is memoized
per-process (= per test binary, since QTest runs all slots in one
process), a test cannot override the sweep back down via
`setMaxConcurrentMappings()` once any earlier test in that binary has read
it; a test whose contract is genuinely concurrency-1-only must instead
guard the sweep-invalidated assertion behind
`!qEnvironmentVariableIsSet("KALBURATOR_TEST_MAX_CONCURRENT_MAPPINGS")` —
see `tst_syncengine_unification.cpp` and `tst_engine_cancellation.cpp` for
the pattern.

**PlanStan-side (thread-per-backend + pin bump + concurrency setting) is
also DONE and merged**, pinning **v0.97** (carries the `BackendRegistry::
m_instances` QMutex fix — a genuine pre-existing write-path race, guarded
the same way as `TransformationRegistry::m_frozenDomainsMutex`). PlanStan's
own `AppSettings::syncMaxConcurrentMappings()` defaults to **4** — that's a
host-side choice, not a library default change.

**Still open, explicitly USER-RUN, not attempted by any agent session:**
re-measuring the original ~76s/11-mapping table against a real
multi-calendar CalDAV account with concurrency on, and a live Radicale gate
exercising real concurrent sync end-to-end with a display. Full history:
`~/dev/PlanStan/docs/superpowers/plans/2026-08-12-parallel-sync.md` (task
source, STATUS header marked complete).

## Architectural-redress campaign — START HERE if on a branch `feature/redress-N-*`

If your CWD is on any branch matching `feature/redress-N-*` (N = 1..11), you are
working the campaign opened 2026-05-29 from a fresh-eyes audit of the post-canon
codebase. The audit found the canon-upgrade convergence (below) succeeded but the
underlying layering, encapsulation, and naming grew leaks no one stopped to name.
The redress is the next sustained body of work.

**Before your first non-trivial change, read in this order:**
1. `docs/campaign/architectural-redress/INVARIANTS.md` — the rules you accept by
   working here. Non-optional.
2. `docs/campaign/architectural-redress/AUDIT.md` — the fresh-eyes findings this
   campaign exists to redress. The audit wins if it disagrees with a plan.
3. `docs/campaign/architectural-redress/STATUS.md` — campaign state, the 11-plan
   sequence, locked decisions, your next action.
4. `docs/campaign/architectural-redress/FINDINGS.md` — the discipline log; append
   to it (invariant 9) when you walk past a smell.
5. The current plan in `docs/campaign/architectural-redress/plans/`.

New smells go in `FINDINGS.md`; update `STATUS.md` in the same commit that
changes plan state.

## Canon-upgrade / convergence campaign — START HERE if on branch `feature/canon-upgrade-convergence`

If your CWD is on branch `feature/canon-upgrade-convergence`, you are working
the campaign that (a) retires `src/transcoding/` into the shape graph and
(b) upgrades the calendar/contacts/todo canons to rich JSON superset encodings
behind a **versioned canonical spine** with a four-kind loss model.

**Status (2026-05-24): the campaign is COMPLETE — all four plans landed; `src/transcoding/`
is deleted and the shape graph is the sole transformation mechanism (invariant 1).**
Downstream port (FINDINGS O7/O12) DONE; O7 resolved 2026-05-27, O12 effectively closed;
branch merged to `main`. See `docs/campaign/STATUS.md` for the full history.

**Before your first non-trivial change, read in this order:**
1. `docs/campaign/INVARIANTS.md` — the rules you accept by working here. Non-optional.
2. `docs/campaign/STATUS.md` — campaign state (now: converged), the 4-plan sequence, locked
   decisions, and the remaining downstream next actions.
3. `docs/campaign/FINDINGS.md` — open watch items (esp. O9) and the discipline log.
4. The plans, all complete: `docs/2026-05-23-plan-1-shape-core-foundations.md`,
   `docs/2026-05-23-plan-2-per-engine-registries.md`, `docs/2026-05-24-plan-3-canon-encodings.md`,
   `docs/2026-05-24-plan-4-calendar-convergence.md`.
5. Design set (as needed): `docs/2026-05-23-canon-upgrade-and-convergence-design.md`,
   `docs/2026-05-23-canon-schema-design.md`, `docs/2026-05-23-vendor-api-shapes-reference.md`.

The one-paragraph why: libkalburator grew **two** parallel conversion mechanisms;
this campaign collapses them into one (the shape graph) and modernizes the canons.
The deepest invariant (INVARIANTS §1): extend the shape graph, never fork a third
mechanism. New issues/smells go in `docs/campaign/FINDINGS.md`; update
`docs/campaign/STATUS.md` in the same commit that changes plan state.

## Sync-graph-redesign campaign (PlanStan-originated) — Phase 1 CLOSED 2026-07-16; current release v0.94

**This repo's Phase 1 is complete — do not redo its work.** The full
cross-repo plan (this repo, Graffodil, PlanStan) lives in PlanStan at
`~/dev/PlanStan/docs/superpowers/plans/2026-07-15-sync-graph-redesign.md`
(status tracked there — see PlanStan's own `CLAUDE.md` for the current
cross-repo campaign summary). This repo's Phase 1 (Tasks 1-5, branch
`sync-graph-engine`, merged to `main` and deleted) added engine-level
sync convergence fixes and per-LC wiring: **L1** un-freezes the
once-per-run fast-path skip set when an earlier mapping in the same
Queue run writes a shared endpoint (`SyncEngine::invalidateSkipsTouching`);
**L2** adds fixpoint passes — a Queue run re-primes over dirtied mappings
(up to `kMaxSyncPasses = 3`, `syncPassStarted(int,int)` signal) so
convergence no longer depends on mapping list order; per-LC
`WiringPolicy` (`CollectionDefault`/`Hub`/`Mesh`/`Chain`/`Manual`) lets
individual logical calendars override the collection's default sync
topology, with `Manual` meaning the compiler skips that LC entirely;
providers (`CalDavProvider`/`MultiProtocolDavProvider`/`CardDavProvider`)
now reliably emit `Connecting`/`Connected`/`Error` connection states with
populated `lastError()` — this required relocating `ProviderConnectionState`
from `providermanager.h` to `iprovider.h` and making
`connectionStateChanged` a genuine C++ signal overload
(`(bool)` and `(ProviderConnectionState)` coexist; disambiguate with
`qOverload<...>(&IProvider::connectionStateChanged)`). Tag: **v0.94**.
Full suite 170/172 at close (the pre-existing `tst_remotecalendarbackend`
Radicale-state flake, plus a pre-existing-but-newly-surfaced
`tst_calendar_canon_roundtrip` failure independently confirmed unrelated
to this campaign — canon/iCal classification encoding, no code-path
overlap). Known gap carried forward for PlanStan's later tasks:
`ProviderManager`'s aggregate `providerStateChanged` surface does not
forward the new Connecting/Error granularity (only Connected/Disconnected)
— PlanStan must bind directly to `IProvider::connectionStateChanged`.

## Sync-excellence campaign — CLOSED 2026-07-09 (CP-C); current release v0.91

**The campaign is complete — do not redo its work.** The full phase plan
and per-phase evidence are archived at
**`docs/campaign/archive/2026-07-07-sync-excellence-phases.md`** (see its
§17 CP-C entry for the closing soak/adversarial/efficiency evidence).
Phases E1–E13 + checkpoints CP-A/B/C landed: honest stats + dead code
(E1), the O26 flake (E2), cancellation/teardown honesty (E3), CalDAV
write-path pins (E4), the async-backend rework deleting the
nested-event-loop re-entrancy (E5/audit B7), EtagCache seeding (E6),
RFC 6578 `sync-collection` (E7), phantom-conflict adoption (E8),
signal/fingerprint polish (E9), PlanStan adoption (E10), the
CalendarManager async API (E11/O39), canon timestamp-stamping (E12/O41),
the PlanStan presentation-freeze fix (E13/O44), and the CP-C deferral
fixes (O42 first-fetch sync-collection amnesia; O45 bounded write-dispatch
window). Tags: v0.85, v0.90, v0.90.1, **v0.91** (close). FINDINGS O26,
O28–O36, O39, O41–O45 all Resolved; the §16 residual inventory was PARKED
at CP-C with rationale. New sync issues get a new O-number in
`docs/campaign/FINDINGS.md`; any future campaign should reuse the §0
session-protocol + strong-model-checkpoint discipline — both prior
campaigns' live checkpoints each caught a blocking bug the green suite
missed (O25, O27), and CP-C caught two more (O42, O45 rulings).

Lineage (context only, all CLOSED): sync-convergence campaign (Tracks A–C,
tags v0.80–v0.82; roadmap `docs/campaign/2026-07-03-sync-convergence-roadmap.md`,
now closed end-to-end) → sync-hardening campaign (D1 threading + O16–O27,
tags v0.83/v0.84; plan archived at
`docs/campaign/archive/2026-07-05-sync-hardening-phases.md`). The
architectural reference both campaigns and this one build on is the
first-principles audit
(`docs/campaign/archive/2026-07-05-first-principles-sync-architecture-audit.md`)
— its §1 target model is what E5 finishes implementing.

## Phase-status docs are living documents

All phase progress is tracked under `docs/phase0/`. When a phase
completes, fails, or pauses, update the corresponding status file in
the same commit that lands the code change. In particular:

- `04b-phase3-status.md` — Phase 3 status. Keep the **Status** line at
  the top accurate ("Phase 3a done", "Phase 3b in progress", etc.) and
  update the "What exists now" and "Next" sections as work lands.
- Any new phase doc should follow the same pattern: Status line at top,
  "What exists" / "What remains" sections, updated every time the phase
  state changes.

Do not leave a status doc saying "paused" after work has resumed, or
"WIP" after it has landed. Future sessions start from these docs — if
they lie, work gets redone or skipped.

## Build

Standalone build:

```
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
```

Default profile: `KALBURATOR_HAVE_ORG_IO=OFF`, `KALBURATOR_HAVE_AKONADI=OFF`.

## Calendar-layer integration tests (since Phase D.0, 2026-04-28)

`tests/calendar/` contains stub-`ISyncHost` integration tests that
pin `SyncEngine` behavior. They are the contract the engine-merger
refactor (Phases D / E / F / G) preserves. Phase F1 (2026-04-30,
tag `v0.13-phase-f1-unify`) collapsed `SyncCoordinator` +
`SyncWorker` + `BlobSyncEngine` into the unified `SyncEngine` at
`src/engine/syncengine.{h,cpp}` — historical references to those
old class names appear in commit messages and FINDINGS but should
not be used in new code or comments.

When writing or modifying tests in this directory:

- Use the four reusable stubs at `tests/calendar/stubs/`:
  `StubSyncHost`, `StubCalendarCollection`, `StubIncidenceRegistry`,
  `StubSyncConfigStore`. Compiled into static lib
  `kalburator_calendar_test_stubs`. Link via the helper function
  `kalburator_add_calendar_integration_test()` in
  `tests/calendar/CMakeLists.txt`.

- **Canonical engine entry: `SyncEngine::runSync(SyncRequest)`**
  returning `QFuture<QList<SyncResult>>` (redress Plan 1). This is the
  **sole** sync entry — the four `runSyncFuture(...)` overloads were
  DELETED in redress Plan 8 step 3 (2026-06-10), along with
  `dispatchSingleNative` and the dual `m_currentSingleIface`/
  `m_currentMultiIface` interface; the engine now holds one
  `m_currentIface` + one `m_currentWatcher` wired by `beginRun()`.
  Build a `SyncRequest` (`mappingIds` empty ⇒ all enabled; size 1 ⇒
  single mapping; size >1 ⇒ subset). Wait via
  `QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000)` (NOT
  `waitForFinished` — Qt6's `waitForFinished` does NOT spin the test
  event loop). Read results via `future.resultAt(0)` — a
  `QList<SyncResult>` (NOT `future.results()`, empty after cancel due
  to a Qt6 quirk). The void `runSync` overloads, `cancelSync`, and the
  `syncCompleted`/`allSyncsCompleted` signals were deleted in
  F2 Task 42.

- **Single-mapping cancel is now native** (Plan 8 step 3): a canceled
  single-mapping `runSync(SyncRequest)` future preserves the F2 Task 23
  contract — `resultCount()==1`, `resultAt(0).first().cancelled==true` —
  with **no `.then()` wrap** and **no `resultCount()>0` guard** needed.
  (Pre-collapse the canonical single path lost this; only the deleted
  shims preserved it. Pinned by `tst_engine_single_mapping_cancel`.)

- **Cancellation** — call `future.cancel()`. The cancellation
  channel propagates through
  `QFutureWatcher::canceled → SyncEngine::onCancelObserved →
  SyncEngineWorker::observeCancel` and wakes the nested `QEventLoop`s
  that gate cancellation: `dispatchSync`'s two fetch-gate loops
  (source/target, H1.1) and the conflict-pause slot. (The `await<Op>`
  template that used to be the shared idiom for this was dead code —
  zero call sites — and was deleted in H1.4.)

- **Write path** — `SyncBackend::storeItems()` / `updateItem()` /
  `writeFinished` were DELETED (canon-upgrade campaign; only stale
  comments mention them). The write API is the 2-arg
  `pushItems(calendarId, items)` returning a `PushOperation*`;
  read `op->state()` / `op->errorString()` for error reporting
  (per the F2 SyncOperation contract). `TranscodingPlan` no longer
  exists — transformation flows through the shape graph.

- **Conflict tests** — set `mapping.conflictPolicy = AskUser` AND
  seed a baseline via `BaselineStore::setBaselineV3()` (the
  mapping-keyed v3 API in `storage/baselinestore.h`; there is no
  `SyncStore` class). Other policies resolve silently without
  signals; the quick-path (no baseline) downgrades AskUser to
  SourceWins.

- **`StubCalendarCollection`** must hold a `MemoryCalendar` with
  `setId(calendarId)` matching the `SyncMapping`'s calendar id, or
  `applyChangesToBackend` can't find it and writes get dropped.

See `docs/phase0/04l-phase-d0-test-harness-design.md` and
`04l-phase-d0-test-harness-plan.md` for the full pattern, including
test-execution model and gotchas.
