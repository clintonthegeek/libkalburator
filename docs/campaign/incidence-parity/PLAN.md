# Incidence-parity campaign — binding execution PLAN

**Opened:** 2026-08-29. **Baseline:** `main` @ `fc1ae61`, suite 214 slots
(210 green; the 4 red are the known environmental Radicale/KDAV slots —
`tst_backend_signals`, `tst_backend_thread_relocation`,
`tst_backend_reentrancy_pin`, `tst_remotecalendarbackend` — reproduce at
pre-vtodo-parity `e1846a3`; not caused by, and not in scope for, this
campaign).

**Live tracker:** `STATUS.md` in this directory. Update it **in the same
commit** that lands each item (invariant 7). Do not leave it saying
"IN PROGRESS" after an item lands.

---

## 0. What this campaign is

The library holds three iCalendar incidence kinds (**VEVENT**, **VTODO**,
**VJOURNAL**) and six shape domains (`calendar`, `todo`, `contacts`,
`note`, `outline`, `blob`). VEVENT and VJOURNAL are promoted/demoted by
`src/calendar/{eventcanonfields,journalcanonfields}.cpp`; VTODO by
`src/todo/vtodocanonfields.cpp`. All three ride the **same**
`{calendar,canon}` shape when they arrive over CalDAV, kind-dispatched at
`src/calendar/icalcanonstages.cpp:52-88`; VTODO **additionally** rides its
own `{todo,canon}` shape when it arrives from Google Tasks or MS To-Do.

Three structural facts, each verified against `fc1ae61`, define the work:

1. **The calendar domain already shares the todo emitter.**
   `icalcanonstages.cpp:56` calls `Kalburator::Todo::todoFieldsToCanon()`
   and `:83` calls `Kalburator::Todo::canonObjectToVtodoBytes()`. There is
   one VTODO emitter, used by two domains.

2. **A canon catalogue and its emitters are two independent sources of
   truth about the same key set, with nothing enforcing agreement.** The
   vtodo-parity campaign added `providerExtrasDigest`, `seriesSplitOf`, and
   `completionAnchor` to the shared VTODO emitter and catalogued them only
   in `todocanonproperties.cpp`. `{calendar,canon}` therefore carries three
   keys its own catalogue has never heard of (O78) — the differ cannot see
   them and the merger silently discards them.

3. **VTODO is the poorest-covered incidence kind in the library, poorer
   than VJOURNAL.** `vtodocanonfields.cpp` has zero references to
   `revision()`, `secrecy()`, `url()`, `organizer()`, `attendees()`,
   `attachments()`, or `color()`. `journalcanonfields.cpp` promotes four of
   those; `eventcanonfields.cpp` promotes all seven. None of the VTODO
   drops is declared in any loss profile (O83) — an undeclared loss, which
   the EEE doctrine's "loud about limits" clause forbids.

The campaign's goal is not to copy each vtodo-parity fix into the other
kinds. Copying is what produced this state and three copies drift faster
than two. The goal is to make the three kinds **structurally** unable to
drift, then close the gaps that drift already opened.

### Scope boundary

`note` (`TextDiffer`), `outline` (`OutlineDiffer`) and `blob`
(`RecordDifferBlob`) do **not** use the catalogue-scoped
`CanonJsonDiffer` and are structurally immune to the O78/O80 class. They
are out of scope. The catalogue-integrity work covers exactly the three
`CanonJsonDiffer` domains: **calendar, todo, contacts**.

### Findings this campaign owns

O78, O79, O80, O81, O82, O83 — see `docs/campaign/FINDINGS.md`. Each item
below names which it closes. Flip OPEN→RESOLVED in the landing commit.

---

## 1. Execution rules (binding on every item)

Items run **strictly in order**. IP.1 gates IP.2; IP.3 gates IP.4–IP.7.
One agent per item. An agent that finishes early does **not** start the
next item — it writes its receipt and stops.

Every item must, in its landing commit:

- **Update `STATUS.md`** — the item's row and the session log.
- **Write a return receipt** at
  `docs/campaign/incidence-parity/2026-XX-XX-<item>-return-receipt.md`,
  following the convention of
  `docs/campaign/vtodo-parity/2026-08-28-vpf-return-receipt.md`: what
  landed, what the plan got wrong, every probe outcome, every corner case
  declared-not-executed. **Corrections to this plan belong in the receipt,
  not in a new analysis doc.**
- **Regenerate the convergence matrix** if any loss profile or edge changed
  (house rule O63):
  `./build/tools/matrixgen/matrixgen > docs/campaign/eee/CONVERGENCE-MATRIX.md`
  and confirm the byte-pin slot in `tests/convergence/` is green.
- **Grep the edge-count pins** in the same commit that grows an `edges()`
  list (house rule O63).
- **Add crossing-gate coverage** for any new vendor pair or domain edge
  (house rule O64).
- **Run the full suite** and report the count. The 4 environmental Radicale
  slots are the only acceptable red. Any other red blocks the item.

Two prohibitions:

- **Do not "fix while passing through."** If an item's work reveals a bug
  outside its scope, log it to FINDINGS and name it in the receipt. The
  next item picks it up. This campaign exists because scope leakage in one
  direction and scope rigidity in the other both did damage.
- **Do not hand-maintain a key list.** IP.1 and IP.3 exist to delete
  hand-maintained lists. Adding one back — in code, in a test, or in a doc
  table — is a regression regardless of whether the suite is green.

---

## IP.1 — Catalogue/emitter coverage gate

**Closes:** nothing yet — it *proves* O78. **Gates:** everything after it.

The whole campaign rests on a test that fails today. Land the test before
any fix, so the bug is pinned by a red slot rather than asserted by a
document.

**Files**

- `tests/calendar/tst_calendar_kind_dispatch.cpp` — replace
  `catalogueIncludesTodoAndJournalFields()` (`:176-186`). That slot is the
  drift's own tombstone: it hand-lists four keys (`due`, `completed`,
  `percentComplete`, `relatedTo`), was never updated when three more keys
  appeared, and passed green through the entire drift.
- New shared helper, `tests/calendar/calendar_test_helpers.h` or a new
  `tests/shape/canonkeycoverage.h` — your call, argue it in the receipt.

**Work**

Write a gate that, for each `(domain, kind)` pair the library can promote:

1. Promotes a **maximal** fixture — one exercising every optional field
   the emitter can produce, not a minimal one. A minimal fixture makes this
   gate vacuous. Extend fixtures under `tests/calendar/fixtures/` as needed
   and say in the receipt what you added.
2. Collects the **top-level** key set of the resulting canon object,
   minus the envelope keys (`_canon`, `uid`, `providerExtras` — read them
   from `CanonEnvelope`, do not hardcode).
