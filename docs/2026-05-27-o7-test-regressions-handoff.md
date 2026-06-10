# Handoff: two test regressions on `main` (O7 ShapeRegistries removal) — fix before v0.57

> **CLOSED (2026-05-27):** Both regressions fixed; `tst_note_shapes` now registers the
> canonical catalogue. Resolved before the v0.57 tag. See MEMORY note `tst-note-shapes-preexisting-fail`.

**Date:** 2026-05-27
**From:** PlanStan (reviewing the merged sync-topology Phase 1 work)
**To:** libkalburator maintainer
**Severity:** Medium — two deterministic test failures on `main`. **Not** caused by the
sync-topology Phase 1 commits (see attribution). Should be green before the next tag.

## Summary

After the sync-topology Phase 1 work merged (commits `347d065..48bed00`), the full
libkalburator suite is **126/128**. Two tests fail, **deterministically** (not flaky):

| # | Test | Result |
|---|------|--------|
| 85 | `tst_note_shapes` | Subprocess aborted (QFATAL) |
| 100 | `tst_providerlifecycle` | Failed (assertion) |

## The failures (verbatim)

**`tst_note_shapes` — `compilesMarkdownToCanon()`**
```
QFATAL : ASSERT failure in TransformationRegistry::registerEdge: "from-shape not registered",
         src/shape/transformationregistry.cpp:75
  buildRegistry()            tests/note/tst_note_shapes.cpp:21
  compilesMarkdownToCanon()  tests/note/tst_note_shapes.cpp:27
```
The test's `buildRegistry()` calls `registerEdge` for an edge whose **from-shape was never
registered** — i.e. the shape-registration the test relies on no longer happens (or happens in
the wrong order / against a different registry instance).

**`tst_providerlifecycle` — `provisionProvider_backendsReadyEmittedAfterConnectAll()`**
```
FAIL!  : 'ready.count() >= 1' returned FALSE
```
After `connectAll()`, the `backendsReady` signal is never emitted (spy count 0) — consistent
with provider backends failing to construct when no `ShapeRegistries` are injected.

## Attribution: NOT the sync-topology Phase 1 work — strongly points to O7

- The Phase 1 commits (`347d065^..48bed00`) touched only: `CMakeLists.txt`,
  `src/types/logicalcalendar.h`, `src/sync/syncmappinggenerator.{h,cpp}` (new),
  `src/calendar/logicalcalendarbuilder.cpp`, `src/engine/syncengine.cpp`, and the four new
  test files. **They do not touch `src/note/`, `src/shape/`, or `src/sync/providerlifecycle.cpp`.**
  Both failures are runtime (the binaries compile and run), so the additive `LogicalCalendar`
  `domain` field cannot be the cause.
- Both tests **pass at the `v0.56-o15-converged` tag** (they shipped green in that release) and
  fail only on post-tag `main`.
- The post-tag, pre-Phase-1 commits are the autofill merge (`d310e5d`), the O13 merge
  (`0f38d50`), and the **O7 ShapeRegistries removal** (`7e11b7b` "remove Ambient-Context
  scaffolding; inject ShapeRegistries only" + `d8946b6`). `tst_note_shapes`' failure is a
  **shape-registry** assertion — squarely O7's surface area; autofill/O13 are unrelated to
  shape registration. The provider-lifecycle failure (backends not ready after `connectAll`)
  is also consistent with O7 if backend construction now requires injected `ShapeRegistries`
  the test no longer provides.

## Likely fix

The two tests almost certainly construct their registries / providers the **pre-O7 (ambient)**
way and need updating to the **injected `ShapeRegistries`** model:
- `tst_note_shapes::buildRegistry()` (`tests/note/tst_note_shapes.cpp:21`) must register the
  from-shape (and any peer/canon shapes) into the same registry instance **before** calling
  `registerEdge`, now that there is no ambient default registry to fall back on.
- `tst_providerlifecycle` must inject the `ShapeRegistries` the providers/backends need so
  `connectAll()` can construct backends and emit `backendsReady` (or the production
  `ProviderLifecycle`/`connectAll` path needs the same injection if this reflects a real gap,
  not just a test gap).

If you want certainty on attribution, the two commits to bisect are `7e11b7b` / `d8946b6`
(both tests should pass at `7e11b7b^`).

## Why it matters now

These don't affect the sync-topology Phase 1 review (that work is correct and its four new
tests pass). But they should be **fixed before cutting the v0.57 tag** — that tag is what
PlanStan's Phase 2 adoption (and the WildPalms handoff) will pin to, and a green baseline
matters for downstream validation.

## Repro

```
cmake --build build -j8
ctest --test-dir build -R "tst_note_shapes|tst_providerlifecycle" --output-on-failure
# or directly:
./build/tests/note/tst_note_shapes
./build/tests/sync/tst_providerlifecycle
```
