# VTODO-parity campaign STATUS

Per `docs/2026-08-25-vtodo-parity-handoff-response.md` (acceptance of
PlanStan's W1–W8 handoff,
`PlanStan/docs/handoffs/2026-08-25-libkalburator-vtodo-parity-handoff.md`,
audit in `PlanStan/docs/audits/2026-08-25-vtodo-parity/`). This file is
the live execution tracker; the response doc holds decisions + receipts.

**Last updated:** 2026-08-29 (CLOSED 2026-08-28 with VP.a–VP.f all DONE; successor campaign opened — see below)

> **SUCCESSOR: the incidence-parity campaign** —
> `docs/campaign/incidence-parity/PLAN.md` + `STATUS.md`, opened
> 2026-08-29. This campaign was correctly scoped to todo, but two of its
> facts only became visible from outside it:
>
> 1. **The calendar domain shares this campaign's VTODO emitter**
>    (`src/calendar/icalcanonstages.cpp:56,:83` call
>    `Kalburator::Todo::todoFieldsToCanon()` /
>    `canonObjectToVtodoBytes()`). So W3/W4/O74's new keys are already
>    emitted into `{calendar,canon}`, where the catalogue declares none of
>    them — differ blind, and `CanonJsonMerger` silently takes the
>    target's value. Filed as **O78**; the successor's IP.1–IP.3 close it.
> 2. **VTODO is still the poorest-covered incidence kind**, poorer than
>    VJOURNAL: no SEQUENCE/CLASS/URL/ORGANIZER/ATTENDEE/ATTACH/COLOR, none
>    of it declared in a loss profile (**O83**). The forward direction —
>    what VEVENT has that VTODO lacks — was never in this campaign's
>    charter.
>
> The VEVENT twins this campaign flagged but did not fix are now numbered:
> **O79** (alarm trigger-form corruption, the W5 twin — plus three more
> call sites), **O81** (W6.2 twin), **O82** (the RANGE=THISANDFUTURE twin
> flagged in VP.e below). **O80** is O74's own predicted follow-through
> into calendar/contacts.

## Where we stand

