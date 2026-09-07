---
status: ideation — concrete walkthrough
date: 2026-04-30
phase: G (pre-design)
companion-to: 04r-phase-g-shape-pipeline-ideation.md, 04r-phase-g-walkthrough.md
scope: scoped shape-pipeline (no cross-domain edges in stock library)
---

# Phase G — Concrete walkthrough: Bob's HotSync

**Status:** Ideation — concrete walkthrough. Companion to
`04r-phase-g-shape-pipeline-ideation.md` and the prior Alice
walkthrough at `04r-phase-g-walkthrough.md`.

This walkthrough validates the **scoped** shape-pipeline architecture
— the intermediate path between full path A (multi-domain backends,
no transformation registry) and full path G3 (shape-pipeline with
inter-domain edges). The scoped path commits to property catalogues,
edge registry, pipeline compilation, shape-typed differs, `Shape::Any`
sinks, and mapping-keyed baselines, but **no cross-domain edges are
registered in the stock library**. Cross-domain edges are an opt-in
extension, not a default feature.

The Alice walkthrough covered a multi-encoding-within-one-domain
case, plus universal sinks, with one cross-domain touch (Scene 9).
This walkthrough covers what Alice's walk deliberately omitted:

- **Single-occupancy resources** — the Palm device requires exclusive
  access; multiple Palm-DB backends share that resource constraint.
- **Multi-domain in one user setup** — calendar + contacts + memos +
  todos + a sui generis Plucker shape, all in one HotSync session.
- **Concurrent mapping execution under resource constraints** — some
  mappings serialise on the Palm device; others run independently
  (potentially concurrently) against Akonadi / remote services.
- **Trigger-driven syncing** — HotSync isn't a scheduled-poll
  pattern; it fires when the user presses a button.
- **Mid-sync cancellation by physical disconnect** — Bob yanks the
  cradle cable; the F2 cancellation channel has to do its job in
  a multi-mapping context.
- **Cross-Palm-Akonadi-Remote conflict scenarios** — three backends
  in one transitive sync graph.
- **`ISyncHost` shape after the bend** — the original brainstorm's
  open question 6 surfaces here because WildPalms is the consumer
  whose host-side wiring differs most from PlanStan's.

If this walkthrough survives without cross-domain edges, the scoped
path is validated. If it requires cross-domain edges to make sense,
we revisit. Spoiler: it survives.

## Setting

Bob has a Palm Treo 650 he keeps in active use. His Palm contains
five databases:

- `DateBookDB` — calendar, native binary
- `AddressDB` — contacts, native binary
- `MemoDB` — memos, native binary (plaintext with category metadata)
- `ToDoDB` — todos, native binary
- `PluckerDB` — offline-readable web pages, Plucker's own format

Bob's PC runs KDE with Akonadi configured for personal use:

- Personal Calendar collection (Akonadi-internal storage,
  iCal-canonical on read/write)
- Personal Contacts collection (vCard-canonical)
- Personal Notes collection (plaintext-canonical)
- Personal Tasks collection (iCal-vtodo-canonical)

He also has remote services:

- A CalDAV server (FastMail) hosting his calendar
- A CardDAV server (also FastMail) hosting his contacts

He configures **seven mappings**:

| # | Source | Target | Mode | Touches Palm? |
|---|---|---|---|---|
| 1 | Palm-DateBook | Akonadi-Calendar | bidirectional | yes |
| 2 | Palm-Address | Akonadi-Contacts | bidirectional | yes |
| 3 | Palm-Memo | Akonadi-Notes | bidirectional | yes |
| 4 | Palm-ToDo | Akonadi-Tasks | bidirectional | yes |
| 5 | Akonadi-Calendar | CalDAV | bidirectional | no |
| 6 | Akonadi-Contacts | CardDAV | bidirectional | no |
| 7 | Palm-Plucker | `~/Documents/PalmPlucker/` raw-files-backup | one-way mirror | yes |

Mappings 1-4 and 7 require the Palm device. Mappings 5-6 don't.
The walkthrough validates that the engine schedules these correctly.

## Scene 1 — Backend self-declaration with resource IDs

Each backend declares its native shapes and a **resource ID**. The
resource ID is the new addition this walkthrough introduces:

```cpp
class SyncBackend {
public:
    virtual QList<Shape> nativeShapes() const = 0;
    virtual QString resourceId() const;  // new in scoped shape-pipeline
};
```

Default `resourceId()` returns a per-instance unique value
(effectively `QString::number(reinterpret_cast<quintptr>(this))`),
meaning each backend is its own resource. Backends that share an
underlying scarce resource override this:

- `PalmDateBookBackend::resourceId()` → `"palm-device:" + deviceSerial`
- `PalmAddressBackend::resourceId()` → same
- `PalmMemoBackend::resourceId()` → same
- `PalmToDoBackend::resourceId()` → same
- `PalmPluckerBackend::resourceId()` → same
- `AkonadiCalendarBackend::resourceId()` → default
  (per-instance — different collections of Akonadi are different
  instances, but writes to the same instance serialise via
  Akonadi's own Resource locking)
- `CalDAVRemoteBackend::resourceId()` → default per-instance
- `CardDAVRemoteBackend::resourceId()` → default per-instance
- `RawFilesBackend::resourceId()` → default per-instance

Native shape declarations:

- `PalmDateBookBackend` → `{ (calendar, palm-datebook) }`
- `PalmAddressBackend` → `{ (contacts, palm-address) }`
- `PalmMemoBackend` → `{ (memo, palm-memo) }`
- `PalmToDoBackend` → `{ (todo, palm-todo) }`
- `PalmPluckerBackend` → `{ (plucker, palm-plucker) }` — sui generis
- `AkonadiCalendarBackend` → `{ (calendar, ical) }`
- `AkonadiContactsBackend` → `{ (contacts, vcard) }`
- `AkonadiNotesBackend` → `{ (memo, plaintext) }`
- `AkonadiTasksBackend` → `{ (todo, ical-vtodo) }`
- `CalDAVRemoteBackend` → `{ (calendar, ical) }`
- `CardDAVRemoteBackend` → `{ (contacts, vcard) }`
- `RawFilesBackend` → `{ Shape::Any }` (universal sink)

Note `(plucker, palm-plucker)`: Plucker is a *sui generis* shape.
No other backend speaks it; the only sensible target is `Shape::Any`
(raw-files backup or generic-db backup). This is fine — the
architecture accommodates shapes with no cross-shape edges by simply
having no edges in the registry that connect them. They can only be
synced to/from `Shape::Any` sinks.

**Validates:**

- `resourceId()` is a clean opt-in for exclusivity.
- Sui generis shapes don't break the architecture; they just have
  fewer pipeline endpoints.

**Exposes:**

- The Palm device's serial number is needed *at backend
  construction time*. WildPalms's existing `PalmDeviceConnection`
  surfaces this. Fine.
- A backend declaring `resourceId()` for an unconnected device might
  return a stale string. The engine should also gracefully handle
  the device-not-connected case (Scene 6 covers this).

## Scene 2 — Pipeline compilation for all 7 mappings

When Bob clicks "Sync now" or fires HotSync, the engine compiles
pipelines for each mapping. The shape-pipeline registry has these
intra-domain edges registered (no cross-domain in scoped path):

For **calendar**:
- `(calendar, palm-datebook) ↔ (calendar, ical)` — lossy: drops
  attendees (Palm has no concept), attachments, custom X-* properties,
  full RRULE complexity (Palm has limited recurrence). Truncates
  description to 24 chars on the Palm side (the actual DateBookDB
  limitation). Categories map to Palm category slots.
- `(calendar, ical) ↔ (calendar, ical)` — identity.

For **contacts**:
- `(contacts, palm-address) ↔ (contacts, vcard)` — lossy: drops
  vCard custom fields, photo, multiple emails (Palm Address has
  only 5 fields), drops free-form notes longer than ~4KB.
- `(contacts, vcard) ↔ (contacts, vcard)` — identity.

For **memo**:
- `(memo, palm-memo) ↔ (memo, plaintext)` — lossless. Palm's memo
  format is just plaintext with a category. Categories survive as
  iCal CATEGORIES on the Akonadi side.
- `(memo, plaintext) ↔ (memo, plaintext)` — identity.

For **todo**:
- `(todo, palm-todo) ↔ (todo, ical-vtodo)` — lossy: drops attendees,
  attachments, custom X-*. Priority maps 1-5 → Palm's 1-5 (the one
  case where Palm and iCal agree). Due dates map cleanly.
- `(todo, ical-vtodo) ↔ (todo, ical-vtodo)` — identity.

For **plucker**: only edges are `Shape::Any` identity.

Pre-flight loss profiles per mapping:

- **#1 (Palm-DateBook ↔ Akonadi-Calendar):** lossy in both directions
  but more lossy from Akonadi → Palm (description truncation,
  attendee drop, attachment drop, RRULE simplification).
- **#2 (Palm-Address ↔ Akonadi-Contacts):** lossy from Akonadi →
  Palm (custom fields, photo, multi-email).
- **#3 (Palm-Memo ↔ Akonadi-Notes):** lossless.
- **#4 (Palm-ToDo ↔ Akonadi-Tasks):** lossy from Akonadi → Palm
  (attendees, attachments).
