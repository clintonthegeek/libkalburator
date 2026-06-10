# Plan 8 — `ISyncHost` neutralization + `runSyncFuture` retirement (consumer wave)

**Audit refs:** FINDINGS "From Plan 3" (the `backendById` unchecked-cast hazard,
partially burned by the v0.66 WildPalms incident), supplement §Plan 8 prep,
STATUS "Plan 8 prep" (PlanStan-first resequencing).
**RFC / ack:** `docs/2026-06-10-plan8-isynchost-runsyncfuture-consumer-wave-rfc.md`
(ours) + `docs/2026-06-10-plan8-consumer-wave-response-planstan.md` (PlanStan's ack,
their `91774225`; step-2 window by 2026-06-14).
**Branch (step 1):** `feature/redress-8-isynchost-neutralization`
**Baseline at open:** `main` @ `568d543`, ctest **146/146**.

This plan is a CONSUMER WAVE — only **step 1 is lib-side and detailed here** (P1).
Steps 2–3 are gated on PlanStan's wave and get task detail when their closing note
arrives:

- **Step 2 (theirs, by 2026-06-14):** PlanStan migrates its `backendById` call sites
  + the `runSyncFuture` sites; WildPalms deletes its two shim overrides + migrates
  `palmruntime.cpp:916/:1031`. Tracked in PlanStan
  `docs/todo/plan8-isynchost-runsyncfuture-migration-wave.md`.
- **Step 3 (ours, after their closing note):** migrate the lib's own
  `syncruncoordinator.cpp:60` + ~87 test sites + `examples/reference_consumer` off
  `runSyncFuture`, then delete the four `[[deprecated]]` overloads (and collapse the
  engine's dual future-interface members per FINDINGS "From Plan 1").

## Step 1 — non-pure `ISyncHost::backendById`/`backends()` with registry defaults

### Constraints (from the ack — binding)

1. **Additive, not a behavior swap.** Existing overriders (PlanStan CC, WildPalms'
   two shims, 10 lib test hosts, `examples/reference_consumer`) keep compiling and
   keep their behavior untouched. PlanStan's CC override bridges its legacy
   `m_backends` hash, which is NOT equivalent to the registry today (registration
   gated on `>1` backend) — whether their override later dies or becomes a permanent
   cache is **their in-wave decision**; nothing in step 1 may assume either.
2. **`dynamic_cast`, not `static_cast`** — a non-calendar backend through the default
   is a clean nullptr, not UB (v0.66 engine precedent).
3. No registry injected ⇒ defaults return nullptr / empty (a host that neither
   overrides nor injects gets safe emptiness, never a crash).

### Design

`src/calendar/isynchost.h` (interface, currently header-only and stateless) gains:

```cpp
class BackendRegistry;   // fwd decl — sync/backendregistry.h included in the new .cpp only

    // ---- Registry access ----
    /// Inject the registry backing the default lookups. Non-owning: the host
    /// app owns the registry and must keep it alive for this host's lifetime
    /// (or inject nullptr first). Plan 8 step 1 (RFC 2026-06-10, acked).
    virtual void setBackendRegistry(BackendRegistry *registry);

    /// Default: registry lookup + dynamic_cast<SyncBackend*> — nullptr when no
    /// registry is injected, the id is unknown, or the instance is not a
    /// calendar-typed backend (clean miss, never UB).
    virtual SyncBackend* backendById(const QString &id);

    /// Default: registry walk with the same cast; non-calendar instances are
    /// OMITTED (not inserted as nullptr).
    virtual QHash<QString, SyncBackend*> backends();

protected:
    BackendRegistry *m_backendRegistry = nullptr;  ///< non-owning (see setBackendRegistry)
```

`configStore()` stays pure (every host genuinely owns one). The calendar→sync
include is the established neutral-interface direction (`calendar/syncbackend.h`
already inherits `sync/syncbackendbase.h`); the header keeps a forward declaration
and the new `src/calendar/isynchost.cpp` does the include (registered in
CMakeLists beside `isynchost.h`). Bodies:

```cpp
void ISyncHost::setBackendRegistry(BackendRegistry *registry)
{ m_backendRegistry = registry; }

SyncBackend* ISyncHost::backendById(const QString &id)
{
    if (!m_backendRegistry) return nullptr;
    return dynamic_cast<SyncBackend*>(m_backendRegistry->backendInstance(id));
}

QHash<QString, SyncBackend*> ISyncHost::backends()
{
    QHash<QString, SyncBackend*> result;
    if (!m_backendRegistry) return result;
    const QStringList ids = m_backendRegistry->registeredInstanceIds();
    for (const QString &id : ids) {
        if (auto *cal = dynamic_cast<SyncBackend*>(m_backendRegistry->backendInstance(id)))
            result.insert(id, cal);
    }
    return result;
}
```

### Tasks

**T1 — implementation + pinning test, one commit.** New
`tests/sync/tst_isynchost_defaults.cpp` (single-source `kalburator_add_sync_test`),
host = minimal `ISyncHost` subclass overriding ONLY `configStore()` (returns
nullptr — unused), backends = `MockBackend` (calendar-typed) and `RawFilesBackend`
over a `QTemporaryDir` (neutral base — the would-be-UB case). Slots:

- `defaults_without_registry_are_empty` — nullptr + empty hash, no crash.
- `backendById_finds_calendar_backend_via_registry`.
- `backendById_base_only_backend_is_clean_nullptr` — the hazard the RFC retires.
- `backends_walks_registry_omitting_non_calendar` — Mock present, RawFiles absent.
- `setBackendRegistry_nullptr_resets_to_empty`.

Falsifiability probe (not committed): flip the default's `dynamic_cast` to a
registry-miss (return nullptr unconditionally) → the find slot goes red; restore.

**T2 — gates + close-out, one commit.** Full ctest (expect 147); PlanStan
build+ctest against this tree (their override untouched ⇒ failed-set = the 21
Not-Run GUI binaries); STATUS (step 1 landed; next = await PlanStan closing note,
then step 3) + FINDINGS (the Plan-3 `backendById` entry gains a step-1 progress
note; full closure only at step 3); flip the RFC doc's status header to
"step 1 LANDED". Merge `--no-ff`, push, cut **v0.69** (the tag PlanStan's wave
pins against).

### Acceptance

- Suite 146 → 147; zero changes needed in any existing host (grep-verified: no
  implementor file touched).
- PlanStan gate failed-set unchanged.
- The two unchecked `static_cast` bridges named in FINDINGS (lib test scaffolds /
  reference_consumer) become deletable in step 3, not now (inv 8: they are
  overriders, and touching them is churn before the consumer wave settles).

## Outcome

_To be filled at T2._
