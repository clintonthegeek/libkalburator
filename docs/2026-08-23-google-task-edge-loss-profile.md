# Google Tasks `Task` ⇄ todo/canon — declared loss profile (EEE Phase 3)

**Date:** 2026-08-23
**Status:** Declared before stage code (campaign invariant 2). Divergence
between declared and actual loss is a RED test. Reference inputs:
vendor-api-shapes-reference §3.1 (+ §3.3 comparison); NO live fixture yet
(tasks were outside the Phase-0 Google corpus sweep — fixture-promotion
slot deferred until capture); canon catalogue
`src/todo/todocanonproperties.cpp`; template: the google-person profile.

## Shapes and registration

- Peer shape `{todo, google-task}` — one Tasks API `Task` resource as
  wire-shape JSON (list envelopes belong to transport).
- Catalogue: `makeGoogleTaskCatalogue()`.
- Edges: `google-task → canon` promote (lossless by construction),
  `canon → google-task` demote (this profile).

## Carrier channel

**None exists.** The Tasks API resource has no extension point of any
kind — no extendedProperties, no clientData, no custom scalars. Every
canon property without a Task home is declared **Dropped**, not carried.
(This is the honest ruling; inventing a stash channel would fork the
provider-extras bag.) Hierarchy note: `parent`+`position` cover ONE level
of full subtasks; deeper canon `relatedTo` trees degrade to the topmost
resolvable parent only.

O66(c) live-corpus check (2026-08-24) CONFIRMS this ruling: UI-recurrent
tasks carry no recurrence field of any kind on the wire; the Tasks API
remains extension-free. Dropped rulings stand.

## Per-property declarations — `canon → google-task`

| Canon property | LossKind | Notes |
|---|---|---|
| `uid` | lossless | ⇄ `id` (per-account anchor; O61(f) class). Copy kept in `providerExtras.google`. |
| `summary` | lossless | ⇄ title (≤1024 chars, vendor-enforced). |
| `description` | lossless | ⇄ notes (≤8192). |
| `status` | Simplified | canon vocabulary collapses to needsAction/completed; anything else → needsAction unless a completed timestamp exists (then completed). |
| `due` | Degraded | vendor discards time-of-day: canon due Json {date, allDay} ⇄ RFC3339 wire verbatim; {dateTime,tz} input degrades to its date part (original preserved in providerExtras on promote). |
| `completed` | lossless | ⇄ completed RFC3339 (string form both sides). |
| `lastModified` | transport | ⇄ updated (output-only on Google). |
| `parentUid` | Simplified | ⇄ parent id (one level only). |
| `sortOrder` | lossless | ⇄ position opaque lexicographic string (verbatim pass-through; set via move API, never hand-edited). |
| `percentComplete`, `priority`, `categories`, `start`, `recurrence`, `alarms`, `location`, `geo`, `checklistItems`, `relatedTo`, `descriptionHtml` | **Dropped** | no Task home and no carrier channel (see above). |
| Google-only: `kind`, `etag`, `deleted`, `hidden`, `links[]`, `webViewLink`, `selfLink`, `assignmentInfo`, unknown fields | transport/extras | verbatim in `providerExtras["google"]`; demote re-emits minus rebuilt keys. |
| cross-domain fields | Dropped | — |

## Verification plan

1. Unit suite `tst_google_task_canon_edge`: rich hand-built promote,
   declared-vs-actual demote walk, C→G→C byte-equal identity for the
   representable set, registry inspection. Fixture-promotion slot deferred
   until a tasks corpus capture lands in tests/fixtures/vendor/google/.
2. Live checkpoint deferred (invariant 6) until googlecli grows tasks
   verbs.
