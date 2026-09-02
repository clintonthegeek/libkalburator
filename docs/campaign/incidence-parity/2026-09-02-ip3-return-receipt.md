# Return receipt — IP.3: Contributed catalogues (+ O84 fix + `allDay` orphan check)

**Delivered:** 2026-09-02
**Consumes:** `docs/campaign/incidence-parity/PLAN.md` §1 (execution rules,
binding), the IP.3 body section, Amendment 1 §A.2's IP.3 row (adds the O84
fix and the `allDay` orphan check to scope, gated on IP.8); `STATUS.md`;
FINDINGS.md O84 (full text, including its explicit "decide whose kind
wins" question); the IP.1/IP.2/IP.8 return receipts for convention.
**Scope discipline:** `git status` at commit time —
`src/calendar/{eventcanonfields,journalcanonfields,calendarcanonproperties}.{h,cpp}`,
`src/todo/{vtodocanonfields,todocanonproperties}.{h,cpp}`,
`src/shape/canonjsonmerger.cpp`,
`tests/shape/tst_canonjson_diff_merge.cpp`, `docs/campaign/FINDINGS.md`
(+O92), `STATUS.md`, this receipt. Nothing else.

---

## 0. What landed

**Contributor exports** (Part A of the work order): each canon-fields
module now exports the top-level `PropertyId` set its emitter can produce,
declared next to the emitter in the same `.h`/`.cpp` pair:

- `eventCanonContributedIds()` — `src/calendar/eventcanonfields.{h,cpp}`
- `journalCanonContributedIds()` — `src/calendar/journalcanonfields.{h,cpp}`
- `vtodoCanonContributedIds()` — `src/todo/vtodocanonfields.{h,cpp}`

Each returns a fixed `QList<PropertyId>` built by hand from a careful
top-to-bottom read of the emitter's own promote function (`eventFieldsToCanon`/
`journalFieldsToCanon`/`todoFieldsToCanon`) — not derived from anything
else, so it is the actual single source of truth PLAN.md asked for. All
three exclude envelope keys (`_canon`/`uid`/`providerExtras`), matching the
existing `calendarCanonPropertyIds()`/`todoCanonPropertyIds()` convention.

**Catalogue refactor** (also Part A): `makeCalendarCanonCatalogue()`
(`src/calendar/calendarcanonproperties.cpp`) and `makeTodoCanonCatalogue()`
(`src/todo/todocanonproperties.cpp`) no longer hand-list property ids.
Each now:

1. Keeps a local, anonymous-namespace `QHash<PropertyId, {PropertyKind,
   displayName}>` metadata table — unchanged *content* from the previous
   `addProperty(...)` calls, just reorganised into a lookup keyed by id
   instead of a flat sequence of calls. This is where PLAN.md's "metadata
   stays in the catalogue files" requirement is satisfied.
2. Computes the catalogue's id set as the union of the relevant
   contributor exports (calendar: event + vtodo + journal; todo: vtodo
   only) plus a local vendor-only id list, order-preserving via a
   `QList` + `QSet`-backed "insert if not seen" helper (not a bare
   `QSet`, so the emitted catalogue order stays deterministic — see §4 on
   why this doesn't matter for the byte-pinned matrix but is good hygiene
   anyway).
3. For each id in that union, looks up its metadata; if found,
   `addProperty()`s it with that kind/name. If NOT found (a genuinely new
   contributed id with no metadata entry yet), it is still catalogued —
   with a safe generic default (`PropertyKind::Json`, display name = the
   id itself) — rather than silently dropped. This is what makes the
   structural-coverage demonstration in §5 work: a throwaway key added to
   a contributor list needs zero catalogue-file edits to appear in the
   catalogue.

**O84 fix** (Part B): `src/shape/canonjsonmerger.cpp`'s `merge()` now
threads the component kind through the re-stamp. See §2 for the full
decision.

**`allDay` orphan check** (Part C): NOT an orphan. See §3.

**Systematic orphan sweep** (Part D): no orphans found anywhere. See §4.

## 1. Vendor-only key list: PLAN.md was wrong on two entries — corrected, not transcribed

PLAN.md's IP.3 body names the calendar catalogue's vendor-only keys as:
`locations`, `onlineMeeting`, `eventType`, `typedProperties`,
`guestsCan*` (×3), `allowNewTimeProposals`, `hideAttendees`, `locked`,
`privateCopy`, `freeBusyStatus`, `responseRequested`, `descriptionHtml`
— 14 keys.

