# Reference: Vendor API Shapes (Google / Microsoft Graph / iCal) for Canonical Domain Design

**Date:** 2026-05-23
**Type:** Reference (durable capture of the API research behind the canon-upgrade design).
**Why this exists:** the design doc (`2026-05-23-canon-upgrade-and-convergence-design.md`, §9)
records only the *conclusions*. This file is the *evidence* — the actual field tables the canon
schema and the transformation stages will target. The forthcoming canon **schema** doc derives the
normalized superset from these tables; this file is the input to that work.
**Verified:** 2026-05-23 from vendor reference docs + Microsoft `[MS-OXCICAL]`/`[MS-STANOICAL]`
open specs. Microsoft pages were `graph-rest-1.0` (GA) unless marked beta.

---

## 0. Cross-cutting findings (read first)

1. **Recurrence: RFC5545 is the superset; Microsoft `patternedRecurrence` is a strict, cleanly-
   mappable subset.** Holds identically for events and todos (To Do reuses the same object). Google
   Calendar's `recurrence` *is* RFC5545 text; Google Tasks has no API recurrence at all. → Canon
   stores **raw RFC5545 text** (design §4.0); structure is derived only at the `canon → Microsoft`
   edge.
2. **Provider-extras bag.** Every vendor object carries fields with no cross-vendor home: MS MAPI
   `singleValue/multiValueExtendedProperties` + open `extensions` (on event, contact, todoTask),
   `changeKey`, folder/immutable-id metadata; Google output-only system fields (`etag`, `selfLink`,
   `links`, `assignmentInfo`). → Each canon needs an **opaque, namespaced provider-extras bag** to
   round-trip these without inventing structure.
3. **Time zones:** Microsoft's primary vocabulary is **Windows zone IDs**; Google/iCal are **IANA**.
   The IANA→Windows map (CLDR `windowsZones.xml`) is **many-to-one/lossy** → canon keeps the original
   IANA string verbatim (loss class "preserved-but-degraded", design §6).
4. **Non-isomorphic structures the canon must carry verbatim (not normalize):** event
   `RANGE=THISANDFUTURE` overrides; todo hierarchy (three incompatible subtask models).

---

## 1. EVENTS

### 1.1 Google Calendar — `Events` resource
Ref: https://developers.google.com/workspace/calendar/api/v3/reference/events

Top-level fields:

| Field | Type | Meaning |
|---|---|---|
| kind | string | `"calendar#event"` |
| etag | etag | Version tag |
| id | string | Event id (base32hex) |
| status | string | confirmed / tentative / cancelled |
| htmlLink | string | UI link |
| created / updated | datetime | RFC3339 timestamps |
| summary | string | Title |
| description | string | Details (may contain HTML) |
| location | string | Free-form location |
| colorId | string | Color reference |
| creator / organizer | object | {id, email, displayName, self} |
| start / end | object | {date \| dateTime, timeZone(IANA)} |
| endTimeUnspecified | boolean | End truly unspecified |
| recurrence | string[] | **RFC5545** RRULE/EXRULE/RDATE/EXDATE lines |
| recurringEventId | string | Parent id for an instance |
| originalStartTime | object | Scheduled start of an instance (= RECURRENCE-ID) |
| transparency | string | opaque / transparent |
| visibility | string | default / public / private / confidential |
| iCalUID | string | RFC5545 cross-system UID |
| sequence | integer | iCal sequence |
| attendees | object[] | see below |
| attendeesOmitted | boolean | List truncated |
| extendedProperties | object | {private{}, shared{}} key-value |
| hangoutLink | string | (deprecated) |
| conferenceData | object | Meeting details (see below) |
| gadget | object | (deprecated) |
| anyoneCanAddSelf | boolean | (deprecated) |
| guestsCanInviteOthers | boolean | Permission |
| guestsCanModify | boolean | Permission |
| guestsCanSeeOtherGuests | boolean | Permission |
| privateCopy | boolean | Disable propagation |
| locked | boolean | Prevent edits |
| reminders | object | {useDefault, overrides[]{method, minutes}} |
| source | object | {url, title} origin |
| workingLocationProperties | object | {type, homeOffice/officeLocation{buildingId,floorId,floorSectionId,deskId,label}/customLocation{label}} |
| outOfOfficeProperties | object | {autoDeclineMode, declineMessage} |
| focusTimeProperties | object | {autoDeclineMode, chatStatus, declineMessage} |
| attachments | object[] | {fileUrl, fileId, mimeType, iconLink, title} (≤25) |
| birthdayProperties | object | {type(anniversary/birthday/custom/other/self), contact, customTypeName} |
| eventType | string | default / birthday / focusTime / fromGmail / outOfOffice / workingLocation |

