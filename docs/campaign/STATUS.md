# Campaign STATUS — canon-upgrade / convergence

**Status: CAMPAIGN CONVERGED — Plans 1–4 all complete and committed.** The two parallel
record-conversion mechanisms are now one: `src/transcoding/` is **deleted** and the shape
graph is the sole mechanism (invariant 1 satisfied). Plan 3 (canon encodings, 13 tasks)
landed the three rich canons (`contacts+canon`/`todo+canon`/`calendar+canon`) with the legacy
text encodings demoted to lossy peers. Plan 4 (calendar convergence,
`docs/2026-05-24-plan-4-calendar-convergence.md`, Tasks 1–9 incl. an added Task 6b) then:
relocated the (non-transcoding) `incidencediff`/`syncdiff` engines to `src/diff/` (T1);
re-homed org-mode RRULE simplification as the `canon ↔ org-ical` shape edge — `Simplified`
loss, original kept verbatim in `X-ORIGINAL-RRULE` (T2–T3); re-sourced the lossy-sync warning
from the demotion pipeline's composed `LossProfile` (T4); dropped the `TranscodingPlan`
parameter from the engine, writer, sync items, and every backend `pushItems`/`startSync`
(T5/T6); converged `CalendarManager`'s direct-write path off the registry (T6b); and deleted
the `TranscodingRegistry`/`Router`/`Plan`/`RRuleTranscoder`/`PropertyTranscoder` machinery (T7/T8).
Full suite: **111/112 green** (the lone failure is the pre-existing `tst_providerlifecycle`
async flake, `b395e5b`, unrelated — passes in isolation).
**Branch:** `feature/canon-upgrade-convergence` (off `main`), **fully pushed** — `origin` is at
HEAD (`435d7d3`), 0 commits ahead. All four plans landed and pushed.
**Last updated:** 2026-05-24.

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
| 2 | Per-engine registries (inject a `ShapeRegistries` bundle — `Transformation`+`Domain`+`DomainOperations` — into `SyncEngine` **and** `PluginManager`; `::instance()` delegates to a documented Ambient-Context default; remove test `clear()` rituals) | `docs/2026-05-23-plan-2-per-engine-registries.md` | **Complete** |
| 3 | Canon encodings (`contacts+canon`/`todo+canon`/`calendar+canon`: catalogues, JSON (de)serialization stages, bridge edges, reusable canon-JSON differ/merger) | `docs/2026-05-24-plan-3-canon-encodings.md` | **Complete** (13 tasks, committed 2026-05-24) |
| 4 | Calendar **convergence only** (retire `src/transcoding/`; RRULE-as-edge `canon → org-ical` Simplified loss; remove `ApplyContext.transcodingPlan` + `CalendarPluginWriter` special-casing). The `calendar+canon` encoding + `ical↔canon` bridges are landed by Plan 3, so Plan 4 only converges the live path. | `docs/2026-05-24-plan-4-calendar-convergence.md` | **Complete** (9 tasks + Task 6b, committed 2026-05-24; `src/transcoding/` deleted) |

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
- **(Plan 2 landed)** The three shape registries (`TransformationRegistry`/`DomainRegistry`/
  `DomainOperationsRegistry`) are no longer cleared as process-wide singletons in tests: each
  test now owns a `ShapeRegistries` fixture member and injects it. The `::instance()` accessors
  remain as documented Ambient-Context scaffolding bound to `defaultShapeRegistries()` (removal
  deferred to the downstream port, FINDINGS O7). `TranscodingRegistry` is a *different* registry,
  still a process-wide singleton; `tst_calendar_transcoding_warning.cpp` still calls
  `TranscodingRegistry::instance().clear()` in `cleanup()` (out of Plan 2 scope, retired in Plan 4).

## Next action

- **The campaign is complete.** All four plans landed; the shape graph is the sole
  record-transformation mechanism. The remaining work is **downstream / out-of-campaign**:
  1. **Push Plan 4** (`git push origin feature/canon-upgrade-convergence`) and decide on the
     merge to `main`.
  2. **Downstream backend port (FINDINGS O7 + O12):** PlanStan and WildPalms must adopt the
     injecting `ShapeRegistries` ctor (O7) **and** drop the `TranscodingPlan` param from their
     `SyncBackend` subclass overrides (O12) before they compile against this branch. Then the
     Ambient-Context `defaultShapeRegistries()` scaffolding (O7) can be deleted.
  3. **Org-on wiring (FINDINGS O12, invariant 8):** wire `OrgBackend` (built only with
     `KALBURATOR_HAVE_ORG_IO=ON`) to declare `{calendar, org-ical}` as its shape so the engine
     routes `canon → org-ical` (RRULE simplification via the edge). The edge + warning are landed;
     only the backend's `nativeShapes()`/`shapeFor()` wiring + a PlanStan org-sync ctest remain.
  4. **Pre-existing flake (FINDINGS O9):** `tst_providerlifecycle` async timing — investigate
     independently; it is not a campaign regression.
  5. **`note` domain (new, post-convergence; FINDINGS O4):** WildPalms requested a plaintext-carrier
     domain (`note`) with a `(note, markdown)` peer and a Markdown-file sink, to put Palm memos on the
     shape graph. Design approved 2026-05-25 → `docs/2026-05-25-note-domain-design.md` (stratum 1:
     body-as-verbatim-string, no AST; `document` name reserved for a future conversion domain).
     Additive; not urgent relative to the `main` merge. Implementation plan pending.
- **Push the branch** once Plan 4 is outlined or before ending the session:
  `git push -u origin feature/canon-upgrade-convergence`.
- **FINDINGS O7 stays OPEN (not Plan 4 scope unless convenient):** removing the
  Ambient-Context `defaultShapeRegistries()` default and the transitional ctor overloads
  is downstream-port work — PlanStan and WildPalms must first adopt the injecting ctors.
