# Phase Ia — vCard 4.0 canonical + WP palm-shape pivot (status)

**Status:** in progress (started 2026-05-07)
**Tag (planned):** `v0.26-phase-ia-vcard4-canonical`
**Spec:** `~/dev/refactor-engine-merger/2026-05-07-phase-i-vcard-pressure-test-design.md`
**Plan:** `~/dev/refactor-engine-merger/2026-05-07-phase-ia-vcard4-canonical-plan.md`

## What exists

(filled in as tasks land)

## What remains

(initial: every task)

## Discoveries

### Engine shape resolution for `IBlobBackend`-only backends (Task 1, 2026-05-07)

**Question.** `WildPalms::ContactsPlugin::ContactsBlobBackend` inherits
only from `Kalburator::Sync::IBlobBackend` (no QObject SyncBackend
base, no `nativeShapes()`/`shapeFor()`). How does the engine learn its
`Shape`, and where would Task 17 plumb a new shape declaration after
the read/write paths are pivoted to palm-native bytes?

**Answer.** The engine never asks `IBlobBackend` for a shape. Every
`IBlobBackend` produced by an `IBackendPluginV2` is wrapped at
registration time in a `SyncBackend`-derived adapter that hardcodes
the shape. The wrapper is the channel; the plugin's `IBlobBackend`
itself plays no part in shape resolution today.

Evidence (file:line, refactor-branch worktrees):

1. **The engine reads `nativeShapes()` only on `SyncBackend*`.**
   In `libkalburator/src/engine/syncengine.cpp:1444-1464`,
   `SyncEngineWorker::processSync` calls
   `m_controller->backendById(...)` (returns `SyncBackend*`) and
   reads `src->nativeShapes().first().domain` to decide whether to
   route through the calendar pipeline or `dispatchBlobSync`.
   `IBlobBackend` is never queried for shape — it has no such
   method (`libkalburator/src/blob/iblobbackend.h:36-70`).

2. **`SyncBackend::shapeFor(...)` falls back to `nativeShapes()[0]`.**
   `libkalburator/src/calendar/syncbackend.cpp:101-107`. Default
   `Shape::Any()` if `nativeShapes()` is empty.

3. **`PalmRuntime` wraps every plugin's `IBlobBackend` in
   `BlobBackendAdapter`.** The adapter is a private SyncBackend
   subclass declared in `WildPalms/src/runtime/palmruntime.cpp:56-128`
   and used at `palmruntime.cpp:259-276`:
   ```
   auto backend = v2->createPalmBackend(m_device.get());          // IBlobBackend
   auto adapter = std::make_unique<BlobBackendAdapter>(            // SyncBackend
       std::move(backend), id);
   m_registry->registerBackendInstance(id, adapter.get());
   ```
   The adapter forwards every `IBlobBackend` method to the wrapped
   blob and, critically, declares:
   ```cpp
   QList<Kalburator::Shape::Shape> nativeShapes() const override {
       return {{ DomainId{"blob"}, EncodingId{"blob"} }};
   }
   ```
   (`palmruntime.cpp:69-72`). Every Palm plugin backend therefore
   reports shape `(blob, blob)` to the engine — including
   `ContactsBlobBackend`, `CalendarBlobBackend`, memo/todo, etc.

4. **`BlobDomainAdapter` keys baselines under the same shape.**
   `libkalburator/src/blob/blobdomainadapter.cpp:442-447` defines
   `kBlobShape = (blob, raw)` for canonical-record persistence in
   `BlobBaselineStore`. Diffing is by `contentHash` only
   (blobdomainadapter.cpp:160-191), so the engine never inspects
   bytes.

**Open questions, answered:**

1. *Wrapper or `BackendRegistry::registerShape`?* Wrapper. There is
   no shape-registration API on `BackendRegistry`
   (`libkalburator/src/calendar/backendregistry.{h,cpp}`); it stores
   `SyncBackend*` instances by id. Shape comes from the wrapper.

2. *Where is `(contacts, vcard)` asserted today?* **Nowhere.** The
   live `ContactsBlobBackend` does not declare any shape. Its
   `BackendRecord.type = "text/vcard"` at
   `WildPalms/src/plugins/contacts/contactsblobbackend.cpp:124,146`
   is a free-text MIME hint never read by the shape system. To the
   engine, `palm-contacts` is a `(blob, blob)` backend like every
   other plugin-managed backend.

   There is a stale `WildPalms::PalmContacts::PalmContactsBackend`
   at `WildPalms/src/palm/contacts/palmcontactsbackend.{h,cpp}`
   that *does* declare `nativeShapes() = (contacts, palm-address)`
   (palmcontactsbackend.cpp:35-39), but it is dead code — `grep -r
   PalmContactsBackend WildPalms/` returns only its own h/cpp. It
   appears to be the "libkalburator palm-address placeholder"
   referenced in plan Task 12.

