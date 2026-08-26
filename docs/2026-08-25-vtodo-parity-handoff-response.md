# Response: VTODO semantic-parity handoff (W1–W8) — ACCEPTED with scoping edits

**From:** libkalburator dev team
**To:** PlanStan app team
**Date:** 2026-08-25
**Re:** `PlanStan/docs/handoffs/2026-08-25-libkalburator-vtodo-parity-handoff.md`
**Status:** ALL EIGHT ITEMS ACCEPTED (two with scoping edits, one closed
N/A-with-caveat). Sequencing adjusted to share legs with the in-flight
B2C campaign (vendor todo backends land first — they are required test
legs for your W1/W2 matrices anyway).

Live plan: `docs/campaign/vtodo-parity/STATUS.md` (this file records the
acceptance + decisions; that file tracks execution).

---

## 0. Corrections to YOUR audit — confirmed, plus two things you missed

Your re-read of our tree was right: X- props survive via
`providerExtras["x-vtodo"]`, RRULE/RDATE/EXDATE lines ride verbatim from
`originalBytes` (component-scoped scanner, master-preferring). Two
additions you didn't see:

1. **providerExtras is INVISIBLE to the canonical differ.** The todo
   domain differs on catalogued canon property ids only
   (`tododomaindefinition.cpp` wires `CanonJsonDiffer(todoCanonPropertyIds())`);
   `providerExtras` is not catalogued, so a sync-confined-to-X-props
   change never dirties a diff. Recorded as **FINDINGS O74**; fix folds
   into W7 (catalogue a derived extras-hash or explicit extras key).
2. **The blob/canon pipeline keys records by UID ALONE.** Your W1
   premise ("confirm how detached instances are keyed") has a sharper
   answer than expected: the incidence-path compound key
   (`uid + '\0' + recurrenceId`, src/diff/syncdiff.cpp:16) exists ONLY
   on the KCalendarCore/SyncRecord path. VTODO-over-CalDAV actually
   flows the blob/canon route (`BackendRecord.id` = UID), where a
   master and its detached exception COLLIDE into one record and the
   component scanner deliberately prefers the master block
   (icalcomponentscan.h:23). W1 is therefore not a documentation task —
   it is the introduction of composite record identity to the blob
   pipeline. Scoping below reflects that.

## 1. Per-item verdicts

### W8 — Unified capability/trait query API — **ACCEPTED, DO FIRST** ✓ (agreed)

Extending `caldavcapabilitydiscovery` into a general capabilities surface,
exposed per logical calendar. Shape:

```cpp
// src/sync/calendarcapabilities.h (public include)
struct CalendarCapabilities {
    enum class AlarmSupport { None, Display, Full };
    AlarmSupport alarms;
    bool recurrenceExceptions;      // detached RECURRENCE-ID instances survive
    bool thisAndFuture;             // RANGE=THISANDFUTURE honored (else series-split offered)
    bool completionAnchoredRepeat;  // org ++/.+ semantics executable
    enum class UnknownProps { Full, XOnly, None };
    UnknownProps unknownPropertyPreservation;
    QString producerId;             // PRODID-derived where discoverable, else backend-type static
};
```

Delivery notes:
- CalDAV leg: discovered (PRODID + supported-report-set probing added to
  discovery; current discovery stops at RFC 4791 component-set/privileges/color/size).
- Google/MSToDo/local/org legs: **static per backend type** (answers your
  Q4 — yes, static; there is no capability protocol on either vendor API
  worth probing beyond what wire knowledge already pins: Google Tasks =
  no exceptions/no alarms/no carriers; MS To-Do = single reminder,
  patternedRecurrence, nav-POST extension carrier).
- Exposed via `DiscoveredCalendar.metadata` + a typed accessor so hosts
  don't string-match.

### W2 — Per-instance completion — **ACCEPTED, representation as you proposed**

Adopting your proposal verbatim: occurrence-N completion = EXDATE(N) on
master + detached instance (RECURRENCE-ID=N, COMPLETED=now,
STATUS:COMPLETED). Producer mapping:
- **Google Tasks:** flatten — the detached instance becomes an ordinary
  standalone completed task (title gains a dated suffix convention TBD
  with you at receipt time); EXDATE is carried on the master via the
  `x-canon-recurrence` carrier channel where lossless round-trip matters;
  declared in the existing google-task loss profile.
