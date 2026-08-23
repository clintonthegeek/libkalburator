# Canon Domains & Cross-Format Conversion Readiness

**Date:** 2026-08-22
**Type:** Reference + evaluation. Explains the data domains and canonical formats the shape graph defines today, evaluates them, and assesses whether we are ready to begin testing cross-format conversions with consumers that exceed iCalendar/vCard.
**Grounded in:** `src/shape/` (registries, envelope, loss profile), the per-domain canon catalogues (`src/{calendar,todo,contacts,note,outline,blob}/`), the plugin contract (`src/plugin/`), the schema design (`docs/2026-05-23-canon-schema-design.md`), the vendor-shapes reference (`docs/2026-05-23-vendor-api-shapes-reference.md`), and the test suite state on `main` @ `bb88dcf` (v1.01).

---

## TL;DR

- **Six domains are registered:** `calendar`, `todo`, `contacts`, `note`, `outline`, `blob`. Three carry rich vendor-superset JSON canons (calendar/todo/contacts); two carry thin ones (note/outline); blob is identity-only.
- **The rich canons genuinely supersede iCal/vCard.** A consumer speaking only our canon JSON can represent data that iCal and vCard cannot hold (multi-location events, online-meeting payloads, typed multi-value contact fields with primary flags, checklist items, three coexisting task-hierarchy models, …). This is not aspirational — it is what the catalogues enumerate and what the differ/merger and baseline store operate on.
- **But every non-trivial transformation edge that exists today terminates in iCal or vCard** (plus todotxt/markdown/opml/org for their domains). **Zero vendor-JSON edges (Google, Microsoft Graph) are implemented.** The canon was designed against those APIs and reserves fields for them; nobody has written the stages.
- **Readiness verdict:** architecturally ready, practically not yet exercised. Starting cross-format testing with a new consumer is feasible *today* if that consumer is canon-JSON-native (the shape graph routes everything through canon anyway, so a JSON-speaking backend needs no new edge at all). Testing conversion to a third wire format requires implementing one `ShapeContribution` with bidirectional edges plus honest loss profiles — the pattern is well-trodden (five domains do it), but each new format is real work with its own loss-model decisions.

---

## 1. How domains and encodings are defined

There is no `Domain` enum. Domains are string-typed ids (`Shape::DomainId`, `Shape::EncodingId` in `src/shape/shape.h`) resolved through process-wide registries populated by plugins:

| Registry | Role | Location |
|---|---|---|
| `DomainRegistry` | `DomainId → DomainDefinition` map | `src/shape/domainregistry.h` |
| `TransformationRegistry` | hub-and-spoke per domain: shapes, property catalogues, canonical spine, transformation edges, freeze semantics | `src/shape/transformationregistry.h` |
| `DomainOperationsRegistry` | read/write record binding per domain | `src/shape/domainoperationsregistry.{h,cpp}` |

Domains are contributed by stock plugins via `registerStockPlugins()` (`src/plugin/stock_plugins.cpp`). Each `DomainDefinition` names its canonical shape and spine; each `ShapeContribution` registers peer shapes and bidirectional `TransformationEdge`s to the canon hub. Pipelines compile `from → canonical(domain) → to`; there are **no cross-domain edges** (`compile()` returns nullopt when `from.domain != to.domain` — deliberate, "no cross-domain in v1").

### The canon envelope (all JSON domains)

`CanonEnvelope` (`src/shape/canonenvelope.h`) defines the reserved structure:

```json
{ "_canon": { "domain": "calendar", "v": 1 [, "kind": "vevent"|"vtodo"|"vjournal"] },
  "uid": "…",
  "<domain properties…>": "…",
  "providerExtras": { "<namespace>": { …opaque… } } }
```

- Spine version is currently **1** everywhere (`kCanonVersion = 1`); authoritative identity is the `EncodingId` (e.g. `calendar+canon`); widening later means a new spine node (`calendar+canon2`) plus bridge edges, never an in-place mutation.
- `providerExtras` is a namespaced opaque bag (`x-ms-graph`, `x-google`, `x-ms-mapi`, `x-vcard`, `x-wp-palm`, …) carried verbatim and explicitly **not a conflict axis**. Unknown top-level keys are retained verbatim on round-trip (forward compatibility).

---

## 2. The domains, explained and evaluated

### 2.1 `calendar` — `calendar/canon`

Catalogue: `makeCalendarCanonCatalogue()`, `src/calendar/calendarcanonproperties.cpp`.

