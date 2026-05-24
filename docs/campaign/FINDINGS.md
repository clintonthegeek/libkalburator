# Campaign FINDINGS — canon-upgrade / convergence

Two sections:
- **Open issues / watch items** — things discovered that a future task must handle.
  When you resolve one, note the commit and move it to "Resolved."
- **Discipline Log** — one-line smell reports per invariant 9. `file:line` + invariant
  number + one phrase. No fix required in the session you log it.

Append, don't rewrite. New issues from any task go here, even off-topic.

---

## Open issues / watch items

### O1 — `LossProfile` is surfaced to the engine layer
`tests/engine/tst_engine_unified_routing.cpp:383` reads `lastLossProfile().level`, and
`tests/engine/tst_carddav_engine_integration.cpp` imports `LossLevel`. Plan 1 Task 2
migrates these to the kinds API. **Watch:** if a stub host or engine code (not just
tests) exposes `LossProfile::level`/`.dropped`, Task 2's full build will name it — fix
with the documented substitutions, don't reintroduce the old fields. (Seeded 2026-05-23.)

### O2 — Carry-verbatim containers not yet field-specified
The non-isomorphic structures (event `RANGE=THISANDFUTURE`; todo hierarchy:
`relatedTo` tree / `parentUid` / `checklistItems`) are decided in principle (schema
§2, §4) but their exact JSON shape and differ/merger handling land in **Plan 3**.
Invariant P4 applies: specify the carrier before diffing it. (Seeded 2026-05-23.)

### O3 — Live API validation deferred, not done
The vendor-shapes reference (`docs/2026-05-23-vendor-api-shapes-reference.md`) is from
docs + MS open specs, not live calls. Edge cases (real recurrence payloads, IANA↔Windows
mappings, Graph immutable-id behavior) should be validated with live Google/Graph calls
**during Plan 3** when the concrete (de)serialization stages are written. Not a blocker
for Plans 1–2. (Seeded 2026-05-23.)

### O4 — WildPalms port is downstream, but its invariants are upstream
WildPalms moves its conduits onto the converged pipeline **after this branch merges**
(handoff §3, §7). We do not wire WildPalms. But the five WildPalms invariants
(invariant 10) must hold throughout — especially that the loss model can express
`Reversible` (X-property round-trip) before Plan 3 writes the palm/canon edges. (Seeded 2026-05-23.)

### O5 — `pipeline.cpp` loss folding assumed clean
Planning grep found no `.level`/`.dropped` usage in `src/shape/pipeline.cpp`, so
`composedLoss()` should fold via `compose()` unchanged. **Watch:** confirm during Plan 1
Task 2's full build; if `composedLoss()` references the removed fields, update it the
same way. (Seeded 2026-05-23.)

### O6 — design §8 was imprecise; corrected when Plan 2 was designed
The §8 planning stub said **two** `Shape::` singletons remain (`TransformationRegistry`,
`DomainRegistry`) and that they would be "owned by the `SyncEngine`." Reading the landed call sites
showed **three** (the engine also reads `DomainOperationsRegistry` at `syncengine.cpp:1874`,`:2423`;
~40 tests `clear()` it), and that engine-ownership is wrong because `PluginManager` is an independent
*writer* of the same registries. §8 was rewritten (2026-05-24) to the resolved topology — an injected
`ShapeRegistries` bundle owned at the composition root, shared by reference with both PluginManager
(writer) and SyncEngine (reader), the OSGi `BundleContext` model. Recorded as a documented deviation
from the stub per the INVARIANTS deviation rule. (Seeded 2026-05-24, Plan 2 design.)

### O7 — Ambient-Context default bundle must be removed after downstream ports
Plan 2 keeps a process-global `defaultShapeRegistries()` and `::instance()` accessors that delegate
to it, purely so PlanStan/WildPalms keep compiling against the current `SyncEngine`/`PluginManager`
ctors (invariant 10). This is Seemann's **Ambient Context anti-pattern**, legitimate **only as
removable scaffolding**. **Watch:** once PlanStan and WildPalms adopt the injecting ctor (downstream-
port work, after this branch merges — see O4), delete `defaultShapeRegistries()` and the three
`::instance()` accessors so the only construction site is the composition root. Not Plan 2's job;
Plan 2 lands the seam green. (Seeded 2026-05-24, Plan 2 design.)