3. Asserts that set is a **subset** of the owning domain's catalogue ids.
4. Reports every offending key **by name** in the failure message.

Pairs to cover: `(calendar, vevent)`, `(calendar, vtodo)`,
`(calendar, vjournal)`, `(todo, vtodo)`, and each contacts canon leg
(vcard, google-person, ms-contact).

**Expected result: RED on `(calendar, vtodo)`**, naming
`providerExtrasDigest`, `seriesSplitOf`, `completionAnchor`. If it comes up
green, the fixture is not maximal — fix the fixture, not the assertion.

**Acceptance**

- The gate is red for exactly the reason above, and green for every other
  pair.
- Land it red, marked with a `QEXPECT_FAIL` carrying `"IP.2"` and the
  O78 reference — never by weakening the assertion.
- Nothing outside `tests/` changes in this item.

---

## IP.2 — Close the live calendar catalogue drift

**Closes:** O78.

**Files:** `src/calendar/calendarcanonproperties.cpp`.

**Work**

Add `providerExtrasDigest` (String), `seriesSplitOf` (String), and
`completionAnchor` (Json) to `makeCalendarCanonCatalogue()`, in the
existing "Union across iCalendar component kinds" block, matching the
declarations in `todocanonproperties.cpp:47-60` exactly — same
`PropertyKind`, same display name. Remove IP.1's `QEXPECT_FAIL`.

**Why this matters — with the blast radius stated honestly.**
`CanonJsonMerger::merge()` (`src/shape/canonjsonmerger.cpp:29-46`) starts
from `QJsonObject out = t` and overrides only catalogued ids, so an
uncatalogued key takes the **target's** value unconditionally, on every
merge, silently. What that costs *today*, per key, verified 2026-08-29:

| Key | Produced by | Live impact now |
|---|---|---|
| `completionAnchor` | `vtodocanonfields.cpp:373`, gated on `X-ORG-REPEATER` present on the source | **Live.** An org-repeater VTODO arriving through a CalDAV *calendar* has anchor changes invisible to the differ and dropped by the merge. Narrow, but real. |
| `seriesSplitOf` | `todoseriessplitter.cpp:203` (`splitSeriesAtInstant()`), whose only callers are in `tests/todo/tst_todo_series_split.cpp` — W3 landed it host-invoked and unwired by decision. Also promoted from a pre-existing `X-CANON-SERIES-SPLIT-OF` on the wire. | **Latent.** No sync path produces it yet. Becomes live the moment W3 is wired to the engine. |
| `providerExtrasDigest` | every VTODO promote with extras | **Benign now.** Calendar ignoring it leaves that leg where it was pre-O74 — an unrealized benefit, which is O80's gap, not a new regression. Merger keeping a stale digest is harmless: it is derived and recomputed at the next promote. |

So IP.2 is a small, low-drama fix. Its value is not the instance — it is
that the instance stops masking the class while IP.3 removes it. Do not
justify this item to anyone as an emergency; justify IP.1 and IP.3, which
are the ones that carry weight.

**Acceptance**

- IP.1's gate is green with no `QEXPECT_FAIL`.
- A new merger slot in `tests/shape/tst_canonjson_diff_merge.cpp` pinning
  that a source-side `seriesSplitOf` change on a `{calendar,canon}`
  vtodo-kind record survives a merge. This is the regression that was live;
  it deserves its own slot, not just gate coverage.
- Matrix regenerated; byte-pin green.

---

## IP.3 — Contributed catalogues

**Closes:** the O78 *class*. **Gates:** IP.4–IP.7.

IP.2 fixes the instance by hand. This item removes the hand.

**Files:** `src/calendar/{eventcanonfields,journalcanonfields}.{h,cpp}`,
`src/todo/vtodocanonfields.{h,cpp}`,
`src/calendar/calendarcanonproperties.cpp`,
`src/todo/todocanonproperties.cpp`.

**Work**

Each canon-fields module exports the `PropertyId` set it can emit —
`eventCanonContributedIds()`, `vtodoCanonContributedIds()`,
`journalCanonContributedIds()` — declared **next to the emitter it
describes**, in the same file, so the two move together under one editor's
eye.

`makeCalendarCanonCatalogue()` becomes the union of its three contributors
plus the event-only vendor keys that no emitter produces (the Google/MS
Graph fields: `locations`, `onlineMeeting`, `eventType`,
`typedProperties`, `guestsCan*`, `allowNewTimeProposals`, `hideAttendees`,
`locked`, `privateCopy`, `freeBusyStatus`, `responseRequested`,
`descriptionHtml`). `makeTodoCanonCatalogue()` becomes
`vtodoCanonContributedIds()` plus its vendor-only keys (`sortOrder`,
`parentUid`, `checklistItems`, `linkedResources`).

Property **metadata** (kind, display name) stays in the catalogue files —
contributors export ids only. Where a contributed id is already declared
with a kind, that declaration is the single one; a key contributed by two
emitters must be declared once and identically.

**Decide and record in the receipt:** whether the vendor-only keys are
better modelled as a fourth contributor exported from the vendor canon
stages (`mseventcanonstages`, `googlecanonstages`, and the todo vendor
stages). The plan's recommendation is **yes, but not in this item** — the
vendor stages have their own carrier/loss machinery and folding them in
here widens the blast radius. Log it as a follow-up.

**Acceptance**

- IP.1's gate still green, and now **structurally** so: adding a key to any
  emitter must reach every catalogue that emitter feeds, with no second
  edit. Demonstrate this — add a throwaway key to `vtodocanonfields`,
  confirm both catalogues grow, revert. Report the demonstration in the
  receipt.
- No catalogue file contains a key that no emitter and no vendor stage
  produces. Any orphan you find gets removed or justified in the receipt.
  Start with top-level `allDay` in `calendarcanonproperties.cpp` — the
  emitters write `allDay` **inside** the `start`/`due` time objects
  (`vtodocanonfields.cpp:43-50`), so the top-level catalogue entry looks
  vestigial. Verify before removing; a stale catalogue entry is harmless
  but a wrongly-removed one is not.
- Suite green.

---

## IP.4 — Shared VALARM shape module

**Closes:** O79.

This is the one place duplicated runtime code has clearly earned
extraction: the JSON row shape is identical across kinds **by design**, and
the trigger-form branching is subtle enough that duplicating it is exactly
how the bug happened.

**The bug.** `src/calendar/eventcanonfields.cpp:374` reads
`alarm->startOffset()` unconditionally. `KCalendarCore::Alarm`'s
`hasTime()` / `hasEndOffset()` / `hasStartOffset()` are mutually exclusive
(probe-confirmed 2026-08-28, W5). So a VEVENT with
`TRIGGER;VALUE=DATE-TIME:` or `TRIGGER;RELATED=END:` promotes to a bogus
`offset: 0` start-relative alarm. This is Reversible→**silently wrong**,
not a declared loss.