Verification method: for every key in that list, `grep -n "\"<key>\""`
against `src/calendar/eventcanonfields.cpp`, `src/calendar/journalcanonfields.cpp`,
and `src/todo/vtodocanonfields.cpp`, then hand-read every hit (grep alone
is not enough — see §4 on false positives from substring matches inside
unrelated string *values*, e.g. `"related": "end"`).

Two keys are wrong:

- **`descriptionHtml`** — `eventcanonfields.cpp:207` (`obj.insert("descriptionHtml", altDesc)`,
  reading `event->nonKDECustomProperty("X-ALT-DESC")`) and
  `vtodocanonfields.cpp:194` (same pattern) both emit it directly. It is
  real emitter output — a Reversible X-ALT-DESC carrier — not a
  vendor-JSON-only field.
- **`freeBusyStatus`** — `eventcanonfields.cpp:250` (`obj.insert("freeBusyStatus", fbs)`,
  reading `event->nonKDECustomProperty("X-MICROSOFT-CDO-BUSYSTATUS")`).
  Also real emitter output.

Corrected vendor-only list (12 keys, used in
`calendarVendorOnlyIds()`): `locations`, `onlineMeeting`, `eventType`,
`typedProperties`, `guestsCanModify`, `guestsCanInviteOthers`,
`guestsCanSeeOtherGuests`, `allowNewTimeProposals`, `hideAttendees`,
`locked`, `privateCopy`, `responseRequested`. Verified each has ZERO hits
in all three base emitters (grep + hand-read).

**No behavior change from this correction** — the catalogue's actual id
SET is identical either way (both keys were already in the catalogue
before this item, just declared via the old hand-listed `addProperty`
calls). This only moves which contributor "owns" the two ids: they now
arrive via `eventCanonContributedIds()`/`vtodoCanonContributedIds()`
instead of the vendor-only list, which is the more honest place for them
given IP.3's whole premise (declare an id where it is actually produced).

PLAN.md's todo-domain vendor-only list (`sortOrder`, `parentUid`,
`checklistItems`, `linkedResources`) was verified correct as written: zero
hits in `vtodocanonfields.cpp`'s promote path (`todoFieldsToCanon`); all
four are only written by `googletaskcanonstages.cpp`/
`mstodotaskcanonstages.cpp` and only *consumed* (not produced) by
`canonObjectToVtodoBytes` on demote.

## 2. The O84 decision: whose kind wins, and why

Two cases, handled deliberately differently, both in
`CanonJsonMerger::merge()`:

**Easy case — agreement, or only one side carries a kind.** Target's kind
wins if present, else source's. This deliberately mirrors the function's
own `mergedUid` computation two lines above it (`CanonEnvelope::uid(t)`
if non-empty, else `CanonEnvelope::uid(s)`) — same target-preferred,
source-fallback shape, for the same reason: target is the record being
written back to, so its identity-adjacent metadata wins by default when
there's no conflict to resolve.

**Disagreement case — both sides carry a non-empty kind and it differs.**
This is not an ordinary merge choice. The same uid promoted to two
different iCalendar component types (a VEVENT on one side, a VTODO on the
other) means something upstream is already wrong — the exact class of
problem O55 named "identity conflicts" and treated as fail-loud
(`EngineDiff::identityConflicts` fails the mapping outright rather than
silently resolving a churn signature).

I considered building the same kind of fail-loud here and decided
against it, for a structural reason rather than a convenience one:
`RecordMerger::merge()` (`src/shape/recordmerger.h`) returns
`CanonicalRecord` **unconditionally** — there is no error/Result channel
for a merger to report "I cannot safely produce a merged record" back
through the engine's merge stage. Building a genuine abort-the-sync
fail-loud here would mean changing the `RecordMerger` interface itself
(touching `CanonJsonMerger`, `RecordMergerVCard`, and the blob/text/
outline mergers, plus whatever calls `merge()` in the engine's merge
stage) — a change with a blast radius well outside "the catalogue/envelope
seam" IP.3 is scoped to touch.

So the decision actually implemented: **loud, not silent — a documented
precedence rule, not a real abort.** `CanonJsonMerger::merge()` calls
`qWarning()` with the uid, domain, and both differing kinds when it
detects the disagreement, then keeps **target's** kind — consistent with
the function's pre-existing target-primary bias (`QJsonObject out = t;`
at the top of the function). This satisfies the EEE doctrine's "loud
about limits" clause (referenced in this repo's `CLAUDE.md`) without
claiming a fail-loud guarantee the current interface cannot deliver.

