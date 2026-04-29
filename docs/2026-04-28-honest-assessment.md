# libkalburator — honest assessment, 2026-04-28

A read-through of the whole tree (calendar + blob + conflict + journal +
transcoding + types) plus how PlanStan and Wild Palms actually consume
it. Written as a sanity check, not a proposal — though it ends with a
short prioritized list.

## TL;DR

It's a **calendar sync library, two-thirds extracted from PlanStan,
with a separate clean blob-sync sub-library growing parallel to it for
non-calendar Palm data.** The blob piece is *not* shoehorned into the
calendar piece — it's beside it, deliberately disconnected (the `╳`
in `04h-blob-layer-design.md`'s own diagram), awaiting a planned merger
that hasn't happened yet.

What feels janky is mostly four things, all of which have plausible
fixes inside the existing phase plan:
1. Half-finished rename and layering (Phase C work, partially landed)
2. An over-broad `ISyncHost` interface that pushes too much onto consumers
3. A god-interface (`SyncBackend`, ~600 LOC of pure virtuals + defaults)
   and a god-store (`SyncStore`, eight different concerns)
4. Debug back-doors and host-specific naming leaking into the public API

It does not deserve a rewrite. It does deserve focused retooling.

## Identity: what is it really?

Not a general sync system. The shape on disk is two parallel libraries
sharing a type vocabulary:

```
calendar/  (~17,800 LOC)   blob/  (~600 LOC)
─────────────────────       ────────────
ISyncHost                   IBlobBackend
SyncBackend (god-iface)     BlobSyncEngine
SyncCoordinator             BlobBaselineStore
SyncWorker                  (LocalBlobBackend, MockBlobBackend)
CalendarManager
SyncStore (god-store)
LocalBackend, RemoteBackend (CalDAV, 2.5k LOC),
OrgBackend, AkonadiBackend, DecSync*,
SubscriptionBackend, HolidaySubscriptionBackend,
IcsFeedFetcher
+ transcoding, journal, conflict, discovery
─────────────────────       ────────────
KCalendarCore::Incidence    QByteArray + metadata
```

The calendar layer is the original PlanStan extraction. The blob layer
is three weeks old (Phase B2–B4, April 2026), explicitly added to give
Wild Palms a foundation for Palm contacts/memos/todos — domains where
forcing data through KCalendarCore would have been ridiculous. So:

- **Both consumers are real.** PlanStan implements the upper-layer
  contracts (`ISyncHost`, `ISyncConfigStore`, `ICalendarCollection`)
  via `CollectionController` etc. Wild Palms implements them too via
  `SyncHost_WP` etc. for its Full-Sync-Mode.
- **WP also implements `IBlobBackend`** in plugin classes
  (`ContactsBlobBackend`, `TodoBlobBackend`, etc.) for the non-calendar
  Palm databases. Those are blob-layer-only.
- **The bridge between layers does not exist yet.** The plan is for
  upper-layer backends to expose `blobBackend()` views and the upper
  engine to compose `BlobSyncEngine` underneath, but none of that is
  wired. Today the blob engine drives nothing in PlanStan, and in WP it
  drives only the non-calendar plugins.

So the honest single-line description is: *"the PlanStan calendar sync
core, plus a not-yet-integrated generic blob sync engine."* Calling it
a "general sync system" overpromises. Calling it "a calendar sync
library with a weird extra schema shoehorned in" undersells the blob
layer — it's clean and well-shaped on its own; it just hasn't been
married to the calendar layer yet.

## Maturity

### Calendar layer — mature in production, mid-renovation in shape

It's battle-tested in PlanStan and shipping. But the code reads as
"freshly extracted yesterday and not yet relaxed into its new home":

- `SyncBackend` is comment-marked for a future rename to
  `ICalendarBackend`; `SyncCoordinator` for `CalendarSyncCoordinator`.
- Several `[[deprecated]]` markers (e.g. `loadItems` superseded by
  `fetchItems`).
- `KalbConfigManager` was renamed to `ISyncConfigStore` but the
  motion is visible in adjacent code and docs.
- `// Future: SyncRouter and qsynccore integration` comments in
  production headers.
- Hardcoded filename `.planstan-sync.db` (in `SyncStore` and
  `BlobBaselineStore`). WP and any future host gets that name.
- `synctypes.h` carries a "Designed for future extraction to qsynccore
  shared library" note — and a `SyncContext` with `// Future:` member
  comments. That future is now (this *is* the library) but the comments
  haven't caught up.
- "Legacy terminology (backward compatibility)" branches in
  `conflictResolutionFromString` (`local-wins`/`remote-wins` →
  `SourceWins`/`TargetWins`). Real on-disk data in the wild.
- Sub-namespace `Kalburator::Sync::QSyncCore` exists for
  ConflictHandlerRegistry/etc. — a transitional namespace from when
  those files were lifted from WP's `qsynccore/`.

None of these are bugs. They're the texture of recently-relocated code.

### Blob layer — young but clean

