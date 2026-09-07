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
- **Post-close integration (same session):** PlanStan filed the VTODO
  semantic-parity handoff (W1–W8). Read the handoff + all four audit
  docs in their repo; ran scoped recon of our todo machinery
  (`vtodocanonfields`, differ wiring, BaselineStore, capability
  discovery, org backend, vendor task stages). Accepted all items with
  scoping edits — response:
  `docs/2026-08-25-vtodo-parity-handoff-response.md`; new campaign
  tracker: `docs/campaign/vtodo-parity/STATUS.md`; coordination page §2f;
  FINDINGS **O74** (providerExtras invisible to canonical differ).
  Key recon surprises: blob pipeline keys by UID alone (master+exception
  collide → W1 is real identity work, not documentation); VALARM already
  first-class; org backend drops unknown X-props; BaselineStore has no
  multi-record transactions. P3 now gates the parity work.
- **P3 OPENED** (same session, continuing autonomously): design pass done
  via recon subagent; decisions pinned in
  `docs/campaign/b2c/2026-08-25-p3-todo-status.md`. Key pins: Tasks API
  has NO sync tokens (full paged listings); envelopes fit stock clients
  (items[] / value[]) so no People-style rawRequest walks; Graph todo
  reads = expanded listings never delta (O70-family); due-required-with-
  recurrence create fails LOUD; demux = two specs (cal/todo) over shared
  RemoteCalendarBackend with FilteredCollectionBackend views — RAW-BYTES
  filter gap flagged (RecordFilter speaks canon-JSON, CalDAV records are
  iCal bytes).
- **P3.b LANDED** (subagent): both todo mock servers + transport suites
  (10+12 slots). Commit 08ed8fc.
- **P3.c LANDED** (subagent): `GoogleTasksBackend` +
  `tst_google_tasks_backend` (9 slots). Note: backends on the neutral
  SyncBackendBase carry vendor JSON verbatim — the canon stage demote is
  NOT invoked in-backend (legacy Incidence seam only existed for
  calendar-typed ops); strip seam implemented directly. Suite baseline
  now ~206 slots.
- **P3.d LANDED** (subagent): `GraphTodoTaskBackend` +
  `tst_graph_todo_backend` (11 slots, first-run green). O66(b) gate
  fires pre-network on create AND update; terminal error message now
  prefers recorded per-record validation messages so the rule is
  nameable. Full suite 205/205 excluding known Radicale set.
- **P3.e LANDED** (subagent): kind-demux — hybrid DAV collections demux
  into cal+todo specs over a shared transport via new `KindDemuxBackend`;
  FCB gained an additive raw-kind mode (first-component-block sniff);
  neither-kind records drop from both views; writes passthrough. 5 new
  slots; sinks suite untouched. WATCH: tst_syncengine_unification flaked
  once under full-suite parallel load, green on re-runs + isolation —
  likely environmental (Radicale stress); re-check at Radicale reset.
- **P3.f LANDED — LIVE CHECKPOINTS PASSED BOTH DIRECTIONS** (subagent;
  orchestrator re-ran binaries with creds env: 4 passed / 3.5s Google,
  4 passed / 2.6s Graph — genuine non-skip). Findings **O75** (Google
  Tasks discovery requires `/users/@me` — vendor regression caught by
  invariant 1; ctor base fixed), **O76** (todoTask wire property is
  `title` not `subject`, create REQUIRES it), **O77** (todoTask
  extension prefix is `microsoft.graph.openTypeExtension.*` — the
  contacts OutlookServices prefix does NOT generalize across resources).
  Due midnight-UTC degradation pinned live; fixture-replay slots green.
  Accounts swept clean.
- **P3 CLOSED 2026-08-26.** Full suite 204/204 excluding known Radicale
  set (unification flake did not recur). The vtodo-parity campaign's
  W1/W2 vendor test legs now exist. Remaining P3-scoped follow-up:
  kind-demux consumer wiring → P4. Next: vtodo-parity VP.a (W8).
- (progress appended as work lands)
