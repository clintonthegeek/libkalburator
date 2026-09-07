# W7 — passthrough verification truth table + O74 differ fix: contract

**Delivered:** 2026-08-28 (implements W7/O74, part of VP.f; recon:
`2026-08-28-vpf-recon-handoff.md`)
**Consumes:** handoff §W7 (binding, `docs/2026-08-25-vtodo-parity-handoff-response.md`);
FINDINGS.md O74 (binding problem statement for the differ fix).
**Status:** BINDING. Mirrors the W1/W3 contract-doc precedent (small,
dedicated, quotable).

---

## 1. Finalized truth table (todo domain)

| Backend | X-props | VALARM | VTIMEZONE | Ordering/extras |
|---|---|---|---|---|
| LocalBlob | preserved verbatim | preserved | preserved | bytes verbatim |
| CalDAV (RemoteCalendarBackend) | preserved (server raw bytes preferred over re-serialization) | preserved | preserved | bytes verbatim |
| Org | **DROPPED** (fixed headline mapping; only OrgRoundtripData{keyword, descHash, repeater} survives) | dropped | n/a | prose kept only while description MD5 unchanged |
| Google Tasks | native keys stashed verbatim C→G; foreign extras (x-vtodo) do NOT ride; recurrence/alarms/priority etc. declared Dropped | dropped (declared) | n/a | — |
| MS To-Do | unmapped wire keys stashed + x-canon-* carrier (live-Reversible) | single reminder ⇄ alarms[0], unified `{type, at}` shape (W5) | n/a | carrier via nav POST (O73 upsert) |

This table is unchanged from the response doc's 2026-08-25 version except
that the MS To-Do VALARM cell now reads "unified `{type, at}` shape (W5)"
— see §3 below for why that phrase matters.

**Org warning sentence (consumer-visible, editor-worthy):** *org saves lose
unknown iCal properties* — any X-/custom iCal property not one of the
handful OrgBackend explicitly round-trips (keyword, description-hash-gated
prose, X-ORG-REPEATER) is silently dropped on an org save. This is not a
bug to fix in this campaign; it is the org format's structural limit
(fixed headline/drawer mapping), and PlanStan's editor UI should surface
this warning wherever a record with foreign X-props is about to be saved
through an org-backed calendar.

## 2. Round-trip test coverage per "preserved" cell

Only the two byte-preserving legs (LocalBlob, CalDAV) have "preserved"
cells requiring proof; the Org/Google/MS cells are Dropped/Simplified/
Reversible and already covered by their loss-profile pins + edge tests.

- **VALARM — LocalBlob + CalDAV:** landed alongside W5 in
  `tests/todo/tst_todo_canon_roundtrip.cpp`:
  `vtodoAlarmOffsetFormRoundTrips` (regression pin, pre-existing shape),
  `vtodoAlarmAbsoluteAtFormRoundTrips` (new `"at"` form + W5 bug-fix pin),
  `vtodoAlarmEndRelatedOffsetRoundTrips` (new `"related":"end"` form + W5
  bug-fix pin), `vtodoAlarmRepeatDurationPairRoundTrips` (new REPEAT/
  DURATION pair), `vtodoAlarmUnpairedRepeatIsNotSynthesized` (demote never
  manufactures an unpaired REPEAT/DURATION). LocalBlob's own passthrough is
  covered by the same canon-shape guarantee (no separate LocalBlob-specific
  alarm test — LocalBlob stores the full VCALENDAR bytes verbatim, per the
  W1 `coLocatedMasterAndException_preservesFullFileBytes` precedent, so any
  VALARM riding those bytes is preserved by construction).
- **X-props — LocalBlob + CalDAV:** `vtodoGenericUnknownXPropSurvivesRoundTrip`
  (`tst_todo_canon_roundtrip.cpp`) feeds a genuinely arbitrary/unknown
  custom property (`X-SOME-RANDOM-CLIENT-FIELD`, not one of the
  recognized/consumed ones like X-ORG-REPEATER/X-ALT-DESC/
  X-CANON-SERIES-SPLIT-OF) through promote→demote and asserts it survives
  via `providerExtras["x-vtodo"]` alone — proving the *generic* passthrough
  mechanism, not just its special-cased consumers.
