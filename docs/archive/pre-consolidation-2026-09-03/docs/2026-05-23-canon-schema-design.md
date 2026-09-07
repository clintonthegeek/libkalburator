# Design: Canon Schema — `calendar+canon`, `contacts+canon`, `todo+canon`

**Date:** 2026-05-23
**Status:** Design for review. Defines the concrete field-level schema of the upgraded canonical
encodings. Derives from the vendor-shapes reference; feeds the implementation plan.
**Companion docs:**
- `2026-05-23-canon-upgrade-and-convergence-design.md` — the architecture (spine, loss model,
  convergence). This doc fills its deferred "field-by-field canon schema" follow-on.
- `2026-05-23-vendor-api-shapes-reference.md` — the vendor field tables these schemas superset.
**Grounded in:** `src/shape/propertycatalogue.h` (`PropertyId`, `PropertyKind`,
`PropertyDescriptor`, `PropertyCatalogue`), `src/shape/recorddiffer.h`, `src/shape/recordmerger.h`,
`src/shape/canonicalrecord.h`.

---

## 1. Serialization contract

### 1.1 Canon bytes = a JSON object keyed by `PropertyId`

A canonical record's `CanonicalRecord::data` (`QByteArray`) is a **UTF-8 JSON object**. Each
top-level key is a `PropertyId` string; each value is typed per the property's `PropertyKind`:

| `PropertyKind` | JSON representation | SQLite (per catalogue `sqlColumnDdl`) |
|---|---|---|
| String | string | TEXT |
| Integer | number (int) | INTEGER |
| Boolean | `true`/`false` | INTEGER |
| DateTime | RFC3339 string | TEXT |
| Duration | ISO8601 duration string | TEXT |
| Bytes | base64 string | BLOB |
| StringList | array of strings | TEXT (JSON) |
| **Json** | object/array (nested, schema below) | TEXT (JSON) |

This is the natural fit for the existing types: `PropertyKind::Json` already exists "for nested or
composite values (attendees, etc.)", `PropertyCatalogue` already maps kinds to SQLite columns, and
`RecordDiffer::diff` already returns a `QSet<PropertyId>`. Each `DomainDefinition::canonicalCatalogue()`
returns the `PropertyCatalogue` enumerated in §2–§4; the differ parses the JSON and compares
per-`PropertyId`.

**Why JSON (decision to confirm):** structured and nested (mirrors the Google/MS object hierarchies
we superset), extensible (provider-extras bag + forward-compatible unknown-key retention), per-property
diffable, human-inspectable, and trivially serialized via `QJsonDocument`. The alternative (keep
canonical bytes as iCal/vCard text) is rejected: those text formats *are* the lossy peers we are
demoting, and cannot hold the rich superset.

### 1.2 Envelope

Every canon record carries a reserved envelope alongside its domain properties:

```json
{
  "_canon": { "domain": "calendar", "v": 1 },
  "uid": "…",
  "...domain properties...": "...",
  "providerExtras": { "...": "..." }
}
```

- `_canon.v` — the **spine node version** (1 for the first rich canon). Authoritative spine identity
  is the `EncodingId` (`calendar+canon`); `_canon.v` is a redundant in-band guard. A future bump is a
  new `EncodingId` (e.g. `calendar+canon2`) appended to the spine (§6).
- `uid` — stable logical identity (`String`, required). Mirrors `CanonicalRecord::recordId`. For
  calendar/todo this is the iCalendar UID; for contacts a stable canon UID (vendor id retained in
  `providerExtras`).
