# W4 (VP.d) — completion-anchored recurrence — RECON & handoff

**Status:** EXPLORATION COMPLETE 2026-08-26; implementation NOT started.
This doc persists the code map so a fresh agent picks up W4 without
re-exploring. Decisions come from
`docs/2026-08-25-vtodo-parity-handoff-response.md` §W4 (binding).

---

## What W4 is (per the response doc, binding)

- **Canon representation:** the org fidelity string stays verbatim
  byte-exact (`OrgRoundtripData.repeaterString` upstream of us in org-io;
  OrgBackend already keeps it byte-exact via `m_roundtripData`) PLUS a
  **derived standard form as a catalogued canon key**:
  `completionAnchor: {type: catchUp|restart, interval, unit}` — catalogued
  so the differ SEES anchor advances.
- **Anchor ownership (Q2):** CALLER advances on completion (PlanStan stages
  it like any field change). The engine NEVER mutates data on diff
  (redress-campaign invariant).
- **CalDAV write-out:** derived RRULE anchored at last completion ONLY
  (no X-prop duplication — the verbatim string rides the `x-vtodo` extras
  channel anyway for org-fidelity consumers).
- **Differ treatment:** an anchor advance is an ordinary field change,
  never a conflict, because both sides converge on the same derived value.
  (Cataloguing the key gives this automatically — see §6.)
- **Note:** today NOTHING executes `.++`/`.+`; execution lives in
  PlanStan/org-io on completion events. Our obligation is representation,
  round-trip, and non-conflict differ treatment.

Scope per `docs/campaign/vtodo-parity/STATUS.md` VP.d row:
"completion-anchor canon key (catalogued) + CalDAV derived-RRULE
write-out + differ non-conflict treatment".

---

## Design sketch (decide before coding)

1. **Catalogued key:** add `completionAnchor` (`PropertyKind::Json`) to
   `makeTodoCanonCatalogue()` in `src/todo/todocanonproperties.cpp` (after
   the `recurrenceRange` line ~:40). JSON shape:
   `{"type": "catchUp"|"restart", "interval": <int>, "unit": "h"|"d"|"w"|"m"|"y"}`.
2. **Promote** (`todoFieldsToCanon`, `vtodocanonfields.cpp`): when the
   source VTODO carries an org repeater marker, derive `completionAnchor`.
   The verbatim org string is available from
   `todo->customProperties()` under an X- key. PROPOSAL: recognize
   `X-ORG-REPEATER:<string>` (or check what org-io writes today; org-io
   stores it ONLY in `OrgRoundtripData.repeaterString`, off-incidence, and
   deliberately does NOT put `X-ORG-REPEATER` on the incidence — see §3).
   So the org-leg promote seam is `OrgBackend::m_roundtripData` (gated
   `KALBURATOR_HAVE_ORG_IO=ON`), NOT the generic vtodo→canon stage.
   DECISION NEEDED: derive in OrgBackend at fetch time, or accept an
   `X-ORG-REPEATER` custom prop in generic promote (testable without
   org-io) + wire OrgBackend to inject it.
3. **Demote** (`canonObjectToVtodoBytes`): if `completionAnchor` present,
   emit a **derived RRULE anchored at last completion ONLY** — build the
   standard RRULE from `{interval, unit}` (mirror `recurrencepatternconverter.cpp`
   RRULE building) and anchor it at `completed` (canon `completed` key),
   appended into the recurrence-injection seam at `vtodocanonfields.cpp:548-562`.
   Do NOT write an extra X-prop (the verbatim string already rides
   `providerExtras["x-vtodo"]` via the custom-props channel `:279-292` /
   re-emit `:512-518`).