- **VTIMEZONE — LocalBlob + CalDAV:** already covered, no new code needed.
  `tst_todo_canon_roundtrip.cpp`'s `kTestVTodoWithVtimezoneNoOwnRecurrence`
  fixture and `vtimezoneRecurrenceDoesNotContaminateNonRecurringTodo`
  pin that KCalendarCore's own re-serialization is not trusted to preserve
  DST rules — the verbatim-recurrence-lines invariant (invariant 3) covers
  this at the same seam.
- **Ordering/extras "bytes verbatim":**
  - LocalBlob: `tests/calendar/tst_localbackend_blob_view.cpp`'s
    `coLocatedMasterAndException_preservesFullFileBytes` (W1) proves
    `recordFromBytes()` keeps the full original file bytes.
  - CalDAV: **new** —
    `tests/calendar/tst_remotecalendarbackend_blob_view.cpp`'s
    `loadRecords_vtodoFetch_prefersServerRawBytesOverReserialization`
    (landed with this item — no prior test asserted byte content
    specifically for VTODO fetch, only href/record-count shape or narrower
    `.contains()` substring checks). Seeds a VTODO with a custom X-prop
    placed *before* SUMMARY in source order — KCalendarCore's own
    serializer always emits well-known properties before custom X-
    properties regardless of original order, so preserving that
    non-canonical order end-to-end is only possible if the backend
    preferred the server's raw bytes (`m_lastRawIcsByUid`) over a
    KCalendarCore re-serialization.

## 3. W5 MS-leg alarm-shape unification (folded into this item's scope note)

The MS To-Do promote/demote code previously produced a *different*
sub-shape for `alarms[0]` — `{"reminder": {dateTime, timeZone}}` — that did
not match the vtodo/CalDAV leg's `{type, offset/at, related, text,
repeatCount, repeatIntervalSecs}` shape at all. Demoting an MS-sourced
canon record through `canonObjectToVtodoBytes` silently produced a bogus
zero-offset `Alarm::Invalid` VALARM. This is now fixed as part of W5 (see
`docs/campaign/vtodo-parity/2026-08-28-vpf-return-receipt.md` §W5 for the
full writeup): MS promote now emits `alarms[0] = {"type": Display, "at":
<UTC ISO>}`, the same shape vtodo produces for an absolute-trigger alarm.
The truth table's "single reminder ⇄ alarms[0]" claim is therefore now
true by *shape* agreement, not merely array-position coincidence.

## 4. O74 fix note

The todo domain's canonical differ (`CanonJsonDiffer(todoCanonPropertyIds())`)
only ever compares catalogued property ids; `providerExtras` is
deliberately never catalogued, so a change confined to X-/extra properties
previously never produced a diff (FINDINGS.md O74). Fixed by a new
catalogued `providerExtrasDigest` (String) key, computed at promote time on
each leg from that leg's own extras content via a new domain-neutral
`Kalburator::Shape::CanonEnvelope::canonicalDigest(const QJsonValue&)`
helper (SHA256 hex, house convention). `providerExtrasDigest` is **Dropped**
on all three legs — it is purely derived/meta, recomputed fresh on every
promote, with no wire representation by design.

**Volatility filtering (the load-bearing part of this fix):** MS and
Google's "everything unmapped" extras stashes both mix real content with
vendor bookkeeping that churns on every server-side write regardless of
semantic content. Hashing that bookkeeping unfiltered would make the
digest spuriously "always dirty." The exact filter lists (see the return
receipt for the full evidence trail):

- **Google:** excludes `etag` only. The other stashed fields
  (`kind`/`deleted`/`hidden`/`links`/`webViewLink`/`selfLink`/
  `assignmentInfo`) are real content or stable transport metadata, not
  per-write bookkeeping, and stay hashed.
- **MS:** excludes `@odata.etag`, `lastModifiedDateTime`, and
  `@odata.context`. Confirmed against a real captured Graph todoTask
  sample that all three land in the unmapped stash; the first two bump on
  every server-side write, and `@odata.context` is a request-URL artifact
  (varies between `v1.0`/`beta` and `$expand` variants of the identical
  unedited record) rather than per-record content. `createdDateTime` is
  deliberately **kept** — it is set once at creation and does not change
  on subsequent edits, so it carries no false-dirty risk.

Differ pin: `tst_canonjson_diff_merge.cpp`'s
`differMarksProviderExtrasDigestChangeOnly` proves a `providerExtrasDigest`
value change is reported like any other catalogued property change.
`differIgnoresProviderExtrasAndCanon` (the pre-existing, narrower-catalogue
test) is unmodified — it still correctly pins the differ's generic
"only catalogued keys" contract in the abstract.
