# Vendor REST API wire notes — Google & Microsoft (EEE campaign consolidation)

**Status:** living reference. Consolidates every wire-level truth learned
live during the EEE campaign (FINDINGS O57–O67, 2026-08-23 → 2026-08-25),
organized by vendor and behavior instead of chronology. FINDINGS.md stays
the evidence record; this page is the browsable protocol knowledge.
Update BOTH in the same commit when a new O-entry lands.

Evidence: machine-local captures (`msgraph/captured/`, `google/captured/`),
committed sanitized fixtures under `tests/fixtures/vendor/{google,microsoft}/`,
and the roundtrip harnesses `tools/msroundtrip` / `tools/groundtrip`.

---

## 1. Google Calendar API v3 — events

### Create (events.insert)

| Behavior | Detail | Finding |
|---|---|---|
| **Read-only fields REJECTED** | `created` / `updated` in an insert body ⇒ HTTP 400 Bad Request (generic message). Demote output must strip before create. | O67(b)(1) |
| **Client transport id REJECTED** | A supplied top-level `id` ⇒ HTTP 400 "Invalid resource id value" regardless of charset conformance. Server mints its own id; the iCalUID anchor is what honors client values. Create seams must strip `id` too. | O68 |
| **Organizer rewritten** | Server replaces any supplied organizer with the AUTHENTICATED account. Source-organizer identity is not preservable through a create. | O67(b)(2) |
| **iCalUID honored** | Client-supplied `iCalUID` survives insert — cross-vendor identity anchors are preservable (unlike Graph). Enables G→C→G byte-equal identity. | O67(b)(4), Phase-2 checkpoint |
| Read-back default echoes | Freshly created events read back with `status:"confirmed"`, `eventType:"default"` even when omitted at create; `transparency` non-default values survive, default (opaque) collapses to absent. | O67(b)(3) |
| Attendee normalization on read-back | email local-parts LOWERCASED server-side; attendee `displayName` DROPPED when it equals the email string. | O67(b)(3) |

### Reads

- `start`/`end`: offset-local authored forms (`-04:00`) canonicalize to UTC
  `Z`-form; `timeZone` (IANA) preserved verbatim. All-day = `{date}` only.
- `reminders.overrides[]` key is `method` (NOT the docs-implied
  `reminderMethod`). O59.
- `extendedProperties.private` values are STRING-typed (carriers are
  JSON-stringified). O59.
- `eventLabelId` exists in the wire schema (supersedes index-based
  `colorId` semantics); carried verbatim via providerExtras. O59.
- Recurrence field is already verbatim RFC5545 lines — promote/demote
  byte-faithful, no parser (the asymmetry that makes the Graph edge hard).

### Carrier channel

`extendedProperties.private["x-canon-*"]` — **live-proven Reversible**
(create + fresh read, consumer account, Phase-2 checkpoint). The ONLY
calendar-domain channel with that verdict; MS event SVEP is offline-only
by contrast (O61(e)).

---

## 2. Google Tasks API

- **No extension point of any kind** — no extendedProperties, no
  clientData, no custom scalars. Every canon property without a Task home
  is honestly `Dropped`. O66(c) corpus-confirms: UI-recurrent tasks carry
  NO recurrence field whatsoever on the wire.
- `due` is date-only midnight-UTC `.000Z`; `position` strings sort
  lexicographically ("00000000000000000002"); `parent`+`position` cover
  ONE subtask level only.
- OAuth: the Tasks API must be ENABLED per GCP project even with the scope
  granted — `accessNotConfigured` 403 until console activation. O66(e).
- List ids may contain colons (`MTYw…NjU6MDox`) — sanitize/id-mint rules
  must accept colon-suffixed base64ish forms.

---

## 3. Microsoft Graph v1.0 — consumer Outlook.com mailbox

### Events (CRUD)

| Behavior | Detail | Finding |
|---|---|---|
| **Fresh uid minted on create** | Client-supplied `uid`/`iCalUId` IGNORED — Graph assigns its own GUID. Opposite of Google; anchor continuity requires post-create re-read + remap. | O67(c)(1) |
| Organizer rewritten | Same rule as Google: authenticating account becomes organizer. | O67(c)(2) |
| Body converted | Authored text `body` comes back as HTML (`contentType` rewritten). | O67(c)(3) |
| Teams auto-provisioning | Mailbox-default online-meeting policy injects `location` + `onlineMeeting.joinUrl` that were never sent. | O67(c)(4) |
| Default alarm injected | `reminderMinutesBeforeStart` default appears as a canon alarm on read-back. | O67(c)(4) |
| Defaults echoed | importance→priority, sensitivity→classification, showAs→freeBusyStatus server defaults surface on re-promote. | O67(c)(4) |
| SVEP carriers STRIPPED | `singleValueExtendedProperties` do not survive consumer creates — carrier-only data must ride PATCH (never delete+re-create). | O61(e) |
| createdDateTime accepted | Unlike Google's rejection of `created`/`updated`, Graph tolerates its timestamp equivalents on create. Vendor asymmetry. | O67 |

