# Phase 0 — Open questions  **[RESOLVED 2026-04-20]**

All 10 questions resolved by maintainer on 2026-04-20, accepting the
proposed defaults as authoritative. This document is now the
record-of-decisions; Phase 1 is unblocked.

---

## 1. `ICalendarSyncCoordinator` vs `BlobSyncEngine`: inherit, compose, or independent?

**RESOLVED: Compose.** `ICalendarSyncCoordinator` holds a
`BlobSyncEngine*` internally and delegates lower-layer transport to
it. Inheritance would leak lower-layer details into the
calendar-typed public API; independence would duplicate transport
logic.

## 2. Namespace

**RESOLVED: `Kalburator::Sync::*`.** Future modules use sibling
namespaces under `Kalburator::*` (e.g., `Kalburator::Transport::*`).

## 3. DecSync layer

**RESOLVED: keep calendar-typed for Phase 1–3.** Don't re-shape
PlanStan's existing code. Phase 4+ can reconsider if Wild Palms asks
for DecSync-for-contacts. **Also deferred per maintainer:** any
DecSync re-layering is explicitly post-Phase-4.

## 4. Per-backend vs per-mapping ConflictHandler registration

**RESOLVED: Per-backend handler registration with global fallback.**
API:

```cpp
coordinator->registerConflictHandler(backendId, handler);
coordinator->setDefaultConflictHandler(fallbackHandler);
```

## 5. `ICalendarCollection` surface — exact method list

**RESOLVED: Phase 1 kickoff task.** First concrete action in Phase 1
is to grep `libs/sync/` for `collection->*` calls and reconcile
against the `ICalendarCollection` draft in
`04-merged-interface-sketch.md`. The draft's ~12 methods are a
starting point, not a final list.

## 6. Wild Palms contacts / memos — library or host?

**RESOLVED: Deferred past Phase 4.** Phase 1 ships blob + calendar
layers. Contacts and memos are Wild Palms' responsibility for now;
if cross-app demand appears later, contacts/memos backends can be
upstreamed to libkalburator as a Phase 5+ addition. Per maintainer:
"eventual contact and memo synching" is explicitly deferred.

## 7. Syncthing module — same repo or sibling?

**RESOLVED: Same repo, optional target.** `Kalburator::Syncthing`
target gated by `-DKALBURATOR_SYNCTHING=ON`. Per maintainer:
Syncthing integration itself is deferred past Phase 4 work — the
target may not actually be populated until then, but the
architectural slot is reserved.

## 8. License final call

**RESOLVED: LGPL-3.0-only.** Compatible with both host projects
(GPLv3), allows commercial/closed-source consumers to link, follows
KF6/Qt6 convention. Phase 1 creates `LICENSES/LGPL-3.0-only.txt`
and adds SPDX headers to all source files.

## 9. Public forge / KDE Invent / Codeberg / GitHub

**RESOLVED: Deferred to Phase 4.** Stays at `~/dev/libkalburator/`
local repo through Phases 1–3.

## 10. "Kalburator" — spelling / branding preference

**RESOLVED:**
- Directory + repo name: `libkalburator` (lowercase, no hyphen)
- Namespace: `Kalburator::Sync::*`
- CMake target alias: `Kalburator::Sync`
- Library file: `libKalburatorSync.so` (KDE-style PascalCase in
  file names, following `libKF6CalendarCore.so` convention)

---

## Resolution summary

All 10 questions resolved with proposed defaults. Phase 0 is frozen
as of this commit. Phase 1 proceeds against
`04-merged-interface-sketch.md` and `05-repo-strategy.md` as
authoritative specs.

## Explicit deferral list (per maintainer)

These are confirmed out-of-scope for Phases 1–4 and will be
revisited only on demand:

- **DecSync re-layering** — stays calendar-typed.
- **Syncthing module population** — target reserved, content deferred.
- **Contacts / memos upstream** — stays Wild-Palms-internal until
  second consumer appears.
