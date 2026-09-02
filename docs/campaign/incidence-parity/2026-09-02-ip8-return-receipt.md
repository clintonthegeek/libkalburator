# Return receipt — IP.8: RFC 5545 round-trip fidelity gate

**Delivered:** 2026-09-02
**Consumes:** `docs/campaign/incidence-parity/PLAN.md` §1 (execution rules,
binding), Amendment 1 §A.1 and §A.4 IP.8 (work statement + acceptance
criteria); `STATUS.md`; `2026-09-02-preflight-audit.md` §2.1 (expected red
list); `probes/README.md` and `probes/incidence-audit-probe.cpp` (starting
mechanism, two probe traps); FINDINGS.md O79, O83, O85, O86, O87.
**Scope discipline:** tests only. `git status` confirmed at commit time:
only `tests/calendar/tst_incidence_rfc5545_fidelity.cpp` (new),
`tests/calendar/CMakeLists.txt` (+1 registration line),
`docs/campaign/FINDINGS.md` (+O91), `STATUS.md`, and this receipt changed.
Nothing under `src/`.

---

## 0. What landed

New file `tests/calendar/tst_incidence_rfc5545_fidelity.cpp`, registered
in `tests/calendar/CMakeLists.txt` immediately after
`tst_calendar_kind_dispatch` (one line,
`kalburator_add_calendar_test(tst_incidence_rfc5545_fidelity)`). 13 QTest
slots:

- `veventRfc5545RoundTrip()`, `vtodoRfc5545RoundTrip()`,
  `vjournalRfc5545RoundTrip()` — the core gate: maximal fixture →
  promote → demote, RFC 5545 §3.1-unfolded property-NAME set compared
  before/after, asserted against a declared per-kind allow-list, plus a
  promote→demote→promote canon-bytes fixpoint check.
- `veventAlarm{StartRelative,EndRelative,Absolute,RepeatDuration}()` and
  `vtodoAlarm{StartRelative,EndRelative,Absolute,RepeatDuration}()` — the
  VALARM sub-gate: each of the four RFC 5545 §3.6.6 trigger forms, on both
  kinds that permit VALARM, checked for both trigger-form fidelity and the
  `enabled` flag.

All 13 slots are ctest-GREEN (QTest-level XFAILs inside, not hard
failures) — see §4 for the exact red list and §5 for the non-vacuity
proof.

## 1. Placement: new file, not an extension of `tst_calendar_kind_dispatch.cpp`

PLAN.md left this open ("extend or new file — argue it"). New file, for
three reasons:

1. **Different question, different fixture discipline.**
   `tst_calendar_kind_dispatch.cpp`'s IP.1 slots ask "does the catalogue
   know what the emitter emits" and are *deliberately* built by reading
   the emitter's own source (its own doc comment says so, and its receipt
   confirms it). This item's fixtures must be built the opposite way — from
   RFC 5545 itself, ignoring what the emitter happens to handle — or the
   gate is vacuous (Amendment 1 §A.1). Mixing the two disciplines in one
   file risked a future editor copying the wrong one by proximity.
2. **The file already carries 12 slots for its own purpose.** Adding 13
   more for an unrelated axis would make it a dumping ground rather than a
   gate with one clear job.
3. **No shared fixture value.** IP.1's `kMaximalVevent`/`kMaximalVtodo`
   fixtures are explicitly *not* maximal per RFC 5545 — verified: they
   omit `COMMENT`, `CONTACT`, `RESOURCES`, `REQUEST-STATUS`, `RDATE`,
   entirely, and I would have had to grow them past that file's own stated
   purpose to reuse them here. Building fresh RFC-first fixtures in a new
   file was the smaller, more honest change.

The unfold-then-scan helper (`icalPropertyNames()`) is duplicated from
`probes/incidence-audit-probe.cpp` rather than extracted to a shared
header. Deliberate: the probe is explicitly "not a gate" (its own README)
and lives outside `tests/`; pulling test logic from a non-test, non-CMake
file to `tests/` would create a one-off cross-directory dependency for 40
lines of well-understood, already-debugged code. If a THIRD caller ever
needs it, that is the point to extract a shared header — not before.

