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

### Static-link visibility: libkalburator domain registrars don't reach .so plugins (Task 15, 2026-05-08)

**Symptom:** When `wildpalms_contacts_v2.so` is loaded via `KPluginFactory`,
calling `Kalburator::Shape::DomainRegistry::initialize(...)` from inside the
`.so` finds an empty registry. The libkalburator contacts plugin's
static-init registrar (`s_contactsPluginRegistrar` in
`libkalburator/src/contacts/contactsdomainplugin.cpp:99`) is never linked
into the `.so` because no symbol in `contactsdomainplugin.cpp` is
referenced from the `.so`'s code, and ELF static-archive linking drops
unreferenced TUs by default.

**Workaround applied in Task 15:** `ContactsDomainExtension::registerWith`
now defensively calls `registerShape(vcard4, {})` (empty placeholder
catalogue) when `catalogueFor(vcard4)` is null, so the palm↔vcard4 edge
registrations don't trip the "to-shape not registered" assert. When
libkalburator's `registerEdges` does eventually run (in code paths that
DO see the plugin), `registerShape` is idempotent and overwrites the
placeholder with the real catalogue.

**Why it matters:** The static-init registrar pattern silently fails
inside any `.so` plugin module that links libkalburator as a static
archive. This affects ALL four stock domain plugins (calendar, contacts,
memo, todo), not just contacts. Memo and todo WP plugins likely have the
same silent gap; nothing currently asserts on it because their backends
emit `(blob, blob)` shapes (per Task 1's audit) and never trigger
edge-routing through their domain plugin's edges.

**Proper fix (deferred):** One of:
- `target_link_libraries(wildpalms_contacts_v2 PRIVATE -Wl,--whole-archive Kalburator::Sync -Wl,--no-whole-archive)` — pulls the registrar into the `.so`. Cross-platform variant: use `LINK_INTERFACE_LIBRARIES_DIRECT` or wrap with `$<LINK_LIBRARY:WHOLE_ARCHIVE,...>`.
- Move the static-init registrar's `registerDomain(...)` call into a referenced symbol path (e.g., a function the plugin's loader explicitly calls).
- Build libkalburator's domain plugins as separate shared libraries that the WP `.so` depends on dynamically.

**Action:** Defer to a post-Phase-Ia phase. Phase Ia's Task 19 integration
test will route through `(contacts, palm) → (contacts, vcard4)` edges
that ARE registered (because Task 15's placeholder makes vcard4 known),
so Phase Ia's pressure-test still proves what it's supposed to prove.
The finding gets a full FINDINGS.md entry in Task 21.

### Engine does not invoke registered TransformationStage at the edge (Task 19, 2026-05-08)

**Symptom:** With both source and target backends declaring distinct
shapes for the same domain (`(contacts, palm)` source,
`(contacts, vcard4)` target) and a registered palm↔vcard4 edge in the
process-wide `TransformationRegistry`, the bytes pushed to the target
are byte-identical to the source bytes. The target receives PalmRecord
wire-bytes — NOT the vCard 4.0 representation that
`PalmToVCardStage::transform` would produce.

**Root cause:** `SyncEngineWorker::dispatchBlobSync`
(`src/engine/syncengine.cpp:1775-1926`) hard-wires the read→diff→write
pipeline:

```cpp
sourceRecords = srcBlob->loadRecords(srcColId);   // raw bytes
...
const EngineDiff = adapter.diff(...);              // hash-equality diff
const EngineMerge = adapter.merge(...);
for each rec in toWrite:
    tgtBlob->createRecord(tgtColId, rec);          // raw bytes, no transform
```

There is no call to `TransformationRegistry::compile(srcShape, tgtShape)`,
no `Pipeline::run(bytes)`. The registry IS consulted — but only to compute
the `LossProfile` passed to `ISyncHost::syncStarted` (see
`syncengine.cpp:1441-1451`). Loss profile is observability metadata, not
an actuator.

**Diagnostic test (landed):** `WildPalms/tests/plugins/contacts/
tst_contacts_palm_engine_sync.cpp` pins this gap. It:
1. Builds two `SyncBackend` subclasses with declared
   `(contacts, palm)` and `(contacts, vcard4)` `nativeShapes()`.
2. Registers `ContactsDomainExtension` so the palm↔vcard4 edge exists.
3. Drives `SyncEngine::runSyncFuture(OneWayUpload)`.
4. Asserts:
   - Host's `syncStarted` got a `Lossless` LossProfile (proves
     registry consultation: `palmToVCardLoss()` returns Lossless;
     the default for an unregistered path is also Lossless, but the
     assertion still pins that the engine reached this path).
   - Target's `createRecord` was called exactly once.
   - Target bytes are byte-equal to source bytes (the gap).
   - Target bytes do NOT contain `BEGIN:VCARD` or `VERSION:4.0` (the
     gap, with teeth).
   - Target bytes round-trip through `PalmRecord::fromWireBytes` —
     proving they're palm bytes, not transformed vCard.