| Phase | Item | State |
|---|---|---|
| — | (prereq) B2C P3 vendor todo backends + kind-demux | tracked in `docs/campaign/b2c/STATUS.md` — lands FIRST (W1/W2 test legs) |
| VP.a | **W8** capabilities API (`CalendarCapabilities`, discovery extensions, static per-backend reports, DiscoveredCalendar exposure) | **DONE 2026-08-26** — public header `src/sync/calendarcapabilities.h`; static reports in `CapabilityReports` + `capabilitiesFromDiscovery()`; discovery gains `<prodid>` extraction (recursive local-name match; falls back to known-product sniff over body + HTTP Server header), `producerId` + `supportsSyncCollection` on `PerCalendarCapabilities` (additive JSON, round-trip pinned); supported-report-set requested+parsed from the depth-1 multistat; DiscoveredCalendar exposure = metadata-backed typed pair (`capabilities()`/`setCapabilities()`, key `"capabilities"`, non-breaking) populated in the 4 vendor backends + RemoteCalendarBackend derivation; suite `tst_calendar_capabilities` (19 slots). Value corrections vs first-pass spec: googleCalendar recurrenceExceptions TRUE + unknown XOnly; msGraphCalendar recurrenceExceptions FALSE (v1 writes flat events+masters only, O61(e)); localBlob/calDAV alarms Full. Legacy dead `struct CalendarCapabilities` removed from backendcapabilities.{h,cpp} (name now owned by the W8 contract). |
| VP.b | **W2** per-instance completion rep + BaselineStore transactions + Google/MSToDo producer mappings | **DONE 2026-08-26** (`7403509`) — exception-create href distinct from master (`<uid>-<stamp>.ics`, was clobbering master); `BaselineStore::transaction(fn)` API + engine persist loop wraps atomically. 12 slots. Return receipt `2026-08-26-w2-return-receipt.md`. **Correction** vs the response doc: Google Tasks has NO extension point (O66(c)) — cannot carry the master EXDATE; MS To-Do carries it via nav-POST x-canon-recurrence (already Reversible). Consumer note delivered to PlanStan (`e1856650`): ConflictInfo ids may now be composite; decompose before display. |
| VP.c | **W1** composite record identity (`uid\x01recurrenceId`) for blob pipeline + contract doc + matrices (needs P3) | **DONE 2026-08-26** — step-1a library foundation (recordidentity.h, vtodo canon recurrenceId/recurrenceRange, scanner recurrenceIdUtc selector); **step-1b RemoteCalendarBackend blob-view wiring** (records minted via `composeRecordIdentity(uid, recurrenceId)` at every incidence-parse site, composite-keyed `m_lastRawIcsByUid`/`m_uidToUrl`, decompose-at-seam for resolveItemUrl/findOwningCalendar/applyRecords+createRecord URL guesses/loadRecord with graceful bare fallback; FakeCalDavServer store refactored from UID-keyed to RESOURCE-FILE-NAME-keyed; tst_remotecalendarbackend_blob_view +5 slots); **step-1c SubscriptionBackend + LocalBackend**. SubscriptionBackend: `subscriptionBlobRecord` now mints via `composeRecordIdentity(uid, hasRecurrenceId() ? recurrenceId() : invalid)` — a feed with a master + detached exception block (separate VTODO/VEVENT sharing one UID) yields TWO records (bare uid + composite, both bytes preserved); `loadRecord` decomposes (composite id → the exception, bare uid → the master, graceful master fallback when a composite id's block was dropped from the feed); write/delete seams stay rejected no-ops (read-only — a composite id never becomes a path). LocalBackend DECISION: do NOT compound record ids at the file level (record id == filename minus `.ics` is a bijection to ONE file path; compounding would break id→filename). Audit found NO truncation — `recordFromBytes` already stores the FULL file bytes (`rec.data = bytes`), so a single `.ics` parsing to master + co-located exception already keeps the RECURRENCE-ID block verbatim. No code change; pin test added. Follow-ups (recon): DecSyncBackend / AkonadiBackend / OrgBackend / GenericSqliteBackend not compounded at this stage. **STEP-2/3 (written contract + matrices) DONE same day** — binding contract `docs/campaign/vtodo-parity/2026-08-26-w1-detached-exceptions-contract.md` (§1 keying, §2 differ treatment of master+exception pairs, §3 delete semantics, §4 non-supporting-peer flatten strategy, §7 full matrix create/edit/delete/reabsorb × caldav/subscription/local/org/google/ms); return receipt `2026-08-26-w1-return-receipt.md`. Two new CalDAV pins: `detachedException_reabsorb_surfacesMasterOnly` + `detachedException_masterDelete_removesOnlyMasterHref` (+ `FakeCalDavServer::removeEventAt`). Engine-level uid-family propagation/cascade remain SPECIFIED-not-executed (§5 of contract doc; per-record engine behavior unchanged). |
| VP.d | **W4** completion-anchor canon key (catalogued) + CalDAV derived-RRULE write-out + differ non-conflict treatment | **DONE 2026-08-27** — catalogued `completionAnchor` key (`todocanonproperties.cpp:47`); generic `X-ORG-REPEATER` custom-prop promote seam (`vtodocanonfields.cpp`, new block after recurrenceId/recurrenceRange) + derived-RRULE demote seam anchored at `completed` (explicit DTSTART emitted only when canon carries no competing explicit `start` — declared corner case, tested); loss profiles declared on all three todo edges (vtodo Reversible, google-task Dropped, ms-todotask auto-carry Reversible); differ non-conflict pinned (2 new slots in `tst_canonjson_diff_merge`); matrix regenerated + byte-pin green same commit (O63). **org-io wiring DEFERRED, not landed**: `KALBURATOR_HAVE_ORG_IO=ON` verified NOT buildable standalone in this repo (actually attempted — fails at moc time, `orgfilemanager.h: No such file`, since no host project supplies the `planstan-org-io` target here); TODO left at the promote seam for whoever next has an org-io-enabled build (e.g. inside PlanStan). Return receipt: `2026-08-27-w4-return-receipt.md`. Full suite green except the 4 known environmental Radicale slots. |
| VP.e | **W3** series-split mechanics + split-association carrier | **DONE 2026-08-28** — correctness fix: `canonObjectToVtodoBytes` demote (`vtodocanonfields.cpp`) now unconditionally refuses to re-emit RANGE=THISANDFUTURE (write-hostile on real servers); the W1-era pinned test that asserted the opposite (`vtodoRoundTripPreservesThisAndFutureRange`) rewritten to `vtodoDemoteNeverEmitsThisAndFutureRange`; `recurrenceRange` → Degraded loss row. New catalogued key `seriesSplitOf` (old-master uid) rides `X-CANON-SERIES-SPLIT-OF` on vtodo/CalDAV (Reversible), auto-carries as `x-canon-series-split-of` on MS To-Do (Reversible, zero handler code), Dropped on Google Tasks (no extension point). New pure host-invoked helper `Kalburator::Todo::splitSeriesAtInstant()` (`src/todo/todoseriessplitter.{h,cpp}`) computes {tightened old master, fresh new master, rebased exceptions} from canon JSON only — text-level RRULE UNTIL rewrite (never loosens past the original bound), deterministic new-master uid (idempotent retry), COUNT-bounded RRULE fails loud (v1 doesn't recompute COUNT), exception rebase is new-identity-not-rename per the W1 contract. NOT wired into SyncEngine/differ/any backend — host-invoked only, per Open decisions 1/9. `parseRruleParts()` exported via `recurrencepatternconverter.h` (was file-local). Contract doc `2026-08-27-w3-series-split-contract.md` (carrier + demote guarantee + helper contract + host realization sequence + engine/transport atomicity gap declared SPECIFIED-not-executed). **VEVENT-side twin bug flagged, NOT fixed** (out of vtodo-parity's todo-only scope): `eventcanonfields.cpp:594-596` still unconditionally re-emits RANGE=THISANDFUTURE. Matrix regenerated + byte-pin green same commit (O63). Return receipt: `2026-08-27-w3-return-receipt.md`. Full suite green (214 tests) except the 4 known environmental Radicale slots. |
| VP.f | **W5** alarm shape extension (abs trigger/RELATED/REPEAT/DURATION) + **W6.2** malformed-date coercion + **W7** passthrough round-trip tests + **O74** differ fix | **DONE 2026-08-28** — closes the whole vtodo-parity campaign's currently-scoped W-item list (VP.a–VP.f all DONE). W6.2: rules (a)/(b) implemented against `todo->dtStart()`/`dtDue()` directly (KCalendarCore probe confirmed the DATE/DATE-TIME mismatch survives independent parsing — no raw-bytes helper needed); rule (a) deliberately follows the binding response-doc text ("DUE's type always wins"), NOT tasks.org's actual symmetric rule (flagged divergence, both directions tested); rule (c) confirmed zero-code no-op by probe (KCalendarCore already drops DURATION-without-DTSTART); bonus DATE round-trip fix is one `setAllDay(true)` call in demote (probe showed the writer keys off the incidence `allDay()` flag alone, not the QDateTime's own shape — simpler than recon anticipated). W5: additive `alarms[]` keys (`at`/`related`/`repeatCount`/`repeatIntervalSecs`, field stays named `offset` not `offsetSecs`); real bug fixed — promote no longer corrupts absolute-trigger/END-related alarms to bogus `offset:0`; REPEAT/DURATION probe found the recon's proposed pairing check doesn't work (`snoozeTime()` defaults to a nonzero 5s class constant, not 0) — promote emits the pair whenever `repeatCount()>0` instead, demote still only synthesizes when both keys present; MS-leg alarm shape unified to `{type,at}` (was a non-conforming `{reminder:{...}}` sub-shape that silently mangled MS-sourced alarms demoted to VTODO — real cross-vendor bug, fixed). O74: new catalogued `providerExtrasDigest` + domain-neutral `CanonEnvelope::canonicalDigest()` helper (SHA256 hex; Qt6's `QJsonDocument::toJson()` already key-sorts at every nesting level, so no custom recursive sort was needed, simpler than the recon's design sketch); MS/Google promote sites filter known-volatile bookkeeping before hashing (Google: `etag`; MS: `@odata.etag`/`lastModifiedDateTime`/`@odata.context`, confirmed against a real captured Graph todoTask sample) so the digest doesn't become spuriously always-dirty; `providerExtrasDigest` → Dropped on all three loss profiles; differ pin added; matrix regenerated (O63). W7: new VALARM round-trip tests (the biggest prior gap — zero coverage), one generic-unknown-X-prop passthrough test, one new CalDAV byte-verbatim-VTODO test (grepped first, confirmed missing), VTIMEZONE coverage confirmed pre-existing. Binding contract doc: `2026-08-28-w7-passthrough-contract.md` (truth table + org warning sentence + O74 note). Return receipt: `2026-08-28-vpf-return-receipt.md` (also documents the two KCalendarCore probe outcomes and the exact volatile-key filter lists). 17 new test slots across 5 files. FINDINGS.md O74 flipped OPEN→RESOLVED. |

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
- **W1 step-1 LANDED (2026-08-26)** — composite exception identity:
  - step-1a (`6a166ac`): `src/sync/recordidentity.h` (compose/decompose/
    isException, `uid\x01UTC-ISO`); vtodo canon gains `recurrenceId`/
    `recurrenceRange` (mirrors event path); scanner role selector
    (master-preference default intact). 15 slots.
  - step-1b (`c5592e0`): RemoteCalendarBackend composites minting at
    all parse loops + decompose-at-seam; maps keyed by composite;
    `loadRecord` accepts bare+composite. 5 slots.
  - step-1c (`9fe2be5`): SubscriptionBackend compounded; LocalBackend
    decision = NO compounding (id↔filename bijection), full-file-bytes
    pin instead. 4 slots.
- **VP.b / W2 LANDED (2026-08-26)** — per-instance completion:
  - W2 engine fixes (`7403509`): exception-create href distinct from
    master (`<uid>-<stamp>.ics`, was clobbering master); BaselineStore
    `transaction(fn)` API + engine persist loop wraps atomically. 12
    slots. (Subagent stalled ~1h before verify; orchestrator finished
    verification + fixed QVERIFY-in-lambda compile errors in tests.)
  - Receipt: `2026-08-26-w2-return-receipt.md`. **Correction** to the
    response doc: Google Tasks has NO extension point (O66(c)) — its
    leg CANNOT carry the master EXDATE; MS To-Do carries it via
    nav-POST x-canon-recurrence (already Reversible).
  - Consumer note delivered to PlanStan (`e1856650` in their repo):
    ConflictInfo ids may now be composite; decompose before display.
- **NEXT:** W4 (VP.d) IMPLEMENTATION — see `2026-08-26-w4-recon-handoff.md`
  (recon done; code map + open decisions there) → W3 series-split (VP.e) → W5+W6.2+W7 (VP.f).
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
- 2026-08-26: **VP.c-step-1c landed** (the remaining two bare-uid blob
  backends). **SubscriptionBackend compounded**: `subscriptionBlobRecord`
  mints via `composeRecordIdentity(uid, hasRecurrenceId() ? recurrenceId() :
  invalid)` — a feed carrying a master + detached exception block (same
  UID, separate VTODO/VEVENT) now yields TWO records (bare master uid +
  composite exception id, both blocks' bytes preserved). `loadRecord` is
  the only id-addressing seam (writes are rejected no-ops, so no path/
  href seam exists); it decomposes — composite id serves the exception,
  bare uid serves the master, and a composite id whose block the feed
  dropped falls back to the bare master. **LocalBackend decision: NO
  compounding** — record id == filename minus `.ics` maps to ONE file
  path, so compounding would break id→filename. Audit found the 
  compensating guarantee ALREADY holds: `recordFromBytes` stores the FULL
  file bytes (`rec.data = bytes`), so a `.ics` parsing to master +
  co-located exception keeps the RECURRENCE-ID block verbatim — no code
  change, pin test added instead. tst_subscriptionbackend_blob_view +3
  slots (14 total), tst_localbackend_blob_view +1 slot (13 total); all
  prior slots UNTOUCHED and green. Full suite green: 206/206
  non-environmental (excl. the 4 known Radicale slots). Follow-ups per
  recon (NOT this stage): DecSyncBackend / AkonadiBackend / OrgBackend /
  GenericSqliteBackend.
- 2026-08-26: **VP.c (W1) COMPLETE** — written contract + full test matrix.
  Binding contract: `2026-08-26-w1-detached-exceptions-contract.md`
  (§1 keying, §2 differ treatment of master+exception pairs — independent
  diffs joined by shared uid for propagation, §3 delete semantics —
  exception-only delete = EXDATE removal + tombstone; master delete =
  cascade tombstone of all keyed exceptions, §4 non-supporting-peer
  flatten strategy shared with W2, §7 full matrix create/edit/delete/
  reabsorb × caldav/subscription/local/org/google/ms). Return receipt:
  `2026-08-26-w1-return-receipt.md`. Two new CalDAV pins landed:
  `detachedException_reabsorb_surfacesMasterOnly` (server-side drop of the
  exception resource → 1 master record; stale composite loadRecord
  degrades to master) and `detachedException_masterDelete_removesOnlyMasterHref`
  (bare-uid delete removes ONLY the master href; exception resource
  survives; refetch = 1 composite record) — supporting seam
  `FakeCalDavServer::removeEventAt(href, fileName)` added.
  tst_remotecalendarbackend_blob_view now 27 slots. Engine-level
  uid-family propagation/cascade remain SPECIFIED-not-executed (per-record
  engine behavior unchanged; contract doc §5). Full suite green except the
  4 known environmental Radicale slots.
- 2026-08-26: **VP.c (W1) CLOSED / W4 recon started** — committed
  `7585152` (contract doc + matrix + 2 CalDAV pins; full suite green except
  the 4 known Radicale slots). W4 (VP.d) exploration done via subagent and
  persisted in `2026-08-26-w4-recon-handoff.md` (code map + open decisions:
  org-leg promote seam, MS carrier ruling, unit alphabet, anchor source).
  W4 implementation is the next task; nothing coded yet for W4.
- 2026-08-27: **VP.d (W4) LANDED** — catalogued `completionAnchor`
  canon key (Json, `todocanonproperties.cpp:47`); generic `X-ORG-REPEATER`
  custom-prop promote seam in `vtodocanonfields.cpp` (org-io regex mirror,
  `.+`/`++` → Restart/CatchUp, bare `+` out of scope); derived-RRULE
  demote seam appended to the pre-existing recurrence-injection point,
  anchored at `completed` via an explicit DTSTART only when canon has no
  competing explicit `start` (declared corner case when it does, pinned
  by test); loss profiles on all three todo edges (canon→vtodo
  Reversible, canon→google-task Dropped — no recurrence field at all,
  canon→ms-todotask auto-carry Reversible via the existing
  unhandled-canon-prop open-extension loop, `x-canon-completion-anchor`);
  differ non-conflict pinned directly (2 new `tst_canonjson_diff_merge`
  slots) — falls out for free once the key is catalogued, no differ code
  changed. Matrix regenerated + `tst_gm_pipeline_convergence` byte-pin
  green in the same commit (O63). 14 new test slots total across
  `tst_todo_canon_roundtrip` (+10), `tst_canonjson_diff_merge` (+2),
  `tst_google_task_canon_edge` (+1), `tst_ms_todotask_canon_edge` (+1).
  **Org-leg (OrgBackend) wiring DEFERRED**: actually attempted
  `cmake -DKALBURATOR_HAVE_ORG_IO=ON` standalone in this repo and
  confirmed it does NOT build (fails at moc time — no host project
  supplies the `planstan-org-io` target outside a build like PlanStan's);
  a TODO is left at the promote seam naming exactly what remains
  (inject `X-ORG-REPEATER` from `OrgBackend::m_roundtripData` at fetch
  time, respecting the incidence-purity invariant pinned by
  `tst_orgbackend_external.cpp:611-615,631-634`) for whoever next has an
  org-io-enabled build. Return receipt: `2026-08-27-w4-return-receipt.md`.
  Full suite green except the 4 known environmental Radicale slots
  (213 total tests, 209 passing + the 4 known failures).
- 2026-08-27: **VP.e (W3) recon done**, persisted in
  `2026-08-27-w3-recon-handoff.md` (code map + open decisions; no code
  written yet). Headline finding: a W1-era pinned test
  (`vtodoRoundTripPreservesThisAndFutureRange`,
  `tests/todo/tst_todo_canon_roundtrip.cpp`) currently asserts the
  OPPOSITE of the binding W3 spec (demote must never emit
  RANGE=THISANDFUTURE) — rewriting it is part of the fix, not incidental
  cleanup. W3 implementation is the next task.
- 2026-08-28: **VP.e (W3) LANDED** — all nine recon open decisions
  implemented as-given. Correctness fix: `vtodocanonfields.cpp` demote
  unconditionally refuses RANGE=THISANDFUTURE re-emission;
  `vtodoRoundTripPreservesThisAndFutureRange` rewritten to
  `vtodoDemoteNeverEmitsThisAndFutureRange` (recon's exact recommended
  assertions); `recurrenceRange` → Degraded loss row added. New
  catalogued `seriesSplitOf` key + carriers: explicit
  `X-CANON-SERIES-SPLIT-OF` custom-prop on vtodo/CalDAV (Reversible),
  zero-code auto-carry as `x-canon-series-split-of` on MS To-Do
  (Reversible), Dropped on Google Tasks. New pure library helper
  `Kalburator::Todo::splitSeriesAtInstant()`
  (`src/todo/todoseriessplitter.{h,cpp}`) — text-level RRULE UNTIL
  tightening (never loosens past the original bound), deterministic
  idempotent new-master uid, COUNT-bounded RRULE fails loud, exception
  rebase treated as new-identity-not-rename (no `composeRecordIdentity()`
  call inside the helper, consistent with the W1 contract). NOT wired
  into SyncEngine/differ/any backend (host-invoked pure function only).
  `parseRruleParts()` exported from `recurrencepatternconverter.cpp` via
  its header (was anonymous-namespace-local) to avoid a second RRULE
  KEY=VALUE parser. Contract doc
  `2026-08-27-w3-series-split-contract.md` written (mirrors the W1
  contract §5 framing for the declared engine/transport atomicity gap).
  **VEVENT-side twin bug flagged, not fixed**: `eventcanonfields.cpp:594-596`
  has the identical write-hostility bug, unfixed, out of vtodo-parity's
  todo-only scope — flagged in the contract doc §2 and the return receipt
  for a future event-focused pass. 18 new test slots across
  `tst_todo_canon_roundtrip` (+2), new `tst_todo_series_split` (9, new
  binary), `tst_ms_todotask_canon_edge` (extended existing slots, no
  count change), `tst_google_task_canon_edge` (same), `tst_canonjson_diff_merge`
  (+1). Matrix regenerated + `tst_gm_pipeline_convergence` byte-pin green
  same commit (O63). Return receipt: `2026-08-27-w3-return-receipt.md`.
  Full suite: 214 tests total (213 pre-W3 + 1 new binary), 210 passing +
  the 4 known environmental Radicale slots (no regressions; grepped for
  other test dependents on the old RANGE=THISANDFUTURE-emitting behavior
  before rewriting — none found). vtodo-parity campaign remaining order:
  W5 (alarm extension) + W6.2 (date coercion) + W7 (passthrough tests)
  (VP.f) — all that's left.
- 2026-08-28: **VP.f (W5+W6.2+W7+O74) recon done**, persisted in
  `2026-08-28-vpf-recon-handoff.md` (code map + open decisions per
  sub-item; no code written yet). Headline findings: W6.2 rules (a)/(b)
  may need a raw-bytes VALUE=DATE probe depending on whether KCalendarCore
  collapses DTSTART/DUE date-only-ness via its single `allDay()` flag
  (unresolved — verify before implementing); O74's extras digest must be
  filtered on the MS/Google legs (Google's `etag` and MS's
  `lastModifiedDateTime`-equivalent bump on every edit and would make the
  new differ signal spuriously always-dirty if hashed unfiltered — Google
  leg evidenced directly by an existing code comment naming `etag` in the
  unfiltered stash); the MS-leg alarm shape (`{reminder:{...}}`) does not
  match the vtodo-leg shape (`{type,offset,text}`) and demoting an
  MS-sourced alarm to VTODO today silently produces a zero-offset
  Invalid-type VALARM — flagged as a real bug, recommended (not
  mandatory) to fold into W5. VP.f implementation is the next task;
  nothing coded yet.
- 2026-08-28: **VP.f (W5+W6.2+W7+O74) LANDED — vtodo-parity campaign's
  currently-scoped W-item list now fully DONE (VP.a–VP.f).** Two
  KCalendarCore probes run before coding (per the recon's own
  instruction): (1) the DTSTART/DUE DATE/DATE-TIME mismatch survives
  independent parsing (branch 1 of Open Decision 5) — W6.2 rules (a)/(b)
  implemented directly against `todo->dtStart()`/`dtDue()`, no raw-bytes
  helper needed; (2) `KCalendarCore::Alarm::snoozeTime()` defaults to a
  nonzero 5-second class constant even with no DURATION property present
  at all, so the recon's proposed `snoozeTime() != 0` pairing check
  (W5 Open Decision 3) does not work as written — promote instead emits
  the REPEAT/DURATION pair whenever `repeatCount() > 0`, documented as a
  deliberate deviation from the recon's exact wording. W6.2 rule (a)
  follows the binding response-doc text ("DUE's type always wins")
  rather than tasks.org's actual symmetric rule — explicit, tested,
  documented divergence (Open Decision 4). W6.2 bonus DATE round-trip fix
  turned out simpler than the recon anticipated: KCalendarCore's writer
  keys VALUE=DATE off the incidence `allDay()` flag alone (probe-verified,
  call order irrelevant), so the fix is one `setAllDay(true)` call in
  demote — `jsonToDateTime` itself needed no change. W5 fixes a real
  pre-existing bug (promote unconditionally read `startOffset()`,
  corrupting absolute-trigger/END-related alarms to bogus `offset:0`) and
  folds in the MS-leg alarm-shape unification (Open Decision 2, recon's
  "yes" recommendation) — MS-sourced alarms previously demoted to a bogus
  zero-offset Invalid-type VALARM on the vtodo/CalDAV leg, a real
  cross-vendor bug, now fixed via the new `{type, "at"}` shape shared with
  vtodo. O74: new catalogued `providerExtrasDigest` + domain-neutral
  `CanonEnvelope::canonicalDigest()` helper — simpler than the recon's
  design sketch since Qt6's `QJsonDocument::toJson()` already key-sorts
  `QJsonObject` at every nesting level (probe-verified), so no custom
  recursive sort was needed. MS/Google promote sites filter volatile
  bookkeeping before hashing (Google: `etag`; MS: `@odata.etag` +
  `lastModifiedDateTime` + `@odata.context`, the last confirmed volatile
  by diffing four real captured samples of the same record fetched via
  different request shapes — `createdDateTime` deliberately NOT filtered,
  it doesn't change on edits). `providerExtrasDigest` → Dropped on all
  three loss profiles; differ pin added; matrix regenerated (O63),
  `tst_gm_pipeline_convergence` byte-pin green. W7: new VALARM round-trip
  tests (zero prior coverage, the campaign's biggest passthrough gap), one
  generic-unknown-X-prop passthrough test, one new CalDAV byte-verbatim-
  VTODO test (grepped first — confirmed no prior test asserted byte
  content for VTODO fetch specifically). Binding contract doc:
  `2026-08-28-w7-passthrough-contract.md`. Return receipt:
  `2026-08-28-vpf-return-receipt.md` (documents both probe outcomes + the
  exact volatile-key filter lists in full). FINDINGS.md O74 flipped
  OPEN→RESOLVED in the same commit as its fix. 17 new test slots across
  `tst_todo_canon_roundtrip` (+12), `tst_ms_todotask_canon_edge` (+2),
  `tst_google_task_canon_edge` (+1), `tst_canonjson_diff_merge` (+1),
  `tst_remotecalendarbackend_blob_view` (+1). Full suite: 214 tests, 210
  passed + exactly the 4 known pre-existing environmental Radicale/KDAV
  failures (`tst_backend_signals`, `tst_backend_thread_relocation`,
  `tst_backend_reentrancy_pin`, `tst_remotecalendarbackend`) — no new
  failures, no regressions (`ctest --test-dir build --output-on-failure
  -j4`). One unrelated environmental flake hit mid-session and resolved:
  `tst_etagcache_seed`'s binary was a truncated/corrupted artifact from an
  earlier build in this session (file(1) reported "data", not ELF) —
  rebuilding that one target fixed it; unrelated to any VP.f code change,
  confirmed by running it standalone before and after.