4. **Loss-profile declarations** (discipline: declared before the matrix
   grows):
   - `canonToVtodoLoss()` (`vtodocanonstages.cpp:65-86`) — add
     `completionAnchor` (Reversible — rides x-vtodo extras + derived RRULE).
   - `canonToGoogleTaskLoss()` (`googletaskcanonstages.cpp:248-271`) — add
     `completionAnchor` to the Dropped list (Google has no recurrence
     field; the demote silently drops unconsumed keys at `:237-239`).
   - `canonToMsTodoTaskLoss()` (`mstodotaskcanonstages.cpp:535-567`) — the
     MS demote's unhandled-canon-prop loop (`:474-507`) auto-carries ANY
     unhandled key as an open-extension carrier (`carrierKey(prop)`, e.g.
     `x-canon-completion-anchor`) unless added to the `handled` set
     (`:476-487`). DECISION NEEDED: let it auto-carry (Reversible, honest)
     or add to `handled`+declare. Recommend: auto-carry + declare
     Reversible, matching the existing `recurrence` Reversible ruling.
5. **Differ non-conflict:** automatic — `todoCanonPropertyIds()` feeds
   `CanonJsonDiffer` (`tododomaindefinition.cpp:27-30`); the key becoming
   catalogued makes an advance an ordinary field change. Pin with a differ
   test (e.g. a `completionAnchor` variant of
   `tests/shape/tst_canonjson_diff_merge.cpp` `differMarksChangedPropertyOnly`).
6. **Matrix:** new canon key does NOT change edge counts (O63: only edges()
   growth does). BUT adding loss-profile rows for `completionAnchor`
   changes the generated `docs/campaign/eee/CONVERGENCE-MATRIX.md` →
   regenerate via `tools/matrixgen` (`./build/tools/matrixgen/matrixgen >
   docs/campaign/eee/CONVERGENCE-MATRIX.md`) and commit the byte-pin
   (`tests/convergence/tst_gm_pipeline_convergence.cpp`) in the SAME
   commit (O63 discipline). Edge-count pins (`tests/todo/tst_vtodo_plugin.cpp:55-61`,
   `tests/calendar/plugin/tst_calendar_plugin.cpp:138`,
   `tests/contacts/tst_vcard_plugin.cpp:53`) are UNCHANGED (no edge growth).
7. **Tests:**
   - `tests/todo/tst_todo_canon_roundtrip.cpp` — new fixture VTODO with an
     org repeater marker; promote→`completionAnchor` present/absent;
     demote→derived RRULE anchored at `completed` byte-preserved; round-trip
     stable. Pattern: `vtodoRoundTripPreservesRecurrenceLines` (:228-277) /
     `vtodoRoundTripPreservesThisAndFutureRange` (:430).
   - Differ pin in `tests/shape/tst_canonjson_diff_merge.cpp` (or a
     `completionAnchor` variant) proving an advance is a normal field
     change.
   - Org leg (gated): `tests/calendar/tst_orgbackend.cpp` has repeater
     tests `testCatchUpRepeaterRoundtrip` (:1322), `testRestartRepeaterRoundtrip`
     (:1357) — extend for completionAnchor if org-leg wiring lands.

---

## Code map (verified 2026-08-26)

### 1. Canon catalogue
- `src/todo/todocanonproperties.cpp:7-58` — `makeTodoCanonCatalogue()`.
  Recurrence block to mirror (:36-40):
  ```cpp
  cat.addProperty({ PropertyId{"recurrence"},       PropertyKind::StringList, QStringLiteral("Recurrence") });
  cat.addProperty({ PropertyId{"recurrenceId"},     PropertyKind::Json,       QStringLiteral("Recurrence ID") });
  cat.addProperty({ PropertyId{"recurrenceRange"},  PropertyKind::String,     QStringLiteral("Recurrence Range") });
  ```
- `:60-67` — `todoCanonPropertyIds()` auto-includes new keys (differ/merger
  visibility for free).
- `src/shape/propertycatalogue.h:22-31` — `PropertyKind` enum (`Json` exists).
- No "derived vs verbatim" catalogue concept exists; comments mark verbatim
  keys (`todocanonproperties.cpp:36`, `:47`).

