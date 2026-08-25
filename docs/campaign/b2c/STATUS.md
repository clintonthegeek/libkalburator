# B2C Campaign STATUS — vendor backends to consumers

Per `docs/2026-08-25-campaign-proposal-vendor-backends-to-consumers.md`
(adopted 2026-08-25). Updated in the same commit as plan state
(phase-status-docs rule). Wire knowledge lives in
`docs/campaign/eee/vendor-rest-api-wire-notes.md` — same-commit rule
applies to new O-entries.

**Last updated:** 2026-08-25 (P0 CLOSED; P1 next)

## Where we stand

| Phase | State |
|---|---|
| P0 transport library-ization | **done 2026-08-25** — `src/net/blockinghttp` + `src/net/backoff.h`; Graph OAuth in `src/graph/graphauthenticator`, Google OAuth in `src/google/googleauth` (injectable endpoints/browser hook); both CLIs re-pointed, lab auth/HTTP deleted; mock records Authorization header (pin); GraphApiClient GETs retry transient failures (default 2, writes never) |
| P1 calendar backends live | **in progress** — GoogleCalendarBackend DONE incl. LIVE checkpoint passed (O68 caught + fixed); MS live-delta verification remains |
| P2 contacts backends | not started |
| P3 todo backends | not started |
| P4 providers/config UX | not started |
| P5 identity wiring | not started |
| P6 consumer handoff | not started |

## Reading order for a fresh session

1. This file.
2. The proposal (§3 invariants are binding):
   `docs/2026-08-25-campaign-proposal-vendor-backends-to-consumers.md`.
3. Wire knowledge: `docs/campaign/eee/vendor-rest-api-wire-notes.md`
   (+ FINDINGS O57–O67 as evidence).
4. Findings index: `FINDINGS.md` in this directory when entries exist;
   numbering continues after O67.

## Findings index (this campaign)

(none yet)
