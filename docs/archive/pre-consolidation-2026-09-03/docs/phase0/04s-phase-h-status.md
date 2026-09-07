# Phase H — Status

**Tag:** `v0.24-phase-h-providers` (landed 2026-05-07)

**Status:** ✅ Complete.

## Summary

Phase H introduces `IProvider` as a first-class concept above
`IBlobBackend` in libkalburator. A provider models a single
auth/connection from which N collections of various shapes flow
(Akonadi, Nextcloud's CalDAV+CardDAV, Google's calendar/contacts
APIs). Phase H ships the abstraction + first implementation
(`CalDavProvider`, calendar collections only). WildPalms / PlanStan
not migrated — the provider machinery is unused-by-anyone-yet
infrastructure that's tested in isolation.

## Tasks

| # | Task | Commit | Status |
|---|---|---|---|
| 1 | IProvider interface header | `a22891e` | ✅ |
| 2 | ProviderManager + 8 unit tests | `001ef0b` | ✅ |
| 3 | CalDAV machinery audit (read-only) | (audit doc only) | ✅ |
| 4 | CalDavProvider skeleton | `aecf2b7` | ✅ |
| 5 | CalDavProvider connect/enumerate/createBackend | `ff6345d` | ✅ |
| 6 | tst_caldav_provider + FakeCalDavServer (8 tests) | `7c233df` | ✅ |
| 7 | CalDavConfigWidget + 5 widget tests | `0124636` | ✅ |
| 8 | tst_caldav_integration end-to-end (5 tests) | `c0cf179` | ✅ |
| 9 | calendarmanager cleanup (minimal — see below) | `dcf463f` | ✅ (reduced) |
| 10 | verify-all + libkalburator baseline refresh | (no commit) | ✅ |
| 11 | Tag `v0.24-phase-h-providers` | (annotated tag) | ✅ |
| 12 | CURRENT-STATUS / ROADMAP / FINDINGS / this doc | (in coordination folder) | ✅ |

## Test posture (post-Phase-H)

- **libkalburator:** 58/58 pass (was 54; +4 Phase H test executables
  totalling 26 sub-tests)
- **PlanStan:** 90/114 pass (24 pre-existing env failures, unchanged)
- **WildPalms:** 75/75 pass (unchanged)
- **`verify-all.sh`:** exit 0 after baseline refresh

## What landed (the surface)

- `src/sync/iprovider.h` — abstract `IProvider` interface
- `src/sync/providermanager.{h,cpp}` — per-profile provider lifecycle
  owner; reads/writes `Providers/<id>` subgroups in a `KConfigGroup`;
  registers each connected provider's per-collection backends with
  the existing `BackendRegistry`
- `src/sync/caldavprovider.{h,cpp}` — wraps the existing
  `CalDavCapabilityDiscovery` + `RemoteBackend` pair behind the
  `IProvider` contract
- `src/sync/caldavconfigwidget.{h,cpp}` — Qt Widgets form
  (displayName, server URL, username, password, Test Connection)
- `src/calendar/caldavcapabilitydiscovery.{h,cpp}` — extended
  additively with a `calendarUrls()` accessor populated alongside
  `m_capabilities.perCalendarCapabilities` (no behavior change to
  existing callers)
- `tests/sync/` — new test directory with `kalburator_add_sync_test()`
  helper + four test executables + `FakeCalDavServer` fixture
  (QTcpServer-based, handles principal → home → calendar-list
  PROPFIND walk; configurable failure modes)

## Architectural choices locked in

1. **Additive design** — providers produce backends that register
   with the existing `BackendRegistry`; the engine doesn't know
   providers exist. `SyncMapping` schema unchanged; `IBlobBackend`
   /`SyncBackend` unchanged.
2. **Built-in to libkalburator** — providers are linked statically
   into the core library, not loaded as KPlugins. CalDAV is built
   in; future Akonadi (Phase I) and CardDAV (Phase I) follow the
   same pattern. KPlugin loading is a deferred polish task.
3. **KConfig persistence** — provider config persists under a
   `Providers/<uuid>` subgroup (kind, displayName, plus
   `BackendConfiguration::connectionParams`).
4. **Plaintext credentials** — same as PlanStan today; KWallet
   integration is out of scope (FINDINGS entry tracks this).
5. **IProvider returns `unique_ptr<IBlobBackend>`** —
   ProviderManager `dynamic_cast`s to `SyncBackend*` for the
   `BackendRegistry::registerBackendInstance(QString, SyncBackend*)`
   API. Phase H's only producer (CalDavProvider) returns
   `RemoteBackend` (which inherits SyncBackend), so the cast always
   succeeds. Future pure-blob providers will need a registry
   extension.

## What did NOT land (and why)

### CalendarManager `davUrl`-block deletion (plan's original Task 9 scope)

The plan envisioned Task 9 as deleting ~300-500 LOC of CalDAV code
from `calendarmanager.cpp`. The Task 3 audit found:

- `CalendarManager` is essentially CalDAV-clean (no QNAM, no
  PROPFIND, no auth state). Real CalDAV machinery lives in
  `RemoteBackend` and `CalDavCapabilityDiscovery`.
- The only CalDAV residue in `calendarmanager.cpp` is two
  ~20-line `davUrl`-construction blocks (lines 110-128, 422-440)
  that build a per-calendar URL from base URL + username + path
  segment, then store it on `binding.metadata` for PlanStan's
  `BackendDiscoveryCoordinator::registerCalendarUrlsFromBindings`
  to replay at app start.

Deleting those blocks would break PlanStan (which still consumes
the persisted `davUrl` strings). PlanStan migration is out of
Phase H scope. Task 9 was reduced to:

- Delete unused `#include "localbackend.h"` (audit §8 risk #2)

The substantive deletion is deferred to whichever phase migrates
PlanStan to drive its CalDAV accounts through `CalDavProvider`.
See FINDINGS.md and the audit doc.

### SyncEngine round-trip integration test (plan's Task 8 stretch goal)

The plan's design doc §6 mentioned "one end-to-end integration
test … runs a mapping against [provider-supplied backends]." In
Task 8 I scoped that to the registration path only (KConfig →
ProviderManager → CalDavProvider → BackendRegistry). Driving
SyncEngine end-to-end would require teaching `FakeCalDavServer`
the item-level CalDAV verbs (REPORT/GET/PUT/DELETE) — a 5-10x
expansion of the fake's complexity for marginal coverage gain.
The engine is independently tested via `tests/calendar/`'s D.0
suite. Documented in Task 8's commit message.

## Next phase — Phase I (CardDAV + optionally Akonadi)

The two natural extensions:

- **CardDAV inside CalDavProvider** — one provider class learns
  two protocols on the same server. Pressure-tests the abstraction
  by adding a second collection type to the same provider.
- **AkonadiProvider** — a separate provider class. Depends on
  kf6pim availability (build flag `KALBURATOR_HAVE_AKONADI`).

Together they make the IProvider abstraction "stable" enough to
declare ready for consumer migration.

## References

- Design: `~/dev/refactor-engine-merger/2026-05-06-phase-h-providers-design.md`
- Plan: `~/dev/refactor-engine-merger/2026-05-06-phase-h-providers-plan.md`
- Audit: `~/dev/refactor-engine-merger/2026-05-06-phase-h-task3-caldav-audit.md`
- FINDINGS: `~/dev/refactor-engine-merger/FINDINGS.md` (Phase H entries appended 2026-05-07)