3. *Cleanest channel for Task 17 to declare `(contacts, palm)`?*
   `BlobBackendAdapter::nativeShapes()` is the wrapper that owns
   shape declaration today, and it hardcodes `(blob, blob)` for
   every plugin. Two viable options surface from this audit; both
   keep `ContactsBlobBackend` itself an `IBlobBackend` (no QObject
   base widening):

   - **Option A (extend `IBackendPluginV2`):** add
     `virtual QList<Shape> nativeShapesFor(IBlobBackend*)` (or
     similar) to the plugin interface. `BlobBackendAdapter`'s
     constructor consults the plugin and stores the result; its
     `nativeShapes()` returns the stored value instead of the
     hardcoded `(blob, blob)`. This is plugin-private knowledge
     surfacing through the plugin entry point.

   - **Option B (per-collection map on the adapter):** if shape
     varies per collection (Palm category slot), `BlobBackendAdapter`
     gains a setter the plugin calls during `createPalmBackend`
     setup; or shape is encoded in `CollectionInfo` and the
     adapter derives `shapeFor(collectionId)` from
     `m_blob->collectionInfo(id)`. The engine already calls
     `shapeFor(collectionId)` (syncbackend.cpp:101-107), so a
     per-collection answer is supported by existing engine
     contracts.

   Option A is simpler if every Palm contacts collection shares
   one shape; Option B is needed only if shape varies per category
   slot. The plan's Task 13 ("Audit domain-extension mechanism —
   path A vs B") looks like it's framed around this same fork.

**Implication for Tasks 16-17.** Changing `ContactsBlobBackend` to
emit/consume palm-native bytes is *not* purely a byte-content swap —
the engine currently sees `(blob, blob)` for it, which means there
is no transcoding edge to `(contacts, vcard4)` and no transformation
the engine could apply. Either the wrapper must be taught to
declare `(contacts, palm)` (so `TransformationRegistry::inspect`
finds an edge), or the consumer side of the mapping (rawfiles PC
backend) must also be `(blob, blob)` and the byte-format
contract becomes "both sides exchange palm-native bytes" — which
defeats the point of canonicalising on vCard 4.0. The wrapper
extension (Option A or B) is therefore on the critical path for
Task 17.

### `PalmRecord` wire-bytes API (Task 11, 2026-05-07)

`PalmRecord` (in
`WildPalms/src/palm/sync/palmrecord.h`) had no serialization API
when Task 11 began — only the POD struct + a defaulted
`operator==`. The plan offered two choices: (a) add the wire-bytes
pair in this task, or (b) defer to Task 13's audit. Path (a) was
taken because `PalmRecord` is internally trivial (5 fields, all
serializable as-is via QDataStream).

**Format chosen:** `QDataStream` at `Qt_6_0` version, writing
`recordId / category / attributes / data / lastModified` in that
order. The methods are inline on the struct (`toWireBytes()` and
static `fromWireBytes()`); no `palmrecord.cpp` was added. These
bytes are an *internal* contract between WP's
`ContactsBlobBackend` and the palm↔vcard4 `TransformationStage`
pair — not an on-disk format. `fromWireBytes` returns a
default-constructed record on truncated/garbage input rather than
throwing.

**Test coverage:** `WildPalms/tests/palmsync/tst_palmrecord_wirebytes.cpp`,
8 sub-tests including default round-trip, populated round-trip,
attribute flags (Dirty + Secret), empty data, empty input, and
truncated-input safety. All green.

**Implication for Tasks 16-17.** When `ContactsBlobBackend` is
re-pointed to emit `(contacts, palm)` shape, the bytes it hands
the transformation pipeline are `PalmRecord::toWireBytes()`. The
PC-side consumer (after the transformation chain runs) gets vCard 4
bytes. Symmetric on the inbound write path: the engine hands the
backend `PalmRecord::toWireBytes()` (after vcard4→palm) and the
backend reverses it via `fromWireBytes` before talking to the
device.

### Domain-extension mechanism (Task 13, 2026-05-07)

**Question.** Per design §3.3, WP needs to register the
`(contacts, palm)` peer shape + edges to/from the contacts canonical
without owning the `contacts` domain (the stock `KalburatorDomainContacts`
plugin owns that). Three paths were on the table:

  (A) Direct registration with `TransformationRegistry::registerShape`
      / `registerEdge` from a non-`DomainPlugin` TU.
  (B) A new "domain-extension" plugin form — multi-plugin per domain
      in `DomainRegistry`, edges merged at `initialize()`-time.
  (C) A callback hook on `KalburatorDomainContacts`.

**Path chosen: (A) — direct `TransformationRegistry` registration
from a WP-side helper class.**

