# Google People `Person` ⇄ contacts/canon — declared loss profile (EEE Phase 3)

**Date:** 2026-08-23
**Status:** Declared before stage code (campaign invariant 2). Divergence
between declared and actual loss is a RED test. Reference inputs:
vendor-api-shapes-reference §2.1; live-corpus fixture
`tests/fixtures/vendor/google/contacts-connections.json` (72 connections,
sanitized); canon catalogue `src/contacts/contactscanonproperties.cpp`;
template: the Phase 2 google-event profile.

## Shapes and registration

- Peer shape `{contacts, google-person}` — one Google People API `Person`
  resource as wire-shape JSON (list envelopes belong to transport).
- Catalogue: `makeGooglePersonCatalogue()` (Google field set the stages
  read/write).
- Edges: `google-person → canon` promote (lossless by construction),
  `canon → google-person` demote (this profile).

## Carrier channel

Google People has **no free-form extension point** like Calendar's
extendedProperties — except **`clientData`** (repeated `{key, value}` rows,
app-namespaced). Carriers ride `clientData` rows:

    {"key": "x-canon-<kebab>", "value": "<string>"}

Same string-typed discipline as every other carrier channel. NOTE (live
wire caveat): clientData rows are returned by people.get
(personFields=clientData) but their write-back semantics on consumer
accounts are UNVERIFIED until the Phase 3 checkpoint — same suspicion class
as O61(e).

## Per-property declarations — `canon → google-person`

| Canon property | LossKind | Notes |
|---|---|---|
| `uid` | lossless | ⇄ `resourceName` (per-account anchor; NOT portable across accounts — Graph-uid lesson O61(f) applies one vendor over). |
| `names` | Simplified | canon holds an ARRAY of name objects; Google People also repeats names — map each canon entry {formatted→displayName, given, family, middle, prefix→honorificPrefix, suffix→honorificSuffix}. Phonetic canon keys (if present) → phonetic* twins. |
| `nicknames` | Simplified | [{value}] direct; canon extra keys dropped-from-pattern but carried. |
| `emails` | lossless | [{value, primary, type}] ⇄ [{value, type, metadata.primary}]; formattedType derived server-side → not stored. |
| `phones` | lossless | [{value, primary, type, canonicalForm}] ⇄ [{value, canonicalForm, type}]. |
| `addresses` | Simplified | [{street, city, region, postalCode, country, countryCode, type, formatted}] ⇄ [{streetAddress, city, region, postalCode, country, countryCode, type, formattedValue}]. |
| `organizations` | Simplified | [{name, title, department, symbol, domain, costCenter, current, startDate, endDate, location, phoneticName, fullTimeEquivalentMillipercent}] ⇄ same-named Google fields; date OBJECTS {y,m,d} pass as-is. |
| `urls` | lossless | [{value, primary?, type}] ⇄ [{value, type}]. |
| `relations` | lossless | [{person, type}] direct. |
| `birthday` | Simplified | canon SINGULAR json {date{y,m,d}|text} ⇄ first primary Google birthday; ALL birthdays stashed verbatim under providerExtras when >1 (re-emitted on demote). |
| `anniversary` / `significantDates` | Reversible | no Google home outside `events[]` → carried `x-canon-*`. |
| `gender` | Degraded | canon {value,...} ⇄ genders[] {value, formattedValue, addressMeAs}: value passes, extras preserved in-entry. |
| `notes` | Simplified | canon STRING ⇄ first biography {value}; additional biographies carried. |
| `photos` | Simplified | [{url, primary}] ⇄ [{url, metadata.primary}]; photo bytes are fetch-by-url on Google (never inline). |
| `categories` | carrier (Reversible) | no Google field → `x-canon-categories`. |
| `languages` | Simplified | StringList ⇄ locales[] {value}. |
| `timeZone` | carrier (Reversible) | no Person field → `x-canon-time-zone`. |
| `externalIds` | lossless | [{value, type, formattedType?}] direct. |
| `memberships` | Simplified | [{contactGroup{id/groupName}|domainMembership}] ⇄ contactGroupMemberships; domainMembership passes through. |
| `interests` / `skills` / `occupations` | Simplified | StringLists ⇄ interests[]/skills[]/occupations[] {value} rows. |
| `sipAddresses` | Simplified | StringList ⇄ [{value, type?}]: type lost unless carried. Declared Simplified. |
| `calendarUrls` | lossless | [{url, type?}] ⇄ [{value→url? no — canon key mapping below}] (canon entries use `value`+`type`; Google uses `url`+`type`). |
| `imClients` | Simplified | [{username, protocol?, type?}] ⇄ imClients[]. |
| `fileAs` (canon names[].fileAs if present) | lossless | ⇄ fileAses[] {value} (first row). |
| Google-only: `metadata`, `etag`, `coverPhotos`, `braggingRights`(dep), `residences`(dep), `ageRanges`(dep) | transport/extras | etag → providerExtras["google"]["etag"]; metadata.sources[0] → extras (updateTime is the concurrency token). |
| cross-domain fields (none defined yet for contacts) | Dropped | — |

Everything unmapped on promote lands verbatim in
`providerExtras["google"]` (nothing discarded); demote re-emits the extras
stash minus rebuilt keys. Unhandled canon props ride `clientData`
carriers (never silently dropped).

## Verification plan

1. Unit suite `tst_google_person_canon_edge`: promote from a rich
   hand-built wire object modeled on reference §2.1 + the committed
   fixture's shapes; declared-vs-actual walk; C→G→C byte-equal identity for
   the lossless+carrier set; registry inspection; committed-fixture
   promotion (all 72 connections promote cleanly).
2. Live checkpoint before any consumer sees the edge (invariant 6) —
   deferred until googlecli grows people write verbs.
