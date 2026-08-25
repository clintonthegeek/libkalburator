# Google Calendar event ⇄ calendar/canon — declared loss profile (EEE Phase 2)

**Date:** 2026-08-23
**Status:** Declared before stage code (campaign invariant 2). The demote
edge (`canon → google-event`) must honor this table exactly; a divergence
between declared and actual loss is a RED test (see the
`tst_vcard3_vcard4_edge::declaredDropsMatchKContactsReality` template).
**Reference inputs:** `docs/2026-05-23-vendor-api-shapes-reference.md` §1.1,
§1.4, §1.5; canon catalogue `src/calendar/calendarcanonproperties.cpp`.

## Shapes and registration

- Peer shape `{calendar, google-event}` — a single Google Calendar v3 `event`
  resource as pretty-printed JSON (the wire shape, one object per record;
  list/delta envelopes belong to transport, not the edge).
- Catalogue: Google's own field set (subset that the stage reads/writes),
  registered in `CalendarStockShapes::peerShapes()`.
- Four edges: `google-event → canon` (promote, `LossProfile{}` — lossless),
  `canon → google-event` (demote, this profile).

## Key structural luck (why this is the friendliest edge)

Google's `recurrence` field **is already verbatim RFC5545 lines**
(RRULE/EXRULE/RDATE/EXDATE), which is exactly the canon `recurrence`
StringList encoding (house invariant: recurrence never parsed canon-side).
The promote/demote of `recurrence` is therefore byte-faithful — no parser,
no loss, the thing that makes Phase 4 (Graph) hard does not exist here.
Google's `timeZone` is IANA, matching canon's `tz` field directly.

## Per-property declarations — `canon → google-event`

Legend: LossKind per `src/shape/lossprofile.h`; "carrier" = stashed into
`extendedProperties.private["x-canon-<prop>"]` (Google's native extension
point — the analogue of iCal X- props) and re-promoted on the way back.

| Canon property | LossKind | Notes |
|---|---|---|
| `uid` | lossless | ⇄ `iCalUID`. (Google `id` is transport-local → `providerExtras["google"]["id"]`, not canon.) |
| `sequence` | lossless | ⇄ `sequence` |
| `created` / `lastModified` | Simplified | ⇄ `created` / `updated`. Canon is second-granular (house convention); Google's millisecond precision (`.000Z`) truncates. Instant-preserving. |
| `summary` | lossless | ⇄ `summary` |
| `description` | lossless | ⇄ `description` |
| `location` | lossless | ⇄ `location` (free-form string both sides) |
| `start` / `end` | lossless; floating → Reversible | `{date}` ⇄ all-day `{date}`; `{dateTime,tz,floating:false}` ⇄ `{dateTime,timeZone}`. Google's offset-local form (`-04:00`) canonicalizes to UTC-ISO (`Z`) with the IANA zone preserved — instant- and zone-preserving normalization. **`floating:true` has no Google form** (§1.5): pinned to UTC, original carried (`x-canon-floating`) and restored on re-promote. |
| `allDay` | lossless | ⇄ date-only start/end |
| `recurrence` | lossless | RFC5545 lines ⇄ RFC5545 lines (verbatim) |
| `recurrenceId` / `recurrenceRange` | lossless | ⇄ `recurringEventId` + `originalStartTime` (§1.4: all three key on original start) |
| `status` | lossless (case-normalized) | canon `confirmed/tentative/cancelled` ⇄ same, lowercase |
| `classification` | Degraded + `losslessValues{public,private,confidential}` | ⇄ `visibility`. Any other canon value (e.g. MS `personal`) → `private`, original carried `x-canon-classification`. Absent classification ⇄ Google `visibility:"default"` (omitted both directions). |
| `timeTransparency` / `freeBusyStatus` | Degraded + `losslessValues{opaque,transparent}` on transparency | ⇄ `transparency`. `freeBusyStatus` values beyond free/busy (`oof`, `workingElsewhere`, `tentative`) → nearest `transparency`, original carried. |
| `color` | Degraded | canon holds a color *string*; Google wants a palette `colorId`. Verbatim passthrough when it matches `^[0-9]+$` (already an id); else carried `x-canon-color`, `colorId` omitted. |
| `categories` | carrier (Reversible) | no Google CATEGORIES field → carried `x-canon-categories` (JSON string) and restored on re-promote |
| `organizer` | lossless | ⇄ `organizer{email,displayName}` (`{id,self}` → providerExtras) |
| `attendees` | Simplified | email/name/responseStatus lossless; canon `role` → `optional` boolean only (chair/req-participant distinction lost → Simplified); `partstat` vocab maps 1:1 (needsAction/accepted/declined/tentative); `rsvp`, `comment`, `additionalGuests` carried. |
| `responseRequested` | carrier (Reversible) | no Google field → `x-canon-response-requested` |
| `priority` | carrier (Reversible) | no Google field → `x-canon-priority` |
| `alarms` | Simplified | ⇄ `reminders{useDefault,overrides[]}`: VALARMs with display/email action + minutes trigger map to overrides; audio/procedure/absolute-TRIGGER alarms carried verbatim `x-canon-alarms`. |
| `descriptionHtml` | carrier (Reversible) | → `x-canon-description-html` (the X-ALT-DESC pattern) |
| `locations` (structured multi) | Simplified | multi → primary `location` string (same as iCal edge), full JSON carried `x-canon-locations` |
| `onlineMeeting` | Degraded | ⇄ `conferenceData` entryPoints where mappable (video URL); full canon JSON carried `x-canon-online-meeting` |
| `attachments` | Simplified | ⇄ `attachments[]` `{fileUrl→fileUrl,title,mimeType}`; canon-only fields carried |
| `url` | Simplified | ⇄ `source.url` (`source.title` has no canon home → providerExtras) |
| `eventType` | Degraded + `losslessValues{default,birthday,focusTime,fromGmail,outOfOffice,workingLocation}` | Google vocab passes; MS vocab (`singleInstance` etc.) → `default` + carried |
| `guestsCanModify` / `guestsCanInviteOthers` / `guestsCanSeeOtherGuests` | lossless | direct booleans |
| `privateCopy` / `locked` | lossless | direct booleans |
| `allowNewTimeProposals` / `hideAttendees` (MS flags) | carrier (Reversible) | no Google field → `x-canon-*` |
| `typedProperties` | carrier (Reversible) | ⇄ `extendedProperties.shared` (name-spaced keys) |
| `geo`, `due`, `completed`, `percentComplete`, `relatedTo` | Dropped | no Google form, no safe carrier semantics for geo; cross-kind fields must not appear in calendar-event records anyway |
| `onlineMeeting` provider extras, `eventType`-specific payloads (`workingLocationProperties`, `outOfOfficeProperties`, `focusTimeProperties`, `birthdayProperties`) | carrier (Reversible) | promoted verbatim into `providerExtras["google"]` on the way in; re-emitted on the way out when present |

