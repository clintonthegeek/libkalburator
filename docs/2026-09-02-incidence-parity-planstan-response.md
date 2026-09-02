# Incidence-parity audit — PlanStan's response

**From:** PlanStan, 2026-09-02, at `master` @ `e1856650` (pinned
`PLANSTAN_LIBKALBURATOR_GIT_TAG = v1.01`).
**Re:** `docs/2026-09-02-incidence-parity-planstan-report.md` (libkalburator
@ `40854f3`).
**Answers:** Q1 → **(a) converge**. Q2 → **DTSTART-wins, confirmed.**

Both answers are evidenced below against our tree, not offered as
preferences. Q1 in particular is *not* the "no strong view, take (a)"
non-answer your §4 offers — we have a specific structural reason why (b)
cannot land as a rename, and it is one your report could not have seen from
your side.

---

## Q1 — Converge. (a).

### First, the sub-question: yes, we read `{calendar,canon}` VTODOs

Not as an edge case. It is our **primary and default** task path.

- `~/Documents/todo_work.kalb` — the fixture the entire todo-UX campaign was
  built against — has one logical calendar, `MyList`, bound to
  `{"backendId": "local", "calendarId": "MyList"}`. Per your §4 table, a
  local backend "never demuxes, under any configuration". So every task in
  our reference vault is a `{calendar,canon}` VTODO.
- `~/Documents/Test6.kalb` is a real GTD vault — `Inbox`, `Next Actions`,
  `Waiting For`, `Someday`, `Acquire`, `LoganList`, `TBS` — 5 of
  `CalendarType::Todo` and 2 of `CalendarType::Hybrid`. Every one of the
  seven is **mirrored**, carrying two bindings:
  `["local", "…:multiproto-dav:…:cal:<slug>"]`. So each task list is a
  `{calendar,canon}` VTODO on *both* legs at once.
- Our org backend (`libs/org-io/`) is a task-first surface by construction —
  org `TODO` headlines — and is likewise never demuxed.

So the seven undeclared drops in your row 6 (`ORGANIZER`, `ATTENDEE`,
`SEQUENCE`, `CLASS`, `URL`, `COLOR`, `ATTACH`) have been hitting **the
default path for tasks in this application**, and W1's composite exception
identity — which we asked for and you delivered — is not reaching the vault
our todo work is tested against. That reframes (a) from "the safe default"
to "the fix for the case that actually matters to us." We would like it.

### Why (b) is not a scheduled migration but a blocked one

(b) is the better end state and we agree with that framing. But your §4
sizes it as "if PlanStan enumerates, filters, maps or persists anything by
domain id." We do all four, and there is a harder problem underneath.

**1. Our domain axis is binary and hardcoded.** In two duplicated helpers:

```cpp
// src/controllers/collectioncontroller.cpp:1667
// src/sync/topology/kalbsynctopologydatasource.cpp:269
return col.type == QLatin1String("contacts")
    ? QStringLiteral("contacts") : QStringLiteral("cal");
```

Everything downstream inherits the assumption — `backendId.endsWith(":cal")`
(`synctopologywidget.cpp:710`), `supportsMembership ? "cal" : "contacts"`
(`:4106`, `:4161`), the `":cal:"` segment parse at
`collectioncontroller.cpp:2468`. There is no third-domain concept anywhere in
the graph model.

**2. Domain ids are persisted verbatim in every user vault.** Via
`logicalcalendarjson.cpp:49`. Live example, `Test6.kalb`:

```json
"backendId": "9430e445-…:multiproto-dav:9430e445-…:cal:Acquire"
"backendId": "local"
```

Note the second line: **the local backend's id is the bare string `local`,
with no domain segment at all.** (b) would require minting a domain-qualified
local backend id where today exactly one exists, and rewriting every vault.

**3. The failure mode of a mismatched id is silent, not loud.** Bindings are
looked up by `(backendId, calendarId)` via `findLogicalCalendarByBinding`.
When that lookup misses, `ItemLoadingCoordinator::shouldLoadCalendar()`
treats the calendar as "loose/unbound" and returns **`true`**
(`itemloadingcoordinator.cpp:108-112`). So a stale `:cal` binding after a
domain move does not error — it loads the calendar unfiltered, outside its
logical calendar, with its enabled/visible state ignored.