**Four call sites, one commit.** Fixing promote alone makes VEVENT
round-tripping *worse*, so all four move together:

| # | Site | Today | Required |
|---|---|---|---|
| 1 | `eventcanonfields.cpp:366-379` promote | unconditional `startOffset()` | branch on trigger form, per `vtodocanonfields.cpp:394-409` |
| 2 | `eventcanonfields.cpp:662-675` demote | unconditional `setStartOffset()`; ignores `at`/`related`/`repeatCount` | honour all row forms |
| 3 | `mseventcanonstages.cpp:1211-1232` | `a.value("offset").toInt()` ⇒ **0** on an `at`-shaped row, which then passes `offsetSecs <= 0 && offsetSecs % 60 == 0` and maps to `reminderMinutesBeforeStart: 0` — an absolute alarm silently becomes "remind at start" | recognise non-start-relative rows and route them to the existing carrier |
| 4 | `googlecanonstages.cpp:345-367` | same reader shape | same treatment |

Sites 3 and 4 are latent today and go live the moment site 1 lands. They
are the reason this is one item and not four.

**Work**

New `src/calendar/alarmshape.{h,cpp}` (or `src/shape/` — argue the
placement in the receipt; `src/calendar/` is the plan's default since
`KCalendarCore::Alarm` is a calendar-layer type and `src/shape/` is
deliberately domain-neutral):

- `QJsonObject alarmToJson(const KCalendarCore::Alarm::Ptr&)`
- `KCalendarCore::Alarm::Ptr alarmFromJson(const QJsonObject&, Incidence*)`
- `AlarmRowForm describeAlarmRow(const QJsonObject&)` — returns
  StartRelative / EndRelative / Absolute / Malformed, so a vendor leg can
  *ask* rather than infer from a missing key defaulting to zero. Site 3's
  bug is precisely an inference from a defaulted zero.

Move `vtodocanonfields.cpp`'s W5 logic into it **verbatim** — it is the
tested-correct version. Then point event promote/demote at it. VJOURNAL
takes no VALARM per RFC 5545 and is correctly untouched; say so in the
receipt so the next reader does not re-derive it.

**Acceptance**

- New slots in `tests/calendar/`: VEVENT round-trip for an absolute
  trigger, an END-related trigger, and a REPEAT/DURATION pair — modelled on
  the VP.f VALARM slots in `tests/todo/tst_todo_canon_roundtrip.cpp`.
- New slots pinning that ms-event and google-event legs **carry** a
  non-start-relative alarm rather than coercing it. Crossing-gate coverage
  per O64.
- `tests/todo/` VALARM slots unchanged and green — proof the extraction was
  behaviour-preserving.
- Loss profiles updated: the calendar `alarms` row on the MS leg is
  currently `Simplified` (`mseventcanonstages.cpp:1387`). Re-verify that
  verdict against the fixed behaviour and correct it if the carrier now
  makes it Reversible. Matrix regenerated.

---

## IP.5 — `providerExtrasDigest` as an envelope-level service

**Closes:** O80.

`CanonJsonDiffer` compares catalogued ids only, and `providerExtras` is
deliberately never catalogued. O74 solved this for the todo domain with a
catalogued digest key. The prediction recorded in O74's own text — "same
shape presumably holds for any domain whose differ is catalogue-scoped" —
is confirmed: `providerExtrasDigest` has zero occurrences under
`src/calendar/` or `src/contacts/`.

Consequence today: a sync whose only change is a vendor X-property or
provider-extras edit on an **event, journal, or contact** does not dirty
the differ and does not propagate. Raw-bytes/blob-view paths are
unaffected; the gap is specific to canon-diff-mediated sync.

**Work**

Do **not** copy the three todo call sites a fourth, fifth and sixth time.
Add an envelope-level helper next to the existing
`CanonEnvelope::canonicalDigest()` in `src/shape/canonenvelope.{h,cpp}`:

```cpp
void stampProviderExtrasDigest(QJsonObject& obj, const QStringList& volatileKeys);
```

It reads `providerExtras`, filters `volatileKeys` at the depth the todo
call sites filter them (check `googletaskcanonstages.cpp:150-161` and
`mstodotaskcanonstages.cpp:406-432` — match their nesting exactly), hashes,
and inserts the catalogued key. Then call it at every promote site in
calendar and contacts, and **retrofit the three todo sites onto it** so
there is one implementation, not four.

The volatile-key filter is not optional and not uniform. An unfiltered
digest is spuriously always-dirty and is worse than no digest. Known
lists, from the VP.f receipt: Google `etag`; MS `@odata.etag`,
`lastModifiedDateTime`, `@odata.context`. Derive the calendar/contacts
lists from real captured payloads under `msgraph/captured/` and
`google/captured/` — **not** by assuming the todo lists transfer. Record
each list and its evidence in the receipt.

**Catalogue** the key in `calendarcanonproperties.cpp` and
`contactscanonproperties.cpp` — via IP.3's contributor mechanism, not by
hand. Declare the loss kind `Dropped` on every affected profile, matching
todo's ruling (`vtodocanonstages.cpp:101-106`).

**Acceptance**

- Differ pin per domain: an extras-only edit dirties the diff.
- Volatile-filter pin per vendor leg: a bookkeeping-only change does
  **not** dirty the digest, and a real content change does. Model on
  `tst_google_task_canon_edge.cpp:138-160`.
- VJOURNAL covered — `journalcanonfields.cpp:102-111` stashes
  `providerExtras["x-ical"]` and gets the same treatment.
- Todo suites unchanged and green after the retrofit.
- Matrix regenerated.

---

## IP.6 — `incidencecommonfields` extraction

**Closes:** O83.

The forward-direction gap. VTODO drops SEQUENCE, CLASS, URL, ORGANIZER,
ATTENDEE, ATTACH, COLOR — all valid on a VTODO per RFC 5545, all promoted
for VEVENT, four of the seven promoted even for VJOURNAL. None of the drops
is declared in any loss profile, so a consumer has no way to learn about
them. This is the item that actually answers "VTODO/VEVENT parity."

**Files:** new `src/calendar/incidencecommonfields.{h,cpp}`; then
`eventcanonfields.cpp`, `vtodocanonfields.cpp`, `journalcanonfields.cpp`
each shed their copy.

**Work**

Extract the fields that are genuinely identical across all three kinds,
operating on `KCalendarCore::Incidence` rather than a subclass:

- `created` / `lastModified` — **including O41's literal-presence guard**
  (`extractICalPropertyLiteral`, because the accessors return a
  construction-time "now" when the property is absent). This guard has
  already been written three times and fixed late once
  (`journalcanonfields.cpp:53` records that it "never got the same
  guard"). Extracting it retires the whole recurrence of that bug.
- `sequence` (`revision()`), `summary`, `description` / `descriptionHtml`,
  `classification` (`secrecy()`), `categories`, `url`, `color`, `location`,
  `geo`, `organizer`, `attendees`, `attachments`, and the generic X-prop
  passthrough into `providerExtras` + IP.5's digest stamp.

Kind-specific fields stay in their own module: VEVENT `end`/`allDay`/
transparency/vendor flags; VTODO `due`/`completed`/`percentComplete`/
`completionAnchor`/`seriesSplitOf`/hierarchy; VJOURNAL its narrower set.

**Sequencing within the item.** Do the pure extraction **first** — three
kinds, no behaviour change, suite green, commit. Then add the missing VTODO
fields as a **second** commit. A reviewer must be able to see, in one
diff, exactly which fields VTODO gained. Do not interleave them.

**Acceptance**

- IP.1's gate still green (VTODO's new keys must reach both catalogues via
  IP.3 — if they do not, IP.3 is incomplete and this item stops and says so
  rather than hand-patching a catalogue).
- Round-trip slots for each newly-promoted VTODO field, on the vtodo leg
  and through `{calendar,canon}`.
- Loss profiles updated for the todo vendor legs: Google Tasks and MS
  To-Do have no wire form for most of these. Declare each honestly —
  `Dropped` where it is dropped. Undeclared loss is the O83 defect; the fix
  is not complete until it is declared.
- VJOURNAL keeps every field it has today. It is the least-attended kind
  and the easiest to quietly regress.
- Matrix regenerated.

---

## IP.7 — Remaining VEVENT corrections

**Closes:** O81, O82.

Two known VEVENT bugs whose VTODO twins are already fixed. They are last
because they are small, independent, and benefit from IP.3–IP.6 having
already normalised the surrounding code.

**IP.7a — RANGE=THISANDFUTURE (O82).**
`eventcanonfields.cpp:594-596` runs
`event->setThisAndFuture(range == QStringLiteral("thisAndFuture"))`
unconditionally on demote. Re-emitting `RANGE=THISANDFUTURE` is
write-hostile on real servers; `vtodocanonfields.cpp:740` now refuses to.
Apply the same refusal, move `recurrenceRange` to a `Degraded` loss row on
the event edges, and rewrite any pinned test that asserts the old
behaviour — VP.e had to do exactly that
(`vtodoRoundTripPreservesThisAndFutureRange` →
`vtodoDemoteNeverEmitsThisAndFutureRange`); grep for the event-side twin
before assuming none exists.

**IP.7b — malformed DTSTART/DTEND coercion (O81).**
`eventcanonfields.cpp` has no DATE-vs-DATE-TIME reconciliation; the VTODO
side gained rules (a)/(b) at `vtodocanonfields.cpp:229-278` under W6.2. A
VEVENT with `DTSTART;VALUE=DATE` and a DATE-TIME `DTEND` (or the reverse)
promotes today with a type-mismatched pair.

**Do not blindly mirror W6.2's rule (a).** That rule resolves the mismatch
by letting DUE's type win, which the VP.f receipt records as a *deliberate
divergence* from tasks.org's symmetric rule, adopted because the binding
response doc said so. No such document constrains VEVENT, and DTSTART/DTEND
is not the same relationship as DTSTART/DUE — DTEND is a bound derived from
DTSTART, so the symmetric argument is weaker here and DTSTART-wins is
likely correct. **Probe KCalendarCore first**, decide on evidence, and
write the rule and its justification into the receipt as a contract before
implementing. If the answer is genuinely ambiguous, stop and ask rather
than guessing — this is the one point in the campaign where a wrong
default silently corrupts user data.

**Acceptance**

- Round-trip slots for both, mirroring the VTODO slots.
- Loss profiles + matrix updated.
- A short contract doc for IP.7b's coercion rule, in this directory,
  following `2026-08-28-w7-passthrough-contract.md`'s form.

---

## 2. Explicitly out of scope

Log to FINDINGS if encountered; do not fix.

- Engine-level uid-family propagation/cascade (W1 contract §5 — still
  SPECIFIED-not-executed).
- `SyncEngine`/differ/backend wiring of `splitSeriesAtInstant()` (W3 —
  host-invoked only by decision).
- org-io promote wiring (W4 — `KALBURATOR_HAVE_ORG_IO=ON` is not buildable
  standalone in this repo).
- The 4 environmental Radicale/KDAV slots.
- `note` / `outline` / `blob` domains — non-`CanonJsonDiffer`, immune to
  this class.
- Vendor-stage contributed catalogues (deferred out of IP.3 by design).

## 3. Success condition

The campaign is done when:

1. No canon catalogue in `calendar`, `todo`, or `contacts` can drift from
   its emitters without a red test (IP.1 + IP.3).
2. VALARM, provider-extras visibility, and the common incidence field set
   have **one** implementation each, shared by all three kinds
   (IP.4, IP.5, IP.6).
3. Every remaining VTODO/VEVENT/VJOURNAL asymmetry is either closed or
   **declared** in a loss profile and visible in the convergence matrix
   (IP.6, IP.7).

Condition 3 is the one that matters to consumers: after this campaign a
consumer can read the matrix and know exactly what each incidence kind
carries on each leg. Today it cannot, because the drops are undeclared.

---
---

# Amendment 1 — 2026-09-02

**Adopted after** the pre-flight audit
(`2026-09-02-preflight-audit.md`), which was commissioned because this
campaign kept discovering defects sideways: O84 was found while building
IP.2's test, not by looking. The audit looked once, deliberately, before
IP.3 starts, and found six more (**O85–O90**).

**This amendment is binding and supersedes §1's ordering rule and §3's
success condition.** Everything else in the body above stands unchanged —
IP.3–IP.7's work statements, files and acceptance criteria are still
correct as written, except where a §A.3 amendment says otherwise. Read the
body first; read this second; do what this says when they differ.

Evidence for every claim below is re-runnable: `probes/run.sh`.

---

## A.1 What the audit changed about the diagnosis

The body's §0 named three structural facts. The audit adds a fourth that
subsumes two of them:

> **`_canon.kind` is written in one place and read in one place.**
> `src/calendar/icalcanonstages.cpp:65` writes it; `:81` reads it. Nothing
> else in the library knows it exists — not the catalogue, not
> `CanonJsonDiffer`, not `CanonJsonMerger`, not the loss profiles, not the
> engine, not the baseline store. Grep-confirmed: exactly two call sites
> outside `canonenvelope.cpp`.

Yet it alone decides whether a canon record demotes as a VEVENT, a VTODO or
a VJOURNAL. O78, O83, O84, O87 and O88 are all symptoms of that one fact.
The campaign's remedy — structural non-drift, not a copy pass — remains
right; it simply was not yet aimed at `kind`.

