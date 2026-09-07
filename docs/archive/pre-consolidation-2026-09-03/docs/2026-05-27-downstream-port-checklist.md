# Downstream port checklist — PlanStan & WildPalms

> **CLOSED (2026-05-27 / 2026-06-10):** All three items (O7 ShapeRegistries ctor, O12
> TranscodingPlan drop, O15 write-path convergence) ported downstream. O7 resolved
> 2026-05-27; O12 effectively closed after the canon-upgrade branch merged to `main`;
> O15 shipped on `feature/o15-calendar-write-convergence` and merged.

**Date:** 2026-05-27
**From:** libkalburator
**To:** PlanStan and WildPalms maintainers (downstream `SyncBackend` / `SyncEngine` consumers)
**Why:** Adopting current libkalburator `main` + the `feature/o15-calendar-write-convergence`
branch requires source changes downstream. This is the consolidated list of every
breaking change and what you must do — three independent items (O7, O12, O15) that a
single porting pass can clear together.

The corresponding open/closed items live in `docs/campaign/FINDINGS.md` (O7, O12) and
its Resolved section (O15); this doc is the actionable summary.

---

## O15 — Calendar write path converged; transaction classes deleted

**What changed.** The calendar domain now writes through the uniform
`DefaultBlobWriter` (the same record path every other domain uses):
`IBlobBackend::createRecord` / `updateRecord` / `deleteRecord`.
`CalendarDomainOperations::createWriter()` returns `nullptr`, and the engine builds a
`DefaultBlobWriter` for calendar. The following are **deleted** (headers gone):

- `CalendarPluginWriter` (`src/calendar/calendarplugin_writer.{h,cpp}`)
- `SyncTransaction`, `SyncTransactionItem` (`src/calendar/synctransaction*.{h,cpp}`)
- `CreateIncidenceItem`, `UpdateIncidenceItem`, `DeleteIncidenceItem`
  (`src/calendar/*incidenceitem.{h,cpp}`)
- `SyncTestHooks` / the `SYNC_HOOK_CALL` macro (`src/calendar/synctesthooks.h`)

**Behavioral change.** Calendar writes are now **best-effort**, not transactional.
On a partial multi-record write failure: the partial writes **remain**, the sync
reports failure (`SyncResult::success == false`), and **baselines are not saved**, so a
retry re-attempts the failed writes and converges. There is no rollback. (The prior
`SyncTransaction` rollback was a MockBackend artifact: `shouldFail` is sticky per
op-type, so rollback ops of a *different* op-type succeeded; under a real systemic
failure all writes — including rollback ops — fail, and `rollbackCommitted` reported
success unconditionally. The retry-safe baseline behavior is the real, preserved
guarantee.)

**What you must do:**

1. **Remove all direct use of the deleted classes.** PlanStan especially —
   libkalburator's `tst_calendar_sync_error_recovery.cpp` was absorbed from PlanStan's
   `tst_sync_error_recovery.cpp`, so PlanStan likely still references `SyncTransaction`
   and the `*IncidenceItem` classes in tests and possibly production. Those won't
   compile.
2. **Rewrite any rollback-asserting tests to the retry-safe contract.** Drop
   all-or-nothing assertions (`target == 0`, "target restored to N"). Use the pattern:
   inject failure → sync → assert `!success` → clear failure → re-sync → assert the
   target **converges** to the expected state. Do **not** assert exact partial-write
   counts (record order through the diff is unordered, hence not contractual). See
   libkalburator's rewritten `tst_calendar_sync_error_recovery.cpp` for worked examples.
3. **Audit each custom backend's `IBlobBackend` write methods.** The converged path
   calls `createRecord`/`updateRecord`/`deleteRecord` **directly**. If any backend's
   blob-write path is weaker than its old `pushItems`/`startSync` path (e.g. skips
   validation or failure signaling), it will now silently succeed where it should fail.
   libkalburator hit exactly this: `MockBackend`'s blob methods had no failure-injection
   and needed `shouldFail()` checks added to match the `pushItems` path.
