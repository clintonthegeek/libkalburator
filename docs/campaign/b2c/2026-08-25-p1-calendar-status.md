# P1 — Calendar backends to production: status

**Status:** in progress (opened 2026-08-25)

Goal (proposal §4 P1): `GoogleCalendarBackend` (new) +
`MSGraphCalendarBackend` hardening; live checkpoints both directions.

## Design decisions (pinned 2026-08-25, from the P1.a exploration)

- **File placement:** `src/google/googleapiclient.{h,cpp}` (envelope-only
  async transport mirroring GraphApiClient) + `src/calendar/googlecalendarbackend.{h,cpp}`;
  test bed `tests/google/` (`mockgoogleserver`, `tst_google_api_client`,
  `tst_google_calendar_backend`) wired like `tests/graph/`.
- **BackendRecord.id = server event id** (updates/deletes address it);
  iCalUID rides inside the wire bytes and remains the canonical anchor
  (promote derives uid from it). addIdAlias still bridged on creates.
- **Fetch:** syncToken walk per collection — paginate `pageToken` until
  absent; `status=="cancelled"` ⇒ tombstone (cache remove); on HTTP 410
  Gone clear token+cache and redo ONE initial full listing (O42 pattern);
  report FULL merged set every time (engine diff contract). No
  `singleEvents=true` (masters + verbatim recurrence[] required by edge).
  Token persisted keyed to the EXACT query template (Google invalidates
  tokens on param changes).
- **Writes:** strip top-level `created`/`updated` before POST (O67(b)(1)
  400 rejection — demote emits them); PATCH in place, never re-create;
  DELETE accepts 200/204/410-as-success (idempotent semantics).
- **Registration scope:** P1 ships backend+transport+tests only;
  provider/contribution is P4 (MSGraph has no provider either yet).

## What exists now

- Design-complete exploration of SyncBackend/SyncBackendBase contract,
  MSGraphCalendarBackend architecture, registration ladder, and mock-server
  mechanics (2026-08-25 session log).
- Transport/auth from P0; google-event edge stages + loss profile landed
  (EEE Phase 2); sanitized live fixtures under tests/fixtures/vendor/google/.

## Checklist

- [x] P1.b: `GoogleApiClient` + `MockGoogleServer` + `tst_google_api_client`
      (landed 2026-08-25 — envelope-only async transport mirroring
      GraphApiClient; typed `GoogleError` with reason discriminator +
      isGone(); pageToken aggregation with nextSyncToken surfacing;
      mock fakes sync-token/410 semantics + O67 insert rejection,
      iCalUID honoring, organizer rewrite; 5 slots green)
- [x] P1.c+d: `GoogleCalendarBackend`
      (`src/calendar/googlecalendarbackend.{h,cpp}`; landed 2026-08-25) —
      calendarList discovery w/ accessRole→writable mapping; syncToken
      incremental fetch (full merged set per engine contract);
      status:"cancelled" tombstones; 410 Gone one-shot self-heal (O42);
      atomic persisted state resume across instances; O67 writes
      (created/updated stripped at POST seam, client iCalUID preserved,
      minted-transport-id alias bridging via addIdAlias, PATCH in place,
      410-delete-as-success). 9 slots green in tst_google_calendar_backend.
- [ ] P1.c: `GoogleCalendarBackend` reads (discovery, syncToken fetch,
      tombstones, 410 resync, persistence resume)
- [ ] P1.d: writes (strip rules, PATCH-in-place, idempotent delete) + pins
- [x] P1.e: MSGraphCalendarBackend hardening audit (2026-08-25):
      PATCH-in-place ✓ structural; post-create id-alias bridging ✓
      (addIdAlias on create); body→HTML/Teams-provisioning normalization ✓
      owned by edge stages per loss profile; FIXED — deltaStep now uses
      getWithRetry (delta walks were the only GETs without transient
      retries). Remaining MS item = live delta verification → P1.f.
- [ ] P1.f: live checkpoints both directions (g-roundtrip / ms-roundtrip
      protocol; verdicts into wire-notes + FINDINGS)

## Open risks carried

- syncToken expiry/param-sensitivity → token persisted with exact query
  template; any param change forces documented full resync.
- contentHash instability across server read-back rewrites (attendee
  lowercasing etc.) — same tolerance as MS backend via baseline diffing.
- Rate limits (403 rateLimitExceeded) — defer to maxConcurrentOperations()
  override if live drills demand it.
