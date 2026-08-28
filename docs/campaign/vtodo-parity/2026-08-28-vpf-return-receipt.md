# Return receipt — VP.f: W5 (VALARM shape) + W6.2 (date coercion) + W7/O74 (passthrough + differ fix)

**Delivered:** 2026-08-28
**Consumes:** handoff §W5/§W6/§W7 (`docs/2026-08-25-vtodo-parity-handoff-response.md`);
FINDINGS.md O74; recon handoff `2026-08-28-vpf-recon-handoff.md` (code map +
9 open decisions across all three sub-items, implemented per that doc's
design sketches except where the doc itself called for an empirical probe —
see §0 below).
**Binding contract:**
`docs/campaign/vtodo-parity/2026-08-28-w7-passthrough-contract.md` (truth
table + org warning sentence + O74 fix note — read that doc for the
normative table; this receipt covers what was built, how, and the test
evidence for all three sub-items).

This is the **last item on the vtodo-parity campaign's currently-scoped
W-item list** — VP.a through VP.f are now all DONE (see STATUS.md).

---

## 0. The two KCalendarCore probes — what they actually found

Per the recon doc's explicit instruction, three genuine unknowns were
resolved by throwaway probes (standalone `g++`-compiled programs linking
`KF6CalendarCore`/`Qt6Core`/`Qt6Gui` directly, run with
`QT_FORCE_STDERR_LOGGING=1` since this environment routes `qDebug()` to the
journal by default) **before** any implementation code was written, per the
task's instruction not to assume the recon's more-likely branch without
checking.

### Probe 1 — W6.2 Open Decision 5 (DATE/DATE-TIME mismatch survival)

Recon's two branches: (1) KCalendarCore preserves the mismatch
independently per property, implementable against the parsed `Todo::Ptr`;
or (2) KCalendarCore collapses it via the single `allDay()` flag, requiring
a raw-bytes text-scan helper. **Result: branch (1).** Parsing
`DTSTART;VALUE=DATE:20260601` + `DUE:20260601T170000Z` produced
`dtStart()` as a floating `Qt::LocalTime` midnight (`2026-06-01T00:00:00`,
no tz) and `dtDue()` as a real `Qt::TimeZone` UTC datetime
(`2026-06-01T17:00:00`) — two independently-inspectable QDateTimes, exactly
matching the `dateTimeToJson`-heuristic (`time()==00:00 &&
timeSpec()==LocalTime`) already used elsewhere in this file. Reversing the
fixture (DATE-TIME DTSTART + DATE DUE) confirmed the same independence in
the other direction. **Consequence:** no raw-bytes helper was needed —
rules (a)/(b) are implemented entirely against `todo->dtStart()`/`dtDue()`.
Also incidentally observed: the incidence-level `allDay()` flag tracks only
DUE's date-only-ness when both are present (false when DTSTART is
date-only + DUE isn't; true when DUE is date-only + DTSTART isn't) —
confirming it is NOT a fused/collapsed view and must not be used for
per-property detection.

### Probe 2 — W6.2 Open Decision 6 / rule (c) (DURATION without DTSTART)

Parsed a VTODO with `DURATION:PT1H` and no DTSTART/DUE. **Result:
confirmed the recon's "likely zero-code no-op" guess.** `dtStart()` and
`dtDue()` both come back invalid; `hasDueDate()` is `false`. KCalendarCore's
parser resolves `DURATION` into `dtDue()` at parse time (an alternative
RFC5545 spelling of DUE, requiring DTSTART to add the duration to) and
simply produces nothing when there is no DTSTART. **Consequence:** rule (c)
required zero promote-side code — only a pinning test,
`vtodoPromoteDropsDurationWithoutDtstart`.

### Probe 3 — W6.2 bonus fix (DATE-value round-trip, `setAllDay` ordering)

Recon flagged two sub-unknowns: does `setAllDay(true)` need to be called
before or after `setDtStart`/`setDtDue`, and does KCalendarCore's writer
decide `VALUE=DATE` from the incidence's `allDay()` flag or from the
`QDateTime`'s own shape? **Both resolved, and the second answer is more
load-bearing than the recon anticipated:**

