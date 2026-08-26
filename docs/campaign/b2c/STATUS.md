# B2C Campaign STATUS — vendor backends to consumers

Per `docs/2026-08-25-campaign-proposal-vendor-backends-to-consumers.md`
(adopted 2026-08-25). Updated in the same commit as plan state
(phase-status-docs rule). Wire knowledge lives in
`docs/campaign/eee/vendor-rest-api-wire-notes.md` — same-commit rule
applies to new O-entries.

**Last updated:** 2026-08-26 (P3 LANDED — P3.f live checkpoints for both vendor todo backends PASSED vs real accounts; P3 gates the vtodo-parity campaign)

## Where we stand

| Phase | State |
|---|---|
| P0 transport library-ization | **done 2026-08-25** — `src/net/blockinghttp` + `src/net/backoff.h`; Graph OAuth in `src/graph/graphauthenticator`, Google OAuth in `src/google/googleauth` (injectable endpoints/browser hook); both CLIs re-pointed, lab auth/HTTP deleted; mock records Authorization header (pin); GraphApiClient GETs retry transient failures (default 2, writes never) |
| P1 calendar backends live | **done 2026-08-25** — GoogleApiClient + MockGoogleServer + GoogleCalendarBackend (syncToken walks, tombstones, 410 self-heal, persisted resume, O67/O68 write rules) and MSGraph hardening (deltaStep retries, O69 skeleton union-merge); BOTH live checkpoints PASSED vs real accounts (`tst_google_calendar_backend_live`, `tst_ms_graph_calendar_backend_live`; skip without creds). Findings O68 + O69 caught & fixed same-session |
| P2 contacts backends | **done 2026-08-25** — GraphContactsBackend + GooglePeopleBackend mock-green, crossing-gated both directions (P2.e), and BOTH live checkpoints PASSED (`tst_graph_contacts_backend_live`, `tst_google_people_backend_live`; skip without creds). Findings O71 (People createContact is collection-level + base-url doubling), O72 (:updateContact REQUIRES etag; displayName server-derived), O73 (Graph carrier nav POST = UPSERT — Q4 settled) caught & fixed same-session |
| P3 todo backends | **done 2026-08-26** — P3.c `GoogleTasksBackend`, P3.d `GraphTodoTaskBackend` (mock-green), P3.e kind-demux (`MultiProtocolDavProvider::createBackends()` partitions hybrid collections into cal+todo specs via `KindDemuxBackend`; raw iCal discriminator sniff; writes pass through unstamped), and **P3.f BOTH live checkpoints PASSED** vs real accounts (`tst_google_tasks_backend_live`, `tst_graph_todo_backend_live`; skip without creds; fixture-replay slots included). Findings caught & fixed same-session: **O75** (Google Tasks discovery now REQUIRES `/users/@me`; ctor base also fixed to version-less-with-/tasks), **O76** (todoTask wire property is `title`, create REQUIRES it — backend/mock/tests re-pinned), **O77** (todoTask extension-id prefix is `microsoft.graph.openTypeExtension.*`; OutlookServices-prefixed expand filter 500s deterministically on /me/todo). Remaining: kind-demux consumer wiring — still gates the vtodo-parity campaign |
| P4 providers/config UX | not started |
| P5 identity wiring | not started |
| P6 consumer handoff | not started |

## Reading order for a fresh session

1. This file.
2. The proposal (§3 invariants are binding):
   `docs/2026-08-25-campaign-proposal-vendor-backends-to-consumers.md`.
3. **If picking up P2:** `2026-08-25-p2-contacts-status.md` — it is the
   handoff document: constraints, reusable assets, checklist, open
   questions.
4. Wire knowledge: `docs/campaign/eee/vendor-rest-api-wire-notes.md`
   (+ FINDINGS O57–O77 as evidence).
5. Findings index below; numbering continues after O77.

## Findings index (this campaign)

- **O68** — Google events.insert REJECTS client-supplied transport ids
  (400 "Invalid resource id value"); create seams strip
  created/updated/id. Caught by the P1.f Google live checkpoint.
- **O69** — consumer Graph delta pages deliver SKELETON projections
  (no uid/iCalUId/subject); MSGraphCalendarBackend union-merges skeletons
  over cached records; identity fallback chain fires on normal traffic.
  Caught by the P1.f MS live checkpoint.
- **O75** — Google Tasks discovery now REQUIRES `/users/@me/lists`
  (plain `users/me` 404s with an HTML page — regression vs the
  2026-08-24 capture); ctor base fixed to version-less-with-/tasks.
  Caught by the P3.f Google live checkpoint.
- **O76** — v1.0 todoTask wire property is `title` (create REQUIRES it;
  no `subject` anywhere); B2C transport layer re-pinned from subject.
- **O77** — todoTask open-extension ids mint as
  `microsoft.graph.openTypeExtension.*`; an OutlookServices-prefixed Id
  in the expand filter ⇒ HTTP 500 deterministically on /me/todo.
(evidence in repo-root `docs/campaign/FINDINGS.md`; wire knowledge in
`docs/campaign/eee/vendor-rest-api-wire-notes.md`)
