# Return receipt — IP.11: VTODO domain-crossing convergence proof

**Delivered:** 2026-09-03
**Consumes:** `docs/campaign/incidence-parity/PLAN.md` §1 (execution rules,
binding), the IP.11 body section (background/problem statement, superseded
for the actual work order) and **`## B.4 IP.11 rescoped — convergence
proof, not a choice`** (Amendment 2 — binding work order);
`docs/campaign/FINDINGS.md` O89 (the finding this item closes).
**Scope discipline — files touched:** `CLAUDE.md`,
`docs/campaign/FINDINGS.md` (O89 → RESOLVED),
`docs/campaign/incidence-parity/STATUS.md` + this receipt;
`tests/calendar/tst_vtodo_domain_convergence.cpp` (new, the crossing gate);
`tests/calendar/CMakeLists.txt` (one new `kalburator_add_calendar_test`
line); `src/sync/multiprotocoldavprovider.cpp` (one `qCInfo` line + comment
in the `!anyTodoBearing` branch).
**Not touched, deliberately:** `src/calendar/calendarcanonproperties.cpp`
(investigated — no gap found, see §2), `src/calendar/localbackend.cpp`,
`src/calendar/decsyncbackend.cpp`, `src/calendar/orgbackend.cpp`,
`src/calendar/akonadibackend.cpp` (investigated — no log line added, see
§3), any (b)-shaped routing code (explicitly forbidden, see §4).

---

## 0. Summary

Amendment 2 §B.4 rescoped this item from "choose a route for O89" to
"prove the ratified route (a) actually converged, and make the surviving
routing choice loud." PlanStan ratified (a) — converge — on 2026-09-02
(recorded in O89 and PLAN.md Amendment 2 §B.1), and IP.3/IP.6/IP.9 already
did essentially all of the convergence work as a side effect of closing
O78/O83/O84/O88/O91. This item's actual job was threefold, all three done:

1. **Crossing gate** — proves the two representations are equivalent.
   `tests/calendar/tst_vtodo_domain_convergence.cpp`, two slots,
   registered in `tests/calendar/CMakeLists.txt`.
2. **Catalogue divergence** — investigated the four still-divergent keys
   named in Amendment 2 §B.4 point 2 and concluded, with evidence, that
   they are legitimate, not an IP.3 gap. No catalogue edit made.
3. **Loud fallback** — `MultiProtocolDavProvider`'s legacy-shape branch now
   logs. The four non-DAV backends were investigated and deliberately left
   unchanged (argued in §3).

**(b) route: not implemented, no hooks left.** Verified in the diff below —
no new domain-routing branch anywhere, no `CalendarType::Hybrid`-adjacent
code, nothing commented out as a future alternative.

## 1. The crossing gate

### 1.1 Why a new file, not a new slot in `tst_gm_pipeline_convergence.cpp`

That file's established `reportAndAssertWithin()` pattern (used by every
existing crossing gate — `eventCrossing*`, `contactCrossing*`,
`todoCrossingsStayDeclared`) has a specific shape: demote a canon record
through vendor B, re-promote it, and check that everything which changed
is DECLARED in vendor B's OWN demote `LossProfile`. That shape assumes
exactly one "governing" loss profile for the round trip.

IP.11's question has a different shape: promote the SAME source through
TWO INDEPENDENT paths and compare the two RESULTS against EACH OTHER, not
against either path's demote loss profile (there is no demote in this
gate at all — both stages are promotes). There is no single `LossProfile`
that could govern "how {calendar,canon} and {todo,canon} differ" — the
two legitimate divergences (the envelope, and the four vendor-only keys)
are asserted explicitly instead, by name, rather than looked up in a
profile. Forcing this into `reportAndAssertWithin()`'s signature
(`before, after, LossProfile`) would have meant either fabricating a
`LossProfile` that doesn't correspond to a real transformation edge, or
silently comparing against the wrong one. A new file, reusing
`tst_incidence_rfc5545_fidelity.cpp`'s maximal-RFC-5545-VTODO fixture
discipline (IP.8) rather than `tst_gm_pipeline_convergence.cpp`'s
hand-built-superset-canon discipline, was the better fit — this question
is fundamentally "does the iCal wire form survive two different domain
wrappings identically," which is closer to IP.8's "does the wire form
survive one domain's own round trip" than to a vendor-crossing question.

