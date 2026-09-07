# Return receipt — IP.10: VJOURNAL parity

**Delivered:** 2026-09-02
**Consumes:** `docs/campaign/incidence-parity/PLAN.md` §1 (execution
rules, binding) and the IP.10 body section (Amendment 1 §A.4);
`docs/campaign/FINDINGS.md` O87 (the defect this item closes); the IP.6
return receipt (`2026-09-02-ip6-return-receipt.md`, for
`incidencecommonfields.cpp`'s current shape and the exact judgment calls
IP.6 already made for VJOURNAL); `src/calendar/eventcanonfields.cpp` and
`src/todo/vtodocanonfields.cpp` as the two reference shapes named in the
task brief (recurrence verbatim-lines convention; VTODO's W1 composite
exception identity + W3 safety fix, explicitly NOT VEVENT's still-buggy
O82 version).
**Scope discipline — `git status` at landing time:** `CLAUDE.md`,
`docs/campaign/FINDINGS.md` (+O95, +O96),
`docs/campaign/eee/CONVERGENCE-MATRIX.md`,
`docs/campaign/incidence-parity/STATUS.md` + this receipt,
`src/calendar/incidencecommonfields.h` (doc-comment updates only — see
§4), `src/calendar/journalcanonfields.cpp`,
`tests/calendar/{tst_calendar_kind_dispatch,tst_incidence_rfc5545_fidelity}.cpp`.
Nothing else — in particular `incidencecommonfields.cpp` (the function
bodies), `eventcanonfields.cpp`, `vtodocanonfields.cpp` and
`icalcanonstages.cpp` are all untouched; this item only **calls**
functions IP.6/IP.9 already built.

---

## 0. Summary