**4. The decisive one — `Hybrid` breaks the data model, not just the ids.**
`CalendarType::Hybrid` is the *default*, not a rarity:

```cpp
// src/controllers/collectioncontroller.cpp:1707
CalendarType type = CalendarType::Hybrid;
if (options.inferTypeFromCapabilities) { … }
```

`todo_work.kalb`'s `MyList` is `"type": 2` — Hybrid. Under (b), one Hybrid
logical calendar's VEVENTs stay in `cal` while its VTODOs move to `todo`, so
that single LC needs **two primary bindings in two domains**. Our model
cannot express that: an LC shows exactly one binding, because
`shouldLoadCalendar()` returns `false` for every non-primary binding
(`itemloadingcoordinator.cpp:118-`), making secondaries invisible by
construction. So (b) against today's PlanStan yields either two bindings
where one is silently unloaded — i.e. **half of every hybrid calendar
disappears** — or a second logical calendar split off per hybrid, which
changes what the user sees in the sidebar.

Concretely, in `Test6.kalb` that is `Next Actions` and `TBS`: each is Hybrid
*and* mirrored, so (b) would have to split **four** bindings across two
domains for those two rows alone — including the two `local` ones, which have
no domain segment to split on.

That is why we call (b) blocked rather than scheduled: it is gated on a
PlanStan-side change to let a logical calendar hold membership in more than
one domain, which we have not designed and are not proposing now.

### So: (a), and please don't hedge IP.3 / IP.6 toward (b)

Take (a) as ratified so IP.3/IP.6 can aim at a single end state, per your §6.
We are not asking you to keep the `{calendar,canon}` and `{todo,canon}`
representations distinguishable for our benefit — converging them until it
stops mattering which one a task gets is exactly the outcome we want.

If you want (b) eventually, open it as its own cross-repo item and we will
spec the multi-domain LC change on our side first. It should not ride in on
a bug fix — agreed.

---

## Q2 — DTSTART-wins for VEVENT. Confirmed.

### It is not a divergence from W6.2 — it is the same rule

We think the framing in your §4 undersells the consistency, and you were
right to refuse to mirror W6.2 blindly. The unifying rule is:

> **The mandatory temporal anchor wins; the optional derived bound is
> coerced to match it.**

- **VTODO:** `DTSTART` is optional, `DUE` is the semantically primary field —
  a task is defined by its deadline, and plenty of tasks carry `DUE` with no
  `DTSTART` at all. Anchor = `DUE`. → **DUE-wins**, which is what W6.2 does.
- **VEVENT:** the polarity is exactly reversed. `DTSTART` is mandatory,
  `DTEND` is optional, may be replaced by `DURATION`, and is *defined
  relative to* `DTSTART`. Anchor = `DTSTART`. → **DTSTART-wins.**

Same principle, different field, because the two components have opposite
optionality. Recording it that way means the next kind (VJOURNAL, IP.10 —
`DTSTART` only, no bound) falls out of the rule instead of needing a third
decision.

### Why it also has to be DTSTART for us specifically

**A mismatched pair is unrepresentable in our model, so something must win
before it reaches us.** `KCalendarCore::Incidence::allDay()` is one boolean
for the whole incidence — there is no per-endpoint value type. Our editor
models it as a single Absolute field over the whole selection
(`libs/editor/src/datetimemodule.cpp:92-96`) and writes the triple
coherently:

```cpp
// libs/editor/src/datetimemodule.cpp:191-
if (allDay) {
    incidence->setDtStart(start);
    if (… TypeEvent && end.isValid()) …->setDtEnd(end);
    incidence->setAllDay(true);
} else { … }
```

If you hand us a mismatched pair, KCalendarCore collapses it anyway — by
whichever setter ran last, which is not a rule, it is an accident. We would
rather the coercion be yours, declared, and deterministic.

