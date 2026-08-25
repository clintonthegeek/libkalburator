# Campaign proposal — vendor-native backends to consumers ("B2C")

**Proposed:** 2026-08-25 (post-Tier-A close, v1.03). Status: DRAFT pending
adoption. Successor to the vendor-convergence (EEE) campaign — consumes its
edges, fixtures, harnesses, and wire knowledge rather than extending them.

**One-line goal:** turn the nine proven edges + lab-grade transports into
five production `SyncBackend`s behind real providers, so both consumers can
map Google/Microsoft accounts the way they already map CalDAV/CardDAV.

---

## 0. Definition of victory

A consumer host (PlanStan first, WildPalms identical mechanically):

1. Adds a Google account and/or a Microsoft account through normal provider
   config UX (OAuth consent, secure token storage — no env vars, no lab CLIs).
2. Creates mappings onto any of the three converged domains — calendar,
   contacts, tasks — against vendor-native APIs (not CalDAV bridges).
3. Runs two-way sync through the unmodified stock `SyncEngine`, with
   conflict policies, baselines, id-aliasing, progress relay, connection
   states, and cancellation all behaving exactly as on the CalDAV path.
4. Survives the O64 discipline: every new vendor-pair/domain crossing has a
   green gate before ship.

Victory is **not**: RSVP flows, ACL editing, scheduling negotiation, or any
Tier-B interior. Those stay deferred (§5 scoping).

## 1. Starting inventory (what we do NOT rebuild)

| Asset | State | Reuse |
|---|---|---|
| 9 stock-shape edges (`google-event`, `ms-event`, `google-person`, `ms-contact`, `google-task`, `ms-todotask` + iCal/vCard peers) | landed, byte-pinned matrix | translation is DONE |
| Loss profiles + declared-normalization sets | maintained in same commit discipline | backends inherit write rules |
| Wire-truth corpus (sanitized fixtures + machine-local captures) | committed / gitignored respectively | test bed |
| `tools/msroundtrip`, `tools/groundtrip` | checkpoint runners | live gates per backend |
| `MSGraphCalendarBackend` + `GraphApiClient` + mock Graph server | mock-green (11 slots) | harden, don't rewrite |
| OAuth flows + HTTP plumbing | inside `tools/{graphcli,googlecli}` | port, then delete duplication |
| Identity layer (`src/identity/`) | standalone, doctrine-pinned | integration target (P5) |
| CalDAV/CardDAV provider pair | production | the pattern to copy for P4 |

## 2. Constraints inherited from prior campaigns

- **Invariant 1 (loss profile first):** no stage work here — but every
  WRITE-path decision a backend makes must trace to a declared row in an
  existing loss profile; if a needed ruling is missing, declare before code.
- **O63:** edge-count/matrix pins move in the same commit as any registry
  growth (should not trigger here; backends grow no edges).
- **O64/O65:** crossing-gate coverage per vendor pair/domain; events never
  index participant emails.
- **EEE Part IV (binding ethics):** local custody of credentials and caches;
  the graph forgets (no cross-account merge); strangers stay strangers; one
  explicit identity link rule; loud about limits. Backends hold tokens and
  caches host-side, never sync them anywhere.
- **Nepomuk post-mortem constraints** (EEE §2): replay-first testing,
  fail-loud identity, no silent best-effort writes.

## 3. Campaign invariants (new)

1. **No backend merges to main without a live-account checkpoint** against
   a real consumer-tier account (mock-green is necessary, not sufficient —
   O67 proved mocks cannot see create-path rewrites).
2. **Every create path strips read-only fields per vendor** (Google:
   `created`/`updated`; Graph tolerates timestamps but mints uid/organizer/
   body-form regardless) — pinned by unit tests derived from O67 tables.
3. **uid continuity is never assumed across a Graph create** — re-read +
   remap through the O55 aliasing machinery; Google legs may trust client
   anchors.
4. **Carriers are written by the documented protocol per channel** (nav
   POSTs + filtered expand for Graph open extensions; plain fields for
   Google), never by whatever the last drill used.
5. **Transport owns no business logic** — auth/HTTP/retry only; semantics
   stay in backends/stages so the mock server remains a faithful double.

## 4. Phases

### P0 — Transport library-ization (~2 sessions)

Port out of the labs, into `src/transport/` (or `src/{graph,google}/`):

- `GoogleAuthentication` (loopback flow) + `GraphAuthentication`
  (device-code) → async, refresh-rotating, multi-profile token stores.
  Secure storage behind the host's existing credential facilities (pattern
  TBD vs how KWallet/secret-service is handled today; local-custody rule).