- Unknown top-level keys are **retained verbatim** on round-trip (forward-compat: a record written by
  a newer canon and demoted through an older library must not lose keys it doesn't understand).

### 1.3 The provider-extras bag

`providerExtras` (`PropertyKind::Json`, optional) is a **namespaced, opaque** carrier for vendor
fields with no cross-vendor home (reference §0.2): MS MAPI `singleValue/multiValueExtendedProperties`
and open `extensions`, `changeKey`, `parentFolderId`/immutable-id metadata, Google output-only system
fields. Structure: `{ "<namespace>": { …opaque… } }`, e.g. `"x-ms-mapi"`, `"x-ms-graph"`,
`"x-google"`, `"x-palm"`. It is **carried verbatim and never a conflict axis** (§5): on merge it
follows the record's origin rather than being three-way merged. This is the structured successor to
today's `X-`-property stamping (handoff §5.1) and is how WildPalms's `X-WP-PALM-*` round-trip
identity survives.

### 1.4 Recurrence is opaque RFC5545 text

Per design §4.0: recurrence lives in **one** property, `recurrence` (`PropertyKind::StringList` —
the RFC5545 RRULE/RDATE/EXDATE lines), stored verbatim. The differ treats it as a single opaque
field. No canon code parses it; only the `canon → Microsoft` transformation stage parses it to emit
`patternedRecurrence` (a localized edge concern).

---

## 2. `calendar+canon`

`PropertyId` table (kind; source legend: iCal / G=Google / M=MS; loss = behavior demoting to iCal):

| PropertyId | Kind | Source | Notes / loss to iCal |
|---|---|---|---|
| uid | String (req) | iCal | UID |
| sequence | Integer | iCal | SEQUENCE |
| created / lastModified | DateTime | iCal | CREATED / LAST-MODIFIED |
| summary | String | iCal | SUMMARY |
| description | String | iCal | DESCRIPTION (plain) |
| descriptionHtml | String | G/M | → `X-ALT-DESC` (reversible-via-extension) |
| location | String | iCal | LOCATION (free-form) |
| locations | Json | M | `[{displayName, address{…}, coordinates{lat,lon}, locationType}]`; multi → dropped to first on iCal |
| status | String | iCal | confirmed/tentative/cancelled |
| classification | String | iCal/M | public/private/confidential/**personal**; `personal` (M) → dropped/`X-` |
| timeTransparency | String | iCal | opaque/transparent |
| freeBusyStatus | String | M/G | free/busy/tentative/**oof**/**workingElsewhere**; oof/workingElsewhere → `X-MICROSOFT-CDO-BUSYSTATUS` (reversible) |
| start / end | Json | iCal | `{date?|dateTime?, tz(IANA verbatim), floating:bool}` per endpoint |
| allDay | Boolean | iCal | VALUE=DATE |
| recurrence | StringList | iCal/G | RFC5545 lines (opaque, §1.4) |
| recurrenceId | Json | iCal | `{dateTime, tz}` — present on an override record (= RECURRENCE-ID / originalStart) |
| recurrenceRange | String | iCal/M | `thisAndFuture` flag for overrides; series-split at vendor edges |
| color | String | G/M | colorId/category color → dropped or `X-APPLE-CALENDAR-COLOR` |
| categories | StringList | iCal | CATEGORIES |
| url | String | iCal | URL |
| organizer | Json | iCal | `{email, name, sentBy?}` |
| attendees | Json | iCal | `[{email, name, role(req/opt/resource), partstat, rsvp, additionalGuests, proposedNewTime?, comment?}]` (`resource` → dropped/CUTYPE on iCal) |
| responseRequested | Boolean | M | → RSVP param △ |
| priority | Integer | iCal | PRIORITY 0–9 (M importance maps to 0/5/9) |
| alarms | Json | iCal | `[{action(display/audio/email), trigger{relative?|absolute?}, repeat?, duration?, description?}]` (VALARM superset; M single reminder maps in) |
| onlineMeeting | Json | G/M | `{provider, joinUrl, entryPoints[{type,uri,label,pin?}], conferenceId?, notes?}` → dropped or CONFERENCE △ |
| attachments | Json | iCal/G/M | `[{kind(file/uri/item), uri?, fileId?, mimeType?, title?}]`; item-attachments → dropped on iCal |
| eventType | String | G | default/birthday/focusTime/outOfOffice/workingLocation/fromGmail → dropped on iCal |
| typedProperties | Json | G | `{workingLocation?{…}, outOfOffice?{…}, focusTime?{…}, birthday?{…}}` (the eventType payloads) → dropped on iCal |
| guestsCanModify / guestsCanInviteOthers / guestsCanSeeOtherGuests | Boolean | G | permissions → dropped on iCal |
| allowNewTimeProposals / hideAttendees / locked / privateCopy | Boolean | G/M | flags → dropped on iCal |
| providerExtras | Json | all | §1.3 |

### 2.1 Sub-structure notes
- **start/end** keep zone per endpoint and the verbatim IANA string (reference §0.3, §1.5); `floating`
  marks zone-less iCal local time (pinned-or-refused at Google/MS edges).
- **recurrence overrides** are *separate records* sharing `uid` with a non-null `recurrenceId` (the
  iCal/KCalendarCore model), not embedded — so no special container is needed; `recurrenceRange`
  carries `thisAndFuture` for the series-split case.

---

## 3. `contacts+canon`

Anchored on vCard4 ∪ Google People (reference §2.1–§2.2). Most properties are **repeated typed**
objects; each carries an optional `type`, free `label`, and `primary` flag (the People/vCard pattern).

| PropertyId | Kind | Notes / loss to vCard |
|---|---|---|
| uid | String (req) | stable canon id |
| names | Json | `[{given, family, middle, prefixes[], suffixes[], displayName, fileAs, phonetic{given,family,middle,company}}]` (repeated; vCard `N` single → first; phonetic → `SORT-AS` △) |
| nicknames | Json | `[{value, type}]` |
| emails | Json | `[{value, type, label, primary}]` |
| phones | Json | `[{value, type, label, primary, canonical(E.164)}]` |
| addresses | Json | `[{type, label, primary, street, extended, poBox, city, region, postalCode, country, countryCode, formatted, geo?{lat,lon}}]` |
| organizations | Json | `[{name, department, title, jobDescription, phoneticName, symbol, domain, location, costCenter, startDate?, endDate?, current?}]` (vCard `ORG`+`TITLE` → first) |
| occupations | StringList | |
| urls | Json | `[{value, type, label}]` (vCard single `URL`-per-entry) |
| imClients | Json | `[{value, protocol, type}]` (vCard `IMPP`) |
| sipAddresses | StringList | |
| calendarUrls | Json | `[{value, type}]` (CALURI/FBURL/CALADRURI) |
| relations | Json | `[{value, type(spouse/child/parent/manager/assistant/…)}]` (vCard `RELATED`; absorbs MS assistantName/manager/spouseName/children) |
| birthday | Json | `{date, hasYear:bool}` (BDAY) |
| anniversary | Json | `{date, hasYear:bool}` (ANNIVERSARY) |
| significantDates | Json | `[{date, type, label}]` (People `events`) |
| gender | Json | `{value, identity?, pronouns?}` (GENDER + People addressMeAs) |
| notes | String | NOTE (+ biographies) |
| photos | Json | `[{uri?|bytes?, mimeType, primary}]` (PHOTO) |
| categories | StringList | CATEGORIES |
| languages | StringList | LANG |
| timeZone | String | TZ |
| externalIds | Json | `[{value, type}]` |
| memberships | Json | `[{group, kind}]` |
| interests / skills | StringList | |
| providerExtras | Json | §1.3 (MS MAPI ext props, changeKey, folder/immutable id; Google source/primary metadata) |

Note: Google People's per-field `metadata{primary,source,verified}` is reduced to the `primary` flag
on each entry; full source/verified metadata (not authored data) goes to `providerExtras`.

---

## 4. `todo+canon`

Anchored on VTODO/RFC5545, augmented per reference §3.3.

| PropertyId | Kind | Source | Notes / loss |
|---|---|---|---|
| uid | String (req) | iCal | UID |
| created / lastModified | DateTime | iCal | CREATED / LAST-MODIFIED |
| summary | String | iCal | SUMMARY (title) |
| description | String | iCal | DESCRIPTION (plain) |
| descriptionHtml | String | M | HTML body → `X-ALT-DESC` (reversible) |
| status | String | iCal/M | needsAction/inProcess/completed/cancelled/**waitingOnOthers**/**deferred** (last two M-only → dropped to needsAction on VTODO, retained in providerExtras) |
| percentComplete | Integer | iCal | PERCENT-COMPLETE (0–100); vendors lack → dropped |
| priority | Integer | iCal | PRIORITY 0–9 (M low/normal/high → 9/5/1) |
| categories | StringList | iCal | CATEGORIES |
| start | Json | iCal | `{date?|dateTime?, tz, floating}` (DTSTART) |
| due | Json | iCal | `{date?|dateTime?, tz, precision(date\|dateTime)}` (DUE; Google date-only sets precision=date) |
| completed | DateTime | iCal | COMPLETED |
| recurrence | StringList | iCal | RFC5545 lines (opaque, §1.4) |
| alarms | Json | iCal | VALARM superset (as §2 alarms); M single reminder maps in; Google none |
| location | String | iCal | LOCATION |
| geo | Json | iCal | `{lat, lon}` (GEO) |
| sortOrder | String | G | opaque sibling-order (Google `position`); no VTODO home → providerExtras on VTODO |
| relatedTo | Json | iCal | `[{uid, reltype(parent/child/sibling)}]` — **the VTODO hierarchy tree, carried verbatim** |
| parentUid | String | G | single-level parent (Google) — coexists with relatedTo (§5) |
| checklistItems | Json | M | `[{label, checked:bool, checkedAt?}]` — lightweight, **distinct from subtasks** |
| linkedResources | Json | M | `[{applicationName, externalId, webUrl, displayName}]` cross-app links → providerExtras on VTODO |
| providerExtras | Json | all | §1.3 |

**Hierarchy (the carry-multiple-representations rule):** `relatedTo` (full tree), `parentUid`
(single-level), and `checklistItems` (checkboxes) are **three independent representations retained
side by side**, never collapsed into one (reference §3.3). A demote to a given peer reads whichever
it supports; none is derived from another inside the canon.

---

## 5. Differ / merger semantics

For each `DomainDefinition::createCanonicalDiffer()` / `createCanonicalMerger()`:

- **Granularity = one `PropertyId` per row above.** A change anywhere inside a composite (`Json`)
  property marks that whole property changed. This is intentionally coarse: it is correct, simple, and
  avoids sub-field 3-way merges we cannot do safely. Finer per-element diffing (e.g. per-attendee) is
  a possible future refinement, explicitly **not** in scope.
- **`recurrence` is opaque** (§1.4): diffed as a single StringList; merged whole (no recomposition).
- **`providerExtras` is not a conflict axis** (§1.3): on merge it follows the chosen record's origin;
  it never forces or resolves a conflict.
- **Hierarchy properties** (`relatedTo`/`parentUid`/`checklistItems`) are carried verbatim and merged
  whole, per representation.
- **`equal()`** = semantic JSON equality over the catalogue properties (key order– and
  whitespace–independent), ignoring the `_canon` envelope and `providerExtras`.
- **Merge** remains the existing 3-way `TakeSource/TakeTarget/TakeBaseline` per property under the
  supplied `ConflictPolicy`; richer canon just means more `PropertyId`s in the union.

---

## 6. Versioning and the spine

- `calendar+canon` / `contacts+canon` / `todo+canon` are spine node **v1** for their domains. The
  `EncodingId` is the authoritative spine identity; `_canon.v` is the in-band guard.
- A future widening is a **new EncodingId** (e.g. `calendar+canon2`) appended to the spine, plus the
  bridge edge pair `canon → canon2` (lossless widen) and `canon2 → canon` (narrow per §6 loss model
  of the architecture doc). Existing peer edges still target `calendar+canon` and are auto-extended
  along the spine — no peer rewrite (architecture §5).
- **Forward-compat rule** (§1.2): unknown top-level keys are retained verbatim, so a `canon2` record
  narrowed to `canon` by an older library keeps the `canon2`-only keys for a later re-widen. This is
  what makes the narrow direction *reversible-via-extension* rather than lossy for our own round-trips.
- The first concrete v2 trigger on the horizon is **people-canon v2** when Microsoft `profile`
  reaches GA (architecture §4 People).

---

## 7. Decisions to confirm / open

1. **JSON serialization** (§1.1) — the core decision. Confirm JSON-keyed-by-`PropertyId` over keeping
   canonical bytes as a text format. (Recommended; aligns with `PropertyKind::Json` + catalogue.)
2. **Coarse per-property diff granularity** (§5) — confirm whole-composite diffing is acceptable for
   v1 (no per-attendee / per-email element diffing).
3. **`uid` for contacts** — vCard has `UID`, but it's inconsistently populated by sources. Confirm the
   canon mints/normalizes a stable contact `uid` and stashes the vendor id in `providerExtras`.
4. **Catalogue completeness for SQLite** — the baseline store derives columns from
   `PropertyCatalogue::sqlColumnDdl()`. Confirm all `Json`/`StringList` props as TEXT columns is fine
   (it is, per the catalogue), i.e. no property needs promotion to a real relational table in v1.
