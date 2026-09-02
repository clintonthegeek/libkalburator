# Return receipt — IP.9: Kind-scoped loss profiles

**Delivered:** 2026-09-02
**Consumes:** `docs/campaign/incidence-parity/PLAN.md` §1 (execution
rules, binding), the IP.9 body section (design options (a)/(b)/(c) +
acceptance criteria); Amendment 1 §A.2's IP.9 row (context only — the
order table there is stale, `STATUS.md` is authoritative); FINDINGS.md
O88 (the defect), O83/O86/O87/O91 (the drops this item declares), O78/O84
(background on `_canon.kind`'s single write/read site); the IP.8 return
receipt (`2026-09-02-ip8-return-receipt.md`, for the allow-list this item
wires up) and the IP.3 return receipt (most recent, for convention).
**Scope discipline:** `git status` at commit time —
`src/shape/{transformationedge,pipeline}.{h,cpp}`,
`src/calendar/{icalcanonstages,journalcanonfields,calendarstockshapes}.{h,cpp}`,
`src/engine/syncengine.cpp`, `src/shape/convergencematrix.h`,
`docs/campaign/eee/CONVERGENCE-MATRIX.md`,
`tests/calendar/{tst_incidence_rfc5545_fidelity,tst_calendar_kind_dispatch}.cpp`,
`docs/campaign/FINDINGS.md` (+O93), `STATUS.md`, this receipt. Nothing
else — in particular, no emitter body
(`eventcanonfields.cpp`/`journalcanonfields.cpp`'s emitter/`vtodocanonfields.cpp`)
changed, per PLAN.md's "declare, don't fix" instruction for this item.

---

## 0. What landed

**The design mismatch (O88), restated:** `CalendarStockShapes::edges()`
registered exactly ONE `{calendar,canon}→{calendar,ical}`
`TransformationEdge` carrying ONE `LossProfile` (`canonToIcalLoss()`,
entirely event-shaped: `onlineMeeting`, `guestsCan*`, ...), even though
`CanonToICalStage::transform()` kind-dispatches to three different
emitters. `materializedLoss()` ran the event profile over every VTODO and
VJOURNAL regardless of kind; `canonToVjournalLoss()` existed, was
declared, returned an empty profile, and had zero call sites.

**Fixed by:**

1. `TransformationEdge` gains `QHash<QString, LossProfile> lossByKind` +
   a `lossFor(const QString& kind) const` accessor
   (`src/shape/transformationedge.h`). `loss` remains the profile for the
   domain's default/untagged kind (empty string ⇒ "vevent" for calendar,
   matching `icalcanonstages.cpp`'s own existing convention of omitting
   the kind key for VEVENT); a kind found in `lossByKind` OVERRIDES `loss`
   outright for that kind — the two are never merged.
2. `Pipeline::composedLoss()` gains an optional `kind` parameter
   (default empty, exactly reproducing pre-IP.9 behaviour for every edge
   that never populates `lossByKind`) and folds `edge.lossFor(kind)`
   instead of `edge.loss` (`src/shape/pipeline.{h,cpp}`).
3. `SyncEngine::materializedLoss()` (`src/engine/syncengine.cpp`) now
   parses `canonData` FIRST (it needs to for the presence check anyway),
   reads `_canon.kind` via `CanonEnvelope::kind()`, and calls
   `pipe.composedLoss(kind)`.
4. Two new/repopulated per-kind profiles:
   - `Kalburator::Calendar::canonToVtodoIcalLoss()` — NEW, declared next
     to `canonToIcalLoss()` in `icalcanonstages.{h,cpp}` (deliberately
     NOT in `vtodocanonfields.{h,cpp}` — see §2 on the naming collision
     with `Kalburator::Todo::canonToVtodoLoss()`).
   - `Kalburator::Calendar::canonToVjournalLoss()` — REPOPULATED in place
     (`journalcanonfields.{h,cpp}`), same declaration, no longer dead.
5. `CalendarStockShapes::edges()` (`calendarstockshapes.cpp`) builds the
   `{calendar,canon}→{calendar,ical}` edge as a named local
   (`canonToIcalEdge`), sets `.loss = canonToIcalLoss()` as before, then
   populates `.lossByKind["vtodo"]` / `["vjournal"]`.
6. `ConvergenceMatrix::generate()` (`src/shape/convergencematrix.h`)
   renders one subsection per kind for an edge whose `lossByKind` is
   non-empty (`### canon → ical (default)`, `(vtodo)`, `(vjournal)`),
   plus a `(kind-scoped: default, vjournal, vtodo)` annotation on the edge
   inventory line. Every other edge (no `lossByKind`) renders exactly as
   before.
7. `tests/calendar/tst_incidence_rfc5545_fidelity.cpp`'s
   `expectedLossTable()` — vtodo/vjournal entries now DERIVED from the
   real profiles via a new `droppedRfcNames()` translation helper; vevent
   stays literal, deliberately (see §5).
8. `tests/calendar/tst_calendar_kind_dispatch.cpp` — three new slots
   pinning the full declared content of both new profiles against the
   REAL registered `CalendarStockShapes` graph, plus a vevent regression
   guard.

## 1. Design decision: (b), and why (a) is not expressible today

PLAN.md asked to verify, not assume, whether the registry could express
option (a) (three separate `canon→ical` edges, discriminated by kind).
Read `src/shape/transformationregistry.{h,cpp}` before choosing:

- `m_edgesFrom` is `QMultiHash<Shape, TransformationEdge>`, but
  `findEdge(a, b)` (`transformationregistry.cpp`) returns the FIRST
  (and, by the invariant below, only) edge whose `.to == b` — there is no
  notion of "the edge for (a,b) discriminated by kind" anywhere in the
  lookup path.
- `registerEdge()` (`:101-129`) explicitly enforces at most one edge per
  `(from, to)` pair: `if (const auto* existing = findEdge(edge.from,
  edge.to))` then `Q_ASSERT_X(sameAffected, ..., "conflicting
  re-registration of (from, to) edge")` — a SECOND `{calendar,canon}→
  {calendar,ical}` edge with a different loss profile would either be
  silently treated as identical (if `loss.affected` happened to compare
  equal, which it never would across three genuinely different profiles)
  or trip this assertion. There is no kind parameter anywhere in
  `registerEdge`'s signature to disambiguate.
- `compileImpl()` (`:137+`) walks the domain's canonical spine and picks
  edges purely by `(Shape, Shape)` — again no kind axis.

So (a) is confirmed NOT expressible without a real interface change to
`TransformationRegistry` (a new key dimension threaded through
`registerEdge`/`findEdge`/`compileImpl`/`m_edgesFrom`, touching every
domain's edge registration, not just calendar's) — exactly PLAN.md's own
suspicion, now verified by reading the code rather than assumed.

(c) — loss as a function of the record — was rejected for the reason
PLAN.md itself gives: `ConvergenceMatrix::generate()` renders loss
profiles from a STATIC `edges()` list with no record in hand; a
record-as-input function has nothing to render without inventing a
synthetic "representative record" per kind, which is just (b) with extra
indirection. (b) was chosen: smallest blast radius (`TransformationEdge`
grows one optional field, defaulted empty, so every non-calendar edge in
every other domain is byte-for-byte unaffected), keeps the graph shape
(no registry interface change, no domain plugin outside calendar needs to
know this exists), and is directly renderable (one subsection per kind is
a mechanical extension of the existing one-section-per-edge renderer).

## 2. Why `canonToVtodoIcalLoss()` lives in `icalcanonstages.h`, not `vtodocanonfields.h`

`Kalburator::Todo::canonToVtodoLoss()` (`src/todo/vtodocanonstages.h`)
already exists and governs a DIFFERENT edge — `{todo,canon}→
{todo,vtodo}`. Declaring the calendar-domain leg's profile under the same
name in a different namespace would be legal C++ but a landmine for the
next reader (`Kalburator::Calendar::canonToVtodoLoss()` vs.
`Kalburator::Todo::canonToVtodoLoss()`, two functions, same tail, adjacent
directories). Named it `canonToVtodoIcalLoss()` instead and put it next to
`canonToIcalLoss()` in `icalcanonstages.{h,cpp}` — the file that already
owns the kind dispatch this profile serves — with an explicit header
comment cross-referencing the sibling function and explaining why the two
currently differ (see O93, §7 below).

## 3. Profile contents (today's ACTUAL drops, not the post-IP.6/IP.10 state)

### VTODO-via-`{calendar,canon}` — `canonToVtodoIcalLoss()`

| Canon `PropertyId` | RFC 5545 property | `LossKind` | Catalogued today? |
|---|---|---|---|
| `attachments` | ATTACH | Dropped | yes (contributed by VEVENT/VJOURNAL) |
| `attendees` | ATTENDEE | Dropped | yes |
| `classification` | CLASS | Dropped | yes |
| `color` | COLOR | Dropped | yes |
| `organizer` | ORGANIZER | Dropped | yes |
| `sequence` | SEQUENCE | Dropped | yes |
| `url` | URL | Dropped | yes |
| `comments` | COMMENT | Dropped | **no** (O91 — no emitter of any kind produces it) |
| `contacts` | CONTACT | Dropped | **no** (O91) |
| `resources` | RESOURCES | Dropped | **no** (O91) |
| `requestStatus` | REQUEST-STATUS | Dropped | **no** (O91, upstream — KCalendarCore has no accessor at all) |
| `geo` | GEO | **Degraded** | yes |

The first seven are O83's finding; the next four are O91's. All eleven
were verified by direct grep of `vtodocanonfields.cpp` for the relevant
KCalendarCore accessor (`attendees()`, `organizer()`, `attachments()`,
`color()`, `secrecy()`/CLASS, `sequence()`, `url()`, `comments()`,
`contacts()`, `resources()`) — zero references, on either the
promote or the demote side, for every one of them.

**`geo` — Degraded, not Dropped, and why.** `vtodocanonfields.cpp` DOES
promote and demote `geo` (`:443-447`, `:793-798`) — its NAME survives a
round trip. What doesn't survive is the VALUE: kcalendarcore's own GEO
serializer corrupts it (O86, upstream, reproduces with no libkalburator in
the picture), so `promote(demote(promote(x)))` is not the identity for a
VTODO carrying `geo`. None of the four `LossKind` values is a precise fit:
`Dropped` is wrong (the property is not absent from the output — this is
exactly why `geo` correctly does NOT appear in IP.8's `expectedLossTable()`
property-NAME-loss list); `Simplified`/`Reversible` both imply the
original is recoverable in some form, which it is not — the corruption
happens inside kcalendarcore's own round trip, there is no reduced-but-
valid form and nothing to "reverse" to. `Degraded` — "mapped through a
lossy vocabulary; original kept verbatim" — is the closest available fit
even though the actual mechanism (a serializer bug) isn't a vocabulary
mapping at all. Chose it because it is the ONLY one of the four that does
not assert something false (that the name is gone, or that the value is
recoverable); the imprecision is documented at the declaration site
(`icalcanonstages.cpp`) rather than papered over. IP.6 owns the real fix
(PlanStan ratified: drop `geo` entirely rather than hand-serialize around
the upstream bug — Amendment 2 §B.5).

### VJOURNAL — `canonToVjournalLoss()` (repopulated)

| Canon `PropertyId` | RFC 5545 propert(y/ies) | `LossKind` | Catalogued today? |
|---|---|---|---|
| `attachments` | ATTACH | Dropped | yes |
| `attendees` | ATTENDEE | Dropped | yes |
| `organizer` | ORGANIZER | Dropped | yes |
| `relatedTo` | RELATED-TO | Dropped | yes |
| `recurrenceId` | RECURRENCE-ID | Dropped | yes |
| `recurrence` | RRULE, RDATE, EXDATE | Dropped | yes |
| `comments` | COMMENT | Dropped | **no** (O91) |
| `contacts` | CONTACT | Dropped | **no** (O91) |
| `requestStatus` | REQUEST-STATUS | Dropped | **no** (O91, upstream) |

Nine `PropertyId`s, eleven RFC names (`recurrence` — the single
verbatim-RFC5545-lines carrier VEVENT/VTODO already share, invariant 3 —
covers all three of RRULE/RDATE/EXDATE at once, since
`journalFieldsToCanon()` has ZERO recurrence handling of any kind). No
`RESOURCES` row — RFC 5545 jourprop does not permit it on VJOURNAL at all,
so its absence is correct, not a drop.

**`recurrenceId` is O87's identity-corruption finding**, not mere field
loss: a detached VJOURNAL instance and its master become indistinguishable
in canon once `RECURRENCE-ID` is dropped (both demote to the same uid with
no discriminator). `LossKind` has no "identity corruption" verdict;
`Dropped` is the closest honest fit (the property genuinely is absent from
the demoted output) but does not, by itself, convey the severity — that
stays recorded in FINDINGS O87, referenced from the declaration site.

### Four ids with no existing canon `PropertyId` — `comments`/`contacts`/`resources`/`requestStatus`

O83's own text already anticipated this: *"None of these drops is
declared in any loss profile. They cannot be:
`LossProfile::affected` is keyed by `PropertyId`, and an uncatalogued key
has no id to key on."* — written about the ORIGINAL seven, all of which
(verified) ARE already catalogued (contributed by VEVENT and/or VJOURNAL).
O91's four ADDITIONAL properties are different: no emitter of any kind
produces `comments`/`contacts`/`resources`/`requestStatus`, so none
reached `calendarcanonproperties.cpp`'s contributed-id union — there is
genuinely no catalogued `PropertyId` for them.

Resolution: declared them anyway, as new `PropertyId` literals
(`comments`, `contacts`, `resources`, `requestStatus` — plural where the
KCalendarCore accessor is plural, i.e. `comments()`/`contacts()`/
`resources()`, camelCase singular for `requestStatus` since no accessor
exists at all to take a cue from). Verified this is safe by reading
`LossProfile`'s own header and every existing call site
(`grep -rn "\.affected" src/ tests/`, ~90 hits): `LossProfile.affected` is
a plain `QHash<PropertyId, LossKind>` with NO catalogue cross-check
anywhere — `materializedLoss()` looks the key up directly in the parsed
canon JSON (`o.value(k)`), `ConvergenceMatrix` renders `PropertyId::toString()`
directly, and several existing profiles (e.g. `googlecanonstages.cpp`'s
`canonToGoogleEventLoss()` reusing calendar-domain-shared ids like `due`/
`completed`) already reference ids outside their own edge's "home"
catalogue. Did NOT add these four to `calendarcanonproperties.cpp` — that
would misrepresent them as "something an emitter can produce," which is
false today, and IP.3's contributed-catalogue mechanism (this campaign's
own prior item) exists specifically to keep the catalogue's id set
structurally tied to what emitters actually contribute. If/when IP.6/IP.10
implement these four for real, whichever item does so should add proper
contributed-id entries at that point — not before.

