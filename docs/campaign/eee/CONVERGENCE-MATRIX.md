# EEE Convergence Matrix

**GENERATED artifact** â regenerate with `tools/matrixgen` and
commit alongside any change that grows or re-rules a stock-shape
`edges()` list. Enforced byte-for-byte by
`tst_gm_pipeline_convergence::committedMatrixMatchesGenerated`
(FINDINGS O63 discipline applied to the ledger).

Per-property ledger of every **declared** loss on every
canonâvendor demote edge. A canon property absent from an
edge's rows survives that crossing unchanged. Loss kinds:
`Dropped` < `Simplified` < `Degraded` < `Reversible` in
increasing fidelity (Reversible = carried verbatim in a vendor
extension channel).

## Carrier-survival verdicts (O66 + correction, 2026-08-24)

Live drills on consumer accounts settled the O61(e) question
per channel. `Reversible` rulings split three ways:

- **live-Reversible** â Google People `clientData`
  (create + fresh read proven), Google Calendar
  `extendedProperties.private` (carriers survive consumer
  creates byte-exact â Phase-2 checkpoint and A4 replay both
  proven), and both Graph open-extension channels (`contact`,
  `todoTask`), which SURVIVE when spoken to properly:
  nav-property `POST .../{id}/extensions` â never PATCH-borne,
  never inline-at-create (todoTask inline-create is echoed but
  NOT persisted) â then collection-level
  `$expand=extensions($filter=Id eq '<full-id>')`; the Outlook
  full-id prefix is `Microsoft.OutlookServices.
  OpenTypeExtension.*`.
- **offline-only** â MS Graph event
  `singleValueExtendedProperties` (O61(e): silently stripped
  on consumer creates; PATCH-in-place works).
- **no channel** â Google Tasks has no extension point; all
  its non-carried properties remain `Dropped` (O66(c)
  corpus-confirms).

Backend rules (binding): nav POSTs only; filtered expand with
the RETURNED full id; re-READ after write â never trust a
create echo. Consumer contact GET-by-id is unreliable â drive
reads through listings/delta/$expand.

## calendar

### Edge inventory

- `canon → canon`
- `ical → canon`
- `canon → ical` (declared lossy) (kind-scoped: default, vjournal, vtodo)
- `org-ical → canon`
- `canon → org-ical` (declared lossy)
- `google-event → canon`
- `canon → google-event` (declared lossy)
- `ms-event → canon`
- `canon → ms-event` (declared lossy)

### canon → ical (default)

| Property | LossKind |
|---|---|
| allowNewTimeProposals | Reversible |
| classification | Degraded |
| descriptionHtml | Reversible |
| eventType | Dropped |
| freeBusyStatus | Reversible |
| geo | Dropped |
| guestsCanInviteOthers | Reversible |
| guestsCanModify | Reversible |
| guestsCanSeeOtherGuests | Reversible |
| hideAttendees | Reversible |
| locations | Simplified |
| locked | Reversible |
| onlineMeeting | Dropped |
| privateCopy | Reversible |
| providerExtrasDigest | Dropped |
| requestStatus | Dropped |
| resources | Dropped |
| responseRequested | Reversible |
| typedProperties | Dropped |

### canon → ical (vjournal)

| Property | LossKind |
|---|---|
| providerExtrasDigest | Dropped |
| recurrenceRange | Degraded |
| requestStatus | Dropped |

### canon → ical (vtodo)

| Property | LossKind |
|---|---|
| geo | Dropped |
| providerExtrasDigest | Dropped |
| requestStatus | Dropped |
| resources | Dropped |

### canon → org-ical

| Property | LossKind |
|---|---|
| recurrence | Simplified |

### canon → google-event

| Property | LossKind |
|---|---|
| alarms | Simplified |
| allowNewTimeProposals | Reversible |
| attendees | Simplified |
| categories | Reversible |
| classification | Degraded |
| color | Degraded |
| completed | Dropped |
| descriptionHtml | Reversible |
| due | Dropped |
| end | Reversible |
| eventType | Degraded |
| freeBusyStatus | Degraded |
| geo | Dropped |
| hideAttendees | Reversible |
| locations | Simplified |
| onlineMeeting | Degraded |
| percentComplete | Dropped |
| priority | Reversible |
| providerExtrasDigest | Dropped |
| relatedTo | Dropped |
| responseRequested | Reversible |
| start | Reversible |
| timeTransparency | Degraded |
| typedProperties | Reversible |
| url | Simplified |

### canon → ms-event

| Property | LossKind |
|---|---|
| alarms | Simplified |
| allDay | Degraded |
| attachments | Simplified |
| attendees | Simplified |
| categories | Reversible |
| classification | Degraded |
| color | Dropped |
| completed | Dropped |
| due | Dropped |
| end | Degraded |
| eventType | Degraded |
| freeBusyStatus | Degraded |
| geo | Dropped |
| guestsCanInviteOthers | Reversible |
| guestsCanModify | Reversible |
| guestsCanSeeOtherGuests | Reversible |
| location | Degraded |
| locations | Degraded |
| locked | Reversible |
| percentComplete | Dropped |
| priority | Simplified |
| privateCopy | Reversible |
| providerExtrasDigest | Dropped |
| recurrence | Simplified |
| recurrenceId | Degraded |
| recurrenceRange | Degraded |
| relatedTo | Dropped |
| responseStatus | Degraded |
| sequence | Simplified |
| start | Degraded |
| status | Degraded |
| typedProperties | Reversible |
| url | Reversible |