The gap this leaves — no way to actually *fail* the sync mapping on a
kind mismatch, only to log and tie-break — is filed as **O92** in
FINDINGS.md, not built here, per the "no fix while passing through"
prohibition (this is a scope boundary I hit while resolving O84, not an
unrelated bug I stumbled on, but the fix genuinely requires an interface
change bigger than IP.3's stated scope, so it gets the same treatment).

`mergerPreservesIncidenceKind()` in `tests/shape/tst_canonjson_diff_merge.cpp`
now passes with both assertions real (no `QEXPECT_FAIL`, confirmed by
running the binary directly and reading `Totals: 22 passed, 0 failed`
with no `XFAIL`/`XPASS` lines — see §6). Two new slots:
`mergerPreservesIncidenceKindWhenOnlySourceHasOne()` pins the easy case
when only one side has a kind at all (a first-sync target with no
envelope kind yet, or a domain that never dispatches on kind at record
-creation time); `mergerKindDisagreementKeepsTargetKindDeliberately()`
pins the deliberate precedence rule itself (source=vevent,
target=vtodo, merged result keeps vtodo) so it cannot silently drift to
source-wins or an unannounced default in a future edit. Ran the binary
directly to confirm the `qWarning()` actually fires with the expected
text on that slot (see §6).

## 3. The `allDay` orphan check: NOT an orphan — the plan's suspicion was wrong

PLAN.md's IP.3 body and Amendment 1 both flag top-level `allDay` in
`calendarcanonproperties.cpp` as a likely orphan, reasoning that "the
emitters write `allDay` inside the `start`/`due` time objects
(`vtodocanonfields.cpp:43-50`)".

That's true for the VTODO leg specifically, but I read (not just
grepped) all three base emitters end to end and found PLAN.md's
generalisation to "the emitters" (plural, all three) is wrong:

- `eventcanonfields.cpp` (`eventFieldsToCanon`): inside the start/end
  block, `obj.insert(QStringLiteral("allDay"), allDay);` at the TOP
  level, right alongside `obj.insert("start", startObj)`. Demote
  (`canonObjectToEventBytes`) reads it back: `const bool allDay =
  obj.value(QStringLiteral("allDay")).toBool();` and calls
  `event->setAllDay(allDay)`.
- `journalcanonfields.cpp` (`journalFieldsToCanon`): same pattern —
  `obj.insert(QStringLiteral("allDay"), true)` at the top level inside
  the date-only branch of the start construction. Demote
  (`canonObjectToJournalBytes`) reads `obj.value("allDay").toBool()` and
  calls `journal->setAllDay(true)`.
- `vtodocanonfields.cpp` (`todoFieldsToCanon`/`canonObjectToVtodoBytes`):
  the ONLY one of the three that never touches a top-level `allDay` key —
  `allDay` only appears inside the `dateTimeToJson()` helper's returned
  sub-object (`{"date": ..., "allDay": true}`), and demote derives
  `setAllDay()` from whether `start`/`due` contain a `"date"` key, not
  from any top-level `allDay` value.

