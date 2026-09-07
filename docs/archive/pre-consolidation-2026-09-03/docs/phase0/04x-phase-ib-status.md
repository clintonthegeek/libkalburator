# Phase Ib — CardDAV transport (status)

**Status:** ✅ landed 2026-05-08 — tag `v0.28-phase-ib-carddav-transport`
**Spec:** `~/dev/refactor-engine-merger/2026-05-08-phase-ib-carddav-transport-design.md`
**Plan:** `~/dev/refactor-engine-merger/2026-05-08-phase-ib-carddav-transport-plan.md`

## What lands

Phase Ib ships the CardDAV transport layer in libkalburator, mirroring Phase H's CalDAV machinery against RFC 6352 (CardDAV):

- **`RemoteBackend` → `RemoteCalendarBackend`** rename (clarifying scope: calendar-only after Phase Ib.5 surfaces domain-generic signals).
- **`CardDavProvider`** — a provider class wrapping CardDAV capability discovery and addressbook backend production, paralleling `CalDavProvider` from Phase H.
- **`CardDavCapabilityDiscovery`** — DAV discovery machinery for addressbooks (PROPFIND to discover principal, addressbook sets, and per-collection properties). Threads through provider initialization.
- **`RemoteContactsBackend`** — `IBlobBackend` subclass for remote addressbooks, producing `BackendRecord` with MIME type `text/vcard` and shape `(contacts, vcard4)` (canonical per Phase Ia).
- **`FakeCardDavServer`** — test fixture mirroring `FakeCaldavServer`, enabling unit tests for discovery and read/write paths without hitting real servers.
- **Engine integration test** (`tst_carddav_engine_sync`) — confirms the contacts domain routes through the unified engine and applies transformations (completing the pressure-test opened by Phase Ia Task 19).
- **PlanStan factory wiring** — one-line addition to the `ProviderManager` factory so `"carddav"` kind instantiates `CardDavProvider`.

Out-of-scope items (KWallet, ETag, CTag, Nextcloud multi-protocol, RFC 6764 auto-discovery, consumer UX) are tracked in `libkalburator/docs/phase0/04w-deferred-work.md` §B and per-consumer todos.

## What landed (all 18 tasks, 2026-05-08)

- Task 1: Status doc + deferral cross-references.
- Task 2: `RemoteBackend` → `RemoteCalendarBackend` rename (libkalburator + PlanStan).
- Task 3: `FakeCardDavServer` test fixture with ETag, If-Match/If-None-Match, delay support.
  19/19 fixture tests pass.
- Task 4: `CardDavCapabilityDiscovery` — RFC 6352 PROPFIND walker + 6 tests.
- Tasks 5–7: `RemoteContactsBackend` — read-side, write-side (ETag semantics), cancellation.
  19/19 backend tests pass.
- Task 8: `CardDavProvider` — mirrors CalDavProvider; `connect()` uses QFutureWatcher pattern.
  20/20 provider tests pass.
- Task 9: Engine integration test — 2 contacts round-trip via SyncEngine::dispatchSync.
  4/4 integration tests pass.
- Task 10: Optional Radicale real-device test (PLANSTAN_ENABLE_CARDDAV_REAL_TESTS, default OFF).
- Task 11: `ProviderManager` factory wiring — one-liner in `providermanager.cpp` (not
  `collectioncontroller.cpp` — library-level is the correct location).
- Task 12: vCard version dialect pass — VERSION:2.1 / missing VERSION handling + CRLF/LF robustness.
- Task 13: Code-review fixes — removed dead vars + placeholder error string in
  `CardDavCapabilityDiscovery::parseAddressbookList`.
- Task 14: FINDINGS.md — 5 Phase Ib discoveries appended.
- Task 15: `verify-all.sh` exit 0; libkalburator baseline refreshed to 75/75.
- Task 16: Doc updates (this file, CURRENT-STATUS.md, ROADMAP.md).
- Task 17: Tag `v0.28-phase-ib-carddav-transport`.
- Task 18: Close-out report.

## What remains

Nothing — all 18 tasks complete. Next phase: Ib.5 (calendar-typed signal
generalization) or Ic (WildPalms accounts UX), per ROADMAP.md.

## vCard version support matrix

Per Task 12 (dialect handling), RemoteContactsBackend shape-tagging:

| VERSION: | Shape        | Handling                                             |
|----------|--------------|------------------------------------------------------|
| 4.0      | vcard4       | native; no transcode needed                          |
| 3.0      | vcard3       | transcoded by engine Pipeline                        |
| 2.1      | vcard3       | best-effort (logged warning); transcoded by Pipeline |
| (absent) | vcard4       | assumed latest (logged warning)                      |

Detection is robust against CRLF (`\r\n`) and LF (`\n`) line endings. Empty vCard bytes are handled gracefully (logged warning, defaulting to vcard4). Covered by tests 4, 15, 16, 17 in `tst_remote_contacts_backend.cpp`.

## Discoveries (see FINDINGS.md for full entries)

- QDomDocument::ParseResult requires `UseNamespaceProcessing` for CardDAV XML.
- CardDavProvider `bool* errorSeen` in lambda: fragile but safe in practice; future fix = shared_ptr.
- Integration test unique_ptr + BackendRegistry raw pointer: safe only when sync completes before unwind.
- ProviderManager factory is library-level (not consumer collectioncontroller).
- vCard version detection must scan raw bytes (KContacts parser strips VERSION field).
