# RFC: recurrenceCapabilities() retirement

**Date:** 2026-06-10
**From:** libkalburator (WP-C6)
**To:** PlanStan maintainer
**Status:** ⚠RFC — single external caller; requires PlanStan migration first

---

## What this is

`SyncBackend::recurrenceCapabilities()` (syncbackend.h:267) is a virtual method
that returns a `RecurrenceCapabilities` struct. It has exactly **one caller**
anywhere in the codebase:

```
PlanStan/src/dialogs/incidenceeditordialog.cpp:588
    m_recurrenceModule->setBackendCapabilities(backend->recurrenceCapabilities());
```

The lib implementation lives in two places:
- `src/calendar/syncbackend.cpp` — base default (returns an empty struct)
- `src/calendar/orgbackend.cpp:48` — the only non-trivial override

`BackendCapabilities` (syncbackend.h:264) already covers the general capability
surface. `RecurrenceCapabilities` is a separate struct that predates the unified
capability model and was never folded in.

## Proposed action sequence

1. **PlanStan: replace the one call site** — `incidenceeditordialog.cpp:588`:
   ```cpp
   // Before:
   m_recurrenceModule->setBackendCapabilities(backend->recurrenceCapabilities());
   
   // After (one option):
   // Check backend->capabilities().recurrenceFlags or equivalent.
   // If RecurrenceCapabilities is just a capability flag struct, inline the
   // struct construction from capabilities() — or pass a default directly if
   // the dialog only needs a yes/no gate.
   ```
   The exact migration depends on what fields `RecurrenceCapabilities` exposes
   and whether `incidenceeditordialog` actually reads them; a PlanStan-side
   grep will surface the shape.

2. **lib: remove `recurrenceCapabilities()` declaration** from `syncbackend.h:267`
   and both implementations (`syncbackend.cpp` default + `orgbackend.cpp:48`).

## Do not land until

- PlanStan confirms `incidenceeditordialog.cpp:588` is migrated and compiles
- PlanStan ctest baseline unchanged
- lib `ctest --test-dir build -j8` still 137/137

## Notes

- `RecurrenceCapabilities` struct itself can be removed from `syncbackend.h`
  once the method is gone (if it has no other users; grep before deleting).
- This is a one-commit change on both sides once the migration is agreed.
