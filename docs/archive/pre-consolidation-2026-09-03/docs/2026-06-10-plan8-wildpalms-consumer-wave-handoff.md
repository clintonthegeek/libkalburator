# Handoff → WildPalms: Plan 8 consumer wave (ISyncHost shims + runSyncFuture retirement)

**Date:** 2026-06-10
**From:** libkalburator dev (architectural-redress campaign, Plan 8)
**To:** WildPalms dev
**Companion docs:** `docs/2026-06-10-plan8-isynchost-runsyncfuture-consumer-wave-rfc.md`
(canonical RFC), `docs/2026-06-10-plan8-consumer-wave-response-planstan.md`
(PlanStan's ack). PlanStan's parallel wave is in flight; this is the WildPalms half.
**libkalburator tip:** `main` @ `7942d1e`, tag **v0.69** (pushed to Codeberg).
**WildPalms current pin:** `v0.66` (`CMakeLists.txt:63`).

---

## 1. What changed upstream (Plan 8 step 1, landed as v0.69)

`Kalburator::Sync::ISyncHost` — previously a header-only, stateless interface whose
`backendById`/`backends()` were **pure virtual** — gained registry-backed defaults:

```cpp
// isynchost.h (v0.69)
virtual void setBackendRegistry(BackendRegistry *registry);   // NEW, non-owning
virtual SyncBackend* backendById(const QString &id);          // now NON-pure; default:
                                                              //   dynamic_cast over registry
virtual QHash<QString, SyncBackend*> backends();              // now NON-pure; registry walk
protected:
    BackendRegistry *m_backendRegistry = nullptr;             // non-owning
// configStore() stays PURE — every host owns one.
```

The default `backendById` is exactly:

```cpp
if (!m_backendRegistry) return nullptr;
return dynamic_cast<SyncBackend*>(m_backendRegistry->backendInstance(id));
```

i.e. **identical** to the logic WildPalms already hand-rolls in `PalmSyncHost`. Step 1
is **purely additive**: no registry injected ⇒ defaults return `nullptr`/empty (never a
crash); every existing override keeps compiling and keeps its behaviour. The four
`SyncEngine::runSyncFuture(...)` overloads are now `[[deprecated]]` and will be **deleted
in step 3** (after both consumer waves confirm migration).

This handoff is the WildPalms half of the wave. It has **two parts**: (A) ISyncHost shim
handling — optional, low-risk, unblocked now; and (B) `runSyncFuture` retirement —
**required before WP bumps its pin past the step-3 deletion tag**, and the part with a
real cancellation caveat.

---

## 2. Part A — ISyncHost shims (unblocked now; mostly optional)

### A.0 — bump the pin first: v0.66 → v0.69 (safe, no code change)

Because step 1 is additive, **bumping `WILDPALMS_LIBKALBURATOR_GIT_TAG` to `v0.69`
compiles as-is** — every current WP override (`PalmSyncHost`, `SyncHost_WP`, the test
hosts) still satisfies the now-non-pure interface. Do this first to get WildPalms onto
current libkalburator (it is several releases behind; see §4). Parts A.1/A.2/B can follow
at WP's pace.

### A.1 — `PalmSyncHost` (src/runtime/palmruntime.cpp:87): delete the overrides

`PalmSyncHost` is **already** the registry + `dynamic_cast` shape that v0.69 ships as the
default, and it is constructed with the registry in hand (`palmruntime.cpp:178`:
`std::make_unique<PalmSyncHost>(m_registry.get())`). So the two overrides are now pure
duplication of the base default. Collapse them:

```cpp
class PalmSyncHost final : public Kalburator::Sync::ISyncHost {
public:
    explicit PalmSyncHost(Kalburator::Sync::BackendRegistry *registry) {
        setBackendRegistry(registry);   // base default now does the dynamic_cast lookup
    }
    Kalburator::Sync::ISyncConfigStore* configStore() override { return nullptr; }
    // backendById()/backends()/m_registry all DELETED — inherited from ISyncHost.
};
```

Behaviour is byte-for-byte identical (same `dynamic_cast<SyncBackend*>` over the same
registry, same nullptr-on-no-registry and non-calendar-omission semantics). Net: the host
drops to a ctor + `configStore()`.

### A.2 — `SyncHost_WP` (src/runtime/synchost_wp.{h,cpp}): KEEP (recommended)

`SyncHost_WP` is **not** registry-backed — its `backendById`/`backends()` read a local
`m_backends` `QHash` filled by `registerBackend(id, backend)`. That is **not equivalent**
to the lib default (which walks a `BackendRegistry`). To delete these overrides, WP would
have to re-route `registerBackend()` into a `BackendRegistry` and inject it via
`setBackendRegistry()`.

**Recommendation: keep `SyncHost_WP`'s overrides.** They compile unchanged against v0.69,
and the local-hash host is a legitimate non-registry implementation (this is the same
keep-the-override call PlanStan made for its `CollectionController`, which bridges a legacy
`m_backends` hash). Migrate to the registry only if WP independently wants to unify on
`BackendRegistry` — it is not required by this wave.

### A.3 — test hosts (no action required)

`tests/test_fullsync_synchost.cpp:69` and `tests/runtime/tst_palm_runtime_clobber_sync.cpp`
exercise host `backendById`. Overriding a now-non-pure virtual is fine; they keep working.
Optional cleanup only.

---

## 3. Part B — `runSyncFuture` retirement (gated; has a cancellation caveat)

WildPalms has **two** production `runSyncFuture` call sites, each using a *different*
deprecated overload. Both must move to the canonical `runSync(SyncRequest)` before WP
adopts the lib tag that deletes the overloads (step 3). Add `#include <syncrequest.h>`
to `palmruntime.cpp`.

`SyncRequest` (libkalburator `src/engine/syncrequest.h`) distinguishes dispatch shape by
`mappingIds`: empty = all-enabled, `>1` = subset, `==1` = single (the only shape that
consults `executionOverride.direction` in full).

### B.1 — `PalmRuntime::runAllMappings()` (palmruntime.cpp:916) — mechanical

Subset overload → subset `SyncRequest`. **Return type is unchanged**
(`QFuture<QList<SyncResult>>`), so the existing `.then([](QList<SyncResult> results){…})`
aggregation is untouched.

```cpp
// before
auto engineFuture = m_engine->runSyncFuture(
    ids, Kalburator::Sync::SyncEngine::SyncBehavior::Unmonitored);

// after
Kalburator::Engine::SyncRequest req;
req.mappingIds = ids;        // subset; if ids is ever empty this becomes all-enabled
req.behavior   = Kalburator::Sync::SyncEngine::SyncBehavior::Unmonitored;
auto engineFuture = m_engine->runSync(req);
```

### B.2 — `PalmRuntime::runMirror()` (palmruntime.cpp:1031) — return-type + cancel change

Single-mapping-with-override overload → single `SyncRequest`. **The return type changes**:
the deprecated `runSyncFuture(id, override)` returned `QFuture<SyncResult>` (a single
result); `runSync(SyncRequest)` always returns `QFuture<QList<SyncResult>>`. So the `.then`
lambda parameter changes from `SyncResult` to `QList<SyncResult>` and reads `.first()`:

```cpp
// before
auto engineFuture = m_engine->runSyncFuture(ids.first(), ov);
...
return engineFuture.then([this](Kalburator::Sync::SyncResult sr) { /* build PalmRunResult */ });

// after
Kalburator::Engine::SyncRequest req;
req.mappingIds        = { ids.first() };   // single-mapping dispatch
req.executionOverride = ov;                // direction override applies in full here
auto engineFuture = m_engine->runSync(req);
...
return engineFuture.then([this](QList<Kalburator::Sync::SyncResult> results) {
    Kalburator::Sync::SyncResult sr =
        results.isEmpty() ? Kalburator::Sync::SyncResult{} : results.first();
    /* build PalmRunResult from sr — body otherwise unchanged */
});
```

### B.3 — ⚠️ Cancellation caveat (read before migrating B.2)

The deprecated single-mapping `runSyncFuture(mappingId, …)` returns
`dispatchSingleNative()`'s future **verbatim**, so it natively carries the F2 Task 23
cancellation contract (`resultCount() == 1` after cancel, with `resultAt(0).cancelled ==
true`). The canonical `runSync(SyncRequest)` single-mapping branch instead **`.then()`-wraps**
that native future — and **Qt6's `QFuture::then()` drops the continuation on a canceled
source, so the cancellation result is lost through the wrapper** (libkalburator
`src/engine/syncengine.h:486-488` + campaign FINDINGS "From Plan 1").

`runMirror` consumes the engine future with `.then()` *and* installs
`m_activeSyncWatcher` (a `QFutureWatcher<void>` on `finished`). After migrating B.2:
- normal completion: `.then()` fires as today;
- on `cancelSync()`: the `.then()` continuation may **not** fire → `runFinished(r)` is
  never emitted → the UI can hang.

**Recommended robust pattern:** on the single-mapping path, stop using `.then()` for result
delivery and instead read the result in the existing `m_activeSyncWatcher::finished` slot —
that slot fires on **both** completion and cancel — via `engineFuture.resultAt(0)` (NOT
`.results()`, which is empty after cancel). Construct the `PalmRunResult` there, including
the cancelled branch. Add a test that drives `cancelSync()` mid-run and asserts
`runFinished` is still emitted.

(Site B.1 / `runAllMappings` returns the multi-result future and is less exposed, but it
uses the same `.then()` + watcher idiom — apply the same cancel-path check there.)

**Coordinate timing with the lib.** libkalburator hits this *same* `.then()`-on-cancel
issue in its own step-3 migration of single-mapping callers; the dual future-interface
collapse is the planned fix. So: WP lands Part B, confirms its cancel path with a test, and
notifies the lib. **The lib will not delete the four `[[deprecated]]` overloads until both
PlanStan and WildPalms confirm.** Until then WP must **not** bump its pin past the step-3
tag — doing so is a hard compile break by design (that's the gate).

### B.4 — test contract notes

For any new/edited sync tests:
- Wait via `QTRY_VERIFY_WITH_TIMEOUT(f.isFinished(), 5000)` — Qt6 `waitForFinished` does
  **not** spin the test event loop.
- Read results via `f.resultAt(0)`, **not** `f.results()` (empty after cancel).

---

## 4. Sequencing summary

| When | Action | Risk |
|---|---|---|
| **Now** (step 1 / v0.69 landed) | Bump pin v0.66 → v0.69 | none — additive, compiles as-is |
| Now, optional | A.1 delete `PalmSyncHost` overrides → `setBackendRegistry` in ctor | none — identical behaviour |
| Now, optional | A.2 `SyncHost_WP`: keep (recommended) | n/a |
| **Before the step-3 tag** | B.1 migrate `runAllMappings` (mechanical) | low |
| **Before the step-3 tag** | B.2 migrate `runMirror` (return-type + B.3 cancel path) | **medium — verify cancel** |
| After both consumers confirm | lib deletes the four overloads (step 3) | — |

## 5. WildPalms pin reality (FYI)

Per libkalburator auto-memory, WildPalms `origin/main` is ~96 commits behind; the local
v0.65 → v0.66 pin bumps are committed but **unpushed**. Recommend pushing WP's outstanding
commits together with the v0.69 bump so the clone-gate / invariant-10 downstream gate runs
against current pins.

## 6. Questions back to the lib

If the single-mapping cancel-through-`.then()` loss is awkward to work around in
`runMirror`, say so — the lib can prioritise the dual future-interface collapse (FINDINGS
"From Plan 1") so the canonical single-mapping path preserves the native cancellation
result, removing the need for the watcher-based workaround. That is on the step-3 table
already; WP's needs can pull it forward.