### 2. vtodo↔canon field seams
- `src/todo/vtodocanonfields.h:11-12` — `todoFieldsToCanon(todo, originalBytes)`.
- `src/todo/vtodocanonfields.h:15` — `canonObjectToVtodoBytes(obj)`.
- `src/todo/vtodocanonfields.cpp:111-295` — promote. Recurrence block
  (:204-214) uses `extractComponentRecurrenceLines(originalBytes, "VTODO",
  uid)` (component-scoped text scanner, `src/calendar/icalcomponentscan.cpp:78-186`).
  `completed` key promote :197-202. Custom-props → `providerExtras["x-vtodo"]`
  :279-292.
- `src/todo/vtodocanonfields.cpp:297-565` — demote. `completed` demote
  :418-426. Recurrence capture :428-432. Extras re-emit :512-518.
  **Recurrence-line injection seam** :548-562 (post-serialization insert
  before `END:VTODO`) — THE seam for the derived RRULE.
- Stage wrappers: `src/todo/vtodocanonstages.cpp:38-59`. Calendar-domain
  kind dispatch routes VTODO here: `src/calendar/icalcanonstages.cpp:55-57`
  (promote) / `:82-83` (demote).

### 3. Org repeater (external org-io, GATED `KALBURATOR_HAVE_ORG_IO=ON`)
- `OrgRoundtripData { todoKeyword, descriptionHash, repeaterString }` —
  `/home/clinton/dev/PlanStan/libs/org-io/include/orgfilemanager.h:67-71`.
- `OrgRepeaterType { None, Cumulative(+1w→RRULE), CatchUp(++1w), Restart(.+1w) }`
  + `OrgRepeaterInfo { type, interval, unit, originalString }` — `:209-222`.
- Parser `OrgFileManager::extractOrgRepeater` — org-io
  `src/orgfilemanager.cpp:1468-1494`. Regex `(\.\+|\+\+|\+)(\d+)([hdwmy])`.
  Maps `.+'→Restart, `++'→CatchUp, else Cumulative.
- Apply: `applyRepeaterToIncidence` — `orgfilemanager.cpp:1496-1535`; sets
  KCalendarCore recurrence for ALL types; stores `roundtrip.repeaterString`
  ONLY for CatchUp/Restart (:1530-1534). **Incidence purity pinned**:
  `tests/calendar/tst_orgbackend_external.cpp:611-615,631-634` assert
  `customProperty("PLANSTAN","X-ORG-REPEATER")` is EMPTY.
- Re-emit: `generateOrgRepeater` — `orgfilemanager.cpp:1537-1622` (prefers
  verbatim repeaterString when `incidence->recurs()`).
- libkalburator has NO own repeater parser (grep in `src/` only hits
  capability docs). W4 may need a small mirror of the regex for the generic
  promote path (or go through OrgBackend only).
- `OrgBackend` roundtrip data: `src/calendar/orgbackend.h:113`
  (`m_roundtripData`), populated `orgbackend.cpp:344`, passed to
  `updateHeadlineFromIncidence`/`createNewHeadline` :252-253,264-265,424-425,
  439-441,794-795,850-851.
- Blob record = `serializeIncidenceToIcal(incidence)` (`orgbackend.cpp:609-617`,
  `orgBlobRecord` :620-634) — repeater NOT in bytes (purity).

### 4. org-ical edge (calendar domain)
- `src/calendar/orgicalcanonstages.cpp` — `OrgICalToCanonStage` is
  Event-only (:107-114); VTODO falls through to `ICalToCanonStage`
  (`:247`) → `todoFieldsToCanon`. Canon→org-ical stashes original RRULE in
  `X-ORIGINAL-RRULE` (:184-226).
- `tests/calendar/tst_orgical_canon_roundtrip.cpp` — all VEVENT-only; no
  VTODO coverage. `canonToOrgIcalLossChargesRecurrenceSimplified` (:234-243)
  pins `recurrence` = Simplified on canon→org-ical.
- `src/calendar/calendarstockshapes.cpp:92-96` — CanonToICalStage routes
  vtodo→`canonObjectToVtodoBytes`.

### 5. CalDAV write-out path
- Engine demotes canon→native at `src/engine/syncengine.cpp:3318` +
  :4630-4649 (`rec.data = canonToTgt->apply(rec.data)`).
