# libkalburator response — scoped Akonadi contacts id-prefix mismatch

**Date:** 2026-06-14
**Direction:** libkalburator → WildPalms (response to
`WildPalms/docs/2026-06-14-libkalburator-akonadi-contacts-id-prefix-mismatch-handoff.md`)
**Branch:** `fix/akonadi-contacts-id-prefix` (off `main` @ v0.74)
**Status:** Fixed + regression-tested. The N>0 contacts read of a real address book
is WP's on-device step (acceptance criterion 2).

---

## Root cause (confirmed verbatim on the v0.74 tree)

`AkonadiProvider::collectionFetchResult` emits `"akonadi-<id>"` for **every**
collection regardless of type (`src/sync/akonadiprovider.cpp:137`) — no per-type
prefix. WP persists that id (`"akonadi-184"`) into the contacts route. But
`AkonadiContactsBackend` parsed only `"akonadi-contacts-<id>"`
(`AKONADI_CONTACTS_PREFIX`), so `akonadiIdForCollection("akonadi-184")` → `-1`,
`ensureScopedCollection` bailed, and `fetchItems` fast-failed
`"Unknown collection: akonadi-184"`. The calendar backend was immune because its
prefix (`"akonadi-"`) already matched the provider. The `"akonadi-contacts-<id>"`
scheme was documented in the header but produced by nothing.

This was latent before the 2026-06-12 fix (the empty `m_collections` fast-fail
masked it) and surfaced once `ensureScopedCollection` made the prefix reachable.
Fix B (v0.74) behaved correctly: the genuine `Failed` fetch op failed the contacts
mapping instead of a silent 0-record success.

## Fix (the handoff's preferred shape — no WP migration)

Aligned the contacts backend to the scheme the provider actually emits:

- `src/contacts/akonadicontactsbackend.cpp` — `AKONADI_CONTACTS_PREFIX` is now
  `"akonadi-"` (was `"akonadi-contacts-"`). Both `collectionIdForAkonadiId`
  (forward map; keys `m_itemsByCollection` from the Monitor) and
  `akonadiIdForCollection` (reverse parse; used by `ensureScopedCollection` /
  `fetchItems`) derive from the one constant, so the Monitor and fetch paths stay
  mutually consistent. Akonadi collection ids are globally unique across the whole
  tree, so the `-contacts-` discriminator bought nothing.
- Header scheme docs updated to `"akonadi-<id>"`.
- Untouched (unrelated to the collection-id prefix): the session-name string,
  the `akonadi-contacts-revisions.ini` filename, and `backendId()`'s
  `"akonadi-contacts:"` registry namespace.

Existing WP-persisted contacts routes targeting `"akonadi-184"` now resolve with
no re-binding.

## Why the v0.74 test missed it (and the correction)

`tst_akonadi_scoped_collection.cpp` hard-coded `kContactsScopedId =
"akonadi-contacts-1"` — the backend's self-invented scheme, which no producer
emits. The test and the only real producer disagreed, so it couldn't catch the
bug.

- Corrected the contacts guard to the **provider** scheme (`"akonadi-1"`).
- Added `contacts_resolvesProviderEmittedIdScheme`: builds the id exactly the way
  `AkonadiProvider` does (`"akonadi-%1".arg(184)`) and asserts the scoped contacts
  backend resolves it — pinning provider↔backend agreement (acceptance criterion 3),
  not the backend's self-invented scheme.

Both verified RED on the unfixed backend (`Unknown collection: akonadi-1` /
`…akonadi-184`) and GREEN after the fix.

## Verification

- `tst_akonadi_scoped_collection`: 5/5 (calendar guard + corrected contacts guard
  + provider-agreement test). RED→GREEN demonstrated.
- Akonadi-profile suite: 158/158, 0 failed (no regressions; the contacts
  collection-id scheme is exercised only by this test — confirmed by sweep).
- Default profile: unaffected and covered transitively — the Akonadi profile is a
  superset of the default profile, so 158/158 includes all default tests. (The
  changed `.cpp` is entirely under `#ifdef HAVE_AKONADI`; only header comments
  change otherwise.)
- PlanStan pretest (built against this branch tree; PlanStan builds libkalburator
  with `HAVE_AKONADI=ON`, so the change IS compiled in): 88/88 buildable tests
  pass — identical to v0.74. The 13 "failures" are the same pre-existing
  `EXCLUDE_FROM_ALL` integration/sync-workflow tests blocked by the harness bug at
  `integrationtestbase.cpp:155` (unrelated; flagged at v0.73). No regression.

## Acceptance criteria

1. A scoped `AkonadiContactsBackend` for a provider-emitted id resolves it
   (`fetchItems` no longer fast-fails) — **done** (guard + agreement tests).
   `loadRecords` N>0 for a non-empty address book = WP on-device.
2. HotSync pulls Akonadi collection 184 into `hub.db` contacts — **WP on-device**.
3. Test pins provider↔backend id-scheme agreement — **done**.
4. lib suite green; PlanStan baseline before tagging — **lib green (158/158);
   PlanStan 88/88 buildable pass (13 pre-existing, unrelated).**

## Not libkalburator's (WP-tracked)

The single-HotSync palm↔hub-before-hub↔remote ordering gap (roadmap item #2) is a
WP mapping-ordering concern, unaffected by this fix.

## Possible follow-up (not done — minimal fix chosen)

The akonadi collection-id format (`"akonadi-<id>"`) is now duplicated across the
provider and both backends. A single shared helper would make the scheme
drift-proof structurally (the agreement test currently pins it by value). Flagged
for a future cleanup; out of scope for this minimal fix.
