# P0 — Transport library-ization: status

**Status:** in progress (opened 2026-08-25)

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

- [ ] Design pass: reconcile graphapiclient vs GraphCLI client; API shape
- [ ] GraphAuthentication → src/
- [ ] GoogleAuthentication → src/
- [ ] Async HTTP + retry/backoff + typed errors
- [ ] CLIs re-pointed; duplication deleted; suites green

## Next

Design pass first (P0.a) before any file moves.
