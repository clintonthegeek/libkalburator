# Microsoft Graph event ⇄ calendar/canon — declared loss profile (EEE Phase 7.B / Phase 4)

**Date:** 2026-08-23
**Status:** Declared before stage code (campaign invariant 2). The demote
edge (`canon → ms-event`) must honor this table exactly; divergence between
declared and actual loss is a RED test. **This is the campaign's one deep
component** — the RFC5545 ⇄ `patternedRecurrence` converter lives INSIDE the
stage (invariant 5: recurrence never parsed canon-side) with its own unit
suite before edge integration.
**Reference inputs:** vendor-api-shapes-reference §1.2, §1.3, §1.5; live
corpus findings **O57**(a)–(h) (`docs/campaign/FINDINGS.md`); canon catalogue
`src/calendar/calendarcanonproperties.cpp`; Phase 2 template
(`docs/2026-08-23-google-event-edge-loss-profile.md`).

## Shapes and registration

- Peer shape `{calendar, ms-event}` — one Microsoft Graph v1.0 `event`
  resource as JSON (wire shape; list/delta envelopes belong to transport).
- Catalogue: the Graph event field set the stages read/write, registered in
  `CalendarStockShapes::peerShapes()`.
- Edges: `ms-event → canon` promote, `canon → ms-event` demote.

## Carrier channels

Unlike Google's string-keyed `extendedProperties.private`, Graph offers
**`singleValueExtendedProperties`** (typed, `{id: "String {guid} Name …",
value}`) and **`multiValueExtendedProperties`**. Carriers use:

```
id: "String {66f5926c-9c3e-4c14-9e4b-7a2f0d1c9eee} x-canon-<prop>"
```

(GUID minted once for the campaign and pinned in the stage source; values
are strings, same valueToCarrierString discipline as the Google edge.)

## Per-property declarations — `canon → ms-event`

