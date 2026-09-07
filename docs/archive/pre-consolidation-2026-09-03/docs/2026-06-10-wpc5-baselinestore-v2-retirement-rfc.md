# RFC: BaselineStore v2 method retirement

**Date:** 2026-06-10
**From:** libkalburator (WP-C5)
**To:** PlanStan maintainer
**Status:** ⚠RFC — lib cannot act unilaterally; requires PlanStan port first

---

## What this is

`src/storage/baselinestore.h` carries five `[[deprecated]]` v2 methods
(backend-keyed API, marked G.4):

| Method | Status |
|--------|--------|
| `setBaseline(backendId, recordId, hash)` | `[[deprecated]]` |
| `baseline(backendId, recordId)` | `[[deprecated]]` |
| `setMappingBaseline(backendId, recordId, newHash)` | `[[deprecated]]` |
| `baselinesForMapping(backendId)` | `[[deprecated]]` |
| `clearMapping(backendId)` | `[[deprecated]]` |

AUDIT G8 wrote "deprecated v2 surface stays until all callers migrate."
That condition is now effectively met: **0 production callers anywhere** —
only two test files still use the v2 API:

- **PlanStan** `tests/sync/tst_syncstore.cpp` — ~20 `setBaseline()` calls
- **PlanStan** `tests/backends/tst_decsynccontrollerstore.cpp` — ~7 calls
- **lib** `tests/storage/tst_baseline_store_per_record_keys.cpp` — lib self-test

## Proposed action sequence

1. **PlanStan: port `tst_syncstore.cpp`** — replace each v2 `setBaseline(backendId, col, uid, hash)` with `setBaselineV3(mappingId, recordId, hash)`. The mapping ID is whatever mapping key the engine would use for that backend/collection pair; the test fixture can invent a stable UUID.

2. **PlanStan: port `tst_decsynccontrollerstore.cpp`** — same substitution pattern.

3. **lib: delete `tst_baseline_store_per_record_keys.cpp`** once its coverage is superseded by the v3 test suite (already has `tst_baseline_store_v3.cpp`).

4. **lib: delete the 5 `[[deprecated]]` methods** from `baselinestore.{h,cpp}` and the v2→v3 migration path (`migrateV2ToV3()`, `setMappingResolver()`).

## v3 key shape (reference)

```cpp
// v3 key = mappingId + recordId (two-column PK in blob_baselines_v3 table)
bool setBaselineV3(const QString &mappingId,
                   const QString &recordId,
                   const QByteArray &hash);

QByteArray baselineV3(const QString &mappingId,
                      const QString &recordId) const;
```

The mapping ID comes from `SyncMapping::id` (the UUID the host assigns when
it creates a mapping). In PlanStan tests that previously used a backend name
as the key, pick any stable string (e.g. `QStringLiteral("test-mapping-uuid")`).

## Do not land until

- PlanStan confirms both test files are ported and their ctest suite is green
  without the deprecated surface
- lib self-test updated or deleted
- Full `ctest --test-dir build -j8` still 137/137

## Locked decision not to revisit

AUDIT G8 "do not delete until all callers migrate" is satisfied by this RFC.
The lib's `[[deprecated]]` annotation already generates compiler warnings on
any accidental new call; the surface is not harmful to keep until PlanStan
has bandwidth for the port.
