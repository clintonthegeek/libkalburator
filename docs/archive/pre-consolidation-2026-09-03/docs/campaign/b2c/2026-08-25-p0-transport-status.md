# P0 — Transport library-ization: status

**Status:** CLOSED 2026-08-25 — all items landed; see close-out below.

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
- [x] GoogleAuthentication → src/ (`src/google/googleauth.{h,cpp}`;
      injectable auth/token endpoints for mock grants; injectable
      browser-launcher std::function replaces the library-side xdg-open)
- [x] Async HTTP + retry/backoff + typed errors — typed `GraphError` +
      async callback client pre-existing (GraphApiClient); P0.d added
      `src/net/backoff.h` (isTransientFailure/retryDelayMsecs, Retry-After
      honored, 30s cap) wired into GraphApiClient GETs via getWithRetry
      (default 2 attempts; writes never auto-retried). Blocking HTTP shared
      via `src/net/blockinghttp.{h,cpp}` (Kalburator::Net).
- [x] CLIs re-pointed; duplication deleted (graphauth/graphclient and
      googleauth/googleclient removed from tools/, both link Kalburator::Sync)
- [x] Mock server Authorization-header recording + pin
      (`RecordedRequest.authorizationHeader`;
      tst_graph_api_client::bearerTokenInjectedOnEveryRequest asserts Bearer
      on every request)

## P0 close-out

All checklist items landed 2026-08-25. Known deferrals (tracked, not lost):
- Device-code flow remains blocking/interactive — an async/QFuture variant
  is a P4 concern (consent UX), not a transport-semantics one.
- Google side has no async `GoogleApiClient` yet — lands with the first
  Google backend (P1) so its shape is driven by a real consumer.
- Token storage is file-based with owner-only perms; host keychain
  integration is a P4 decision.

## Next

P1 — calendar backends to production: GoogleCalendarBackend (new) +
MSGraphCalendarBackend live hardening (proposal §4 P1).
