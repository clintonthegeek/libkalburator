# Akonadi/Org dark-coverage lane

**Decision:** WP-D10 (2026-06-10 audit supplement item 10)
**Status:** Lane documented; periodic run recommended

## Context

The default build profile (`KALBURATOR_HAVE_AKONADI=OFF`, `KALBURATOR_HAVE_ORG_IO=OFF`)
gates out 11 test sources covering ~2.7k LOC of backend code:

| Gate | Tests |
|------|-------|
| `KALBURATOR_HAVE_ORG_IO=ON` | tst_orgbackend_blob_view, tst_orgbackend, tst_orgbackend_external |
| `KALBURATOR_HAVE_AKONADI=ON` | tst_akonadibackend_blob_view, tst_akonadi_payload, tst_akonadibackend_live, tst_akonadicontactsbackend, tst_akonadiprovider, tst_akonadiprovider_plugin_registration (+ tst_akonadibackend_live which requires a running Akonadi server) |

These backends contribute **0 green tests** in the default CI lane.
The Akonadi backend is PlanStan's only production path to the system calendar.
The risk of silent breakage is non-trivial.

## Lane

A `build-akonadi/` directory already exists and last built clean on 2026-06-10.

### Configure

```bash
cmake -S . -B build-akonadi \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_BUILD_TYPE=Debug \
  -DKALBURATOR_HAVE_AKONADI=ON \
  -DKALBURATOR_HAVE_ORG_IO=ON
```

### Build

```bash
make -C build-akonadi -j8
```

### Run (excluding the live-server test)

```bash
ctest --test-dir build-akonadi --output-on-failure -j8 \
  -E tst_akonadibackend_live
```

`tst_akonadibackend_live` requires a running Akonadi server daemon; skip it
in headless/CI runs. All other gated tests are self-contained.

## Cadence

Run this lane:
- Before cutting any release tag
- After any change to `src/calendar/{akonadi,org}*`, `src/sync/akonadi*`,
  or `src/contacts/akonadi*`
- When PlanStan reports an Akonadi integration regression

No automated CI lane is wired up (the default profile is the CI baseline).
This is a manual verification step until a dedicated CI runner with Akonadi
is available.

## Watch item

See FINDINGS.md `[D1]` — the Akonadi/Org backends contribute 0 tests to the
default lane; breakage is silent unless this lane is run periodically.