Three weeks old. Phase B2 (mirror + twoWayNaive), B3 (BlobBaselineStore),
B4 (twoWayWithBaseline + ConflictStore wiring) all landed. Has the
library's only owned tests (3 test executables in `tests/blob/`).
Interface is tight (~12 methods on `IBlobBackend`, mostly pure or
default-no-op), value-returning, uses `std::optional`. The deferral list
in `04h-blob-layer-design.md` § "Explicitly deferred" is honest about
what's still missing. WP's plugin code is consuming `IBlobBackend`
shape (verified — `contactsblobbackend.h`, `todoblobbackend.cpp`,
etc.) so it's not theoretical. What I cannot verify from source alone
is whether `twoWayWithBaseline` has been driven against a real
multi-thousand-record Palm dataset end-to-end yet; that would be the
next maturity gate.

## Where it does what a library should

- **Type vocabulary** (`BackendRecord`, `CollectionInfo`,
  `LogicalCalendar`, `SyncMapping`, `ConflictInfo`, `SyncStats`,
  `BackendCapabilities`) is genuinely good — small, well-named,
  serializable, easy to reason about.
- **Concrete calendar backends** are real reusable assets. CalDAV alone
  (RemoteBackend, 2.5k LOC) is the kind of code you do not want to
  re-extract into another project.
- **Conflict policy framework** (ConflictPolicy + ConflictHandler +
  ConflictStore + Registry) is well-thought and used by both layers.
- **Transcoding system** (PropertyTranscoder, RruleTranscoder) is real
  domain code that earns its keep — backend-capability-aware lossy
  recurrence handling is exactly the kind of thing that belongs in a
  shared library.
- **Two-target CMake split** (`Kalburator::Types` vs `Kalburator::Sync`)
  with the `KALBURATOR_PROVIDE_TYPES=OFF` toggle for hosts that already
  have the type vocabulary is a thoughtful detail.
- **The blob layer interface** itself, considered in isolation, is what
  a small library should look like.

## Where it leaves consumers doing too much

This is where the strain shows. Specific cases:

1. **`ISyncHost` is a ~10-method contract** the host must implement
   end-to-end. Backend lookup, calendar collection access, incidence
   registry, incidence source, locale source, config store, three
   `applyIncidenceXxx` mutators, calendar unload, and *sync-mapping
   regeneration*. Both consumers wrote glue classes
   (`CollectionController` in PlanStan, `SyncHost_WP` in Wild Palms)
   to satisfy it. The library is asking "give me ten callbacks" when
   it could be asking "give me the data and I'll do the bookkeeping."
   A default in-library implementation covering most of these — backed
   by a small `IIncidenceModel` that the host plugs in — would shrink
   each consumer's host-glue from hundreds of lines to dozens.

2. **`applyIncidenceAddition(..., bool stageForSync = true)`**.
   The host interface exposes the library's own staging concept as
   a parameter the host has to know about. Either the staging is the
   library's business and the parameter shouldn't be in the host
   interface, or the host genuinely needs to control it and the
   default should not be `true` (it's the wrong default for any
   non-sync-driven mutation).

3. **Two parallel CRUD entry points** — `CalendarManager::createIncidence`
   (immediate, sync to all bindings) and `ISyncHost::applyIncidenceAddition`
   (sync→model). PlanStan uses both. The library has a "staged for
   sync" model and an "immediate, all backends" model coexisting,
   and consumers have to understand both.

4. **Three async patterns coexist** in `SyncBackend`:
   - Synchronous-with-pointer-returning: `loadItems(MemoryCalendar*, bool)`
   - Operation-handle-based: `fetchItems()` → `FetchOperation*`,
     `pushItems()` → `PushOperation*`
   - Signal-based streaming: `fetchStarted` / `itemFetched` /
     `fetchFinished`
   `loadItems` is `[[deprecated]]` but still pure-virtual in the
   abstract base, so every backend implements it. Consumers see all
   three styles.

5. **`SyncBackend` is a ~600-LOC god-interface.** Three or four
   distinct interfaces are trying to escape:
   - Backend identity + capabilities + collection discovery
   - Incidence I/O (CRUD)
   - Calendar-level lifecycle CRUD (create/rename/delete calendar)
   - Backend-property accessors (color, description, file path)
   - Debug back-doors (`getRawIcs`, `setRawIcs`)
   The presence of debug methods on the production interface is a tell.
   They should be a separate optional `IDebugBackend` aspect.

6. **`SyncStore` is a god-store** — version hashes, baselines (iCal
   text), last-sync-time, property baselines, CTags, local fingerprints,
   conflicts, plus delegated identity mappings. Eight distinct concerns
   in one SQLite class. Phase C planning already calls for splitting
   this; until that lands, consumers see one big storage thing they
   don't have a clean model of, and the schema is denormalized across
   concerns.

7. **`.planstan-sync.db` filename** is hardcoded inside the library —
   in the design doc *and* live code paths. Wild Palms gets that
   filename today. It should be configurable or library-named.