- Call order does **not** matter — `setAllDay(true)` before or after
  `setDtStart`/`setDtDue` produced byte-identical output in both orders
  tested.
- **The writer decides purely from the `allDay()` flag, NOT from the
  QDateTime's own time-of-day/timeSpec shape.** A `Qt::UTC`-midnight
  `QDateTime` (the pre-existing `jsonToDateTime` reconstruction — see
  below) with `allDay()` left `false` serializes as a full
  `DTSTART:20260601T000000Z` DATE-TIME. Even a `Qt::LocalTime`-midnight
  `QDateTime` (matching the *shape* `dateTimeToJson`'s own heuristic
  detects) with `allDay()` left `false` **still** serializes as a
  DATE-TIME (`DTSTART:20260601T000000`, no `VALUE=DATE`). Only an explicit
  `setAllDay(true)` call produces `DTSTART;VALUE=DATE:20260601` — and it
  does so correctly even with the pre-existing UTC-midnight reconstruction,
  no other change needed.
- **Consequence — a simpler fix than the recon anticipated:** the recon's
  design sketch left open whether `jsonToDateTime`'s reconstruction shape
  needed to change too. It does not. The bonus fix is exactly one
  addition: call `todo->setAllDay(true)` when either the `start` or `due`
  canon object carries a `"date"` key. `jsonToDateTime` itself is
  unchanged.

### Probe 4 — W5 Open Decision 3 (REPEAT/DURATION pairing) — recon's assumption was WRONG

Recon recommended: "on promote only emit the pair when both
`repeatCount() > 0` AND `snoozeTime().asSeconds() != 0`." **Probed and
found this check does not work as stated.** Parsing a VALARM with
`REPEAT:3` and **no** `DURATION` property at all produced `repeatCount()
== 3` and `snoozeTime().asSeconds() == 5` — a **nonzero class default**,
not zero. (Confirmed deterministic: re-run with `REPEAT:5` alone still
produced `snoozeTime() == 5`; a VALARM with neither REPEAT nor DURATION
also defaults to `snoozeTime() == 5`.) There is no public
`KCalendarCore::Alarm` API to distinguish "DURATION was explicitly 5
seconds" from "DURATION was never present, defaulted to the class's 5s."
**Resolution (a deliberate, documented deviation from the recon's exact
wording):** promote emits the REPEAT/DURATION pair whenever
`repeatCount() > 0`, trusting whatever `snoozeTime()` KCalendarCore parsed
— the same "trust the parsed accessor, no raw-bytes cross-check" posture
this file already takes for the pre-existing offset field. Building a
raw-bytes VALARM scanner to detect literal DURATION presence was judged
out of this item's scope (no second caller, YAGNI, matching the campaign's
standing policy against premature raw-bytes helpers — see W6.2's own
Open Decision 5 resolution above for the same policy applied the other
way). On **demote**, the recon's pairing rule stands unmodified and is the
one that actually matters for correctness: only synthesize
`setRepeatCount`/`setSnoozeTime` when **both** `repeatCount` and
`repeatIntervalSecs` canon keys are present — pinned by
`vtodoAlarmUnpairedRepeatIsNotSynthesized`.

Also probed as part of the same session (Open Decision 2's mutual-
exclusivity assumption): `hasStartOffset()`/`hasEndOffset()`/`hasTime()`
are confirmed mutually exclusive at the parsed-object level (a VALARM with
`TRIGGER;RELATED=END:-PT15M` has `hasEndOffset()==true`,
`hasStartOffset()==false`, `hasTime()==false`; a separate VALARM with
`TRIGGER;VALUE=DATE-TIME:...` has `hasTime()==true`, both offset flags
false) — confirmed as given, no surprises.

---

## 1. W6.2 — malformed DTSTART/DUE coercion

**File:** `src/todo/vtodocanonfields.cpp`.

