# Incidence-parity pre-flight audit — 2026-09-02

**Status:** evidence document. **Not** a plan. The plan it feeds is
`PLAN.md` **Amendment 1**; the tracker is `STATUS.md`.

**Commissioned because** the campaign kept discovering defects sideways —
O84 was found while building IP.2's test, not by looking for it. This audit
went looking, once, deliberately, before IP.3 starts.

**Method.** Code first, docs never. Two probe programs, kept in
`probes/` and re-runnable with `probes/run.sh`, measured the real tree at
`40854f3`. Nothing here is inferred from a comment or a status doc.

**Baseline:** `main` @ `40854f3`, suite **214 slots — 210 green, 4 red**
(the known environmental Radicale/KDAV slots; failure text confirms the
diagnosis rather than merely matching the names —
`applyRecordsInFlight_neverRunsNested()` reports *"the requested timeout
(15000 ms) was too short, 29700 ms would have been sufficient"*).

---

## 0. The finding that subsumes the others

> `_canon.kind` is **written in exactly one place**
> (`src/calendar/icalcanonstages.cpp:65`) and **read in exactly one place**
> (`icalcanonstages.cpp:81`). Nothing else in the library knows it exists —
> not the catalogue, not `CanonJsonDiffer`, not `CanonJsonMerger`, not the
> loss profiles, not the engine, not the baseline store, not the identity
> layer.

Verified by exhaustive grep: `CanonEnvelope::kind` and `kindKey()` have
exactly those two call sites outside `canonenvelope.cpp` itself.

Yet that key is the sole determinant of whether a canon record demotes as a
VEVENT, a VTODO, or a VJOURNAL. **O78, O83 and O84 are three symptoms of
that one fact**, and so are three of the six findings this audit adds. The
campaign's existing remedy (structural non-drift, not a copy pass) is the
right one; it is simply not yet aimed at `kind`.

---

## 1. What the suite does not measure

210 passing tests, and not one notices any defect below.

`tests/shape/canonkeycoverage.h` — IP.1's gate — asserts **emitted ⊆
catalogued**: agreement between two of *our own* artifacts. Every defect
this audit found is a disagreement between our emitter and **RFC 5545**.
There is no gate on that axis, which is precisely why these surface
sideways during unrelated work.

**This is the single highest-leverage gap in the campaign.** One gate —
parse a maximal conformant component, promote, demote, diff the property
sets — would have caught items 2.2 through 2.6 simultaneously. It is
Amendment 1's **IP.8**, and it runs first, exactly as IP.1 preceded IP.2.

---

## 2. Measured results

### 2.1 Round-trip property loss (`probes/run.sh`, section 2)

Maximal RFC 5545-conformant component → `{calendar,canon}` → back to iCal.
Properties present in the source and absent from the output:

| Kind | Lost on demote | Canon stable? |
|---|---|---|
| **VEVENT** | `GEO`, `RELATED-TO` | YES |
| **VTODO** | `ATTACH`, `ATTENDEE`, `CLASS`, `COLOR`, `ORGANIZER`, `SEQUENCE`, `URL` — **plus `GEO` corrupted** | **NO** |
| **VJOURNAL** | `ATTACH`, `ATTENDEE`, `EXDATE`, `ORGANIZER`, `RECURRENCE-ID`, `RELATED-TO`, `RRULE` | YES |

**None of these drops is declared in any loss profile.** The EEE doctrine's
"loud about limits" clause is violated for all three kinds.

Two rows deserve emphasis:

- **VTODO `canon-stable=NO`** — promote→demote→promote is not a fixpoint,
  because `GEO` survives the first promote and is destroyed by the demote.
  Everything else in the library assumes canon is a fixpoint.
- **VJOURNAL `RECURRENCE-ID`** — dropping it means a detached journal
  instance and its master **become indistinguishable in canon**. Two
  records collapse onto one uid. This is identity corruption, not field
  loss, and it is the most serious item in the table.

### 2.2 VEVENT alarm trigger forms (O79 — confirmed, exact damage)

`src/calendar/eventcanonfields.cpp:374` reads `startOffset()`
unconditionally. `KCalendarCore::Alarm`'s `hasTime()`/`hasEndOffset()`/
`hasStartOffset()` are mutually exclusive, so:

```
TRIGGER;RELATED=END:-PT5M       →  TRIGGER:PT0S
TRIGGER;VALUE=DATE-TIME:2026…   →  TRIGGER:PT0S
REPEAT:3 / DURATION:PT5M        →  dropped
```

The **VTODO** leg handles all four forms correctly (W5, VP.f). Same
two-file pair, opposite behaviour — the plan's existing IP.4 framing is
confirmed correct, including its insistence that all four call sites move
in one commit.

### 2.3 Alarms come back disabled — NEW (O85)

All four alarm call sites construct `new Alarm(...)` — whose
`enabled()` defaults to **false** — and never call `setEnabled(true)`;
no promote records `enabled()`. Measured round trip:

```
SOURCE enabled=1  →  demoted: X-KDE-KCALCORE-ENABLED:FALSE  →  reparsed: enabled=0
```

Affects VEVENT **and** VTODO — including the legs VP.f's W5 just corrected.
This is the campaign's own thesis demonstrated in miniature: W5 fixed
trigger form on one leg, left the twin leg broken (O79), and neither leg
touched `enabled`. Blast radius is bounded — `X-KDE-KCALCORE-ENABLED` is a
KDE extension that non-KDE clients ignore — but for Akonadi/KOrganizer
round trips the user's reminders silently stop firing.

### 2.4 GEO is corrupted, not dropped — NEW (O86), upstream

Reproduces with **no libkalburator in the picture**
(`probes/kcalendarcore-probe.cpp`, section A):

```
setGeoLatitude(1.5); setGeoLongitude(2.5);   // accessors read back correctly
serialized:  GEO:2.5;<uninitialized bytes>   // latitude slot holds the LONGITUDE
```

Affects `Event` and `Todo` alike in kcalendarcore 6.29.0. We currently
promote `GEO` into canon on the VTODO leg and demote it back, so **we emit
malformed iCal to servers**, with bytes that differ run to run. The VEVENT
leg never promotes `geo` at all, so it merely drops it.

This one is not ours to fix in an emitter; it needs a deliberate decision
(hand-serialize `GEO` bypassing KCalendarCore, or declare it `Dropped` and
stop emitting it). That decision belongs to IP.6 and is stated as an open
question in Amendment 1.

### 2.5 One loss profile serves three kinds — NEW (O88)

`CalendarStockShapes::edges()` registers **one** `canon → ical` edge
carrying `canonToIcalLoss()`. That profile is entirely event-shaped
(`onlineMeeting`, `eventType`, `guestsCan*`, `hideAttendees`, …).

Meanwhile `canonToVjournalLoss()` exists, is defined at
`journalcanonfields.cpp:214`, returns an **empty** profile with the comment
*"VJOURNAL maps its full field-set; no non-reversible loss to declare"* —
and has **zero call sites**. It is dead code, and its comment is false
(§2.1 lists seven drops).

Consequence, measured at `syncengine.cpp:4635`: `materializedLoss()` runs
the *event* profile over a VTODO. A user demoting a VTODO is warned about
`guestsCanModify` and told nothing about losing its `ATTENDEE`s. **The
loss-profile system's unit is the edge; the calendar `ical` encoding is a
union of three schemas.** That is a design-level mismatch, not an oversight.

### 2.6 VTODO has two canonical representations — NEW (O89)

Which one a given VTODO gets is decided by transport metadata:

- `MultiProtocolDavProvider` demuxes into `{todo,canon}` **only** when a
  collection advertises `VTODO` in `supported-calendar-component-set`
  (`multiprotocoldavprovider.cpp:214-226`). Otherwise the "legacy shape"
  sends everything through `{calendar,canon}`.
- `LocalBackend`, `DecSyncBackend`, `OrgBackend` and `AkonadiBackend` each
  declare **only** `{calendar,ical}`. They never demux, under any
  configuration.

So the same VTODO gets the rich `{todo,canon}` path (27-key catalogue, the
thorough `canonToVtodoLoss()`, all of vtodo-parity's W1–W7 work) on a
well-advertising Radicale, and the impoverished `{calendar,canon}` path
(event-shaped loss profile, seven undeclared drops, O84) in a local `.ics`
file. Different catalogue, different differ, different merger, different
loss profile — same user data.

The demux itself is sound: the two views are disjoint (`VEVENT`+`VJOURNAL`
vs `VTODO`), so no record is double-counted. The defect is that the
*fallback* is silent and the non-DAV backends have no path to the good
representation at all.

**This is the finding with consumer-visible consequences**, and the one
question in this audit that libkalburator should not answer alone.

### 2.7 Demote is not a pure function of canon — NEW (O90)

KCalendarCore stamps a heap-address-derived `X-UID` into every serialized
`ATTENDEE`:

```
process A:  ...CUTYPE=INDIVIDUAL;X-UID=93826400444256:mailto:a@example.com
process B:  ...CUTYPE=INDIVIDUAL;X-UID=94004632973840:mailto:a@example.com
```

Stable within a process, different across runs. **Severity is bounded and
should be stated honestly:** the engine's skip cache compares each
backend's own `contentHash` of *stored* bytes (`syncengine.cpp:3700-3712`)
and the differ works on canon, so an unchanged record is not rewritten and
this does **not** cause a write storm today. What it does cost: demoted
bytes are not reproducible across runs, the server accumulates meaningless
per-process identifiers, and any future byte-pin or content-addressed
optimisation on demoted output is impossible. File it, fix it cheaply, do
not dramatise it.

### 2.8 Residual catalogue divergence

IP.2 closed three of seven drifted keys. Still absent from the calendar
catalogue but present in todo's: `checklistItems`, `linkedResources`,
`parentUid`, `sortOrder`. These are vendor-only keys that no iCal emitter
produces, so they are **latent, not live** — but they are exactly the class
IP.3's contributor mechanism must make impossible, and IP.3 should confirm
it does rather than leave four hand-shaped exceptions.

Also confirmed (already known, re-measured): `CanonJsonMerger` takes the
**target's** value for any uncatalogued key
(`canonjsonmerger.cpp:29`, `out = t`) — so catalogue drift is data loss,
not merely differ blindness.

---

## 3. What this audit does *not* claim

Recorded so the next agent does not chase them:

- **Attendees round-trip correctly.** An early revision of this audit
  reported them lost. That was a fixture artifact: libical drops the entire
  `ATTENDEE` property when the mail domain is single-label (`a@x`). Pinned
  in `probes/kcalendarcore-probe.cpp` section B.
- **A second revision reported `ATTENDEE` lost again**, this time because
  the probe parsed folded lines without unfolding. Also fixed, also pinned
  in `probes/README.md`. Two false positives on the same property from two
  different causes is a good reason to keep the probes in-repo.
- **The kind-demux is not double-counting.** The two filtered views are
  disjoint by construction.
- **No new defect was found in `contacts`, `note`, `outline` or `blob`.**
  The scope boundary in PLAN.md §0 holds.
- **The 4 red suite slots are environmental**, re-confirmed by their
  failure text, not by their names.

---

## 4. Findings filed

| ID | Summary | Item |
|---|---|---|
| O85 | VALARM `enabled` lost on every round trip, all four call sites | IP.4 |
| O86 | KCalendarCore 6.29.0 serializes `GEO` corrupt; we emit malformed iCal | IP.6 |
| O87 | VJOURNAL undeclared drops incl. `RECURRENCE-ID` identity aliasing | IP.10 |
| O88 | One edge-level loss profile serves three kinds; `canonToVjournalLoss()` dead | IP.9 |
| O89 | VTODO's canonical representation depends on transport metadata | IP.11 |
| O90 | Demote not a pure function of canon (attendee `X-UID`) | IP.12 |

Full text: `docs/campaign/FINDINGS.md`. Execution: `PLAN.md` Amendment 1.
