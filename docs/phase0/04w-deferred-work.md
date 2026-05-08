# 04w — Deferred work catalog

**Status:** living document, opened 2026-05-08.
**Purpose:** every architectural / feature item that the engine-
merger refactor (Phases D–J) explicitly deferred, with the
motivation, target phase, and acceptance criteria. This file
travels with the libkalburator repo and survives the merge of
`refactor/engine-merger` into `main`. Anything important enough to
defer is important enough to track somewhere durable; loose notes
in the coordination folder do not survive merge.

When a deferred item starts work, change its status from `⬜
deferred` to `⏳ in flight` and link the phase doc / plan that owns
it. When it lands, change to `✅ landed YYYY-MM-DD` and link the tag.

When you discover a new deferral mid-phase, add an entry here in
the **same commit** that lands the deferral decision. Don't trust
your memory; don't trust the coordination folder.

---

## Index

- [A. Engine architecture (Phase Ib.5)](#a-engine-architecture-phase-ib5)
  - [A.1 Calendar-typed signals → domain-generic](#a1-calendar-typed-signals--domain-generic)
  - [A.2 Remove KCalendarCore from engine TU](#a2-remove-kcalendarcore-from-engine-tu)
  - [A.3 Delete `IDomainAdapter` and `CalendarDomainAdapter`](#a3-delete-idomainadapter-and-calendardomainadapter)
  - [A.4 Restructure blob batch diff/merge into per-record loop](#a4-restructure-blob-batch-diffmerge-into-per-record-loop)
- [B. Transport features (no phase yet)](#b-transport-features-no-phase-yet)
  - [B.1 ETag-based optimistic concurrency at engine level](#b1-etag-based-optimistic-concurrency-at-engine-level)
  - [B.2 CTag-based change detection](#b2-ctag-based-change-detection)
  - [B.3 RFC 6764 email-based auto-discovery](#b3-rfc-6764-email-based-auto-discovery)
  - [B.4 KWallet credential storage](#b4-kwallet-credential-storage)
  - [B.5 Combined multi-protocol provider (Nextcloud-style)](#b5-combined-multi-protocol-provider-nextcloud-style)
  - [B.6 vCard version negotiation hardening](#b6-vcard-version-negotiation-hardening)
- [C. Backends (Phase J or beyond)](#c-backends-phase-j-or-beyond)
  - [C.1 Akonadi backend](#c1-akonadi-backend)
  - [C.2 IMAP/JMAP transport](#c2-imapjmap-transport)
- [D. Consumer UX (Phase Ic)](#d-consumer-ux-phase-ic)
  - [D.1 PlanStan: CardDAV add-account UI](#d1-planstan-carddav-add-account-ui)
  - [D.2 WildPalms: accounts settings dialog](#d2-wildpalms-accounts-settings-dialog)
  - [D.3 WildPalms: ProviderManager wiring in PalmRuntime](#d3-wildpalms-providermanager-wiring-in-palmruntime)
  - [D.4 Default-mapping logic](#d4-default-mapping-logic)
- [E. Test infrastructure](#e-test-infrastructure)
  - [E.1 Real-device verification gate](#e1-real-device-verification-gate)

---

## A. Engine architecture (Phase Ib.5)

### A.1 Calendar-typed signals → domain-generic

**Status:** ⬜ deferred from Phase Ia.5 (2026-05-08).
**Target phase:** Phase Ib.5 — calendar-typed signal generalization.
**Source:** `~/dev/refactor-engine-merger/2026-05-08-phase-ia.5-engine-unification-design.md` §3 "Out of scope".

`SyncEngine`'s public signals still emit `KCalendarCore::Incidence::Ptr`:

- `itemReady(calendarId, KCalendarCore::Incidence::Ptr, type)`
- `itemFetched(calendarId, KCalendarCore::Incidence::Ptr)`
- `ConflictInfo` (in `synctypes.h`) carries `Incidence::Ptr` payloads.

These shapes are a contract with PlanStan and WildPalms calendar
consumers. Generalizing them is what makes the engine truly
domain-agnostic; until then, anything that #includes
`syncengine.h` pulls KCalendarCore into its TU.

**Why deferred:** the contract change requires migrating consumer
slot handlers in PlanStan and WildPalms in lockstep with the
engine change. Phase Ia.5's audit estimated ~2 weeks of consumer
migration on top of the engine work; that scope inflation would
have blocked Ia close-out and Ib.

**Acceptance:**
- Signal payloads are `BackendRecord` (or a typed envelope over it)
  rather than `Incidence::Ptr`.
- `synctypes.h` no longer includes any KCalendarCore header.
- All consumer slot handlers updated; `verify-all.sh` clean.
- Phase G's verbatim success criterion ("engine deals only in
  `BackendRecord`") is now achieved.

---

### A.2 Remove KCalendarCore from engine TU

**Status:** ⬜ deferred from Phase Ia.5.
**Target phase:** Phase Ib.5.
**Depends on:** A.1 (the public signal generalization).
**Source:** Phase G success criterion in `ROADMAP.md`; Phase Ia.5
design §3.

`syncengine.cpp` currently `#include`s KCalendarCore in two places:

1. The transitional `dispatchCalendarLegacy` helper that preserves
   calendar-typed signal emission inside the unified file.
2. The signal slot bodies that construct `Incidence::Ptr` payloads.

Both go away once A.1 lands.

**Acceptance:**
- `grep -r KCalendarCore libkalburator/src/engine/` returns zero
  hits.
- The unified `dispatchSync` is the only path; no
  `dispatchCalendarLegacy` helper remains.
- `tests/calendar/tst_calendar_sync_*` continue to pass on the
  fully-generalized path.

---

### A.3 Delete `IDomainAdapter` and `CalendarDomainAdapter`

**Status:** ⬜ deferred from Phase Ia.5.
**Target phase:** Phase Ib.5.
**Depends on:** A.1, A.2.
**Source:** Phase Ia.5 plan Tasks 15 + 17 (re-scoped 2026-05-08).

Phase Ia.5 deleted `BlobDomainAdapter`. The two calendar-typed
adapters survived because their last callers are inside
`dispatchCalendarLegacy` (the calendar-typed signal emission path).

- `libkalburator/src/engine/idomainadapter.h` — Phase F1 leftover;
  superseded conceptually by `DomainPlugin`.
- `libkalburator/src/calendar/calendardomainadapter.{h,cpp}` —
  calendar-typed convenience helpers (`diffCalendarRecords`,
  `applyChangesToBackend(SyncBackend*, …, SyncChange…)`) that
  pre-date the plugin/writer machinery.

**Acceptance:**
- Both files removed from the tree.
- `m_calendarAdapter` member and ctor wiring removed from
  `SyncEngineWorker`.
- `IRecordDifferICal` / `IRecordMergerICal` and the calendar
  plugin's `CalendarPluginWriter` cover everything the adapters
  did.

---

### A.4 Restructure blob batch diff/merge into per-record loop

**Status:** ⬜ deferred from Phase Ia.5.
**Target phase:** Phase Ib.5 or later (lower priority than A.1–A.3).
**Source:** Phase Ia.5 FINDINGS — "BlobBackendAdapter blob/blob→
blob/raw shape gotcha"; design discussion of `IRecordDiffer` /
`IRecordMerger` impedance mismatch.

`IRecordDiffer` and `IRecordMerger` are per-record interfaces.
`BlobDomainAdapter` was batch-shaped (`QList<...> diff(QList<...>,
QList<...>, QList<...>)`). Phase Ia.5 closed the API gap by
introducing free functions (`blobBatchDiff`,
`blobBatchMergeWithPlugin`) that bridge the batch interface to the
per-record one. This works but is a transitional shape; the engine
should eventually run a true per-record loop calling
`differ->diff(srcRecord, tgtRecord, baseline)` on each pairing.

**Why deferred:** Phase Ia.5 chose Approach A (cosmetic deletion
via free functions) over Approach B (loop restructure) to keep the
phase scoped. The free functions are correct and tested; the
restructure is purely a code-shape improvement.

**Acceptance:**
- `blobBatchDiff` / `blobBatchMergeWithPlugin` removed.
- Engine drives per-record loop directly; differ/merger called
  once per record pair.
- Same test posture (`tests/blob/tst_engine_mirror_direction.cpp`,
  `tests/engine/tst_engine_unified_routing.cpp`).

---

### A.5 CustomMerge and Duplicate conflict policies in unified path

**Status:** ⬜ deferred from Phase Ib.5 Task 6.
**Target phase:** post-Ib.5; only needed when a consumer wires these policies.
**Source:** Phase Ib.5 parity audit (04y); Task 6 grep sweep.

`unifiedHandleConflicts` routes CustomMerge and Duplicate conflict ops to
the `default: ++conflictsDeferred` branch (same as the legacy
`blobBatchMergeWithPlugin` when no custom merger was available). No
`tests/calendar/` or `tests/contacts/` integration test sets
`conflictPolicy = CustomMerge` or `conflictPolicy = Duplicate` on a mapping
(confirmed by Task 6 grep sweep). Acceptance bar met without a fix.

**Why deferred:** CustomMerge in the unified path requires the domain
plugin's `IRecordMerger::merge()` to be called at yield-resume time (when
the ConflictManager returns `CustomMerge` resolution). Duplicate requires
cloning the record with a new UUID and writing both copies. Neither use case
has a test or a consumer wiring it today.

**Acceptance (CustomMerge):**
- In `resumeAfterConflict`, when `resolution == CustomMerge` and the
  dispatch path is Unified, call `plugin->createCanonicalMerger()->merge()`
  with the stored `op.record`, `op.targetRecord`, `op.baselineRecord`.
- Result written to `finalTarget` and `updatedBaselines`.

**Acceptance (Duplicate):**
- In `unifiedHandleConflicts`, when policy == Duplicate, clone the record
  with a new UUID (domain-agnostic: append "-dup-<uuid>" to the id) and
  push both the original and clone to `finalTarget`.
- Update `updatedBaselines` for both.

---

### A.6 Calendar property phase: baseline-aware diff in unified path

**Status:** ⬜ deferred from Phase Ib.5 Task 5.
**Target phase:** post-Ib.5; low priority — no integration test exercises
this path.
**Source:** Phase Ib.5 parity audit (04y).

`dispatchCalendarLegacy` runs `fetchCalendarProperties` +
`computePropertyDiff` + `applyPropertyChanges`, using
`CalendarBaselineStore` to store typed `CalendarPropertyRecord` baselines
and compute a property diff (displayName, color, timeZone, etc.).

The unified `dispatchSync` calls `runPropertyPhase(plugin, ...,
baseline=QVariantMap{})` with an *empty* baseline. This means the property
phase always runs in "first-sync" mode — it sees no prior state and
overwrites collection-level properties on every sync, but since most sources
and targets agree on calendar metadata in practice, this is harmless.

No `tests/calendar/` integration test verifies baseline-aware property diff
(confirmed by Task 5 grep sweep). The acceptance bar is therefore met without
a fix.

**Why deferred:** `CalendarBaselineStore` stores `CalendarPropertyRecord`
JSON; bridging to the `QVariantMap` interface that `runPropertyPhase` expects
requires a schema migration or a parallel store entry. Not worth the
complexity until a consumer reports a property regression.

**Acceptance:**
- `CalendarBaselineStore` property records converted to `QVariantMap` or
  a new typed store added to `BlobBaselineStore` (v4 baseline schema).
- `runPropertyPhase` called with the persisted baseline instead of `{}`.
- `tests/calendar/` gains a test that seeds a property baseline, changes
  displayName on one side, and asserts the change propagates without
  overwriting the unchanged side.

---

## B. Transport features (no phase yet)

### B.1 ETag-based optimistic concurrency at engine level

**Status:** ⬜ deferred indefinitely.
**Target phase:** none assigned; revisit when CardDAV/CalDAV
production usage surfaces concurrent-write conflicts in practice.
**Source:** Phase Ib design §3 "Out of scope".

ETags exist today inside `RemoteCalendarBackend` (and Phase Ib's
`RemoteContactsBackend`) as private state, used for `If-Match`
PUT/DELETE conditional requests. The engine itself doesn't see
them — `BackendRecord` carries no ETag field, and conflict
detection is baseline-driven (Phase F2's three-way merge).

Lifting ETags into `BackendRecord` would let the engine detect
"server changed under us" without consulting baselines, which
matters for high-concurrency CalDAV/CardDAV deployments.

**Why deferred:** today's baseline-driven detection works for the
typical desktop-sync cadence. Lifting ETags is a contract change
on `BackendRecord` and `IBlobBackend`; not urgent.

**Acceptance:**
- `BackendRecord` carries an opaque per-record version token
  (ETag-shaped but transport-agnostic).
- `IRecordDiffer` consults the token alongside content equality.
- Backends that lack version tokens (e.g., LocalCalendarBackend
  on a flat file) return empty tokens; engine falls back to
  baseline-driven detection.

---

### B.2 CTag-based change detection

**Status:** ⬜ deferred indefinitely.
**Target phase:** none assigned; performance optimization.
**Source:** Phase H + Phase Ib design.

`RemoteCalendarBackend::modifiedSince` and `deletedSince` issue a
full PROPFIND on every poll. CalDAV/CardDAV expose a `getctag`
property on the collection that summarizes its modification state;
clients can short-circuit the full PROPFIND if the CTag hasn't
changed since the last poll.

**Why deferred:** the full PROPFIND cost is acceptable for typical
collection sizes (≤ 1000 records). CTag optimization matters for
collections with tens of thousands of records and frequent polling.

**Acceptance:**
- `RemoteCalendarBackend` and `RemoteContactsBackend` cache the
  last-seen CTag per collection.
- `modifiedSince` / `deletedSince` PROPFIND skipped when CTag
  matches.
- Cache invalidated on any local write through the backend.
- Verified against Radicale + Nextcloud: ≥ 90% PROPFIND skip rate
  on idle polls.

---

### B.3 RFC 6764 email-based auto-discovery

**Status:** ⬜ deferred indefinitely.
**Target phase:** Phase Ic at the earliest (it's a UX affordance,
not a transport requirement).
**Source:** Phase Ib design §3 "Out of scope".

RFC 6764 lets a CalDAV/CardDAV client take just `user@example.com`
and discover the server URL via:

1. SRV record `_caldavs._tcp.example.com` (or `_carddavs.`)
2. `.well-known/caldav` (or `/carddav`) on `https://example.com/`
3. PROPFIND from there.

**Why deferred:** users today enter the server URL manually. Auto-
discovery is convenience, not capability.

**Acceptance:**
- New helper `DavAutoDiscovery` (in `src/sync/`) that takes an
  email address and returns a candidate server URL or fails.
- Both `CalDavConfigWidget` and the future `CardDavConfigWidget`
  offer a "discover from email" affordance.
- Tests against canned SRV / well-known responses.

---

### B.4 KWallet credential storage

**Status:** ⬜ deferred indefinitely.
**Target phase:** none assigned; security review trigger.
**Source:** Phase H baseline; Phase Ib design §3 "Out of scope".

Provider credentials (`username`, `password`) are stored in
`BackendConfiguration::connectionParams` as plaintext strings,
persisted in the consumer's profile (`.kalb` file or equivalent).

**Why deferred:** the security review hasn't fired yet; the
current posture matches the pre-extraction PlanStan baseline.

**Acceptance:**
- Provider configurations store a wallet handle, not the password
  directly.
- KWallet integration optional behind a build flag for
  environments without KWallet.
- Migration path for existing plaintext profiles (read once,
  store in wallet, scrub from profile).

---

### B.5 Combined multi-protocol provider (Nextcloud-style)

**Status:** ⬜ deferred indefinitely.
**Target phase:** none assigned; UX refinement.
**Source:** `iprovider.h:25` docstring; Phase Ib design §3.

`IProvider`'s docstring mentions "Nextcloud (one server speaking
CalDAV+CardDAV)" as an example. The actual Phase H + Phase Ib
implementation ships two separate providers (CalDavProvider,
CardDavProvider) pointed at the same URL.

**Why deferred:** combining them is a UX win (one account = both
protocols) but not a transport capability gain. Two providers
co-exist fine. Combination requires a `MultiProtocolProvider`
abstraction that aggregates collections from multiple discoveries
under one configuration UI.

**Acceptance:**
- New `NextcloudProvider` (or similar) wraps both CalDAV and
  CardDAV discovery against the same URL.
- `collections()` returns the union, with domain-tagged ids.
- `createBackend(id)` dispatches by domain.
- Single config UI in PlanStan / WildPalms.

---

### B.6 vCard version negotiation hardening

**Status:** ⬜ deferred from Phase Ib (will revisit if real-server
testing surfaces problems).
**Target phase:** Phase Ic or later.
**Source:** Phase Ib design §5 Risk 2.

Phase Ib's `RemoteContactsBackend` declares
`nativeShapes() = { (contacts, vcard4) }` but inspects each loaded
record's version line and tags as `(contacts, vcard3)` if the
server returns 3.0. Some servers don't honor `version=4.0`
preference; the engine Pipeline handles transcode either way.

If real-world server testing surfaces additional version dialects
(2.1, custom vendor extensions), this is the bucket to capture
them in.

**Acceptance:**
- Documented matrix of tested servers + their version-negotiation
  behavior.
- `RemoteContactsBackend` handles each dialect or rejects with a
  clear error.

---

## C. Backends (Phase J or beyond)

### C.1 Akonadi backend

**Status:** ⬜ deferred since Phase D.
**Target phase:** Phase J or beyond.
**Source:** `ROADMAP.md` ("Akonadi/CardDAV deferred"); now CardDAV
ships in Phase Ib but Akonadi remains.

KDE PIM's Akonadi service exposes calendars and addressbooks as
local resources. A native `AkonadiCalendarBackend` /
`AkonadiContactsBackend` would let libkalburator participate in
the KDE PIM ecosystem.

**Why deferred:** Akonadi has a heavy runtime dependency; gating
the library build on Akonadi presence is undesirable. Should be
optional via `KALBURATOR_HAVE_AKONADI=ON` (which already exists in
the CMake config but is unused).

**Acceptance:**
- `AkonadiCalendarBackend` + `AkonadiContactsBackend` behind the
  build flag.
- `IProvider` `AkonadiProvider` that enumerates the local
  resources.
- Tests against a fake Akonadi resource (or skipped if Akonadi
  isn't available at build time).

---

### C.2 IMAP/JMAP transport

**Status:** ⬜ speculative; not on any roadmap.
**Target phase:** none.
**Source:** discussion only.

vCard records can be carried as MIME attachments in IMAP folders.
JMAP has native contacts/calendar collections. Either is a viable
alternate transport for environments without DAV.

**Why deferred:** no user demand surfaced yet; not architecturally
blocked.

---

## D. Consumer UX (Phase Ic)

### D.1 PlanStan: CardDAV add-account UI

**Status:** ⬜ deferred from Phase Ib (Phase Ib in flight 2026-05-08).
**Target phase:** Phase Ic.
**Source:** Phase Ib design §3; cross-reference `libkalburator/docs/phase0/04x-phase-ib-status.md`.
**Tracked:** `PlanStan/docs/todo/carddav-account-ui.md`.

Phase Ib lands the CardDAV transport in libkalburator and a one-
line factory entry in PlanStan's `ProviderManager` factory at
`PlanStan/src/controllers/collectioncontroller.cpp:1691-1707`.
PlanStan still needs:

- An "Add CardDAV account" entry alongside the existing CalDAV
  one in the account-creation flow.
- A `CardDavConfigWidget` mirroring `CalDavConfigWidget`.
- Addressbook selection UI after successful discovery.
- Profile persistence for the new provider kind.

**Acceptance:** see PlanStan-side todo file.

---

### D.2 WildPalms: accounts settings dialog

**Status:** ⬜ deferred since Phase H (cross-cuts Phase Ic).
**Target phase:** Phase Ic.
**Source:** `ROADMAP.md` Phase Ic row.
**Tracked:** `WildPalms/docs/TODO-contacts-account-ux.md`.

WildPalms today has no UI for configuring DAV accounts. Phase Ic
adds a SettingsDialog Accounts page where users can add CalDAV +
CardDAV accounts and bind their collections to Palm category
slots.

**Acceptance:** see WildPalms-side todo file.

---

### D.3 WildPalms: ProviderManager wiring in PalmRuntime

**Status:** ⬜ deferred since Phase H.
**Target phase:** Phase Ic.
**Source:** `ROADMAP.md` Phase Ic row.
**Tracked:** `WildPalms/docs/TODO-contacts-account-ux.md`.

PalmRuntime currently constructs Palm-native backends directly.
Phase Ic threads a `ProviderManager` through it so non-Palm
providers (CalDAV, CardDAV, eventually Akonadi) can supply
backends to the Palm sync flow.

**Acceptance:** PalmRuntime owns or references a `ProviderManager`,
and the existing `BlobBackendAdapter` test scaffolding gets the
same `--whole-archive` static-link treatment Phase Ia.5 already
applied.

---

### D.4 Default-mapping logic

**Status:** ⬜ deferred since Phase H.
**Target phase:** Phase Ic.
**Source:** `ROADMAP.md` Phase Ic row.

When a user adds a new account with multiple collections (e.g., a
CardDAV server with three addressbooks), the consumer needs a
default-mapping policy: bind which addressbook to which Palm
category slot, or to which PlanStan logical calendar. Today this
would be entirely manual.

**Acceptance:**
- Heuristic: bind first discovered collection to "default" slot;
  prompt user for rest.
- Override UI surfaces the mapping for review/edit.

---

## E. Test infrastructure

### E.1 Real-device verification gate

**Status:** ⬜ deferred since Phase D.
**Target phase:** independent of all engine phases; runs once
before any release tag escapes the dev branch.
**Source:** `~/dev/refactor-engine-merger/CURRENT-STATUS.md`.

`scripts/verify-all.sh` builds and tests all three repos against
mocks and fakes. Real-device verification (PalmOS hardware,
Radicale + Nextcloud + Sabre/DAV servers, Akonadi local resource)
is a separate gate that requires manual setup.

**Why deferred:** automation against real devices/servers requires
either dedicated CI infrastructure or a manual gate. The phase
work has been gated on `verify-all.sh` (mock-based) so far.

**Acceptance:**
- A documented manual-test matrix for each backend (CalDAV
  servers, CardDAV servers, Palm hardware, Akonadi if shipped).
- A pass on the matrix before any tag rolls out of `refactor/
  engine-merger` to `main`.

---

## How to update this file

When opening a new phase that owns a deferred item:
1. Change its status from `⬜ deferred` to `⏳ in flight (Phase X)`.
2. Link the phase doc / plan.
3. Same commit also updates the phase-status doc and
   `CURRENT-STATUS.md`.

When a deferred item lands:
1. Change to `✅ landed YYYY-MM-DD`.
2. Link the tag.
3. The acceptance-criteria section becomes a brief retrospective
   ("delivered as designed" / "scope reduced because …").

When discovering a new deferral:
1. Add the entry under the right section (or create a new section).
2. Same commit as the decision to defer.
3. Cross-reference from the relevant phase's design doc.
