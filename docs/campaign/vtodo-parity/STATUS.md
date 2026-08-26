# VTODO-parity campaign STATUS

Per `docs/2026-08-25-vtodo-parity-handoff-response.md` (acceptance of
PlanStan's W1–W8 handoff,
`PlanStan/docs/handoffs/2026-08-25-libkalburator-vtodo-parity-handoff.md`,
audit in `PlanStan/docs/audits/2026-08-25-vtodo-parity/`). This file is
the live execution tracker; the response doc holds decisions + receipts.

**Last updated:** 2026-08-26 (VP.c-step-1b landed — RemoteCalendarBackend composite record identity wiring)

## Where we stand

| Phase | Item | State |
|---|---|---|
| — | (prereq) B2C P3 vendor todo backends + kind-demux | tracked in `docs/campaign/b2c/STATUS.md` — lands FIRST (W1/W2 test legs) |
| VP.a | **W8** capabilities API (`CalendarCapabilities`, discovery extensions, static per-backend reports, DiscoveredCalendar exposure) | **DONE 2026-08-26** — public header `src/sync/calendarcapabilities.h`; static reports in `CapabilityReports` + `capabilitiesFromDiscovery()`; discovery gains `<prodid>` extraction (recursive local-name match; falls back to known-product sniff over body + HTTP Server header), `producerId` + `supportsSyncCollection` on `PerCalendarCapabilities` (additive JSON, round-trip pinned); supported-report-set requested+parsed from the depth-1 multistat; DiscoveredCalendar exposure = metadata-backed typed pair (`capabilities()`/`setCapabilities()`, key `"capabilities"`, non-breaking) populated in the 4 vendor backends + RemoteCalendarBackend derivation; suite `tst_calendar_capabilities` (19 slots). Value corrections vs first-pass spec: googleCalendar recurrenceExceptions TRUE + unknown XOnly; msGraphCalendar recurrenceExceptions FALSE (v1 writes flat events+masters only, O61(e)); localBlob/calDAV alarms Full. Legacy dead `struct CalendarCapabilities` removed from backendcapabilities.{h,cpp} (name now owned by the W8 contract). |
| VP.b | **W2** per-instance completion rep + BaselineStore transactions + Google/MSToDo producer mappings | not started |
| VP.c | **W1** composite record identity (`uid\x01recurrenceId`) for blob pipeline + contract doc + matrices (needs P3) | **STEP-1b DONE 2026-08-26** — step-1a library foundation (recordidentity.h, vtodo canon recurrenceId/recurrenceRange, scanner recurrenceIdUtc selector) plus **RemoteCalendarBackend blob-view wiring**: records minted via `composeRecordIdentity(uid, recurrenceId)` at every incidence-parse site (serveCachedItems, fetchItems finished-hook, all-from-cache branch, multiget, loadRecords) and `blobRecordFromIcal` now composes internally; `m_lastRawIcsByUid`/`m_uidToUrl` keyed by composite id; seams decomposed at `resolveItemUrl` (full-id map lookup + bare-uid guess fallback + graceful master fallback for pre-composite cached state), `findOwningCalendar` pass 0, `applyRecords`/`createRecord` URL guesses, `loadRecord` (accepts BOTH bare uid and composite id, graceful bare fallback). Bare-uid surfaces UNCHANGED (incidence world): pushItems, startSync, launchStartSyncModify, removeItem, deleteItems. FakeCalDavServer store refactored from UID-keyed to RESOURCE-FILE-NAME-keyed so a master + detached exception can coexist as two hrefs sharing one UID (query/multiget/GET/PUT/DELETE all resource-href-aware; sync-collection report remains per-UID — exception deltas via sync-collection out of scope). tst_remotecalendarbackend_blob_view +5 slots (23 total) pin two-records-per-collection, exception-addressed PUT/DELETE to the exception href, id stability across writes, loadRecord both id forms; all prior slots UNTOUCHED and green. Known limitation carried: multi-block resources (master+overrides in ONE blob) get distinct composite ids but share a href (no block splitting); exception CREATE at a guessed `<uid>.ics` is not exercised. Full suite green except the 4 known environmental Radicale slots. Follow-ups: SubscriptionBackend (`subscriptionBlobRecord` bare uid, subscriptionbackend.cpp:250) and LocalBackend (`recordFromBytes` bare uid, localbackend.cpp:1136) compound naturally when this stage's pattern is promoted. |
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
- **SEQUENCING CORRECTION (2026-08-26):** VP.b (W2) and VP.c (W1) are
  SWAPPED relative to the response doc — W2's detached-instance
  representation CANNOT round-trip until composite record identity
  exists (the blob pipeline still keys by UID alone; master+exception
  collide — see recon finding above). New order: VP.c-step-1 (composite
  identity `uid\x01recurrenceId` in the blob pipeline) FIRST, then
  VP.b (W2 rep + BaselineStore transactions + producer mappings), then
  the rest of W1 (contract doc + matrices). PlanStan was told W8 → W2 →
  W1 in the receipt note; the receipt for W2 will state the dependency
  explicitly. No consumer impact (they consume receipts, not our internal
  order).
