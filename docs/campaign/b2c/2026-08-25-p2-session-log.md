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
- (progress appended as work lands)
