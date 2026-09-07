# Return receipt — IP.6: `incidencecommonfields` extraction

**Delivered:** 2026-09-02
**Consumes:** `docs/campaign/incidence-parity/PLAN.md` §1 (execution
rules, binding), the IP.6 body section, Amendment 1 §A.3.2 ("IP.6 also
owns the O86 GEO decision" + VEVENT's RELATED-TO gap), Amendment 2 §B.5
(GEO settled — drop it); `docs/campaign/FINDINGS.md` O83, O86, O91, O93
(all four RESOLVED by this item; O94 filed); the IP.9 return receipt
(`2026-09-02-ip9-return-receipt.md`, for the `lossByKind` machinery this
item edits the *content* of) and the IP.3 return receipt (contributed-
catalogue convention).
**Scope discipline — `git status` at landing time:**
`CLAUDE.md`, `docs/campaign/FINDINGS.md`,
`docs/campaign/eee/CONVERGENCE-MATRIX.md`, this directory's `STATUS.md`
+ this receipt, `src/calendar/{calendarcanonproperties,eventcanonfields,
icalcanonstages,incidencecommonfields,journalcanonfields}.{h,cpp}` (new:
`incidencecommonfields.{h,cpp}`), `src/todo/{googletaskcanonstages,
mstodotaskcanonstages,todocanonproperties,vtodocanonfields,
vtodocanonstages}.cpp`, `tests/calendar/{tst_calendar_canon_roundtrip,
tst_calendar_kind_dispatch,tst_incidence_rfc5545_fidelity}.cpp`,
`tests/todo/tst_todo_canon_roundtrip.cpp`. Nothing else. Two commits,
strictly separated per PLAN.md's instruction:

- **Commit 1** `1210484` — `refactor(incidence-parity): IP.6 commit 1 —
  incidencecommonfields extraction` — pure structural extraction.
- **Commit 2** — `feat(incidence-parity): IP.6 commit 2 — O83/O86/O91
  field fixes` — the behaviour changes (hash filled in after landing;
  see `git log`).

---

## 0. Summary of decisions (answers to the orchestrator's explicit questions)

- **GEO (O86):** option (b) confirmed and implemented — `geo`
  promote/demote code removed entirely from `vtodocanonfields.cpp` (the
  only place it lived); declared `Dropped` (not `Degraded`) in every
  affected profile. Already ratified by PlanStan (Amendment 2 §B.5);
  this item only executes it.
- **COMMENT/CONTACT (O91) judgment call:** extracted into
  `incidencecommonfields.cpp` and wired to **all three kinds**, including
  VJOURNAL. RFC 5545 §3.6.3's jourprop grammar permits both on VJOURNAL
  (verified against the ABNF myself, not assumed — see §3 below); the fix
  is a one-line common-module call, identical in shape to what VEVENT/
  VTODO needed, so there was no reason to defer it to IP.10.
  `organizer`/`attendees`/`attachments`/`relatedTo` were **NOT** wired for
  VJOURNAL — PLAN.md's IP.10 body explicitly assigns that wiring to IP.10
  ("VJOURNAL should get [these] essentially for free... once IP.6 has
  extracted the common incidence fields" — i.e. IP.6 builds the
  functions, IP.10 does the wiring), and I judged closing that boundary
  cleanly (RECURRENCE-ID identity + the four organizer-shaped fields
  landing together in one IP.10 commit) more valuable than partial credit
  now. `resources` stayed VEVENT+VTODO-only — RFC 5545 jourprop does not
  permit RESOURCES on VJOURNAL at all (verified against the grammar).
- **REQUEST-STATUS:** left permanently uncatalogued (no `PropertyId`
  metadata entry in either catalogue), matching IP.9's precedent — no
  emitter will ever produce it (`KCalendarCore` exposes no accessor
  anywhere), so catalogueing it would misrepresent it as
  emitter-producible. Declared `Dropped` in every relevant `LossProfile`
  instead. Argued as a deliberate consistency choice, not an oversight.
- **O93 resolution:** RESOLVED, exactly as PLAN.md's O93 entry predicted
  — since `{todo,canon}`'s `canonToVtodoLoss()` and `{calendar,canon}`'s
  `canonToVtodoIcalLoss()` demote through the literal same emitter
  (`canonObjectToVtodoBytes()`), fixing the emitter fixed both edges at
  once. `canonToVtodoLoss()` was still edited — to add the genuinely
  permanent drops (`geo`, `requestStatus`, and O94's `resources`) it had
  never declared at all, closing the "undeclared loss" half of O93 too.
  New pin: `canonToVtodoLossProfileMatchesFixedEmitter()`.
- **New finding O94** (upstream, not fixable in an emitter): filed and
  resolved-by-declaration in the same session — see §4.

---

## 1. Commit 1 — structural extraction (behaviour-identical)

### 1.1 What was actually genuinely identical today (verified, not assumed)

PLAN.md's IP.6 body listed a longer candidate set (`sequence, summary,
description/descriptionHtml, classification, categories, url, color,
location, geo, organizer, attendees, attachments`) as "the fields that
are genuinely identical across all three kinds." I read all three
emitters' full bodies (`eventcanonfields.cpp`, `vtodocanonfields.cpp`,
`journalcanonfields.cpp` as they stood at `60e1ac3`) before extracting
anything, and the claim does **not** hold for most of that list:

| Field | VEVENT | VTODO | VJOURNAL | All 3 identical? |
|---|---|---|---|---|
| created/lastModified (O41 guard) | yes | yes | yes | **YES** |
| summary | yes | yes | yes | **YES** |
| description | yes | yes | yes | **YES** |
| categories | yes | yes | yes | **YES** |
| X-prop passthrough → providerExtras | yes (skips 2 keys) | yes (no skip, +digest) | yes (no skip) | **YES**, parameterized |
| sequence | yes | **no** (O83) | yes | no — VTODO missing |
| classification | yes (guarded insert) | **no** (O83) | yes (**unconditional** insert — the O91/IP.10 phantom-key bug) | no — VTODO missing, and VJOURNAL's own impl differs from VEVENT's |
| color | yes | **no** (O83) | yes | no — VTODO missing |
| url | yes | **no** (O83) | yes | no — VTODO missing |
| location | yes | yes | **no** (RFC-correct: no LOCATION in jourprop) | no — but VJOURNAL's absence is RFC-correct, not a defect |
| priority | yes | yes | **no** (RFC-correct: no PRIORITY in jourprop) | no — same as location |
| organizer/attendees/attachments | yes | **no** (O83) | **no** (O87) | no — two kinds missing |
| geo | no | yes (corrupt, O86) | N/A (RFC-excludes) | no — not even 2-of-3 agree on *whether* to have it |
| relatedTo | **no** (Amendment §A.3.2) | yes | **no** (O87) | no — only VTODO has it |
| descriptionHtml | yes | yes | **no** | no — deliberately deferred to IP.10 per PLAN.md's own IP.10 text |

So commit 1's extraction is deliberately **smaller** than PLAN.md's
starting list: `created`/`lastModified`, `summary`, `description`,
`categories`, and the X-prop passthrough. Everything else either (a) is
genuinely a commit-2 fix (a kind gains a field it lacked), or (b) is
correctly kind-specific per RFC 5545 and stays untouched
(`location`/`priority` on VJOURNAL), or (c) is explicitly out of this
item's scope (VJOURNAL's `descriptionHtml` and phantom-classification-
insert bug, both named as IP.10's in PLAN.md's own IP.10 body — I did not
touch either).

### 1.2 What landed

New `src/calendar/incidencecommonfields.{h,cpp}`, operating on
`KCalendarCore::Incidence::Ptr` (the base class all three kinds share)
rather than any subclass. Functions used by commit 1:
`promoteTimestamps`/`demoteTimestamps`/`stripInjectedTimestamps` (the O41
guard, now one copy instead of three — retiring the exact recurrence
`journalcanonfields.cpp:53`'s old comment described),
`promoteSummaryDescription`/`demoteSummaryDescription`,
`promoteCategories`/`demoteCategories`,
`promoteCustomPropertyPassthrough`/`demoteCustomPropertyPassthrough`
(parameterized by an optional `QSet<QByteArray>` skip-list, preserving
VEVENT's `{X-ALT-DESC, X-MICROSOFT-CDO-BUSYSTATUS}` skip and VTODO's own
`providerExtrasDigest` stamp, which stayed local to
`vtodocanonfields.cpp` — O80/IP.5 scope, not touched).

**Procedural note, argued honestly rather than hidden:** the full
function set this item ultimately needs (`promoteSequence` through
`promoteResources`/`demoteResources`, i.e. everything commit 2 wires up)
was written into `incidencecommonfields.{h,cpp}` in commit 1, since the
module was designed as a whole before the two-commit split was staged.
Commit 1 remains genuinely behaviour-identical — those extra functions
have **zero call sites** anywhere in commit 1's diff, and a function with
no caller has no runtime effect, confirmed by the unchanged 215/211 suite
run below. PLAN.md's actual requirement — "a reviewer must be able to
see, in one diff, exactly which fields VTODO gained" — is satisfied by
commit 2's diff to `vtodocanonfields.cpp`/`eventcanonfields.cpp`, which
shows precisely which `promote*`/`demote*` calls each file newly makes.
`incidencecommonfields.h/incidencecommonfields.cpp` carry no further diff
in commit 2 (all functions were already there); only their *callers*
change.

`eventcanonfields.cpp`, `vtodocanonfields.cpp`, `journalcanonfields.cpp`
rewired to call the four commit-1 functions and shed their own copies.
`CMakeLists.txt` gained the new source file.

### 1.3 Verification

Full build clean (`cmake --build build -j8`, zero errors, one
pre-existing unrelated deprecation warning in `vtodocanonfields.cpp`
about `QDateTime(QDate,QTime,Qt::TimeSpec,int)`, not touched by this
item). Full suite: **215 tests, 211 passed, 4 failed** — the same
`tst_backend_signals`/`tst_backend_thread_relocation`/
`tst_backend_reentrancy_pin`/`tst_remotecalendarbackend` environmental
set, verified by failure TEXT (KDAV 30s-transfer-timeout, Radicale
412/409), not by name. No test file changed in commit 1 (as expected for
a behaviour-identical refactor).

---

## 2. Commit 2 — the field fixes

### 2.1 VTODO gains O83's seven fields

`vtodocanonfields.cpp` now calls `promoteSequence`/`demoteSequence`,
`promoteClassification`/`demoteClassification`, `promoteColor`/
`demoteColor`, `promoteUrl`/`demoteUrl`, `promoteOrganizer`/
`demoteOrganizer`, `promoteAttendees`/`demoteAttendees`,
`promoteAttachments`/`demoteAttachments` — the exact same functions
`eventcanonfields.cpp` was rewired to call in the same commit (its own
inline implementations of organizer/attendees/attachments/classification/
color/url/sequence moved into `incidencecommonfields.cpp` verbatim, then
both callers point at the shared copy). `classification` is deliberately
**not** wired for VJOURNAL — see §0's judgment-call note; VJOURNAL's own
unconditional-insert implementation is untouched, phantom-key bug and
all, left for IP.10 exactly as PLAN.md's IP.10 body assigns it.

### 2.2 VEVENT gains RELATED-TO

`promoteRelatedTo`/`demoteRelatedTo` extracted from VTODO's existing,
already-correct implementation (unchanged logic — `RelTypeParent` only,
matching KCalendarCore's own limitation) and called from
`eventcanonfields.cpp` too.

### 2.3 GEO dropped entirely (O86)

Removed both blocks from `vtodocanonfields.cpp` (promote: the
`hasGeo()`/`geoLatitude()`/`geoLongitude()` block; demote: the
`geoObj`/`setGeoLatitude`/`setGeoLongitude` block) — this was the only
place in the library `geo` was ever promoted or demoted. Removed from
`vtodoCanonContributedIds()`. Left the `geo` metadata entries in both
`calendarPropertyMetadata()` and `todoPropertyMetadata()` — argued
deliberately: `geo` is still a valid RFC 5545 property, and a future item
representing it differently (e.g. a display-only, non-round-tripped
field) would want the metadata already in place rather than reinventing
it; an orphaned metadata entry costs nothing (it simply never appears in
either catalogue's actual `properties()` list any more, since no
contributor produces it).

### 2.4 COMMENT/CONTACT/RESOURCES (O91)

`promoteComments`/`demoteComments` and `promoteContacts`/`demoteContacts`
wired to VEVENT, VTODO **and** VJOURNAL. `promoteResources`/
`demoteResources` wired to VEVENT and VTODO only (RFC-excluded from
VJOURNAL, verified against §3.6.3's jourprop grammar: `jourprop = *(
class / created / description / dtstart / dtstamp / last-mod / organizer
/ recurid / seq / status / summary / uid / url / *(attach / attendee /
categories / comment / contact / exdate / related / rdate / rrule /
rstatus / x-prop / iana-prop) )` — `comment`/`contact` are in the
repeatable optional set, `resources` is not present at all).

### 2.5 O94 — RESOURCES does not actually round-trip through KCalendarCore's ICalFormat (new, upstream)

While writing round-trip coverage for O91's fields I found `RESOURCES`
missing from the demoted output on both VEVENT and VTODO, even though
canon correctly carried it after promote. Direct two-part probe
(temporarily added to `tst_incidence_rfc5545_fidelity.cpp`, removed
before landing — not part of the committed diff):

1. Parse `RESOURCES:Projector,VCR` from a real source line, read
   `incidence->resources()` immediately after — **empty**. The parser
   never populates it.
2. Fresh `Event`, `setResources({"Projector","VCR"})`,
   `resources()` reads it back correctly (object model is genuinely
   fine), then `ICalFormat::toICalString()` — **no RESOURCES line in the
   output at all**. The writer silently discards it.

This directly falsifies O91's own claim ("verified they round-trip fine
through KCalendarCore's own ICalFormat") for RESOURCES specifically —
verified TRUE for COMMENT/CONTACT in the identical fixture, so the
mistake was RESOURCES-specific, not a wholesale misreading. Filed as
**O94**, resolved the same session by the same reasoning PLAN.md
prescribes for O86: kept the correct object-model code (useful for any
non-`ICalFormat` caller, forward-compatible with a future kcalendarcore
fix), declared `resources: Dropped` on the three affected ical wire
edges. Full text: `docs/campaign/FINDINGS.md` O94.

### 2.6 Catalogue updates

`calendarcanonproperties.cpp`: added `PropertyKind::StringList` metadata
for `comments`/`contacts`/`resources` (new ids). `sequence`/
`classification`/`color`/`url`/`organizer`/`attendees`/`attachments`
already had metadata there (VEVENT already contributed them before this
item) — no change needed.

`todocanonproperties.cpp`: added the same three StringList entries, PLUS
`sequence`(Integer)/`classification`(String)/`color`(String)/`url`(String)/
`organizer`(Json)/`attendees`(Json)/`attachments`(Json) — **none of these
seven had a metadata entry in the todo catalogue before this item**
(confirmed by reading the file; the todo domain's own catalogue never
needed them because `vtodoCanonContributedIds()` never produced them).
Kind/display-name values copied verbatim from
`calendarcanonproperties.cpp` to keep the two catalogues in the
established lock-step convention (matching `seriesSplitOf`/
`completionAnchor`/`providerExtrasDigest`'s precedent).

`requestStatus`: deliberately left uncatalogued in both files — see §0.

### 2.7 Loss profile updates (exact diff, by function)

**`canonToIcalLoss()` (VEVENT, `icalcanonstages.cpp`)** — gained:
`geo: Dropped`, `requestStatus: Dropped`, `resources: Dropped`. Nothing
removed (RELATED-TO was never declared here — it was the undeclared O83-
class drop this item fixes, so there was nothing to remove).

**`canonToVtodoIcalLoss()` (VTODO via `{calendar,canon}`,
`icalcanonstages.cpp`)** — REMOVED: `attachments`, `attendees`,
`classification`, `color`, `organizer`, `sequence`, `url`, `comments`,
`contacts` (all now round-trip). `geo` CHANGED from `Degraded` to
`Dropped` (no longer emitted at all, not merely corrupted). KEPT:
`requestStatus: Dropped`. ADDED: `resources: Dropped` (O94).

**`canonToVjournalLoss()` (`journalcanonfields.cpp`)** — REMOVED:
`comments`, `contacts` (now round-trip, per the §0 judgment call). KEPT
unchanged: `attachments`, `attendees`, `organizer`, `relatedTo`,
`recurrenceId`, `recurrence` (all IP.10's), `requestStatus` (permanent).

**`canonToVtodoLoss()` (VTODO via `{todo,canon}`,
`vtodocanonstages.cpp`, the O93 fix)** — previously declared NONE of
O83/O91's drops (that was O93's own finding). Now declares exactly the
same permanent trio as `canonToVtodoIcalLoss()`: `geo: Dropped`,
`requestStatus: Dropped`, `resources: Dropped`. Everything else stayed
as it was (`linkedResources`, `descriptionHtml`, `checklistItems`,
`sortOrder`, `parentUid`, `completionAnchor`, `seriesSplitOf`: Reversible;
`status`, `recurrenceRange`: Degraded; `providerExtrasDigest`: Dropped) —
none of that content is O83/O91-related.

**Google Tasks (`canonToGoogleTaskLoss()`)** — added all ten new ids
(`sequence`, `classification`, `color`, `url`, `organizer`, `attendees`,
`attachments`, `comments`, `contacts`, `resources`) to the existing
Dropped loop. Verified by reading `googletaskcanonstages.cpp`'s
promote/demote code end to end — no wire field, no generic carrier
mechanism exists in this file (unlike MS To-Do), so Dropped is the
honest declaration for every one of them.

**MS To-Do (`canonToMsTodoTaskLoss()`)** — added all ten to the existing
Reversible loop, **not** Dropped. This required actually reading the
promote/demote code, not just declaring by analogy with Google Tasks:
`mstodotaskcanonstages.cpp` has a genuinely generic "unhandled canon
props → open-extension carrier" mechanism (`carrierKey()`/
`propFromCarrierKey()`, kebab-case ↔ camelCase, `valueToCarrierString()`/
`carrierStringToValue()` round-tripping arbitrary JSON scalars/objects/
arrays through a string-valued Microsoft Graph open extension property).
None of the ten new ids is in that file's `handled` set (the list of
canon keys the auto-carry loop skips because they already have a native
Graph field), so all ten fall through into the auto-carry loop exactly
like the pre-existing `percentComplete`/`relatedTo`/`geo`/etc entries —
genuinely Reversible with **zero code changes** to
`mstodotaskcanonstages.cpp` itself, only the loss-profile declaration.

### 2.8 IP.8's gate (`tst_incidence_rfc5545_fidelity.cpp`)

- `propertyIdToRfcNames()` gained `geo → GEO` (needed once
  `canonToVtodoIcalLoss()` started declaring `geo` as `Dropped` — without
  this the `droppedRfcNames()` translator's `Q_ASSERT_X` would fire).
- `expectedLossTable()`: VEVENT's literal shrank from six entries
  (`COMMENT, CONTACT, GEO, RELATED-TO, REQUEST-STATUS, RESOURCES`) to
  three permanent ones (`GEO, REQUEST-STATUS, RESOURCES`). VTODO's
  (derived) list shrank to the same three, and `expectFixpoint` flipped
  `false → true` (O86 resolved — no more corrupted-not-dropped GEO to
  break the fixpoint). VJOURNAL's (derived) list lost `COMMENT`/
  `CONTACT`.
- The per-kind named `QEXPECT_FAIL` breakdown in `runKindCase()`:
  VEVENT's and VTODO's blocks **removed entirely** — every property they
  covered is now either fixed (no assertion needed) or a permanent
  drop with no future item to attribute a `QEXPECT_FAIL` to (folded
  into the real-gate literal/derived list instead, the same treatment
  `onlineMeeting`/`eventType` already get with no dedicated assertion).
  VJOURNAL's ATTACH/ATTENDEE/EXDATE/ORGANIZER/RECURRENCE-ID/RELATED-TO/
  RRULE/RDATE block is **unchanged** (still IP.10's). VJOURNAL's second
  block (COMMENT/CONTACT/REQUEST-STATUS) **removed** — COMMENT/CONTACT
  fixed, REQUEST-STATUS folded into the permanent list.
- Non-vacuity: ran the full gate before any of these edits (confirmed 2
  real `FAIL!`s at exactly the properties this item fixes — the O94
  RESOURCES surprise was found this way), then after (13→11 slots
  reporting, all green, `Totals: 11 passed, 0 failed`).

### 2.9 New test coverage

`tests/calendar/tst_calendar_kind_dispatch.cpp`: both IP.9-era pins
(`vtodoDemoteLossProfileIsVtodoShapedNotEventShaped`,
`vjournalDemoteLossProfileIsVjournalShapedNotEventShaped`) rewritten to
match the new profile content, each gaining a positive "must NOT still
carry the fixed properties" regression guard (so a future accidental
re-add is caught, not just a missing-drop).

`tests/todo/tst_todo_canon_roundtrip.cpp`: four new slots —
`vtodoRoundTripPreservesO83Fields` (all seven, both directions, via
`VTodoToCanonStage`/`CanonToVTodoStage`), `vtodoCommentsContactsRoundTrip
ResourcesDoesNot` (proves COMMENT/CONTACT survive and RESOURCES does not,
pinning O94 on this edge specifically), `vtodoNeverPromotesGeoAnyMore`
(proves geo is never promoted, AND that a stale pre-existing canon
record's `geo` key is not re-emitted on demote), `canonToVtodoLoss
ProfileMatchesFixedEmitter` (the O93 resolution, pinned directly against
the real registered profile).

`tests/calendar/tst_calendar_canon_roundtrip.cpp`: one new slot,
`icalRoundTripPreservesRelatedToCommentsContacts` (VEVENT gains all
three, both directions), plus three new `QCOMPARE`s in the existing
`canonToIcalLossProfileChargesDroppedAndReversible` slot for `geo`/
`requestStatus`/`resources`.

Total new QTest slots this item: 4 (todo roundtrip) + 1 (calendar
roundtrip) + 0 new files = 5 new slots inside existing ctest binaries
(plus the two rewritten kind-dispatch slots, not counted as "new"). Per
IP.3/IP.9's own precedent, the ctest-level executable count does not
move — 215 before, 215 after.

### 2.10 Matrix

Regenerated with `./build/tools/matrixgen/matrixgen`. Diff (39 lines
changed, confirmed by `git diff --stat`) matches §2.7's loss-profile
edits line for line: new `geo`/`requestStatus`/`resources` rows appear
on the VEVENT `canon → ical` and VTODO `canon → ical (vtodo)` sections;
`comments`/`contacts` rows disappear from the VTODO `canon → ical
(vtodo)` and `canon → vjournal` sections; ten new rows appear on both
`canon → google-task` and `canon → ms-todotask` (`Dropped` on the
former, `Reversible` on the latter). No section outside those six
changed. `tst_gm_pipeline_convergence` green (verified before and after
— red immediately after the loss-profile edits with the OLD committed
matrix, green after regenerating, confirming the byte-pin is a real
check).

No edge added or removed by this item — `CalendarStockShapes::edges()`
and `TodoStockShapes::edges()` are unchanged; only existing edges'
`LossProfile` *content* changed. House rule O63's "grep edge-count pins
in the same commit that grows an `edges()` list" is therefore not
applicable — confirmed by grepping `edges().size()` pins across
`tests/` and finding all of them still assert 9 for the calendar/todo/
contacts stock-shape registries, which this item did not touch.

---

## 3. Full suite results

**After commit 1:** 215 tests, 211 passed, 4 known-environmental failed
(same set/signatures as baseline).

**After commit 2:** 215 tests, 211 passed, 4 known-environmental failed
(`tst_backend_signals`, `tst_backend_thread_relocation`,
`tst_backend_reentrancy_pin`, `tst_remotecalendarbackend`) — verified by
failure TEXT: `tst_backend_reentrancy_pin` shows the documented
"requested timeout (15000 ms) was too short, ... would have been
sufficient" pattern against the local Radicale's 30s KDAV timeout;
`tst_backend_signals`/`tst_remotecalendarbackend` show 412/409 responses
from the same local Radicale instance; `tst_backend_thread_relocation`
aborts on the same KDAV-timeout class. All four reproduce independent of
this item's changes (verified by re-reading the exact same failure
signatures the IP.3/IP.9 receipts already documented at prior commits).

ctest executable count: 215 → 215 (unchanged — new slots landed inside
existing binaries, per IP.3/IP.9's precedent). QTest-slot count grew:
+5 new slots, +2 slots meaningfully rewritten (not just touched).

---

## 4. Findings

**RESOLVED this item:** O83 (VTODO's seven undeclared drops — closed by
extraction), O86 (GEO — dropped entirely, ratified decision executed),
O91 (COMMENT/CONTACT/RESOURCES/REQUEST-STATUS — COMMENT/CONTACT fixed on
all three kinds, RESOURCES corrected to a declared-Dropped upstream gap
by O94, REQUEST-STATUS declared Dropped permanently), O93 (`{todo,canon}`
sibling loss profile — resolved as a byproduct of fixing the shared
emitter, plus its own missing permanent-drop declarations added).

**Filed this item:** O94 — `KCalendarCore::ICalFormat` 6.29.0 never reads
or writes a `RESOURCES` property on the wire, despite the object model
(`resources()`/`setResources()`) working correctly; corrects part of
O91's own claim. Full text in `docs/campaign/FINDINGS.md`. Not owned by
any future item for a code-level fix — there is no fix short of
hand-serializing a line, which the same doctrine that settled O86
explicitly forbids. Logged for a future kcalendarcore-version re-check,
same as O86's own note.

**Not fixed, not owned by this item, logged only (per PLAN.md §1's "no
fix while passing through" prohibition):**

- `journalcanonfields.cpp`'s phantom unconditional-`classification`-insert
  bug (a VJOURNAL with no CLASS gains `classification: "public"` in
  canon) — untouched, exactly as PLAN.md's IP.10 body assigns it. I did
  NOT route VJOURNAL through the new common `promoteClassification`/
  `demoteClassification` (which would have silently fixed this as a
  byproduct) — confirmed by reading `journalcanonfields.cpp` after
  landing: its classification block is byte-identical to before this
  item.
- O87's remaining scope (RECURRENCE-ID identity, RRULE/RDATE/EXDATE,
  organizer/attendees/attachments/relatedTo wiring for VJOURNAL,
  `descriptionHtml`) — all still IP.10's, untouched.
- O92 (`CanonJsonMerger` has no fail-loud channel for a kind mismatch) —
  unrelated to this item's scope, not touched.

---

## 5. Deviations from PLAN.md, and why

1. **Commit 1's extraction set is smaller than PLAN.md's starting list**
   — argued in full in §1.1. The plan's own text anticipates this
   ("PLAN.md's list is a starting point, not gospel" per the orchestrator
   brief); I verified every field against the real code before deciding.
2. **`incidencecommonfields.{h,cpp}`'s full function set landed in
   commit 1**, not incrementally split across both commits — a
   procedural shortcut, argued not to violate PLAN.md's actual
   requirement (reviewer visibility of which fields VTODO/VEVENT
   *gained*, which commit 2's caller-side diffs still show cleanly). See
   §1.2.
3. **VJOURNAL's COMMENT/CONTACT wired early**, ahead of IP.10, contrary
   to PLAN.md's own IP.10-body attribution — a judgment call this item
   was explicitly asked to make (see the orchestrator's task brief §4),
   decided and recorded in §0. STATUS.md's IP.10 row and its own body
   text are corrected to reflect this so IP.10 does not duplicate the
   fix or get confused reading the stale PLAN.md attribution.
4. **O94 is new** — not anticipated by any prior item's findings; found
   incidentally while building this item's own round-trip coverage,
   resolved the same session per PLAN.md §1's allowance for fixing (or
   here, honestly declaring) something surfaced while working an item
   actually scoped to touch that surface (distinct from "fixing while
   passing through" something outside scope — RESOURCES's loss profile
   is squarely inside IP.6's `canonToVtodoIcalLoss()`/`canonToIcalLoss()`/
   `canonToVtodoLoss()` scope).

---

## 6. Acceptance criteria checklist

- [x] IP.1's gate (`tst_calendar_kind_dispatch.cpp`) still green,
      structurally — every new id reaches both catalogues via
      `vtodoCanonContributedIds()`/`eventCanonContributedIds()`/
      `journalCanonContributedIds()`, no hand-patching.
- [x] Round-trip slots for each newly-promoted VTODO field, on the vtodo
      leg (`tests/todo/tst_todo_canon_roundtrip.cpp`) and through
      `{calendar,canon}` (`tests/calendar/tst_incidence_rfc5545_fidelity.cpp`
      + `tst_calendar_canon_roundtrip.cpp`).
- [x] Loss profiles updated for the todo vendor legs, each new gap
      declared honestly (`Dropped` for Google Tasks, `Reversible` for MS
      To-Do — verified against each file's actual promote/demote code,
      not assumed identical).
- [x] VJOURNAL keeps every field it had today, plus gains COMMENT/CONTACT
      (judgment call) — its RECURRENCE-ID/RRULE/RDATE/EXDATE/organizer/
      attendees/attachments/relatedTo/descriptionHtml/phantom-
      classification-bug scope is untouched, confirmed by diff.
- [x] Matrix regenerated; `tst_gm_pipeline_convergence` green; diff
      explained in §2.10.
- [x] Full suite green except the 4 known-environmental slots, both
      commits, verified by failure TEXT not name.

---

## 7. Files changed

`CLAUDE.md`, `docs/campaign/FINDINGS.md`,
`docs/campaign/eee/CONVERGENCE-MATRIX.md`,
`docs/campaign/incidence-parity/STATUS.md`,
`docs/campaign/incidence-parity/2026-09-02-ip6-return-receipt.md` (this
file), `src/calendar/calendarcanonproperties.cpp`,
`src/calendar/eventcanonfields.cpp`, `src/calendar/icalcanonstages.cpp`,
`src/calendar/incidencecommonfields.h`,
`src/calendar/incidencecommonfields.cpp` (new, commit 1),
`src/calendar/journalcanonfields.cpp`, `src/todo/googletaskcanonstages.cpp`,
`src/todo/mstodotaskcanonstages.cpp`, `src/todo/todocanonproperties.cpp`,
`src/todo/vtodocanonfields.cpp`, `src/todo/vtodocanonstages.cpp`,
`tests/calendar/tst_calendar_canon_roundtrip.cpp`,
`tests/calendar/tst_calendar_kind_dispatch.cpp`,
`tests/calendar/tst_incidence_rfc5545_fidelity.cpp`,
`tests/todo/tst_todo_canon_roundtrip.cpp`, `CMakeLists.txt` (commit 1,
new source file registration).