### O8 — Plan-3/Plan-4 boundary: calendar canon moved into Plan 3 (scope decision)
Design §10's file-change list placed "`calendarstockshapes.cpp`: add canon + `ical↔canon`
bridges" under the **Plan 4** (convergence) work, while the STATUS Plan-3 row named
`calendar+canon` as Plan-3 scope — two sources of truth disagreeing (inv 7). Surfaced while
writing Plan 3 (2026-05-24). **Decision (human, 2026-05-24):** land all three canons —
including `calendar+canon` and its `ical↔canon` bridges — in **Plan 3**; narrow **Plan 4** to
pure convergence (retire `src/transcoding/`, RRULE-as-edge, remove `ApplyContext.transcodingPlan`
+ `CalendarPluginWriter` special-casing). Safe because the engine reads canonical from
`DomainDefinition::canonicalShape()` (not the graph) and transcoding is dormant in the default
build (empty plans; `RRuleTranscoder` only fires for `orgmode` backends, not built when
`KALBURATOR_HAVE_ORG_IO=OFF`). Plan 3 Task 13 records the same in STATUS on completion.
(Seeded 2026-05-24, Plan 3 authoring.)

### O9 — pre-existing `tst_providerlifecycle` failure (unrelated, not introduced by campaign)
`tst_providerlifecycle::provisionProvider_backendsReadyEmittedAfterConnectAll` fails in
test #87 (introduced in commit `b395e5b`, before the campaign branch). The failing assertion
`ready.count() >= 1` implies a signal-timing or async issue in the `ProviderLifecycle` test
harness. The campaign suite is 111/112 green; this one excluded. **Watch:** investigate
independently; do not confuse with campaign regressions. (Seeded 2026-05-24, Plan 3 Task 13.)

### O10 — design §10 "delete src/transcoding/ in full" is wrong: incidencediff/syncdiff are load-bearing
While researching Plan 4 (2026-05-24), found that `src/transcoding/incidencediff.{h,cpp}` and
`src/transcoding/syncdiff.{h,cpp}` are **conflict/diff engines**, not transcoding, and are used widely
outside the dir: `src/engine/syncengine.{h,cpp}`, `enginediff.h`, `propertydiff.h`,
`src/calendar/icalrecorddiffer.{h,cpp}`, `icalrecordmerger.cpp`, `updateincidenceitem.cpp`,
`decsyncactivecontroller.{h,cpp}`, and tests `tst_syncdiff.cpp`/`tst_incidencediff.cpp`/
`tst_calendar_subsequent_sync_uses_blob_view.cpp`. So design §10's "Delete `src/transcoding/` in full"
(inv 7: two sources of truth) cannot be taken literally. **Plan 4 retires only the transcoding
machinery** (`transcodingregistry`, `transcodingrouter`, `transcodingplan`, `rruletranscoder`,
`propertytranscoder`) and must **keep `incidencediff`/`syncdiff`** (move them out of the dir or leave
the dir as their home — decision pending). (Seeded 2026-05-24, Plan 4 research.)

### O11 — legacy RRuleReverseTranscoder was a silent no-op (latent bug, superseded by Plan 4 Task 2)
While re-homing RRULE simplification (Plan 4 Task 2, commit 624d2f3), found that the legacy
`src/transcoding/rruletranscoder.cpp` `RRuleReverseTranscoder::transcode` (~:138-174) restored the
stashed original RRULE via `KCalendarCore::ICalFormat::fromString(RecurrenceRule*, ruleStr)` — which
**always returns false for RRULE strings** (both the `RRULE:FREQ=…` form and the value-only form),
so the reverse transcoder silently restored *nothing*. No test ever covered it. The new
`OrgICalToCanonStage` sidesteps the broken API: it stashes verbatim iCal recurrence *lines* (joined by
`|`, a separator RFC5545 never uses) and restores by byte-level line injection — strictly more
faithful (byte-for-byte) and proven by `tst_orgical_canon_roundtrip`'s round-trip slot. The legacy
class is deleted in Plan 4 Task 8 regardless. (Seeded 2026-05-24, Plan 4 Task 2.)