### 1.2 What it proves

`maximalVtodoConvergesAcrossDomains()`: builds a maximal RFC 5545 §3.6.2
VTODO (every property the grammar permits — SEQUENCE, CLASS, PRIORITY,
STATUS, PERCENT-COMPLETE, DTSTART/DUE, RRULE/RDATE/EXDATE, CATEGORIES, URL,
COLOR, GEO, RELATED-TO, ORGANIZER, ATTENDEE, ATTACH, COMMENT, CONTACT,
RESOURCES, REQUEST-STATUS, VALARM, an X- custom property — the same list
IP.8's `kVtodoMaster` used, reproduced here rather than shared across
translation units per this codebase's one-file-per-QTest-binary
convention), promotes it through:

- `Kalburator::Calendar::ICalToCanonStage` (the `{calendar,ical}` →
  `{calendar,canon}` edge, kind-dispatched to `vtodo`), and
- `Kalburator::Todo::VTodoToCanonStage` (the `{todo,ical-vtodo}` →
  `{todo,canon}` edge),

then diffs the two resulting canon objects field by field (a real
key-by-key, value-by-value comparison — `QJsonValue::operator==`, which is
recursive and order-independent — not a shallow key-set check), explicitly
excluding only `_canon` (asserted separately) from the diff.

**Result: zero diffs outside `_canon`, including `uid` and
`providerExtras`.** This is not a coincidence to be relieved about — it is
the expected, provable consequence of a fact both PLAN.md and O89 already
stated: `icalcanonstages.cpp:56` and `vtodocanonstages.cpp`'s
`VTodoToCanonStage::transform` both call the exact same
`Kalburator::Todo::todoFieldsToCanon(todo, icalBytes)` on the exact same
parsed `KCalendarCore::Todo` and the exact same raw bytes. The only two
call-site differences are the envelope stamp (`domain` +, on the calendar
leg only, `kind`) — which the test asserts explicitly, both ways, so a
future regression that makes the envelope ACCIDENTALLY converge too (e.g.
a kind leaking into the todo domain, or the calendar leg silently
forgetting to tag `kind`) would also be caught. **The crossing gate
therefore genuinely proves what Amendment 2 §B.4 point 1 asked for:
equivalent canon, modulo an explicitly-checked envelope difference — not
vacuously, since it is a real key-by-key comparison over a maximal fixture
that would show ANY of the O83/O86/O91/O94 -era per-property drops if they
were still asymmetric between the two legs.**

### 1.3 Non-vacuity

Per the campaign's standing verification convention (IP.1/IP.2/IP.3/IP.7):
temporarily inserted a throwaway key
(`obj.insert("ip11NonVacuityProbe", "x")`) into
`VTodoToCanonStage::transform()` only, rebuilt, reran. Result: a real
`FAIL!` naming exactly `ip11NonVacuityProbe` as the offending diff key —

```
QINFO  : ...maximalVtodoConvergesAcrossDomains: diff ip11NonVacuityProbe
FAIL!  : ...'diffs.isEmpty()' returned FALSE. (calendar-domain and
          todo-domain canon diverge outside the envelope ...)
```

— confirming the diff mechanism actually inspects both objects and is not
silently short-circuiting. Reverted (`cp` from a pre-edit backup, `diff`
confirmed byte-identical), rebuilt clean, reconfirmed both slots green.

## 2. The four divergent catalogue keys — investigated, found legitimate

Amendment 2 §B.4 point 2 named `checklistItems`, `linkedResources`,
`parentUid`, `sortOrder` as catalogued in `todocanonproperties.cpp` and
absent from `calendarcanonproperties.cpp`, instructing: fix it if IP.3's
contributor mechanism has a gap, otherwise state precisely why the
divergence is legitimate.

