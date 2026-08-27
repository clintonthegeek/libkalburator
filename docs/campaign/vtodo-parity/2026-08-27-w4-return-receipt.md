# Return receipt — W4 completion-anchored recurrence

**Delivered:** 2026-08-27 (commit `43d74b1`)
**Consumes:** handoff §W4; response doc §W4 ("Completion-anchored
recurrence — ACCEPTED with decision"); recon handoff
`2026-08-26-w4-recon-handoff.md` (code map + 4 open decisions).

## Canon schema / envelope keys

- New catalogued key `completionAnchor` (`PropertyKind::Json`) —
  `src/todo/todocanonproperties.cpp:40-47` (right after `recurrenceId`/
  `recurrenceRange`, mirroring their placement). Shape:
  `{"type": "catchUp"|"restart", "interval": <int>, "unit": "h"|"d"|"w"|"m"|"y"}`.
  Catalogued ⇒ automatically differ-visible via `todoCanonPropertyIds()`
  (`:60-67`, unchanged) — no differ code change needed for the
  non-conflict treatment (see Tests below).
- The verbatim org repeater string is NOT duplicated into a dedicated
  canon key — it rides `providerExtras["x-vtodo"]["X-ORG-REPEATER"]` via
  the pre-existing generic custom-prop channel
  (`vtodocanonfields.cpp:279-292` promote / `:596-602` demote,
  unchanged), exactly as the binding spec specifies ("no X-prop
  duplication").

## Seams touched

- **Promote** — `src/todo/vtodocanonfields.cpp`, new block right after
  the `recurrenceId`/`recurrenceRange` block (was `:216-231` pre-W4).
  Recognizes a generic `X-ORG-REPEATER` custom prop
  (`todo->nonKDECustomProperty("X-ORG-REPEATER")`) and derives
  `completionAnchor` via a new helper `parseOrgRepeaterToCompletionAnchor()`
  in the file's anonymous namespace. Regex mirrors org-io's
  `(\.\+|\+\+|\+)(\d+)([hdwmy])` (longest-sigil-first ordering, same
  discipline as the repo's grep-alternation house rule); only `.+`
  (Restart) and `++` (CatchUp) derive a `completionAnchor` — a bare `+`
  (Cumulative) is out of W4 scope and is left alone (still captured
  verbatim in `providerExtras["x-vtodo"]` like any other custom prop, but
  does not produce a `completionAnchor`).
- **Demote** — same file, two new pieces:
  1. `completionAnchorFreqForUnit()` helper (unit alphabet → RRULE FREQ:
     h→HOURLY, d→DAILY, w→WEEKLY, m→MONTHLY, y→YEARLY — decision 3,
     verbatim as specified).
  2. A `derivedRecurrenceBytes` block computed right after the
     `recurrenceArr` capture (was `:512-516` pre-W4): builds
     `RRULE:FREQ=<X>` (`;INTERVAL=<n>` appended only when `interval != 1`,
     matching the RRULE-building convention already used by
     `recurrencepatternconverter.cpp`), guarded so it only fires when
     `recurrenceArr` is empty (verbatim recurrence always wins —
     invariant 3; defensive, since a `completionAnchor`-bearing VTODO
     should never also carry a native RRULE in practice — pinned by
     `vtodoVerbatimRecurrenceTakesPrecedenceOverCompletionAnchor`).
  The derived bytes are appended into the SAME pre-existing
  recurrence-injection seam (post-serialization insert before
  `END:VTODO`, was `:685-699` pre-W4) that already handles verbatim
  recurrence lines — no new injection point.

## Design decision beyond the recon doc: "anchored at last completion"

The binding spec says the derived RRULE is "anchored at last completion
ONLY (no X-prop duplication)" but does not say *how* to encode an anchor
in RRULE syntax — RFC5545 recurrence rules are anchored by DTSTART, not
by a rule parameter. I resolved this concretely: when canon carries no
explicit `start` of its own (tracked via a new `hadExplicitStart` bool,
mirroring the existing `hadCreated`/`hadLastModified` pattern), the
derived RRULE is accompanied by an explicit `DTSTART:<completedUTC>`
line — so the rule's real RFC5545 anchor literally IS the completion
timestamp. If canon already has an explicit `start`, that DTSTART is
left untouched (must not clobber a real scheduled start) and the RRULE
rides on it instead — a declared, narrow corner case (pinned by
`vtodoCompletionAnchorDoesNotOverrideExplicitStart`) since a
`completionAnchor`-bearing org task ordinarily has no DTSTART of its
own. This is loud-about-limits by design: the corner case is named,
tested, and documented rather than silently "handled."

## Loss-profile declarations (all in the same commit as the code, O63)

- `canonToVtodoLoss()` (`src/todo/vtodocanonstages.cpp:65-90`):
  `completionAnchor` → **Reversible** (rides `providerExtras/x-vtodo` +
  the derived RRULE — round-trippable, matches the recon recommendation).
- `canonToGoogleTaskLoss()` (`src/todo/googletaskcanonstages.cpp:246-269`):
  `completionAnchor` added to the `Dropped` list alongside `recurrence` —
  Google Tasks has no recurrence field of any kind, so the derived
  standard form is equally unrepresentable and there is no carrier
  channel (matches the existing `recurrence`/`priority` ruling on this
  edge).
- `canonToMsTodoTaskLoss()` (`src/todo/mstodotaskcanonstages.cpp:474-570`):
  `completionAnchor` is deliberately **NOT** added to the demote's
  `handled` set — it auto-carries through the existing unhandled-canon-
  prop loop as an open-extension carrier row
  (`x-canon-completion-anchor` on the `kalburator.canon` open extension,
  same mechanism as `percentComplete`/`relatedTo`/etc.), declared
  **Reversible** in the loss profile — decision 2, matching the existing
  `recurrence` ruling on this edge exactly.
- Matrix regenerated: `./build/tools/matrixgen/matrixgen >
  docs/campaign/eee/CONVERGENCE-MATRIX.md`. Diff is exactly the three new
  rows above (`completionAnchor | Reversible` under canon→ical-vtodo,
  `completionAnchor | Dropped` under canon→google-task,
  `completionAnchor | Reversible` under canon→ms-todotask) — no edge-count
  change (O63: only `edges()` growth moves those pins, and none of the 9
  todo edges changed). `tst_gm_pipeline_convergence` byte-pin passes
  against the committed regenerated file.

## The 4 open decisions — how each was resolved

1. **Org-leg promote seam.** Landed as **generic promote support only**
   (`X-ORG-REPEATER` custom-prop recognition in `todoFieldsToCanon`),
   fully testable in the default profile. **`KALBURATOR_HAVE_ORG_IO=ON`
   is NOT buildable standalone in this environment** — verified by
   actually running the configure+build:
   `cmake -S . -B /tmp/orgio-test-build -DKALBURATOR_HAVE_ORG_IO=ON` and
   building it fails at moc/compile time on `orgbackend.h`:
   ```
   fatal error: orgfilemanager.h: No such file or directory
       6 | #include "orgfilemanager.h"
   ```
   because the CMakeLists comment is accurate: "When ON, host must
   provide the `planstan-org-io` target via PUBLIC linkage from
   outside" — no such host exists in a standalone libkalburator build
   (org-io's own `CMakeLists.txt` under
   `~/dev/PlanStan/libs/org-io/` exists, but nothing in this repo's
   build wires its include path when built standalone). Per the task's
   own instructions this means: **OrgBackend wiring is DEFERRED**, not
   skipped-without-record. A TODO comment is left at the promote seam
   (`vtodocanonfields.cpp`, in the new block) naming exactly what remains:
   wire `OrgBackend` to inject `X-ORG-REPEATER` from
   `m_roundtripData.repeaterString` at fetch time, at the canon-promote
   boundary (never by mutating the incidence, per the incidence-purity
   invariant pinned by `tst_orgbackend_external.cpp:611-615,631-634`),
   once a build with a real `planstan-org-io` host target is available to
   verify against those purity pins. This repo's standalone `tst_orgbackend`
   suite (`tests/calendar/tst_orgbackend.cpp`, gated
   `KALBURATOR_HAVE_ORG_IO=ON`) was consequently **not extended** —
   there is no way to compile or run it here. `testCatchUpRepeaterRoundtrip`
   / `testRestartRepeaterRoundtrip` extension is still open work for
   whoever next has an org-io-enabled build (e.g. building inside
   PlanStan, which supplies the host target).
2. **MS carrier.** Auto-carry, as recommended — see loss-profile section
   above. Not added to `handled`; declared Reversible.
3. **Unit alphabet → RRULE FREQ.** h→HOURLY, d→DAILY, w→WEEKLY,
   m→MONTHLY, y→YEARLY, exactly as specified.
4. **Anchor source.** Canon `completed` timestamp, encoded as described
   in the "Design decision" section above (explicit DTSTART line when no
   competing explicit `start` exists).

## Tests proving acceptance

- `tests/shape/tst_canonjson_diff_merge.cpp` (+2, now 14 slots):
  `differMarksCompletionAnchorAdvanceAsOrdinaryChange` (an advance in
  `completed` alongside an unchanged `completionAnchor` is reported as an
  ordinary `completed` change — the anchor itself doesn't spuriously
  show as changed when both sides already converged on it) and
  `differMarksCompletionAnchorContentChangeOnly` (a genuine anchor
  content edit — interval bump — is reported like any other catalogued
  property, no special conflict machinery). Together these pin the
  binding spec's non-conflict claim: cataloguing the key is the entire
  mechanism, and the differ has no separate "conflict" concept to
  bypass — it just reports changed keys.
- `tests/todo/tst_todo_canon_roundtrip.cpp` (+10, now 23 slots):
  - `vtodoPromotesRestartCompletionAnchorFromOrgRepeaterMarker` /
    `vtodoPromotesCatchUpCompletionAnchorFromOrgRepeaterMarker` — marker
    → correct `{type,interval,unit}`; verbatim marker also lands in
    `providerExtras/x-vtodo`.
  - `vtodoBareCumulativeRepeaterDoesNotDeriveCompletionAnchor` — `+1w`
    (Cumulative) does NOT derive a `completionAnchor` (W4 scope pin).
  - `vtodoPromoteWithoutRepeaterMarkerHasNoCompletionAnchor` — no marker
    ⇒ no key.
  - `vtodoDemoteDerivesRruleAnchoredAtCompletedForRestartAnchor` —
    `completionAnchor` (no explicit start) → `RRULE:FREQ=WEEKLY` (interval
    1 omitted) + `DTSTART:<completed>`; re-parsed todo recurs and
    `dtStart(true)` (the literal-DTSTART overload — see inline comment on
    why the no-arg getter is unsuitable for a recurring todo) matches.
  - `vtodoDemoteCatchUpAnchorEmitsIntervalAndUnitMapping` — interval=2,
    unit=d → `RRULE:FREQ=DAILY;INTERVAL=2` + matching DTSTART.
  - `vtodoCompletionAnchorDoesNotOverrideExplicitStart` — explicit
    `start` present ⇒ exactly one DTSTART line, matching the explicit
    start, not `completed` (declared corner case).
  - `vtodoVerbatimRecurrenceTakesPrecedenceOverCompletionAnchor` —
    defensive precedence guard.
  - `vtodoCompletionAnchorRoundTripStable` — promote → demote → promote
    reproduces the identical `completionAnchor` (re-derived from the
    marker, which survives via the extras channel — demote's re-emitted
    `X-ORG-REPEATER` carries a KCalendarCore-added `;VALUE=TEXT` param,
    so the test asserts via the parsed `nonKDECustomProperty()` getter
    rather than a literal byte match).
  - `canonToVtodoLossProfileChargesCompletionAnchorReversible` — registry
    `inspect()` pin.
- `tests/todo/tst_google_task_canon_edge.cpp` (+1, now 7 slots):
  `completionAnchor` present in canon ⇒ absent from the demoted Task
  wire object (silent drop, no carrier — same test group as the existing
  priority/recurrence drop assertion); `inspectDeclaresGoogleTaskEdge`
  extended with the `Dropped` pin.
- `tests/todo/tst_ms_todotask_canon_edge.cpp` (+1, now 8 slots):
  `completionAnchor` present in canon ⇒ a `kalburator.canon` extension
  row carrying `x-canon-completion-anchor` (auto-carry pin, same test
  group as the existing `percentComplete` carrier assertion);
  `inspectDeclaresMsTodoTaskEdge` extended with the `Reversible` pin.
- `tests/convergence/tst_gm_pipeline_convergence.cpp` — byte-pin passes
  against the regenerated `CONVERGENCE-MATRIX.md` (same commit).

Full suite: 213 tests total (was 209 pre-W4; +4 net new test binaries'
worth of slots inside existing suites — no new test executables). Ran
`ctest --output-on-failure -j"$(nproc)"`: **209 passed, 4 failed** — the
failures are exactly the 4 known pre-existing environmental Radicale/KDAV
slots (`tst_backend_signals`, `tst_backend_thread_relocation`,
`tst_backend_reentrancy_pin`, `tst_remotecalendarbackend`), unrelated to
this work. All W4-touched suites individually green:
`tst_todo_canon_roundtrip` 23/23, `tst_canonjson_diff_merge` 14/14,
`tst_google_task_canon_edge` 7/7, `tst_ms_todotask_canon_edge` 8/8,
`tst_gm_pipeline_convergence` 1/1.

## Deprecations / behavior changes affecting PlanStan callers

- None breaking. `completionAnchor` is a new, additive catalogued canon
  key — absent unless a `X-ORG-REPEATER` custom prop is present on
  promote (generic seam) or a caller stages it directly (the intended
  usage per the binding spec's Q2 answer: "caller advances on
  completion"). No existing canon consumer sees a new required field.
- **Correction / gap vs the recon doc:** decision 1's "if it builds
  cleanly" branch does not apply here — this repo's standalone build
  cannot exercise `KALBURATOR_HAVE_ORG_IO=ON` at all (confirmed by
  actually attempting it, not just by reading the CMake comment). The
  OrgBackend injection wiring is therefore fully deferred, not partially
  landed. Anyone picking this up needs a build where a parent project
  (e.g. PlanStan) supplies the `planstan-org-io` target.

## Next

W3 series-split (VP.e), then W5 (alarm extension) + W6.2 (date coercion)
+ W7 (passthrough tests) (VP.f).