- 2026-08-26: **VP.a (W8) landed.** Files: `src/sync/calendarcapabilities.{h,cpp}`
  (+ CMake registration), `backendconfiguration.{h,cpp}` (additive
  producerId/supportsSyncCollection + JSON), `caldavcapabilitydiscovery.{h,cpp}`,
  `discoveredcalendar.h`, capability population in googlecalendar/
  msgraphcalendar/googletasks/graphtodotask/remotecalendar backends,
  `tests/sync/tst_calendar_capabilities.cpp` (+ FakeCalDavServer prodid/
  Server-header/sync-collection fixture knobs). Full suite green except the
  4 known environmental Radicale slots; tst_syncengine_unification flaked
  under parallel load, PASSED isolated. Return receipt:
  `2026-08-26-w8-return-receipt.md`. Four evidence-based capability-value
  corrections vs the original spec (receipt §contracts): googleCalendar
  exceptions TRUE / unknown XOnly; msGraphCalendar exceptions FALSE (v1
  read-only); localBlob+caldav alarms Full.
- 2026-08-26: **VP.c-step-1a landed** (sequencing correction above: VP.c
  runs before VP.b). Library-level detached-exception identity only —
  no backend/blob-pipeline wiring (that is a later step of VP.c).
  Canon JSON keys chosen: `recurrenceId` = `{"dateTime": <UTC ISO>}` and
  `recurrenceRange` = `"thisAndFuture"` — byte-for-byte the event path's
  shapes (calendarcanonproperties.cpp:43-44 mirrored in todocanonproperties).
  Scanner overload is additive (`recurrenceIdUtc` selector, default empty
  ⇒ master-preference unchanged); normalization honors TZID params and
  VALUE=DATE (→ UTC midnight), and needed an RFC-5545 BASIC-form parser
  because Qt's Qt::ISODate rejects `20260602T090000Z`. Matrix byte-pin
  unchanged; convergence gate green.
- 2026-08-26: **VP.c-step-1b landed** (the blob-pipeline wiring step 1a
  deferred). Composite minting in RemoteCalendarBackend's three
  incidence-parse loops + fetchItems finished-hook + loadRecords;
  composite-keyed `m_uidToUrl`/`m_lastRawIcsByUid`; decompose-at-seam for
  resolveItemUrl/findOwningCalendar/applyRecords+createRecord URL guesses/
  loadRecord (bare + composite, graceful). FakeCalDavServer store now keyed
  by resource file name (master + exception as two hrefs sharing a UID).
  tst_remotecalendarbackend_blob_view +5 slots. Full suite green (209/209
  non-environmental).
