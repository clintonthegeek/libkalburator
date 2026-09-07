# P2 — Contacts backends: status

**Status:** CLOSED 2026-08-25 — both contacts backends live-checkpointed
(P2.f PASSED both directions); see checklist + FINDINGS O71/O72/O73.

Goal (proposal §4 P2): `GraphContactsBackend` + `GooglePeopleBackend`,
crossing-gated both directions, live-checkpointed per invariant 1.

## Design decisions (pinned 2026-08-25, from the design pass + live probes)

- **File placement:** `src/contacts/graphcontactsbackend.{h,cpp}` +
  `src/contacts/googlepeoplebackend.{h,cpp}`; test bed `tests/contacts/`
  (`mockgraphcontactsserver`, `mockpeopleserver`, backend suites, live
  suites) wired like `tests/graph/` / `tests/google/`.
- **Base class: `SyncBackendBase` directly** (contacts domain has no
  calendar-typed `SyncBackend` subclass; the P1 backends are the shape
  template — heap-held FetchState/ApplyState via shared_ptr, single-shot
  fetch memo, atomic persisted state file per cacheDir, per-collection
  FIFO via enqueueOperation, terminal write contract).
- **BackendRecord.id = server contact id** (Graph: raw `id` — ids ending
  `=` must NEVER be URL-encoded in paths, O66(d); Google:
  `resourceName` e.g. `people/c123`). Vendor JSON rides in record data;
  canon conversion stays owned by the landed edge stages.
- **Graph READS = expanded full listings, NOT delta** — live probe found
  contacts change tracking rejects `$expand/$top/$filter/...` outright
  (**FINDINGS O70**), so carriers are unreachable on delta pages and
  GET-by-id enrichment is broken (O66(f)). Every fetch = per-folder walk
  of `/me/contactFolders/{id}/contacts?$expand=extensions($filter=Id eq
  'Microsoft.OutlookServices.OpenTypeExtension.kalburator.canon')`
  (+ `/me/contacts` for the default folder), reporting the FULL merged
  set. Correctness over incrementality; delta is a v2 option.
- **Graph WRITES:** create = POST `/me/contacts` with demoted body MINUS
  the `extensions[]` carrier row (never inline-at-create, O66
  correction); then nav POST the carrier to `/contacts/{id}/extensions`;
  never trust the create echo — next fetch's expand re-reads. Update =
  PATCH in place with plain fields only (carrier changes routed to the
  nav channel, not PATCH-borne). addIdAlias bridged on create.
  Delete-by-id accepts 204/200; a 404 triggers one confirming re-list —
  gone ⇒ success (idempotent semantics under O66(f) flakiness),
  still-present ⇒ fail loud (no silent best-effort).
- **Folder discovery:** GET `/me/contactFolders`; each folder becomes an
  available collection (writable except `contacts`-style system folders?
  — v1: all folders writable; `parentFolderId` recorded in extras).
- **Google reads/writes:** `people.connections?pageSize=&pageToken=&
  personFields=` paging per the promote stage's field needs;
  `personFields` must cover every field the promote stage reads (fields
  list shared constant between client call and stage expectations);
  `nextSyncToken` captured when present. Creates/updates ride the normal
  People API seams (`createContact`, `{resourceName}:updateContact` with
  `updatePersonFields`) — clientData carriers ride inline (live-Reversible,
  O66 verdict table). Deletes via `{resourceName}:deleteContact`.
- **Photos:** v1 declares existing loss-profile rows and does nothing
  more — MS Dropped (nav resource), Google Simplified URL-only. No
  binary fetch in P2 (open question Q3 closed).
- **Identity-free backends:** records carry exactly what the edges emit
  (anchors + email rows); resolver needs nothing extra (open Q2 closed).
- **O69 lesson retained where applicable:** any listing page item lacking
  expected richness is union-merged over cached rich records rather than
  clobbering (defensive, cheap).

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

- [x] Design pass (2026-08-25): SyncBackend contract read; record-id
      anchor policy pinned (`=` ids never URL-encoded); live probes
      settled delta-vs-listing (**O70**), carrier ingest shape, photo
      scope, identity interplay — decisions above
