# Sync-convergence campaign — Tracks A/B/C (archived, complete)

> **ARCHIVED 2026-07-04.** Tracks A, B, and C of the sync-convergence campaign
> are fully landed (tags v0.80, v0.81, v0.82; PlanStan pinned and verified
> live). This file preserves the detailed phase specs (problem, evidence,
> exact code references, fix design, test plan) that used to live in
> `docs/campaign/2026-07-03-sync-convergence-roadmap.md` — kept for
> archaeology only, not actionable. The live roadmap now carries only Track D
> (remaining work) plus a pointer here. Final landing detail for each phase is
> in `docs/campaign/FINDINGS.md` and `PlanStan/docs/complete/
> 2026-07-04-archived-claude-md-landed-work.md`.

## Track A — canon correctness

### Phase A1 — Finish per-kind canon dispatch (F4)

**Status:** Tasks 1–3 of 9 are merged to `main` (commits `5498804` kind
discriminator, `d89a863` VEVENT helper extraction, merge `905265f`). Tasks 4–9
remain. **This phase is already fully planned — execute the existing plan:**

- Plan: `docs/superpowers/plans/2026-06-28-calendar-per-kind-canon-dispatch.md`
  (Tasks 4–9: VJOURNAL helpers, kind dispatch in the calendar stages, property
  catalogue union, fail-loud transcode guard, engine-level hybrid reconcile
  test, full-suite verification + v0.80 bump).
- Spec: `docs/superpowers/specs/2026-06-28-calendar-per-kind-canon-dispatch-design.md`
- Handoff (problem statement + RED test): `docs/2026-06-28-calendar-vtodo-vjournal-shape-dispatch-handoff.md`

**Interaction with this campaign:** Task 7's fail-loud guard
(`src/engine/transcodeguard.h`, wired at the engine promote site and the two
demote sites syncengine.cpp:2721 / :2743) is the systemic backstop for the
whole N1/F4 bug class — a non-empty record that transcodes to empty must fail
the mapping loudly. Do not soften it.

**Acceptance:** existing plan's Task 8/9 gates; plus, re-run against evidence:
a VTODO fetched from the FakeCalDavServer round-trips ical→canon→ical
non-empty with kind preserved.

### Phase A2 — N1: recurrence scraped from VTIMEZONE corrupts events  **[CRITICAL]**

**Problem.** The "verbatim recurrence preservation" helper collects
`RRULE:`/`RDATE:`/`EXDATE:` lines by scanning the **entire** iCal byte string.
When the blob contains a `VTIMEZONE` (any event with a TZID DTSTART — the
common case for Nextcloud events), the timezone's DST transition rules are
harvested and re-injected into the rebuilt component as *event recurrence*.