So top-level `allDay` is real, load-bearing production+consumption code
for VEVENT and VJOURNAL, both directions. Left in place. Verification
method: full read of all three emitter files (not grep-only — grep alone
would have shown `allDay` "hits" in all three files, which is exactly the
false-positive trap; the substantive check is whether the hit is a
top-level `obj.insert`/`obj.value` call or embedded inside a nested
object literal). Noted as a genuine surprise — PLAN.md's own text was
right about the mechanism (VTODO's `allDay` really is nested) but wrong
to generalise it to VEVENT/VJOURNAL, which had never been checked before
this item.

`allDay` is declared once, as `Boolean`, in `calendarPropertyMetadata()`
and reached via `eventCanonContributedIds()` + `journalCanonContributedIds()`
(not `vtodoCanonContributedIds()`, correctly — VTODO never contributes
it).

## 4. Systematic orphan sweep (Part D) — none found

Method: for each of the 43 non-`uid` keys in `calendarcanonproperties.cpp`
and the 26 non-`uid` keys in `todocanonproperties.cpp`, grepped every
occurrence of `"<key>"` (with quotes, to anchor on the JSON string
literal) across `eventcanonfields.cpp`, `journalcanonfields.cpp`,
`vtodocanonfields.cpp`, `mseventcanonstages.cpp`, `googlecanonstages.cpp`,
`googletaskcanonstages.cpp`, `mstodotaskcanonstages.cpp`, then hand-read
every hit rather than trusting the hit count.

**Two false positives caught by hand-reading, both would have produced a
wrong conclusion from grep alone:**

- `"end"` shows 2 hits in `vtodocanonfields.cpp` — both are the string
  *value* `"end"` for an alarm's `"related"` key (`a.insert("related",
  "end")`), not a top-level `obj.insert("end", ...)`. Confirms VTODO
  genuinely never produces a top-level `end` key (it uses `due` instead).
- `"allDay"` shows 2 hits in `vtodocanonfields.cpp` — both inside the
  `dateTimeToJson()` helper's nested object construction (see §3), not a
  top-level insert.

**Result:** every one of the 43 + 26 keys is accounted for by exactly one
of {the relevant emitter contributor(s), the domain's vendor-only list}.
No catalogue key has zero producers. Full accounting kept in this
session's scratch notes; the short version, by category:

- Keys produced by exactly one emitter and declared once: e.g.
  `timeTransparency`, `organizer`, `attendees`, `attachments` (event
  only); `completed`, `percentComplete`, `geo`, `relatedTo`,
  `completionAnchor`, `seriesSplitOf`, `providerExtrasDigest` (todo
  only — O83's known VTODO-only additions).
- Keys produced by two of the three calendar-domain emitters (declared
  once, since both agree on kind/name): `sequence`, `classification`,
  `color`, `url` (event + journal — O83 already documents these as
  VTODO's known gaps); `descriptionHtml`, `location`, `priority`,
  `alarms`, `recurrence`, `recurrenceId`, `recurrenceRange` (event +
  todo).
- Keys produced by all three: `created`, `lastModified`, `summary`,
  `description`, `categories`, `start`, `status`.
- The 12 corrected vendor-only calendar keys (§1) and the 4 vendor-only
  todo keys (`sortOrder`, `parentUid`, `checklistItems`,
  `linkedResources`).

The absent-from-VTODO keys above (`classification`/`color`/`url`/
`organizer`/`attendees`/`attachments`/`sequence`) are exactly O83's
already-filed VTODO gaps — NOT new orphans, and NOT this item's to fix
(IP.6's `incidencecommonfields` extraction owns them). An orphan would be
a catalogue key with NO producer anywhere; a key some emitters produce
and others don't is just an uneven catalogue, which is what O83/IP.6 are
for.

## 5. Structural-coverage demonstration

Per PLAN.md's acceptance criterion: added `PropertyId{QStringLiteral("ip3ThrowawayDemoKey")}`
to the end of `vtodoCanonContributedIds()`'s returned list ONLY — no edit
to `calendarcanonproperties.cpp` or `todocanonproperties.cpp`.

```
$ cmake --build build -j$(nproc) --target kalburator tst_property_catalogue tst_calendar_kind_dispatch
[...clean build, no errors...]

$ ./ip3_demo   # scratch probe linking the built libkalburator.a directly,
               # calling Kalburator::Calendar::calendarCanonPropertyIds()
               # and Kalburator::Todo::todoCanonPropertyIds()
calendarCanonPropertyIds contains ip3ThrowawayDemoKey: YES
todoCanonPropertyIds contains ip3ThrowawayDemoKey:     YES

$ ./build/tests/calendar/tst_calendar_kind_dispatch
[...]
Totals: 14 passed, 0 failed, 0 skipped, 0 blacklisted, 9ms
```

Both catalogues picked up the throwaway key automatically — confirming
the union mechanism works, and confirming the "unknown id gets a safe
`Json`-default entry rather than being dropped" fallback in §0 point 3
actually fires (there was deliberately no metadata-table entry added for
`ip3ThrowawayDemoKey`). IP.1's gate stayed green with the extra key
present, as expected (a superset addition can only help the
emitted-⊆-catalogued check).

Reverted: `cp` the pre-edit backup of `vtodocanonfields.cpp` back over
the throwaway-key version, confirmed `diff` reports no differences
(byte-identical revert), rebuilt `kalburator` + the four directly
affected test binaries clean.

## 6. Test evidence

- `./build/tests/shape/tst_canonjson_diff_merge` run directly: `Totals:
  22 passed, 0 failed, 0 skipped, 0 blacklisted` — no `XFAIL`/`XPASS`
  lines anywhere in the output. The disagreement slot's `qWarning()`
  fired with the expected text: `CanonJsonMerger::merge: kind mismatch
  for uid "t-6" in domain "calendar" - source kind "vevent" target kind
  "vtodo" - an identity conflict (O84); keeping target's kind`.