### Wire-shape truths (reads)

- `type` is reconstructable structurally but often absent from listings;
  canon never stores topology (demote re-derives it).
- Graph mixes null and ABSENT representations for "no value"
  (`recurrence: null` vs key absent) — normalizers must treat as equal.
- Sentinel family: year-1 `.NET` datetimes (`0001-01-01`) appear as
  zero-values everywhere — attendee `status.time`, `range.endDate`,
  todoTask `dayOfMonth`/`month`/`numberOfOccurrences`. Normalize ABSENT.
- Type name is `dateTimeTimeZone` but each instance keys its zone as plain
  `timeZone`. O63.

### Contacts (consumer)

- **GET-by-id flaky/broken**: recently-created contacts 404 on
  `/me/contacts/{id}` persistently while appearing fine in listings;
  DELETE-by-id also 404s. Drive ALL reads/deletes through
  listings / delta / `$expand`. O66(f).
- Open-extension carriers SURVIVE via nav `POST …/contacts/{id}/extensions`
  + collection-level `$expand=extensions($filter=Id eq '<full-id>')`.
  PATCH-borne extensions break (500s); wrong prefix in filter ⇒ HTTP 500. O66 correction.

### Todo (todoTask)

- Inline-create extensions are a WIRE-LIE: echoed in the POST response,
  NOT persisted ($expand null on v1.0 AND beta). Never trust a create
  echo — re-read after write. O66 correction.
- Carriers survive via nav `POST …/tasks/{id}/extensions` + filtered expand. O66 correction.
- Creating WITH recurrence REQUIRES `dueDateTime` (undocumented 400);
  server then REWRITES your dueDateTime to align with the pattern
  (posted 2026-08-25T10:00 → stored next-occurrence midnight). O66(b).

### URL/transport quirks

- Outlook extension full-id prefix: `Microsoft.OutlookServices.OpenTypeExtension.*`
  (NOT `microsoft.graph.openTypeExtension.*`).
- Contact/item ids ending `=` must NOT be URL-encoded in paths (encoded
  form ⇒ 404); but `$expand` filter values need `%27` quoting.
- `@odata.context` URLs embed internal Exchange identity
  (`outlook_<hex>@outlook.com`) AND consumer `%40`-encoded addresses —
  sanitizers must catch both shapes.

---

## 4. Cross-vendor invariants (observed live)

1. **Both vendors rewrite organizer** to the authenticated account on
   create — source-organizer identity never survives a create anywhere.
2. **uid continuity is vendor-asymmetric**: Google honors client anchors;
   Graph mints fresh ones. Any cross-vendor sync needs post-create
   mapping on the Graph leg.
3. **Invitation propagation is real**: a probe created on account A
   appears on account B as an attendee-invite copy (observed gmail→hotmail
   alias within one sweep cycle) — cleanup tooling must sweep both sides.
4. Carrier survival splits three ways (matrix preamble is canonical):
   live-Reversible (Google Calendar extendedProperties.private; People
   clientData; Graph contact/todoTask open extensions via nav POSTs),
   offline-only (MS event SVEP only), no-channel (Tasks API).

---

## 5. Tooling notes (lab CLIs + fixtures)

- `googlecli capture <path>`: path MUST begin with `/` — the tool prepends
  `calendar/v3`; without the slash you get `/calendar/v3<path>` 404s.
- `googlecli create event [cal] <file>`: positional calendarId optional;
  stdout is human-readable text ("Created:\n id = …"), NOT JSON — parse
  accordingly in scripts.
- `sweep-clean [tag]`: bare form deletes EVERY subject starting `CORPUS:`;
  tagged form deletes only that run's probes. Always sweep both vendors
  after drills (see invariant 3).
- `ms-roundtrip` / `g-roundtrip`: exit 0 iff every wire′ diff is in the
  declared-normalization set. That set and the matching loss-profile doc
  move TOGETHER, never silently (O63 discipline applied to declarations).
- `make-fixtures.py` (both CLIs): pick order preserves regeneration byte-
  stability for pre-existing labels; scrub rules are payload-context
  scoped (`tasks#*` kinds, `/todo/` @odata.context) so older fixtures stay
  stable; deepscrub must handle `%40`-encoded consumer emails inside Graph
  URLs.
