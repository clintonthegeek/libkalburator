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
  - [C.1 Akonadi provider (backend exists; provider + contacts + plugin-ization pending — Phase L)](#c1-akonadi-provider-backend-exists-provider--contacts--plugin-ization-pending--phase-l)
  - [C.2 IMAP/JMAP transport](#c2-imapjmap-transport)
- [D. Consumer UX (Phase Ic)](#d-consumer-ux-phase-ic)
  - [D.1 PlanStan: CardDAV add-account UI](#d1-planstan-carddav-add-account-ui)
  - [D.2 WildPalms: accounts settings dialog](#d2-wildpalms-accounts-settings-dialog)
  - [D.3 WildPalms: ProviderManager wiring in PalmRuntime](#d3-wildpalms-providermanager-wiring-in-palmruntime)
  - [D.4 Default-mapping logic](#d4-default-mapping-logic)
- [E. Test infrastructure](#e-test-infrastructure)
  - [E.1 Real-device verification gate](#e1-real-device-verification-gate)
- [F. Phase O closures (provider/backend UX gaps)](#f-phase-o-closures-providerbackend-ux-gaps)
  - [F.1 G7: BackendRegistry signals + dynamic kind picker](#f1-g7-backendregistry-signals--dynamic-kind-picker)
  - [F.2 G8 part 1: per-provider state introspection](#f2-g8-part-1-per-provider-state-introspection)
  - [F.3 G10 part 1: CollectionInfo capability metadata](#f3-g10-part-1-collectioninfo-capability-metadata)
  - [F.4 G9: CollectionController provider lifecycle API](#f4-g9-collectioncontroller-provider-lifecycle-api)
  - [F.5 G1: Add Account → addLogicalCalendarsFromCollections](#f5-g1-add-account--addlogicalcalendarsfromcollections)
  - [F.6 G2: PlanStan AccountsSettingsPage](#f6-g2-planstan-accountssettingspage)
  - [F.7 G3: SyncTopologyWidget provider awareness](#f7-g3-synctopologywidget-provider-awareness)
  - [F.8 G4 (partial): NewCollectionWizard reshape](#f8-g4-partial-newcollectionwizard-reshape)
  - [F.9 G5: BackendsSettingsPage provider ownership](#f9-g5-backendssettingspage-provider-ownership)
  - [F.10 G6: Provider credentials vs backend params split](#f10-g6-provider-credentials-vs-backend-params-split)
  - [F.11 G10 part 2: provenance display in topology canvas](#f11-g10-part-2-provenance-display-in-topology-canvas)
  - [F.12 Deferred to follow-up phase (post-O.4)](#f12-deferred-to-follow-up-phase-post-o4)

---

## A. Engine architecture (Phase Ib.5)

### A.1 Calendar-typed signals → domain-generic

**Status:** ✅ landed 2026-05-08 (`v0.28.5-phase-ib.5-engine-generalization`).
**Outcome:** `itemReady` and `itemFetched` signals deleted outright
(zero subscribers anywhere in the tree; generalization became
deletion). `ConflictInfo` was already domain-generic — no change
needed. `synctypes.h` did not include any KCalendarCore header even
before Ib.5. Phase G's verbatim success criterion is now delivered.

---

### A.2 Remove KCalendarCore from engine TU

**Status:** ✅ landed 2026-05-08 (`v0.28.5-phase-ib.5-engine-generalization`).
**Outcome:** `grep -r KCalendarCore libkalburator/src/engine/` returns
zero hits. `dispatchCalendarLegacy` deleted; no remaining includes.

---

### A.3 Delete `IDomainAdapter` and `CalendarDomainAdapter`

**Status:** ✅ landed 2026-05-08 (`v0.28.5-phase-ib.5-engine-generalization`).
**Outcome:** `idomainadapter.h`, `calendardomainadapter.h/.cpp` removed.
`m_calendarAdapter` member and ctor wiring removed from
`SyncEngineWorker`. `IRecordDifferICal` / `IRecordMergerICal` and
`CalendarPluginWriter` cover everything the adapters did.

---

### A.4 Restructure blob batch diff/merge into per-record loop

**Status:** ✅ landed 2026-05-17 (Phase N.1, tag `v0.44-phase-n1-perrecord-diff-merge`).
**Target phase:** Phase N.1 (delivered).
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

**Status:** CustomMerge ✅ landed 2026-05-17 (Phase N.1). Duplicate already
implemented in the unified path (Ib.5 era, never recorded here) but no test
coverage; A.5 remains ⬜ for Duplicate-test follow-up only.
**Target phase:** Phase N.1 for CustomMerge (delivered); test-only follow-up
for Duplicate.
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

**Status:** ✅ landed 2026-05-16 (Phase M, tag pending user authorization).
**Tag:** `v0.42-phase-m-multiprotocol-dav` (pending).
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

**Acceptance (as delivered):**
- `MultiProtocolDavProvider` (Phase M.2–M.5) wraps parallel
  CalDAV + CardDAV discovery against the same URL.
- `collections()` returns the union, with domain-tagged ids.
- `createBackend(id)` dispatches by `"caldav:"` / `"carddav:"`
  prefix.
- `MultiProtocolDavConfigWidget` (M.6) provides the single config
  UI surface.
- `MultiProtocolDavBackendContribution` + `MultiProtocolDavProviderPlugin`
  registered in `stock_plugins.cpp` (M.7–M.8).
- WildPalms `AddAccountDialog` surfaces "Multi-protocol DAV" label
  (M.13); `AccountsPage` embeds `AccountsListWidget` (M.14).
- Both consumers receive the provider via `stock_plugins`.
- PlanStan migration gated to M.5 (wizard-based flow; see D.1).

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

### C.1 Akonadi provider (backend exists; provider + contacts + plugin-ization pending — Phase L)

**Status:** ✅ landed 2026-05-16 (Phase L, tag `v0.41-phase-l-akonadi-provider`).
**Target phase:** Phase L (delivered).
**Source:** `ROADMAP.md` ("Akonadi/CardDAV deferred"); CardDAV
shipped in Phase Ib. Akonadi calendar backend was actually
already in tree at the start of the refactor — see below.

KDE PIM's Akonadi service exposes calendars and addressbooks as
local resources. Letting libkalburator participate in the KDE PIM
ecosystem requires both per-collection backends *and* an
`IProvider` that models the local Akonadi service as an account.

**What's already there (audit, 2026-05-15):**

- `src/calendar/akonadibackend.{h,cpp}` — full calendar
  `SyncBackend` (1100 LOC, "The Embassy" design, originated in
  PlanStan Feb 2026 as commit `5964252d`, migrated into
  libkalburator during the Step-6 library extraction). Uses
  `Akonadi::Session` + `Akonadi::Monitor` with `ignoreSession()`
  to avoid feedback loops. Calendar id scheme
  `"akonadi-<collectionId>"`.
- `tests/calendar/tst_akonadibackend_blob_view.cpp` — 67-line
  smoke test (identity, `isAvailable()==false` when no session,
  empty `availableCollections()`).
- `CMakeLists.txt:34` — `KALBURATOR_HAVE_AKONADI` cache var
  (default OFF). `:479-481` filter the source out when OFF.
  `:525-528` find `KPim6Akonadi` and define `HAVE_AKONADI` when
  ON. So the build flag is wired; what 04w originally claimed
  ("unused") is no longer accurate.

**What's actually missing:**

1. **`AkonadiProvider`** (`IProvider` impl). The big new piece:
   discovers Akonadi agent resources via `Akonadi::AgentManager`,
   enumerates calendar + addressbook collections via
   `CollectionFetchJob`, hands out backends. Modeled flat —
   one `AkonadiProvider` instance = "the local Akonadi service,"
   not per-resource (since the user already configures Akonadi
   resources via `kcm5_akonadi`).
2. **`AkonadiContactsBackend`.** The existing backend is
   calendar-only. Needs the same Monitor/Session pattern but for
   contacts collections (`KContacts::Addressee` payload, or
   `Akonadi::ContactGroup`).
3. **Plugin packaging.** K.7 + K.8a moved provider/backend wiring
   onto `Kalburator::Plugin` + `BackendContribution`. Today the
   existing `AkonadiBackend` is still compile-gated inside the
   core `kalburator` target, not a contribution. To fit current
   architecture, an `AkonadiProviderPlugin` should be the
   single registration surface (built/registered only when
   `KALBURATOR_HAVE_AKONADI=ON`).
4. **Engine gap — `BackendConfiguration::enabled` is unread.**
   `synctypes.h:233` (`SyncMapping::enabled`) is honored by the
   engine; `backendconfiguration.h:96`
   (`BackendConfiguration::enabled`) is not. Plumbing this in is
   prerequisite for the "duplicate-collection fallback" UX
   pattern described below.
5. **Consumer wiring.** WildPalms' `AddAccountDialog` is
   provider-generic post-K.8b — registering the contribution is
   enough to surface "Akonadi" in the combo. Still need: a
   minimal `AkonadiConfigWidget` (Akonadi has no server URL or
   credentials — local DBus — so displayName + maybe a "show
   hidden Akonadi resources" toggle is the whole config surface),
   a per-provider `enabled` checkbox in `AccountsPage`, and a
   per-mapping `enabled` checkbox in the mapping editor.
   PlanStan has no add-account UI yet (it relies on raw config
   editing) — Phase L scope deliberately excludes a PlanStan UX
   build-out.

**Fallback / failover (design decision, 2026-05-15):** Akonadi is
notorious for occasional breakage that locks the user out of
their data. We considered a `FallbackBackend` composition wrapper
or engine-level hotswap policy and **rejected both as
over-abstraction** — the whole concern collapses to "let the user
enable/disable providers and mappings." The schema already
supports this (`BackendConfiguration::enabled`,
`SyncMapping::enabled`); the engine already honors mapping
disable; what's missing is the provider-disable engine check + UX
checkboxes. The fallback *pattern* is then: create a
`CalDavProvider` mapping for the same upstream as an Akonadi
calendar mapping, leave one disabled, flip them when Akonadi
breaks. Per-calendar granularity follows naturally because the
user creates separate mappings per calendar; no special "fallback
edge" is encoded in the data model. Reads-only fallback (i.e.,
disable Akonadi-mapping for reads but not writes) is *not*
modeled — users who need writes during an Akonadi outage just
flip both at once.

**Acceptance (Phase L):**

- `AkonadiBackendContribution` + `AkonadiProviderPlugin` registered
  via `stock_plugins.cpp` when `KALBURATOR_HAVE_AKONADI=ON`.
- `AkonadiProvider` enumerates calendar + addressbook collections;
  `createBackend()` routes by mime type.
- `AkonadiContactsBackend` lands with parity to existing calendar
  backend (Monitor + Session, push/fetch/delete operations).
- Engine consults `BackendConfiguration::enabled` during dispatch
  and skips disabled providers' mappings entirely.
- WildPalms `AccountsPage` exposes per-provider enable checkbox;
  `MappingEditorDialog` (or `MappingRowDialog`) exposes per-
  mapping enable checkbox.
- Tests: env-gated against a live Akonadi (skipped otherwise)
  *plus* a non-gated unit test for the engine's
  provider-disable check (mock the BackendConfiguration, verify
  dispatch skips). Existing `tst_akonadibackend_blob_view.cpp`
  retained.
- Phase L tag: `v0.41-phase-l-akonadi-provider` on
  libkalburator's `refactor/engine-merger`.

**Plan:** see
`/home/clinton/dev/refactor-engine-merger/2026-05-15-phase-l-akonadi-plan.md`.

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

**Status:** ✅ fully closed 2026-05-18 (O.2.3 + O.2.4,
tag `v0.46-phase-o2-planstan-provider-lifecycle`).
**Landed:** Phase M.5 — `v0.43-phase-m5-runtime-add-account`;
O.1.4 — `v0.45-phase-o1-libkalburator-ui-foundations`;
O.2.3 + O.2.4 — `v0.46-phase-o2-planstan-provider-lifecycle`.
**Source:** Phase Ib design §3; cross-reference `libkalburator/docs/phase0/04x-phase-ib-status.md`.
**Tracked:** `PlanStan/docs/todo/carddav-account-ui.md`.

Phase Ib lands the CardDAV transport in libkalburator and a one-
line factory entry in PlanStan's `ProviderManager` factory at
`PlanStan/src/controllers/collectioncontroller.cpp:1691-1707`.
Phase M (2026-05-16) ships `ProviderConfigDialog` (M.11) and
`MultiProtocolDavProvider` (M.2–M.5), and migrates WildPalms
`AccountsPage` (M.14). PlanStan migration was **gated out** of
Phase M because PlanStan has no `CalDavAddDialog` — its CalDAV
add flow is wizard-based (`provisionCalDavProvider` via
`AdditionalBackendsPage`). Tasks M.15/16 assumed a
`CalDavAddDialog` that does not exist (Finding F-M5).

O.2.3 (2026-05-18) closed G1: `MainWindow::onAddAccountTriggered`
was switched to the registry-aware `ProviderConfigDialog` and now
calls `addLogicalCalendarsFromCollections` after provisioning so
the user's selected collections are actually persisted as
`LogicalCalendar` bindings. O.2.4 closed G2: `AccountsSettingsPage`
was added and wired into `SettingsDialog` between Behaviours and Sync.
Both items are now complete; D.1 is fully closed.

**Acceptance (delivered):**
- PlanStan `onAddAccountTriggered` uses registry-aware
  `ProviderConfigDialog`.
- `addLogicalCalendarsFromCollections` called after provision.
- `AccountsSettingsPage` registered in `SettingsDialog`.
- `tst_collectioncontroller` covers the new flows.

**Note:** WildPalms `AccountsPage` is migrated (M.14). The
remaining gap is PlanStan-only.

---

### D.2 WildPalms: accounts settings dialog

**Status:** ✅ landed 2026-05-09 (tag `v0.29-phase-ic-wildpalms-contacts-ux`).
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

**Status:** ✅ landed 2026-05-09 (tag `v0.29-phase-ic-wildpalms-contacts-ux`).
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

**Status:** ✅ landed 2026-05-09 (tag `v0.29-phase-ic-wildpalms-contacts-ux`).
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

### D.5 WildPalms multi-device cleanup (Phase L Task 0)

**Status:** ✅ landed 2026-05-15 (Phase L Task 0, commits A/B/C on
`refactor/engine-merger` in WildPalms).

**Source:** `2026-05-15-phase-l-multidevice-cleanup-design.md` +
`2026-05-15-phase-l-task0-multidevice-cleanup-plan.md` (coordination
folder root).

**Outcome:** WildPalms is now explicitly 1:1 device↔profile in code
*and* docs. Changes:

- `AutoSyncOrchestrator::onPalmDetected` no longer silently creates
  profile dirs for unrecognised devices; it emits
  `unregisteredDeviceDetected` and `KF6MainWindow` shows a
  confirmation `QMessageBox`. New public method
  `createProfileForDevice(serial,name,id)` does the actual creation
  on user consent.
- `KF6Settings::DeviceRegistry` group (fingerprint-keyed) deleted
  along with its 5 methods. `DeviceSerials` (USB-serial-keyed) is
  now the sole device→profile lookup. Migration in the
  `KF6Settings` ctor copies legacy entries into `DeviceSerials`
  once and `deleteGroup()`s the old group. Idempotent.
- `DeviceFingerprint::registryKey()` + `fromRegistryKey(...)` deleted
  (only existed to serve the deleted group).
- `WildPalms/docs/ROADMAP.md §5.5 "Multiple Device Support"` deleted.
- `SettingsDialog` "Registered Devices" page now enumerates the
  serial-keyed group instead of the fingerprint-keyed one; columns
  are Serial + Profile filename instead of the full fingerprint
  breakdown.

**Retained (deliberately, per design):** `DeviceFingerprint` struct,
`Profile::deviceFingerprint()` persistence,
`KF6MainWindow::handleDeviceFingerprint` mismatch dialog, dashboard
+ sidebar device-info display panels. These are the per-profile
"this profile is for *this* device" warning surface and are useful
even (especially) in a 1:1 world.

**Net code change:** ~150 LOC deleted across `kf6settings.{h,cpp}`,
`profile.h`, `autosyncorchestrator.cpp`, `settingsdialog.cpp`; ~80
LOC added (migration + new test fixtures). WildPalms test count
71 → 73.

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

## F. Phase O closures (provider/backend UX gaps)

Items G7, G8, G10 from the Phase O inventory
(`2026-05-17-provider-backend-ux-inventory.md`). They were never
separate 04w entries because the inventory pre-dated this file's
update for Phase O.

### F.1 G7: BackendRegistry signals + dynamic kind picker

**Status:** ✅ closed 2026-05-18 (Phase O.1.1 + O.1.4 +
O.4.10 ctor deletion,
tags `v0.45-phase-o1-libkalburator-ui-foundations`,
`v0.48-phase-o4-legacy-cleanup`).

`BackendRegistry::contributionRegistered(id)` and
`contributionUnregistered(id)` added (O.1.1). `ProviderConfigDialog`
registry-aware constructor subscribes to those signals and rebuilds
the kind-picker combo live (O.1.4). Old hardcoded-kinds constructor
deleted in Phase O.4 (task 10).

---

### F.2 G8 part 1: per-provider state introspection

**Status:** ✅ landed 2026-05-18 (Phase O.1.2 introspection +
post-O.4 follow-up signal removal). Tag
`v0.45-phase-o1-libkalburator-ui-foundations` for the initial
introspection; deprecated boolean signal removed 2026-05-18 in a
follow-up commit (no separate tag — §F.12 follow-up bucket).

`ProviderManager::providerState(id)` returns `ProviderConnectionState`;
`providerStateChanged(id, ProviderConnectionState)` signal added.
`ProviderConnectionState` enum: Disconnected / Connecting / Connected /
Error. Old boolean `providerConnectionStateChanged(id, bool)` was
preserved through Phase O.4 because WildPalms still subscribed to it.
The 2026-05-18 follow-up migrated WildPalms `AccountController` to the
enum signal (1:1 enum mapping into its existing `ConnectionState`) and
deleted the boolean signal from `ProviderManager`. The redundant
`tst_provider_manager::providerConnectionStateChanged_signal_carries_provider_id`
test was deleted (id-routing is now covered by
`providerState_transitionsThroughLifecycle`).

**Note:** `Connecting` and `Error` are forward-looking reserved values.
Current code paths map only to Connected/Disconnected because
`IProvider::connectionStateChanged` is still a boolean signal. Phase
O.3 must make that signal enum-typed before UI code can branch on
Connecting or Error (see also F-O1-2 in FINDINGS).

---

### F.3 G10 part 1: CollectionInfo capability metadata

**Status:** ✅ landed 2026-05-18 (Phase O.1.5,
tag `v0.45-phase-o1-libkalburator-ui-foundations`).

`CollectionInfo` gained `readOnly`, `contentTypes`, `estimatedSizeBytes`
fields. `CollectionPickerWidget` renders capability chips: content-type
labels (calendar / contacts / tasks / notes / raw) + read-only chip
(lock icon) + disabled checkbox for read-only collections.

---

### F.4 G9: CollectionController provider lifecycle API

**Status:** ✅ landed 2026-05-18 (Phase O.2.1,
tag `v0.46-phase-o2-planstan-provider-lifecycle`).

`CascadePolicy` enum added (Strict / DropBindings / DropBindingsAndOrphans).
`CollectionController::listProviders()`, `updateProvider(uuid, cfg)`, and
`removeProvider(uuid, policy)` added, giving PlanStan's UI layer a typed
API for the full provider lifecycle.

---

### F.5 G1: Add Account → addLogicalCalendarsFromCollections

**Status:** ✅ landed 2026-05-18 (Phase O.2.2 data layer + O.2.3 UI layer,
tag `v0.46-phase-o2-planstan-provider-lifecycle`).

O.2.2 added `CollectionController::addLogicalCalendarsFromCollections(uuid)`
which iterates the provider's `collections()` (filtered by
`selectedCollectionIds`) and creates `LogicalCalendar` bindings. O.2.3 wired
this into `MainWindow::onAddAccountTriggered` so that completing the
`ProviderConfigDialog` actually persists the user's selected collections
rather than dropping them on the floor.

---

### F.6 G2: PlanStan AccountsSettingsPage

**Status:** ✅ landed 2026-05-18 (Phase O.2.4,
tag `v0.46-phase-o2-planstan-provider-lifecycle`).

`AccountsSettingsPage` added to PlanStan and wired into `SettingsDialog`
between the Behaviours and Sync pages. Embeds `AccountsListWidget` (shipped
in Phase M as the generic provider-list widget). Provides in-app accounts
list, remove-account, and edit-account affordances. WildPalms equivalent
(`AccountsPage`) was already shipped in Phase M (M.14).

---

### F.7 G3: SyncTopologyWidget provider awareness

**Status:** ✅ landed 2026-05-18 (Phase O.3 Tasks 1, 2, 4, 6,
tag `v0.47-phase-o3-topology-canvas-v2`).

`ISyncTopologyDataSource` gained provider CRUD (`providers()`,
`addProvider()`, `updateProvider()`, `removeProvider()`) with default empty
impls, overridden by `KalbSyncTopologyDataSource` delegating to
`CollectionController`. `TopologyChangeset` gained provider/local-backend
node tracking with atomic apply. New `ProviderNode` and `LocalBackendNode`
IGraphNode subclasses render alongside `BackendNode` in the graph;
`SyncTopologyWidget` applies provider+backend+mapping changes atomically.

---

### F.8 G4: NewCollectionWizard reshape

**Status:** ✅ landed 2026-05-18 (Phase O.3 Task 9 + Phase O.4
Tasks 5–6 + post-O.4 wizard-chrome integration follow-up). Tags
`v0.47-phase-o3-topology-canvas-v2`,
`v0.48-phase-o4-legacy-cleanup`; wizard-chrome wiring landed
2026-05-18 in a follow-up commit (no separate tag — §F.12 bucket).

`WizardChromeOverlay` (O.3.9) lands as a composable wrapper around
`SyncTopologyWidget` exposing a sidebar checklist of provider/backend/binding
steps and a Finish button gated by `GraphValidationResult`.

Phase O.4 (Task 5) rewired `File → New Collection` to a minimal
path-prompt + layout switch flow that creates an empty `.kalb`
collection and switches the main window to the `collection` layout
(which already renders `sync_topology` per O.3.10). The
`NewCollectionWizard` + `AdditionalBackendsPage` files were deleted
(Task 6).

Post-O.4 follow-up (2026-05-18): `SyncTopologyViewPanel` gained
`enterWizardMode()` which wraps `m_topologyWidget` in a
`WizardChromeOverlay`. `MainWindow::onActionNewCollection` calls
`enterWizardMode()` after switching to the `collection` layout, and
connects `wizardCancelled` to a handler that prompts, then calls
`closeCollection() + QFile::remove(kalbPath)`. Finish swaps the
chrome back out, leaving the user in standard edit mode. Test:
`tst_synctopologyviewpanel_wizard` (PlanStan tests/sync/).

---

### F.9 G5: BackendsSettingsPage provider ownership

**Status:** ✅ closed 2026-05-18 by Phase O.4 deletion of
`BackendsSettingsPage` (tags `v0.47-phase-o3-topology-canvas-v2`,
`v0.48-phase-o4-legacy-cleanup`).

Rather than retrofit `BackendsSettingsPage` with a provider column, Phase O.3
promoted the topology canvas to a main view (`sync_topology`) where provider
ownership is rendered structurally as graph edges between `ProviderNode`s
and `BackendNode` / `LocalBackendNode` / `LogicalCalendarsBlock`. Phase O.4
Tasks 2–3 dropped `BackendsSettingsPage` (and `CalendarsSettingsPage`) from
`CollectionSettingsDialog` + `CollectionSettingsViewPanel` registrations and
then deleted the source files.

---

### F.10 G6: Provider credentials vs backend params split

**Status:** ✅ closed 2026-05-18 across Phase O.1.4 + Phase O.3 +
Phase O.4.10 hardcoded-ctor deletion
(tags `v0.45-phase-o1-libkalburator-ui-foundations`,
`v0.47-phase-o3-topology-canvas-v2`,
`v0.48-phase-o4-legacy-cleanup`).

Phase O.1.4 added the registry-aware `ProviderConfigDialog` ctor that
constructs provider widgets via `IProvider::createConfigWidget`, decoupling
provider credentials from `BackendConfigWidgetBase` subclasses. Phase O.3
extended this with provider CRUD on `ISyncTopologyDataSource` so the canvas
can edit provider credentials directly via the registry-aware dialog without
going through backend-shaped UI. Phase O.4 Task 10 deleted the legacy
hardcoded-kinds `ProviderConfigDialog` constructor.

---

### F.11 G10 part 2: provenance display in topology canvas

**Status:** ✅ landed 2026-05-18 (Phase O.3 Tasks 4, 5,
tag `v0.47-phase-o3-topology-canvas-v2`).

Building on F.3 (CollectionInfo capability fields), Phase O.3 surfaces
provider ownership directly in the topology canvas: `ProviderNode` renders
capability chips and a connection-state chip; `LogicalCalendarsBlock`
displays multi-row per-calendar binding rows linking each logical calendar
back to its owning provider+backend edge. Provenance is now visible
structurally rather than buried in tooltips.

---

### F.12 Deferred to follow-up phase (post-O.4)

**Status:** ✅ landed 2026-05-18 — items the Phase O design called out
but Phase O.4 (or earlier O sub-phases) explicitly did not deliver. All
three bullets below shipped as post-O.4 follow-ups (no separate phase
tag). The only remaining post-O.4 item is the §7.1 manual smoke
checklist (user-driven, agentic execution has no UI driver).

- **`ProviderManager::providerConnectionStateChanged(QString, bool)` removal.**
  ✅ landed 2026-05-18. WildPalms `AccountController` migrated to
  `providerStateChanged(QString, ProviderConnectionState)`; the boolean
  signal was deleted from `ProviderManager`. See F.2 above.

  Note on the earlier scoping claim: §F.12 originally listed a separate
  "WildPalms `ProviderConfigDialog` migration" bullet asserting that
  WildPalms instantiated libkalburator's `ProviderConfigDialog`. That was
  inaccurate — WildPalms uses its own `AddAccountDialog` (already
  registry-aware via `BackendRegistry*`). The only WildPalms-side coupling
  to the deprecated surface was the boolean signal consumer in
  `accountcontroller.cpp`. That bullet has been removed.

- **`SyncTopologyViewPanel` wizard-chrome mode.** ✅ landed 2026-05-18.
  `SyncTopologyViewPanel::enterWizardMode()` wraps the canvas in
  `WizardChromeOverlay`; `MainWindow::onActionNewCollection` calls it
  after the layout switch and handles `wizardCancelled` by deleting the
  freshly-created `.kalb`. See F.8 above.

- **Right-click context menu on canvas.** ✅ landed 2026-05-18.
  `SyncTopologyWidget::populateNodeContextMenu` extends the existing edge
  + BackendNode menu to dispatch on the v2 nodes:
  - `ProviderNode` (configured) → "Edit configuration…" (emits
    `editProviderRequested(uuid)`; `SyncTopologyViewPanel` handles by
    opening `ProviderConfigDialog` in `EditExisting` mode and calling
    `IProvider::applyConfig` on accept) + "Remove" (cascade-policy
    prompt: Strict / DropBindings / DropBindingsAndOrphans, then
    `stagePendingProviderRemoval`).
  - `LocalBackendNode` (configured) → "Remove" (Yes/No confirm →
    `stagePendingLocalBackendRemoval`).
  - Unconfigured (palette-dropped) nodes get no actions.

  Reconnect deferred as a small follow-up — would call
  `IProvider::disconnect()` + `connect()` from the menu; no infrastructure
  blockers, just a separate UX decision (do we re-prompt for credentials?).

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