PropertyIds: `uid`*, `sequence`, `created`, `lastModified`, `summary`, `description`, `descriptionHtml`, `location`, `locations`, `status`, `classification`, `timeTransparency`, `freeBusyStatus`, `start`, `end`, `allDay`, `recurrence`, `recurrenceId`, `recurrenceRange`, `color`, `categories`, `url`, `organizer`, `attendees`, `responseRequested`, `priority`, `alarms`, `onlineMeeting`, `attachments`, `eventType`, `typedProperties`, `guestsCanModify`, `guestsCanInviteOthers`, `guestsCanSeeOtherGuests`, `allowNewTimeProposals`, `hideAttendees`, `locked`, `privateCopy`, plus the component-union keys `due`, `completed`, `percentComplete`, `relatedTo`, `geo`.

Notable structural decisions:

- **Per-kind union:** VEVENT/VTODO/VJOURNAL share `{calendar, canon}`, discriminated by `_canon.kind` with absent-kind ⇒ `vevent` byte-stable back-compat (dispatch at `src/calendar/icalcanonstages.cpp`). This is what the 2026-06-28 per-kind canon dispatch work shipped.
- **Time:** `start`/`end` are objects `{date?|dateTime?, tz (IANA verbatim), floating}` — zone kept per endpoint, floating time flagged rather than silently pinned.
- **Recurrence:** one opaque `StringList` of verbatim RFC5545 RRULE/RDATE/EXDATE lines. No canon code parses it; only a demote edge that needs structured recurrence (e.g. a future Microsoft stage) parses it locally.
- **Overrides** are separate records sharing `uid` with a `recurrenceId` object (the KCalendarCore model); `recurrenceRange = thisAndFuture` carries the series-split case that no wire format represents as a single object.

**Evaluation.** This is the strongest canon. It cleanly supersets iCal with the Google (`eventType`, `typedProperties`, guestsCan*, `onlineMeeting` conferenceData) and Microsoft (`locations[]`, `freeBusyStatus` oof/workingElsewhere via reversible X-prop, `classification: personal`, allowNewTimeProposals/hideAttendees/locked/privateCopy) extras identified in the vendor-shapes reference, with sensible loss classes assigned per field. Weaknesses: the union-with-`_canon.kind` approach means the calendar catalogue contains todo-ish keys (`due`, `percentComplete`, `relatedTo`) that a pure-events consumer must ignore; and several G/M fields (`eventType`, `typedProperties`, `onlineMeeting` entryPoints) currently have **no demote home at all** — they survive in canon but are Dropped on the iCal edge, which is honest but means they have never been exercised end-to-end against a real vendor payload.

### 2.2 `todo` — `todo/canon`

Catalogue: `makeTodoCanonCatalogue()`, `src/todo/todocanonproperties.cpp`.

PropertyIds: `uid`*, `created`, `lastModified`, `summary`, `description`, `descriptionHtml`, `status`, `percentComplete`, `priority`, `categories`, `start`, `due`, `completed`, `recurrence`, `alarms`, `location`, `geo`, `sortOrder`, `relatedTo`, `parentUid`, `checklistItems`, `linkedResources`.

**Evaluation.** Anchored on VTODO/RFC5545 and augmented correctly: extended statuses (MS `waitingOnOthers`/`deferred`, Degraded with Reversible carriers into `providerExtras`), HTML body, due-precision flag, sibling order. The standout decision is the **three-hierarchy rule**: `relatedTo` (VTODO tree), `parentUid` (Google single-level), and `checklistItems` (MS degenerate checkboxes) are three independent representations retained side by side, never collapsed or derived from each other inside the canon — each demote reads whichever it supports. That is the right answer to a genuinely non-isomorphic problem. Caveat: like the calendar union, most of the augmentation (`checklistItems`, `linkedResources`, `sortOrder`) has never met a real Google Tasks or MS To Do payload in this codebase — it is designed-in but unexercised.

### 2.3 `contacts` — `contacts/canon`

Catalogue: `makeContactsCanonCatalogue()`, `src/contacts/contactscanonproperties.cpp`.

PropertyIds: `uid`*, `names`, `nicknames`, `emails`, `phones`, `addresses`, `organizations`, `occupations`, `urls`, `imClients`, `sipAddresses`, `calendarUrls`, `relations`, `birthday`, `anniversary`, `significantDates`, `gender`, `notes`, `photos`, `categories`, `languages`, `timeZone`, `externalIds`, `memberships`, `interests`, `skills`.

**Evaluation.** Anchored on vCard4 ∪ Google People. Most properties are repeated typed entries carrying optional `type`, free `label`, and `primary` — the People/vCard pattern generalized. Coverage is broad and correct: phonetic name sets, E.164 canonical phone form, geo addresses, gender with pronouns, relations absorbing the MS scalar assistant/manager/spouse/children fields, memberships, significant dates beyond birthday/anniversary. The known open design item is contact `uid` minting (schema-design §7.3): vCard UID is inconsistently populated, so the canon must normalize a stable uid and stash the vendor id in `providerExtras` — implemented via the `x-vcard` stash (`vcardcanonstages.cpp`), but the "stable across arbitrary sources" story has not been stress-tested. Losses on the vCard4 demote are declared honestly: occupations/interests/skills Dropped; sipAddresses/calendarUrls/externalIds Reversible (X-stash).