- **#5 (Akonadi-Calendar ↔ CalDAV):** lossless. Both `(calendar, ical)`.
- **#6 (Akonadi-Contacts ↔ CardDAV):** lossless. Both `(contacts, vcard)`.
- **#7 (Palm-Plucker → raw-files-backup):** lossless. Identity edge
  via `Shape::Any`.

Bob has already configured these mappings; pre-flight ran during
configuration. He sees a small "ⓘ lossy" indicator next to mappings
1, 2, 4. Mapping 5 and 6 are clean.

**Validates:**

- Multi-domain pipeline compilation works. Each domain has its own
  hub; no cross-domain edges needed.
- Sui generis shape (Plucker) compiles cleanly to `Shape::Any` sink.

**Exposes:**

- Nothing new this scene.

## Scene 3 — HotSync trigger event

Bob places his Palm in the cradle and presses the HotSync button.
The Palm's HotSync protocol negotiates with WildPalms's
`PalmDeviceConnection`, which emits `deviceConnected(serial)`.

WildPalms's `HotSyncCoordinator` (post-rewrite — the artefact that
replaces today's `SyncRunner_wp`) receives the signal. Its job:

1. Identify which `SyncMapping`s are tagged for HotSync triggers
   (or which touch Palm-typed backends matching `serial`).
2. Fire `SyncEngine::runSyncFuture(mappingIds)` with the relevant
   subset.
3. Connect to the resulting `QFuture`'s progress and completion
   signals to drive the UI.

```cpp
class HotSyncCoordinator : public QObject {
    Q_OBJECT
public:
    HotSyncCoordinator(SyncEngine* engine, BackendRegistry* registry,
                       QObject* parent);

private slots:
    void onDeviceConnected(const QString& serial) {
        const auto palmMappingIds = m_registry->mappingsTouchingResource(
            QStringLiteral("palm-device:") + serial);
        auto future = m_engine->runSyncFuture(palmMappingIds);
        m_currentFuture = future;
        // ... connect QFutureWatcher to UI signals ...
    }

    void onDeviceDisconnected() {
        if (m_currentFuture && !m_currentFuture->isFinished()) {
            m_currentFuture->cancel();  // F2 cancellation channel
        }
    }
};
```

In Bob's case, `mappingsTouchingResource("palm-device:abc123")`
returns mappings 1-4 and 7. The future runs all five.

**Critical engine-side change:** `runSyncFuture(mappingIds)` is a
new overload — runs a *subset* of registered mappings, not all.
Today's API has `runSyncFuture()` (all enabled) and
`runSyncFuture(mappingId)` (single). The subset form is needed for
trigger-driven flows where the trigger selects a slice.