**And the reason these keep surfacing sideways:** IP.1's gate asserts
*emitted ⊆ catalogued* — agreement between two of **our own** artifacts.
Every defect the audit found is a disagreement between our emitter and
**RFC 5545**. Nothing measures that axis. IP.8 is that measurement, and it
is now the campaign's highest-leverage item.

## A.2 Revised execution order — binding

Items still run **strictly in order, one agent each**, but the order is now
the one below rather than numeric. New items are numbered IP.8+ so existing
receipts, FINDINGS entries and STATUS rows keep their references.

| # | Item | Closes | Why here |
|---|---|---|---|
| 1 | **IP.8** — RFC-5545 round-trip fidelity gate | *proves* O85, O86, O87; re-pins O79, O83 | Tests only. Lands RED. Same doctrine as IP.1→IP.2: pin the bugs with red slots before fixing any. Gates everything. |
| 2 | **IP.3** — contributed catalogues **+ O84 fix + `allDay` orphan** | O78 *class*, **O84** | Unchanged from the body. Now gated on IP.8. |
| 3 | **IP.9** — kind-scoped loss profiles | **O88** | Must precede every item whose acceptance says "declare the loss honestly" — today there is nowhere truthful to put such a declaration. |
| 4 | **IP.4** — shared VALARM module **+ O85** | **O79, O85** | Unchanged plus §A.3.1. |
| 5 | **IP.5** — `providerExtrasDigest` as envelope service | **O80** | Unchanged. |
| 6 | **IP.6** — `incidencecommonfields` **+ O86 decision** | **O83, O86** | Unchanged plus §A.3.2. |
| 7 | **IP.10** — VJOURNAL parity | **O87** | After IP.6, so it inherits the extracted common module rather than growing a fourth copy. |
| 8 | **IP.7** — VEVENT corrections | O81, O82 | Unchanged plus §A.3.3. |
| 9 | **IP.11** — VTODO representation unification | **O89** | Last of the substantive items: the largest contract change, and the only one needing consumer ratification. |
| 10 | **IP.12** — demote purity | **O90** | Trivial; last because it is trivial. |

The two prohibitions in §1 (**no fixing while passing through**, **no
hand-maintained key lists**) remain binding on every item, as do the
per-item obligations (STATUS update, return receipt, matrix regeneration,
edge-count grep, crossing-gate coverage, full-suite report).

## A.3 Amendments to existing items

### A.3.1 — IP.4 also closes O85

Add to IP.4's work and acceptance: **promote must record
`alarm->enabled()` and demote must honour it.** All four sites construct
`new Alarm(...)`, whose `enabled()` defaults to false, and none calls
`setEnabled(true)`, so *every* alarm round-tripped through canon comes back
disabled and serializes `X-KDE-KCALCORE-ENABLED:FALSE` — on the VTODO leg
too, which W5 otherwise corrected.

Decide and record: whether `enabled` becomes a row key (`"enabled": bool`,
absent ⇒ true, keeping pre-existing rows valid the way W5's additive keys
did) or whether demote simply always calls `setEnabled(true)` because a
disabled alarm has no iCal representation to begin with. The plan's
recommendation is **the latter** — RFC 5545 has no "disabled alarm"; the
KDE X-prop is a KCalendarCore-local concept — but the round trip must then
be shown not to lose a deliberately-disabled KOrganizer alarm, or the
receipt must state that it does and why that is acceptable.

Acceptance gains: a slot proving an enabled alarm survives promote→demote
enabled, on **both** the VEVENT and VTODO legs.

### A.3.2 — IP.6 also owns the O86 GEO decision

`GEO` is corrupt at the KCalendarCore layer (O86, upstream, reproduces with
no libkalburator in the picture). The VTODO leg currently promotes and
demotes it, so we write malformed `GEO` lines to real servers and VTODO
promote→demote→promote is **not a fixpoint**. The VEVENT leg never promotes
`geo` despite the catalogue declaring it.

IP.6 must choose **one** and write the justification into its receipt:

- **(a)** hand-serialize the `GEO` line after `toICalString`, in the style
  of the existing `stripICalPropertyLine` post-processing, and promote it
  on all three kinds; or
- **(b)** stop emitting `geo` entirely and declare it `Dropped` on the
  affected profiles.

Do **not** "fix" it by round-tripping through the broken accessor pair, and
re-verify against the installed kcalendarcore version first — an upgrade
may have retired it. Whichever is chosen, the VEVENT/VTODO asymmetry must
end: today one drops `geo` and the other corrupts it.

Also add to IP.6's scope: **VEVENT drops `RELATED-TO`** (measured; the
calendar catalogue declares `relatedTo`, `eventcanonfields.cpp` never emits
it). It is a common incidence field and belongs in the extraction.

### A.3.3 — IP.7b's contract needs consumer ratification

The body already says to probe first and "stop and ask rather than
guessing" on the VEVENT `DTSTART`/`DTEND` coercion rule. Amendment: the
asking is **already in flight** —
`docs/2026-09-02-incidence-parity-planstan-report.md` puts it to PlanStan
as Question 2, since PlanStan set the W6.2 precedent for VTODO. IP.7b must
not land before that answer arrives. If it has not arrived when IP.7 comes
up, IP.7a lands alone and IP.7b waits.

---

## A.4 New items

### IP.8 — RFC-5545 round-trip fidelity gate

**Proves:** O85, O86, O87. **Re-pins:** O79, O83. **Gates:** everything.

The campaign's central missing measurement. IP.1 asks "does the catalogue
know what the emitter emits?" This asks **"does the emitter honour the
standard?"** — and it is the question every defect in the audit answers
badly.

**Files:** `tests/calendar/` (new slot file, or extend
`tst_calendar_kind_dispatch.cpp` — argue the placement in the receipt);
fixtures under `tests/calendar/fixtures/`.

**Work**

For each of `vevent`, `vtodo`, `vjournal`:

1. Take a **maximal RFC 5545-conformant** fixture for that component —
   every property the RFC permits on it, not every property our emitter
   happens to handle. This direction matters: a fixture built from the
   emitter's capabilities makes the gate vacuous in exactly the way §A.1
   describes. `probes/incidence-audit-probe.cpp`'s fixtures are a starting
   point, **not** a finish line; they were built to demonstrate known
   defects, not to be exhaustive.
2. Promote → demote. Compute the property-name set of source and output,
   **unfolding per RFC 5545 §3.1 first** (see `probes/README.md` trap 2 —
   a per-line parse silently misreports `ATTENDEE`).
3. Assert `lost == expectedLoss[kind]`, where `expectedLoss` is a declared
   allow-list of intentional drops **that must equal the kind's loss
   profile** once IP.9 lands. Until then, assert against a literal list and
   leave a `TODO(IP.9)` naming the coupling.