### Carrier rule (what makes "carrier (Reversible)" honest)

Everything stashed in `extendedProperties.private["x-canon-*"]` MUST be
re-promoted by `GoogleEventToCanonStage` and re-emitted by
`CanonToGoogleEventStage` — a round-trip `canon → google → canon` must be
byte-equal on every property except the Degraded/Simplified entries above.
The round-trip test asserts this. If a carrier cannot round-trip, its
declaration must be downgraded to Simplified/Dropped through review — never
silently.

## Promote (`google-event → canon`): lossless by construction

Every Google field either maps to a canon property (table above, reverse
arrows) or lands in `providerExtras["google"]` (etag, id, htmlLink,
hangoutLink, creator, gadget, anyoneCanAddSelf, attendeesOmitted,
`extendedProperties` remainder, eventType payloads). Nothing is discarded on
promote — the corpus test pins this with a real captured-shaped payload.

## Verification status

Implemented + pinned 2026-08-23 (`tst_google_event_canon_edge`, 7 slots green;
suite baseline moves to 181 total / 179 passing). Wire-truth corrections made
during implementation against the live Calendar API v3 events reference
(FINDINGS O59): reminders overrides key is `method` (not `reminderMethod`);
`extendedProperties.private.(key)` values are string-typed (all carriers
JSON-stringified); `eventLabelId` exists in the wire schema (supersedes
index-based `colorId`) and is carried verbatim via providerExtras until the
canon catalogue grows a home for it.

**Additional declared normalizations (2026-08-25, Tier A4 live checkpoint,
via `tools/groundtrip`).**

1. **Attendee `organizer` flag**: redundant with the top-level
   `organizer`/`creator` identity; no canon home, not carried — demote emits
   no per-attendee `organizer` boolean. The organizer fact itself survives at
   top level.
2. Transport-field absence (`etag`, `htmlLink`, `hangoutLink`, `creator`,
   `kind`, `sequence`, false-flag booleans, etc.): these live in
   `providerExtras["google"]`, not canon, and do not survive a canon crossing
   by design (declared in the runner's normalization set, same discipline as
   the ms-event profile).

## Out of scope for this edge (deliberately)

- Transport (OAuth, list/delta/syncToken envelopes) — Phase 7 layering.
- `RANGE=THISANDFUTURE` semantics — series-split modeling, §1.4 hard case,
  engine-level concern, not an edge concern.
- Working-location/out-of-office/focus-time *semantics* — carried verbatim
  (Reversible), interpretation deferred to canon v2.

## Verification plan

1. Unit: `tests/calendar/tst_google_event_canon_edge.cpp` — promote from a
   captured-shaped payload (modeled on reference §1.1 + live Google exports),
   demote declared-loss table assertions (declared-vs-actual, per property),
   round-trip identity for the lossless+carrier set.
2. Registry: edge registration visible via `inspect()`; freeze semantics
   respected (registration at plugin load only).
3. Suite: full build green; baseline moves from 180/178.