**Conclusion: legitimate. No IP.3 gap. No catalogue edit made.**

Evidence, not assertion:

1. **Grep across every `src/calendar/*.cpp` file that could conceivably
   populate these keys** — `eventcanonfields.cpp`, `journalcanonfields.cpp`,
   `googlecanonstages.cpp`, `mseventcanonstages.cpp` — finds **zero**
   occurrences of any of the four names. The calendar domain's own
   emitters, iCal AND vendor JSON alike, never reference task
   hierarchy/checklist/sort-order concepts at all; Google Calendar events
   and MS Graph events have no such fields on the wire to promote from in
   the first place.
2. **`vtodocanonfields.cpp` itself already carries a dated comment
   explaining this**, from IP.3 (2026-09-02): `vtodoCanonContributedIds()`
   deliberately omits all four, with the note that `todoFieldsToCanon`
   (the SAME function both domains call) never produces them at the top
   level — they arrive only via the Google Tasks / MS To-Do vendor JSON
   stages and are only ever *consumed* (never produced) by
   `canonObjectToVtodoBytes` on demote, as best-effort `X-CANON-*`
   carriers that are not read back on re-promote (verified: grepped
   `todoFieldsToCanon`'s body for `X-CANON-CHECKLISTITEMS`/
   `X-CANON-SORTORDER` — zero hits; only the demote side writes them).