## contacts

### Edge inventory

- `canon → canon`
- `vcard4 → canon`
- `canon → vcard4` (declared lossy)
- `vcard3 → vcard4`
- `vcard4 → vcard3` (declared lossy)
- `google-person → canon`
- `canon → google-person` (declared lossy)
- `ms-contact → canon`
- `canon → ms-contact` (declared lossy)

### canon → vcard4

| Property | LossKind |
|---|---|
| calendarUrls | Reversible |
| externalIds | Reversible |
| interests | Dropped |
| occupations | Dropped |
| providerExtrasDigest | Dropped |
| sipAddresses | Reversible |
| skills | Dropped |

### vcard4 → vcard3

| Property | LossKind |
|---|---|
| gender | Dropped |
| kind | Dropped |
| lang | Dropped |
| member | Dropped |

### canon → google-person

| Property | LossKind |
|---|---|
| addresses | Simplified |
| anniversary | Reversible |
| birthday | Simplified |
| categories | Reversible |
| gender | Degraded |
| interests | Simplified |
| languages | Simplified |
| memberships | Simplified |
| names | Simplified |
| nicknames | Simplified |
| notes | Simplified |
| occupations | Simplified |
| organizations | Simplified |
| photos | Simplified |
| providerExtrasDigest | Dropped |
| significantDates | Reversible |
| sipAddresses | Simplified |
| skills | Simplified |
| timeZone | Reversible |

### canon → ms-contact

| Property | LossKind |
|---|---|
| addresses | Simplified |
| anniversary | Reversible |
| birthday | Simplified |
| calendarUrls | Reversible |
| emails | Simplified |
| externalIds | Reversible |
| gender | Reversible |
| imClients | Simplified |
| interests | Reversible |
| languages | Reversible |
| memberships | Reversible |
| names | Simplified |
| nicknames | Simplified |
| occupations | Simplified |
| organizations | Simplified |
| phones | Simplified |
| photos | Dropped |
| providerExtrasDigest | Dropped |
| relations | Simplified |
| significantDates | Reversible |
| sipAddresses | Reversible |
| skills | Reversible |
| timeZone | Reversible |
| urls | Simplified |

## todo

### Edge inventory

- `canon → canon`
- `ical-vtodo → canon`
- `canon → ical-vtodo` (declared lossy)
- `ical-vtodo → todotxt` (declared lossy)
- `todotxt → ical-vtodo`
- `google-task → canon`
- `canon → google-task` (declared lossy)
- `ms-todotask → canon`
- `canon → ms-todotask` (declared lossy)

### canon → ical-vtodo

| Property | LossKind |
|---|---|
| checklistItems | Reversible |
| completionAnchor | Reversible |
| descriptionHtml | Reversible |
| geo | Dropped |
| linkedResources | Dropped |
| parentUid | Reversible |
| providerExtrasDigest | Dropped |
| recurrenceRange | Degraded |
| requestStatus | Dropped |
| resources | Dropped |
| seriesSplitOf | Reversible |
| sortOrder | Reversible |
| status | Degraded |

### ical-vtodo → todotxt

| Property | LossKind |
|---|---|
| alarms | Dropped |
| attachments | Dropped |
| attendees | Dropped |
| customproperties | Dropped |
| description | Dropped |
| rrule | Dropped |

### canon → google-task

| Property | LossKind |
|---|---|
| alarms | Dropped |
| attachments | Dropped |
| attendees | Dropped |
| categories | Dropped |
| checklistItems | Dropped |
| classification | Dropped |
| color | Dropped |
| comments | Dropped |
| completionAnchor | Dropped |
| contacts | Dropped |
| descriptionHtml | Dropped |
| due | Degraded |
| geo | Dropped |
| location | Dropped |
| organizer | Dropped |
| percentComplete | Dropped |
| priority | Dropped |
| providerExtrasDigest | Dropped |
| recurrence | Dropped |
| relatedTo | Dropped |
| resources | Dropped |
| sequence | Dropped |
| seriesSplitOf | Dropped |
| start | Dropped |
| status | Simplified |
| url | Dropped |

### canon → ms-todotask

| Property | LossKind |
|---|---|
| alarms | Simplified |
| attachments | Reversible |
| attendees | Reversible |
| checklistItems | Dropped |
| classification | Reversible |
| color | Reversible |
| comments | Reversible |
| completed | Simplified |
| completionAnchor | Reversible |
| contacts | Reversible |
| description | Simplified |
| descriptionHtml | Simplified |
| due | Simplified |
| geo | Reversible |
| linkedResources | Dropped |
| location | Reversible |
| organizer | Reversible |
| parentUid | Reversible |
| percentComplete | Reversible |
| priority | Degraded |
| providerExtrasDigest | Dropped |
| recurrence | Reversible |
| relatedTo | Reversible |
| resources | Reversible |
| sequence | Reversible |
| seriesSplitOf | Reversible |
| sortOrder | Reversible |
| start | Simplified |
| url | Reversible |

