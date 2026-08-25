# P2 — Contacts backends: status

**Status:** OPEN (handed to a fresh session 2026-08-25). Nothing started
beyond this scoping.

Goal (proposal §4 P2): `GraphContactsBackend` + `GooglePeopleBackend`,
crossing-gated both directions, live-checkpointed per invariant 1.

## Binding constraints for this phase

- **O66(f): consumer Graph contact GET-by-id is flaky/broken** — ALL reads
  and deletes go through listings / delta / `$expand`. Never address a
  contact by id on consumer accounts; treat item-by-id as unreliable even
  where it once worked.
- **O66+correction carrier protocol:** Graph open-extension carriers ride
  nav `POST .../contacts/{id}/extensions` — NEVER PATCH-borne, never
  inline-at-create — then collection-level
  `$expand=extensions($filter=Id eq 'Microsoft.OutlookServices.OpenTypeExtension.kalburator.canon')`.
  Filter on the RETURNED full-id prefix. Re-read after write; never trust
  a create echo.
- **Google side:** `people.connections` paging (`pageSize`/`pageToken`,
  `personFields` projection); carriers = `clientData` rows
  (`x-canon-*`, string values) — the FIRST proven live-Reversible channel
  (O66 verdict table).
- **O69 lesson applied proactively:** assume delta/listing pages may
  project reduced field sets; union-merge partial items over cached rich
  records rather than replacing (the ms-calendar fix is the template).
- **O65:** events never index participant emails — persons belong to the
  contacts/person domain; keep that boundary in tests.
- Wire knowledge lives in
  `docs/campaign/eee/vendor-rest-api-wire-notes.md` (§3 contacts section);
  update it same-commit as any new O-entry.

## Reusable from P1 (do not rebuild)

- Transport: `GraphApiClient` (typed errors, retries) /
  blocking HTTP; auth via `src/graph/graphauthenticator.h` +
  `src/google/googleauth.h`; token caches under `KALBURATOR_{MSGRAPH,GOOGLE}_DIR`.
- Mock-server pattern: `tests/graph/mockgraphserver.*` /
  `tests/google/mockgoogleserver.*` are the templates for a
  `MockGraphContactsServer` / People double (record requests incl.
  Authorization header — pin Bearer injection from test one).
- Backend pattern: `GoogleCalendarBackend` is the reference shape
  (heap-held FetchState/ApplyState, single-shot memo, atomic persisted
  state, terminal write contract); MS delta machinery in
  `msgraphcalendarbackend.cpp`.
- Edge stages already landed + loss profiles declared:
  `src/contacts/{mscontactcanonstages,googlepersoncanonstages}.*`;
  sanitized fixtures `tests/fixtures/vendor/{microsoft/contacts-listing.json,
  google/contacts-connections.json}`; promotion-slot precedent in the
  calendar suites.
- Live-checkpoint suite pattern:
  `tests/google/tst_google_calendar_backend_live.cpp` (QSKIP without
  creds; CORPUS-tagged probes; unconditional cleanup; sweep-clean both).

## Checklist

- [ ] Design pass: read SyncBackend contract notes (see P1 file's design
      decisions), decide record-id anchor policy per vendor (contact ids
      ending `=` must NOT be URL-encoded in paths — encoded ⇒ 404)
- [ ] Mock servers (contacts-flavored) + transport-level pins
- [ ] `GraphContactsBackend`: listings/delta reads, nav-POST carrier
      writes, folder discovery
- [ ] `GooglePeopleBackend`: connections paging, clientData carriers
- [ ] Crossing gates both directions (O64 rule)
- [ ] Live checkpoints vs real accounts (invariant 1)

## Open questions to settle in the design pass

1. Delta vs listing reads on Graph contacts: does `/me/contacts/delta`
   suffer O69-style skeletons here too? Probe BEFORE building on it.
2. Person-identity interplay: contacts sync touches the §5 identity layer
   (P5 wires it into the engine) — keep backends identity-free, but make
   sure records carry whatever the resolver needs.
3. Photo/binary fields (contact photos) — v1 scope or declared Dropped?
