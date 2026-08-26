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
- (progress appended as work lands)
