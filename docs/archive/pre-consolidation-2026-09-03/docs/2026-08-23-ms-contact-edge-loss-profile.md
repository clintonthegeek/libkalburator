# Microsoft Graph `contact` ⇄ contacts/canon — declared loss profile (EEE Phase 3)

**Date:** 2026-08-23
**Status:** Declared before stage code (campaign invariant 2). Divergence
between declared and actual loss is a RED test. Reference inputs:
vendor-api-shapes-reference §2.2 (+ §5.2 beta horizon); live-corpus fixture
`tests/fixtures/vendor/microsoft/contacts-listing.json` (sanitized);
canon catalogue `src/contacts/contactscanonproperties.cpp`; template: the
google-person profile (`docs/2026-08-23-google-person-edge-loss-profile.md`).

## Shapes and registration

- Peer shape `{contacts, ms-contact}` — one Graph v1.0 `contact` resource
  as wire-shape JSON (list envelopes / `@odata.nextLink` belong to
  transport).
- Catalogue: `makeMsContactCatalogue()` (the Graph field set the stages
  read/write).
- Edges: `ms-contact → canon` promote (lossless by construction),
  `canon → ms-contact` demote (this profile).

## Carrier channel

Graph v1.0 `contact` has **no inline free-form scalar bag** among its GA
properties; the only writable extension point is **open extensions**
(`extensions` nav collection, creatable alongside the resource):

    extensions: [{"@odata.type": "microsoft.graph.openTypeExtension",
                  "extensionName": "kalburator.canon",
                  "x-canon-<kebab>": <string-typed value>, ...}]

Same `x-canon-*` kebab + string-typed discipline as every other carrier
channel. LIVE-WIRE CAVEAT: extension write-back/survival semantics are
UNVERIFIED until the Phase 3 checkpoint — same suspicion class as O61(e)
(carriers did NOT survive creates on consumer Outlook.com for events).
Until verified, treat every carrier-routed ruling as offline-Reversible
only; backend write paths must prefer PATCH over re-create.

**Standing amendment (O66 + correction, 2026-08-24):** carrier survival
VERIFIED live — via nav `POST .../contacts/{id}/extensions` (NOT
PATCH-borne extensions; NOT inline at create) + collection-level
`$expand=extensions($filter=Id eq 'Microsoft.OutlookServices.OpenTypeExtension.kalburator.canon')`.
The Reversible rulings below are live-workable under that protocol.
Supersedes the "prefer PATCH" advice above: nav POST, then re-read — never
trust a create echo. Additional transport finding (O66(f)): consumer
GET-by-id is flaky/broken on this mailbox class — drive reads/deletes
through listings/delta/$expand (our fixture shape is already correct).

## Per-property declarations — `canon → ms-contact`

| Canon property | LossKind | Notes |
|---|---|---|
| `uid` | lossless | ⇄ `id` (per-account anchor, O61(f) class; moves folder ⇒ may change unless ImmutableId prefer header). No duplicate id copy in extras — uid is the single anchor so C→MS→C stays byte-equal. |
| `names` | Simplified | canon ARRAY collapses onto the ONE flat Graph name: names[0] {formatted→displayName, given→givenName, family→surname, middle→middleName, prefix→title, suffix→generation, phoneticGiven→yomiGivenName, phoneticFamily→yomiSurname, fileAs→fileAs}. Additional name entries have no home → carried. |
| `nicknames` | Simplified | first value ⇄ `nickName` scalar; additional entries carried. |
| `emails` | Simplified/Degraded | [{value, name?, primary?}] ⇄ emailAddresses[{address,name}]; primaryEmailAddress match ⇒ primary flag. Positional secondary/tertiary typing has no per-row home → Degraded. |
| `phones` | Simplified | typed rows collapse into the fixed buckets: type home→homePhones[], work→businessPhones[], mobile→mobilePhone (single). Other types carried. |
| `addresses` | Simplified | [{street, city, region→state, country→countryOrRegion, postalCode, type}] ⇄ homeAddress/businessAddress/otherAddress by type (home/work/other); only 3 slots, others carried; no geo anywhere. |
| `organizations` | Simplified | org[0] {name←companyName, title←jobTitle, department, location←officeLocation}; additional orgs carried. |
| `occupations` | Simplified | StringList ⇄ `profession` scalar (first value). |
| `urls` | Simplified | first entry ⇄ `businessHomePage` (untyped, single). |
| `relations` | Simplified | name-only rows: {person, type spouse/manager/assistant} ⇄ spouseName/manager/assistantName; type child rows ⇄ children[] (names only). |
| `birthday` | Simplified | canon {dateTime:<wire>} ⇄ birthday DateTimeOffset VERBATIM string (precision preserved through the stash form; Google-style {date:{y,m,d}} canon input degrades to midnight). |
| `notes` | lossless | ⇄ personalNotes string. |
| `categories` | lossless | StringList ⇄ categories[]. |
| `imClients` | Simplified | [{username}] ⇄ imAddresses StringList (protocol/type dropped). |
| `gender`, `anniversary`, `significantDates`, `timeZone`, `languages`, `interests`, `skills`, `calendarUrls`, `sipAddresses`, `memberships`, `externalIds` | Reversible (carriers) | no GA `contact` home → `kalburator.canon` open extension, `x-canon-*`. |
| `photos` | Dropped | photo is a fetch-only nav resource, never inline payload. |
| Graph-only: `@odata.etag`, `changeKey`, `parentFolderId`, `createdDateTime`, `lastModifiedDateTime`, `initials`, unknown fields | transport/extras | verbatim in `providerExtras["msgraph"]`; demote re-emits minus rebuilt keys. |
| cross-domain fields | Dropped | — |

Everything unmapped on promote lands verbatim in
`providerExtras["msgraph"]`; demote re-emits the stash minus rebuilt keys.
Unhandled canon props ride the open-extension carrier (never silently
dropped), except `photos`.

## Verification plan

1. Unit suite `tst_ms_contact_canon_edge`: promote from a rich hand-built
   wire object modeled on reference §2.2 + the committed fixture's shapes;
   declared-vs-actual demote walk (flat-name collapse, bucket collapse,
   carriers); C→MS→C byte-equal identity for the lossless+carrier set;
   registry inspection; committed-fixture promotion (every connection in
   contacts-listing.json promotes cleanly, incl. the null-vs-empty-string
   variance — O60 rule).
2. Live checkpoint deferred (invariant 6) until the Graph write-path drill;
   carrier survival is the specific question (O61(e) class).