### 2.4 `note` and `outline` — thin canons

- **note:** `uid`*, `body`, `categories`, `lastmodified` (`src/note/noteproperties.cpp`); markdown ⇄ canon edge with frontmatter riding `providerExtras` as Reversible.
- **outline:** `uid`*, `title`, `created`, `lastModified`, `attributes`, `children` (nested node JSON) (`src/outline/outlinecanonproperties.cpp`); opml ⇄ canon (task fields dropped on demote, `note` Reversible), plus an optional org-mode edge behind `KALBURATOR_HAVE_OUTLINE_ORG`.

**Evaluation.** Fit for purpose as identity hubs for their text formats, but they are *not* rich-superset canons — a hypothetical advanced-notes consumer (e.g. Evernote/MediaWiki-style metadata, note linking, attachments) would be extending these catalogues, not reusing depth. Fine; just don't mistake them for the calendar-class exemplars.

### 2.5 `blob` — identity only

Canonical encoding is the raw bytes themselves (`id`*, `data`; hash-equality differ, whole-record-replace merger). No transformations. Correctly minimal.

### 2.6 Edge inventory — what conversions actually exist today

Every domain registers a canon identity hub first; the substantive peers:

| Domain | Edges | Loss character |
|---|---|---|
| calendar | `ical ⇄ canon`, `org-ical ⇄ canon` | promote lossless; demote declares per-field losses (`canonToIcalLoss()`; org demote marks `recurrence` Simplified w/ `X-ORIGINAL-RRULE`) |
| contacts | `vcard4 ⇄ canon`, `vcard3 ⇄ vcard4` | vcard3 reaches canon via 2-hop spine routing |
| todo | `ical-vtodo ⇄ canon`, `todotxt ⇄ ical-vtodo` | todotxt reverse lossless; forward drops description/attendees/rrule/etc. |
| note | `markdown ⇄ canon` | frontmatter Reversible |
| outline | `opml ⇄ canon` (+ optional `org ⇄ canon`) | task fields Dropped |
| blob | identity only | — |

**The observation that drives the rest of this document: aside from todotxt/markdown/opml/org side-edges, every rich-data path in the library begins or ends in iCal or vCard. There is not yet a single Google-JSON or Microsoft-Graph edge**, despite the canon's field vocabulary being derived from exactly those APIs.

---

## 3. Cross-cutting machinery — evaluation

**Loss model (strong).** Exactly four kinds — `Dropped > Simplified > Degraded > Reversible` — with per-edge `LossProfile`s, associative composition across pipeline hops, value-dependent `losslessValues` exemptions, and runtime enforcement: mapping-level loss reported to the host at `syncStarted` (`src/engine/syncengine.cpp:2529`), per-record materialized-loss warnings emitted before every demote write (`materializedLoss()`, `syncengine.cpp:61`), and a loud abort when a demote would empty a record. This is the single best asset for the "new formats" future: any new edge is forced by convention to declare what it destroys, and the engine surfaces it per record.

**Differ/merger semantics (adequate, deliberately coarse).** One `PropertyId` per diff unit; a change anywhere inside a composite (attendees list, addresses array) marks the whole property changed. No per-element 3-way merging (explicitly out of scope, schema-design §5). For sync correctness this is safe; for a rich consumer with heavy concurrent editing of large composites it will produce more conflicts than strictly necessary. Acceptable for first cross-format testing; revisit if conflict volume becomes a complaint.

**Versioning/spine (sound, untested beyond v1).** Ordered spine nodes per domain, bridge-edge requirement documented, unknown-key retention makes narrowing reversible-via-extension. All domains sit at v1; the bump path has never been walked in anger. First plausible trigger remains MS Graph `profile` reaching GA for people-canon v2.

**Extensibility (well-defined).** New format = a `ShapeContribution` returning peer shapes + two edges to the canon hub, registered through the plugin manifest contract (`org.kalburator.Plugin/1.0`) with validation (endpoints must exist, first-registration-wins per domain, atomic rollback). Freeze semantics mean extensions must register before the first `compile()` against the domain. Five working examples exist to copy.

---

## 4. Readiness for cross-format testing beyond iCal/vCard

### What "a consumer managing richer data" splits into

Two very different scenarios are being conflated in the question; separating them changes the answer:

**(A) A consumer whose native representation *is* richer than iCal/vCard, syncing through libkalburator.**
If that consumer speaks (or can speak) canon-shaped JSON — or we give it a `DomainOperations` binding backed by e.g. `GenericSqliteBackend` — then **we are ready today**. Everything the engine does (diffing, merging, baselines v8, id aliasing O55/O56, conflict resolution repair) operates on canonical records; iCal/vCard are just demote targets. Such a consumer gets the full superset with zero new transformation code. This is the cheapest possible first "beyond-iCal/vCard" test, and it exercises the parts of the stack that actually need exercise.

**(B) Converting between canon and a new third-party wire format (Google Calendar/People JSON, Microsoft Graph event/contact/todoTask JSON).**
Here the honest answer is: **designed-for, not built, not tested.**

Ready:
- Field vocabulary for both vendors is fully enumerated in the canon catalogues and justified field-by-field in `2026-05-23-vendor-api-shapes-reference.md` — including the hard analysis (recurrence RFC5545 ⊇ `patternedRecurrence`; Windows-vs-IANA zones; the three hierarchy models; THISANDFUTURE series-split).
- The edge/loss/plugin machinery gives an exact recipe for adding a format, with five in-tree precedents.
- The engine refuses-to-write rather than silently truncating when a demote empties a record, and reports per-record losses.

Not ready:
- **Zero vendor stages exist.** Every `patternedRecurrence` parse, Windows-zone normalization, `dateTimeTimeZone` mapping, People-metadata reduction, and Graph id/changeKey handling named in the reference docs is still unwritten. The reference doc's own §1.3 table shows even the "easy direction" (MS→RFC5545) has a long tail of cannot-represent rules needing explicit loss decisions.
- **Recurrence parsing lives nowhere in the canon** by design — the first vendor edge will have to build the RFC5545⇄structured parser as part of its stage. That is localized (per schema-design §1.4) but nontrivial.
- **No live-vendor verification culture yet for canon content.** The suite's vendor-facing tests are stub-backend; recent live verification (O54) covered URL behavior, not canon field fidelity. First Graph/Google integration will surface payload realities (output-only fields, positional typing in Graph contacts, Google's discarded-task-time) that docs always miss.
- **Contacts uid stability** across non-vCard sources is asserted, not proven (§7.3 of the schema design remains formally open).

### Known suite state relevant to this assessment (verified 2026-08-22 on `main` @ `bb88dcf`)

Roundtrip family: `tst_calendar_canon_roundtrip` fails exactly one slot — `canonPersonalClassificationProducesPrivateAndStash` (personal classification must stash verbatim in `X-CANON-CLASSIFICATION`; pre-existing, uncatalogued). Everything else passes: `tst_orgical_canon_roundtrip`, `tst_todo_canon_roundtrip`, `tst_contacts_canon_roundtrip`, `tst_markdown_canon_roundtrip`, `tst_outline_canon_roundtrip`. Suite-wide baseline remains the documented 180 total / 177 passing (Radicale-state-dependent failures otherwise). So: the canon layer itself is green everywhere except that one classification X-prop slot — worth fixing before building on top of it, and worth a FINDINGS number since it has never been triaged.

### Recommendation — a staged path

1. **Now (scenario A):** stand up a canon-JSON-native consumer against `GenericSqliteBackend` as the hub. This tests "richer-than-iCal data management" end-to-end with no new transformation code, and it stress-tests exactly the machinery recent campaigns hardened (id aliasing, all-or-nothing conflict holds) with records that actually populate `locations[]`, `checklistItems`, `significantDates`, etc.
2. **Fix the one red canon slot** and log it in FINDINGS.
3. **First true cross-format edge (scenario B):** pick the friendliest vendor surface first. Suggested order: **Google Calendar events** (recurrence is already RFC5545 text — no parser needed; zones already IANA) → **Google People** (typed multi-values map almost 1:1 onto the contacts canon) → **Microsoft Graph** last (Windows zones, `patternedRecurrence`, positional contact typing = the three hardest problems, all in one API). Each edge lands with its `LossProfile` written *first*, so the four-kind model governs scope from day one.
4. **Defer:** people-canon v2 (`profile` GA trigger), per-element composite diffing, and any cross-domain edge until a consumer demands them.

### Verdict

Could we today write a consumer that manages more advanced calendar and contact data than iCal/vCard allows? **Yes — if it consumes canon JSON; the superset is real, stored, diffed, merged, and persisted.** Are we ready to begin testing conversions into new external formats with such a consumer? **The scaffolding is complete and proven by precedent, but the first vendor-format edge has not been written; treat scenario B as a small campaign (one format, loss profile first, stub tests then one live account), not a drop-in.** The library will not mislead us when we do — the loss model and fail-loud writes guarantee we find out exactly what each new format costs.