4. Assert promote→demote→promote is a **fixpoint** (canon bytes equal).
   VTODO fails this today via O86.
5. Report every unexpected drop **by name**.

Add a VALARM sub-gate over the four trigger forms (start-relative,
END-relative, absolute, REPEAT/DURATION) and the `enabled` flag, for both
VEVENT and VTODO.

**Expected result: RED** on `(calendar, vtodo)` — 7 drops + GEO + fixpoint,
`(calendar, vjournal)` — 7 drops, `(calendar, vevent)` — GEO, `RELATED-TO`,
and the VALARM sub-gate on both legs. Land each red assertion with a
`QEXPECT_FAIL` naming the item that will close it (IP.4, IP.6, IP.9,
IP.10). **If any comes up green, the fixture is not maximal — fix the
fixture, not the assertion.**

**Acceptance**

- Red for exactly the reasons above, green everywhere else.
- Each `QEXPECT_FAIL` names its closing item and its O-number.
- The allow-list is **declared data, not scattered literals**, and is the
  thing IP.9 later wires to the real loss profiles.
- Nothing outside `tests/` changes. Suite green (XFAILs are not ctest
  failures).

### IP.9 — Kind-scoped loss profiles

**Closes:** O88. **Gates:** IP.4, IP.6, IP.10 (every item that must declare
a loss).

**The problem.** The loss-profile system's unit is the **edge**; the
calendar `ical` encoding is a **union of three schemas**. One
`canon → ical` edge carries `canonToIcalLoss()`, which is entirely
event-shaped, so `materializedLoss()` (`syncengine.cpp:4635`, `:4675`)
warns a VTODO's user about `guestsCanModify` and says nothing about the
`ATTENDEE` it just lost. Meanwhile `canonToVjournalLoss()` is **dead code**
with a false comment.

**Work**

Pick one and justify it in the receipt:

- **(a)** Split the edge per kind — three `canon → ical` edges discriminated
  by `_canon.kind`. Cleanest conceptually; requires the transformation
  registry to key on more than `(shape, shape)`, which is a real change to
  `TransformationRegistry` and touches every domain. Check whether the
  registry can express it at all before choosing this.
- **(b)** Give `LossProfile` a kind dimension, so one edge carries three
  profiles and `materializedLoss()` selects by the record's kind. Smaller
  blast radius; keeps the graph shape. **The plan's recommendation.**
- **(c)** Make the profile a function of the record rather than a constant
  of the edge. Most flexible, least declarative, hardest to render into the
  convergence matrix.

Whichever wins: `canonToVjournalLoss()` is either wired or deleted — it
must not survive this item as dead code. And `matrixgen` must render the
per-kind profiles, because the matrix's whole purpose is letting a consumer
read what each leg carries; today it reports the calendar `ical` leg as
though every record on it were a VEVENT.

**Acceptance**

- A VTODO demote through `{calendar,ical}` warns about VTODO drops and not
  about event-only fields. Pin it.
- Same for VJOURNAL.
- `canonToVjournalLoss()` wired or gone; grep proves no dead loss function
  remains.
- Matrix regenerated and now **kind-aware**; byte-pin updated deliberately,
  with the diff explained in the receipt (this is the one item expected to
  change the matrix substantially).
- IP.8's allow-list is wired to the real profiles, closing its
  `TODO(IP.9)`.

### IP.10 — VJOURNAL parity

**Closes:** O87.

VJOURNAL is the least-attended kind and the audit found it the worst:
seven undeclared drops, one of which is an identity defect.

**Files:** `src/calendar/journalcanonfields.{h,cpp}`, plus the
`incidencecommonfields` module IP.6 will have created.

**Work**

Once IP.6 has extracted the common incidence fields, VJOURNAL should get
`organizer`, `attendees`, `attachments`, `relatedTo` essentially for free —
verify that and say so rather than re-adding them by hand. Then close what
remains, which is journal-specific:

- **`RECURRENCE-ID` — do this first and separately.** Dropping it makes a
  detached journal instance and its master indistinguishable in canon: two
  records collapse onto one uid. This is identity corruption, not field
  loss. Model the fix on VTODO's W1 composite exception identity
  (`recurrenceId` + `recurrenceRange`), which already solved exactly this
  problem for tasks — reuse its shape, do not invent a second one.
- **`RRULE` / `EXDATE`** — VJOURNAL takes recurrence per RFC 5545 and the
  emitter has no recurrence handling at all, so a recurring journal is
  silently flattened. Reuse the verbatim-RFC5545-lines convention
  (invariant 3) the other two kinds already use.
- **`descriptionHtml`** — check whether the X-ALT-DESC carrier the other
  kinds use applies; if not, say why in the receipt.

**Also fix the phantom key:** `journalcanonfields.cpp:91` inserts
`classification` unconditionally, so a VJOURNAL with no `CLASS` gains
`classification: "public"` in canon. Harmless today, but it is a
catalogue/emitter asymmetry of exactly the kind this campaign exists to
remove; guard it like the sibling fields or justify the difference.

**Acceptance**

- IP.8's `(calendar, vjournal)` gate goes green, `QEXPECT_FAIL` removed.
- A slot proving a detached VJOURNAL instance and its master remain
  distinct through a full promote→demote→promote.
- Recurrence round-trip slot.
- Losses declared through IP.9's mechanism.
- VEVENT and VTODO slots unchanged and green.

### IP.11 — VTODO representation unification

**Closes:** O89. **Blocked on:** PlanStan's answer to Question 1 of
`docs/2026-09-02-incidence-parity-planstan-report.md`.

**The problem.** A VTODO gets the rich `{todo,canon}` representation or the
impoverished `{calendar,canon}` one depending on transport metadata:
`MultiProtocolDavProvider` demuxes only when a collection advertises
`VTODO` (`multiprotocoldavprovider.cpp:214-226`), and `LocalBackend`,
`DecSyncBackend`, `OrgBackend` and `AkonadiBackend` never demux at all
(`nativeShapes()` returns `{calendar, ical}` only). Same task, different
catalogue, differ, merger and loss profile, decided by where it is stored.

**This item does not choose the answer.** It implements the ratified one.
The two candidates put to PlanStan:

- **Converge** — bring `{calendar,canon}` VTODO to full parity, so the two
  representations are equivalent and it stops mattering which you get.
  IP.3/IP.6/IP.9 do most of this already; IP.11 would then be a *proof*
  item (a crossing gate showing the two paths produce equivalent canon)
  plus closing whatever residue remains, notably the four still-divergent
  vendor keys (`checklistItems`, `linkedResources`, `parentUid`,
  `sortOrder`).