When Phase Ib lands the engine-side fix (compile + run Pipeline at the
edge), the diagnostic assertions in this test flip: `BEGIN:VCARD` and
`VERSION:4.0` substrings WILL appear, and the round-trip-as-palm
assertion will fail. That flip is the load-bearing "fix landed" signal.

**Why this is the architectural finding the phase was designed to
deliver:** Task 19 was framed as the pressure-test that would surface
"any architectural assumption baked into the engine that hurts
contacts." The finding is that the engine has no concept of "transform
at the shape edge" at all — the assumption is that all sync is
identity-bytes, with the `BlobDomainAdapter` doing hash-equality diff.
This worked while every WP plugin emitted `(blob, blob)` (Task 1's
audit), because identity-bytes IS the right behaviour when source and
target shape are the same.

The pivot to `(contacts, palm)` vs `(contacts, vcard4)` (Phase Ia's
read-path pivot in Task 16) created the first cross-shape mapping in
production, exposing the gap. Today's WP UX hides this because the PC
side is also a blob-shaped mock; production users never see palm bytes
arrive at a "PC" backend that's actually a vcard4 store, because no
such store exists yet.

**Decision:** Land the diagnostic test as DONE_WITH_CONCERNS for Phase
Ia. The engine fix is the natural Phase Ib opening task. Forking it
into Phase Ia would have:
- Required substantial engine-thread work (`compile()` returns
  `std::optional<Pipeline>`; per-record run; threading model around
  the Pipeline's invocation; loss-policy enforcement
  (`Abort` / `Warn` / `Proceed` on lossy transforms); baseline-keying
  semantics (do we baseline source-shape bytes or canonical bytes?)).
- Potentially destabilized libkalburator's calendar test contract
  pinned by Phase D.0. The dispatchBlobSync path is shared with the
  calendar adapter's blob fallback path; an in-place change risks
  flipping calendar tests.

**Concrete Phase Ib backlog (forked from this finding):**
1. `SyncEngineWorker::dispatchBlobSync` — call
   `TransformationRegistry::compile(srcShape, tgtShape)` and apply the
   Pipeline to each `BackendRecord::data` before
   `tgtBlob->createRecord`/`updateRecord`. Pipeline::run is
   bytes-in-bytes-out, so this is a contained change.
2. Loss-policy enforcement: when Pipeline's composed LossProfile is
   non-Lossless and the mapping's `lossPolicy == Abort`, fail the
   sync with the LossProfile in the error message. When `Warn`, emit
   `transcodingWarning`. When `Proceed`, run silently.
3. Baseline keying: stored baselines on the target side are the
   transformed bytes (target shape). The diff at next sync compares
   target-shape bytes against target-shape baseline; the source side
   is independent. This requires a small `BlobBaselineStore`
   contract sharpening.
4. Bidirectional case (TwoWay): both directions need the inverse
   pipeline. `compile(tgtShape, srcShape)` + apply on `finalSource`.
5. Flip Task 19's diagnostic assertions and add a positive assertion
   that target bytes are valid vCard 4.0 (parses via KContacts).
