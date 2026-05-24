# Campaign STATUS — canon-upgrade / convergence

**Status:** Plan 1 implemented and committed (Tasks 1–7 complete). Plan 2 **design is locked**
(design §8 rewritten 2026-05-24 to the injected-`ShapeRegistries`-bundle topology); its task plan
is being written against the landed APIs. Plans 3–4 remain outlined (invariant P1).
**Branch:** `feature/canon-upgrade-convergence` (off `main`; not pushed).
**Last updated:** 2026-05-23.

> Living document. Update the Status line, the plan table, the Plan-1 task checklist,
> and the decision ledger **in the same commit** that changes any of them
> (invariant 7). Do not leave this saying "not started" after work has begun.

---

## The campaign in one paragraph

libkalburator has two parallel record-conversion subsystems; `src/transcoding/`
(backend-type-keyed, calendar-only, in practice just org-mode RRULE simplification)
must be retired into the shape graph (`src/shape/`). At the same time we upgrade the
calendar/contacts/todo canonical shapes from iCal/vCard/vtodo to rich JSON superset
encodings (capable of holding Google + Microsoft Graph richness), with iCal/vCard/
vtodo demoted to lossy peer encodings. The router is generalized from a single
canonical hub to a **versioned canonical spine** so future canon upgrades are
append-only and transparent to existing peers, and the loss model gains a four-kind
per-property taxonomy. memo stays on `(blob, raw)` (out of scope).

## Plan sequence

| # | Plan | Doc | Status |
|---|---|---|---|
| 1 | Shape-core foundations (four-kind loss model, versioned spine, synthetic v1→v2 fixture) | `docs/2026-05-23-plan-1-shape-core-foundations.md` | **Complete** |
| 2 | Per-engine registries (inject a `ShapeRegistries` bundle — `Transformation`+`Domain`+`DomainOperations` — into `SyncEngine` **and** `PluginManager`; `::instance()` delegates to a documented Ambient-Context default; remove test `clear()` rituals) | `docs/2026-05-23-plan-2-per-engine-registries.md` | **Design locked (§8 rewritten); plan being written** |
| 3 | Canon encodings (`calendar+canon`/`contacts+canon`/`todo+canon`: catalogues, JSON (de)serialization stages, bridge edges, differ/merger) | _not written_ | Outlined (schema doc) |
| 4 | Calendar convergence (retire `src/transcoding/`; RRULE-as-edge; remove `ApplyContext.transcodingPlan` + `CalendarPluginWriter` special-casing) | _not written_ | Outlined (design §7, §10) |

Plans 2–4 are deliberately outlines: their task code must be written against the
**landed** APIs of the prior plan, not guessed (invariant P1).

## Plan 1 task checklist (mirror — update as tasks land)

- [x] Task 1 — New `LossProfile` shape (kinds map + helpers)
- [x] Task 2 — Migrate all `LossProfile` construction & assertion sites
- [x] Task 3 — Spine storage + `appendCanonicalVersion` / `canonicalSpine`
- [x] Task 4 — Spine-aware `compileImpl` routing
- [x] Task 5 — Freeze guards a frozen spine against late version appends
- [x] Task 6 — Widening/narrowing fixture stages + auto-extension test
- [x] Task 7 — `v1 → v2 → v1` round-trip compatibility guard

## Locked decisions ledger

Confirmed with the human during planning. Changing one requires a documented
deviation (invariant scope note).

1. **Canon is its own JSON model**, keyed by `PropertyId`, values typed per
   `PropertyKind`, composites nested as `Json`. Not a clone of any vendor; not
   raw iCal/vCard. (schema §1.1)
2. **Recurrence stored as raw RFC5545 text**; parsed only at the `canon → Microsoft`
   edge; differ treats it as one opaque field. (design §4.0, invariant 3)
3. **Versioned canonical spine**, append-only upgrades; existing peer edges never
   rewritten. (design §5, invariant 2)
4. **Four-kind loss model** (Dropped/Simplified/Reversible/Degraded). (design §6, invariant 4)
5. **Per-engine registries** (Plan 2) via an injected `ShapeRegistries` bundle (the OSGi
   `BundleContext` model; finishes the DI pattern `BackendRegistry` already uses). Three `Shape::`
   registries fold in — incl. `DomainOperationsRegistry` (the §8 stub said two; corrected, FINDINGS
   O6). A process-global default bundle is kept as **documented Ambient-Context scaffolding** so
   downstream stays green (invariant 10); its removal is deferred to the downstream port (FINDINGS
   O7). `BackendRegistry` stays separate (non-goal). Until Plan 2 lands, tests `clear()` the
   singletons in `cleanup()`. (design §8)
6. **Coarse diff granularity** — one `PropertyId` per row; a change anywhere in a
   composite marks the whole property changed. No per-element (per-attendee) diffing in v1.
7. **Contacts `uid`** — canon mints/normalizes a stable id; vendor id → `providerExtras`.
8. **Provider-extras bag** — namespaced opaque carrier for vendor-only fields; carried
   verbatim; never a conflict axis. (schema §1.3)
9. **memo out of scope** — stays `(blob, raw)`.
10. **Scope boundary** (design §2): build foundations/canon/convergence; transparent
    third-party auto-bridge, capability objects, and load-time enforcement are
    design+synthetic-test only (invariant 8).

## Build / test reference

```bash
cmake -S /home/clinton/dev/libkalburator -B /home/clinton/dev/libkalburator/build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON   # configure once
cmake --build /home/clinton/dev/libkalburator/build --target <tst_name>                                              # build one test
ctest --test-dir /home/clinton/dev/libkalburator/build -R <tst_name> -VV                                             # run one test
ctest --test-dir /home/clinton/dev/libkalburator/build                                                               # full suite
```

Qt6 test gotchas (from repo `CLAUDE.md`, still in force):
- Use `QTRY_VERIFY_WITH_TIMEOUT(...)`, **not** `waitForFinished` (Qt6 doesn't spin the
  test event loop).
- Read futures via `future.resultAt(0)`, not `future.results()` (empty after cancel).
- `TransformationRegistry`/`DomainRegistry`/`TranscodingRegistry` are process-wide
  singletons → every test `cleanup()` calls `...::instance().clear()` until Plan 2.

## Next action

- **Plan 2 authoring (in progress):** design §8 is rewritten and locked; the task plan is being
  written via `superpowers:writing-plans` against the landed `ShapeRegistries`/`SyncEngine`/
  `PluginManager` signatures, saved to `docs/2026-05-23-plan-2-per-engine-registries.md`.
- **After Plan 2 lands:** write Plan 3 (canon encodings) against the then-real bundle APIs;
  remember FINDINGS O7 (remove the Ambient-Context default) is downstream-port work, not Plan 3.