- `./build/tests/calendar/tst_calendar_kind_dispatch` run directly:
  `Totals: 14 passed, 0 failed` — `calendarCatalogueDeclaresVeventKeys`/
  `VtodoKeys`/`VjournalKeys` and `todoCatalogueDeclaresVtodoKeys` all
  green (IP.1's gate, unchanged from IP.2's 14/14-green state).
- `ctest --test-dir build -R "tst_calendar_kind_dispatch|tst_canonjson_diff_merge|tst_property_catalogue|tst_incidence_rfc5545_fidelity"`:
  4/4 passed.
- Matrix: `./build/tools/matrixgen/matrixgen` output diffed byte-for-byte
  against the committed `docs/campaign/eee/CONVERGENCE-MATRIX.md` —
  **identical**. `tst_gm_pipeline_convergence` green. Confirmed, not
  assumed, per house rule O63 — this item touches no loss profile and
  grows no `edges()` list, so this was expected, but was checked anyway.
- **Full suite:** two full `cmake --build build -j$(nproc)` +
  `ctest --test-dir build` cycles run (the second after the throwaway-key
  revert, to get a clean final count). Both: **215 tests, 211 passed, 4
  failed.** The 4 failures are the same known-environmental set
  (`tst_backend_signals`, `tst_backend_thread_relocation`,
  `tst_backend_reentrancy_pin`, `tst_remotecalendarbackend`) — verified
  by failure TEXT, not name: all four carry the `RemoteCalendarBackend:
  KDAV job exceeded transfer timeout ( 30000 ms)` signature or its direct
  consequence (`tst_backend_reentrancy_pin`'s "the requested timeout
  (15000 ms) was too short, 29650/29700 ms would have been sufficient
  this time"; `tst_remotecalendarbackend`'s `testStoreMultipleItems`
  `'succeededCount >= 1' returned FALSE`, downstream of the same KDAV
  timeout). Unchanged from the IP.8 baseline (215/211/4) — IP.3 added
  test slots inside an existing ctest binary
  (`tst_canonjson_diff_merge`), not a new ctest executable, so the
  ctest-level test count does not move.
- One incidental, unrelated build-environment issue hit and fixed during
  this session: after the first full build+test cycle, `tst_property_catalogue`
  came back `BAD_COMMAND` ("permission denied"), then "cannot execute
  binary file" after `chmod +x` — `file` reported `data` (no ELF header,
  all-zero first bytes), i.e. a corrupted/truncated linker output, almost
  certainly from an earlier build invocation that got interrupted when a
  600s tool timeout backgrounded it mid-link while a second `cmake
  --build` was issued concurrently. Deleted the stale binary and rebuilt
  just that target (`cmake --build build --target tst_property_catalogue`)
  — came back a valid ELF, test passes. Not a code defect; not filed.

## 7. Corner cases declared-not-executed

- **Vendor stages exporting their own contributed-id lists** (PLAN.md's
  "yes, but not in this item" recommendation) — not built, as instructed.
  Would let `mseventcanonstages`/`googlecanonstages`/the vendor todo
  stages export their own vendor-only ids instead of the hand-written
  `calendarVendorOnlyIds()`/`todoVendorOnlyIds()` lists in this item.
  Left as a stated follow-up; the current hand-written vendor-only lists
  are small (12 and 4 entries), verified against the real vendor stage
  files (§1), and not a hand-maintained *key list masquerading as a
  source of truth* in the sense the campaign's prohibition targets — they
  are the catalogue's own honest declaration of "these ids have no
  emitter, only a vendor stage," which is a fact about the vendor
  stages that would need vendor-stage-side code to assert authoritatively
  instead.
- **O92** (RecordMerger has no error channel for a genuine fail-loud) —
  filed, not built. See §2.
- **Auditing other `RecordMerger::merge()` implementations
  (`RecordMergerVCard` etc.) for the same silent-tie-break shape** — not
  done. O92's text flags this as a question for whoever picks up the
  interface change, not something IP.3 investigated.
- **Loss-profile completeness** (whether every catalogued property has a
  loss-profile entry on every edge) — explicitly out of IP.3's scope
  (that's O88/IP.9 territory: kind-scoped loss profiles). Not audited
  here.

## 8. Next

**IP.9** — kind-scoped loss profiles (closes O88). Gates IP.4/IP.6/IP.10.
