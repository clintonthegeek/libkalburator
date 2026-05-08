# Phase Ib — CardDAV transport (status)

**Status:** in flight (2026-05-08)
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

## What remains

(initial: all 18 tasks pending)

## vCard version support matrix

Per Task 12 (dialect handling), RemoteContactsBackend shape-tagging:

| VERSION: | Shape        | Handling                                             |
|----------|--------------|------------------------------------------------------|
| 4.0      | vcard4       | native; no transcode needed                          |
| 3.0      | vcard3       | transcoded by engine Pipeline                        |
| 2.1      | vcard3       | best-effort (logged warning); transcoded by Pipeline |
| (absent) | vcard4       | assumed latest (logged warning)                      |

Detection is robust against CRLF (`\r\n`) and LF (`\n`) line endings. Empty vCard bytes are handled gracefully (logged warning, defaulting to vcard4). Covered by tests 4, 15, 16, 17 in `tst_remote_contacts_backend.cpp`.

## Discovery

(none yet; Phase Ib is in flight)

## How to update this file

When tasks complete or discoveries surface, add them here. When the phase lands, flip Status to `✅ landed YYYY-MM-DD` and link the tag. Keep this file ≤ 150 lines.
