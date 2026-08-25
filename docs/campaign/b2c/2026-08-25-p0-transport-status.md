# P0 — Transport library-ization: status

**Status:** in progress (opened 2026-08-25). P0.b landed 2026-08-25:
Graph OAuth + shared blocking HTTP in the library; graphcli re-pointed
(graph suites green: api_client, mock_server, ms_backend).

Goal (proposal §4 P0): port auth + HTTP out of the lab CLIs into the
library, async per the E5 threading contract, base-URL injection preserved,
retry/backoff + typed error discrimination; lab CLIs re-pointed and their
duplicated auth deleted; mock suites green.

## What exists now

- `tools/graphcli/`: `graphauth.{h,cpp}` (device-code flow, token cache w/
  refresh rotation, multi-profile dirs), `graphclient.{h,cpp}` (blocking
  HTTP loop), `main.cpp` verbs.
- `tools/googlecli/`: `googleauth.{h,cpp}` (loopback OAuth), `googleclient.*`.
- `src/graph/graphapiclient.{h,cpp}`: first library-cut Graph client used by
  `MSGraphCalendarBackend` + mock server tests.

## What remains

(see proposal §4 P0; updated here as work lands)

- [x] Design pass: reconcile graphapiclient vs GraphCLI client; API shape
      (two explore reports 2026-08-25; decisions: per-vendor auth types in
      src/{graph,google}, shared blocking HTTP in src/net, lab-path helpers
      stay CLI-side, mock gains header recording before client dedup)
- [x] GraphAuthentication → src/ (`src/graph/graphauthenticator.{h,cpp}`,
      namespace Kalburator::Graph: Tokens/TokenStore/DeviceCodeFlow/
      refreshTokens; authority URL injectable; consent printing + lab paths
      remain in tools/graphcli/labpaths)
- [ ] GoogleAuthentication → src/
- [ ] Async HTTP + retry/backoff + typed errors (blocking HTTP now shared
      via `src/net/blockinghttp.{h,cpp}` — Kalburator::Net)
- [~] CLIs re-pointed; duplication deleted (graphcli done — graphauth/
      graphclient removed, links Kalburator::Sync); googlecli pending
- [ ] Mock server Authorization-header recording + pin (test gap)

## Next

Design pass first (P0.a) before any file moves.