- **MS To-Do:** same flattening; master RRULE ⇄ patternedRecurrence
  conversion already exists (recurrencepatternconverter); EXDATE rides
  the kalburator.canon carrier (live-Reversible, O66/O73).
- Undo/atomicity (your Q1): **BaselineStore today has NO multi-record
  transactions** — every baseline write is an independent autocommit.
  Rather than force you to stage atomically forever, W2 delivery INCLUDES
  a scoped transaction API (`BaselineStore::transaction(...)` applying a
  batch of baseline mutations atomically; SQLite WAL makes this cheap),
  and the engine's persist step uses it whenever a batch contains a
  master+exception pair. Until that lands, keep CreateExceptionCommand
  staging as-is — it will simply become redundant rather than wrong.

### W1 — Detached exceptions end-to-end contract — **ACCEPTED, rescope: identity layer first**

Rescoped because of the collision finding above. Delivery order inside W1:
1. Composite record id for the todo blob pipeline: `<uid>` stays the
   master id; a detached instance becomes `<uid>\x01<recurrenceId>`
   (mirroring the SyncRecord precedent and the O55 alias sink-resolution
   rules, so baselines/id-aliasing keep working unchanged). Component
   scanner learns to emit BOTH blocks instead of master-preferring when
   RECURRENCE-ID is present.
2. Written contract doc: keying, differ treatment of master+exception
   pairs (independent diffs joined by shared uid for propagation logic),
   delete semantics (exception-only delete = EXDATE removal + tombstone;
   master delete = cascade tombstone of all keyed exceptions).
3. Non-supporting peers (Google/MSToDo): documented flatten strategy
   shared with W2's mapping.
Test matrix (create/edit/delete/reabsorb × caldav/org/google/ms) follows
once the B2C P3 vendor todo backends land — hence P3-before-W1 in the
sequence.

### W4 — Completion-anchored recurrence — **ACCEPTED with decision**

- Canon representation: org fidelity string stays verbatim
  (`OrgRoundtripData.repeaterString` upstream of us; on our side the
  `{todo,org}` entry keeps it byte-exact) PLUS a derived standard form as
  a catalogued canon key: `completionAnchor: {type: catchUp|restart,
  interval, unit}` — catalogued so the differ SEES anchor advances
  (answers your differ-treatment concern: an advance is an ordinary
  field change, never a conflict, because both sides converge on the
  same derived value; the verbatim org string is compared only on the
  org leg).
- Anchor ownership (your Q2): **caller advances on completion**
  (PlanStan stages it like any field change). The engine never mutates
  data on diff — redress-campaign invariant.
- CalDAV write-out: derived RRULE anchored at last completion ONLY
  (no X-prop duplication — the verbatim string rides the x-vtodo extras
  channel anyway for org-fidelity consumers).
- Note: today NOTHING executes `.++`/`.+`; execution itself lives in
  PlanStan/org-io on completion events. Our obligation is representation,
  round-trip, and non-conflict differ treatment.

### W3 — This-and-future — **ACCEPTED: capability-gated series split**

RANGE=THISANDFUTURE is write-hostile on real servers; we will NOT emit it
even where discovered. Strategy: series split everywhere (master ends
UNTIL<N>; new master starts at N with copied RRULE remainder);
re-association via a Reversible carrier row linking new master ← old uid
(`x-canon-series-split-of`), so your editor MAY display them as one
logical series where the carrier survived, else as two (honest fallback).
Capability flag `thisAndFuture` stays false for every v1 backend; flag
exists so a future server-honoring backend can flip it without API churn.
No orphaned exceptions: split regenerates exceptions with start ≥ N onto
the new master (RECURRENCE-ID rebased), asserted by test.

### W5 — VALARM — **ACCEPTED, partially built already**

Todo canon ALREADY models alarms (`alarms[] {type, offsetSecs, text}`,
vtodocanonfields.cpp:216/:417; catalogued; MS maps single reminder ⇄
alarms[0]; Google declares Dropped). Gap vs your spec: no absolute-trigger
form, no RELATED=START/END discrimination, no REPEAT/DURATION. Delivery:
extend the alarm row shape (additive JSON keys — old rows stay valid),
per-backend trait flags via W8, producer-distrust list = none for us
(your Nextcloud<0.6-style gating is tasks.org-specific; we expose
producerId and let YOU gate).

### W6 — Producer shims — **1 is N/A, 2 accepted, 3 declined-as-built**

