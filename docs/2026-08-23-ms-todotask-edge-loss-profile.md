# Microsoft Graph `todoTask` ⇄ todo/canon — declared loss profile (EEE Phase 3)

**Date:** 2026-08-23
**Status:** Declared before stage code (campaign invariant 2). Divergence
between declared and actual loss is a RED test. Reference inputs:
vendor-api-shapes-reference §3.2 (+ §3.3, §5.3 "no meaningful beta
delta"); NO live fixture yet (/me/todo was outside the Phase-0 Graph
corpus sweep — fixture-promotion slot deferred until capture); canon
catalogue `src/todo/todocanonproperties.cpp`; recurrence machinery reused
from Phase 7.B (`recurrencepatternconverter`).

## Shapes and registration

- Peer shape `{todo, ms-todotask}` — one Graph v1.0 `todoTask` resource
  as wire-shape JSON.
- Catalogue: `makeMsTodoTaskCatalogue()`.
- Edges: `ms-todotask → canon` promote (lossless by construction),
  `canon → ms-todotask` demote (this profile).

## Carrier channel

Same open-extensions channel as the ms-contact edge
(`docs/2026-08-23-ms-contact-edge-loss-profile.md`, §Carrier channel):
`extensions[]` row with `extensionName: "kalburator.canon"` carrying
`x-canon-*` string-typed props. Used for (a) unhandled canon props and
(b) the recurrence cannot-represent rulings from
`rruleLinesToPatternedRecurrence().carriedLines` + exdates (Reversible,
mirroring the 7.B event edge's carried-set discipline). Write-back
survival UNVERIFIED until a live drill — O61(e) suspicion class.

## Nav collections = transport, not edge scope

`checklistItems`, `linkedResources`, `attachments` are separate-endpoint
nav collections, NOT part of the item payload (like list envelopes).
Canon `checklistItems`/`linkedResources` properties exist for when a
backend fetches/expands them; at THIS edge they are out of scope
(effectively Dropped in the item-only round trip). Declared here so the
differ/merger never expects them from this encoding.

## Per-property declarations — `canon → ms-todotask`

| Canon property | LossKind | Notes |
|---|---|---|
| `uid` | lossless | ⇄ `id` (changes on list move by default — O61(f) class anchor). Copy kept in `providerExtras.msgraph`. |
| `summary` | lossless | ⇄ title. |
| `description` / `descriptionHtml` | Simplified | ⇄ body {content, contentType text/html}: html ⇒ descriptionHtml, text ⇒ description; demote prefers descriptionHtml (contentType html) else description (text). |
| `status` | lossless | String pass-through — canon holds the full taskStatus vocabulary incl. waitingOnOthers/deferred (superset anchor §4). |
| `priority` | Degraded | importance enum ⇄ VTODO priority: low→9, normal→5, high→1; demote thresholds: ≥8 low, 4–7 normal, ≤3 high. |
| `due` | Simplified | dueDateTime {dateTime, dateTimeTimeZone} ⇄ due Json {dateTime, tz}; allDay/date-only input degrades to midnight UTC. |
| `start` | Simplified | startDateTime ⇄ start Json, same form as due. |
| `completed` | Simplified | completedDateTime.dateTime ⇄ completed ISO string (zone dropped to UTC form). |
| `categories` | lossless | StringList ⇄ categories[]. |
| `recurrence` | Simplified (+ Reversible carries) | patternedRecurrence ⇄ RFC5545 lines via RecurrencePattern (lossless MS→RFC5545); demote carries cannot-represent rulings + EXDATEs via the extension carrier (byte-equal re-promote, 7.B discipline). |
| `alarms` | Simplified | single reminder only: alarms[0] {"reminder": <reminderDateTime verbatim>, "isReminderOn": bool}; multi-alarm canon loses alarms[1..] on this target. |
| `percentComplete`, `relatedTo`, `parentUid`, `sortOrder`, `location`, `geo` | Reversible (carriers) | no todoTask home → `kalburator.canon` open extension, `x-canon-*`. |
| `checklistItems`, `linkedResources` | **Dropped** | separate-endpoint nav collections — transport, not item payload (see above). |
| MS-only: `@odata.etag`, `bodyLastModifiedDateTime`, `createdDateTime`, `lastModifiedDateTime`, `hasAttachments`, `isReminderOn` (when no alarm row), unknown fields | transport/extras | verbatim in `providerExtras["msgraph"]`; demote re-emits minus rebuilt keys. |
| cross-domain fields | Dropped | — |

## Verification plan

1. Unit suite `tst_ms_todotask_canon_edge`: rich hand-built promote,
   declared-vs-actual demote walk (importance thresholds, body content
   type split, recurrence carry), C→MS→C byte-equal identity for the
   representable+carrier set, registry inspection. Fixture-promotion slot
   deferred until a /me/todo corpus capture lands under
   tests/fixtures/vendor/microsoft/.
2. Live checkpoint deferred (invariant 6) alongside the Graph write-path
   drill; carrier survival is the specific question (O61(e) class).