| Canon property | LossKind | Notes |
|---|---|---|
| `uid` | lossless | ⇄ top-level `uid` (= `iCalUId`, O57(a)). Transport `id`/`changeKey` → `providerExtras["msgraph"]`. |
| `sequence` | Simplified | Graph has no SEQUENCE; carried. |
| `created` / `lastModified` | lossless | ⇄ `createdDateTime` / `lastModifiedDateTime` |
| `summary` | lossless | ⇄ `subject` |
| `description` | lossless | ⇄ `body{contentType:text, content}` |
| `descriptionHtml` | lossless | ⇄ `body{contentType:html}` — Graph natively holds both; better than iCal. |
| `location` | Degraded | rich `location{}` ⇄ free-form string; canonical round-trip keeps `displayName` only unless structured fields survive in `locations[]` |
| `locations` | Degraded | multi ⇄ `locations[]`; Bing-resolved extras (`uniqueId`, `locationUri`, `addedBy`, O57(c)) → `providerExtras["msgraph"]` on promote; re-emitted when present. |
| `start` / `end` | Degraded (zone vocabulary) | `{dateTime,tz,floating:false}` ⇄ `dateTimeTimeZone`. **Windows-vocabulary zones normalize to IANA via the vendored CLDR `windowsZones.xml` map** (many-to-one → `Degraded`, e.g. `"Eastern Standard Time"` → `America/New_York`); the ORIGINAL zone id is preserved verbatim (canon side carries it; demote re-emits it). Floating time: no Graph form → UTC pin + carrier (same as Google edge). |
| `allDay` | Degraded | Graph has no date-only form: midnight-to-midnight timed pair + `isAllDay:true`; demote emits the pair; promote detects the pair. |
| `recurrence` | Simplified (directional) | THE deep component, see below. MS→RFC5545 is lossless (§1.3 table); **RFC5545→MS has a cannot-represent list** → declared per-rule below. |
| `recurrenceId` / `recurrenceRange` | Degraded | ⇄ instance records keyed by `originalStart` (+`type:"exception"`, O57(h)); THISANDFUTURE → series-split, declared Simplified (no single-object form anywhere). |
| `status` | Degraded + `losslessValues{confirmed,tentative,cancelled}` | ⇄ `showAs`? No — ⇄ `isCancelled` + `status`… Graph models cancellation as `isCancelled:true` (and O57 dual-semantics); tentative has no master-level form → carrier. |
| `classification` | Degraded + `losslessValues{public,private,confidential}` | ⇄ `sensitivity`; canon `personal` → `private` + carrier (mirrors iCal edge). |
| `timeTransparency` / `freeBusyStatus` | Degraded + `losslessValues{opaque,transparent}` on transparency | ⇄ `showAs` (`busy/free/tentative/oof/workingElsewhere`): `free`→transparent, else opaque; full value preserved via `X-MICROSOFT-CDO-BUSYSTATUS`-equivalent carrier. |
| `color` | Dropped | Graph events carry no color (calendar-level only). |
| `categories` | lossless | ⇄ `categories[]` (string array, direct) |
| `organizer` | lossless | ⇄ `organizer{emailAddress{name,address}}` |
| `attendees` | Simplified | email/name/partstat lossless (`responseStatus.status` vocab matches needsAction/accepted/declined/tentative); role → `type{required,optional,resource}` (chair→required, distinction lost); `rsvp` flag → carrier. **O57(t) alias-expansion caveat:** attendee rows may multiply per vendor-canonical identity — stage maps rows faithfully; convergence is an engine/identity-layer concern, not an edge concern. |
| `responseRequested` | lossless | ⇄ `responseRequested` (Graph has it!) |
| `responseStatus` (owner's own partstat) | Degraded | ⇄ organizer `responseStatus`; year-1 sentinel times (`0001-01-01T00:00:00Z`, O57(d)) normalize to ABSENT — never manufactured stamps. |
| `priority` | Degraded + `losslessValues{low,normal,high}`? No: | canon priority is iCal 0–9 integer; Graph `importance` is low/normal/high → **Simplified** (bucket mapping), original carried. |
| `alarms` | Simplified | ⇄ `isReminderOn` + `reminderMinutesBeforeStart` (single reminder, minutes-before only). VALARMs beyond the first / non-display actions / absolute triggers → carried via multiValueExtendedProperties. |
| `onlineMeeting` | lossless-ish | ⇄ `onlineMeeting{joinUrl}` + `onlineMeetingProvider`; canon Json superset → providerExtras remainder. |
| `attachments` | Simplified | ⇄ `attachments[]` file/reference forms; item attachments have no canon home → carried. |
| `url` | lossless | ⇄ `webLink` (read-only on Graph: demote emits it only if already present; canon-authored url → carrier) |
| `eventType` | Degraded + `losslessValues{singleInstance,occurrence,exception,seriesMaster}` | Graph `type` is RECORD TOPOLOGY, not semantics (unlike Google's eventType!). Canon eventType holds Google vocab — the two vocabs do NOT translate: Google values → carrier, Graph topology → derived from record structure, never stored as canon eventType. **Decision:** canon `eventType` untouched by this edge; Graph `type` reconstructed structurally on demote. |
| MS-only flags: `allowNewTimeProposals`, `hideAttendees`, `isOrganizer`, `transactionId`, `isDraft` | Reversible / transport | flags carried; transactionId/isDraft are transport-local → providerExtras. |
| `geo`, cross-kind fields | Dropped | same ruling as Google edge |

## Recurrence conversion detail (invariant 5: inside the stage)

**MS → RFC5545 (promote): lossless.** Per reference §1.3:
daily/weekly/absoluteMonthly/relativeMonthly/absoluteYearly/relativeYearly +
interval/month/dayOfMonth/daysOfWeek/firstDayOfWeek/index → RRULE with
BYDAY/BYMONTHDAY/BYMONTH/WKST/BYSETPOS (index first..last → BYSETPOS 1..-1).
Range: endDate→UNTIL, numbered→COUNT, noEnd→neither.
`cancelledOccurrences[]` → EXDATE lines materialized into canon `recurrence`.

**RFC5545 → MS (demote): cannot-represent list → declared loss:**

| Rule feature | Ruling | Carrier |
|---|---|---|
| sub-daily FREQ (HOURLY/MINUTELY/SECONDLY) | Simplified: nearest representable (daily) + full RRULE carried | `x-canon-recurrence` |
| BYWEEKNO, BYYEARDAY | Simplified: dropped from pattern, RRULE carried | `x-canon-recurrence` |
| general BYSETPOS (non-index semantics) | Simplified: same | `x-canon-recurrence` |
| BYMONTHDAY+BYDAY intersection ("Friday the 13th") | Simplified: same | `x-canon-recurrence` |
| multi-value BYMONTHDAY/BYMONTH | Simplified: first value emitted, RRULE carried | `x-canon-recurrence` |
| multiple RRULE lines | Simplified: first rule emitted, all lines carried | `x-canon-recurrence` |
| EXRULE | Simplified: expanded to cancelledOccurrences where computable offline? NO — carried only | `x-canon-recurrence` |
| RDATE (ad-hoc added occurrences) | Dropped from pattern; lines carried | `x-canon-recurrence` |
| WKST on non-weekly | Simplified: omitted, RRULE carried | — |

Every carried case re-promotes to byte-identical canon `recurrence` — the
round-trip test pins this set explicitly.

## Zones

Vendor `windowsZones.xml` (CLDR) vendored WITH version stamp into the stage
directory; many-to-one Windows→IANA resolution declares `Degraded` with the
original Windows id kept verbatim in the promoted record (satisfies O57(b)'s
original-zone-preservation requirement). IANA-side zones pass through
untouched. Ambiguous aliases resolve to the CLDR-preferred IANA id.

## Verification plan

1. Unit suite for the converter FIRST: `tst_recurrence_pattern_converter`
   — every §1.3 row both directions, every cannot-represent ruling above,
   sentinel/zero handling.
2. Edge suite `tst_ms_event_canon_edge`: captured-shaped payloads
   (sanitized corpus extracts once available), declared-vs-actual loss table
   walk, round-trip identity for the lossless+carrier set, split-brain zones
   (O57(b)), masters-only listing shapes.
3. Registry inspection slot mirroring the Google edge.
4. Live checkpoint before any consumer sees it (proposal invariant 6).

## Verification status

Landed 2026-08-23 (stub-level; live checkpoint pending). Converter suite
green first (31 slots), then `tst_ms_event_canon_edge` (10 slots) including
the C→G→C byte-equal identity for BOTH the representable set and the
unrepresentable-rule carrier path. Declared-vs-actual divergence found
during implementation: none — two implementation traps hit and fixed are
logged as FINDINGS O60 (QJsonValue Null-default trap; wall-time zone
interpretation). Implementation decisions that refine this profile:
redundant-topology suppression on promote (`type` consumed when equal to
the structural derivation — keeps C→G→C byte-equal); wire-fidelity stashes
preferred over rebuilds on demote (attendees/attachments/locations);
demote timestamps carry the full ".0000000Z" wire form. The
committed-live-fixture slot awaits Graph-side fixture sanitization.

## Out of scope

- RSVP/accept endpoints (PlanStan has no organization affordances yet;
  proposal §7.b MVP scoping).
- Override write-back (v1 writes flat events + masters; exceptions expand
  read-only).