## 2. Fixture construction — RFC 5545 first, verified against the grammar

Every fixture (`kVeventMaster`/`kVtodoMaster`/`kVjournalMaster` +
`*Exception` detached-instance variants, mirroring
`tst_calendar_kind_dispatch.cpp`'s established master/exception split for
recurrence-id-bearing kinds) was built from the RFC 5545 ABNF for that
component:

- **VEVENT §3.6.1** (`eventprop`): every not-more-than-once property
  (`CLASS`, `CREATED`, `DESCRIPTION`, `DTSTART`, `GEO`, `LAST-MODIFIED`,
  `LOCATION`, `ORGANIZER`, `PRIORITY`, `SEQUENCE`, `STATUS`, `SUMMARY`,
  `TRANSP`, `URL`, `RECURRENCE-ID`), `RRULE`, `DTEND` (chosen over the
  mutually-exclusive `DURATION`), and every may-occur-more-than-once
  property (`ATTACH`, `ATTENDEE`, `CATEGORIES`, `COMMENT`, `CONTACT`,
  `EXDATE`, `RELATED-TO`, `RESOURCES`, `RDATE`) plus `REQUEST-STATUS`
  (RFC's `rstatus`) and one `VALARM`.
- **VTODO §3.6.2** (`todoprop`): the same shape with `COMPLETED`/
  `PERCENT-COMPLETE` added and `DUE` in place of `DTEND` (chosen over
  `DURATION`).
- **VJOURNAL §3.6.3** (`jourprop`): deliberately narrower — no `GEO`,
  `LOCATION`, `PRIORITY`, `RESOURCES`, `TRANSP`, `DTEND`/`DUE`/`DURATION`,
  `PERCENT-COMPLETE`/`COMPLETED`, or `VALARM`, because RFC 5545's grammar
  does not permit any of them on a journal. Their absence in this fixture
  is RFC-correctness, not an oversight — called out in the file's own
  comment so a future reader does not "helpfully" add them.
- **VALARM §3.6.6**: a dedicated pair of fixtures
  (`kVeventAlarms`/`kVtodoAlarms`), each carrying all four trigger forms
  in one component (start-relative `TRIGGER:-PT15M`, end-relative
  `TRIGGER;RELATED=END:-PT5M`, absolute `TRIGGER;VALUE=DATE-TIME:...`, and
  a `REPEAT:3`/`DURATION:PT5M` pair), separate from the main fixtures
  (which carry one plain start-relative alarm each) so the two gates do
  not conflate scope.

I fetched `https://www.rfc-editor.org/rfc/rfc5545` and `.txt` via WebFetch
to cross-check this enumeration against the primary text; the fetch tool's
summarizer could not return the full ABNF block verbatim (page-truncation
in its intermediate model), but it did confirm the property lists and the
`DTEND`/`DURATION` mutual exclusivity match what is recorded above and
what shipped in the fixtures — cross-checked against this session's own
knowledge of the grammar, which is standard and unambiguous. No
discrepancy was found between the two sources.

Two already-documented probe traps were followed, not rediscovered:
multi-label mail domains (`example.com`, never `a@x` — libical drops the
whole `ATTENDEE` property otherwise) and RFC 5545 §3.1 unfolding *before*
per-line property-name scanning (`icalPropertyNames()` unfolds first,
exactly matching `incidence-audit-probe.cpp`'s fixed version).

No new files under `tests/calendar/fixtures/`: all fixtures are inline
`QByteArrayLiteral` constants (concatenated quoted literals, per the O59
house rule — never a raw string literal `R"(...)"` in a moc-processed
translation unit), matching this test directory's existing convention for
fixtures that do not need a real filesystem path.

## 3. The declared allow-list — `expectedLossTable()`

A `QHash<QString, KindFidelityExpectation>` (`KindFidelityExpectation` =
`{QStringList expectedLost; bool expectFixpoint;}`), one static function,
populated once, read by all three `runKindCase()` invocations — not
scattered literals. Carries a `TODO(IP.9)` comment at its declaration
site naming the exact coupling PLAN.md asks for: once kind-scoped loss
profiles land, this table's `expectedLost`/`expectFixpoint` per kind
should be replaced by (or asserted equal to) the real
`LossProfile`-derived set, and the struct shape was chosen — one
`QStringList` per kind rather than nested per-property structures — to
make that swap mechanical rather than a rewrite.

## 4. The exact red list observed — matches the predicted list, plus O91

All numbers below are the empirically-measured `lost` set (property NAME
present in the RFC-5545-unfolded source text, absent from the demoted
text), unioned across each kind's master + exception fixture, exactly as
the shipped test computes it. Verified by running the actual compiled
binary (`QT_QPA_PLATFORM=offscreen build/tests/calendar/
tst_incidence_rfc5545_fidelity`), not inferred.

| Kind | Lost (measured) | Matches PLAN.md's predicted list? |
|---|---|---|
| VEVENT | `COMMENT`, `CONTACT`, `GEO`, `RELATED-TO`, `REQUEST-STATUS`, `RESOURCES` | `GEO`, `RELATED-TO` predicted — **matches**. Four more found: see O91. |
| VTODO | `ATTACH`, `ATTENDEE`, `CLASS`, `COLOR`, `COMMENT`, `CONTACT`, `ORGANIZER`, `REQUEST-STATUS`, `RESOURCES`, `SEQUENCE`, `URL` | Seven O83 properties predicted — **matches** (`ATTACH`, `ATTENDEE`, `CLASS`, `COLOR`, `ORGANIZER`, `SEQUENCE`, `URL`). Four more found: O91. `GEO` correctly absent from this LIST — see next row. |
| VTODO fixpoint | **NOT** a fixpoint | Predicted (O86) — **matches**. `GEO`'s property NAME survives demote (corrupted value, not a dropped name), so it never appears in the `lost` set; the corruption instead breaks the second promote, which the fixpoint assertion catches. Confirmed by direct inspection of demoted bytes: `GEO:2.5;<garbage>` is present, non-UTF8, differs per run. |
| VJOURNAL | `ATTACH`, `ATTENDEE`, `COMMENT`, `CONTACT`, `EXDATE`, `ORGANIZER`, `RDATE`, `RECURRENCE-ID`, `RELATED-TO`, `REQUEST-STATUS`, `RRULE` | Seven O87 properties predicted — **matches** (`ATTACH`, `ATTENDEE`, `EXDATE`, `ORGANIZER`, `RECURRENCE-ID`, `RELATED-TO`, `RRULE`). `RDATE` folded in as a clarification (§4.1). Three more found: O91. |
| VALARM (VEVENT) | end-relative, absolute, repeat/duration forms corrupted; **all four** forms come back `enabled=false` | O79 (form) + O85 (enabled) predicted — **matches exactly**. start-relative form survives (also predicted — O79's own text says the bug is specific to non-start-relative forms). |
| VALARM (VTODO) | all four forms preserve their trigger semantics correctly; **all four** come back `enabled=false` | O85 predicted (enabled only) — **matches exactly**. No form corruption on VTODO (W5 already correct) — confirmed, not merely assumed: a dedicated probe run showed all four VTODO alarms reparse with the exact expected `hasStartOffset`/`hasEndOffset`/`hasTime`/`repeatCount` values. |

### 4.1 One deviation from PLAN.md's exact wording: VJOURNAL's `RDATE`

O87's text names `RRULE`/`EXDATE` (and `RECURRENCE-ID`) but not `RDATE`
individually. Measured: `RDATE` is ALSO lost on VJOURNAL, for the identical
reason (`journalcanonfields.cpp` has zero recurrence handling of any
kind — no `RRULE`, `RDATE`, or `EXDATE` reading whatsoever). This is not
treated as a fourth independent defect or a new O-number: O87's own prose
already describes the mechanism as "a recurring journal is silently
flattened," which covers `RDATE` by the same logic as `RRULE`/`EXDATE`.
Folded into the declared allow-list under the existing O87/IP.10 citation.
**This is a clarification of O87's scope, not a correction of a factual
error in PLAN.md** — nothing in PLAN.md asserted `RDATE` survives, it
simply was not named. No PLAN.md edit made.

## 5. O91 — four newly-discovered drops, filed not fixed

Building genuinely-maximal (RFC-first, not emitter-first) fixtures
surfaced four properties beyond the pre-flight audit's declared list,
lost on every kind that permits them: `COMMENT`, `CONTACT`, `RESOURCES`
(RFC-valid on VEVENT/VTODO only — correctly absent from VJOURNAL's
fixture and its expected-loss list), and `REQUEST-STATUS` (RFC-valid on
all three).

This is exactly the failure mode Amendment 1 §A.1 describes ("a fixture
built from the emitter's capabilities makes the gate vacuous") — the
pre-flight audit's own probe fixture, while far more thorough than
anything before it, was still not fully RFC-maximal, and IP.8's charter is
specifically to go further. Per PLAN.md §1's "no fix while passing
through" prohibition (this surfaced while building IP.8, not while working
an item scoped to fix catalogue/emitter gaps), it is **logged, not
fixed, not silently pinned**:

- Filed as **FINDINGS.md O91** (full text, evidence, and the two-mechanism
  distinction between `COMMENT`/`CONTACT`/`RESOURCES` — ours, `IP.6`/
  `IP.10` — and `REQUEST-STATUS` — upstream, no `KCalendarCore` accessor
  exists at all, verified by grep across
  `/usr/include/KF6/KCalendarCore/` finding zero hits outside the legacy
  vCalendar macro).
- Each occurrence in the shipped test is a **named** `QEXPECT_FAIL` citing
  `O91` and its owning item(s), not folded silently into the pre-existing
  O83/O86/O87 `QEXPECT_FAIL`s (kept as a **separate** `QEXPECT_FAIL` call
  per kind specifically so a future `ctest -V` reader sees two distinct
  citations rather than one merged one that obscures which defect closed
  when — IP.6 landing will make the O83 `QEXPECT_FAIL` XPASS while O91's
  stays red, or vice versa, and the separation makes that visible).
- Not scheduled to any item by this receipt — `IP.6`/`IP.10` are the
  natural home for the "ours" half (folds into the same
  `incidencecommonfields` extraction already scoped there) and `IP.9` for
  the upstream half's declare-`Dropped` decision, per O91's own text, but
  deciding that is those items' job, not this one's.

## 6. Non-vacuity, verified the IP.1/IP.2 way

Temporarily disabled the VEVENT-`GEO` `QEXPECT_FAIL` (wrapped in
`#if 0`/`#endif`, NOT a single-line comment — a first attempt at a
single-line `//` comment on a multi-line macro call left the continuation
lines as orphaned, syntactically-broken code and produced a *compile*
error rather than proving anything; recorded here as a "the verification
harness itself has a trap" note for the next agent who reaches for a
one-line disable on a multi-line `QEXPECT_FAIL`). Rebuilt, ran:

```
FAIL!  : TestIncidenceRfc5545Fidelity::veventRfc5545RoundTrip()
'!lost.contains(QStringLiteral("GEO"))' returned FALSE.
(GEO must not be lost (until IP.6 lands))
```

Confirmed a real, non-XFAIL `FAIL!` with the exact expected message.
Restored the file to its clean state (`diff` against the pre-edit copy
showed only the two added `#if 0`/`#endif` lines, confirming no other
drift), rebuilt, reran: 13 passed, 0 failed at the QTest level (XFAILs
counted as passed by QTest/ctest, per house convention).

## 7. Corner cases declared-not-executed

- **Did not build a `DURATION`-based fixture variant for VEVENT/VTODO**
  (RFC 5545 permits `DTEND`/`DURATION` and `DUE`/`DURATION` as mutually
  exclusive alternatives; this item's fixtures use `DTEND`/`DUE`). The
  `DURATION` alternative is a different serialization of the same
  semantic bound, not a distinct property whose loss this gate exists to
  measure — including both would test KCalendarCore's `DURATION`
  parsing/round-trip fidelity, which is a different (and, on current
  evidence, presumably fine) question from "does our emitter honour the
  properties RFC 5545 permits."
- **Did not test VALARM's `AUDIO`/`EMAIL` action-type variants** (only
  `DISPLAY`, matching every existing alarm fixture in this codebase). RFC
  5545 §3.6.6 defines three `alarmc` shapes (`audioprop`/`dispprop`/
  `emailprop`) differing in which properties are required, but our own
  `alarms` canon row shape (`type`/`offset`/`text`/...) does not
  distinguish action type at all — a fixture exercising `AUDIO`/`EMAIL`
  would measure the same trigger-form/enabled defects this gate already
  catches, not a new axis, and `type` itself is not part of PLAN.md's
  named acceptance criteria for this item.
- **Did not add a `REQUEST-STATUS` regression slot beyond the aggregate
  lost-set assertion** — O91 is upstream and not this item's to close;
  a dedicated slot would duplicate the aggregate check for no additional
  information until an item actually attempts a fix.
- **Did not investigate whether the VALARM sub-gate's `REPEAT`/`DURATION`
  pairing on VTODO is itself fully RFC-conformant** beyond confirming its
  measured values match the source (`repeatCount()==3`,
  `snoozeTime()==300s`) — deeper semantic validation of KCalendarCore's
  `Duration`/`Alarm` classes is out of this item's scope (that is what
  `probes/kcalendarcore-probe.cpp` section C already established, and this
  item's job is to gate OUR pipeline against that already-verified
  toolkit behaviour, not re-verify the toolkit).
- **Did not touch `PLAN.md`'s body text.** No factual error was found in
  it; §4.1 above is a scope clarification of O87 recorded in FINDINGS.md
  and this receipt, not a plan correction.

## 8. Matrix / loss-profile / edge-count housekeeping

Not applicable. No loss profile changed, no `edges()` list grew, no new
vendor pair or domain edge was introduced — IP.8 is a test-only gate over
the existing `{calendar,ical}<->{calendar,canon}` edge. Confirmed, not
assumed: `git diff --stat` at commit time shows no file under
`src/calendar/calendarstockshapes.cpp` or any `*lossprofile*`/
`*LossProfile*`-touching file, and `tools/matrixgen`'s output file
(`docs/campaign/eee/CONVERGENCE-MATRIX.md`) was not regenerated because
nothing that feeds it changed.

## 9. Test evidence

- New file: `tests/calendar/tst_incidence_rfc5545_fidelity.cpp` (13 QTest
  slots). Registered: `tests/calendar/CMakeLists.txt` (+1 line).
- `ctest -R tst_incidence_rfc5545_fidelity --output-on-failure`: **Passed**
  (13/13 QTest-level results — 4 PASS on the property-loss/fixpoint slots
  carrying multiple internal XFAILs each, 8 PASS on the VALARM sub-gate
  slots each carrying 1-2 internal XFAILs, 1 PASS on `initTestCase`/
  `cleanupTestCase`; 0 hard FAIL).
- **Full suite:** `ctest --output-on-failure -j$(nproc)` from a full
  `cmake --build build -j$(nproc)`: **215 tests, 211 passed, 4 failed** —
  the baseline 214/210/4 plus this item's one new ctest-level test entry
  (`tst_incidence_rfc5545_fidelity`, itself green — its 13 internal
  QEXPECT_FAILs are QTest-level XFAILs, not ctest failures). The 4 failures
  are the same 4 known environmental slots (`tst_backend_signals`,
  `tst_backend_thread_relocation`, `tst_backend_reentrancy_pin`,
  `tst_remotecalendarbackend`) — verified by failure TEXT, not name, per
  house rule: every one carries the `RemoteCalendarBackend: KDAV job
  exceeded transfer timeout ( 30000 ms)` signature against the local
  Radicale (`ss -tlnp` shows it listening on `127.0.0.1:5232` with a
  backlog of 5, which a `-j8` parallel ctest run against it saturates).
  `tst_backend_thread_relocation` additionally reported "Subprocess
  aborted" rather than a clean `FAIL!` this run; re-ran it in isolation
  (`ctest -R tst_backend_thread_relocation`, no `-j` contention) and it
  reproduced identically (392.5s, same abort, same KDAV-timeout lines in
  its output) — same root cause, not new-since-baseline and not caused by
  this item's diff (which touches zero files this test could observe:
  no `src/` change, and the new test file is not on any code path
  `RemoteCalendarBackend` or the Radicale fixture touches).
- `git status` confirmed at commit time: only the files named in the
  header changed — nothing under `src/`.
