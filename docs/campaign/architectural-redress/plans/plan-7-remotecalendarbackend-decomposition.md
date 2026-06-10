# Plan 7 — `RemoteCalendarBackend` decomposition

**Audit refs:** B3 (MAJOR, corrected + 2026-06-06 stale-evidence annotation), supplement S4
(13 containers / state triplication), S2 (backend-internal half), the AUDIT MAJOR
"11× QEventLoop boilerplate → extract `sendCustomRequestSync`", and the MODERATE
"three seed-before-use verbs" (this plan deletes one of the three).
**Branch:** `feature/redress-7-remotecalendarbackend-decomposition`
**Baseline at open:** `main` @ `14b3dba`, ctest **144/144**.
**State:** plan written 2026-06-10 against the CURRENT tree (2718 cpp / 472 h), per the
STATUS requirement that Plan 7 be planned against the post-v0.63 code, not audit B3's
2026-05-29 line numbers.

## Scope decision (documented deviation)

AUDIT B3 names two god classes: `RemoteCalendarBackend` AND `LocalBackend` (~1300 LOC).
**This plan decomposes `RemoteCalendarBackend` only.** Rationale (INVARIANTS "Scope and
exceptions", citing P3): one plan must land green as one coherent unit; the RCB work is
already session-sized, and the LocalBackend mirror (fingerprint cache, 4 metadata
setters) shares no code with this plan — splitting loses nothing. The deferral is
recorded in FINDINGS at close-out with a mirror-task sketch so Plan 11 (or a Plan 7b)
can pick it up against post-Plan-7 idioms.

## The Plan-1 lesson, made a requirement

Plan 1 (SyncEngine) decomposed by **adding collaborator classes** and keeping
deprecated forwarding shims; the engine surface grew +629 LOC ("richer structure,
fewer responsibilities per file"). The structure won but the metric lost, and STATUS
has since logged the engine *regrowing* (2846 → 2933). For Plan 7 the user-set goal is
explicit: **better organized AND smaller.** Therefore this plan is sequenced
subtract-first, and net LOC is an acceptance gate, not an aspiration:

- **Gate:** net LOC across all touched `src/` files (including new files) ≤ **−350**
  versus the 3190 baseline (2718 cpp + 472 h). Stretch: −500.
- **Gate:** `remotecalendarbackend.cpp` ≤ 2100; `remotecalendarbackend.h` ≤ 410.
- **Gate:** `QEventLoop` construction sites in `remotecalendarbackend.cpp`: 11 → ≤ 2
  (one in the shared sync-request helper, one in the operation-await helper).
- **Gate:** RCB-specific public methods beyond interface obligations: 16 → 9, each
  survivor with a named consumer (table below).
- **Gate:** private stateful members: 13 → ≤ 6.

## Why not the textbook collaborator split (approach comparison)

Three shapes were considered. The reasoning is committed here per INVARIANTS §7.

**A. Archived-plan shape** (archive/plans-2026-05-28: extract `CalDavCTagStore`,
`CalendarDiscoveryCache`, `MetadataFactory` as injected collaborators; deprecate old
getters, delete in a later sweep). Rejected as the *primary* mechanism: it is exactly
the Plan-1 LOC-inflation pattern — every extracted QObject/DI collaborator costs a
header, includes, ctor plumbing, and forwarding shims that keep the god surface alive
until "a later plan" (which then regrows first, cf. SyncEngine). Also: the archived
plan predates the verified audit and v0.63; its premise that the six getters can be
deprecated is wrong — most survivors are base-class `override`s or live PlanStan calls
(consumer table below), so deprecation would just be churn.

**B. Pure subtraction** (delete dead code, collapse duplication into file-local
helpers, privatize, extract nothing). Rejected as *insufficient*: the SQLite content
cache is a genuinely separate machine (own connection lifecycle, own schema, zero
reads of backend state beyond a seed string) living inside a network backend — that is
the one place where a real collaborator pays for its boilerplate several times over,
and extraction also fixes a real defect (the per-instance cache DB connection is
**never closed** — `~RemoteCalendarBackend` is `= default` — leaking registered
QSqlDatabase connections per backend instance).

**C. Subtract-first hybrid — CHOSEN.** Order: delete verified-dead code → collapse
duplication into anonymous-namespace helpers and private methods → extract exactly one
collaborator (`CalDavContentCache`) → unify the per-calendar state → shrink the public
surface. The audit's fix direction is honored in substance:

- "*extract a `DiscoveredCalendarInfo` DTO*" → the per-calendar facts unify into ONE
  `QMap<QString, CalendarFacts>` (kills the 4-parallel-map S2 smell inside the
  backend); the public seed DTO already exists (`PrimedCalendar`, v0.63, tested) and
  stays. The seven `discoveredX` getters cannot collapse further today: five are
  interface `override`s, two are PlanStan production calls — the *getter*-collapse is
  Plan 8 consumer-wave material and is noted there.
- "*split IBlobBackend / ChangeDetection / calendar-CRUD into collaborators*" → the
  duplicated *surfaces* are what actually split: ChangeDetection keeps exactly one
  public face (the interface; the backend's own duplicate ctag API goes private or
  dies), the CRUD and blob paths are rebuilt on the shared `davSyncRequest`/codec
  helpers. A literal three-QObject split would add back-pointers and signal plumbing —
  the inv-3 "one-and-a-half collaborators" hazard — and is declined with this rationale
  as the documented deviation.

## Consumer contract — what MUST keep compiling and behaving

Verified by grep across libkalburator `src/ tests/ examples/`, PlanStan
`src/ tests/`, WildPalms `src/ tests/` (2026-06-10):

| Surface | Consumer | Disposition |
|---|---|---|
| ctor `(QUrl, user, pass, parent)` | PlanStan `collectioncontroller.cpp:1108`, lib providers, tests | keep |
| `create()` factory | BackendRegistry registration | keep |
| `setDbPath` / `setCacheDir` | PlanStan `collectioncontroller.cpp:1626-1629` | keep |
| `createCalendar(...)` | PlanStan `backenddiscoverycoordinator.cpp:248` | keep (rebuilt internals) |
| `discoveredUrl`, `discoveredSupportsEvents/Todos` | PlanStan `backenddiscoveryhelper.cpp:83-85` | keep |
| `discoveredColor/CalendarType/Writable`, `calendarColor/Description` | `SyncBackend` overrides | keep |
| `startSync` | **PlanStan PROD** `stagingcontroller.cpp:307` + live test | keep (rebuilt internals; signals pinned by new T1 test) |
| `removeItem`, `storeCalendars`, `loadCalendars` | `SyncBackend` pure virtuals | keep |
| `getRawIcs`/`setRawIcs` | PlanStan `incidencecontextmenubuilder.cpp:233/245` (via base) | keep |
| operations API + IBlobBackend + ChangeDetection overrides | engine, blob tests | keep |
| `primeCalendars` + `PrimedCalendar` | CalDav/MultiProto providers (v0.63) | keep |
| `registerCalendarUrl` | providers + PlanStan-adjacent flows + tests | keep |
| **`tst_remotecalendarbackend_convergence`** | v0.63 contract | **preserve verbatim — file untouched** |
| **`tst_remotecalendarbackend_blob_view`** | blob contract | **preserve verbatim — file untouched** |

Zero-caller verdicts (the deletion warrant — each grep covered lib + PlanStan +
WildPalms, src and tests):

| Symbol | Evidence |
|---|---|
| `primeCtagCache()` + `PrimedCtag` + `m_primedCtags` + `kPrimedCtagFreshnessMs` + the fetchItems primed-ctag fast path | zero callers anywhere; the engine fast-path uses `ChangeDetection::collectionRevisions`/`cachedCollectionRevision`/`primeRevisionCache` only (`syncengine.cpp:761-830,1218`) — the 60 s freshness path can never engage in production |
| `discoveredCtag()` | zero callers (not an override) |
| `currentEtags()` | zero callers |
| `runJobsSequentially()` | zero callers (cpp:1221-1267) |
| `clearCachedContentForCalendar()` | zero callers (cpp:478-499) |
| `m_etags`, `m_itemUrls` | written never, only `.remove()`d (cpp:1200-1201) |
| `m_configuredCollectionUrls` | declared (h:461), referenced nowhere |
| `etagForItem` decl (h:452) + commented-out `configuredDavUrl` overload decl (h:453) | declaration without definition / dead comment |
| ctag cluster `ctag/setCtag/clearCtag/fetchAllCtags` | zero EXTERNAL callers → go **private** (internally load-bearing) |

API note for the release log: `primeCtagCache`, `discoveredCtag`, `currentEtags` are
public-symbol removals. Both consumers verified clean; this rides the next minor tag.

## Duplication ledger (what consolidates, where it lives)

| Pattern | Sites today | Consolidation |
|---|---|---|
| QNAM + Basic-auth + QEventLoop sync request | 7 (`fetchAllCtags`, `createCalendar`, `updateCalendar`, `deleteCalendar`, `fetchItems` inline PROPFIND, `getRawIcs`, `setRawIcs`) + `modifiedSince` duplicate | anonymous-ns `davSyncRequest()` returning `DavResponse` |
| Depth:0 getctag PROPFIND + parse | 2 verbatim copies (`fetchItems` ~1698-1738, `modifiedSince` ~2653-2694) | private `fetchFreshCtag(calendarId)` |
| getctag multistatus XML parse | 2 (QXmlStreamReader in `fetchAllCtags`; regex in the Depth:0 copies) | anonymous-ns `parseCtagMultistatus()` (stream-reader based) |
| Incidence↔iCal via throwaway `MemoryCalendar` (incl. the no-op-deleter + manual `delete` dance) | 6+ (`startSync` lambda, `pushItems`, `serveCachedItems`, `fetchItems` ×2, `loadRecords`, `createRecord`) | anonymous-ns `icalFromIncidence()` / `incidencesFromIcal()` |
| post-write etag + content-cache bookkeeping | 6 (`startSync` ×3 lambdas, `pushItems`, `setRawIcs`) | private `noteItemWritten(urlKey, etag, ical)` |
| post-delete etag + cache eviction | 4 (`startSync` delete, `removeItem`, `deleteItems`, `fetchItems` deleted-loop) | private `noteItemErased(urlKey)` |
| `startSync` 412-retry twins (`forceUpdateIncidence` vs `startUpdateJobForIncidence`, ~40 LOC each, differ only in etag) | 2 | one `launchModifyForStartSync(inc, etag, checkDone)` |
| QEventLoop wait on `SyncOperation::finished` | 3 (`loadRecords`, `createRecord`, `deleteRecord`) | anonymous-ns `awaitOperation(SyncOperation*)` |
| principal-path calendar-URL building | 3 (`createCalendar`, `updateCalendar`, `deleteCalendar` fallback) | private `calendarUrlForCrud(calendarId)` |

Behavioral normalizations bundled into the consolidation (each deliberate, named
here per inv 6/7): (a) `deleteItems` and `removeItem` will now also evict the content-
cache row (today only `startSync`-delete and the fetch deleted-loop do; the orphan rows
were harmless-but-garbage since cache hits are etag-keyed); (b) the two regex getctag
parsers unify on the stream-reader parser. Nothing else changes observable behavior.

## End-state shape

```
src/calendar/remotecalendarbackend.h     ~390 LOC  (public: interface overrides + the
                                                    9 consumer-verified RCB methods;
                                                    private: ctag cluster, helpers,
                                                    QMap<QString, CalendarFacts>)
src/calendar/remotecalendarbackend.cpp   ~2000 LOC (CTagStore stays file-local;
                                                    anonymous-ns: davSyncRequest,
                                                    parseCtagMultistatus, iCal codec,
                                                    awaitOperation)
src/calendar/caldavcontentcache.h        ~70 LOC   (new collaborator, no QObject)
src/calendar/caldavcontentcache.cpp      ~180 LOC
```

Private state after T6 (≤ 6 stateful members + 3 credential scalars):

```cpp
std::unique_ptr<CTagStore> m_ctags;            // persistent ctag store (unchanged)
std::unique_ptr<CalDavContentCache> m_contentCache;
QMap<QString, CalendarFacts> m_calendars;      // davUrl+color+contentTypes+pendingCtag
QStringList m_primedCalendarIds;               // ordered primed replay (v0.63)
std::shared_ptr<KDAV::EtagCache> m_etagCache;  // KDAV delta detection (write-mostly)
QMap<QString, QString> m_localEtags;           // readable etag mirror — REQUIRED:
                                               // KDAV::EtagCache has no public getter
```

The `m_localEtags`/`m_etagCache` pair survives deliberately; the why (KDAV's
`EtagCache` exposes `setEtag/contains/etagChanged/removeEtag` but **no
`etag(url) → QString`**) gets a comment at the declaration so the "ETags×2" stops
reading as accidental.

```cpp
struct CalendarFacts {
    KDAV::DavUrl davUrl;     // empty url == not registered (see davUrlFor())
    QColor color;            // invalid == undiscovered (matches old map-miss)
    KDAV::DavCollection::ContentTypes contentTypes = {};
    bool hasContentTypes = false;  // preserves "absent → assume both" tri-state
    QString pendingCtag;     // discovery/fetch ctag awaiting persist-after-success
};
std::optional<KDAV::DavUrl> davUrlFor(const QString &calendarId) const;
```

`QMap` (not `QHash`) keeps the key-sorted iteration the old `m_davUrls` had —
`availableCollections()` ordering and the `loadRecord`/`updateRecord`/`deleteRecord`
first-match-wins scans stay deterministic and identical.

## Tasks

Each task = one commit, tree green (full `ctest` via `build/`, `-j 8` per the
machine rule, never `--parallel`). Convergence + blob_view test files are not edited
by any task.

### T1 — Protective tests first (inv 6)

The legacy write path (`startSync`, `removeItem`) is PlanStan-production-load-bearing
but covered only by the dark live-Radicale lane; the calendar CRUD trio likewise. T3/T4
rewrite their internals, so pin them in the default lane first.

1. Extend `tests/sync/fakecaldavserver.{h,cpp}`:
   - `MKCALENDAR` → if href already in `m_calendars` reply **405**, else register and
     reply **201**.
   - `PROPPATCH` → reply **207** with a minimal multistatus body.
   - `DELETE` on a registered *collection* href → unregister + **204**; unknown
     collection href → **404** (item DELETE behavior unchanged).
2. New `tests/sync/tst_remotecalendarbackend_writepaths.cpp` (CMake: clone the
   convergence-test stanza), slots:
   - `startSync_creations_reach_server_with_signal_contract` — 2 creations; spy
     `writeStarted(calId,2)`, 2× `writeProgressChanged`, `itemLoaded` ×2,
     `syncCompleted(collectionId)`; `server.hasEvent()` both.
   - `startSync_deletions_remove_from_server` — seed 1, stagedDeletions {uid→etag};
     `itemRemoved` + `syncCompleted`; `!server.hasEvent()`.
   - `startSync_empty_stages_completes_immediately` — `syncCompleted` synchronously.
   - `startSync_unknown_calendar_completes_without_jobs`.
   - `removeItem_deletes_and_emits_itemRemoved`.
   - `createCalendar_201_registers_url_and_emits` — `calendarCreated` +
     `calendarDiscovered`; follow-up `discoveredUrl()` non-empty.
   - `createCalendar_405_is_idempotent_success`.
   - `updateCalendar_proppatch_updates_color_cache` — returns true;
     `calendarColor()` reflects the new color; `calendarUpdated` emitted.
   - `deleteCalendar_204_unregisters` / `deleteCalendar_404_returns_false`.
3. Falsifiability check (not committed): comment out `emit syncCompleted` in the
   empty-stage early-return → the empty-stages slot must go red; restore.
4. Full suite: expect 145/145.

### T2 — Delete the verified-dead code

Pure subtraction, no behavior change. Header: drop decls/doc-blocks for
`currentEtags` (h:283), `discoveredCtag` (h:127-136), `primeCtagCache` (h:154-163),
`PrimedCtag`+`m_primedCtags`+`kPrimedCtagFreshnessMs` (h:394-402),
`runJobsSequentially` (h:456-458), `etagForItem` + dead comment (h:452-453),
`clearCachedContentForCalendar` (h:429-430), `m_configuredCollectionUrls` (h:460-461),
`m_etags` (h:463-464), `m_itemUrls` (h:466-467). Cpp: bodies at 329-332, 617-620,
622-630, 1221-1267, 478-499; in `fetchItems` remove the primed-ctag fast path
(~1684-1696: the `freshFromPrimedCache` flag and its branch — the remaining logic
becomes `if (!storedCtag.isEmpty()) { …PROPFIND… }` with the cache-match check
unchanged); in the `startSync` delete-callback drop lines 1197+1200-1201 (`itemKey`,
`m_etags.remove`, `m_itemUrls.remove`; line 1196's `calendarId` read stays — it feeds
`itemRemoved`). In `primeCalendars`' doc comment, drop the now-stale primeCtagCache
cross-reference (h:168-171). Expected: ≈ −175 LOC. Suite 145/145; convergence
untouched-and-green proves the primed-CALENDARS path (different machinery) is intact.

### T3 — One synchronous DAV request helper; rebuild the 8 boilerplate sites

Anonymous namespace in `remotecalendarbackend.cpp`:

```cpp
struct DavResponse {
    int status = 0;                       // HTTP status; 0 = no HTTP response
    QByteArray body;
    QString etag;                         // response ETag header, unquoted
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    QString errorString;
    bool transportOk() const { return error == QNetworkReply::NoError; }
};

DavResponse davSyncRequest(const QUrl &url, const QByteArray &verb,
                           const QString &username, const QString &password,
                           const QByteArray &body = {},
                           const QList<std::pair<QByteArray, QByteArray>> &rawHeaders = {},
                           const QByteArray &contentType =
                               QByteArrayLiteral("application/xml; charset=utf-8"));
```

Implementation: stack QNAM (per-call, as today), credentials stripped from the URL
and sent as a Basic `Authorization` header (as today), `sendCustomRequest`,
QEventLoop until `finished`, ETag header unquoted into `.etag`. Plus
`QMap<QString, QString> parseCtagMultistatus(const QByteArray &xml)` — the
stream-reader href→ctag parser lifted from `fetchAllCtags`.

Rebuild on it, preserving each site's status-code policy exactly:

- `fetchAllCtags` — per-group `davSyncRequest(parent, "PROPFIND", …, {{"Depth","1"}})`
  + `parseCtagMultistatus` + existing href match-back.
- new private `QString fetchFreshCtag(const QString &calendarId)` — Depth:0 variant;
  `fetchItems` and `modifiedSince` call it (deletes both inline copies and the regex
  parser).
- `createCalendar` (MKCALENDAR; 201 / 405 / 409 policy verbatim), `updateCalendar`
  (PROPPATCH; 207/200/204), `deleteCalendar` (DELETE; 200/204 vs 404) — each also
  gains private `calendarUrlForCrud(calendarId)` for the shared principal-path
  construction (delete-side keeps its prefer-discovered-URL branch).
- `getRawIcs` ("GET", 200→body), `setRawIcs` ("PUT", If-Match when cached etag
  exists, 200/201/204; new etag ← `.etag`, empty → evict, ctag cleared on success —
  all verbatim policy).

Expected ≈ −190 LOC. T1's CRUD/raw-write tests + convergence + blob_view +
caldav_provider/integration pin behavior. Suite 145/145.

### T4 — Codec + bookkeeping helpers; collapse the write-path duplication

1. Anonymous namespace:

```cpp
QByteArray icalFromIncidence(const KCalendarCore::Incidence::Ptr &inc);
QList<KCalendarCore::Incidence::Ptr> incidencesFromIcal(const QString &ical);
bool awaitOperation(SyncOperation *op);   // returns op->state()==Succeeded
```

`icalFromIncidence`/`incidencesFromIcal` use an owning
`KCalendarCore::Calendar::Ptr cal(new KCalendarCore::MemoryCalendar(...))` — the six
no-op-deleter + manual-`delete` dances go away. `awaitOperation` spins the local
QEventLoop on `SyncOperation::finished` iff `!op->isFinished()` (the `loadRecords` /
`createRecord` / `deleteRecord` copies, verbatim).

2. Private methods:

```cpp
void noteItemWritten(const QString &urlKey, const QString &etag, const QString &ical);
void noteItemErased(const QString &urlKey);
```

`noteItemWritten`: `m_etagCache->setEtag` + `m_localEtags[urlKey]` + cache `store`
(skipped when etag empty — matching today's guards). `noteItemErased`: removeEtag +
`m_localEtags.remove` + cache `remove` (the deliberate normalization for
`deleteItems`/`removeItem` named above). Rewrite all 10 bookkeeping sites onto them.

3. In `startSync`: replace `forceUpdateIncidence` + `startUpdateJobForIncidence` with
one `launchModifyForStartSync(inc, etag, checkDone)` member-lambda (callers pass the
cached etag or `QStringLiteral("*")`); rebuild the create/update/delete loops on the
codec + bookkeeping helpers. The signal contract (`writeStarted` /
`writeProgressChanged` / `itemLoaded` / `itemRemoved` / `syncCompleted`) and the
412-fallback policies are pinned by T1 — do **not** attempt to merge `startSync` onto
`pushItems` (different completion/signal contracts; PlanStan staging UI consumes
these signals; documented as the boundary of this consolidation).

Expected ≈ −150 LOC. Suite 145/145.

### T5 — Extract `CalDavContentCache`

New `src/calendar/caldavcontentcache.{h,cpp}` (no QObject, no KDAV — Qt Sql/Core
only), registered in root `CMakeLists.txt` beside the RCB entries (h ~line 154,
cpp ~line 191):

```cpp
namespace Kalburator::Sync {
class CalDavContentCache {
public:
    explicit CalDavContentCache(const QString &accountSeed); // url.host()+url.path()
    ~CalDavContentCache();                 // closes + removes the QSqlDatabase
                                           // connection (fixes the leak: the old
                                           // backend never closed it)
    void setCacheDir(const QString &dir);  // before first ensureOpen()
    bool ensureOpen();                     // lazy, idempotent; FNV-1a filename
    QString content(const QString &itemUrl, const QString &expectedEtag) const;
    void store(const QString &itemUrl, const QString &etag, const QString &ical);
    void remove(const QString &itemUrl);
    struct Row { QString url; QString ical; };
    QList<Row> rowsByPathFragment(const QString &pathFragment) const;
private:
    QString m_seed, m_dirOverride, m_connectionName;
    bool m_open = false;
};
}
```

Move verbatim: `stableContentCacheHash` (FNV-1a), the schema/init logic
(`initContentCache`), `getCachedContent` → `content`, `setCachedContent` → `store`,
`removeCachedContent` → `remove`, and the SQL half of `serveCachedItems` →
`rowsByPathFragment` (LIKE `%fragment%`, as today). RCB drops those five private
methods + `m_cacheConnectionName`/`m_cacheInitialized`/`m_cacheDirOverride`, holds
`std::unique_ptr<CalDavContentCache> m_contentCache` constructed in the ctor with the
seed; `setCacheDir` forwards; `serveCachedItems` keeps only the parse+`itemFetched`
loop over `rowsByPathFragment`. The convergence suite's filename-determinism and
cancellation-cache-integrity slots are the protective pin (they assert the on-disk
filename and schema validity through the public fetch path).

Net ≈ +30 LOC here (boilerplate cost), `remotecalendarbackend.cpp` −≈190.
Suite 145/145.

### T6 — Unify per-calendar state; shrink the public surface; header diet

1. Introduce `CalendarFacts` + `QMap<QString, CalendarFacts> m_calendars` +
   `davUrlFor()` (shapes above). Mechanically rewrite every `m_davUrls` /
   `m_calendarColors` / `m_calendarContentTypes` / `m_calendarCtags` site
   (`m_calendarCtags` → `pendingCtag` field; the `hasContentTypes` flag preserves the
   "unknown calendar → supports both" defaults in `discoveredSupportsEvents/Todos/
   CalendarType`). `availableCollections()` iterates `m_calendars`, skipping entries
   without a DAV URL — with `QMap`, ordering is unchanged.
2. Move to `private:`: `ctag`, `setCtag`, `clearCtag`, `fetchAllCtags`. Move the four
   inline `ChangeDetection` override bodies (h:198-216) into the cpp.
3. Header diet: drop `#include <QSqlDatabase>` (now cache-internal); audit the rest
   (`QPointer` usage post-T4); forward-declare `CalDavContentCache`.
4. Recount the gates (`wc -l`, public-method census, member census, `grep -c
   QEventLoop`). Suite 145/145; `compile_commands.json` regenerated; clangd clean on
   touched TUs.

### T7 — Close-out, gates, merge

1. Metrics table (before/after for every gate above) — into this plan file's
   "Outcome" section.
2. `FINDINGS.md` appendix: LocalBackend deferral + mirror sketch; the
   "self-priming `collectionRevisions`" perf idea (the engine's fresh PROPFIND could
   pre-warm the per-fetch ctag check — the machinery T2 deleted could return *live*
   via the ChangeDetection path if measurements ever justify it); `pushItems` has no
   412-update fallback by design (creates only — updates flow `updateRecord` →
   `setRawIcs`; asymmetry named); FakeCalDavServer still doesn't enforce
   `If-Match`/`If-None-Match`, so the 412 fallback paths remain dark-lane-only.
3. `STATUS.md`: Plan 7 row → DONE, "Next action" → Plan 8 prep; same commit as the
   plan-state change. `AUDIT.md` B3: RESOLVED-for-RCB annotation with commit hash,
   LocalBackend remainder noted.
4. Gates: full ctest (expect 145/145); **PlanStan** `ctest` (reachable surface —
   header changed; run from its build dir, `-j 8`), compare failed-set to its
   documented headless baseline; **WildPalms** — grep-verified non-consumer of every
   changed symbol (only a comment mentions RCB), so the five invariants hold by
   construction; state this in STATUS rather than running the 96-commits-behind gate.
   If a live Radicale answers on :5232, also run the dark-lane
   `tst_remotecalendarbackend` once.
5. Merge `--no-ff` to `main`, push, delete branch.

## Risks

- **412-retry paths are rebuilt with only dark-lane coverage** (fake server can't
  force 412). Mitigation: T4 collapses only the two *textually twin* lambdas and
  keeps the retry decision logic verbatim at the call sites; diff-review the collapsed
  lambda against both originals; optional live-lane run at T7.
- **`startSync` signal contract regression would hit PlanStan staging.** Mitigation:
  T1 pins the full signal sequence before any rewrite.
- **Facts-map unification changes a fallback subtly** (the tri-state contentTypes,
  invalid-color, URL-presence semantics). Mitigation: the `hasContentTypes` flag +
  `davUrlFor()` are designed to reproduce each old map-miss behavior; T1/T6 suite runs
  cover the getter paths via provider tests (`tst_caldav_provider` asserts discovered
  metadata).
- **Public-symbol deletions** (`primeCtagCache`, `discoveredCtag`, `currentEtags`).
  Both consumers grep-verified clean; rides the next minor tag with a release note.

## Outcome

_To be filled at T7._