- [x] P2.b: contacts-flavored mocks + transport pins (landed 2026-08-25 —
      `MockGraphContactsServer` (folders, expand-on/off carrier
      projection, nav-POST carriers, PATCH-with-extensions ⇒ 500,
      wrong-prefix filter ⇒ 500, `=`-suffixed minted ids) +
      `MockPeopleServer` (pageSize/pageToken, personFields projection,
      requestSyncToken + queued changes + expired-token 410,
      createContact clientData echo); 11+9 slots green)
- [x] P2.c: `GraphContactsBackend` (landed 2026-08-25 — expanded-listing
      reads over `/me/contacts` + per-folder paths, never delta (O70);
      listing authoritative w/ union-merge enrichment; create strips
      extensions[] → POST → nav POST carriers → alias bridge;
      PATCH-in-place plain-fields; delete 204/200 with 404-then-relist
      confirmation; atomic state resume. 10 slots green)
- [x] P2.d: `GooglePeopleBackend` (landed 2026-08-25 — manual page-walk
      over connections[] (fetchCollection hard-reads items[] — calendar
      transport only); shared personFields constant = promote-stage read
      surface; sync_token incremental + 410 one-shot re-walk (O42);
      metadata.deleted tombstones; create rides clientData inline,
      alias bridged to minted resourceName; updatePersonFields derived
      from body keys; 404-delete-as-success (People not flaky —
      deliberate deviation from Graph pin); state keyed to query-
      template sha256. 12 slots green)
- [x] P2.e: Crossing gates both directions (landed 2026-08-25 — superset
      canon (all 26 catalogue props) crossed both ways, every divergence
      declared; no undeclared losses found. Two notes: Google demote
      defaults `primary:true` on every name row when canon has none
      (gate-legal, semantically inventive — future O-note candidate);
      `canonToMsContactLoss` declares photos Dropped but the MS demote's
      unhandled-set routing actually makes it carrier-Reversible —
      declared ⊇ actual, conservative, mislabel to fix next time the
      profile is touched)
- [x] P2.f: Live checkpoints vs real accounts (invariant 1) — LANDED
      2026-08-25, BOTH PASSED. `tests/contacts/tst_graph_contacts_backend_live.cpp`
      (folder discovery → expanded full listing 36 records → CORPUS probe
      create w/ carrier nav POST ('=' id minted, alias bridged) → expand
      read-back → PATCH rename → Q4 carrier update → delete+verify;
      unconditional cleanup guard) and
      `tests/contacts/tst_google_people_backend_live.cpp` (connections walk
      72 records → CORPUS person create w/ inline clientData → carrier
      intact on refetch → etag-bearing :updateContact rename → delete+
      verify). Live findings caught & fixed same-session: **O71**
      (`people.createContact` is collection-level; base-url/versioning
      doubling in GooglePeopleBackend ctor default), **O72**
      (:updateContact REQUIRES etag — listings always deliver it;
      displayName server-derived), **O73** (Graph nav POST = UPSERT —
      settles open question 4: NO backend change needed). Mocks re-pinned
      to the corrected shapes; sweep-clean run after passes.

## Open questions to settle in the design pass

1. ~~Delta vs listing reads on Graph contacts~~ — SETTLED: delta rejects
   `$expand` (O70) ⇒ expanded listings every fetch; see pinned decisions.
2. ~~Person-identity interplay~~ — SETTLED: backends identity-free; edges
   already emit what the resolver needs.
3. ~~Photo/binary fields~~ — SETTLED: declared per existing loss profiles
   (MS Dropped, Google Simplified URL-only); no binary fetch in P2.
4. ~~Carrier UPDATE path on Graph~~ — SETTLED at P2.f (**O73**, live):
   nav POST with the same `extensionName` is UPSERT on consumer Graph
   (deterministic id kept, values replaced, one row on read-back); the
   backend's strip-then-nav-POST update channel is correct as-is; a nav-
   PATCH variant would be redundant.