3. **`calendarcanonproperties.cpp`'s own `calendarVendorOnlyIds()` comment
   already states the general principle** ("no {event,todo,journal}
   emitter produces" these) and its list is 12 EVENT-vendor-only keys
   (`locations`, `onlineMeeting`, `eventType`, ...) — the four TASK-vendor
   keys were never candidates for that list because no calendar-domain
   emitter, vendor or iCal, ever touches them, confirmed by (1).
4. **Positive confirmation of where the keys DO come from**:
   `googletaskcanonstages.cpp:119-127` maps a Google Tasks JSON payload's
   `"parent"`/`"position"` wire fields to canon's `parentUid`/`sortOrder`.
   This is the `{todo,google-task}` → `{todo,canon}` edge — a peer shape
   that exists **only** under the todo domain
   (`TodoStockShapes::peerShapes()`); `CalendarStockShapes::peerShapes()`
   (`calendarstockshapes.cpp`) names exactly `ical`/`org-ical`/
   `google-event`/`ms-event` and nothing else.
5. **The crossing gate itself demonstrates this is not merely a static
   claim**: since §1.2 proves the two iCal-based promote paths are
   byte-identical for a maximal fixture, and neither produces these four
   keys (verified directly, `vendorOnlyKeysHaveNoCalendarDomainCounterpart()`
   part (a)), there is no scenario in which "the same VTODO crossed both
   domains" could ever diverge on these fields — their origin never
   touches iCal at all, on either leg. Part (b) of the same slot promotes
   a real Google Tasks JSON payload and confirms `parentUid`/`sortOrder`
   DO appear in `{todo,canon}` from that leg, and part (c) confirms
   structurally (iterating the real, registered
   `CalendarStockShapes::peerShapes()`) that no `google-task`/
   `ms-todotask` peer shape exists under the calendar domain to compare
   against.

This is exactly the "modulo the vendor-only keys that genuinely have no
iCal representation" exception PLAN.md's Amendment 2 §B.4 acceptance
criteria anticipated — stated explicitly rather than left implicit, per
its own instruction, and it is a **structural** exception (the vendor legs
that populate these fields never ride the calendar domain, full stop), not
merely "nobody happened to wire it yet."

## 3. Making the silent fallback loud

### 3.1 `MultiProtocolDavProvider` — the actual runtime fallback

`src/sync/multiprotocoldavprovider.cpp`'s `createBackends()`, in the
`!anyTodoBearing` branch (the "legacy shape" — one unfiltered
`RemoteCalendarBackend` for every calendar, taken when no calendar in the
account advertised `VTODO`, including the case where the server never
reported `contentTypes` at all): added a `qCInfo(lcMultiDav)` line, using
the SAME logging category and `.nospace()`/multi-line-literal style
already established at lines ~110 and ~408-417 in this file (no new
category invented), naming that any VTODO synced through this branch
rides `{calendar,canon}`, not `{todo,canon}`. Comment above it cross-
references the crossing gate this item added, so a future reader who
finds the log line and wonders "does that still matter" lands on the
answer (it's observability, not a functional gap, since IP.11).

Verified this compiles cleanly and the branch's existing dedicated test
(`tests/sync/tst_multiprotocoldavprovider.cpp`) still passes unchanged —
purely additive logging, no behavior change (confirmed: `spec`,
`backend`, `primed` construction all untouched, only a `qCInfo` call
inserted before them).

### 3.2 The four non-DAV backends — investigated, deliberately NOT given a log line

`LocalBackend`, `DecSyncBackend`, `OrgBackend`, `AkonadiBackend` — read all
four `nativeShapes()` implementations in full
(`localbackend.cpp:149-154`, `decsyncbackend.cpp:24-29`,
`orgbackend.cpp:21-26`, `akonadibackend.cpp:72-77`). All four are
byte-identical one-line returns:

```cpp
QList<Kalburator::Shape::Shape> XBackend::nativeShapes() const
{
    return { Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("calendar")},
        Kalburator::Shape::EncodingId{QStringLiteral("ical")} } };
}
```

No branch, no server-metadata check, no state read — a fixed, compile-time
declaration. This is a structurally different situation from
`MultiProtocolDavProvider`'s branch, and the difference matters for
whether "loud about limits" applies:

- **The DAV provider's branch is a genuine runtime FALLBACK**: the SAME
  backend type, talking to a DIFFERENT server, would take the OTHER branch
  (the demuxed shape) if that server had advertised `VTODO`. Something
  discoverable was checked and found absent, and a decision was made as a
  consequence. That decision is exactly what "loud about limits" asks to
  surface — a human debugging a specific account benefits from being told
  "this collection didn't get the good path because of X."
- **The four non-DAV backends make no such decision.** There is no
  configuration, no server response, no account state under which
  `LocalBackend::nativeShapes()` would ever return anything else — it is
  not "currently falling back," it structurally cannot do otherwise. There
  is nothing to discover and nothing to log AT THE POINT OF DISCOVERY,
  because there is no such point; the fact is a permanent property of the
  backend class, already documented in O89's own text ("never demux under
  any configuration") and in this campaign's CLAUDE.md section.
- **A log line in `nativeShapes()` would also be the wrong SHAPE of log**:
  this accessor is a small, frequently-queried identity method (called
  wherever the engine or a shape-graph consumer asks "what can this
  backend produce"), not a one-time collection-setup event the way
  `createBackends()` is. Logging on every call would be noise; logging
  once (e.g. in the constructor) would require inventing a NEW logging
  category in four files that currently have none at all (confirmed by
  grep — no `Q_LOGGING_CATEGORY` in any of the four), for an event that,
  unlike the DAV case, is true of every single instance unconditionally
  and was already known and documented before this item started.

**Judgment call: "loud about limits" is about a decision that could have
gone the other way and was made silently. These four backends have no
such decision to surface — surfacing "this backend, as documented, only
ever does the thing it always does" is not what the doctrine clause is
protecting against, and would train users/support staff to ignore log
lines from this category as boilerplate.** No code change made to any of
the four files. If a future item disagrees, the natural place is each
backend's constructor, following the SAME `lcMultiDav`-style category
convention — but establishing four new logging categories (`orgbackend.cpp`
already uses bare `qDebug()`/`qWarning()` with no category at all) was
judged out of proportion to what this item's scope actually asked for.

## 4. (b)-route scaffolding — verified absent

Re-read the full diff before finalizing this receipt. No new domain-id
handling, no second `ProviderBackendSpec` path added anywhere, no
`CalendarType`-adjacent code, no commented-out alternative implementation,
no `// TODO: could route here` marker. The three changed/added files are:
a new self-contained test file, one `CMakeLists.txt` registration line,
and one `qCInfo` + comment block in an existing branch. Nothing added
here could be mistaken for, or repurposed into, (b) routing.

## 5. Matrix and byte-pin

No `LossProfile` or `TransformationEdge` was added, removed or changed by
this item (the crossing gate calls existing stages; the log line has no
effect on any transformation). Per house rule O63, matrix regeneration is
only required when a loss profile or edge changes — neither did here, so
`./build/tools/matrixgen/matrixgen` was NOT re-run; confirmed correct by
inspecting the diff (no `src/**/lossprofile`-touching file, no
`edges()`/`peerShapes()` change). `tst_gm_pipeline_convergence`'s
`committedMatrixMatchesGenerated` slot is included in the full suite run
below and stays green, confirming this by execution as well as by
reasoning.

## 6. Full suite

Full build (`cmake --build build -j8`) clean, no errors (only pre-existing
`QDateTime`/`QDomDocument` deprecation warnings from files this item did
not touch).

`ctest --output-on-failure` (full, unfiltered): **216 tests, 212 passed, 4
known-environmental failed** (`tst_backend_signals`,
`tst_backend_thread_relocation`, `tst_backend_reentrancy_pin`,
`tst_remotecalendarbackend`) — same four as baseline, verified by failure
TEXT not name (KDAV 30s-transfer-timeout / local-Radicale 412/409 class).
New test executable count: verified directly via `ctest -N` — **215 →
216 (+1)**, matching CLAUDE.md's standing "215 tests" baseline exactly
(`tst_vtodo_domain_convergence` — a genuinely new binary, unlike most
recent items which added slots to an existing one). Note: a few earlier
session-log entries elsewhere in `STATUS.md` (IP.8's, IP.9's) cite "227"/
"228"-shaped figures that do not match a direct `ctest -N` count taken at
the start of this item; not reconciled here, out of this item's scope.

`tst_vtodo_domain_convergence` itself: 2/2 QTest slots passed (4/4
including `initTestCase`/`cleanupTestCase`). `tst_multiprotocoldavprovider`
(the DAV provider's own dedicated suite, covering the branch this item's
log line touches): reconfirmed green independently before the full run.

## 7. Corner cases declared-not-executed

- **Did not build a second crossing-gate fixture with a detached
  RECURRENCE-ID exception instance.** IP.11's deliverable is specifically
  about domain-crossing equivalence, not RFC 5545 coverage breadth (that's
  IP.8's charter, already closed and reused here for the fixture itself).
  Since both promote paths call the identical function on identical bytes
  (§1.2's structural argument), a second fixture would exercise the same
  equality property through different code paths inside
  `todoFieldsToCanon` (the recurrenceId branch) without testing anything
  new about the CROSSING itself — judged low-value for this item's actual
  question, though it would be a reasonable IP.8-style addition if a
  future item wants broader coverage.
- **Did not add a demonstration of MS To-Do's `checklistItems`
  specifically** (only Google Tasks' `parentUid`/`sortOrder` was
  exercised in §1's slot (b)). `mstodotaskcanonstages.cpp`'s own comment
  (line ~621) notes `checklistItems`/`linkedResources` are
  "separate-endpoint" (a second API call, not present on the base
  `todoTask` resource) — exercising that would need a materially
  different, more elaborate fixture for a fact already established by the
  Google Tasks demonstration (a vendor-JSON-only origin with no
  calendar-domain counterpart). Judged sufficient; not built.
- **Did not investigate whether a hand-crafted `{calendar,canon}` VTODO
  record COULD be given one of the four vendor-only keys by, e.g., a
  future `CanonJsonMerger` merge pulling from a `{todo,canon}` sibling
  record.** Out of scope — no such merge path exists today (calendar and
  todo are separate `CanonJsonDiffer`/merger domains entirely, per the
  campaign's own scope boundary), and inventing one to test against would
  be building a fixture from a hypothetical, not from what the library
  actually does — the house rule this campaign holds itself to.
