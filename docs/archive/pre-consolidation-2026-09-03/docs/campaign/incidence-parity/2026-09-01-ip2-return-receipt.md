# Return receipt — IP.2: close the live calendar catalogue drift (O78 RESOLVED)

**Delivered:** 2026-09-01
**Consumes:** `docs/campaign/incidence-parity/PLAN.md` §1 (execution rules,
binding) and the IP.2 section; `STATUS.md`; the IP.1 receipt
(`2026-08-29-ip1-return-receipt.md`); FINDINGS.md O78.
**Scope discipline:** one `src/` file changed
(`src/calendar/calendarcanonproperties.cpp`, +3 catalogue entries and a
comment). Everything else is tests and docs. One out-of-scope bug found —
**O84** — logged and deliberately **not fixed**.

---

## 0. What landed

1. **`src/calendar/calendarcanonproperties.cpp`** — `seriesSplitOf`
   (String, "Series Split Of"), `completionAnchor` (Json, "Completion
   Anchor") and `providerExtrasDigest` (String, "Provider Extras Digest")
   added to the existing "Union across iCalendar component kinds" block.
   Kind and display name match `todocanonproperties.cpp:47-60` character
   for character — checked by diffing the two declaration texts, not by
   eye. The block comment names the mechanism (shared emitter via
   `icalcanonstages.cpp:56`) and the two costs (differ blindness + merger
   drop) so the next reader does not have to reconstruct them.

2. **`tests/calendar/tst_calendar_kind_dispatch.cpp`** — IP.1's
   `QEXPECT_FAIL` removed from `calendarCatalogueDeclaresVtodoKeys()`. The
   comment above it is rewritten in the past tense rather than deleted; a
   future reader hitting a regression here should learn what this slot once
   caught. All 7 IP.1 coverage slots are now green with no XFAIL.

3. **`tests/shape/tst_canonjson_diff_merge.cpp`** — three new slots, all
   built on the **real** `Kalburator::Calendar::calendarCanonPropertyIds()`
   rather than a hand-listed `PropertyId` set (see §1):
   - `mergerKeepsCalendarVtodoSeriesSplitOfFromSource()` — the regression
     PLAN.md's acceptance criteria name explicitly: a source-side
     `seriesSplitOf` change on a `{calendar,canon}` vtodo-kind record
     survives a merge.
   - `mergerKeepsCalendarVtodoAnchorAndDigestFromSource()` — the other two
     keys, source-vs-target conflicting, source wins under the default
     policy.
   - `differSeesCalendarVtodoDriftedKeys()` — the differ half: an edit
     confined to any one of the three dirties the diff.
   - plus `mergerPreservesIncidenceKind()`, the **O84 pin** — see §3.

4. **`docs/campaign/FINDINGS.md`** — O78 flipped OPEN → RESOLVED with a
   note that the *class* stays open until IP.3; **O84 filed**.

## 1. The one design decision this item actually had

PLAN.md asked for "a new merger slot ... pinning that a source-side
`seriesSplitOf` change on a `{calendar,canon}` vtodo-kind record survives a
merge." Every pre-existing slot in that file constructs its merger from a
small inline literal set — `CanonJsonMerger m("calendar", {
PropertyId{"summary"} })`. Writing the new slot that way would have made it
**pass identically before and after the IP.2 fix**, because the hand-listed
set is exactly the thing that drifts. That is the same failure mode as the
tombstone slot IP.1 deleted, reproduced in a new file, and PLAN.md's second
prohibition ("do not hand-maintain a key list ... in code, in a test, or in
a doc table") forbids it.

So all four new slots construct their differ/merger from
`calendarCanonPropertyIds()` — the live catalogue — which required adding
`#include "calendarcanonproperties.h"` to a `tests/shape/` test. That is
free: `src/calendar` is already a PUBLIC include directory of the
`kalburator` target (root `CMakeLists.txt:789`) and `tests/shape` link
whole-archive against `Kalburator::Sync`, so no CMake change was needed.
The pre-existing narrow-catalogue slots were left untouched — they pin the
differ/merger *mechanism* independently of any domain's catalogue contents,
which is a different and still-worth-having assertion.

**Verified the new slots are not vacuous**: reverted the
`calendarcanonproperties.cpp` change locally, rebuilt, and confirmed
`mergerKeepsCalendarVtodoSeriesSplitOfFromSource` fails with the target's
(absent) value and `differSeesCalendarVtodoDriftedKeys` fails on all three
keys; restored.

## 2. Probe outcomes

- **The three keys are now catalogued and the IP.1 gate is green without
  XFAIL.** `ctest -R tst_calendar_kind_dispatch`: 14/14 QTest results, 14
  PASS, 0 XFAIL, 0 FAIL (was 13 PASS + 1 XFAIL at IP.1).
- **The merger regression is closed.** Pre-fix, the merged record came back
  with no `seriesSplitOf` at all (target had none; `out = t` kept that);
  post-fix it carries `"old-master-uid"`.
- **Convergence matrix is byte-identical.** Regenerated via
  `./build/tools/matrixgen/matrixgen` and diffed against the committed
  `docs/campaign/eee/CONVERGENCE-MATRIX.md` — **no change**, which is the
  expected and correct outcome: the matrix is generated from loss profiles
  and edges, and IP.2 changed neither. The byte-pin slot in
  `tests/convergence/` is green. No `edges()` list grew, so the O63
  edge-count grep is a no-op here; no new vendor pair or domain edge, so no
  new crossing gate is owed under O64.

## 3. Finding logged, NOT fixed — O84 (`CanonJsonMerger` erases `_canon.kind`)

The merger slot was originally written to assert, alongside the surviving
`seriesSplitOf`, that the merged record was still kind-tagged `vtodo`. It
failed. The cause is not O78 and not the catalogue:

`CanonJsonMerger::merge()` ends with the **3-arg**
`CanonEnvelope::stampEnvelope(out, m_domain, mergedUid)`
(`canonjsonmerger.cpp:60`). `stampEnvelope` constructs a *fresh* `_canon`
object and writes `kind` only when the argument is non-empty
(`canonenvelope.cpp:27-32`), so it **erases** the incoming kind rather than
leaving it alone. `CanonToICalStage` then reads an empty kind and takes the
v1 back-compat branch — `vevent` (`icalcanonstages.cpp:85`).

**Confirmed end-to-end, not inferred.** With the `QEXPECT_FAIL`
temporarily removed, a merged kind-`vtodo` record fed to
`CanonToICalStage` demoted to:

```
BEGIN:VCALENDAR
...
BEGIN:VEVENT
UID:t-4
SUMMARY:edited
```

This is strictly worse than O78 — O78 dropped three field values; O84
changes the component type of the whole record — and it hits VJOURNAL as
well as VTODO. It is confined to the `calendar` domain: every other domain
is single-kind and its demote stage does not dispatch on `_canon.kind`.

**Why it is not fixed here.** PLAN.md §1: "Do not fix while passing
through. If an item's work reveals a bug outside its scope, log it to
FINDINGS and name it in the receipt. The next item picks it up." IP.2's
stated file scope is `src/calendar/calendarcanonproperties.cpp`; the fix
lives in `src/shape/canonjsonmerger.cpp` and turns on a question this item
has no mandate to answer — **whose kind wins when source and target
disagree.** That is not a formality: a kind mismatch on a single uid means
something upstream has already gone wrong, and silently picking one is how
the O78 class of defect gets planted. Whoever owns it should decide
explicitly and write the decision down.

Instead the bug is **pinned**, in the campaign's own idiom (IP.1's
precedent): `mergerPreservesIncidenceKind()` lands with two
`QEXPECT_FAIL(..., Continue)` assertions — the consequence first (demoted
bytes are not `BEGIN:VTODO`), then the symptom (`kind(o)` is empty). It
reports XFAIL, so ctest stays green, and the day the fix lands both XPASS
and force the slot's `QEXPECT_FAIL`s to be removed. The slot must be
*repaired*, never deleted.

**Recommended owner: IP.3.** It already works the catalogue/envelope seam,
it gates IP.4–IP.7 (so the fix lands before anything builds on merged
records), and its "structural, not a copy pass" framing fits: the real
question is whether `stampEnvelope`'s erase-on-restamp behaviour should
exist at all, or whether a `restampPreservingKind` / kind-carrying overload
is the right shape. **This is a plan amendment and it belongs to the
campaign owner, not to this receipt** — recorded here as a recommendation
per PLAN.md's "corrections to this plan go in receipts."

## 4. Corner cases declared-not-executed

- **Did not probe whether the engine's conflict path can actually produce a
  merged VTODO in a real CalDAV sync** (O84's practical frequency). The
  merger is the live one (`calendardomaindefinition.cpp:38` builds it with
  `calendarCanonPropertyIds()`), and the demote consequence is proven at
  the stage level; establishing the end-to-end frequency needs an engine
  integration test, which belongs to whoever fixes it, not to the report.
- **Did not touch `allDay`'s possibly-vestigial top-level catalogue
  entry** — explicitly IP.3's question (PLAN.md IP.3 acceptance), and IP.1
  deferred it the same way.
- **Did not consider adding the three keys to `contactscanonproperties.cpp`.**
  They are VTODO-emitter keys; contacts has no VTODO leg. `providerExtrasDigest`
  *will* reach contacts, but under IP.5 and via IP.3's contributor
  mechanism — not hand-added here, which would be exactly the anti-pattern
  IP.3 exists to remove.
- **Did not re-verify the four environmental Radicale/KDAV failures against
  a clean Radicale state.** Out of scope per PLAN.md §2; they reproduce
  identically to the IP.1 and `fc1ae61` baselines.

## 5. Corrections to PLAN.md's IP.2 section

**None to the substance.** The item played out exactly as written: the
three keys, the declaration-matching requirement, the merger slot, the
"small, low-drama fix" framing, and the honest blast-radius table all held
up. Two notes for the campaign owner:

1. **The acceptance criterion "a new merger slot ... pinning that a
   source-side `seriesSplitOf` change ... survives a merge" is
   under-specified in one respect that matters** — it does not say the slot
   must be built from the real catalogue, and the file's existing
   convention actively pushes the other way. Written the conventional way,
   the slot would have passed before the fix. §1 records the choice; a
   future item writing a "pin the fix" slot in that file should read this
   first.
2. **IP.2 found a worse bug than IP.2 was about.** That is not a criticism
   of the plan — the plan explicitly predicted IP.2 would be small and told
   the item not to oversell it, which was right. But it is evidence for
   PLAN.md's own thesis: the *class* (independent sources of truth about
   one record's shape, with nothing enforcing agreement) is where the
   damage is, and `_canon.kind` is a second instance of that class in a
   place nobody had looked. IP.3's charter should be read as covering it.

## 6. Test evidence

- `tests/calendar/tst_calendar_kind_dispatch.cpp`: 12 slots, **14/14 QTest
  results PASS, 0 XFAIL** (IP.1 landed 13 PASS + 1 XFAIL).
- `tests/shape/tst_canonjson_diff_merge.cpp`: 14 → **18** slots;
  **20 passed, 0 failed**, with 2 XFAIL results inside
  `mergerPreservesIncidenceKind()` (O84, by design).
- Convergence matrix: regenerated, **byte-identical**; `tests/convergence/`
  byte-pin green.
- **Full suite:** `ctest -j$(nproc)` after a full rebuild — **214 tests,
  210 passed, 4 failed**: `tst_backend_signals`,
  `tst_backend_thread_relocation`, `tst_backend_reentrancy_pin`,
  `tst_remotecalendarbackend` — exactly PLAN.md's four known environmental
  Radicale/KDAV slots, unchanged from the IP.1 and `fc1ae61` baselines. No
  new failures.
- **One local build-artifact accident, not a repo issue:** an interrupted
  build left `build/tests/shape/tst_transformation_registry` truncated and
  non-executable, which surfaced as a spurious 5th ctest failure
  (`BAD_COMMAND` / "permission denied", 7.9 MB vs the 13.7 MB its siblings
  link to). Deleting it and re-running the target relinked it clean and it
  passes. Recorded so a future transcript reader does not mistake it for a
  regression; nothing in the repo caused it and nothing was changed to fix
  it.

## 7. Next

**IP.3 — contributed catalogues.** Gated on IP.2 (now satisfied). Carries
two additions from this receipt: the **O84 fix** (§3, with the
whose-kind-wins decision written down) and the `allDay` orphan check that
IP.1 and IP.2 both deferred to it.