## 4. `materializedLoss()` and the presence check

`SyncEngine::materializedLoss()` already had `canonData` in hand to check
each affected property's presence before warning (skip absent/empty
values). Extracting `_canon.kind` from the SAME parsed object costs one
extra `CanonEnvelope::kind()` call — moved the `QJsonObject::parse` call
above the (now removed) early lossless-check short-circuit so it runs
unconditionally, since the kind is needed before the loss profile can
even be selected. This is a small, deliberate behaviour change (parsing
happens even for a would-be-lossless legacy call) but parsing a
already-in-memory `QByteArray` is cheap and the alternative — parsing
twice, once for kind and once for presence — is worse.

## 5. IP.8's `expectedLossTable()`: vtodo/vjournal derived, vevent stays literal

Wired via a new `droppedRfcNames(const LossProfile&)` helper plus a small,
necessarily hand-declared `propertyIdToRfcNames()` translation table
(canon `PropertyId` → RFC 5545 property NAME(s) — one-to-many for
`recurrence`). `droppedRfcNames()` only translates `LossKind::Dropped`
entries: this gate measures property-NAME presence/absence, which is
exactly what `Dropped` means; `Reversible`/`Simplified` both keep the
NAME present in demoted output (nothing for a before/after name-set diff
to see); `Degraded` is used exactly once across both new profiles
(VTODO's `geo`) and its damage is a corrupted VALUE, measured by the
gate's SEPARATE fixpoint check, not the lost-name list — so Dropped-only
translation is a complete mapping onto this gate's axis for both profiles
as declared today, not a gap. A `Q_ASSERT_X` in `droppedRfcNames()` fails
loudly (rather than silently under-reporting) if a future edit adds a
Dropped id with no translation entry.

**vevent's list stays a hand-typed literal, deliberately.** Its real
profile (`canonToIcalLoss()`) is a DIFFERENT vocabulary of loss entirely —
canon-JSON vendor keys with no RFC-name counterpart at all (`onlineMeeting`,
`guestsCan*`, ...) — from the RFC-property-name drops this gate measures
(`GEO`, `RELATED-TO`, `COMMENT`, `CONTACT`, `RESOURCES`,
`REQUEST-STATUS`). Declaring those six RFC-name drops on
`canonToIcalLoss()` is explicitly PLAN.md Amendment 1 §A.3.2's territory —
"IP.6 also owns the O86 GEO decision" plus "VEVENT drops RELATED-TO ...
belongs in the [IP.6] extraction" — and O91's COMMENT/CONTACT/RESOURCES/
REQUEST-STATUS findings were filed against IP.6/IP.9 jointly with the
understanding that IP.9 owns the MECHANISM (O88's kind-polymorphism) and
IP.6 owns VEVENT's own content. Wiring vevent's table now would mean
either fabricating profile entries IP.6 has not ratified, or deriving an
EMPTY list (a false "nothing lost" signal, worse than the honest literal).
Left the literal in place with an explanatory comment; revisit when IP.6
lands.

## 6. Matrix diff — explained, not just pinned

```
- `canon → ical` (declared lossy)
+ `canon → ical` (declared lossy) (kind-scoped: default, vjournal, vtodo)

- ### canon → ical
+ ### canon → ical (default)
  [... unchanged rows ...]

+ ### canon → ical (vjournal)
+ | attachments | Dropped | ... [9 rows, see §3]
+
+ ### canon → ical (vtodo)
+ | attachments | Dropped | ... [12 rows, see §3]
```

This is the ONE item PLAN.md flags as expected to change the matrix
substantially, and it did: the calendar domain's edge inventory line now
carries a `(kind-scoped: ...)` annotation, the old single `### canon →
ical` section is relabeled `(default)` (still `canonToIcalLoss()`,
byte-identical content), and two new sections appear for vtodo/vjournal
with exactly the rows in §3. No other domain's section changed (contacts,
todo, and calendar's org-ical/google-event/ms-event sections are
byte-identical — confirmed by the full diff, not assumed). Regenerated via
`./build/tools/matrixgen/matrixgen`, diffed against the pre-edit committed
copy to confirm the diff was EXACTLY this and nothing else, then copied
over the committed file. `tst_gm_pipeline_convergence`'s
`committedMatrixMatchesGenerated()` is green (see §8).

## 7. New finding, not fixed: O93

While writing `canonToVtodoIcalLoss()`, confirmed that
`src/todo/vtodocanonstages.cpp`'s `CanonToVTodoStage::transform()` calls
the exact same `canonObjectToVtodoBytes()` as the calendar-domain leg —
meaning `{todo,canon}→{todo,vtodo}`'s existing `canonToVtodoLoss()`
(`src/todo/vtodocanonstages.cpp:65-109`) is demoting through IDENTICAL
code to what IP.9's new calendar-domain profile now honestly declares, yet
`canonToVtodoLoss()` still declares none of O83/O91's eleven drops or the
`geo` corruption. PLAN.md's IP.9 body described this leg as "already
good" — verified false by the same grep method used for the calendar leg.
Logged as **FINDINGS O93**, not fixed (out of IP.9's scope — O88 is
specifically the calendar domain's kind-polymorphism, not this sibling
edge's own profile completeness) and not owned by any item as of this
filing; text includes candidate homes (fold into IP.6, or a small
standalone follow-up once IP.9's pattern exists to copy) and notes that
IP.8's RFC-5545 fidelity gate never exercises `(todo, vtodo)` at all, so
this leg's undeclared loss was never measured against the standard the
way the calendar leg was.

## 8. Test evidence

- **Non-vacuity, IP.8's derived vtodo table:** temporarily removed the
  `url`→`Dropped` line from `canonToVtodoIcalLoss()`, rebuilt
  `kalburator` + `tst_incidence_rfc5545_fidelity`, confirmed a real
  `FAIL!  : ... Compared lists have different sizes.` (12 passed, 1
  failed — the removed entry breaks the non-XFAIL `QCOMPARE`), reverted
  (`diff` against the pre-edit file confirmed a byte-identical revert),
  rebuilt clean — 13/13 passed again. Method matches IP.1/IP.2/IP.3's
  established non-vacuity convention.
- `./build/tests/calendar/tst_incidence_rfc5545_fidelity` run directly:
  `Totals: 13 passed, 0 failed, 0 skipped, 0 blacklisted` — same XFAIL
  set as before this item (unchanged reasons/messages for vevent/vtodo/
  vjournal's undeclared-drop XFAILs — this item does not close any of
  them, only makes the DECLARATION mechanism truthful).
- `./build/tests/calendar/tst_calendar_kind_dispatch` run directly:
  `Totals: 17 passed, 0 failed` (was 14 before this item — three new
  slots: `vtodoDemoteLossProfileIsVtodoShapedNotEventShaped`,
  `vjournalDemoteLossProfileIsVjournalShapedNotEventShaped`,
  `veventDemoteLossProfileUnchangedByIp9`). These pin the FULL declared
  content of both new profiles (every row in §3, both directions —
  present-and-Dropped/Degraded for the kind's own properties,
  absent-entirely for every one of `canonToIcalLoss()`'s event-only
  vendor keys) against the REAL `CalendarStockShapes::edges()` +
  `TransformationRegistry::compile()` graph — not a synthetic
  registry-in-miniature.
- **Dead-code grep proof** (acceptance criterion, verbatim command +
  result):
  ```
  $ grep -rn "canonToVjournalLoss" src/ tests/
  src/calendar/journalcanonfields.h:20:Kalburator::Shape::LossProfile canonToVjournalLoss();
  src/calendar/calendarstockshapes.cpp:94:    canonToIcalEdge.lossByKind.insert(QStringLiteral("vjournal"), canonToVjournalLoss());
  src/calendar/journalcanonfields.cpp:214:Kalburator::Shape::LossProfile canonToVjournalLoss()
  tests/calendar/tst_incidence_rfc5545_fidelity.cpp:117:// canonToVjournalLoss(), both new/repopulated by IP.9 — see
  tests/calendar/tst_incidence_rfc5545_fidelity.cpp:229:        // Kalburator::Calendar::canonToVjournalLoss(). RDATE/RRULE/EXDATE
  tests/calendar/tst_incidence_rfc5545_fidelity.cpp:235:            droppedRfcNames(Kalburator::Calendar::canonToVjournalLoss()),
  ```
  Real call sites now exist (edge registration, IP.8 gate wiring) — no
  longer declared-with-zero-callers. Also spot-checked every OTHER
  `LossProfile`-returning function declared under `src/` (12 total,
  including the two new/repopulated ones) for a "declaration + definition,
  zero calls" pattern the way `canonToVjournalLoss()` used to be — none
  found; every one has at least one real caller (typically its owning
  `edges()` registration).
- Matrix: regenerated, diffed against the pre-edit committed copy (§6),
  copied over deliberately (not byte-identical — this is the expected
  exception, per PLAN.md). `tst_gm_pipeline_convergence` green.
- **Full suite:** `cmake --build build -j$(nproc)` (clean full rebuild,
  no errors) then `ctest --output-on-failure` (full run, no `-R` filter):
  **215 tests, 211 passed, 4 failed** — the same 4 known-environmental
  slots as the IP.3 baseline (`tst_backend_signals`,
  `tst_backend_thread_relocation`, `tst_backend_reentrancy_pin`,
  `tst_remotecalendarbackend`), verified by failure TEXT, not name: three
  of the four (`tst_backend_reentrancy_pin`'s three sub-failures,
  explicitly) carry the documented `RemoteCalendarBackend: KDAV job
  exceeded transfer timeout ( 30000 ms)` signature, and the other two
  (`tst_backend_signals`, `tst_remotecalendarbackend`) show the LOCAL
  Radicale test server at `127.0.0.1:5232` returning `412 Precondition
  Failed`/`409 Conflict` on calendar/item creation (`Failed to create
  test calendar`) — a different transient symptom of the SAME local
  Radicale test-server dependency (stale/contended state from prior runs
  or system load), not a KDAV timeout this run, but the identical root
  cause class CLAUDE.md names ("KDAV 30s-transfer-timeout vs the local
  Radicale"). Total count unchanged from IP.3's baseline (215/211/4) —
  see §10 for why this item's new slots do not move the ctest-level
  count.

## 9. Acceptance-criteria checklist

- [x] A VTODO demote through `{calendar,ical}` warns about VTODO drops
  and not event-only fields — pinned at the Pipeline/LossProfile level
  (`tst_calendar_kind_dispatch.cpp`'s
  `vtodoDemoteLossProfileIsVtodoShapedNotEventShaped()`), which exercises
  the REAL registered stock-shape graph end to end from
  `TransformationRegistry::compile()` through `Pipeline::composedLoss(kind)`
  — the exact mechanism `SyncEngine::materializedLoss()` calls. See §10
  for why a full `SyncEngine`-level demonstration (a real sync emitting
  `transcodingWarning`) was attempted and abandoned as unreliable for
  today's codebase, not skipped for convenience.
- [x] Same for VJOURNAL — `vjournalDemoteLossProfileIsVjournalShapedNotEventShaped()`.
- [x] `canonToVjournalLoss()` wired or gone; grep proves no dead loss
  function remains — §7 (well, §8 for the grep; §3 for the content).
- [x] Matrix regenerated and now kind-aware; byte-pin updated
  deliberately, diff explained — §6.
- [x] IP.8's allow-list wired to the real profiles, closing its
  `TODO(IP.9)` — §5 (vtodo/vjournal derived; vevent's non-wiring is
  explained, not a leftover TODO).

## 10. An end-to-end `SyncEngine` warning demonstration was attempted and abandoned — here is why

PLAN.md's acceptance criteria read naturally as "a real sync warns," so a
`SyncEngine`-level test (modeled on the existing
`tst_calendar_transcoding_warning.cpp`) was built first, using `geo` —
the ONE property in either new profile that `vtodocanonfields.cpp`
actually promotes AND demotes today (every other O83/O91 drop is dropped
at PROMOTE too, so none of them ever reaches canon to be "materialized"
by a real sync — that gap is exactly IP.6's job, not fixable here without
violating "no fixing while passing through").

It failed for a reason worth recording: `MockBackend::addIncidence()`
(`src/calendar/mockbackend.cpp:552-563`) round-trips the incoming
`Incidence::Ptr` through `KCalendarCore::ICalFormat::toICalString()` +
`fromString()` IMMEDIATELY, before the sync engine ever runs — and O86's
GEO corruption strikes at exactly that serialization boundary. So a VTODO
with `geo` set, added via `MockBackend::addIncidence()`, already has a
corrupted/unparseable GEO line baked into the backend's stored copy before
`SyncEngine::runSync()` is ever called; the corrupted line then fails to
re-parse on the source-side promote, so `hasGeo()` is false by the time
canon is built, and `materializedLoss()` correctly (if unhelpfully, for
THIS test's purposes) finds nothing to warn about. This was confirmed by
direct observation of a garbled GEO value in the qWarning libical emits at
`addIncidence()` time, before any sync-related log line appears.

There is currently no property that is BOTH (a) actually present in canon
after a real `{calendar,ical}`-native VTODO round-trips through a
`MockBackend`, AND (b) one of IP.9's newly-declared drops — `geo` is the
only candidate and O86 defeats it before the mechanism under test even
runs. Building a synthetic path that bypasses `MockBackend`'s own
serialization (e.g. injecting canon bytes directly) would test something
materially different from "a real sync." Concluded the Pipeline-level
test in `tst_calendar_kind_dispatch.cpp` is the RIGHT layer to pin this at
today — it exercises the exact mechanism O88 broke
(`TransformationEdge`/`Pipeline::composedLoss(kind)` selection against the
REAL registered graph) without depending on an unrelated upstream bug's
current behaviour. A true `SyncEngine`-level demonstration becomes
possible once IP.6 lands (any of its newly-wired properties would serve).

**Consequence for the "adds tests, count should grow" expectation:** the
ctest-level EXECUTABLE count does not grow from this item (215 → 215,
matching IP.3's own precedent of adding QTest slots to an EXISTING
binary) — the three new slots landed inside
`tst_calendar_kind_dispatch`'s existing executable, not a new one.
QTest-SLOT count did grow (+3). Flagging this explicitly rather than
padding in a flaky standalone executable to move a number.

## 11. Corner cases declared-not-executed

- **Retrofitting `{todo,canon}`'s own `canonToVtodoLoss()`** — filed as
  O93, not built (out of IP.9's scope).
- **Adding `(todo, vtodo)` coverage to IP.8's RFC-5545 fidelity gate** —
  noted in O93 as a gap, not built here (IP.8 is closed; this would be a
  new item's or a follow-up's scope).
- **Registering `comments`/`contacts`/`resources`/`requestStatus` in
  `calendarcanonproperties.cpp`** — deliberately not done; see §3's
  closing paragraph for the reasoning (would misrepresent them as
  emitter-producible, which IP.3's contributed-catalogue mechanism exists
  to prevent).
- **Extending `TransformationEdge`'s idempotent-re-registration check
  (`registerEdge`'s `sameAffected` comparison) to also compare
  `lossByKind`** — not done; today's codebase registers each edge exactly
  once from `edges()`, so this gap has no live consequence, but a future
  caller that re-registers the SAME `(from,to)` pair with a DIFFERENT
  `lossByKind` would not be caught by the existing assertion. Noted here
  rather than silently left for a future debugging session to rediscover.

## 12. Next

**IP.6** — `incidencecommonfields` extraction (3 kinds) + the missing
VTODO fields + the O86 GEO decision (PlanStan ratified: drop it). Highest
remaining severity — these drops are live on PlanStan's default task
path. Read `PLAN.md`'s IP.6 section plus Amendment 1 §A.3.2 and Amendment
2 §B.5 before starting.
