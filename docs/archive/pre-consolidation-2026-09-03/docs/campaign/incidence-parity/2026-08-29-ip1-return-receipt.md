# Return receipt — IP.1: catalogue/emitter coverage gate (O78 pinned RED)

**Delivered:** 2026-08-29
**Consumes:** `docs/campaign/incidence-parity/PLAN.md` §1 (execution rules,
binding) and the IP.1 section; `docs/campaign/incidence-parity/STATUS.md`;
FINDINGS.md O78.
**Scope discipline:** test-only. Nothing under `src/` changed. IP.1 does not
close O78 — it proves it with a red slot, per the plan's explicit intent
("land the test before any fix, so the bug is pinned by a red slot rather
than asserted by a document").

---

## 0. What landed

`tests/calendar/tst_calendar_kind_dispatch.cpp`'s
`catalogueIncludesTodoAndJournalFields()` (the hand-listed four-key slot at
`:176-186`, the drift's own tombstone per PLAN.md) is gone. In its place:
seven new slots, one per `(domain, kind)` pair named in the plan, each:

1. promotes a **maximal** fixture (built for this item — see §2) through the
   real promote stage for that pair,
2. parses the resulting canon bytes into a `QJsonObject`,
3. calls a new shared helper, `Kalburator::TestSupport::verifyCanonKeysDeclared()`,
   which computes the emitted top-level key set minus the three envelope
   keys (read from `CanonEnvelope::canonKey()/uidKey()/providerExtrasKey()`,
   never hardcoded) and asserts it is a subset of that domain's catalogue
   ids — reporting every offending key by name if not.

New slots (`tests/calendar/tst_calendar_kind_dispatch.cpp`):

- `calendarCatalogueDeclaresVeventKeys()` — GREEN
- `calendarCatalogueDeclaresVtodoKeys()` — **RED**, `QEXPECT_FAIL("IP.2 / O78: ...", Continue)`
- `calendarCatalogueDeclaresVjournalKeys()` — GREEN
- `todoCatalogueDeclaresVtodoKeys()` — GREEN
- `contactsCatalogueDeclaresVcardKeys()` — GREEN
- `contactsCatalogueDeclaresGooglePersonKeys()` — GREEN
- `contactsCatalogueDeclaresMsContactKeys()` — GREEN

Net: file goes from 6 QTest slots to 12 (5 untouched pre-existing slots +
7 new; the old 1-slot tombstone is removed).

New shared header: `tests/shape/canonkeycoverage.h` (see §1 for the
placement decision this item was asked to justify).

## 1. Placement decision: `tests/shape/canonkeycoverage.h`, not `calendar_test_helpers.h`

The plan offered a choice. Went with a **new file under `tests/shape/`**,
not `tests/calendar/calendar_test_helpers.h`, for two reasons:

1. **The gate is domain-neutral by construction and this item proves it
   spans three domains in one file.** `calendar_test_helpers.h` currently
   holds exactly one calendar-specific helper (`calendarTestRec()`, builds a
   `{calendar,ical}` `CanonicalRecord`). Adding a helper whose very purpose
   is "works identically for calendar, todo, and contacts" to a
   calendar-named file would misname it the moment a second caller (IP.3's
   structural-drift demonstration, or a future contacts-side coverage test)
   wants it from outside `tests/calendar/`.
2. **`tests/shape/` is where the shape-layer's own property/catalogue
   machinery already lives** (`tst_property_catalogue.cpp`,
   `tst_canonjson_diff_merge.cpp`, `tst_loss_profile.cpp`) — this helper
   operates purely on `QJsonObject` + `QList<PropertyId>` +
   `CanonEnvelope`, no calendar/todo/contacts type in sight. It belongs
   next to the tests of the machinery it exercises, not next to one
   caller's fixtures.

Mechanically: `tests/shape/` is not on `tst_calendar_kind_dispatch`'s
include path, and no CMake change was made to add it — the header is
pulled in via a relative quoted include, `#include "../shape/canonkeycoverage.h"`,
which resolves regardless of configured include directories. If a future
item needs this header from a directory that is not a build-tree sibling of
`tests/shape/`, that will need an explicit include-directory addition at
that point; not needed here.

## 2. Fixture design — maximal, and why each one is

Per PLAN.md's explicit warning ("a minimal fixture makes the gate
vacuous"), every fixture was built by reading the actual emitter source
(not the catalogue, not this plan's own field lists) end to end and listing
every top-level canon key it can produce, then constructing wire input that
triggers each one. Full inventories (verified 2026-08-29 against
`fc1ae61`+this session's HEAD):

- **VEVENT** (`src/calendar/eventcanonfields.cpp`): sequence, created,
  lastModified, summary, description, descriptionHtml, location, status,
  classification, timeTransparency, freeBusyStatus, start, allDay, end,
  recurrence, recurrenceId, recurrenceRange, color, categories, url,
  organizer, attendees, priority, alarms, attachments, providerExtras.
  `kMaximalVevent` covers all of these except recurrenceId/recurrenceRange;
  `kMaximalVeventException` (a detached exception occurrence — real
  RECURRENCE-ID VEVENTs do not also carry their own RRULE) covers those
  two. The two objects' key sets are unioned before the subset check — the
  gate only requires each key to appear in *some* promoted instance of the
  pair, not all at once in one physically-realistic instance.
- **VTODO** (`src/todo/vtodocanonfields.cpp`, the emitter **shared** by
  `{calendar,canon}` and `{todo,canon}` per PLAN.md fact 1): created,
  lastModified, summary, description, descriptionHtml, status,
  percentComplete, priority, categories, start, due, completed, recurrence,
  recurrenceId, recurrenceRange, seriesSplitOf, completionAnchor, alarms,
  location, geo, relatedTo, providerExtras, providerExtrasDigest.
  `kMaximalVtodo` + `kMaximalVtodoException` (same master/exception split
  as VEVENT) cover all of these; the slot additionally asserts (before the
  catalogue check) that the three O78 keys are actually present in the
  promoted object, so a future refactor that accidentally stops emitting
  one of them fails loudly here instead of the gate silently going green
  for the wrong reason.
- **VJOURNAL** (`src/calendar/journalcanonfields.cpp`): created,
  lastModified, sequence, summary, description, start, allDay, status,
  classification, color, url, categories, providerExtras. One fixture
  (`maximalJournal`, local to the slot) covers all of these — no
  master/exception split needed since VJOURNAL has no recurrence-id concept
  in its emitter at all.
- **vCard4** (`src/contacts/vcardcanonstages.cpp`): names, nicknames,
  emails, phones, addresses, organizations, urls, imClients, birthday,
  anniversary, gender, notes, photos, categories, languages, timeZone,
  relations, memberships, providerExtras. `kMaximalVcard` covers all of
  these in one vCard (no mutually-exclusive shapes in this emitter).
- **google-person** (`src/contacts/googlepersoncanonstages.cpp`): names,
  emails, phones, addresses, urls, relations, externalIds, memberships,
  imClients, calendarUrls, interests, skills, occupations, languages,
  sipAddresses, nicknames, birthday, gender, notes, photos, organizations,
  providerExtras, plus categories/timeZone/anniversary/significantDates
  **only** via the generic `clientData` `x-canon-*` carrier round-trip
  (Google People has no native home for those four — see the promote
  code's own comment at `googlepersoncanonstages.cpp:777-779`).
  `kMaximalGooglePerson` includes four `clientData` rows
  (`x-canon-categories`, `x-canon-time-zone`, `x-canon-anniversary`,
  `x-canon-significant-dates`) specifically to exercise that path — without
  them this fixture would silently miss four catalogued keys.
- **ms-contact** (`src/contacts/mscontactcanonstages.cpp`): names,
  nicknames, emails, phones, imClients, addresses, organizations,
  occupations, urls, relations, notes, birthday, categories,
  providerExtras, plus gender/anniversary/significantDates/timeZone/
  languages/interests/skills/calendarUrls/sipAddresses/memberships/
  externalIds **only** via the generic `kalburator.canon` open-extension
  `x-canon-*` carrier (no native Graph `contact` home for any of these —
  `mscontactcanonstages.cpp:706-709`). `kMaximalMsContact` includes one
  extension row with all eleven `x-canon-*` keys — **note the promote code
  treats an extension row atomically**: if any one key inside it fails to
  map via `propFromCarrierKey`, the *entire* row is dumped to
  `providerExtras` unpromoted (`mscontactcanonstages.cpp:363-373`,
  `break` on first unmapped key) — so this fixture had to get every key
  spelling right in one shot, verified by first getting a false negative
  (gate almost went green with `x-canon-*` absent from the promoted object
  entirely, immediately obvious from a debug run) and correcting it.

No new files were added under `tests/calendar/fixtures/`. All fixtures live
inline in the test file, matching this file's own pre-existing convention
(`kJournal`/`kVtodo` were already inline `QByteArray` constants before this
item). `tests/calendar/fixtures/` holds real on-disk `.ics` files consumed
via `KALBURATOR_CALENDAR_FIXTURE_DIR` by other tests that need a real
filesystem path (e.g. bulk-load tests); nothing here needed that, so
"extend fixtures ... as needed" resolved to "not needed" for the on-disk
directory specifically, while still adding substantial new fixture content
inline.

## 3. A tooling trap hit and worked around (not in FINDINGS — already a
house rule, O59)

First implementation used `R"json(...)"` raw string literals for the two
JSON vendor fixtures (google-person, ms-contact). Build succeeded but link
failed: `undefined reference to vtable for TestCalendarKindDispatch`, with
moc's own diagnostic ("No relevant classes found") giving no indication
why. This is the exact, already-documented O59 house rule
(`docs/campaign/FINDINGS.md` — "moc silently produces NO output for a
Q_OBJECT class in a translation unit containing a terminated raw string
literal `R"(...)"`"). Fixed by rewriting both fixtures as concatenated
quoted string literals (with escaped inner quotes) instead of raw string
literals — no other change needed, and the file's own moc output confirmed
clean immediately after. Recorded here rather than as a new FINDINGS entry
since O59 already covers it in full; this is a "the trap fired, here's the
scar tissue" note for the next reader of this file who wonders why the two
JSON fixtures look uglier than idiomatic C++.

Also hit, purely a local build-cache accident (not a repo issue, not
recorded as a finding): I deleted an AUTOMOC timestamp-deps directory by
hand while chasing the above and had to re-run `cmake -S . -B build` to
regenerate it. Unrelated to any code change; noted only so a future
transcript reader isn't confused by the `CMakeFiles/Makefile2:...: No such
file or directory` blip that appears in this session's raw log.

## 4. Probe outcomes (verifying the plan's expected result empirically)

- **Confirmed RED for exactly the expected reason.** With the
  `QEXPECT_FAIL` temporarily removed (diagnostic-only, reverted before
  landing), `calendarCatalogueDeclaresVtodoKeys()` fails with:

  ```
  FAIL!  : TestCalendarKindDispatch::calendarCatalogueDeclaresVtodoKeys()
  'offending.isEmpty()' returned FALSE.
  ((calendar, vtodo): catalogue does not declare emitted key(s):
   completionAnchor, providerExtrasDigest, seriesSplitOf)
  ```

  Exactly the three keys PLAN.md predicts, by name, nothing else — the
  gate is not vacuous and not over-firing.
- **Confirmed GREEN for every other pair**, including both contacts legs
  that carry canon keys exclusively through a generic `x-canon-*`/
  `clientData` carrier round-trip (google-person, ms-contact) — those two
  pairs are the closest thing to a second O78-shaped risk in the codebase
  (generic key round-trip through a vendor-specific string carrier,
  independently maintained from the catalogue), and both came back clean.
  This is worth stating plainly: **IP.1 did not find a second live drift**,
  only the one PLAN.md already knew about and asked this item to pin.
- **With `QEXPECT_FAIL` restored, `ctest -R tst_calendar_kind_dispatch`
  reports the file as Passed** (14/14 QTest-level results: 13 PASS + 1
  XFAIL, 0 hard FAIL) — this is the correct ctest-level signal for a
  landed-red-on-purpose gate; it is not masked as a suite failure.

## 5. Corner cases declared-not-executed

- **Did not attempt to combine RRULE and RECURRENCE-ID on the same parsed
  VEVENT/VTODO instance.** A real exception occurrence does not carry its
  own RRULE; rather than gamble on how KCalendarCore's parser behaves with
  both present on one component (untested territory, and irrelevant to
  this item's actual question), each kind gets two fixtures — a
  recurring "master" and a standalone "exception" — with their promoted
  key sets unioned before the subset check. This suffices for the gate's
  purpose (top-level key *presence*, not per-instance semantic validity)
  and avoids introducing an untested KCalendarCore interaction as a
  dependency of this test.
- **Did not attempt every vCard4/Google/MS-Graph field this library will
  eventually support** (e.g. vCard `MEMBER`/`KIND:group` semantics were not
  chased down for exact KContacts parsing behavior) — once a fixture
  produces a comfortably large key set for a pair that was never expected
  to be red, further chasing marginal fields returns no additional
  information for this item's purpose. If IP.3 or a later item needs a
  *more* maximal contacts fixture for its own gate, build one there against
  that item's own needs rather than assuming this one is exhaustive.
- **Did not investigate whether `allDay`'s catalogue entry is vestigial**
  (flagged in PLAN.md/STATUS.md recon notes) — that is explicitly IP.3's
  question, not this item's; not touched here.
- **Did not touch `src/` in any way** — confirmed via `git status` before
  committing (see below); this was a hard constraint from the plan, not a
  judgment call.

## 6. No corrections to PLAN.md's IP.1 section

Everything in the IP.1 section played out as written: the expected-red
pair and its three named keys, the subset semantics, the "report every
offending key by name" requirement, and the choice-and-justify framing for
the helper's location. No wording in that section was found to be wrong or
in need of amendment.

## 7. Findings logged, not fixed (per the "no fix while passing through" rule)

None. Building the maximal fixtures required reading essentially the whole
surface of six promote emitters end to end, and no bug independent of O78
was discovered in that reading (the ms-contact "whole-extension-row atomic
mapping" behavior noted in §2 is working as designed, not a bug — flagged
here only because it shaped how the fixture had to be written, not because
it needs fixing).

## 8. Test evidence

- `tests/calendar/tst_calendar_kind_dispatch.cpp`: 6 → **12** slots (+7 new,
  -1 removed net +6... concretely: 5 pre-existing slots unchanged, 1
  tombstone slot removed, 7 new slots added).
- New file: `tests/shape/canonkeycoverage.h` (header-only, no new `.cpp`,
  no new CMake target — pulled in via relative include from the one caller
  so far).
- `ctest -R tst_calendar_kind_dispatch --output-on-failure`: **Passed**
  (14/14 QTest results — 13 PASS, 1 XFAIL as designed).
- **Full suite:** `ctest --output-on-failure -j$(nproc)` from a full
  `cmake --build build -j$(nproc)`: **214 tests, 210 passed, 4 failed** —
  exactly the four known pre-existing environmental Radicale/KDAV failures
  named in PLAN.md's baseline (`tst_backend_signals`,
  `tst_backend_thread_relocation`, `tst_backend_reentrancy_pin`,
  `tst_remotecalendarbackend`). No new failures, no regressions, and
  `tst_calendar_kind_dispatch` itself is counted among the 210 passed (its
  one intentionally-red assertion is a QTest-level XFAIL, not a ctest-level
  failure).
- `git status` confirmed at commit time: only `tests/`,
  `docs/campaign/incidence-parity/STATUS.md`, and this receipt changed —
  nothing under `src/`.

## 9. Matrix / loss-profile / edge-count housekeeping

Not applicable to this item. No loss profile changed, no `edges()` list
grew, no new vendor pair or domain edge was introduced — IP.1 is a
test-only coverage gate over existing edges. Per PLAN.md §1, the matrix
regeneration and `edges()` grep-pin steps are skipped here (as the task
briefing for this item also states explicitly) and remain IP.2+'s
responsibility where applicable.