## Resolved

### O1 — `LossProfile` engine-layer migration (resolved Plan 1 Task 2, 2026-05-23)
`tst_engine_unified_routing.cpp` and `tst_carddav_engine_integration.cpp` were migrated
to `isLossless()` / `droppedProperties()` in Task 2. Full build confirmed no remaining
`LossLevel`/`.level`/`.dropped` references in production or test code.

### O2 — Carry-verbatim containers (resolved Plan 3, 2026-05-24)
All non-isomorphic structures (`relatedTo` tree / `parentUid` / `checklistItems` for todo;
`recurrence` verbatim string list for both ical and vtodo) landed in Plan 3:
`todocanonproperties.cpp` defines the catalogue fields; `vtodocanonstages.cpp` +
`icalcanonstages.cpp` capture RRULE/RDATE/EXDATE as raw RFC5545 lines (invariant 3). The
differ (CanonJsonDiffer) treats each as one opaque-field change per coarse-granularity rule.

### O3 — Live API validation deferred (partially resolved Plan 3, 2026-05-24)
The concrete JSON (de)serialization stages for all three domains are written and covered by
round-trip tests. The remaining unknowns (live Google/Graph payloads, IANA↔Windows timezone
mapping, Graph immutable-id edge cases) still require live integration testing, deferred to
when real provider connectors are wired. Not a blocker for convergence (Plan 4).

### O5 — `pipeline.cpp` loss folding (resolved Plan 1 Task 2, 2026-05-23)
`composedLoss()` in `src/shape/pipeline.cpp` uses only `compose()` — no direct field
access — so it compiled cleanly without changes. Confirmed by Task 2's full build.

---

## Discipline Log

Format: `YYYY-MM-DD — file:line — inv N — phrase`

2026-05-24 — src/contacts/vcardcanonstages.cpp (VCard4ToCanonStage, birthday mapping) — inv 4 — `birthday.hasYear` is hardcoded `true`: KContacts exposes `birthdayHasTime()` but no `birthdayHasYear()`, so a `--MMDD` (year-less) vCard4 BDAY round-trips with a spurious year. Edge case, not exercised by current tests; revisit if year-less birthdays become a contract.
2026-05-24 — {contacts,todo,calendar}domaindefinition.cpp `richnessRank()` — deviation note — Plan 3 A4/B4/C4 specified `s==canonicalShape()?100:10`; implementations use 100 (canon) / 50 (primary legacy peer: vcard4, ical-vtodo, ical) / low (vcard3=10, todotxt=3, calendar-other=0). Documented deviation per the INVARIANTS deviation rule: only relative ordering matters (canon strictly highest) and the 3-tier scheme is consistent across all three domains and models the extra peers (vcard3, todotxt) more accurately than a flat 10. Harmless; recorded so the spec/code divergence is not mistaken for a bug.
2026-05-24 — Plan 3 Parts A, B & C — inv 4/5 post-review fixups (commits 89edbbb, 7f68e36, 5b00a47) — a single subagent ran A5→Task13 unsupervised (per-task review checkpoints skipped) and introduced the SAME false-loss-contract bug in all three domains: a loss classified Reversible/Degraded whose verbatim stash was never emitted in code. Specifically: contacts sipAddresses/calendarUrls/externalIds (Reversible, A5); VTODO Degraded-status + checklistItems/sortOrder Reversible (B5); iCal classification="personal" Degraded (C5). Recurrence round-trip tests also asserted substring, not byte-identity. Fixed: originals now stashed as `CANON-*`/`X-CANON-*` custom props that round-trip into providerExtras; recurrence tests assert byte-identical RRULE/EXDATE; each fix has a falsifiable round-trip test. Caught by retroactive spec-compliance review of B and C, then — prompted by the human asking "is the mess cleaned up" — the identical bug was found and fixed in contacts (A) too. Lesson: when one agent silently expands scope past its task, review EVERY task it touched, not just the ones you remember dispatching.