**Evidence.** `NewCollection2.kalb.d/calendars/TBS/81f191f2-….ics`: a one-off
"Work" event with `DTSTART;TZID=America/Toronto`, now carrying 6 RRULEs +
3 stray RDATEs — byte-identical to the America/Toronto VTIMEZONE transition
lines. The pristine copy in the content cache has zero RRULEs. Downstream:
Sabre rejects the re-serialized event with 415 ("not valid iCalendar 2.0
data"; RFC 5545 allows at most one RRULE) — log lines ~940–1030, ~46× 415 +
4× 400 per cycle.

**Exact references (all three copies must be fixed — one definition):**
- `src/calendar/eventcanonfields.cpp:152` `extractRecurrenceLines()` — naive
  line scan; consumed at `:277` (encode); re-injected verbatim at `:567–:583`
  (decode).
- `src/todo/vtodocanonfields.cpp:108` — identical copy; consumed at `:214`.
- `src/calendar/orgicalcanonstages.cpp:107` `extractRecurrenceLinesLocal()` —
  third copy; consumed at `:212` (comment at `:275` even cites the shared
  approach).

**Fix design.** Replace whole-blob line scanning with **component-scoped**
extraction: walk the raw text and collect RRULE/RDATE/EXDATE lines only
between the `BEGIN:`/`END:` of the *target* component (VEVENT / VTODO /
VJOURNAL), skipping any nested VALARM and everything inside
`BEGIN:VTIMEZONE…END:VTIMEZONE`. Implementation notes:
- Keep the verbatim-line strategy (campaign invariant 3 — recurrence must
  round-trip byte-exact; KCalendarCore re-serialization does not guarantee
  that). Only the *scope* of the scan changes.
- Extract ONE shared helper, e.g.
  `Kalburator::Calendar::extractComponentRecurrenceLines(const QByteArray&, QByteArrayView componentName)`
  in a small shared TU (natural home: a new `src/calendar/icalcomponentscan.{h,cpp}`
  or fold into `eventcanonfields` and have todo/org include it — follow
  INVARIANTS "one definition"). Delete the two copies.
- Handle iCal line folding (continuation lines start with space/tab) — the
  current scanner splits on `\n` and `trimmed()`s, which also mangles folded
  RRULEs; the replacement must unfold before matching (RFC 5545 §3.1).
- Multiple VEVENTs in one blob (recurrence exceptions / RECURRENCE-ID
  siblings): scope to the component whose UID matches the record, or
  concatenate per-component — decide in-spec; add a fixture either way.

**Tests (RED first).**
1. Unit (extend `tests/calendar/tst_calendar_canon_roundtrip.cpp`): fixture =
   single one-off VEVENT + full VTIMEZONE (copy the America/Toronto block from
   the evidence file into `tests/calendar/fixtures/`). Assert: canon
   `recurrence` array is **empty**; ical→canon→ical output contains **zero**
   RRULE/RDATE/EXDATE lines; output parses via `KCalendarCore::ICalFormat` and
   the parsed event has `recurs() == false`.
2. Same fixture but the event ALSO has its own `RRULE:FREQ=WEEKLY` → exactly
   that one line survives, byte-exact.
3. Folded-line fixture: an RRULE folded across two physical lines round-trips
   intact.
4. VTODO analogue in `tests/todo/` (vtodocanonfields path), VJOURNAL analogue
   once A1 Task 4 exists, org-ical analogue if `KALBURATOR_HAVE_ORG_IO` lane
   is exercised (`orgicalcanonstages`).
5. Regression guard: no change to `tst_orgical_canon_roundtrip`,
   `tst_todo_canon_roundtrip` green baselines.

**Acceptance.** All new tests green; full suite green; manual spot-check —
run the evidence blob (pristine copy from the content cache) through
ical→canon→ical and diff: only known-lossy fields may differ, recurrence
byte-identical.

**Size:** S–M (one helper + three call sites + fixtures).

---

## Track B — sync-stack integrity & convergence

### Phase B1 — N3: remote blob records stamp `lastModified = now`

**Problem.** `src/calendar/remotecalendarbackend.cpp:1936`
`blobRecordFromIcal()` sets `rec.lastModified = QDateTime::currentDateTimeUtc()`
(`:1946`) for every record on every load (call sites `:2039` in
`loadRecords`, `:2053` in `readRecord`). Every LastWriteWins comparison
(engine `lastwritewins.h`; conflict walk syncengine.cpp:2417–2426) therefore
sees the remote as "modified this instant" — the v0.64 LWW tie-bias work is
defeated at the blob level. The in-code comment already flags it ("Phase E
can improve this").

**Fix design.** Parse `LAST-MODIFIED` (fall back to `DTSTAMP`, then `CREATED`)
from the iCal bytes inside `blobRecordFromIcal`, mirroring the semantics
LocalBackend already implements (`src/calendar/localbackend.cpp:980–1023`,
v0.64 fix A2: explicit stamp is authoritative). Only stamp `now` when the
bytes contain no usable timestamp — and prefer `QDateTime()` (invalid) over
`now` in that case so LWW's "valid > invalid" modify-delete rule stays
meaningful. Check `tst_remotecalendarbackend_convergence` and the LWW tests
(`tests/calendar/tst_calendar_conflict.cpp`, engine LWW suites) for
encoded-now assumptions before changing.

**Tests.** Unit: record built from iCal with `LAST-MODIFIED:20250101T000000Z`
carries exactly that instant; without any stamp → invalid QDateTime.
Engine-level: modify-modify conflict between a stale remote and a fresher
local now resolves to local under LWW (RED against current behavior).

**Size:** S.

### Phase B2 — N4: multiget chunking + transport-error truth

**Problem.** `RemoteCalendarBackend::fetchItems` builds ONE
`KDAV::DavItemsFetchJob` with every changed href (`remotecalendarbackend.cpp:1510`;
href list built at `:1395–:1400`). For the user's 673-item calendar the
REPORT dies every time — `qt.network.http2: stream 3 error: "HTTP/2 protocol
error"` — surfaced to the app as `"Invalid username/password (401)"` (KIO's
error text). That calendar has **never** synced. Mature clients chunk
multigets (Evolution ~90 hrefs/REPORT).

**Fix design.**
1. **Chunk** `urlsToFetch` into batches (constant, e.g.
   `kMultigetChunkSize = 75`; make it a settable member for tests). Run
   batches **sequentially** (do not parallelize against rate-limited shared
   hosts), accumulating `fetchJob->items()` into the existing
   `fetchedItemsMap`; only after the final batch run the existing
   process-all-items loop (`:1520–:1640`). Any batch error fails the op
   (state `Failed`) — never proceed with a partial map (see B3 for why).
2. **Error truth:** when a job fails, include the KJob error *and* the HTTP
   status if available; stop collapsing transport-layer failures into the
   auth-shaped message. While here: `setRawIcs` (`:1863–1918` region) must log
   `resp.body` on non-2xx — Sabre returns the exact rejection reason
   (`This resource only supports valid iCalendar 2.0 data…`) in the body;
   discarding it cost this investigation hours.
3. **HTTP/2 fallback (investigate, optional):** the multiget goes through
   KDAV→KIO, so `QNetworkRequest::Http2AllowedAttribute` isn't directly
   reachable. Chunking alone likely avoids the stream reset (smaller
   payloads). If stream errors persist at chunk size 75, either shrink the
   chunk or replace `DavItemsFetchJob` with an in-house multiget REPORT via
   `davSyncRequest` (`remotecalendarbackend.cpp:188–248` — it already
   supports arbitrary verbs/bodies) + a multistatus parser (pattern:
   `parseCtagMultistatus` at `:250–280`), where HTTP/2 can be disabled
   per-request. Prefer the smallest change that makes the 673-item lane pass.

**Tests.** Extend `tests/sync/fakecaldavserver.{h,cpp}`:
- Serve a calendar with > 2× chunk-size items; assert the server saw
  ⌈N/chunk⌉ REPORTs and the op returns all N items.
- Fail the second REPORT (500) → op state `Failed`, no CTag committed
  (coordinates with B3), error string contains the real status.
- Regression: `tst_remotecalendarbackend_convergence`,
  `tst_caldav_integration` green; optional live-probe against Radicale with
  ~200 generated events (`KALBURATOR_BUILD_LIVE_PROBES`).

**Size:** M.

### Phase B3 — N5: CTag ↔ content-cache coherence

**Problem.** The persisted CTag (class `CTagStore`,
`remotecalendarbackend.cpp:62`, table `remote_ctags` in the consumer's
sync.db, wired via `setDbPath` `:342`) can get ahead of the content cache.
Observed end state (log lines 1329–1335): `CTag unchanged for …cal:Next
Actions ("http://sabre.io/ns/sync/2385") - serving from cache` → `Served 0
incidences from cache (CTag match)` — for a calendar with **673 items on the
server** whose every multiget had failed. `fetchItems` then completes
**successfully with 0 items**, so the engine reads the remote as empty. With
baselines present, that is a mass-delete generator pointed at the mirror
(only PlanStan's missing guard — Phase C1 — and the also-empty local side
prevented damage).

**Exact references.**
- CTag short-circuit + serve path: `remotecalendarbackend.cpp:1335–1360`
  (`ctag()`/`fetchFreshCtag(:635)`/`serveCachedItems(:1291)`).
- `pendingCtag` staging: `:1360` (fresh mismatch), `:506`
  (`facts.pendingCtag = ctag` — **discovery priming** hands the backend a
  fresh server CTag before any item bytes exist).
- The two commit sites: `:1498–1500` (all-from-cache path — commits even when
  every item was a cache-miss skip) and `:1624–1626` (multiget path — commits
  even when `countSkipped > 0`).
- `primeRevisionCache` (`:694`) also writes CTags directly — it exists for the
  ChangeDetection skip-path (`src/sync/changedetection.h`) and must NOT feed
  the fetch-freshness check uncritically.

**Fix design (rules, in order of importance):**
1. **Commit `pendingCtag` only on a *complete* materialization**: in both
   terminal paths, require `countSkipped == 0` and (multiget path) no failed
   batches; otherwise leave the stored CTag untouched so the next cycle
   re-lists.
2. **Serve-path sanity check:** in the CTag-match branch, if
   `serveCachedItems` returns 0 items, do not trust the match — clear the
   stored CTag (`clearCtag`, `:368`) and fall through to the full list+fetch.
   (A genuinely empty calendar re-lists cheaply: one PROPFIND that returns 0
   items and legitimately re-commits.) Stronger variant if desired: persist
   the item count alongside the CTag in `CTagStore` and compare.
3. **Separate concerns:** the ChangeDetection revision cache (used by
   `SyncEngine::prepareSyncFastPath`, syncengine.cpp:660) and the
   fetch-freshness CTag must not be conflatable in a way where priming one
   silently validates the other. Minimal fix: make discovery priming stage
   only `pendingCtag` (never `setCtag` directly) — audit `:694`'s callers.

**Tests.** FakeCalDavServer scenarios (extend `tests/sync/`):
- List OK, multiget fails → op Failed, `ctag()` still empty; next fetch
  re-lists (RED against current `:1624` commit).
- CTag matches but cache empty while server has items → full refetch happens,
  items served, CTag re-committed (RED against current serve path).
- Genuine empty calendar → serve 0 with CTag match stays cheap (no refetch
  loop).

**Size:** M.

### Phase B4 — N2: per-side baseline hashes  **[CRITICAL — the convergence fix]**

**Problem.** Change detection gates on `BackendRecord.contentHash` = SHA-256
of each backend's **native bytes** (`localbackend.cpp:989`;
`remotecalendarbackend.cpp:1936ff`), compared against a **single** stored
baseline hash per record. Two backends never serialize the same logical
record identically (PRODID, property order, folding, server normalization),
so after any cross-backend write, at least one side's hash permanently
differs from baseline → every record is "modified" (or conflicted) on
**every** sync, forever. The code knows about the tension — see
`perrecorddiff.cpp:96–101` ("baseline records store contentHash but not
actual data bytes…") and the "contentHash staleness" caveats
(syncengine.cpp:2169).

**Exact references (every site that compares or stores a baseline hash):**
- Diff core: `src/engine/perrecorddiff.cpp:100–107` (`equalRecords` —
  hash-equality first), state machine `:113–147`.
- Baseline load (hash into `rec.contentHash`): syncengine.cpp:2185–2210.
- Implicit baseline seeding: syncengine.cpp:2238
  (`if (srcRec.contentHash != tgtRec.contentHash) continue;` — under
  cross-serialization this **never** seeds, silently disabling
  deletion-detection bootstrap for already-in-sync records).
- First-sync mirror compare: syncengine.cpp:1733
  (`it.value().contentHash != sr.contentHash` in `dispatchFirstSync`).
- Baseline save: syncengine.cpp:2763–2779 (`canonical.data =
  rec.contentHash.toUtf8()`, shape `{blob,raw}`, via `setBaselineV3`);
  `updatedBaselines` population sites syncengine.cpp:1508–1586 (conflict walk)
  and :2320–2330 (toSource ops) — each appends a record carrying **one**
  side's hash.
- Store: `src/storage/baselinestore.{h,cpp}` — v3 table
  `blob_baselines_v3(mapping_id, record_id, canonical_shape_domain,
  canonical_shape_encoding, canonical_bytes, updated_at)`
  (`baselinestore.cpp:210–218`), API `setBaselineV3` / `baselinesForMappingV3`
  (`baselinestore.h:83–90`), schema versioning via `PRAGMA user_version`
  (`kSchemaV3Introduced = 4`, `baselinestore.cpp:229–235`).

**Fix design.** A baseline must record **what each side's bytes hashed to at
the moment of the last successful sync**: `{recordId, sourceHash, targetHash}`.

1. **Storage:** add nullable `source_hash TEXT` / `target_hash TEXT` columns
   to `blob_baselines_v3` (idempotent `ALTER TABLE` migration, bump
   `user_version` → 5, follow the existing v2→v3 migration pattern
   `baselinestore.cpp:204–260`). Legacy rows (only `canonical_bytes` = the old
   single hash): treat that hash as *both* side-hashes on load — first
   post-upgrade sync will re-diff exactly as today, then write proper
   per-side rows; no data migration pass needed. Extend `setBaselineV3` /
   `baselinesForMappingV3` (or add V4 siblings — follow the repo's
   deprecation style, `baselinestore.h:128–144`).
2. **Engine plumbing:** introduce an engine-side baseline type (e.g.
   `BaselineEntry {id, sourceHash, targetHash}`) instead of smuggling hashes
   through `BackendRecord.contentHash`. `perRecordDiff` signature changes
   (callers: syncengine.cpp:2215 + tests). `equalRecords(side, baseline)`
   becomes `sRec.contentHash == b.sourceHash` / `tRec.contentHash ==
   b.targetHash`.
3. **Capture written-bytes hashes:** after the demote transcode at the two
   apply sites (syncengine.cpp:2721 target, :2743 source), the bytes actually
   written to each side are known — hash them there and carry per-record
   `{srcHash, tgtHash}` into the baseline save (replace the single-hash
   `updatedBaselines` bookkeeping at :1508–1586/:2320–2330; an
   `EngineMerge`-level `QHash<QString, PendingBaseline>` keyed by record id is
   cleaner than mutating record copies). For a record only written to one
   side, the other side's hash = its current (read) contentHash.
4. **Fix the two raw-compare sites** (`:1733`, `:2238`): implicit seeding
   should seed `{srcHash = source's, tgtHash = target's}` whenever the two
   sides are *semantically* equal — use the domain differ
   (`dd->createCanonicalDiffer()`, already available) for that one
   equality check instead of hash equality; `dispatchFirstSync`'s update
   condition likewise must not byte-compare across backends (on a true first
   sync target is empty, so the compare site only matters for the
   non-empty-deferral path — verify and document).
5. **Known residual (document, don't chase):** if a server normalizes stored
   bytes on PUT (rare; Sabre stores verbatim), the first post-write fetch
   yields a hash ≠ recorded targetHash → one extra "target modified" pass
   that settles on the following sync. Acceptable; the convergence test in B5
   must use FakeCalDavServer (verbatim storage) so this doesn't flake.

**Tests (RED first).**
- Unit `tests/engine/`: perRecordDiff with per-side baselines — all 3-way
  states (both-match, src-changed, tgt-changed, both-changed, deletes)
  expressed with *different* src/tgt serializations of equal content.
- Store: migration test (open a DB stamped v4 with single-hash rows → loads
  as both-sides; new writes persist per-side; reopen round-trips).
- **The convergence test (engine-level):** LocalBackend (tmp dir) ↔
  FakeCalDavServer via a real mapping; seed server with mixed VEVENT+VTODO
  (post-A1/A2 both survive); sync once (mirror populates), sync again →
  assert **zero** creates/updates/deletes/conflicts on run 2 (inspect
  `SyncResult` and/or writer batches). This test is the campaign's core
  regression gate; name it prominently, e.g.
  `tests/engine/tst_sync_convergence.cpp::secondSyncIsNoOp`.

**Size:** L. Touches engine + storage; keep it one branch, land whole.

### Phase B5 — Convergence acceptance gate + change-detection fast path

**Goal.** Prove the end state (§1) and make idle cycles nearly free.

1. **Acceptance matrix** (all engine-level, FakeCalDavServer + LocalBackend):
   - second sync no-op (from B4);
   - third sync after one local edit → exactly one update PUT, then no-op;
   - remote edit (bump server item + ctag) → exactly one multiget of one
     item, one local write, then no-op;
   - remote delete → one local delete (guard consulted per thresholds), no-op.
2. **Fast path:** with convergence proven, wire the skip: both backends
   already implement `Sync::ChangeDetection` (`src/sync/changedetection.h`;
   remote = CTag `remotecalendarbackend.cpp:675–695`, local =
   name|mtime|size fingerprint `localbackend.cpp:166`).
   `SyncEngine::prepareSyncFastPath` (syncengine.cpp:660; toggle
   `setSkipUnchangedMappings` :654) currently reports
   `of 7 mappings, 0 are unchanged` — verify it computes per-side revisions
   correctly post-B3/B4, add a would-skip telemetry log line, then (Track C4)
   flip PlanStan's `AppSettings::syncSkipUnchanged` default to true
   (consumer wiring at `PlanStan/src/controllers/collectioncontroller.cpp:1824–1834`).
3. **Idle-cycle budget (assert in a test where measurable, otherwise log):**
   per unchanged mapping: ≤ 1 CTag PROPFIND (remote) + 1 dir fingerprint
   (local), **zero** item fetches, zero file parses, zero writes. Note the
   remaining structural waste for later: each mapping currently runs
   `fetchItems` at least twice (worker gating fetch syncengine.cpp:2004 +
   `loadRecords`' internal reuse, `remotecalendarbackend.cpp:2020–2035`) —
   fold to one if the skip path doesn't already make it moot.

**Tag v0.82** after this phase; PlanStan pin bump + end-to-end (Track C4).

---

## Track C — PlanStan-side integration

(References are into `~/dev/PlanStan`; keep each phase's commit paired with
its tracking-doc update per PlanStan's docs/bugs convention.)

### Phase C1 — N6: register an `IMassDeleteGuard`  **[do first in this track — data-loss vector]**

PlanStan never calls `SyncEngine::setMassDeleteGuard`
(lib: `src/engine/syncengine.h:220`; interface
`src/engine/imassdeleteguard.h` — note its threading contract: called from
the worker thread, implementation must marshal to the GUI itself). Without
it, the engine's >10-or->25%-of-baseline delete gate is inert
(syncengine.cpp:2640–2665) — combined with N5 that was a silent
wipe-the-mirror vector.

Implement `PlanStan::SyncMassDeleteGuard` (suggested:
`src/controllers/syncmassdeleteguard.{h,cpp}`): `confirmMassDelete` marshals
via `QMetaObject::invokeMethod(qApp, …, Qt::BlockingQueuedConnection)` to a
`KMessageBox`/`QMessageBox` naming the mapping, target backend, and counts;
default-deny on headless/test runs unless an env override is set. Register in
`CollectionController::initializeSyncInfrastructure`
(`collectioncontroller.cpp:1759`, right after the `new SyncEngine` at
`:1820`). Headless test: fake guard records invocation; engine-level delete
storm (>10) is blocked when guard says no (lib already tests the gate —
PlanStan's test only asserts registration + marshalling doesn't deadlock).

### Phase C2 — N8: stop loading sync-spoke calendars into the model

`ItemLoadingCoordinator::loadAllItems`
(`src/controllers/itemloadingcoordinator.cpp:92–114`) iterates
`m_collection->calendars()` and loads **both** the local primary and the
remote sync1 calendar of every logical calendar → every incidence enters
`GlobalIncidenceModel` twice (86 `UID … now exists in 2 different calendars`
warnings per pass in the evidence log), plus a redundant full remote fetch at
open (the engine performs its own fetches for sync).

Rule: load a calendar iff it is some enabled logical calendar's **primary**
binding, or belongs to no logical calendar (loose/unbound). Terminal-mode
collections keep working because there the *remote* binding is primary.
Use `m_configManager->findLogicalCalendarByBinding(QString(), calId)`
(already consulted at `:102–107`) and compare against
`logCal.primaryBinding()`. Also audit `loadItemsForBackend` (`:77–90`,
already primary-only — good) and the sync-completion refresh path
(`itemloadingcoordinator.cpp:240ff`) for the same rule. Verify: mirror
collection open → zero duplicate-UID warnings; agenda still shows all items;
terminal collection still loads.

### Phase C3 — N9 + minor orchestration

- Auto-sync-on-load fires before sync infra exists:
  `collectioncontroller.cpp:467–494` (`runSync()` at `:494` logs `No sync run
  coordinator (single backend?)` — evidence log line 286). Defer until
  `initializeSyncInfrastructure` (`:1632→:1759`) has built the coordinator
  (connect once to whatever signal marks infra-ready, or queue a pending
  auto-sync flag consumed at the end of `initializeSyncInfrastructure`).
- Re-entry noise: `SyncRunCoordinator::runSync: sync already in progress`
  fires when the 120 s timer overlaps a long run — after Track B the cycles
  are short, but add a debounce/skip-with-log so an in-flight run is not
  queue-stacked.

### Phase C4 — pin bumps, end-to-end verification, collection recovery

At each lib tag (v0.80/v0.81/v0.82/v0.83): bump
`PLANSTAN_LIBKALBURATOR_GIT_TAG` (`PlanStan/CMakeLists.txt:69`), full PlanStan
suite (`WAYLAND_DISPLAY=wayland-0 ctest`, never force `QT_QPA_PLATFORM`),
update the relevant PlanStan bug docs (delete
`docs/bugs/libkalburator-calendar-canon-drops-vtodo.md` when A1+A2 verified;
update `docs/bugs/sync-nonconvergence-vtimezone-corruption-and-dav-transport.md`
per-finding; `docs/bugs/wizard-local-mirror-creation-flow.md` F4 line). Per
INVARIANTS §10, notify/coordinate the WildPalms pin separately.

**Live verification script (manual, after v0.82 + C1–C3):** recreate the
user's scenario — new account-based collection against the Nextcloud account
(my.opendesktop.org), local-mirror mode, pick the ~7 calendars incl. the
673-item one. Gates: all mirror files non-empty and parse; no RRULE lines in
mirrored one-off events; the 673-item calendar fully syncs (chunked
multigets); second and third 120 s ticks produce zero PUTs/conflicts and no
perceptible UI stall; kill -9 mid-session then reopen → no re-download storm,
no false deletes. **Recovery note:** collections created before v0.80 have
corrupted mirrors and poisoned baselines/CTags — recreate them (or wipe
`<name>.kalb.d/{calendars,cache}/*` + `sync.db*`) rather than trusting an
in-place resync.

---

## Landing detail (from the roadmap's §5 phase-status log)

- [x] A1 per-kind dispatch Tasks 4–9 (plan: 2026-06-28-calendar-per-kind-canon-dispatch.md) — merged to main 2026-07-03
- [x] A2 N1 component-scoped recurrence extraction — merged to main 2026-07-03 (same branch/merge as A1)
- [x] — tag v0.80
- [x] B1 N3 remote lastModified honesty — merged to main 2026-07-04
- [x] B2 N4 multiget chunking + error truth — merged to main 2026-07-04
- [x] B3 N5 CTag/content-cache coherence — merged to main 2026-07-04
- [x] — tag v0.81
- [x] B4 N2 per-side baselines — landed 2026-07-04 on branch
      `feature/sync-convergence-b4-baselines` (not yet merged/tagged). Schema
      bumped v5→**v6** (blob_baselines_v3 gained nullable source_hash/
      target_hash; the roadmap's original "schema v5" framing was stale —
      v5 was already taken by the K.5 collection_baselines work). New engine
      type `Kalburator::Engine::BaselineEntry`; `perRecordDiff` now diffs
      each side against its OWN baseline hash. Written-bytes hashes are
      captured via a post-write re-fetch through each backend's own
      `contentHash` (not an engine-computed hash — needed because not every
      backend recomputes a hash the same way on read, caught by
      `tst_contacts_engine_witness`). Implicit baseline seeding and
      `harvestBaselinesAfterFirstSync` (the first-sync mirror path) also
      fixed — both had the same single-shared-hash defect. Core regression
      gate `tests/engine/tst_sync_convergence.cpp::secondSyncIsNoOp` (real
      FakeCalDavServer + LocalBackend + SyncEngine, VEVENT+VTODO, verified
      RED against a reintroduced single-hash bug, then GREEN); plus 6 new
      `tst_perrecorddiff` per-side cases and 4 new `tst_baseline_store_v3`
      migration/round-trip cases. Full suite green, net +1 test binary +
      16 new cases in existing binaries, zero regressions.
- [x] B5 convergence gate + fast path — landed 2026-07-04 on branch
      `feature/sync-convergence-b5-acceptance-gate` (not yet merged/tagged).
      **Acceptance matrix** (`tests/engine/tst_sync_convergence.cpp`, +4 new
      cases beyond the existing `secondSyncIsNoOp`): `localEditPropagates
      ExactlyOncePut`, `remoteEditFetchesExactlyOneChangedItem`,
      `remoteDeleteRemovesExactlyOneLocally` (exercises the engine's
      built-in mass-delete threshold with NO guard registered — proceeds
      per the documented backward-compatible default, distinct from
      PlanStan's C1 guard), `fastPathSkipsGenuinelyUnchangedMapping`. All
      use a shared `ConvergenceFixture`/`makeConvergenceFixture()` rig
      (real FakeCalDavServer + CalDavProvider + RemoteCalendarBackend +
      LocalBackend + SyncEngine + BaselineStore, isolated per-test tmp
      dirs) and assert exact FakeCalDavServer request-count deltas (PUT/
      DELETE/REPORT/multigetReportCount/PROPFIND), not just pass/fail.
      **Fast path confirmed working** — `SyncEngine::prepareSyncFastPath`'s
      existing logic (predates this phase) was already structurally
      correct; the "of 7 mappings, 0 are unchanged" real-world
      under-reporting was traced to two separate, now-fixed causes: (1) a
      one-cycle staleness in `onWorkerSyncCompleted`'s revision write-back
      (fixed: now re-queries each side's LIVE `ChangeDetection::
      collectionRevision()` post-completion instead of persisting the
      pre-dispatch snapshot — syncengine.cpp), and (2) a genuinely new,
      MAJOR convergence bug found by the acceptance tests themselves:
      `RemoteCalendarBackend::loadRecords()` re-derived every record's raw
      bytes via `icalFromIncidence()` on every call, and KCalendarCore
      non-deterministically bakes a fresh DTSTAMP (and phantom CREATED/
      LAST-MODIFIED) into that re-serialization — making `contentHash`
      unstable across independent `loadRecords()` calls for byte-identical
      server content, silently defeating B4's per-side baselines whenever
      the "runs fetchItems at least twice per mapping" structural residual
      (below) landed in different wall-clock seconds (~30-40% of runs in
      a stress-test loop). Fixed by having `RemoteCalendarBackend` remember
      the last verbatim raw bytes served per uid
      (`m_lastRawIcsByUid`) and preferring that over re-derivation; a
      companion fix in `eventcanonfields.cpp`/`vtodocanonfields.cpp` makes
      the canon encoders trust literal property presence in the source
      bytes rather than KCalendarCore's parse-time-defaulted
      created()/lastModified() accessors. Full details + verification
      numbers: `docs/campaign/FINDINGS.md` 2026-07-04 entries. Telemetry:
      the "would skip unchanged mapping (flag off)" / "skipping unchanged
      mapping" qInfo lines the roadmap asked for already existed
      (pre-dating this phase, from the original Plan-8-era skip-eligibility
      feature) — confirmed sufficient, no new logging added.
      **Idle-cycle budget confirmed moot on the structural double-fetch**:
      when a mapping is skip-eligible and the flag is on, `advanceQueue`'s
      `m_skippedMappingIds` branch short-circuits BEFORE the mapping ever
      dispatches to the worker — `fetchItems()` (gating call + `loadRecords`'
      internal reuse) never runs even once. `fastPathSkipsGenuinelyUnchanged
      Mapping` asserts exactly +1 PROPFIND (prepareSyncFastPath's own CTag
      check) and zero REPORT/multiget/PUT/DELETE/local-file-touch on the
      truly-idle cycle. **One residual, documented characteristic (not a
      bug, not fixed)**: a BRAND NEW collection needs its first TWO real
      sync cycles before the fast path can engage on the third — cycle 1
      populates but can't commit the source's CTagStore yet (fetchItems'
      own commit-on-complete logic is gated on a pre-existing stored CTag
      to compare against); cycle 2 is a real, correctly-detected-as-no-op
      dispatch that finally commits it; cycle 3 is the first that can
      skip. See the fast-path test's doc comment for the full mechanism.
      Full suite: 157/157 (unchanged count — new cases live in the existing
      `tst_sync_convergence` binary/ctest entry); the new acceptance-matrix
      tests were run 40x in a stress loop post-fix with 0 failures (they
      were ~30-40% flaky before the `m_lastRawIcsByUid` fix, confirming the
      fix, not luck, closed it).
- [x] — tag v0.82 (libkalburator main @ `1e985e5`, 2026-07-04)
- [x] C1 PlanStan mass-delete guard — landed in PlanStan (`SyncMassDeleteGuard`,
      registered in `CollectionController::initializeSyncInfrastructure`);
      see PlanStan/CLAUDE.md sync-convergence campaign section
- [x] C2 PlanStan spoke-loading fix — `ItemLoadingCoordinator::shouldLoadCalendar()`
      gates `loadAllItems()` + the `calendarAdded` discovery hookup (2026-07-04).
      NOTE: C4 live verification found this was incomplete — the sync-side fetch
      path bypassed it; see C4.
- [x] C3 PlanStan auto-sync ordering — deferred auto-sync-on-load via a pending
      flag consumed once the sync coordinator exists; 120s timer skips when a
      sync is already in progress (2026-07-04, N9).
- [x] C4 pin bumps + live end-to-end + recovery-scope — PlanStan pinned to
      v0.82, `syncSkipUnchanged` flipped true. **First live run against the
      real Nextcloud multiproto mirror (2026-07-04): sync CONVERGED** (all
      mappings settle to `+0 ~0 -0` no-op). Two PlanStan-side issues found +
      fixed on that run: (a) **N8 residual** — C2's gate missed the shared
      `onItemFetched` chokepoint the sync-side fetch also travels, so a live
      sync still duplicated the spoke into the model; moved the gate there +
      added a post-initial-sync model reload (the engine's `recordChanged`
      apply callback is inert — tracked in PlanStan
      `docs/todo/sync-apply-phase-model-refresh.md`). (b) the "Discovered
      Calendars" dialog fired for wizard-unpicked calendars — added a
      per-collection ignore list in the `.kalb`. **Third PlanStan-side issue
      found on a follow-up live run (2026-07-04):** `SyncEngine::
      prepareSyncFastPath` never once engaged across 8 consecutive idle
      cycles on a fresh collection — a DISTINCT root cause from the DTSTAMP/
      stale-revision bugs this campaign's B4/B5 phases already fixed for the
      same-looking "of 7 mappings, 0 are unchanged" symptom. PlanStan's
      `initializeSyncInfrastructure()` wired `setDbPath()` into
      `RemoteCalendarBackend` but never into `LocalBackend`, so the local
      side of every mirror mapping could never report a cached fingerprint —
      permanently defeating the fast path for PlanStan's default
      local-mirror topology regardless of B4/B5. Fixed PlanStan-side (the
      missing `setDbPath()` call); **verified live on a clean reopen
      (2026-07-04)** — cycle 1 cold-starts as documented, cycle 2 converges
      to `7 are unchanged; 7 actually skipped` and holds. **C4 is now fully
      closed** for the fresh-creation + reopen happy path. Still open
      (separate, smaller item): collection recovery for pre-v0.80
      collections (recreate rather than in-place resync). See
      PlanStan/CLAUDE.md.
