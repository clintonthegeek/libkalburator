# VTODO-parity campaign STATUS

Per `docs/2026-08-25-vtodo-parity-handoff-response.md` (acceptance of
PlanStan's W1–W8 handoff,
`PlanStan/docs/handoffs/2026-08-25-libkalburator-vtodo-parity-handoff.md`,
audit in `PlanStan/docs/audits/2026-08-25-vtodo-parity/`). This file is
the live execution tracker; the response doc holds decisions + receipts.

**Last updated:** 2026-08-25 (campaign opened; nothing implemented)

## Where we stand

| Phase | Item | State |
|---|---|---|
| — | (prereq) B2C P3 vendor todo backends + kind-demux | tracked in `docs/campaign/b2c/STATUS.md` — lands FIRST (W1/W2 test legs) |
| VP.a | **W8** capabilities API (`CalendarCapabilities`, discovery extensions, static per-backend reports, DiscoveredCalendar exposure) | not started — DO FIRST per handoff |
| VP.b | **W2** per-instance completion rep + BaselineStore transactions + Google/MSToDo producer mappings | not started |
| VP.c | **W1** composite record identity (`uid\x01recurrenceId`) for blob pipeline + contract doc + matrices (needs P3) | not started |
| VP.d | **W4** completion-anchor canon key (catalogued) + CalDAV derived-RRULE write-out + differ non-conflict treatment | not started |
| VP.e | **W3** series-split mechanics + split-association carrier | not started |
| VP.f | **W5** alarm shape extension (abs trigger/RELATED/REPEAT/DURATION) + **W6.2** malformed-date coercion + **W7** passthrough round-trip tests | not started |

## Key recon findings pinned 2026-08-25 (evidence for the response doc)

- **Blob/canon pipeline keys by UID alone** — master + detached exception
  collide; compound `uid+'\0'+recurrenceId` keying exists only on the
  SyncRecord/incidence path (`src/diff/syncdiff.cpp:16`). W1's real work.
- **providerExtras invisible to the todo differ** — CanonJsonDiffer runs
  on catalogued ids only (`tododomaindefinition.cpp:27`); X-prop-only
  changes never dirty a diff. Recorded as FINDINGS **O74**.
- **VALARM already first-class** in todo canon ({type, offsetSecs, text};
  MS maps single reminder ⇄ alarms[0]; Google Dropped) — W5 is an
  extension, not a build.
- **OrgBackend re-serializes** through a fixed headline/drawer mapping —
  unknown X- props do NOT survive an org round trip (Q3 answer: no).
- **BaselineStore has NO multi-record transactions** (single-statement
  autocommits only) — Q1 answer; transaction API added in VP.b.
- Capability discovery today = RFC 4791 component-set/privileges/color/
  maxResourceSize + serverProduct sniff; NO PRODID/report-set probing;
  richer caps stuck in provider layer, only supports*/writable/color on
  DiscoveredCalendar. W8 fills all of that.

## Session log

- 2026-08-25: campaign opened; handoff accepted with scoping edits
  (response doc); sequencing integrated after B2C P3; recon findings
  pinned above; O74 recorded.