Mappings 5 and 6 (Akonadi ↔ Remote) are *not* in the HotSync subset.
They fire on their own schedule (timer-driven, or on-demand from
Bob's "Sync remote services now" UI button), independent of HotSync.

**Validates:**

- Open question 9 (HotSync UX-to-engine mapping): **decided**.
  HotSync is just a trigger that fires `runSyncFuture(mappingIds)`
  with a Palm-resource-filtered subset. Not a special API; not a
  "sync session" abstraction. Just normal mappings selected by a
  trigger.
- The engine grows one new API: `runSyncFuture(QList<MappingId>)`.

**Exposes:**

- WildPalms needs a `BackendRegistry::mappingsTouchingResource(QString)`
  query. Trivial; the registry already maps mappings to backends and
  backends to resources.
- The HotSync UX is a thin wrapper around the engine, not an
  orchestrator. WildPalms's existing `SyncRunner_wp` shrinks to a
  ~50-line `HotSyncCoordinator` plus per-Palm-DB `SyncBackend`
  implementations.

## Scene 4 — Engine scheduling under resource constraints

The engine has been handed 5 mappings (1, 2, 3, 4, 7). It computes
the resource set per mapping:

| Mapping | Source resource | Target resource | Combined |
|---|---|---|---|
| 1 | `palm-device:abc123` | `akonadi-cal-default` | `{palm, akonadi-cal}` |
| 2 | `palm-device:abc123` | `akonadi-contacts-default` | `{palm, akonadi-contacts}` |
| 3 | `palm-device:abc123` | `akonadi-notes-default` | `{palm, akonadi-notes}` |
| 4 | `palm-device:abc123` | `akonadi-tasks-default` | `{palm, akonadi-tasks}` |
| 7 | `palm-device:abc123` | `rawfiles-default` | `{palm, rawfiles}` |

All 5 share `palm-device:abc123`. They form a single connected
component in the resource graph. They serialise.

The engine's scheduler (call it `MappingScheduler`) runs them one
at a time, in mapping registration order: 1, 2, 3, 4, 7.

If Bob *also* fired mappings 5 and 6 simultaneously (e.g., a manual
"sync everything now" button), they'd join the queue. Their resource
sets are `{akonadi-cal, caldav}` and `{akonadi-contacts, carddav}`.
Mapping 5 shares `akonadi-cal` with mapping 1 (also serialises);
mapping 6 shares `akonadi-contacts` with mapping 2 (also serialises).
But mappings 5 and 6 don't share resources with each other, so they
*could* run concurrently if mapping 1 isn't holding `akonadi-cal`
and mapping 2 isn't holding `akonadi-contacts`.

For Bob's HotSync-only flow, none of this matters — only the Palm
group runs.

**Engine pseudo-code for the scheduler:**

```cpp
class MappingScheduler {
public:
    void schedule(const QList<MappingId>& ids) {
        // Build resource → mapping inverse index.
        QHash<QString, QList<MappingId>> resourceUsers;
        for (MappingId id : ids) {
            for (const QString& res : resourcesFor(id)) {
                resourceUsers[res].append(id);
            }
        }
        // Run loop: dispatch ready mappings (no resource conflict
        // with currently-running mappings); wait for completions;
        // dispatch newly-ready mappings.
        // Conservative initial implementation: capacity-1 per
        // resource. Resource pools with capacity > 1 deferred.
    }
};
```

**Initial scoped-path commitment: serial within Palm group; sequential
across groups in registration order.** The MappingScheduler ships
with capacity-1 per resource and a single-active-mapping cap globally
(no cross-group concurrency yet). Concurrent execution of
resource-disjoint mapping groups is a follow-up optimisation that
slots in cleanly because the scheduling primitive is already
resource-aware.

**Validates:**

- Open question 4 (single-occupancy resources): **decided**.
  Per-backend `resourceId()` plus a resource-aware scheduler in the
  engine. No "sync session" abstraction needed; resource sharing is
  a backend property, scheduling is an engine concern.
- Open question 5 (concurrent mapping execution): **decided** with
  caveat. Architecture supports resource-disjoint concurrency;
  initial implementation runs one mapping at a time. Concurrency is
  a future capacity bump.

**Exposes:**

- Capacity-N resources (e.g., a CalDAV server that supports 4
  concurrent connections) are a future optimisation. Initial: 1.
- Cross-mapping cancellation in a queued group: if mapping 1 fails,
  do mappings 2-4 still run? Probably yes (independence by default),
  but with an `abortGroupOnFailure` option per mapping group. Add
  to design-doc scope.

## Scene 5 — First-sync execution: a calendar event walkthrough

Bob's Palm has a meeting "Team standup" with truncated description
"Mon-Fri 10am team check-in". His Akonadi has the same meeting
(synced from CalDAV) but with the full description "Mon-Fri 10am
team check-in. Bring laptop. Standing meeting; aim for 15 minutes."

Mapping 1 runs first (it's first in the queue). It's a first sync —
no baselines.

**Engine walk for "Team standup":**

1. Fetch source: Palm-DateBook returns the event as
   `(calendar, palm-datebook)` bytes — the native record format.
2. Fetch target: Akonadi-Calendar returns the event as
   `(calendar, ical)` bytes.
3. Promote both to canonical `(calendar, ical)`:
   - Palm side: pipeline `(calendar, palm-datebook) → (calendar, ical)`
     produces an iCal VEVENT with description "Mon-Fri 10am team
     check-in" (the truncated form the Palm carries).
   - Akonadi side: identity, already canonical.
4. Diff at canonical:
   - No baseline. First-sync policy says `quickPath: SourceWins` for
     this mapping. Palm is the source; Palm's truncated description
     "wins."
5. Project to both sides:
   - Palm: identity (Palm side already has the truncated form).
   - Akonadi: writes the truncated description back to Akonadi.
6. Update baseline.

**This is a regression** — Bob just lost his full description because
the Palm's truncated copy overwrote the Akonadi version.

The first-sync policy was *wrong* for Bob's situation. He'd want
`AkonadiWins` (the richer side) on first sync, with later syncs
treating any deltas symmetrically.

**Architectural choice:** the *richness rank* the ideation doc
proposed is one mechanism for resolving this — domain plugins
declare a partial richness order over their shapes. For calendar:
`(calendar, ical) > (calendar, palm-datebook)` because ical is
strictly more expressive. First-sync policy can default to "richer
side wins" rather than "source wins."

But richness rank was deferred for cross-domain only. We need it
intra-domain too.

**Decision:** **richness rank is intra-domain too.** Domain plugins
declare per-domain shape ordering. First-sync `quickPath` policy can
opt into `RicherSideWins` as the default for bidirectional mappings
where the shapes differ. For Bob's mapping #1, that means Akonadi
wins first sync; the truncated Palm description is replaced with
the full Akonadi description (then projected back to Palm,
re-truncating to 24 chars on write — but the Akonadi side keeps
the full version).

**Validates:**

- Richness rank is needed for intra-domain too, not just cross-domain.
- First-sync policy benefits from a `RicherSideWins` option.
- The walkthrough is exposing real design tension — the kind that
  paper exercises are meant to surface.

**Exposes:**

- The cycle "Palm holds truncated → Akonadi receives truncated → on
  next sync Bob re-edits the truncated Akonadi description back to
  full → next Palm sync re-truncates" needs to be considered. Each
  sync round Bob "loses" the rich form on Akonadi if the truncation
  came from Palm. **Mitigation:** baseline stores the canonical-shape
  (richer) form. Diff detects "Palm side has truncated_X; baseline
  has full_X; this is *not a delta* — Palm is just incapable of
  representing full_X." Encode this as: when computing a diff, drop
  fields that the source's shape literally cannot represent
  (per the loss profile of the source's reverse projection edge).
  The diff is only over fields the source *can* express. Promoted
  to design-doc scope.

This is a meaningful catch. Without it, every Palm sync would
gradually erase rich Akonadi data.

## Scene 6 — Mid-sync cancellation by physical disconnect

Mapping 2 (Palm-Address ↔ Akonadi-Contacts) is mid-flight.
PalmAddressBackend has fetched 200 records; the engine is applying
them to Akonadi when Bob accidentally bumps the cradle and the USB
connection drops.

`PalmDeviceConnection` emits `deviceDisconnected(serial)`.
`HotSyncCoordinator::onDeviceDisconnected` fires
`m_currentFuture->cancel()`.

The F2 cancellation channel propagates:

1. `QFutureWatcher::canceled` fires on the engine thread.
2. `SyncEngine::onCancelObserved()` posts to worker.
3. `SyncEngineWorker::observeCancel()` sets `m_cancelled = true`.
4. The worker's nested `QEventLoop` (in `await<Op>`) wakes; the
   in-flight `PushOperation` is told to cancel. The Akonadi-side
   apply stops.
5. The mapping completes with `cancelled=true, skipped=true` in its
   `SyncResult`.
6. The scheduler observes mapping 2's cancelled completion and
   *halts the queue* — subsequent Palm-touching mappings (3, 4, 7)
   never start.
7. The future resolves with results for {1: success, 2: cancelled,
   3-4-7: not-run}.

**Architectural question:** when one mapping in a queue is cancelled
because the device disconnected, should the rest of the queue *also*
be cancelled (because they too need the device that just left)?

**Decision:** **yes**. Each mapping's preflight asserts its required
resources are available; if a mapping later in the queue requires
`palm-device:abc123` and the engine knows that resource is gone
(from the cancellation cause), it short-circuits with
`cancelled=true, errorMessage="device disconnected"`.

This requires a slight enrichment of the cancellation channel: a
*reason*. The current F2 channel is binary (cancelled / not). For
this case the engine wants to know "cancelled because device gone"
vs "cancelled because user pressed Stop." When the device-gone form,
the scheduler aborts the whole queue; when the user-Stop form, only
the in-flight mapping is cancelled and queued mappings continue at
the user's discretion (probably also cancel, but distinguishably).

```cpp
enum class CancellationReason {
    UserRequested,
    ResourceLost,
    Timeout,
    UnrecoverableError,
};
```

The cancellation reason rides on `QFuture::cancel()` via a
companion `QHash<QFutureInterfaceBase*, CancellationReason>`-style
side-channel, or via a `cancelWithReason(CancellationReason)`
method on a `SyncEngineFuture` wrapper around `QFuture`.

**Decision (provisional):** introduce `SyncEngineFuture` as a thin
wrapper around `QFuture<QList<SyncResult>>` that adds
`cancelWithReason(CancellationReason)`. Existing `QFuture::cancel()`
maps to `UserRequested`. For internal use the engine and the host
can use the richer form.

**Validates:**

- Cancellation generalises cleanly to multi-mapping with resource
  awareness.
- The F2 channel stays load-bearing; no new architecture, just a
  reason annotation.

**Exposes:**

- `SyncEngineFuture` is a small new type. Acceptable.
- The "scheduler aborts queue on resource-lost cancellation" rule is
  another architectural decision; document it.

## Scene 7 — Three-way conflict: Palm + Akonadi + CalDAV

Bob has an event "Dentist" originally created in Akonadi. It synced
to CalDAV via mapping 5 and to Palm via mapping 1. So all three
have it. Then:

- Bob edits it on his Palm: changes time from 14:00 to 15:00.
- Bob edits it in Akonadi via the desktop UI: changes location from
  "Main St" to "Oak St".
- Bob edits it via the CalDAV web UI: adds "Bring x-rays" to the
  description.

Then he HotSyncs. The Palm-touching subset (mappings 1-4, 7) runs.
Mappings 5 and 6 don't run (they're not HotSync-triggered).

**Mapping 1 — Palm-DateBook ↔ Akonadi-Calendar:**

- Palm side: time 15:00, location "Main St" (unchanged since last
  sync), description (truncated form of Akonadi's then-current).
- Akonadi side: time 14:00, location "Oak St", description
  unchanged from CalDAV's then-current ("Bring x-rays" not yet
  pulled because mapping 5 hasn't run).
- Wait — actually Akonadi's description doesn't have "Bring x-rays"
  *unless* mapping 5 ran since CalDAV's edit. Which it didn't, in
  this HotSync. So Akonadi has the *pre-edit* description.
- Promote both to canonical. Diff vs baseline.
- Time: Palm modified (14:00→15:00); Akonadi unchanged. Take Palm.
- Location: Palm unchanged; Akonadi modified. Take Akonadi.
- Description: both unchanged from baseline. Take baseline.
- Merge: time 15:00, location "Oak St", description from baseline.
- Project both sides. Akonadi gets time 15:00. Palm gets location
  "Oak St" (within its 30-char limit, fine).
- Update baseline.

**Mapping 5 (Akonadi-CalDAV) doesn't run during HotSync.**

Bob clicks "Sync remote" later. Mapping 5 runs:

- Akonadi side: time 15:00, location "Oak St", description
  unchanged (baseline).
- CalDAV side: time 14:00 (CalDAV doesn't know about Bob's Palm
  edit), location "Main St" (CalDAV doesn't know about Bob's
  Akonadi edit), description "...Bring x-rays" (CalDAV-side edit).
- Promote both to canonical (no-op; both `(calendar, ical)`).
- Diff vs baseline.
- Time: Akonadi modified (baseline 14:00 → now 15:00); CalDAV
  unchanged from baseline. Take Akonadi: 15:00.
- Location: Akonadi modified; CalDAV unchanged. Take Akonadi:
  "Oak St".
- Description: Akonadi unchanged; CalDAV modified. Take CalDAV:
  "...Bring x-rays".
- Merge: time 15:00, location "Oak St", description with x-rays.
- Project both sides. Akonadi gets the x-ray description. CalDAV
  gets the time and location updates.

**Final state after Bob's two-step sync (HotSync, then remote-sync):**

- Akonadi: time 15:00, location "Oak St", description with x-rays.
- CalDAV: time 15:00, location "Oak St", description with x-rays.
- Palm: time 15:00, location "Oak St", description (truncated from
  Akonadi's, now including x-ray text within its 24 chars).

All three converged correctly across the *two-step* flow.
Pairwise mappings + per-mapping baselines + the "drop unrepresentable
fields from source diff" rule (Scene 5's late addition) work.

**The artefact from the Alice walkthrough's Scene 7 doesn't bite
here** because each pair of edits was orthogonal — Palm changed
time, Akonadi changed location, CalDAV changed description. None
collided.

**If Bob had edited the same field on all three sides** — say, time
15:00 on Palm, time 16:00 in Akonadi, time 17:00 on CalDAV — we'd
hit the Alice Scene 7 artefact: mapping 1 picks Palm-vs-Akonadi
winner (15:00 vs 16:00, last-write-wins or AskUser), then mapping 5
picks Akonadi-vs-CalDAV (whatever-mapping-1-decided vs 17:00). Final
state depends on resolution order. This is the documented limitation
of pairwise sync, deferred to the future "mapping groups" feature.
Same answer as before; this walkthrough doesn't surface anything new
for that case.

**Validates:**

- Multi-backend transitive propagation works for orthogonal edits
  across three backends.
- Per-mapping baselines correctly handle "another mapping hasn't run
  yet" — the baseline is local to that mapping pair, so it doesn't
  see the other mapping's deltas until both mappings have run.

**Exposes:**

- Nothing new beyond what Alice already exposed.

## Scene 8 — `ISyncHost` shape after the bend

WildPalms and PlanStan both implement `ISyncHost` today. The
interface today is calendar-shaped:

```cpp
class ISyncHost {
public:
    virtual ICalendarCollection* calendarFor(const QString&) = 0;
    virtual void emitTranscodingWarning(...) = 0;
    virtual void incidenceCreated(...) = 0;
    virtual void incidenceUpdated(...) = 0;
    virtual void incidenceDeleted(...) = 0;
    // ... ~15 more calendar-typed methods ...
};
```

In the scoped shape-pipeline, the engine doesn't make per-domain
decisions — it just runs pipelines. The host's role narrows. Walking
through what each existing `ISyncHost` method needs to become:

- `calendarFor(id)` → engine doesn't need this; it asks backends
  directly.
- `emitTranscodingWarning(...)` → replaced by per-mapping loss
  profile reporting (already established in Scene 3 above).
- `incidence{Created,Updated,Deleted}` → replaced by generic
  `recordChanged(mappingId, recordId, ChangeKind)` — the host
  doesn't need to know the records are incidences.
- The remaining methods are mostly progress/status callbacks that
  generalise to `progressChanged(mappingId, current, total, msg)` etc.

So `ISyncHost` post-bend looks like:

```cpp
class ISyncHost {
public:
    // Lifecycle
    virtual void syncStarted(MappingId, Pipeline::LossProfile);
    virtual void syncFinished(MappingId, SyncResult);

    // Per-record events
    virtual void recordChanged(MappingId, RecordId, ChangeKind);

    // Conflict resolution (only fired for AskUser policies)
    virtual ConflictResolution resolveConflict(MappingId, RecordId,
                                                CanonicalRecord src,
                                                CanonicalRecord tgt,
                                                CanonicalRecord baseline);

    // Progress
    virtual void progressChanged(MappingId, int current, int total,
                                  QString msg);
    virtual void phaseChanged(MappingId, Phase);

    // Errors
    virtual void errorOccurred(MappingId, QString msg);
};
```

That's about a third the size, generic, and the same interface is
implementable by both PlanStan (as `PlanStanSyncHost`) and WildPalms
(as `WildPalmsSyncHost`). The calendar-typed methods that used to be
on `ISyncHost` move into per-domain consumer logic; e.g., PlanStan's
consumer logic now calls KCalendarCore directly when it gets a
`recordChanged` for a calendar mapping, parsing the bytes itself.

This **answers open question 6 from the original brainstorm**:
ISyncHost dissolves into a generic event sink. Calendar-shaped
methods retire; consumers do their own per-domain handling on the
record-changed events.

**Validates:**

- Open question 6 (ISyncHost shape after the bend): **decided**.
  Generic event sink with per-record callbacks; consumers own the
  per-domain logic.
- The interface is small enough to be a stable contract.

**Exposes:**

- Existing PlanStan code that uses `ISyncHost::calendarFor(id)`
  needs to migrate to direct backend calls. Mechanical change.
- `CanonicalRecord` is a new value type — basically `BackendRecord`
  with the canonical shape attached. Minor.

## Scene 9 — WildPalms UX surface

Pre-rewrite (today), Bob's experience:

- Plug Palm into cradle, press HotSync button.
- WildPalms tray icon shows progress as each Palm DB conduit runs.
- `SyncRunner_wp::run(SyncMode::TwoWay)` iterates 6 plugins (the V2
  plugin set), calling `engine.runBlobTwoWay(...)` per plugin.
- Each plugin produces a `Sync::SyncResult`; aggregated in
  `SyncRunner_wp` and emitted via `finished(SyncResult)`.
- UI displays per-plugin progress (from emitted progress signals)
  and per-plugin results (from finished-aggregate).

Post-rewrite, Bob's experience:

- Plug Palm into cradle, press HotSync button.
- WildPalms tray icon shows progress as each mapping runs.
- `HotSyncCoordinator::onDeviceConnected` fires
  `engine.runSyncFuture(palmMappingIds)`.
- The engine's progress signals fire per-mapping; UI displays them.
- The future resolves with `QList<SyncResult>`; UI displays the
  per-mapping results.

**Equivalent UX, simpler internals.** The mapping abstraction also
gives Bob a UX feature he didn't have before: he can configure which
Palm DBs to sync (in case he doesn't want, say, contacts to round-
trip through Akonadi). Each mapping can be enabled/disabled
independently. WildPalms's Sync menu/dialog grows mapping toggles.

The **bigger UX gain** is the *non-Palm* mappings (5 and 6). Today
WildPalms doesn't coordinate Akonadi ↔ remote at all. Post-rewrite,
those mappings are first-class — Bob configures them once, fires
them on a timer or from a "Sync remote" button. The multi-PIM
coordination value the user articulated earlier ("Bob can have
three-way, four-way sync within Wild Palms for free") materialises
naturally because mappings 5 and 6 are just normal mappings.

**Validates:**

- WildPalms's UX maps cleanly onto the engine. No special HotSync
  API needed; the trigger fires `runSyncFuture(subset)`.
- The "WildPalms gains PlanStan's features for free" goal is
  realised: same engine, same mapping registry, configurable through
  the same UI patterns.

**Exposes:**

- WildPalms's existing per-plugin progress UI assumes plugins as the
  unit of granularity. New: mappings are the unit. Minor UX
  refactor in WildPalms.
- The `SyncProgressManager` PlanStan uses (already QFuture-aware
  post-F2) is reusable in WildPalms with minor namespace adjustments.
  Possible future: lift it into a shared `libkalburator-qtwidgets`
  sibling library.

## What graduated; what's still ideation

**Decisions taken inline (graduate to design doc):**

- **`SyncBackend::resourceId()`** — backends declare a string
  resource ID; default unique per-instance; Palm backends share.
  (Scene 1)
- **MappingScheduler is resource-aware** with capacity-1 per
  resource initially; concurrent disjoint-resource execution as
  future capacity bump. (Scene 4)
- **`runSyncFuture(QList<MappingId>)`** subset-form added to engine.
  (Scene 3)
- **Richness rank is intra-domain too**, not just cross-domain.
  Domain plugins declare a per-shape partial order. (Scene 5)
- **First-sync policy supports `RicherSideWins`** as a default
  option. (Scene 5)
- **Diff drops source fields the source's shape can't represent.**
  This prevents the "Palm truncation gradually erases Akonadi rich
  data" cycle. (Scene 5)
- **Cancellation has a `CancellationReason`** (UserRequested /
  ResourceLost / Timeout / UnrecoverableError). The scheduler
  short-circuits queued mappings when the cancellation reason is
  ResourceLost and they share the lost resource. (Scene 6)
- **`SyncEngineFuture` wrapper** for `QFuture<QList<SyncResult>>`
  with `cancelWithReason(...)`. (Scene 6)
- **`ISyncHost` becomes a generic event sink** with per-record
  callbacks; calendar-typed methods retire. (Scene 8)
- **`HotSyncCoordinator`** in WildPalms is a ~50-line trigger-to-
  engine adapter; replaces `SyncRunner_wp`. (Scenes 3, 9)

**Still open (deferred to design doc):**

- Capacity-N resource pools (e.g., a remote service with concurrent
  connection limit > 1).
- `abortGroupOnFailure` per mapping group.
- `BackendRegistry::mappingsTouchingResource(QString)` query API.
- Cross-mapping error semantics in queued execution.

**Architectural break-glass triggers — none hit in this walk.**

The two real catches were:

1. **First-sync policy default needed updating** (Scene 5) — Palm's
   truncation overwrote Akonadi's rich form. Fixed inline by adding
   richness rank and `RicherSideWins`.
2. **Diff needed to ignore source-shape-unrepresentable fields**
   (Scene 5) — without this, every Palm sync slowly erases Akonadi.
   Fixed inline.

Both are real catches. Both are clean fixes. Both feel like the kind
of thing a design pass should surface — the walkthrough is doing its
job.

**Cross-domain edges remain unregistered throughout this walk.** The
scoped shape-pipeline architecture covers Bob's full multi-PIM
HotSync flow without invoking cross-domain projection. The scoped
path is validated.

## Recommendation

**Graduate to design.** Both walkthroughs (Alice + Bob) survive
without architectural break-glass. The scoped shape-pipeline
architecture is the right destination. 22 inline decisions across
the two walkthroughs are now ready to feed into a `04r-phase-g-design.md`.

Two follow-up walkthroughs were queued from the Alice walk; the
WildPalms walk just retired one of them (HotSync). The remaining
follow-up:

- **Migration walkthrough** — concrete slicing of: ~8000 lines of
  test moves PlanStan→libkalburator; deprecated synchronous I/O
  retirement; F1 facade retirement; SyncRunner_wp dissolution into
  HotSyncCoordinator + per-Palm-DB SyncBackends; mapping-keyed
  baseline migration; introduction of property catalogues and edge
  registry. Probably 6-10 phases. **This is the implementation
  ordering question** — what lands first, what depends on what,
  what's safe to ship behind the deprecation-with-overlap pattern.

After that walkthrough, design doc, then plan doc, then code.

## Cross-references

- `04r-phase-g-shape-pipeline-ideation.md` — architectural
  exploration this walkthrough validates against the Palm consumer
- `04r-phase-g-walkthrough.md` — Alice's todos walk (intra-domain
  multi-encoding + universal sinks + light cross-domain touch)
- `04q-phase-f2-threading-outcome.md` — F2 cancellation channel
  used in Scene 6
- `~/dev/refactor-engine-merger/CURRENT-STATUS.md` — Phase G design
  exploration in flight (path D, scoped shape-pipeline)
- `~/dev/refactor-engine-merger/ROADMAP.md` — original Phase G
  framing