**Rationale (verified against the .cpp, not just header comments):**

1. **No ownership check on `TransformationRegistry`.**
   `registerShape` (`libkalburator/src/shape/transformationregistry.cpp:12-18`)
   and `registerEdge`
   (`libkalburator/src/shape/transformationregistry.cpp:54-80`) accept
   any `Shape`/`TransformationEdge` regardless of caller. Neither
   references `DomainPlugin` or any owner identity. The only gate is
   `m_frozenDomains` membership (transformationregistry.cpp:13, 58-59).

2. **Idempotency confirmed in source.** `registerEdge` at
   `transformationregistry.cpp:70-78` looks up an existing edge for
   the `(from, to)` pair and, if found, asserts only on conflicting
   `loss.level`/`loss.dropped`; identical re-registration returns
   silently. `registerShape` at lines 17 unconditionally overwrites
   the catalogue (last-write-wins idempotent, not conflict-detecting —
   so two callers with different catalogues for the same shape would
   silently disagree, but that's a non-issue here: only WP registers
   `(contacts, palm)`'s catalogue). `declareCanonical` at lines 25-34
   detects same-value vs. conflicting redeclaration and rejects the
   conflict — but WP never declares contacts canonical, so this is
   moot for path A.

3. **Freeze timing is safe for WP's plugin-load sequence.**
   `freeze()` (`transformationregistry.cpp:49-52`) is called only
   from `compile()` (lines 139-142) on a non-identity successful
   pipeline. WP plugins load via `KPluginFactory` at app startup,
   long before any sync compiles a pipeline. The first compile for
   the contacts domain happens when a sync actually runs against a
   palm-contacts mapping — after WP's plugin TU has registered.
   The freeze-after-first-compile gate is therefore satisfied
   structurally, not by accident of timing.

4. **`isFrozen()` is public** (`transformationregistry.h:44-49`),
   so the WP helper can defensively assert / log if it ever runs
   after freeze (defense in depth, not a workaround).

5. **Path B's mechanism already exists in skeleton form, but A
   subsumes it.** `DomainRegistry::registerPlugin`
   (`libkalburator/src/shape/domainregistry.cpp:41-59`) already
   supports post-`initialize()` plugin registration with
   idempotent edge re-registration. So path B's "register a second
   plugin for an existing domain" works today *if* the second
   thing is a `DomainPlugin`. But that buys nothing path A doesn't
   already get — and forces WP to construct a `DomainPlugin`
   subclass with mostly stub methods (`canonicalShape()`,
   `canonicalCatalogue()`, `createCanonicalDiffer()`,
   `createCanonicalMerger()`) it doesn't actually own. Path A skips
   the boilerplate.

6. **The test suite confirms idempotency at the
   `DomainRegistry::initialize` level too** —
   `tst_domain_registry.cpp:65-80` verifies `initialize()` is a
   no-op on second call. Path A doesn't depend on this, but it
   means path B would have been workable too if needed.

**Implication for Task 14.** The `ContactsDomainExtension` (or
whatever it's named) is **NOT** a `DomainPlugin` subclass. It is a
plain helper class with a static
`registerWith(TransformationRegistry&)` method (or equivalent
free function) that:

  1. Calls `r.registerShape({DomainId{"contacts"}, EncodingId{"palm"}}, palmCatalogue)`.
  2. Calls `r.registerEdge(...)` for `(contacts, palm) → (contacts, vcard4)` and the reverse.
  3. Is invoked from WP's plugin TU at plugin-load time (probably
     from the `IBackendPluginV2` factory's `init()` or equivalent
     entry point), before any sync runs.

It does **not** call `DomainRegistry::registerDomain` or
`registerPlugin`. The contacts domain remains owned by the stock
`KalburatorDomainContacts` plugin; WP only contributes a peer
shape and its two transcoding edges to the canonical hub.

**Naming note carried from Task 12.** The stale
`WildPalms::PalmContacts::PalmContactsBackend` declares
`(contacts, palm-address)` (with hyphen) at
`palmcontactsbackend.cpp:35-39`. Task 14 should use `palm` (no
hyphen) per the design doc, matching the EncodingId convention
established by `(calendar, ical)`, `(contacts, vcard4)`, etc.
The hyphenated dead code can be removed in a later cleanup task or
left to die when its files are eventually deleted; it is not
referenced by any build target (Task 1's audit confirmed this).

**Revisit trigger.** If a future phase finds path (A) too loose —
e.g., WP and a hypothetical second extender both want to add peer
shapes for the same domain and register conflicting edges — lift
to path (B) by formalizing a `DomainExtensionPlugin` interface
that `DomainRegistry` understands as distinct from canonical-owner
plugins. Today, with one extender (WP) and one extension point
(`(contacts, palm)`), that's overkill.
