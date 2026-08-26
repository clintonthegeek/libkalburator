# P2 — Contacts backends: session log

Orchestration log for P2 (started 2026-08-25, fresh session after the
`d6ed881` handoff). Status/checklist lives in
`2026-08-25-p2-contacts-status.md` (kept accurate per phase-status rule);
this file records per-step progress, decisions, and deviations.

## Session 1 — 2026-08-25

- Read handoff (`STATUS.md`, proposal, `2026-08-25-p2-contacts-status.md`,
  wire notes §3 contacts).
- Launched design-pass exploration: SyncBackend contract + P1 backend
  reference shapes; contacts edge assets (stages, fixtures, mock-server
  patterns, carrier protocols).
- **Live probes (design pass):**
  - `/me/contacts/delta?$top=3` ⇒ 400 `ErrorInvalidUrlQuery` — change
    tracking over Contacts rejects `$orderby/$filter/$select/$expand/
    $search/$top`. Bare `/me/contacts/delta` works; initial walk returns
    FULL projections (no O69 skeletons).
  - Plain listing + filtered `$expand` works; carrier returned inline
    with full-prefix id (`Microsoft.OutlookServices.OpenTypeExtension.
    kalburator.canon`); live account already carries an
    `x-canon-gender` row on a GraphCLI test contact.
  - Consequence recorded as **FINDINGS O70** (+ wire-notes §3 update):
    delta cannot carry extensions ⇒ v1 read strategy = expanded full
    listings per folder, every fetch.
- **Design decisions pinned** in `2026-08-25-p2-contacts-status.md`
  (file placement, SyncBackendBase base class, record-id anchors,
  Graph read/write/carrier/delete semantics, folder discovery, Google
  People paging + personFields constant, photos closed as declared
  losses, identity-free backends). Open Q4 added: nav-PATCH extension
  update seam — probe at P2.f.
- Next: P2.b contacts-flavored mock servers + transport pins.
- **P2.b LANDED** (subagent): `MockGraphContactsServer` +
  `MockPeopleServer` + two transport suites (11+9 slots green). Note:
  QUrlQuery PrettyDecoded does not match `%24expand`-style encoded keys —
  the Graph double resolves query items FullyDecoded (O60-family; pin
  candidate for an O-entry if it bites again).
- **P2.c LANDED** (subagent): `GraphContactsBackend` +
  `tst_graph_contacts_backend` (10 slots green). Read-path semantics
  pinned during implementation: complete listing is AUTHORITATIVE
  (absent ids ⇒ dropped, deletes propagate); cached rich copies serve as
  merge-enrichment only. Carrier-POST failure settles the record failed
  (fail-loud over silent carrier loss). Suite baseline now 202 slots;
  only the 4 known Radicale-environmental failures remain.
- **P2.d LANDED** (subagent): `GooglePeopleBackend` +
  `tst_google_people_backend` (12 slots green). Notable: People speaks
  `connections[]`, so the page walk is rawRequest-based (fetchCollection
  is calendar-shaped); tombstones via `metadata.deleted`; delete treats
  404 as success WITHOUT a confirming re-list (deliberate deviation from
  Graph O66(f) pin — People deletes are not flaky). Suite baseline now
  203 slots.
- **P2.e LANDED** (subagent): contacts superset crossing gates both
  directions green on first iteration — all 26 catalogue properties
  crossed G⇄M, every divergence declared. No undeclared losses. Two
  observations recorded in the status doc: Google primary-invention on
  unpinned name rows; MS photos profile row mislabeled Dropped vs actual
  carrier-Reversible (conservative direction).
- **P2.f LANDED — LIVE CHECKPOINTS PASSED BOTH DIRECTIONS** (subagent
  wrote suites; orchestrator re-ran with creds env to confirm genuine
  non-skipped passes):
  - `tst_graph_contacts_backend_live` vs real Outlook.com (6.3s):
    folder discovery → expanded listing (36 records) → probe create w/
    carrier ('='-id minted) → expand read-back → PATCH rename → **Q4
    verdict: nav POST is UPSERT** → delete+verify.
  - `tst_google_people_backend_live` vs real Google account (5.4s):
    connections walk (72 records) → create w/ clientData → refetch
    carrier intact → etag-bearing :updateContact rename → delete+verify.
  - Findings caught & fixed same-session: **O71** (People createContact
    is COLLECTION-level `/v1/people:createContact`; resource-level form
    404s; plus GooglePeopleBackend ctor base was version-full ⇒ doubled
    `/v1/v1/` on live callers — house rule now: vendor base URLs are
    VERSION-LESS), **O72** (:updateContact REQUIRES etag; listings
    always deliver it; displayName server-derived from given+family),
    **O73** (Graph carrier nav POST = UPSERT — open Q4 settled, no nav-
    PATCH needed). Mocks re-pinned to corrected shapes; accounts swept.
- **P2 CLOSED.** Suite baseline 205 slots (203 mock + 2 live); only the
  4 known Radicale-environmental failures remain. Next phase: P3 todo
  backends + kind-demux deliverable.
- (progress appended as work lands)