- **Route** — send every VTODO to `{todo,canon}` regardless of transport,
  by giving the non-DAV backends a demux path. Eliminates the duplicate
  representation outright, but changes which domain a consumer observes a
  task in — hence the ratification requirement.

**Whichever is chosen**, one thing is not optional: **the silent fallback
must become loud.** A collection whose components are routed by absent
server metadata should say so, at minimum in a log line, per the EEE
doctrine's "loud about limits" clause.

**Acceptance**

- Depends on the ratified direction; write the acceptance criteria into the
  receipt **before** implementing, and have them reviewed against the
  PlanStan answer.
- A crossing gate (house rule O64) covering the calendar-leg and todo-leg
  VTODO paths, whichever survives.
- The fallback is observable.

### IP.12 — Demote purity

**Closes:** O90.

`KCalendarCore::ICalFormat` stamps a heap-address-derived `X-UID` parameter
into every serialized `ATTENDEE`, so `demote(canon)` is not a function of
`canon` alone — output differs across processes.

**Do not dramatise this.** It causes no write storm today: the differ works
on canon (no `X-UID`) and the skip cache compares each backend's own
`contentHash` of *stored* bytes (`syncengine.cpp:3700-3712`). The costs are
non-reproducible demoted output, server-side accumulation of meaningless
identifiers, and the impossibility of any future byte-pin over demoted
bytes.

**Work.** Strip the `X-UID` attendee parameter post-serialization, in the
style of the existing `stripICalPropertyLine` calls in
`eventcanonfields.cpp` / `journalcanonfields.cpp`. Confirm first that
nothing round-trips it deliberately (grep `X-UID`); if something does, stop
and say so rather than removing it.

**Acceptance**

- A slot proving two demotes of the same canon in **different processes**
  are byte-identical. A same-process assertion is vacuous — it already
  passes. Achieve this by demoting a fixture, comparing against a committed
  expected-bytes file, or by forking; argue the mechanism in the receipt.
- Suite green.

---

## A.5 Revised success condition

Supersedes §3. The campaign is done when:

1. No canon catalogue in `calendar`, `todo` or `contacts` can drift from
   its emitters without a red test. *(IP.3 + IP.8)*
2. No incidence kind can lose an RFC 5545 property without a red test or a
   declared, kind-scoped loss profile row. *(IP.8 + IP.9)*
3. VALARM, provider-extras visibility, and the common incidence field set
   have **one** implementation each, shared by all three kinds.
   *(IP.4, IP.5, IP.6)*
4. `_canon.kind` survives every operation the shape layer performs on a
   canon record — promote, diff, merge, demote. *(IP.3)*
5. All three kinds round-trip their own RFC 5545 field set, or declare what
   they do not. *(IP.6, IP.7, IP.10)*
6. A VTODO's canonical representation does not depend on where it is
   stored — or, if it deliberately does, that is ratified, documented and
   loud. *(IP.11)*

Condition 2 is the one that would have prevented this audit from being
necessary. Condition 6 is the one a consumer can feel.

---
---

# Amendment 2 — 2026-09-02 (ratified answers)

**Source:** `docs/2026-09-02-incidence-parity-planstan-response.md` —
PlanStan @ `master` `e1856650`, pinned `v1.01`.

**Both questions are answered and ratified. Nothing in this campaign is
blocked on a consumer any more.**

- **Q1 → (a) converge.** Not the "no strong view" default the report
  offered — an evidenced answer, with a structural reason (b) cannot land.
- **Q2 → DTSTART-wins for VEVENT**, with a better unifying principle than
  the plan had, and a precise three-part rule.

This amendment records both, settles the O86 open question, and **reorders
the remaining items** on the strength of one disclosure in the answer.
Amendment 1 otherwise stands.

---

## B.1 The disclosure that changes priorities

PlanStan reads `{calendar,canon}` VTODOs as their **primary and default**
task path — not an edge case:

- `todo_work.kalb`, the fixture their entire todo-UX campaign was built
  against, binds its one list to the `local` backend, which never demuxes.
- `Test6.kalb`, a real GTD vault, has seven task lists, each **mirrored**
  across a `local` and a `multiproto-dav` binding — so every task is a
  `{calendar,canon}` VTODO on *both* legs simultaneously.
- Their org backend is a task-first surface and is likewise never demuxed.

**So O83's seven undeclared drops have been hitting the default task path
of the consuming application**, and W1's composite exception identity —
which PlanStan asked for and this library delivered — is not reaching the
vault their todo work is tested against.

That makes IP.6 the highest-impact user-data fix in the campaign, and it is
currently sixth. See §B.3.

## B.2 Q2 — the rule, and the principle behind it

PlanStan confirmed **DTSTART-wins** and, more usefully, supplied the
principle that makes W6.2 and this the *same* rule rather than two
decisions:

> **The mandatory temporal anchor wins; the optional derived bound is
> coerced to match it.**

- **VTODO** — `DTSTART` is optional, `DUE` is semantically primary (a task
  is defined by its deadline; many carry `DUE` and no `DTSTART`). Anchor =
  `DUE` ⇒ **DUE-wins**, which is exactly W6.2.
- **VEVENT** — polarity reversed. `DTSTART` is mandatory; `DTEND` is
  optional, may be replaced by `DURATION`, and is *defined relative to*
  `DTSTART`. Anchor = `DTSTART` ⇒ **DTSTART-wins**.

**Adopt this framing in the IP.7b contract doc.** Amendment 1 §A.3.3 and
the body's IP.7b both describe W6.2 as a "deliberate divergence" this
should not mirror. That was right about the *action* and wrong about the
*reason*: it is one rule applied to components with opposite optionality.
Stating it that way means **VJOURNAL falls out for free** — `DTSTART` only,
no bound, nothing to coerce — instead of needing a third decision at IP.10.

### The rule to implement

1. Coerce **`DTEND` to `DTSTART`'s value type. Never the reverse.**
   - `DTSTART` `DATE` + `DTEND` `DATE-TIME` ⇒ take `DTEND`'s date part.
   - `DTSTART` `DATE-TIME` + `DTEND` `DATE` ⇒ `DTEND` at `00:00` in
     `DTSTART`'s timezone. (House rule O60: construct the wall time
     directly **in** the target zone; do not build it elsewhere and
     convert.)
2. If the coerced `DTEND <= DTSTART`, **drop `DTEND`** and let RFC 5545's
   default stand — rather than synthesising a bound.
3. `DURATION` present instead of `DTEND` ⇒ nothing to coerce. Leave it.