1. Priority bands: **N/A, differ is exact** for the iCal leg (integer
   preserved byte-exact through canon; nothing bands it). Caveat: the MS
   To-Do leg bands to low/normal/high (declared Simplified in that edge's
   loss profile) — inherent to the vendor schema, not fixable by shims.
   Org leg preserves exact integer via the PRIORITY drawer property.
2. Malformed DTSTART/DUE coercion: **ACCEPTED** — rules land in
   `vtodocanonfields` promote with unit tests on real-world broken
   samples (we'll lift fixtures from the tasks.org audit §9 list:
   DATE/DATE-TIME mismatch ⇒ coerce to DUE's type; DUE ≤ DTSTART ⇒ drop
   DTSTART; DURATION-without-DTSTART ⇒ drop DURATION). Bonus from our own
   recon: DATE-value DTSTART currently does not round-trip as DATE
   (demote reconstructs midnight-UTC DATE-TIME) — fixing alongside.
3. PRODID-gated trust: **declined as library behavior** — producerId
   exposure via W8 covers it; trust policy is host-layer (Part IV ethics:
   the library stays loud about facts, silent about judgment).

### W7 — Passthrough verification table — **ACCEPTED (recon done, tests pending)**

Current truth table (from code read, 2026-08-25):

| Backend | X-props | VALARM | VTIMEZONE | Ordering/extras |
|---|---|---|---|---|
| LocalBlob | preserved verbatim | preserved | preserved | bytes verbatim |
| CalDAV (RemoteCalendarBackend) | preserved (server raw bytes preferred over re-serialization) | preserved | preserved | bytes verbatim |
| Org | **DROPPED** (fixed headline mapping; only OrgRoundtripData{keyword, descHash, repeater} survives) | dropped | n/a | prose kept only while description MD5 unchanged |
| Google Tasks | native keys stashed verbatim C→G; foreign extras (x-vtodo) do NOT ride; recurrence/alarms/priority etc. declared Dropped | dropped (declared) | n/a | — |
| MS To-Do | unmapped wire keys stashed + x-canon-* carrier (live-Reversible) | single reminder ⇄ alarms[0] | n/a | carrier via nav POST (O73 upsert) |

Plus the O74 differ-blind-spot fix. Round-trip tests per "preserved"
cell land with W7; the org row gets an editor-warning-worthy contract
sentence ("org saves lose unknown iCal properties").

## 2. Sequencing (integrated with B2C)

1. **B2C P3** — vendor todo backends + kind-demux (already planned; gives
   W1/W2 their Google/MSToDo test legs).
2. **VP.a (W8)** — capabilities struct + discovery extensions + static
   reports. Public header; you can wire demo consumption after this.
3. **VP.b (W2)** — per-instance completion rep + BaselineStore
   transactions + producer mappings.
4. **VP.c (W1)** — composite identity + contract doc + matrices.
5. **VP.d (W4)** — completion-anchor canon key + org/CalDAV write-out.
6. **VP.e (W3)** — split mechanics + carrier association.
7. **VP.f (W5+W6.2+W7)** — alarm-shape extension, coercion rules,
   passthrough tests/table finalization.

Each delivered item gets a return receipt per your §4 protocol (exact JSON
keys, public headers/signatures, contracts, test names, deprecations).

## 3. Answers to your open questions (inline)

- **Q1 (W2):** Not guaranteed today (no multi-record transactions in
  BaselineStore — every write is an independent autocommit). We are
  ADDING transaction support in W2's delivery; keep your atomic staging
  meanwhile.
- **Q2 (W4):** Caller (PlanStan) owns advancing the anchor on completion;
  engine never mutates on diff. Derived anchor value is catalogued so the
  differ treats the advance as an ordinary field change.
- **Q3 (W7):** No. OrgBackend re-serializes through a fixed headline/
  drawer mapping; unknown X- properties are lost across an org round
  trip today. Table above; changing that is out of scope unless you tell
  us the editor needs it (it would mean an org-side X-prop drawer
  convention — say the word and we scope it).
- **Q4 (W8):** Static per backend type for Google/MSToDo/local/org;
  discovered for CalDAV (PRODID + report-set probing being added).

## 4. Pin/release expectation

Work begins after B2C P3 lands (same repo, same main). Expect the first
parity-bearing release tag after VP.a; you can hold your pin until the
receipt for the items you actually consume (W8 flags first).