- **Rule (a)** (promote, `todoFieldsToCanon`'s start/due block): when both
  DTSTART and DUE are present and their date-only-ness (via the
  `dateTimeToJson`-style heuristic) disagrees, START is coerced to match
  DUE's type — promoted up to a DATE-TIME (midnight, in DUE's zone,
  falling back to UTC) when DUE is DATE-TIME, or truncated down to
  DATE-only (same calendar date) when DUE is DATE-only. **DUE's type
  always wins, unconditionally** — this is the binding response-doc
  wording taken literally.
- **Rule (a) — deliberate divergence from tasks.org's actual behavior
  (Open Decision 4):** tasks.org's real rule (per the audit doc, §9 line
  144) is **symmetric** — whichever side is DATE-TIME wins, regardless of
  which property that is. The binding response-doc text instead says
  unconditionally "coerce to DUE's type," which in the
  DATE-TIME-DTSTART/DATE-only-DUE direction produces the **opposite**
  result from tasks.org (tasks.org would promote DUE up to DATE-TIME;
  this implementation truncates DTSTART down to DATE-only instead). Per
  the recon's explicit recommendation, the binding text was followed
  exactly rather than tasks.org's literal behavior. Both directions are
  tested: `vtodoCoercesDateOnlyStartToDueDateTimeType` (DUE wins, agrees
  with tasks.org) and `vtodoCoercesDateTimeStartToDueDateOnlyType` (DUE
  wins, **disagrees** with tasks.org — the divergence case, with an
  explicit comment at the test site pointing here).