**The user-visible stake is the all-day band.** The common real-world
malformed case is an all-day event from a sloppy producer: `VALUE=DATE`
`DTSTART` with a stray `DATE-TIME` `DTEND`. Under DTEND-wins that event
promotes to *timed* and moves out of the all-day banner into a 00:00 slot in
our day/week views — a visible, wrong relocation of the most common instance
of the bug. DTSTART-wins keeps it where the author meant it.

### The rule we would like, precisely

1. Coerce **`DTEND` to `DTSTART`'s value type**. Never the reverse.
   - `DTSTART` is `DATE`, `DTEND` is `DATE-TIME` → take `DTEND`'s date part.
   - `DTSTART` is `DATE-TIME`, `DTEND` is `DATE` → `DTEND` at `00:00` in
     `DTSTART`'s timezone.
2. If the coerced `DTEND <= DTSTART`, **drop `DTEND`** and let RFC 5545's
   default stand (one day for a `DATE` `DTSTART`; zero duration for a
   `DATE-TIME` one), rather than synthesising a bound.
3. `DURATION` present instead of `DTEND` → nothing to coerce; leave it.

Item 2's degenerate-case handling is our preference, not a requirement — if
your RFC read favours clamping to `DTSTART + 1 day` over dropping, take that
and say so in the contract doc. Items 1 and 3 we would like as written.

A contract doc in the shape of `2026-08-28-w7-passthrough-contract.md` is the
right vehicle; we will read it.

---

## §5 — confirmations, so you can stop flagging these

- **Convergence matrix reshape (IP.9): no-op for us.** We parse and pin
  nothing. `grep -rn "ConvergenceMatrix\|CONVERGENCE-MATRIX" src/ libs/
  tests/` → zero hits. Reshape it freely.
- **New loss warnings: wanted, and no spam risk.** We consume no loss profile
  programmatically (`lossProfile|declaredLoss` → zero hits in `src/`,
  `libs/`), so nothing of ours will start shouting. We agree with your read
  that the undeclared drops in rows 2/3/6 are the contract breach independent
  of the bugs.
- **`geo`: drop it, we don't consume it.** Our only incidence-`geo` reference
  is a display label in the conflict diff table
  (`src/sync/conflictdiffwidget.cpp:325`, `"GEO"` → "Location (Geo)"), which
  degrades to simply never showing a `GEO` row.
  `AppSettings::GeoCoordinate` / `effectiveLocation()` is the *user's own*
  location for locale purposes and is unrelated. Don't hand-serialize around
  an upstream kcalendarcore bug on our account.
- **Alarms (O79/O85): invisible to our UI today, still worth fixing.** We
  have no alarm editing at all — the audit that produced our W1–W8 handoff
  recorded "no VALARM support" and that is still true. But we are a
  passthrough for alarms other clients authored, and "every alarm
  round-trips back disabled" corrupts those. Please treat our lack of UI as
  *zero* reason to deprioritise IP.4.
- **VJOURNAL additive fields (IP.10), alarm `enabled` key (IP.4):** fine
  either way.

---

## One carry-over we owe you, not a request

Your W1 receipt warned that `ConflictInfo.sourceId` / `targetId` may now
carry a composite id (`uid \x01 recurrenceId`) and should be decomposed
before display. We have not done that yet. It bites us in three places:

- `src/sync/conflictdiffwidget.cpp:76` and
  `src/sync/conflictdockwidget.cpp:175` render `sourceId` as the fallback
  label — a raw `\x01` would reach the UI.
- `src/sync/conflictdockwidget.cpp:339-340` passes `sourceId` to
  `showInCalendarRequested(calendarId, sourceId)` as a **lookup key**, which
  will simply miss for an exception record.

Ours to fix, tracked on our side. Noting it so the receipt's warning is
visibly received and you don't have to re-issue it.

---

## Timing

Neither answer is provisional; treat both as ratified and unblock IP.11 and
IP.7b. We are still pinned at **v1.01**, so everything from the vtodo-parity
campaign (W1–W7, all post-`v1.03` and currently untagged) is landed on your
`main` but not yet consumed here. Our adoption pass is a separate piece of
work on our side and does not gate anything in your plan — when you next cut
a tag, we will pick it up then.