VJOURNAL was the least-attended, worst-measured kind in the campaign
(O87: seven undeclared drops, one — RECURRENCE-ID — identity corruption
rather than mere field loss). It is now at parity with VEVENT/VTODO on
everything RFC 5545 §3.6.3's jourprop grammar permits, with one honestly
declared exception (`RELATED-TO`, blocked upstream — O95, not this item's
to fix) and one honestly declared pre-existing sibling gap noticed along
the way (O96, also not this item's to fix).

**What was genuinely free** (PLAN.md's own prediction, verified rather
than assumed): `organizer`, `attendees`, `attachments`,
`classification` — four calls into `incidencecommonfields.cpp`'s
already-guarded, already-generic (`KCalendarCore::Incidence::Ptr`-typed)
functions, zero new field-specific logic. `comments`/`contacts` were
already done by IP.6 (verified by reading `journalcanonfields.cpp` before
touching it — confirmed present, not re-added).

**What needed new code**, all modeled on an existing shape rather than
invented: RECURRENCE-ID identity + `recurrenceRange` (VTODO's W1/W3
shape), the verbatim `recurrence` lines (VEVENT's/VTODO's convention),
and `descriptionHtml` (VEVENT's/VTODO's inline X-ALT-DESC pattern — not
extracted into `incidencecommonfields.cpp`, matching IP.6's own decision
to leave that one un-extracted).

**One exception found mid-item, not anticipated by PLAN.md**: wiring
`relatedTo` compiles and runs cleanly, but `KCalendarCore::ICalFormat`'s
VJOURNAL parser silently never populates `relatedTo()` from a source
`RELATED-TO` line — a narrow upstream gap (§3, O95).

---

## 1. RECURRENCE-ID identity (the most important part of this item)

Modeled on `vtodocanonfields.cpp`'s promote (~340-363) and demote
(~706-730), NOT `eventcanonfields.cpp`'s (which still carries the O82 bug
— unconditional `RANGE=THISANDFUTURE` re-emission — explicitly IP.7's to
fix, not copied here per the task brief's caution).

- **Promote**: `journal->recurrenceId()` valid ⇒ emit
  `{recurrenceId: {dateTime: <UTC ISO>}}`; `journal->thisAndFuture()` ⇒
  additionally emit `recurrenceRange: "thisAndFuture"` (a read-side-only
  fact, per the demote-side comment below).
- **Demote**: canon's `recurrenceId.dateTime` present ⇒
  `journal->setRecurrenceId(dt)` **and** `journal->setThisAndFuture(false)`
  **unconditionally** — the W3 safety rule, regardless of what canon's
  `recurrenceRange` says. `RANGE=THISANDFUTURE` is write-hostile on real
  CalDAV servers; VTODO's own comment (quoted verbatim in
  `journalcanonfields.cpp`'s new block) explains why this is a hard
  backstop, not merely "the common case."

**Why this is the fix, precisely**: before this item, `journalFieldsToCanon()`
had zero references to `recurrenceId()` at all — a detached VJOURNAL
instance and its master promoted to canon objects sharing the same `uid`
with literally nothing distinguishing them (O87's identity-corruption
finding). After this item, the exception's canon object carries
`recurrenceId` and the master's does not.

**Test**: `vjournalMasterAndExceptionRemainDistinctThroughRoundTrip`
(`tests/calendar/tst_calendar_kind_dispatch.cpp`) — promotes a maximal
master and a detached-exception fixture sharing `UID:journal-maximal-1`,
round-trips both through demote→promote, then asserts: (a) same `uid`;
(b) master has no `recurrenceId` key; (c) exception's `recurrenceId.dateTime`
matches the source `RECURRENCE-ID` value; (d) the two canon **objects as a
whole** are not equal (`QVERIFY2(masterObj != exceptionObj, ...)` — a
non-vacuity check: a passing `recurrenceId`-only assertion would not catch
a hypothetical future bug that collapsed every OTHER field too).
`vjournalMasterHasNoRecurrenceId`, `vjournalRoundTripPreservesRecurrenceId`,
and `vjournalDemoteNeverEmitsThisAndFutureRange` pin the individual pieces
(byte-identical RECURRENCE-ID line, real `hasRecurrenceId()`/
`thisAndFuture()` state, RANGE never re-emitted) — direct analogues of
`tst_todo_canon_roundtrip.cpp`'s `vtodoRoundTripPreservesRecurrenceId` /
`vtodoMasterHasNoRecurrenceId` / `vtodoDemoteNeverEmitsThisAndFutureRange`.

---

## 2. RRULE / RDATE / EXDATE (verbatim lines)

Wired via `extractComponentRecurrenceLines(originalBytes, "VJOURNAL", uid)`
— the exact same function VEVENT/VTODO call, same "recurrence" canon key
(covers all three RFC properties at once, per invariant 3 — never
re-derived, always captured verbatim). Promote builds the JSON array;
demote re-injects the lines as literal text before the `END:VJOURNAL`
marker (mirrors `eventcanonfields.cpp`'s `END:VEVENT` injection exactly,
same string-search-and-insert mechanism).

**Test**: `vjournalRoundTripPreservesRecurrenceVerbatim` — RRULE, RDATE
and EXDATE all present in `kMaximalVjournal`, all asserted
byte-identical in the demoted output, plus a real `outJournal->recurs()`
check confirming KCalendarCore itself parses the re-injected RRULE back
into a live recurrence.

---

## 3. `RELATED-TO` — wired, but blocked upstream on the promote side (O95)

`journalFieldsToCanon()`/`canonObjectToJournalBytes()` call the exact same
`promoteRelatedTo()`/`demoteRelatedTo()` VEVENT/VTODO use — no VJOURNAL-
specific code, no difference from the organizer/attendees/attachments
wiring. It does not work end-to-end, and the reason is entirely outside
this codebase.

**Probe (KCalendarCore 6.29.0 alone, no libkalburator involved)**:
1. Parse a VJOURNAL carrying `RELATED-TO:parent-uid-1`; read
   `relatedTo(RelTypeParent)` — **empty**.
2. Parse the IDENTICAL line on a VEVENT instead — reads back
   `"parent-uid-1"` correctly.
3. Construct a `Journal`, `setRelatedTo("x", RelTypeParent)`, serialize —
   output correctly contains `RELATED-TO:x`.

So the WRITE/demote direction is genuinely lossless; only the parser
(promote direction) silently drops it, for VJOURNAL specifically. This is
a new upstream-defect shape not seen elsewhere in the campaign — O86
(GEO) corrupts on write with a correct parser, O94 (RESOURCES) is
silent-no-op in both directions; this one is broken read-only.

**Handling, mirroring O94's precedent exactly**: kept
`promoteRelatedTo`/`demoteRelatedTo` wired (correct against the object
model, exercises the WRITE direction losslessly for any canon-sourced
`relatedTo`, forward-compatible with a future kcalendarcore fix).
`canonToVjournalLoss()` correctly does NOT declare `relatedTo` — that
function is scoped (by its own doc comment) to the canon→vjournal DEMOTE
direction, which is not lossy here; declaring it there would misdescribe
which direction is actually broken. The RFC 5545 fidelity gate instead
adds `RELATED-TO` to vjournal's expected-lost list **by hand**, via a new
`vjournalExpectedLostList()` helper (`tst_incidence_rfc5545_fidelity.cpp`)
documented inline with the reasoning — the shape graph has no
promote-direction loss-profile mechanism to derive this from at all (a
structural gap; `calendarstockshapes.cpp`'s `{calendar,ical}→{calendar,canon}`
edge is a hard-coded, non-kind-scoped `LossProfile{}` for every kind).
Filed as **FINDINGS O95**, not owned by any item.

`journalCanonContributedIds()` and the catalogue metadata still list
`relatedTo` (matching O94's precedent for `resources`: the function
genuinely CAN produce it, structurally, for any Journal object not
sourced via `ICalFormat::fromString`).

---

## 4. `classification` — phantom-key bug closed

`journalcanonfields.cpp:81` used to run
`obj.insert("classification", classToString(journal->secrecy()))`
**unconditionally**, so a VJOURNAL with no `CLASS` property gained
`classification: "public"` in canon out of nowhere. Switched to the
already-guarded `promoteClassification()`/`demoteClassification()` from
`incidencecommonfields.cpp` (the same functions VEVENT/VTODO already
use) — this is a pure wiring change, no new logic; the local
`classToString()`/`journalStatusToString()`-adjacent helper is removed
(dead after the switch; `journalStatusToString`/`journalStatusFromString`
remain, they are STATUS-specific and untouched). Confirmed fixed by the
RFC 5545 fidelity gate's `gained.isEmpty()` sanity net, which explicitly
existed to catch exactly this class of regression.

`incidencecommonfields.h`'s doc comments (touched, no logic) — updated
five stale "IP.10's job" / "deferred wiring" notes that would otherwise
have read as still-true after this item landed; see §7 of the file-level
comment and the per-function comments on `promoteClassification`,
`promoteOrganizer`/`promoteAttendees`/`promoteAttachments`,
`promoteRelatedTo`, and `promoteSummaryDescription`
(`descriptionHtml`'s note). Judgment call: PLAN.md's own convention
throughout this campaign is that these header comments are a living
narrative of what's wired where — leaving them saying "IP.10's job" after
IP.10 landed would mislead the next reader exactly the way stale
`STATUS.md` prose is explicitly prohibited from doing.

---

## 5. `descriptionHtml` — wired

Same X-ALT-DESC convention VEVENT/VTODO already use, added inline
(`journalFieldsToCanon()`/`canonObjectToJournalBytes()`, three lines
each) rather than extracted into `incidencecommonfields.cpp` — matching
IP.6's own deliberate decision to leave this one un-extracted (three
near-identical one-liners were judged not worth a shared function; see
`incidencecommonfields.h`'s own note, now updated to describe VJOURNAL's
copy too).

**Decision, and why**: RFC 5545 gives VJOURNAL a real `DESCRIPTION`
property (§3.6.3's jourprop explicitly includes it), and X-ALT-DESC is a
KOrganizer-family convention that has never been component-kind-specific
in this codebase or (as far as this item could determine) in any real
client's usage — it means "the HTML form of this component's
DESCRIPTION," and VJOURNAL has a DESCRIPTION. There was no RFC or
object-model reason to leave it out; PLAN.md explicitly allowed either
outcome as long as justified, and this is the justification for wiring
it. Added the `X-ALT-DESC` skip key to VJOURNAL's `x-ical` passthrough
(mirroring `eventcanonfields.cpp`'s skip list exactly) so it does not
double-ride both the named `descriptionHtml` key and the generic
passthrough.

Exercised by `kMaximalVjournal`'s
`X-ALT-DESC;FMTTYPE=text/html:<p>Saw the sea</p>` line, asserted present
in `calendarCatalogueDeclaresVjournalKeys()`'s `kMustBePresent` list.

---

## 6. Loss profile (`canonToVjournalLoss()`)

**Removed** (six entries, all now genuinely fixed): `attachments`,
`attendees`, `organizer`, `relatedTo`, `recurrence`, `recurrenceId`.

**Added**: `recurrenceRange: Degraded` — the bare RECURRENCE-ID identity
survives losslessly; only the `RANGE=THISANDFUTURE` modifier is never
re-emitted (the W3 safety rule this item's own demote code implements).
Declaring this surfaced **O96**: the sibling `canonToVtodoIcalLoss()`
(`icalcanonstages.cpp`) demotes through the identical W3-shaped code
(shared `vtodocanonfields.cpp` emitter) but does not declare the same
degradation, while the *other* VTODO edge's own profile
(`vtodocanonstages.cpp`'s `canonToVtodoLoss()`) does. A pre-existing,
low-severity declaration gap (read-side-only fact, no data loss, just an
incomplete warning) — logged, not fixed here, since IP.10 was not
otherwise touching `icalcanonstages.cpp` and fixing it in passing is
exactly PLAN.md §1's prohibited move.

**Kept**: `requestStatus: Dropped` — permanent, upstream, unchanged (no
KCalendarCore accessor exists at all, per O91).

**Kept absent, correctly**: `resources` (RFC 5545 §3.6.3's jourprop
grammar does not permit RESOURCES on VJOURNAL at all — RFC-correct
absence, not a drop). `classification`/`descriptionHtml` also correctly
absent — both are guarded/Reversible now, not lossy, matching VTODO's own
precedent of not declaring them in its calendar-domain profile either
(`vtodoDemoteLossProfileIsVtodoShapedNotEventShaped`'s `kEventOnly` list
excludes both for the same reason).

---

## 7. Catalogue

`journalCanonContributedIds()` gained: `recurrence`, `recurrenceId`,
`recurrenceRange`, `organizer`, `attendees`, `relatedTo`,
`descriptionHtml`, `attachments`. All eight already had `PropertyKind`/
display-name entries in `calendarPropertyMetadata()`
(`calendarcanonproperties.cpp`) — VEVENT/VTODO already declare every one
of them — so, exactly as PLAN.md predicted, this was pure contributor-list
membership, zero new metadata. Verified by building and running
`calendarCatalogueDeclaresVjournalKeys()` (IP.1's gate) with a genuinely
maximal fixture (extended from the old summary/description/status-only
one to exercise every newly-wired key, plus a detached-exception fixture
unioned in for `recurrenceId`/`recurrenceRange` — mirroring
`calendarCatalogueDeclaresVeventKeys()`'s established master+exception
pattern) — green on the first real run, no catalogue orphans.

---

## 8. IP.8's gate

`vjournalRfc5545RoundTrip()`'s dedicated `QEXPECT_FAIL` block removed
entirely. The "real gate" `QCOMPARE(sortedList(lost), expectation.expectedLost)`
now passes for real, with `vjournal`'s `expectedLost` computed by a new
`vjournalExpectedLostList()` helper: `droppedRfcNames(canonToVjournalLoss())`
(today: just `REQUEST-STATUS`) plus one hand-added `RELATED-TO` entry,
documented inline with the O95 reasoning (§3 above). Fixpoint check
(`expectFixpoint: true`) unchanged and still passes — the master fixture
carries no `RECURRENCE-ID`/`RANGE`, so the identity/W3 machinery does not
interact with it.

---

## 9. VEVENT/VTODO regression check

Built and ran `tst_calendar_canon_roundtrip` (VEVENT-only suite) and
`tst_todo_canon_roundtrip` (VTODO) before committing, both fully green
(15/15 and 41/41 respectively) — unaffected, as expected, since this item
never edits `incidencecommonfields.cpp`, `eventcanonfields.cpp` or
`vtodocanonfields.cpp` (confirmed by the `git status` scope list above;
only `incidencecommonfields.h`'s doc comments were touched, no logic).
`tst_calendar_kind_dispatch`'s own VEVENT/VTODO slots
(`veventStillRoundTrips`, `vtodoSurvivesIcalCanonRoundTrip`,
`vtodoDemoteLossProfileIsVtodoShapedNotEventShaped`,
`calendarCatalogueDeclaresVeventKeys`/`VtodoKeys`) also re-run green in
the same binary as the new VJOURNAL slots.

---

## 10. Matrix + byte-pin

Regenerated via `./build/tools/matrixgen/matrixgen`. Diff is exactly the
loss-profile edit from §6: six `Dropped` rows removed from the
`canon → ical (vjournal)` section, one `recurrenceRange | Degraded` row
added — 7 lines changed total, no other section touched.
`tst_gm_pipeline_convergence` green, including
`committedMatrixMatchesGenerated` (the byte-pin) and the two crossing-gate
slots that were already passing (`eventCrossingGoogleToMsStaysDeclared`,
etc. — none of the crossing gates touch the calendar-domain VJOURNAL
edge, so no change expected or observed there).

---

## 11. Full suite results

Full `ctest --test-dir build` run: **215 tests, 211 passed, 4
known-environmental failed** (`tst_backend_signals`,
`tst_backend_thread_relocation`, `tst_backend_reentrancy_pin`,
`tst_remotecalendarbackend`) — verified by failure TEXT (KDAV
30s-transfer-timeout / 412/409 against the local Radicale instance), not
by name, matching every prior baseline this campaign has recorded.

ctest executable count: 215 → 215 (unchanged — every new slot landed
inside the two existing binaries `tst_calendar_kind_dispatch.cpp` and
`tst_incidence_rfc5545_fidelity.cpp`, per IP.3/IP.6/IP.9's own
precedent). New QTest slots: +5 in `tst_calendar_kind_dispatch.cpp`
(`vjournalMasterHasNoRecurrenceId`, `vjournalRoundTripPreservesRecurrenceId`,
`vjournalDemoteNeverEmitsThisAndFutureRange`,
`vjournalMasterAndExceptionRemainDistinctThroughRoundTrip`,
`vjournalRoundTripPreservesRecurrenceVerbatim`), plus two existing slots
meaningfully rewritten (`calendarCatalogueDeclaresVjournalKeys`,
`vjournalDemoteLossProfileIsVjournalShapedNotEventShaped`); one
`QEXPECT_FAIL` block removed from `tst_incidence_rfc5545_fidelity.cpp`.

---

## 12. Findings

**RESOLVED this item:** O87 (VJOURNAL's seven undeclared drops — six
fully fixed, RECURRENCE-ID identity corruption fixed; `RELATED-TO`
carved out honestly as O95 rather than claimed fixed).

**Filed this item:**
- **O95** — `KCalendarCore::ICalFormat`'s VJOURNAL parser never populates
  `relatedTo()` from a source `RELATED-TO` line (upstream, promote-
  direction only; the write/demote direction is correct). Full probe
  transcript and structural-gap discussion in `docs/campaign/FINDINGS.md`.
  Not owned by any item.
- **O96** — `recurrenceRange`'s W3-shaped Degraded loss is declared for
  VJOURNAL (this item) but not for the sibling VTODO calendar-domain
  profile (`canonToVtodoIcalLoss()`), despite identical underlying demote
  behavior. Low severity (no data loss, incomplete warning only). Not
  owned by any item.

**Not fixed, not owned by this item, logged only:** none beyond O95/O96 —
every item PLAN.md's IP.10 body and the task brief named was either fixed
or, for `relatedTo`, honestly declared as blocked and why.

---

## 13. Deviations from the task brief, and why

1. **`incidencecommonfields.h` was edited** (doc comments only, five
   spots) despite the brief's "you shouldn't be editing that file at all
   for this item... but verify" caution. Argued in §4: the brief itself
   frames this as a verification step ("verify" that no logic touch was
   needed), and the file's own header explicitly documents itself as a
   per-function *history* of who calls what and why — leaving five
   comments asserting VJOURNAL's wiring was still IP.10's future job,
   after IP.10 landed, would be exactly the kind of stale living-document
   drift this campaign's own conventions (STATUS.md's "never leave a row
   saying IN PROGRESS after work has landed") prohibit elsewhere. No
   function body, signature, or behavior changed — verified by the diff
   being comment-only.
2. **`recurrenceRange: Degraded` added to the loss profile** — not
   explicitly named in PLAN.md's IP.10 acceptance list, but a direct,
   necessary consequence of implementing the W3 safety rule (§1): once
   demote unconditionally drops the RANGE modifier, honesty requires
   declaring that loss, per the item's own "Losses declared through IP.9's
   mechanism, kept honest" acceptance criterion.
3. **One commit, matching PLAN.md's own note** that (unlike IP.6) no
   structural/behavioral split is mandated here; the identity fix and the
   "free" wiring landed together since separating them would not aid
   review (the identity fix's demote-side code sits inside the same
   function as the free wiring, interleaved with it in the same order
   `eventcanonfields.cpp`/`vtodocanonfields.cpp` use).

---

## 14. Acceptance criteria checklist

- [x] IP.8's `(calendar, vjournal)` gate goes green, `QEXPECT_FAIL`
      removed for everything actually fixed (all of O87 except
      `RELATED-TO`, honestly re-declared instead).
- [x] A slot proving a detached VJOURNAL instance and its master remain
      distinct through a full promote→demote→promote —
      `vjournalMasterAndExceptionRemainDistinctThroughRoundTrip`.
- [x] Recurrence round-trip slot (RRULE/RDATE/EXDATE verbatim) —
      `vjournalRoundTripPreservesRecurrenceVerbatim`.
- [x] Losses declared through IP.9's mechanism, kept honest —
      `canonToVjournalLoss()` updated; `recurrenceRange`/`requestStatus`
      the only two remaining entries, both honestly justified.
- [x] VEVENT and VTODO slots unchanged and green — verified before AND
      after (§9); `incidencecommonfields.cpp` function bodies untouched.
- [x] Matrix regenerated; byte-pin test green (§10).
- [x] Full suite green except the 4 known-environmental slots, verified
      by failure TEXT (§11).

---

## 15. Files changed

`CLAUDE.md`, `docs/campaign/FINDINGS.md`,
`docs/campaign/eee/CONVERGENCE-MATRIX.md`,
`docs/campaign/incidence-parity/STATUS.md`,
`docs/campaign/incidence-parity/2026-09-02-ip10-return-receipt.md` (this
file), `src/calendar/incidencecommonfields.h` (doc comments only),
`src/calendar/journalcanonfields.cpp`,
`tests/calendar/tst_calendar_kind_dispatch.cpp`,
`tests/calendar/tst_incidence_rfc5545_fidelity.cpp`.