- `RemoteCalendarBackend::applyRecords` (`remotecalendarbackend.cpp:3088-3280`)
  pushes `rec.data` bytes. So the "CalDAV write-out" is entirely inside
  `canonObjectToVtodoBytes` — no backend change needed.
- RRULE-building precedent: `src/calendar/recurrencepatternconverter.cpp`
  (MS edge; builds RRULE text, `:225,:252-275`).
- Event analog of the injection seam: `eventcanonfields.cpp:713-725`.

### 6. Differ / merger
- `src/todo/tododomaindefinition.cpp:27-30` — `CanonJsonDiffer(todoCanonPropertyIds())`;
  `:32-36` same for merger. New catalogued key is differ-visible
  automatically.
- `src/shape/canonjsondiffer.cpp:14-38` — coarse per-property diff; one
  change inside a Json composite marks the whole property changed.
- Differ is pure/read-only; engine never mutates on diff → Q2 satisfied by
  construction (caller stages the advance; differ just sees it).

### 7. Tests to extend
- `tests/todo/tst_todo_canon_roundtrip.cpp` — fixtures :58-135
  (`kTestVTodo`, `kTestVTodoWithRecurrence`, `kTestVTodoWithVtimezoneNoOwnRecurrence`),
  helpers `parseTodoFromICal` :86, `todoComponentOf` :97. Recurrence-pin
  pattern `vtodoRoundTripPreservesRecurrenceLines` :228-277; recurrenceId
  pattern `vtodoRoundTripPreservesRecurrenceId` :386, ThisAndFuture :430,
  `vtodoMasterHasNoRecurrenceId` :467. Loss-pin
  `canonToVtodoLossProfileChargesDroppedAndReversible` :328-349.
- `tests/shape/tst_canonjson_diff_merge.cpp` — `differMarksChangedPropertyOnly`
  :43, `differTreatsCompositeAsWhole` :62 (Json-property change pattern to
  copy for `completionAnchor`).
- `tests/calendar/tst_orgbackend.cpp:1251-1416` — org repeater round-trips
  (gated).
- Matrix byte-pin: `tests/convergence/tst_gm_pipeline_convergence.cpp:586`.

### 8. Matrix generation
- `tools/matrixgen/main.cpp` — regenerate + commit with any loss-profile
  change. `docs/campaign/eee/CONVERGENCE-MATRIX.md` is the committed output.
- Todo edge declarations: `src/todo/todostockshapes.cpp` — 9 todo edges
  (:42-79); `canon→vtodo` loss `:58`, `canon→google-task` `:68-70`,
  `canon→ms-todotask` `:75-77`.

---

## Open decisions for the implementing agent

1. **Org-leg promote seam:** derive in OrgBackend at fetch time (gated,
   needs `KALBURATOR_HAVE_ORG_IO=ON` build to verify) vs generic promote
   reading `X-ORG-REPEATER` custom prop (testable without org-io) + OrgBackend
   injecting it. Recommend: generic-promote support (testable, additive) +
   OrgBackend injection if the org-io build is available; otherwise document
   the org seam and land the canon+CalDAV layer first (testable in default
   profile).
2. **MS carrier:** auto-carry `completionAnchor` as an open-extension
   carrier (Reversible, honest) vs add to `handled`. Recommend auto-carry.
3. **Unit alphabet:** org-io uses `[hdwmy]`; derived RRULE FREQ mapping
   h→HOURLY, d→DAILY, w→WEEKLY, m→MONTHLY, y→YEARLY (mirror
   `recurrencepatternconverter.cpp`).
4. **Anchor source:** "anchored at last completion" = the derived RRULE's
   reference is the canon `completed` timestamp (demote `completed` :418-426
   already emits COMPLETED); no separate X-prop.

## Session note
Exploration ran as a subagent (2026-08-26). W1 (VP.c) is COMPLETE and
committed (`7585152`); W2 (VP.b), W8 (VP.a) landed earlier. After W4:
VP.e (W3 series-split), VP.f (W5+W6.2+W7). No W4 code has been written.