PlanStan offered item 2 as a preference and invited a different call on an
RFC read. **Take their preference — the RFC read agrees with it.** RFC 5545
§3.6.1 requires `DTEND` to be strictly greater than `DTSTART`, so a
non-conforming pair has no valid value to clamp *to*; and the same section
already defines the absent-`DTEND` behaviour (one day for a `DATE`
`DTSTART`, zero duration for a `DATE-TIME` one). Dropping therefore falls
back to a **defined** default, while clamping to `DTSTART + 1 day` would
invent a bound the author never wrote and make a malformed all-day event
indistinguishable from a well-formed one. Record this reasoning in the
contract doc — the invitation to differ was genuine, and so is the reason
for not taking it.

**Why it matters to them, worth keeping in the contract:** the common
real-world malformed case is an all-day event from a sloppy producer —
`VALUE=DATE` `DTSTART` with a stray `DATE-TIME` `DTEND`. Under DTEND-wins
it promotes to *timed* and moves out of PlanStan's all-day banner into a
00:00 slot. DTSTART-wins keeps it where the author meant it. And because
`KCalendarCore::Incidence::allDay()` is one boolean for the whole
incidence, a mismatched pair is unrepresentable downstream anyway —
KCalendarCore collapses it by whichever setter ran last, "which is not a
rule, it is an accident."

**IP.7b is UNBLOCKED.** Contract doc first, in the shape of
`docs/campaign/vtodo-parity/2026-08-28-w7-passthrough-contract.md`;
PlanStan will read it.

## B.3 Revised execution order (supersedes Amendment 1 §A.2)

Only the tail moves. **IP.6 and IP.10 advance ahead of IP.4 and IP.5**,
because §B.1 shows IP.6 fixes live data loss on the consumer's default task
path, and IP.10 (which depends on IP.6's extraction) closes the
`RECURRENCE-ID` identity corruption — the two highest-severity items in the
audit. Nothing depends on IP.4 or IP.5, so moving them later is free.

| # | Item | Closes | Change |
|---|---|---|---|
| 1 | **IP.8** — RFC-5545 round-trip fidelity gate | proves O85–O87 | — |
| 2 | **IP.3** — contributed catalogues + O84 | O78 class, O84 | — |
| 3 | **IP.9** — kind-scoped loss profiles | O88 | — |
| 4 | **IP.6** — `incidencecommonfields` + drop `geo` | **O83, O86** | **was 6** — §B.1 |
| 5 | **IP.10** — VJOURNAL parity | **O87** | **was 7** — follows IP.6 |
| 6 | **IP.4** — shared VALARM module | O79, O85 | was 4 |
| 7 | **IP.5** — providerExtrasDigest | O80 | was 5 |
| 8 | **IP.7** — VEVENT corrections | O81, O82 | **7b unblocked** (§B.2) |
| 9 | **IP.11** — convergence proof | O89 | **unblocked, rescoped** (§B.4) |
| 10 | **IP.12** — demote purity | O90 | — |

**IP.4 is not deprioritised on the grounds that PlanStan has no alarm UI.**
They raised this themselves and asked us not to: they are a *passthrough*
for alarms other clients authored, so "every alarm round-trips back
disabled" corrupts third-party data flowing through them. Their words:
*"treat our lack of UI as zero reason to deprioritise IP.4."* It moves
because IP.6/IP.10 got more urgent, not because IP.4 got less.

## B.4 IP.11 rescoped — convergence proof, not a choice

**Closes:** O89. **No longer blocked.** Supersedes Amendment 1's IP.11.

(a) is ratified, and PlanStan explicitly asked us **not** to keep the two
representations distinguishable for their benefit: *"converging them until
it stops mattering which one a task gets is exactly the outcome we want."*
So IP.11 stops being a design decision and becomes a proof:

**Work**

1. A **crossing gate** (house rule O64) demonstrating that the same VTODO
   promoted through `{calendar,canon}` and through `{todo,canon}` yields
   **equivalent canon** — modulo the vendor-only keys that genuinely have
   no iCal representation. This is the item's deliverable; everything else
   is cleanup toward making it pass.
2. Close the residual catalogue divergence: `checklistItems`,
   `linkedResources`, `parentUid`, `sortOrder` are in the todo catalogue
   and absent from calendar's. If IP.3's contributor mechanism has not
   already unified them, that is a defect in IP.3 — say so rather than
   hand-patching a catalogue.
3. **Make the silent fallback loud.** A collection routed to
   `{calendar,canon}` because a server advertised no component types should
   say so in a log line, per the EEE doctrine's "loud about limits" clause.
   This survives convergence: even when the two paths are equivalent, which
   one you took should be observable.

**Do NOT implement (b) routing**, and do not leave hooks for it. It is
blocked on a PlanStan-side data-model change (a logical calendar holding
membership in more than one domain) that they have not designed. The reason
is recorded in FINDINGS O89 so it is not re-proposed as a rename; the short
version is that `CalendarType::Hybrid` is their *default*, and under (b) a
hybrid calendar would need two primary bindings in two domains — which
their model cannot express, so half of every hybrid calendar would silently
stop loading.

**Acceptance**

- The crossing gate passes, or names precisely which keys still diverge and
  why each is legitimate.
- The fallback is observable.
- No `(b)`-shaped scaffolding anywhere in the diff.

## B.5 Settled — stop flagging these

Amendment 1 and the report flagged five things as "will change under you".
PlanStan cleared all five; treat them as decided:

| Flagged | Ratified |
|---|---|
| Matrix reshape (IP.9) | **No-op for them** — they parse and pin nothing (`grep ConvergenceMatrix` → zero hits). Reshape freely. |
| New loss warnings | **Wanted, no spam risk** — they consume no loss profile programmatically. They agree the undeclared drops are the contract breach independent of the bugs. |
| `geo` (O86) | **Drop it.** They don't consume it. Do not hand-serialize around the upstream bug. Amendment 1 §A.3.2's choice is closed: option **(b)**. |
| VJOURNAL additive fields (IP.10) | Fine either way. |
| Alarm `enabled` key (IP.4) | Fine either way — §A.3.1's recommendation (always enable on demote) stands unless the round-trip evidence says otherwise. |

## B.6 Received, not ours

PlanStan acknowledged the W1 receipt's warning that `ConflictInfo.sourceId`
/ `targetId` may carry a composite id (`uid \x01 recurrenceId`) needing
decomposition before display, named the three places it bites them
(`conflictdiffwidget.cpp:76`, `conflictdockwidget.cpp:175`, and `:339-340`
where it is used as a *lookup key*), and are tracking it on their side.
**No action here — do not re-issue the warning.**

## B.7 One thing this repo owes them

PlanStan is pinned at **`v1.01`**. Everything from the vtodo-parity
campaign (W1–W7) is landed on `main` and **untagged**, so they cannot
consume any of it. They state plainly that their adoption pass does not
gate this plan and they will pick it up "when you next cut a tag."

Not an item in this campaign, and not a blocker — but a tag is cheap and
they are currently one release behind their own delivered requirements.
Worth raising with the maintainer at the next natural stopping point.