- Async HTTP clients on QNetworkAccessManager per the E5 threading
  contract (lab CLIs' blocking loops are reference-only).
- Base-URL injection preserved everywhere (mock-server testability).
- Retry/backoff + rate-limit handling (both vendors send Retry-After);
  error discrimination surfaced as typed failures (the H-series lesson).

Exit: lab CLIs re-pointed at the library (delete duplicated auth), mock
suite still green.

### P1 — Calendar backends to production (~3–4 sessions)

- **`GoogleCalendarBackend`** (new): calendarList discovery; events
  list/paged reads; incremental via `syncToken` (+ full-resync on 410 Gone);
  CRUD with O67 rules; iCalUID anchor trusted client-side (invariant 3's
  asymmetry note); carriers = extendedProperties.private (live-Reversible).
- **`MSGraphCalendarBackend` hardening**: delta-query walk against the LIVE
  account (deltaLink persistence + 410 fallback exist in design; verify
  against reality); PATCH-over-recreate write strategy; post-create re-read
  + uid remap; series/override fetch via calendarView/instances (7.B
  machinery); body→HTML and Teams-provisioning normalization on read-back.
- **Live checkpoints both directions** with `ms-roundtrip`/`g-roundtrip`
  (invariant 1).

Exit: both calendars sync two-way vs real accounts through stock engine
tests; crossing gate G-calendar ⇄ canon ⇄ MS-calendar green.

### P2 — Contacts backends (~2 sessions)

- `GraphContactsBackend`: listings/delta reads ONLY (O66(f)); open-extension
  carriers via nav POSTs + collection-level filtered expand (full-id prefix
  rule); folder discovery.
- `GooglePeopleBackend`: `people.connections` paging; `clientData` carriers
  (live-Reversible); contactGroups read.
- Crossing gates both directions.

Exit: contacts converge G ⇄ MS through the hub with person-identity
resolution holding (strangers stay strangers).

### P3 — Todo backends (~2 sessions)

- `GoogleTasksBackend`: tasklists + tasks CRUD; position-string ordering;
  NO carrier channel (Dropped rulings stand) — honest losses.
- `GraphTodoTaskBackend`: dueDateTime-required-with-recurrence create rule;
  server dueDateTime rewrite tolerance; extension carriers via nav POST;
  **re-read after every write** (inline-create wire-lie).

**Kind-demux deliverable (added 2026-08-25, architecture decision):**
iCal bundles VEVENT+VTODO in ONE collection while vendor-native providers
keep events and tasks discrete. The rectification rule — adopted because
the `todo` canon domain's spine IS `[ical-vtodo → canon]` (EEE Phase 3),
making Radicale VTODOs and vendor tasks data-model peers already — is that
**transport grouping never crosses a domain boundary**: a mixed DAV
collection surfaces as TWO `ProviderBackendSpec`s from one provider (a
calendar-domain spec filtered to VEVENT/VJOURNAL; a todo-domain spec
filtered to VTODO), using the proven multi-spec mechanism
(`MultiProtocolDavProvider` precedent) plus `RecordFilter`/
`FilteredCollectionBackend` (already inside `RemoteCalendarBackend`).
Filters guarantee disjoint record sets ⇒ no write contention on shared
hrefs; ids stay href-stable in both views; `DiscoveredCalendar`
supportsVTodo-style flags demote to discovery/UI metadata only. This
unlocks the full EEE triangle (DAV todo ⇄ vendor task convergence through
the todo hub); without it only vendor⇄vendor task sync works. Vendor-side
todo backends need nothing here — they are discrete by construction.

Exit: fixture-promotion corpora replayed through the real backends; both
todo-edge roundtrip contracts survive the network.

### P4 — Providers, config UX, plugin contribution (~1–2 sessions)

- `IProvider` implementations for Google and Microsoft per domain-group
  (mirroring the v0.93 `createBackends()` spec-list contract).
- Consent UX (device-code text flow for MS MVP; loopback browser for
  Google), account management, token revocation paths.
- Connection states, progress relay, parallel-sync opt-ins wired like the
  DAV pair.

Exit: a consumer host can map a vendor account end-to-end without touching
library internals.

### P5 — Identity-layer integration (~1–2 sessions)

- PersonDirectory consulted during engine runs for cross-vendor person
  convergence; explicit-link rule enforced (one manual link, never inferred
  merges); attendee alias expansion (O57(t)) handled at identity layer, not
  string-matching.

Exit: the A1 engine hub test extended to live vendor records converging
with persons resolved.

### P6 — Consumer handoff (their repos)

PlanStan: pin bump, provider registration, mapping wiring (~1–2 sessions
their side; they have done this dance for CalDAV). WildPalms: same shape.
Library obligation ends at documented provider API + coordination-page
update.

## 5. Explicit non-goals (this campaign)

RSVP/iTIP flows, scheduling negotiation (B1) — gated on PlanStan
organization features. Visibility/ACL editing (B2), resource calendars
(B3), taxonomy entities (B4), MAPI deep probe (B5) — recon continues per
the EEE roadmap but nothing here depends on them. Attendees ride canon
read-only; hosts ignore what they don't model yet.

## 6. Risk register

| Risk | Mitigation |
|---|---|
| Live-API behaviors mocks can't reproduce (create rewrites, flaky GET-by-id) | invariant 1 live checkpoints; O67 tables as unit-test seed |
| Token-storage platform variance | abstract store interface; host provides impl; lab profile dirs are the reference behavior |
| Graph delta semantics differ from docs | budget one session of delta-reality drift; deltaLink 410 fallback designed in from 7.C |
| Scope creep toward Tier-B interiors | §5 non-goals are binding; new interiors open their own mini-campaign pages |
| Ethics violations via convenience (e.g. caching identities broadly) | Part IV pins restated in code-review checklist; seizure test applied to every new cache |

## 7. Session protocol

Same house rhythm as EEE: stub/mock-green first, live checkpoint second,
docs updated in the landing commit (phase-status rule), findings numbered
continuously after O67, wire-notes doc updated in the same commit as any
new O-entry. Each phase opens/closes a status file under
`docs/campaign/b2c/`.

---

*Adopting this campaign does not reopen EEE; it consumes it.*
