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