- **Rule (b)** (same block, evaluated after rule (a)'s reconciliation):
  `due <= start` ⇒ `start` key omitted from canon entirely. Tested:
  `vtodoDropsStartWhenDueNotAfterDtstart`.
- **Rule (c):** zero implementation — see Probe 2 above. Tested:
  `vtodoPromoteDropsDurationWithoutDtstart`.
- **Bonus fix** (demote, `canonObjectToVtodoBytes`'s start/due block): a
  new `anyDateOnly` flag is set when either the `start` or `due` canon
  object carries a `"date"` key; if set, `todo->setAllDay(true)` is
  called once after both `setDtStart`/`setDtDue`. Safe as a single
  incidence-level flag because rule (a)'s promote-side reconciliation
  already guarantees start/due agree on date-only-ness by the time they
  reach canon. Tested (both promote AND demote, byte-level):
  `vtodoAllDayRoundTripPreservesDateValueForm` — asserts the demoted bytes
  contain `DTSTART;VALUE=DATE:20260601` / `DUE;VALUE=DATE:20260602`, not a
  `T000000Z` DATE-TIME regression.
- **The tasks.org "§9 list" fixture reference:** confirmed (again, per the
  recon) to not exist as a literal fixture artifact anywhere in this repo
  or PlanStan — it is prose describing tasks.org's Kotlin source. All five
  test fixtures were synthesized directly from the rules, following the
  existing `kTestVTodo`-style pattern.

## 2. W5 — VALARM shape extension

**Files:** `src/todo/vtodocanonfields.cpp` (vtodo/CalDAV leg),
`src/todo/mstodotaskcanonstages.cpp` (MS To-Do leg, shape-unification fold-in).

### Vtodo/CalDAV leg

New additive JSON keys on the existing `alarms[]` row shape (old rows stay
valid): `"at"` (absolute UTC-ISO trigger, `TRIGGER;VALUE=DATE-TIME`),
`"related"` (`"end"`, optional, absent implies `"start"` — back-compat
preserved exactly), `"repeatCount"` + `"repeatIntervalSecs"` (always
paired). **The field is literally named `"offset"`, not `"offsetSecs"`** —
confirmed unchanged, per the recon's correction to the response doc's own
citation.

- **Promote:** branches on `alarm->hasTime()` → emit `"at"`; else
  `alarm->hasEndOffset()` → emit `"offset"` + `"related":"end"`; else
  (default/`hasStartOffset()`) → emit `"offset"` only (unchanged shape).
  REPEAT/DURATION pair emitted per Probe 4's resolution above.
- **Demote:** reads `"at"` first (→ `alarm->setTime()`); else `"offset"` +
  `"related"` (`"end"` → `setEndOffset`, else `setStartOffset`). REPEAT/
  DURATION only synthesized when both keys present.
- **Real bug fixed (not hypothetical):** pre-W5 promote unconditionally
  read `alarm->startOffset()` regardless of trigger form — for an
  absolute-trigger or END-related alarm already present on an incoming
  wire VTODO, this silently corrupted it into a bogus `offset: 0`. Now
  fixed as the same code path that adds the new shape. Pinned directly:
  `vtodoAlarmAbsoluteAtFormRoundTrips` and
  `vtodoAlarmEndRelatedOffsetRoundTrips` both assert the alarm does NOT
  also carry a bogus `"offset"`/wrong-direction offset.
- **Capability struct — no new field added,** per Open Decision 1's
  recommendation (localBlob/CalDAV already `Full`; MS `Display` already
  correctly signals reduced fidelity with no finer distinction to carry;
  Google `None`). No caller/consumer of `CalendarCapabilities` was found
  that would read a hypothetical new flag.
- **Loss profile:** no new rows needed on the vtodo leg (alarms not listed
  in `canonToVtodoLoss()` at all — implicitly lossless, confirmed still
  true). MS's existing `Simplified` row and Google's existing `Dropped`
  row for `alarms` are unchanged and confirmed still correct.

### MS-leg shape unification (Open Decision 2 — folded in, as recommended)

**Real, demonstrable bug found during recon, confirmed and fixed:** MS
promote previously built `alarms[0] = {"reminder": {dateTime, timeZone}}`
— a shape that did not match the vtodo leg's `{type, offset/at, related,
text, repeatCount, repeatIntervalSecs}` at all. Demoting an MS-sourced
canon record through `canonObjectToVtodoBytes` read `a.value("type")` →
missing → `0` (`Alarm::Invalid`) and `a.value("offset")` → missing → `0`,
silently producing a **zero-offset Invalid-type VALARM** — i.e. any
cross-vendor sync of an MS-To-Do reminder to a CalDAV/local VTODO
destination previously mangled the alarm every time.

**Fix:** MS promote now emits `alarms[0] = {"type": 1 /* Display */, "at":
<UTC ISO>}` — the unified shape. Graph's `reminderDateTime` is inherently
an absolute trigger (`{dateTime, timeZone}`, never an offset), so `"at"`
is the natural home. A new local helper `msReminderToUtcIso()` mirrors
`mseventcanonstages.cpp`'s `msTimeToCanon` wall-time interpretation
(Windows-vocabulary zone ids resolved through the vendored CLDR map;
offset-less wall time interpreted **in the target zone**, never the
process-local zone) but returns a plain UTC ISO string rather than a
`{dateTime,tz,tzOriginal}` object, since RFC5545 `TRIGGER;VALUE=DATE-TIME`
is always UTC. Demote reads `alarms[0]`'s `"at"` first (via the reverse
helper `utcIsoToMsReminder()`, always emitting `timeZone: "UTC"` — no
original vendor zone survives past canonicalization), falling back to the
legacy `{"reminder": {...}}` sub-key only for backward-compat with a
canon record written by a pre-W5 build. An offset-form alarm arriving from
a non-MS leg (e.g. a CalDAV-sourced VTODO alarm crossing to MS) is **not**
synthesized into an absolute reminder — this is within the already-declared
`Simplified` loss kind for `alarms` on this leg (MS structurally supports
exactly one absolute reminder), not a new regression; flagged explicitly
in code comments rather than silently left undiscovered.

Tested: `msTodoTaskAlarmRoundTripUnifiesShape` (C→MS→C round trip on the
unified shape) plus `promoteRichTaskIsLossless`'s alarm assertion rewritten
to check the new `{type, at}` shape and assert the legacy `"reminder"` key
is absent.

## 3. O74 + W7

### O74 differ fix

**Root cause (confirmed unchanged from FINDINGS.md):**
`CanonJsonDiffer(todoCanonPropertyIds())` only ever compares catalogued
property ids; `providerExtras` is deliberately never catalogued, so an
X-prop/extras-only edit never produced a diff.

**Fix:**

1. New catalogued key `providerExtrasDigest` (`PropertyKind::String`,
   `src/todo/todocanonproperties.cpp`).
2. New domain-neutral helper
   `Kalburator::Shape::CanonEnvelope::canonicalDigest(const QJsonValue&)`
   (`src/shape/canonenvelope.{h,cpp}`). **Simpler than the recon
   anticipated:** the recon's design sketch called for a custom recursive
   key-sort pass before hashing, reasoning that `QJsonObject` iteration
   order might not be stable. Verified empirically (a standalone probe)
   that `QJsonDocument::toJson()` in this Qt6 build **already** serializes
   `QJsonObject` keys in sorted order at every nesting level, regardless
   of insertion order — confirmed with a nested-object case, not just
   flat. So `canonicalDigest` is just `QJsonDocument(value).toJson(Compact)`
   (wrapped in a single-element array for a bare scalar) fed to
   `QCryptographicHash::hash(..., Sha256).toHex()` — the house SHA256+hex
   convention (`localblobbackend.cpp`, `subscriptionbackend.cpp`,
   `mockbackend.cpp`), no custom sorting code needed.
3. Three call sites, one per todo promote path, each inserting
   `providerExtrasDigest` only when the (filtered) extras object is
   non-empty:
   - `vtodocanonfields.cpp` — hashes the whole `xvtodo` object, unfiltered
     (vtodo/CalDAV extras are genuine X- custom properties only, no vendor
     bookkeeping rides this channel).
   - `mstodotaskcanonstages.cpp` — hashes a filtered copy (see filter list
     below).
   - `googletaskcanonstages.cpp` — hashes a filtered copy (see filter list
     below).
4. Loss profile: `providerExtrasDigest` → `Dropped` on all three
   `canonTo*Loss()` functions, with a comment explaining it is derived/meta
   (recomputed fresh on every promote, no wire representation by design).
5. Matrix regenerated (`./build/tools/matrixgen/matrixgen
   docs/campaign/eee/CONVERGENCE-MATRIX.md`) — diff is exactly the three
   new `providerExtrasDigest | Dropped` rows, no `edges()` growth.
   `tst_gm_pipeline_convergence`'s byte-pin (`committedMatrixMatchesGenerated`)
   verified green after regeneration.
6. Differ pin: `differMarksProviderExtrasDigestChangeOnly`
   (`tests/shape/tst_canonjson_diff_merge.cpp`) — a `providerExtrasDigest`
   value change is reported like any other catalogued property.
   `differIgnoresProviderExtrasAndCanon` (the pre-existing, narrower test)
   was **not** modified — it still correctly pins the differ's generic
   "only catalogued keys" contract using its own narrow `{summary}`
   catalogue, unrelated to `providerExtrasDigest`.
7. On the MS leg, `providerExtrasDigest` was also added to the demote's
   `handled` set (the "unhandled canon props → open-extension carrier"
   loop) so it is explicitly excluded from auto-carrying as a bogus
   `x-canon-provider-extras-digest` wire carrier — it must stay Dropped,
   not Reversible.

### The exact volatile-key filter lists (the critical part)

**Google:** excludes `etag` only. Confirmed via the existing promote
comment (`googletaskcanonstages.cpp`) naming the full unfiltered stash
(`kind/etag/deleted/hidden/links/webViewLink/selfLink/assignmentInfo`);
`etag` is the one field that demonstrably bumps on every server-side
write regardless of content; the other five are real content or stable
transport metadata and stay hashed. Tested:
`providerExtrasDigestIgnoresEtag` (`tst_google_task_canon_edge.cpp`) —
constructs three wire objects (base, only-etag-changed, real-content-
changed via `hidden`) and asserts the digest is stable across the first
pair and different for the third.

**MS:** excludes `@odata.etag`, `lastModifiedDateTime`, and
`@odata.context`. Verified (not assumed) against a real captured Graph
todoTask sample —
`msgraph/captured/20260824-145017-773-me-todo-lists-…-tasks-….json` —
whose top-level keys include `@odata.context`, `@odata.etag`,
`createdDateTime`, `lastModifiedDateTime`, none of which are in the
`consumed` set, confirming all four land in the unfiltered stash.
`@odata.etag` and `lastModifiedDateTime` both bump on every write.
`@odata.context` was additionally confirmed (by diffing four separate
captures of the *same* record) to vary purely by request shape (`v1.0` vs
`beta`, plain vs `$expand=extensions()`) — a request-URL artifact, not
per-record content, so it risks a false diff between two different fetch
paths with zero real edit even setting aside write-bumping. **`createdDateTime`
is deliberately kept (NOT filtered)** — it is set once at creation and
does not change on subsequent edits, so it carries no false-dirty risk;
filtering it would only reduce the digest's real coverage for no benefit.
Tested: `providerExtrasDigestIgnoresVolatileMsBookkeeping`
(`tst_ms_todotask_canon_edge.cpp`) — same three-way construction as the
Google test, varying `@odata.etag`/`lastModifiedDateTime`/`@odata.context`
together for the "only bookkeeping changed" case and `hasAttachments` for
the "real content changed" case.

### W7 round-trip tests

- **VALARM (LocalBlob + CalDAV):** landed with W5 above — see §2. This was
  the single biggest W7 gap (zero prior VALARM coverage on the todo path,
  confirmed by the recon's grep).
- **X-props (generic passthrough):**
  `vtodoGenericUnknownXPropSurvivesRoundTrip` feeds a genuinely
  arbitrary/unknown custom property (`X-SOME-RANDOM-CLIENT-FIELD`, not one
  of the recognized/consumed ones) through promote→demote, proving the
  *generic* mechanism rather than only its special-cased consumers.
- **VTIMEZONE:** already covered — confirmed and cited, no new code (see
  the contract doc §2).
- **CalDAV byte-verbatim VTODO:** grepped
  `tests/calendar/tst_remotecalendarbackend_blob_view.cpp` for an existing
  byte-equality assertion on a fetched VTODO — found none (only
  `.contains()` substring checks on unrelated concerns, and href/record-
  count-focused tests). New test added:
  `loadRecords_vtodoFetch_prefersServerRawBytesOverReserialization` —
  seeds a VTODO with a custom X-prop placed *before* SUMMARY in source
  order and asserts that ordering survives in the fetched record's bytes
  (KCalendarCore's own serializer would always place custom X- properties
  after well-known ones, so surviving non-canonical order is only possible
  if the backend's raw-bytes-preference (`m_lastRawIcsByUid`) won over
  re-serialization).
- **Org contract sentence:** written as its own dedicated doc per the
  recon's recommended option (a) —
  `docs/campaign/vtodo-parity/2026-08-28-w7-passthrough-contract.md`,
  containing the finalized truth table, the org warning sentence, the W5
  MS-leg shape-unification note, and the O74 fix note (mirrors the W1/W3
  contract-doc precedent).

---

## Test evidence

Slot counts below are QTest-slot-function counts (grepped, not loose
assertion counts), each independently checkable:

- `tests/todo/tst_todo_canon_roundtrip.cpp`: 23 → **35** slots (+12): W6.2
  (`vtodoCoercesDateOnlyStartToDueDateTimeType`,
  `vtodoCoercesDateTimeStartToDueDateOnlyType`,
  `vtodoDropsStartWhenDueNotAfterDtstart`,
  `vtodoPromoteDropsDurationWithoutDtstart`,
  `vtodoAllDayRoundTripPreservesDateValueForm`), W5
  (`vtodoAlarmOffsetFormRoundTrips`, `vtodoAlarmAbsoluteAtFormRoundTrips`,
  `vtodoAlarmEndRelatedOffsetRoundTrips`,
  `vtodoAlarmRepeatDurationPairRoundTrips`,
  `vtodoAlarmUnpairedRepeatIsNotSynthesized`), W7/O74
  (`vtodoGenericUnknownXPropSurvivesRoundTrip`,
  `vtodoProviderExtrasDigestTracksExtrasContent`); plus the loss-profile
  test extended with a `providerExtrasDigest → Dropped` assertion
  (no new slot, existing `canonToVtodoLossProfileChargesDroppedAndReversible`
  extended).
- `tests/todo/tst_ms_todotask_canon_edge.cpp`: 6 → **8** slots (+2):
  `providerExtrasDigestIgnoresVolatileMsBookkeeping`,
  `msTodoTaskAlarmRoundTripUnifiesShape`; plus `promoteRichTaskIsLossless`
  (alarm shape + digest presence) and `inspectDeclaresMsTodoTaskEdge`
  (`providerExtrasDigest → Dropped`) extended in place.
- `tests/todo/tst_google_task_canon_edge.cpp`: 5 → **6** slots (+1):
  `providerExtrasDigestIgnoresEtag`; plus `promoteRichTaskIsLossless` and
  `inspectDeclaresGoogleTaskEdge` extended in place.
- `tests/shape/tst_canonjson_diff_merge.cpp`: 13 → **14** slots (+1):
  `differMarksProviderExtrasDigestChangeOnly`.
- `tests/calendar/tst_remotecalendarbackend_blob_view.cpp`: 27 → **28**
  slots (+1): `loadRecords_vtodoFetch_prefersServerRawBytesOverReserialization`.
- `tests/convergence/tst_gm_pipeline_convergence.cpp`: byte-pin green
  after matrix regeneration (diff = exactly 3 new `providerExtrasDigest`
  rows, no edge-count change).

**Total new test slots this item: 17.**

**Full suite:** `ctest --test-dir build --output-on-failure -j4`: 214
tests, **210 passed**, 4 failed — exactly the 4 known pre-existing
environmental Radicale/KDAV failures (`tst_backend_signals`,
`tst_backend_thread_relocation`, `tst_backend_reentrancy_pin`,
`tst_remotecalendarbackend`); no new failures, no regressions. (One
unrelated flake hit mid-session: `tst_etagcache_seed`'s binary was a
truncated/corrupted artifact from an earlier build in this same session —
rebuilding that one target fixed it; confirmed unrelated to any VP.f
change by running it standalone.) All individually-run touched suites are
green: `tst_todo_canon_roundtrip` 35/35,
`tst_ms_todotask_canon_edge` 8/8, `tst_google_task_canon_edge` 6/6,
`tst_canonjson_diff_merge` 14/14, `tst_remotecalendarbackend_blob_view`
28/28, `tst_gm_pipeline_convergence` 10/10.

## Deprecations / behavior changes affecting PlanStan callers

- **Behavior change (W6.2):** a VTODO whose DTSTART/DUE date-value-types
  disagree now has its DTSTART's type coerced to match DUE's on promote
  (previously each side kept its own literal type in canon). A VTODO
  where DUE ≤ DTSTART now has no `start` key in canon at all. A
  DATE-only DTSTART/DUE now correctly round-trips as `VALUE=DATE` on
  demote (previously silently became a UTC-midnight DATE-TIME).
- **Behavior change (W5):** the `alarms[]` row shape has additive keys
  (`at`/`related`/`repeatCount`/`repeatIntervalSecs`) — old rows
  (`{type, offset, text}`) remain valid and are interpreted identically.
  An MS-sourced canon record's `alarms[0]` shape changed from
  `{"reminder": {...}}` to `{"type", "at"}` — any PlanStan code reading
  `alarms[0].reminder` directly (bypassing this library's demote) would
  need updating; the demote seam itself reads both shapes for backward
  compat.
- **New additive canon key `providerExtrasDigest`** (O74) — absent unless
  a leg's `providerExtras` is non-empty at promote time. Purely internal/
  derived; no PlanStan consumer should read or write it directly (it is
  Dropped on demote by design).