Attendee: {id, email, displayName, organizer, self, resource, optional, responseStatus
(needsAction/declined/tentative/accepted), comment, additionalGuests, asyncOperation}.
ConferenceData: {createRequest, entryPoints[](video/phone/sip/more), conferenceSolution,
conferenceId, signature, notes}.

### 1.2 Microsoft Graph — `event` resource
Ref: https://learn.microsoft.com/en-us/graph/api/resources/event?view=graph-rest-1.0
(✗ = no iCal equivalent; △ = partial / X-prop only)

| Property | Type | Meaning | iCal |
|---|---|---|---|
| subject | String | Title | SUMMARY |
| body | itemBody | HTML/text body | DESCRIPTION △ |
| bodyPreview | String | Text preview | △ |
| start / end | dateTimeTimeZone | {dateTime, timeZone} per endpoint | DTSTART/DTEND |
| isAllDay | Boolean | All-day flag | VALUE=DATE |
| recurrence | patternedRecurrence | Pattern + range | RRULE (subset) |
| originalStart | DateTimeOffset | Occurrence/exception original start (UTC) | RECURRENCE-ID |
| originalStartTimeZone / originalEndTimeZone | String | Zone at creation | △ |
| type | String | singleInstance/occurrence/exception/seriesMaster | △ |
| seriesMasterId | String | Master id | UID (shared) |
| cancelledOccurrences | String[] | occurrenceIds cancelled (master only) | EXDATE |
| categories | String[] | Category names | CATEGORIES |
| location | Location | Rich primary location | LOCATION (string) △ |
| locations | Location[] | All locations | ✗ multi |
| attendees | Attendee[] | w/ responseStatus, type(required/optional/**resource**), proposedNewTime | ATTENDEE;PARTSTAT |
| organizer | Recipient | Organizer | ORGANIZER |
| responseStatus | ResponseStatus | Owner's response | PARTSTAT |
| responseRequested | Boolean | Wants RSVP | △ |
| importance | String | low/normal/high | PRIORITY △ |
| sensitivity | String | normal/personal/private/confidential | CLASS (`personal` ✗) |
| showAs | String | free/tentative/busy/oof/workingElsewhere/unknown | TRANSP (oof/workingElsewhere ✗ → X-MICROSOFT-CDO-BUSYSTATUS) |
| isReminderOn | Boolean | Reminder enabled | VALARM presence |
| reminderMinutesBeforeStart | Int32 | Lead time | VALARM TRIGGER |
| isCancelled | Boolean | Cancelled | STATUS:CANCELLED |
| isDraft | Boolean | Edits not yet sent | ✗ |
| isOrganizer | Boolean | Owner is organizer | ✗ |
| allowNewTimeProposals | Boolean | Invitees may propose | ✗ |
| hideAttendees | Boolean | Attendees see only self | ✗ |
| isOnlineMeeting | Boolean | Has online meeting | ✗ (CONFERENCE △) |
| onlineMeeting | OnlineMeetingInfo | Join details (read-only) | CONFERENCE △ |
| onlineMeetingProvider | enum | teamsForBusiness/skype/... | ✗ |
| onlineMeetingUrl | String | (deprecated) | △ |
| iCalUId | String | Cross-calendar UID | UID |
| transactionId | String | Client idempotency key | ✗ |
| webLink | String | Open-in-OWA | URL △ |
| hasAttachments | Boolean | Derived | ✗ |
| attachments | Attachment[] | file/item/reference | ATTACH (item ✗) |
| id | String | Graph id (container-dependent) | ✗ |
| changeKey | String | Version/etag | SEQUENCE △ |
| createdDateTime / lastModifiedDateTime | DateTimeOffset | Timestamps (UTC) | CREATED/LAST-MODIFIED |

Nav: calendar, instances, exceptionOccurrences, extensions, singleValueExtendedProperties,
multiValueExtendedProperties.

`patternedRecurrence` = `recurrencePattern` { type(daily/weekly/absoluteMonthly/relativeMonthly/
absoluteYearly/relativeYearly), interval, month, dayOfMonth, daysOfWeek[], firstDayOfWeek,
index(first/second/third/fourth/last) } + `recurrenceRange` { type(endDate/noEnd/numbered),
startDate, endDate, recurrenceTimeZone, numberOfOccurrences }.

### 1.3 Recurrence: MS pattern ↔ RFC5545 RRULE

| MS pattern | MS fields | RRULE |
|---|---|---|
| daily | interval | `FREQ=DAILY;INTERVAL=n` |
| weekly | interval, daysOfWeek[], firstDayOfWeek | `FREQ=WEEKLY;INTERVAL=n;BYDAY=…;WKST=…` |
| absoluteMonthly | interval, dayOfMonth | `FREQ=MONTHLY;INTERVAL=n;BYMONTHDAY=d` |
| relativeMonthly | interval, daysOfWeek[], index | `FREQ=MONTHLY;INTERVAL=n;BYDAY=…;BYSETPOS=k` |
| absoluteYearly | interval, dayOfMonth, month | `FREQ=YEARLY;INTERVAL=n;BYMONTH=m;BYMONTHDAY=d` |
| relativeYearly | interval, daysOfWeek[], month, index | `FREQ=YEARLY;INTERVAL=n;BYMONTH=m;BYDAY=…;BYSETPOS=k` |

Range: `endDate→UNTIL`, `numbered→COUNT`, `noEnd→neither`. `index` first/…/last → `BYSETPOS=1/…/-1`
(NOT numbered `BYDAY` — different semantics). **MS → RFC5545 lossless** (emit BYSETPOS).

**RFC5545 → MS cannot represent:** sub-daily FREQ (HOURLY/MINUTELY/SECONDLY); `BYWEEKNO`;
`BYYEARDAY`; general `BYSETPOS`; intersected `BYMONTHDAY`+`BYDAY` ("Friday the 13th"); multi-value
`BYMONTHDAY`/`BYMONTH`; multiple RRULE lines; `EXRULE`; `RDATE` (no ad-hoc added occurrences);
`EXDATE` (→ materialized cancelledOccurrences); `WKST` on non-weekly. Exchange expands-or-fails on
unsupported rules (`[MS-OXCICAL]` §2.1.3.2).

### 1.4 Exceptions / overrides
All three key on **original start time**: iCal `RECURRENCE-ID` ↔ Google `originalStartTime` ↔ MS
`originalStart`/`occurrenceId`. Cancellation: iCal `EXDATE` ↔ Google `status=cancelled` instance ↔
MS `cancelledOccurrences[]`. **Hard cases:** `RANGE=THISANDFUTURE` (no single-object form — modeled
as series-split everywhere, lossy); RDATE-added instances under MS (no attach point); Google's
cancelled instance can carry residual data EXDATE cannot.

### 1.5 Time / start-end
Google {date | dateTime + IANA timeZone}; MS dateTimeTimeZone per endpoint + originalStart/EndTimeZone
(Windows or IANA-alias vocabulary); iCal DTSTART/DTEND;TZID. Gaps: IANA↔Windows many-to-one;
per-endpoint differing zones (all three support — keep zone per endpoint); **floating time** (iCal
only — neither Google nor MS has it; pin-or-flag); all-day (Google date-only / iCal VALUE=DATE / MS
timed midnight pair + isAllDay).

---

## 2. PEOPLE

### 2.1 Google People — `Person` resource
Ref: https://developers.google.com/people/api/rest/v1/people

Most fields are **repeated**, each carrying a `metadata` { primary, sourcePrimary, verified,
source(ACCOUNT/PROFILE/DOMAIN_PROFILE/CONTACT/OTHER_CONTACT/DOMAIN_CONTACT) }.

Field groups: **names** (display/family/given/middle/honorificPrefix/suffix + full phonetic set),
**nicknames** (typed), **emailAddresses** {value, type, displayName}, **phoneNumbers** {value,
canonicalForm(E.164), type}, **addresses** {formattedValue, type, poBox, streetAddress,
extendedAddress, city, region, postalCode, country, countryCode}, **organizations** {type, startDate,
endDate, current, name, phoneticName, department, title, jobDescription, symbol, domain, location,
costCenter, fullTimeEquivalentMillipercent}, **occupations**, **skills**, **biographies**,
**birthdays** {date, text(dep)}, **genders** {value, formattedValue, addressMeAs(pronouns)},
**events** (anniversary/other), **relations** {person, type(spouse/child/parent/.../manager/assistant)},
**urls** (typed), **imClients** {username, type, protocol}, **sipAddresses**, **calendarUrls**,
**externalIds**, **interests**, **locales**, **locations** {value, type, current, buildingId, floor,
floorSection, deskCode}, **photos**/**coverPhotos** (output-only), **memberships**
{contactGroupMembership, domainMembership}, **miscKeywords** (Outlook-compatible),
**clientData** (app key/value), **userDefined**, **fileAses**. Deprecated: ageRanges,
relationshipStatuses, relationshipInterests, residences, braggingRights, taglines.

### 2.2 Microsoft Graph — `contact` resource (editable, GA, vCard-like)
Refs: contact / physicalAddress / emailAddress / profilePhoto under
https://learn.microsoft.com/en-us/graph/api/resources/<name>?view=graph-rest-1.0

| Property | Type | Meaning |
|---|---|---|
| id | String | Id (changes on folder move unless ImmutableId prefer header) |
| displayName | String | Display name (may auto-overwrite) |
| givenName / surname / middleName / initials / nickName | String | Name parts (scalar — single name only) |
| title | String | Honorific prefix |
| fileAs | String | File-under name |
| generation | String | Suffix (Jr/III) → vCard N suffix |
| yomiGivenName / yomiSurname / yomiCompanyName | String | Phonetic (Japanese) |
| emailAddresses | emailAddress[] | {name, address} (no type in v1.0) |
| primaryEmailAddress / secondaryEmailAddress / tertiaryEmailAddress | emailAddress | Positional typing |
| imAddresses | String[] | IM addresses (no protocol typing) |
| homePhones / businessPhones | String[] | Phones |
| mobilePhone | String | Single mobile |
| homeAddress / businessAddress / otherAddress | physicalAddress | 3 fixed slots |
| companyName / department / jobTitle / officeLocation / profession | String | Org (single, scalar) |
| businessHomePage | String | Single untyped URL |
| assistantName / manager / spouseName | String | Name-only relations |
| personalNotes | String | Notes |
| children | String[] | Child names |
| birthday | DateTimeOffset | Birthday (no separate anniversary) |
| categories | String[] | → vCard CATEGORIES |
| parentFolderId | String | Folder container |
| createdDateTime / lastModifiedDateTime | DateTimeOffset | Timestamps |
| changeKey | String | Concurrency token |

`physicalAddress` { street, city, state, countryOrRegion, postalCode } — **no type, no geo**.
`emailAddress` { name, address } — **no type/label in v1.0** (that's beta `typedEmailAddress`).
Nav: photo (profilePhoto), extensions, singleValue/multiValueExtendedProperties (MAPI).

**`contact` gaps vs vCard/People:** single flat name (no repeats), no gender, no anniversary
(birthday only), only `businessHomePage` (no typed URL list), relations limited to
spouse/children/assistant/manager (names only), `imAddresses` untyped, no geo, untyped emails
(positional), fixed phone buckets, single photo, single org, no TZ/LANG/occupation list, only 3
address slots.
**`contact`-only contributions to the union:** MAPI extended properties, `changeKey`,
`parentFolderId`, immutable-vs-mutable id duality → the provider-extras bag (§0.2). yomi*/fileAs/
assistant/manager *are* covered by Google People, so they survive inside the union.

### 2.3 Microsoft Graph — `person` and `profile` (orientation only)
- `person` (GA, read-only): a **relevance-ranked aggregation** with `scoredEmailAddresses`,
  `personType`, `isFavorite`. List-only. Insight/metadata, **not** authored canon data.
  Ref: https://learn.microsoft.com/en-us/graph/api/resources/person?view=graph-rest-1.0
- `profile` (**beta only**, no v1.0 page): rich professional graph across ~20 nav collections
  (accounts, names, emails, phones, addresses, skills, languages, positions, projects,
  educationalActivities, certifications, awards, patents, publications, interests, responsibilities,
  anniversaries, notes, webAccounts, websites). **Excluded from canon v1**; first real
  people-canon v2 trigger when it reaches GA.
  Ref (beta): https://learn.microsoft.com/en-us/graph/api/resources/profile?view=graph-rest-beta

---

## 3. TODOS

### 3.1 Google Tasks — `Task` resource (minimal subset)
Ref: https://developers.google.com/workspace/tasks/reference/rest/v1/tasks

| Field | Type | Meaning |
|---|---|---|
| id | string | Id |
| kind / etag | string | `"tasks#task"` / version |
| title | string | Name (≤1024) |
| notes | string | Description (≤8192) |
| status | string | **needsAction / completed only** |
| due | string(RFC3339) | **Date-only — time discarded** |
| completed | string(RFC3339) | Completion timestamp |
| updated | string | Modified (output) |
| parent | string | Parent task id (one level only) |
| position | string | Opaque lexicographic sibling order (set via `move`) |
| links[] | object | {type, description, link} (output-only) |
| webViewLink / selfLink | string | URLs (output) |
| deleted / hidden | boolean | Flags |
| assignmentInfo | object | Provenance (Docs/Chat) (output) |

**No** priority, percent-complete, categories, start, duration, recurrence (UI-only, not in API),
alarms, geo. Nesting **one level**, ≤2000 subtasks. Recurring tasks have behavioral constraints but
expose no RRULE.

### 3.2 Microsoft Graph — `todoTask` resource
Refs: todotask / todotasklist / checklistitem / linkedresource under graph-rest-1.0
(older `outlookTask` is deprecated)

| Property | Type | Meaning |
|---|---|---|
| id | String | Id (changes on list move by default) |
| title | String | Name |
| body | itemBody | {content, contentType(text/**html**)} |
| bodyLastModifiedDateTime | DateTimeOffset | Body mtime |
| status | taskStatus | notStarted/inProgress/completed/**waitingOnOthers/deferred** |
| importance | importance | low/normal/high |
| isReminderOn | Boolean | Reminder set |
| reminderDateTime | dateTimeTimeZone | Single reminder (absolute) |
| createdDateTime / lastModifiedDateTime | DateTimeOffset | Timestamps |
| dueDateTime / startDateTime / completedDateTime | dateTimeTimeZone | Datetime+zone (not date-only) |
| categories | String[] | Category names |
| recurrence | patternedRecurrence | **Same object as events** (§1.2/§1.3) |
| hasAttachments | Boolean | Derived |

Nav: checklistItems, linkedResources, attachments, extensions.
`checklistItem` { id, displayName, isChecked, createdDateTime, checkedDateTime } — **lightweight
checkbox, NOT a full task; no children**.
`linkedResource` { applicationName, externalId, webUrl, displayName } — cross-app deep-link.

### 3.3 Todo comparison vs VTODO (RFC5545)
- **VTODO is the superset spine:** full RRULE ⊇ patternedRecurrence; multi-`VALARM` ⊇ single
  reminder; PRIORITY 0–9 ⊇ low/normal/high; PERCENT-COMPLETE (no vendor equivalent); `RELATED-TO`
  arbitrary tree ⊇ both single-level models.
- **Hierarchy is irreducible (carry all three):** VTODO `RELATED-TO` tree / Google parent+position
  single-level *full* subtasks / MS `checklistItems` single-level *degenerate* items. Not isomorphic.
- **todoTask adds beyond VTODO:** status `waitingOnOthers`/`deferred`; `linkedResources`; lightweight
  `checklistItems`; HTML body; `importance` enum.
- **Google Tasks adds:** `position` (sibling order), `assignmentInfo`, typed `links` (output-only).
- **Due precision differs:** VTODO date|datetime; MS datetime+zone; Google date-only → canon needs a
  precision flag.

---

## 4. Canon-targeting summary (per domain)

| Domain | Superset anchor | Augment with | Carry-verbatim / extras |
|---|---|---|---|
| calendar | iCal/RFC5545 (recurrence as text) | Google+MS rich fields (online meeting, sensitivity, showAs, eventType, working-location, response tracking, multi-location) | THISANDFUTURE series-split; provider-extras bag (MAPI/extendedProperties); IANA zone verbatim |
| contacts | vCard4 ∪ Google People | (People already covers yomi/fileAs/assistant/manager/relations/typed multi-values) | provider-extras bag (MAPI ext props, changeKey, folder/immutable id) |
| todo | VTODO/RFC5545 | extended status, linkedResources, checklistItems, HTML body, due-precision flag, sibling order | three hierarchy representations; provider-extras bag (extensions) |

The canon **schema** doc (follow-on) turns this table into concrete field definitions, property
catalogues, and the differ/merger field lists. Live API calls, if needed, belong to that step to
validate edge cases against real payloads.

---

## 5. ADDENDUM 2026-08-23 — Beta horizon diff (what GA will bring)

Diffed `graph-rest-beta` resource schemas against §1–§3 (v1.0 tables) ahead of EEE Phase 4.
Campaign invariant 4 still applies: **implement against GA only**; this section is spine-v2
planning input so the widening is a designed `canon2` node, not a surprise.

### 5.1 event — two structural additions

1. **`exceptionOccurrences`** (nav, `[event]`, `$select`/`$expand` on series masters): structured
   access to exception records directly from the master. If/when GA, this dissolves most of the
   O57(g) problem — no calendarView/instances walking needed to collect overrides; cancelled
   instances remain via `cancelledOccurrences`. Until then: instances/calendarView walk stands.
2. **`occurrenceId` documented**: format `OID.{seriesMasterId}.{occurrence-start-date}` (date in
   the range's `recurrenceTimeZone`). Stable addressing of any occurrence incl. modified/cancelled
   — better than start-time keying for override matching. Already present in v1.0 payloads
   (observed live); only the documentation is new.

Also documented in beta: `Prefer: IdType="ImmutableId"` (id stability across container moves —
relevant to providerExtras id handling).

### 5.2 contact — beta closes nearly every gap §2.2 catalogued

| v1.0 limitation (§2.2) | Beta replacement | Canon home |
|---|---|---|
| untyped positional emails (`primaryEmailAddress`…) | `emailAddresses: [typedEmailAddress]` (type + label) | `emails[].{type,label,primary}` — **already modeled** |
| fixed phone buckets (home/business/mobile) | `phones: [phone]` typed collection | `phones[]` — already modeled |
| 3 fixed address slots | `postalAddresses: [physicalAddress]` collection | `addresses[]` — already modeled |
| single `businessHomePage` | `websites: [website]` typed collection | `urls[]` — already modeled |
| no anniversary | `weddingAnniversary: Date` | `anniversary {date, hasYear}` — already modeled |
| no gender | `gender: String` | `gender {value,…}` — already modeled |
| — | `flag: followupFlag` (new) | no home → providerExtras on promote |

Verdict: **`contacts+canon` needs zero widening for Graph-beta contacts** — the union design
already covers it; the beta edge would be near-lossless promote. Strong validation of the
superset approach.

### 5.3 todoTask — no meaningful delta

Beta ≈ v1.0 plus typed `taskFileAttachment` nav (§3.2 already current). Stable domain; nothing to
plan around.

### 5.4 uid/iCalUId series semantics — verified against beta AND v1.0 (2026-08-23, corrected)

Initial analysis of this section claimed both doc versions misdocument uid stability. **That
claim was wrong** — the comparison had crossed series (the mailbox held four same-named series
from repeated sweep runs, matched by subject). Redone keyed by explicit series id, on BOTH
`/v1.0` and `/beta` against the same live series; results identical on both surfaces:

- **`uid` IS series-stable**: master, every plain occurrence, and every exception share one
  byte-identical uid. Beta's doc is correct.
- **`iCalUId` differs per occurrence record** (and from that record's own uid) in exactly the
  MAPI-GOID instance-date byte run — beta's doc is correct.
- `uid == iCalUId` holds only on series masters and single instances (as in our first captures,
  which motivated this investigation).

Identity guidance (unchanged in substance, now on solid ground): **anchor series identity on
`uid`; never use `iCalUId` as a series key** — it is per-occurrence. When matching against
CalDAV-side iCal UIDs, expect Exchange to expose per-occurrence iCalUIds for instances.
Methodology note for future corpus analysis: sweep scenarios that create same-named objects
across runs MUST be matched by id, never by subject.