8. **Raw `KCalendarCore::MemoryCalendar*` is part of the public
   surface.** `ICalendarCollection::calendars()` returns a list of
   raw pointers; backends accept raw `MemoryCalendar*` to load into.
   Ownership semantics are doc-comments rather than types. This is the
   single biggest "leaked abstraction" I see — the host ends up owning
   calendar instances and threading them through, when the library
   could keep them internal.

9. **Two of every backend type, no bridge.** `LocalBackend`
   (calendar) and `LocalBlobBackend` (blob); `MockBackend` and
   `MockBlobBackend`. Each one solves the same problem at a different
   layer. The promised bridge — calendar backends exposing a
   `blobBackend()` view, calendar engine composing the blob engine —
   has not been built. The longer that goes, the more "two parallel
   libraries" hardens into the de facto shape.

10. **`DataDomain { Calendar, Project }` enum.** A small smell. It's a
    hint that PlanStan's "Project" tasks were forced into the
    calendar-shaped incidence pipeline. Either the upper layer is
    *truly* incidence-shaped (and Project should be modeled as
    incidences without apology) or it's actually a more general "typed
    record" pipeline that wants a different name.

## Sane API and boundary?

| Concern                  | Lower (blob) | Upper (calendar) |
|--------------------------|--------------|------------------|
| Interface size           | small        | bloated          |
| Ownership semantics      | values, optional | raw pointers + comments |
| Async story              | sync (deliberate) | three patterns coexisting |
| Host contract weight     | none (engine doesn't know about hosts) | heavy ISyncHost |
| Naming consistency       | clean        | transitional debris |
| Testability              | tested       | not (tests in PlanStan) |
| Composability with the other layer | n/a yet | n/a yet |

The blob layer is what a library should look like at its size. The
calendar layer is what a library looks like *while* it's being
extracted — most of the moving pieces are right, but the seams are
still showing.

## Does it deserve a rewrite or a retooling?

**Not a rewrite.** Concretely:
- The architecture is mostly right (layered with shared vocabulary).
- The conflict framework is well-thought and reused across both layers.
- The transcoding/diff/baseline machinery is real and would be
  expensive to reproduce.
- Both consumers compile and ship against it.
- Most of what I'd call out is *already in the project's planning
  documents* — Phase C explicitly proposes splitting `SyncStore`,
  renaming `SyncBackend` to `ICalendarBackend`, layering directories.

**Yes a retooling.** The phase plan covers a lot of what I'd want, but
not all of it. Things I'd add to the existing roadmap:

1. **Build the calendar↔blob bridge.** Without it, the two layers stay
   parallel and you keep paying the "two of every backend" tax.
2. **Slim `ISyncHost`** by providing a default in-library implementation
   (or better, an `AbstractSyncHost` base with the bookkeeping built
   in) so consumers implement 3–4 callbacks rather than ~10.
3. **Split `SyncBackend`** into smaller interfaces:
   `IBackendIdentity`, `IIncidenceBackend`, `ICalendarLifecycle`,
   optional `IDebugBackend`. Most concrete backends implement all
   three; some won't need the lifecycle one.
4. **Move debug methods** (`getRawIcs`/`setRawIcs`) off the production
   interface entirely, behind an opt-in.
5. **Internalize staging.** Remove `bool stageForSync` from the host
   interface — the library should manage staging itself.
6. **Fix the filename** — `.planstan-sync.db` → configurable, with a
   migration window.
7. **Decide what `DataDomain` actually is.** If "Project" is just an
   incidence subtype, drop the enum. If it's a different domain, the
   incidence pipeline isn't the right vehicle for it.
8. **`MemoryCalendar*`** ownership — make the library own its
   calendars, expose a const view to hosts. Lower priority but a real
   ergonomic win when it lands.

## A short prioritized list, if you wanted one

1. **Bridge the two layers.** Without this the rest is decoration.
2. **Carve up `SyncStore`** (Phase C already plans this). Resolves the
   god-store. Unblocks #1.
3. **Slim `ISyncHost`** with a default in-library implementation.
   Single biggest ergonomic improvement for consumers.
4. **Split `SyncBackend`** into focused sub-interfaces.
5. **Move debug surface, fix filename, retire `DataDomain`.**
   Cosmetic but each one removes a "weird" question every new reader
   asks.

Items 1–3 are the load-bearing ones. 4–5 are polish. None of this is
a rewrite; it's a coherent next two phases of the work that's already
been happening.

## Where I might be wrong

- I read headers and design docs more than I read implementations. The
  `SyncCoordinator`/`SyncWorker` algorithm complexity, the actual cost
  of the "three async patterns coexist" critique in practice, and the
  real performance characteristics of `SyncStore` are things you'd
  know better from running it.
- I have not verified that the blob engine has been driven end-to-end
  against a real Palm dataset; if it has, the blob layer is more
  mature than I'm crediting.
- I treat "consumer ergonomics" as a primary concern. If the library
  is intended to stay a two-consumer library forever, some of the
  "library should do this for the host" critiques don't apply with
  the same force — the host code is the same code as the library code,
  just on the other side of a CMake target boundary.