4. `pushItems` / `deleteItems` / `startSync` still exist on `SyncBackend` but calendar
   **no longer writes through them**. Don't rely on them being the write path.

---

## O12 — `TranscodingPlan` parameter dropped from `SyncBackend`

**What changed.** Plan 4 removed the `TranscodingPlan` parameter from
`SyncBackend::pushItems` / `startSync` (and the deprecated `storeItems` / `updateItem`).
Conversion is now the shape graph's job, not a per-write plan. Current signatures
(`src/calendar/syncbackend.h`):

```cpp
virtual void startSync(const QString &collectionId,
                       KCalendarCore::MemoryCalendar *calendar,
                       const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
                       const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
                       const QMap<QString, QString> &stagedDeletions);

virtual PushOperation *pushItems(const QString &calendarId,
                                 const QList<KCalendarCore::Incidence::Ptr> &items);
```

**What you must do:**

1. **Update every `SyncBackend` subclass override** to match these signatures (drop the
   old `TranscodingPlan` argument). An override with the old arity silently stops
   overriding (or fails to compile), so check each one.
2. **Org backend (PlanStan):** declare its backend shape as `{calendar, org-ical}` via
   `nativeShapes()` / `shapeFor()` so the engine routes `canon → org-ical` and applies
   RRULE simplification through the shape edge. libkalburator landed the edge and the
   lossy-sync warning re-sourcing; only the backend's shape wiring + a PlanStan org-sync
   ctest remain. (Built only with `KALBURATOR_HAVE_ORG_IO=ON`.)

---

## O7 — Inject `ShapeRegistries` instead of the Ambient-Context default

**What changed.** `SyncEngine` and `PluginManager` take an injected
`Shape::ShapeRegistries &`. A process-global default
(`Shape::defaultShapeRegistries()`) plus `TransformationRegistry::instance()` etc. are
kept **only as transitional Ambient-Context scaffolding** so you compile unchanged
today. Current ctors:

```cpp
// Injecting (target):
SyncEngine(BackendRegistry *registry, ISyncHost *host,
           Kalburator::Shape::ShapeRegistries &shape, QObject *parent = nullptr);
PluginManager(Sync::BackendRegistry *registry, Shape::ShapeRegistries &shape);

// Transitional Ambient-Context overloads (scheduled for deletion):
SyncEngine(BackendRegistry *registry, ISyncHost *host, QObject *parent = nullptr);
PluginManager(Sync::BackendRegistry *registry);
```

**What you must do:**

1. At your composition root, construct **one** `ShapeRegistries` and pass the **same
   instance** to both the `PluginManager` that populates it and the `SyncEngine` that
   reads it. Stop using the transitional (no-`ShapeRegistries`) overloads.
2. Once both PlanStan and WildPalms have adopted the injecting ctors, libkalburator
   will delete `defaultShapeRegistries()` and the `::instance()` accessors (FINDINGS
   O7), leaving the composition root as the only construction site.

---

## Coordination notes

- **Keep PlanStan's ctest green** before any of this lands upstream (existing policy;
  see `planstan-pretest-for-upstream`). The O15 behavioral change (best-effort writes)
  will surface in PlanStan's error-recovery tests first — that's expected; rewrite them
  per O15 step 2.
- **ORG / AKONADI build profiles** could not be validated in libkalburator's CI
  environment: `KALBURATOR_HAVE_ORG_IO=ON` fails on the OrgGrove FetchContent
  dependency (an unpushed OrgGrove commit — `orggrove-push-pending`). Push that before
  relying on org-profile validation. AKONADI profile is also untested here.
- **Branch state:** O15 is on `feature/o15-calendar-write-convergence` (8 commits off
  `main`). The provider-lifecycle fixes that were briefly entangled with it now live
  separately on `fix/provider-lifecycle` (3 commits, `providermanager.cpp` only) — not
  part of this port, but a wanted correctness fix for any `ProviderManager` consumer.
```
